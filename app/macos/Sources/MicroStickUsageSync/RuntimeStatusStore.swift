import Foundation

final class RuntimeStatusStore: @unchecked Sendable {
    private struct Status: Codable {
        let version: Int
        let bluetoothState: String
        let codexState: String
        let usageSource: String?
        let lastDeliveryAt: Int64?
        let updatedAt: Int64
    }

    private let queue = DispatchQueue(label: "com.deanxizian.microstick.usage-sync.status")
    private let url: URL
    private var bluetoothState = "starting"
    private var codexState = "starting"
    private var usageSource: String?
    private var lastDeliveryAt: Int64?

    init(
        url: URL = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent(
                "Library/Application Support/MicroStick/usage-sync-status-v1.json"
            )
    ) {
        self.url = url
    }

    func setBluetoothState(_ value: String) {
        queue.async { [self] in
            bluetoothState = value
            persist()
        }
    }

    func setCodexState(_ value: String) {
        queue.async { [self] in
            codexState = value
            persist()
        }
    }

    func setUsageSource(_ value: String) {
        queue.async { [self] in
            usageSource = value
            persist()
        }
    }

    func delivered(at date: Date = Date()) {
        queue.async { [self] in
            lastDeliveryAt = Int64(date.timeIntervalSince1970.rounded(.towardZero))
            persist()
        }
    }

    private func persist() {
        let now = Int64(Date().timeIntervalSince1970.rounded(.towardZero))
        let status = Status(
            version: 1,
            bluetoothState: bluetoothState,
            codexState: codexState,
            usageSource: usageSource,
            lastDeliveryAt: lastDeliveryAt,
            updatedAt: now
        )
        guard let data = try? JSONEncoder().encode(status) else { return }
        let directory = url.deletingLastPathComponent()
        try? FileManager.default.createDirectory(
            at: directory,
            withIntermediateDirectories: true,
            attributes: [.posixPermissions: 0o700]
        )
        try? FileManager.default.setAttributes(
            [.posixPermissions: 0o700],
            ofItemAtPath: directory.path
        )
        try? data.write(to: url, options: [.atomic])
        try? FileManager.default.setAttributes(
            [.posixPermissions: 0o600],
            ofItemAtPath: url.path
        )
    }
}
