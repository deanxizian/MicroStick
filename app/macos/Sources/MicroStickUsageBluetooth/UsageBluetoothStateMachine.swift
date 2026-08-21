import Foundation

public enum UsageBluetoothState: Equatable, Sendable {
    case stopped
    case unavailable
    case scanning
    case connecting
    case discoveringService
    case discoveringCharacteristic
    case ready
    case retrying(attempt: Int)
}

public enum UsageBluetoothEvent: Equatable, Sendable {
    case startPoweredOn
    case startUnavailable
    case poweredOn
    case poweredOff
    case peripheralDiscovered
    case connected
    case serviceDiscovered
    case characteristicDiscovered
    case failed
    case retryElapsed
    case stop
}

public enum UsageBluetoothAction: Equatable, Sendable {
    case startScan
    case connect
    case discoverService
    case discoverCharacteristic
    case becameReady
    case scheduleRetry(seconds: TimeInterval)
    case cancel
}

public struct UsageBluetoothStateMachine: Sendable {
    public private(set) var state: UsageBluetoothState = .stopped
    private var poweredOn = false
    private var retryAttempt = 0

    public init() {}

    @discardableResult
    public mutating func handle(_ event: UsageBluetoothEvent) -> [UsageBluetoothAction] {
        switch event {
        case .startPoweredOn, .poweredOn:
            poweredOn = true
            retryAttempt = 0
            state = .scanning
            return [.startScan]
        case .startUnavailable:
            poweredOn = false
            state = .unavailable
            return []
        case .poweredOff:
            poweredOn = false
            state = .unavailable
            return [.cancel]
        case .peripheralDiscovered where state == .scanning:
            state = .connecting
            return [.connect]
        case .connected where state == .connecting:
            state = .discoveringService
            return [.discoverService]
        case .serviceDiscovered where state == .discoveringService:
            state = .discoveringCharacteristic
            return [.discoverCharacteristic]
        case .characteristicDiscovered where state == .discoveringCharacteristic:
            retryAttempt = 0
            state = .ready
            return [.becameReady]
        case .failed:
            guard poweredOn, state != .stopped else { return [] }
            retryAttempt += 1
            state = .retrying(attempt: retryAttempt)
            let delay = min(30, pow(2, Double(max(0, retryAttempt - 1))))
            return [.cancel, .scheduleRetry(seconds: delay)]
        case .retryElapsed:
            guard poweredOn else { return [] }
            state = .scanning
            return [.startScan]
        case .stop:
            poweredOn = false
            retryAttempt = 0
            state = .stopped
            return [.cancel]
        default:
            return []
        }
    }
}

public struct UsageDeliveryGate: Sendable {
    public var heartbeatInterval: TimeInterval
    public private(set) var lastDeliveredPayload: Data?
    public private(set) var lastDeliveredAt: Date?

    public init(heartbeatInterval: TimeInterval = 5 * 60) {
        self.heartbeatInterval = heartbeatInterval
    }

    public func shouldDeliver(_ payload: Data, now: Date) -> Bool {
        guard lastDeliveredPayload == payload, let lastDeliveredAt else { return true }
        return now.timeIntervalSince(lastDeliveredAt) >= heartbeatInterval
    }

    public mutating func didDeliver(_ payload: Data, at date: Date) {
        lastDeliveredPayload = payload
        lastDeliveredAt = date
    }

    public mutating func resetConnection() {
        lastDeliveredAt = nil
    }
}
