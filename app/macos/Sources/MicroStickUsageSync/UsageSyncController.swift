import AppKit
import Foundation
import MicroStickUsageBluetooth
import MicroStickUsageCodex
import MicroStickUsageCore
import OSLog

final class UsageSyncController: @unchecked Sendable {
    private let logger = Logger(
        subsystem: "com.deanxizian.microstick.usage-sync",
        category: "runtime"
    )
    private let queue = DispatchQueue(
        label: "com.deanxizian.microstick.usage-sync.controller",
        qos: .utility
    )
    private let cache: UsageCache
    private let bluetooth: UsageBluetoothClient
    private let codex: CodexAppServerClient
    private let statusStore = RuntimeStatusStore()
    private var periodicTimer: DispatchSourceTimer?
    private var currentSnapshot: UsageSnapshot?
    private var workspaceObservers: [NSObjectProtocol] = []

    init(
        cache: UsageCache = UsageCache(),
        bluetooth: UsageBluetoothClient = UsageBluetoothClient(),
        codex: CodexAppServerClient = CodexAppServerClient()
    ) {
        self.cache = cache
        self.bluetooth = bluetooth
        self.codex = codex
    }

    func start() {
        configureBluetoothCallbacks()
        configureCodexCallbacks()

        if let restored = cache.load() {
            currentSnapshot = restored
            statusStore.setUsageSource("cache")
            bluetooth.update(restored)
        }

        bluetooth.start()
        codex.start()
        queue.async { [weak self] in self?.startPeriodicTimer() }
        observePowerEvents()
        logger.info("MicroStickUsageSync started")
    }

    func stop() {
        for observer in workspaceObservers {
            NSWorkspace.shared.notificationCenter.removeObserver(observer)
        }
        workspaceObservers.removeAll()
        queue.sync {
            periodicTimer?.cancel()
            periodicTimer = nil
        }
        codex.stop()
        bluetooth.stop()
        logger.info("MicroStickUsageSync stopped")
    }

    private func configureBluetoothCallbacks() {
        bluetooth.onStateChange = { [weak self] state in
            self?.statusStore.setBluetoothState(String(describing: state))
            self?.logger.info(
                "Bluetooth state changed: \(String(describing: state), privacy: .public)"
            )
        }
        bluetooth.onDiagnostic = { [weak self] message in
            if message.contains("failed") {
                self?.logger.error("Bluetooth diagnostic: \(message, privacy: .public)")
            } else {
                self?.logger.debug("Bluetooth diagnostic: \(message, privacy: .public)")
            }
        }
        bluetooth.onDelivery = { [weak self] result in
            switch result {
            case .success:
                self?.statusStore.delivered()
                self?.logger.debug("Usage snapshot delivered")
            case .failure(let error):
                self?.logger.error(
                    "Usage delivery failed: \(error.localizedDescription, privacy: .public)"
                )
            }
        }
    }

    private func configureCodexCallbacks() {
        codex.onStateChange = { [weak self] state in
            self?.statusStore.setCodexState(String(describing: state))
            self?.logger.info(
                "Codex quota source state changed: \(String(describing: state), privacy: .public)"
            )
        }
        codex.onDiagnostic = { [weak self] message in
            if message.contains("unavailable") || message.contains("cached") {
                self?.logger.info("Codex quota diagnostic: \(message, privacy: .public)")
            } else {
                self?.logger.error("Codex quota diagnostic: \(message, privacy: .public)")
            }
        }
        codex.onSnapshot = { [weak self] snapshot in
            self?.queue.async { self?.accept(snapshot) }
        }
    }

    private func accept(_ observed: UsageSnapshot, relativeTo now: Date = Date()) {
        let selected = currentSnapshot?.acceptingIfNotOlder(
            observed,
            relativeTo: now
        ) ?? observed
        guard selected.updatedAt == observed.updatedAt else {
            logger.info("Ignored an older active usage snapshot")
            publish(selected)
            return
        }
        if observed != currentSnapshot {
            do {
                try cache.save(observed)
            } catch {
                logger.error("Could not save the quota-only cache")
            }
        }
        statusStore.setUsageSource("network")
        publish(selected)
    }

    private func publish(_ next: UsageSnapshot) {
        guard next != currentSnapshot else { return }
        currentSnapshot = next
        bluetooth.update(next)
        logger.debug("Usage snapshot changed; stale=\(next.stale, privacy: .public)")
    }

    private func markCurrentSnapshotStaleIfNeeded(relativeTo now: Date = Date()) {
        guard let currentSnapshot else { return }
        publish(currentSnapshot.markingStale(relativeTo: now))
    }

    private func startPeriodicTimer() {
        let timer = DispatchSource.makeTimerSource(queue: queue)
        timer.schedule(
            deadline: .now() + 5 * 60,
            repeating: 5 * 60,
            leeway: .seconds(15)
        )
        timer.setEventHandler { [weak self] in
            guard let self else { return }
            self.markCurrentSnapshotStaleIfNeeded()
            self.codex.refresh()
            self.bluetooth.heartbeat()
        }
        periodicTimer = timer
        timer.resume()
    }

    private func observePowerEvents() {
        let center = NSWorkspace.shared.notificationCenter
        workspaceObservers.append(center.addObserver(
            forName: NSWorkspace.didWakeNotification,
            object: nil,
            queue: .main
        ) { [weak self] _ in
            self?.bluetooth.recoverAfterWake()
            self?.codex.recoverAfterWake()
            self?.queue.async { self?.markCurrentSnapshotStaleIfNeeded() }
        })
        workspaceObservers.append(center.addObserver(
            forName: NSWorkspace.willSleepNotification,
            object: nil,
            queue: .main
        ) { [weak self] _ in
            self?.logger.debug("Mac is entering sleep")
        })
    }
}
