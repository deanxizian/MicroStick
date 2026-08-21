import CoreBluetooth
import Foundation
import MicroStickUsageCore

public final class UsageBluetoothClient: NSObject, @unchecked Sendable {
    public static let serviceUUID = CBUUID(string: "BE1E47D1-4C59-4DAB-80CF-E26202B981D8")
    public static let characteristicUUID = CBUUID(string: "27A7328B-193D-4961-9C85-CC44006E7E0D")

    public var onStateChange: (@Sendable (UsageBluetoothState) -> Void)?
    public var onDelivery: (@Sendable (Result<Void, Error>) -> Void)?
    /// Connection-stage diagnostics only. Messages never include Codex session
    /// paths, account data, or payload contents.
    public var onDiagnostic: (@Sendable (String) -> Void)?

    private let queue: DispatchQueue
    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var characteristic: CBCharacteristic?
    private var machine = UsageBluetoothStateMachine()
    private var deliveryGate: UsageDeliveryGate
    private var retryWork: DispatchWorkItem?
    private var latestPayload: Data?
    private var activePayload: Data?
    private var writeQueue: [Data] = []
    private var messageID: UInt8 = 0
    private var running = false

    public init(
        queue: DispatchQueue = DispatchQueue(label: "com.deanxizian.microstick.usage.bluetooth"),
        heartbeatInterval: TimeInterval = 5 * 60
    ) {
        self.queue = queue
        self.deliveryGate = UsageDeliveryGate(heartbeatInterval: heartbeatInterval)
        super.init()
    }

    public func start() {
        queue.async { [weak self] in
            guard let self, !self.running else { return }
            self.running = true
            self.central = CBCentralManager(delegate: self, queue: self.queue,
                                            options: [CBCentralManagerOptionShowPowerAlertKey: false])
        }
    }

    public func stop() {
        queue.async { [weak self] in
            guard let self else { return }
            self.running = false
            self.perform(self.machine.handle(.stop))
        }
    }

    public func update(_ snapshot: UsageSnapshot) {
        queue.async { [weak self] in
            guard let self else { return }
            do {
                self.latestPayload = try UsagePayloadCodec.encode(snapshot)
                self.attemptDelivery(now: Date())
            } catch {
                self.onDelivery?(.failure(error))
            }
        }
    }

    public func heartbeat(now: Date = Date()) {
        queue.async { [weak self] in self?.attemptDelivery(now: now) }
    }

    public func recoverAfterWake() {
        queue.async { [weak self] in
            guard let self, self.running else { return }
            self.deliveryGate.resetConnection()
            self.perform(self.machine.handle(.failed))
        }
    }

    private func transition(_ event: UsageBluetoothEvent) {
        perform(machine.handle(event))
        onStateChange?(machine.state)
    }

    private func perform(_ actions: [UsageBluetoothAction]) {
        for action in actions {
            switch action {
            case .startScan:
                retryWork?.cancel()
                central?.stopScan()
                /* ChatGPT may already own the physical BLE link. CoreBluetooth
                   can attach this client to that connected peripheral even when
                   the device has stopped advertising. */
                if let connected = central?.retrieveConnectedPeripherals(
                    withServices: [Self.serviceUUID]
                ).first {
                    peripheral = connected
                    connected.delegate = self
                    transition(.peripheralDiscovered)
                    continue
                }
                central?.scanForPeripherals(withServices: [Self.serviceUUID],
                                            options: [CBCentralManagerScanOptionAllowDuplicatesKey: false])
            case .connect:
                guard let peripheral else { transition(.failed); continue }
                central.stopScan()
                central.connect(peripheral, options: nil)
            case .discoverService:
                peripheral?.discoverServices([Self.serviceUUID])
            case .discoverCharacteristic:
                guard let service = peripheral?.services?.first(where: {
                    $0.uuid == Self.serviceUUID
                }) else { transition(.failed); continue }
                peripheral?.discoverCharacteristics([Self.characteristicUUID], for: service)
            case .becameReady:
                deliveryGate.resetConnection()
                attemptDelivery(now: Date())
            case let .scheduleRetry(seconds):
                retryWork?.cancel()
                let work = DispatchWorkItem { [weak self] in self?.transition(.retryElapsed) }
                retryWork = work
                queue.asyncAfter(deadline: .now() + seconds, execute: work)
            case .cancel:
                retryWork?.cancel()
                central?.stopScan()
                if let peripheral { central?.cancelPeripheralConnection(peripheral) }
                characteristic = nil
                activePayload = nil
                writeQueue.removeAll()
            }
        }
    }

    private func attemptDelivery(now: Date) {
        guard machine.state == .ready, activePayload == nil,
              let characteristic, let payload = latestPayload,
              deliveryGate.shouldDeliver(payload, now: now) else { return }
        guard characteristic.properties.contains(.write) else {
            transition(.failed)
            return
        }
        do {
            messageID &+= 1
            activePayload = payload
            writeQueue = try UsageFrameCodec.frames(payload: payload, messageID: messageID)
            writeNextFrame()
        } catch {
            activePayload = nil
            onDelivery?(.failure(error))
        }
    }

    private func writeNextFrame() {
        guard let characteristic, let frame = writeQueue.first else {
            if let activePayload {
                deliveryGate.didDeliver(activePayload, at: Date())
                onDelivery?(.success(()))
            }
            activePayload = nil
            attemptDelivery(now: Date())
            return
        }
        peripheral?.writeValue(frame, for: characteristic, type: .withResponse)
    }
}

extension UsageBluetoothClient: CBCentralManagerDelegate {
    public func centralManagerDidUpdateState(_ central: CBCentralManager) {
        guard running else { return }
        if central.state == .poweredOn {
            transition(machine.state == .stopped ? .startPoweredOn : .poweredOn)
        } else {
            transition(machine.state == .stopped ? .startUnavailable : .poweredOff)
        }
    }

    public func centralManager(
        _ central: CBCentralManager,
        didDiscover peripheral: CBPeripheral,
        advertisementData: [String: Any],
        rssi RSSI: NSNumber
    ) {
        guard machine.state == .scanning else { return }
        onDiagnostic?("discovered usage service rssi=\(RSSI)")
        self.peripheral = peripheral
        peripheral.delegate = self
        transition(.peripheralDiscovered)
    }

    public func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        onDiagnostic?("connected to usage service")
        transition(.connected)
    }

    public func centralManager(
        _ central: CBCentralManager,
        didFailToConnect peripheral: CBPeripheral,
        error: Error?
    ) {
        let detail = error.map { String(describing: $0) } ?? "unknown"
        onDiagnostic?("connect failed error=\(detail)")
        transition(.failed)
    }

    public func centralManager(
        _ central: CBCentralManager,
        didDisconnectPeripheral peripheral: CBPeripheral,
        error: Error?
    ) {
        guard running else { return }
        let detail = error.map { String(describing: $0) } ?? "none"
        onDiagnostic?("disconnected error=\(detail)")
        if case .retrying = machine.state { return }
        if machine.state == .stopped || machine.state == .unavailable { return }
        deliveryGate.resetConnection()
        transition(.failed)
    }
}

extension UsageBluetoothClient: CBPeripheralDelegate {
    public func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let error {
            onDiagnostic?("service discovery failed error=\(String(describing: error))")
        }
        guard error == nil,
              peripheral.services?.contains(where: { $0.uuid == Self.serviceUUID }) == true else {
            transition(.failed)
            return
        }
        transition(.serviceDiscovered)
    }

    public func peripheral(
        _ peripheral: CBPeripheral,
        didDiscoverCharacteristicsFor service: CBService,
        error: Error?
    ) {
        if let error {
            onDiagnostic?("characteristic discovery failed error=\(String(describing: error))")
        }
        guard error == nil,
              let characteristic = service.characteristics?.first(where: {
                  $0.uuid == Self.characteristicUUID
              }), characteristic.properties.contains(.write) else {
            transition(.failed)
            return
        }
        self.characteristic = characteristic
        transition(.characteristicDiscovered)
    }

    public func peripheral(
        _ peripheral: CBPeripheral,
        didWriteValueFor characteristic: CBCharacteristic,
        error: Error?
    ) {
        if let error {
            onDiagnostic?("write failed error=\(String(describing: error))")
            onDelivery?(.failure(error))
            transition(.failed)
            return
        }
        if !writeQueue.isEmpty { writeQueue.removeFirst() }
        writeNextFrame()
    }
}
