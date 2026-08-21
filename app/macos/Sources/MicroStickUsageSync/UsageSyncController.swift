import AppKit
import Foundation
import OSLog
import MicroStickUsageBluetooth
import MicroStickUsageCore

final class UsageSyncController: @unchecked Sendable {
    private let logger = Logger(subsystem: "com.deanxizian.microstick.usage-sync", category: "runtime")
    private let queue = DispatchQueue(label: "com.deanxizian.microstick.usage-sync.scanner",
                                      qos: .utility)
    private let sessionsURL: URL
    private let scanner: CodexSessionScanner
    private let cache: UsageCache
    private let bluetooth: UsageBluetoothClient
    private let statusStore = RuntimeStatusStore()
    private var watcher: SessionChangeWatcher!
    private var periodicTimer: DispatchSourceTimer?
    private var debounceWork: DispatchWorkItem?
    private var pendingChangedURLs: Set<URL> = []
    private var pendingFullScan = false
    private var currentSnapshot: UsageSnapshot?
    private var workspaceObservers: [NSObjectProtocol] = []

    init(
        sessionsURL: URL = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent(".codex/sessions", isDirectory: true),
        scanner: CodexSessionScanner? = nil,
        cache: UsageCache = UsageCache(),
        bluetooth: UsageBluetoothClient = UsageBluetoothClient()
    ) {
        self.sessionsURL = sessionsURL
        self.scanner = scanner ?? CodexSessionScanner(sessionsURL: sessionsURL)
        self.cache = cache
        self.bluetooth = bluetooth
        watcher = SessionChangeWatcher(url: sessionsURL, queue: queue) {
            [weak self] urls, forceFullScan in
            self?.scheduleScan(changedURLs: urls, forceFullScan: forceFullScan)
        }
    }

    func start() {
        bluetooth.onStateChange = { [weak self] state in
            self?.statusStore.setBluetoothState(String(describing: state))
            self?.logger.info("Bluetooth state changed: \(String(describing: state), privacy: .public)")
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
            case let .failure(error):
                self?.logger.error("Usage delivery failed: \(error.localizedDescription, privacy: .public)")
            }
        }
        if let restored = cache.load() {
            currentSnapshot = restored
            bluetooth.update(restored)
        }
        bluetooth.start()
        queue.async { [weak self] in
            guard let self else { return }
            _ = self.watcher.startIfAvailable()
            self.scanNow()
            self.startPeriodicTimer()
        }
        observePowerEvents()
        logger.info("MicroStickUsageSync started")
    }

    func stop() {
        for observer in workspaceObservers {
            NSWorkspace.shared.notificationCenter.removeObserver(observer)
        }
        workspaceObservers.removeAll()
        queue.sync {
            debounceWork?.cancel()
            periodicTimer?.cancel()
            periodicTimer = nil
            watcher.stop()
        }
        bluetooth.stop()
        logger.info("MicroStickUsageSync stopped")
    }

    private func scheduleScan(changedURLs: [URL], forceFullScan: Bool) {
        if forceFullScan {
            pendingFullScan = true
            pendingChangedURLs.removeAll()
        } else if !pendingFullScan {
            pendingChangedURLs.formUnion(changedURLs.map(\.standardizedFileURL))
        }
        debounceWork?.cancel()
        let work = DispatchWorkItem { [weak self] in
            guard let self else { return }
            let changed = self.pendingFullScan ? nil : Array(self.pendingChangedURLs)
            self.pendingFullScan = false
            self.pendingChangedURLs.removeAll()
            self.scanNow(changedURLs: changed)
        }
        debounceWork = work
        queue.asyncAfter(deadline: .now() + 0.75, execute: work)
    }

    private func scanNow(changedURLs: [URL]? = nil) {
        let now = Date()
        let next: UsageSnapshot?
        if let observed = scanner.latestSnapshot(now: now,
                                                  changedURLs: changedURLs) {
            let selected = currentSnapshot?.acceptingIfNotOlder(
                observed,
                relativeTo: now
            ) ?? observed
            next = selected
            if selected.updatedAt == observed.updatedAt,
               observed != currentSnapshot {
                do {
                    try cache.save(observed)
                } catch {
                    logger.error("Could not save the quota-only cache")
                }
            } else if selected.updatedAt != observed.updatedAt {
                logger.info("Ignored an older usage snapshot after session rotation")
            }
        } else {
            next = currentSnapshot?.restoredFromCache() ?? cache.load()
        }
        if let next, next != currentSnapshot {
            currentSnapshot = next
            bluetooth.update(next)
            logger.debug("Usage snapshot changed; stale=\(next.stale, privacy: .public)")
        }
    }

    private func startPeriodicTimer() {
        let timer = DispatchSource.makeTimerSource(queue: queue)
        timer.schedule(deadline: .now() + 5 * 60, repeating: 5 * 60,
                       leeway: .seconds(15))
        timer.setEventHandler { [weak self] in
            guard let self else { return }
            _ = self.watcher.startIfAvailable()
            self.scanNow()
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
            self?.queue.async { self?.scanNow() }
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
