import Foundation

public final class UsageCache: @unchecked Sendable {
    private struct StoredSnapshot: Codable {
        let version: Int
        let sevenDayRemainingBasisPoints: UInt16?
        let sevenDayResetAt: Int64?
        let updatedAt: Int64
        let stale: Bool
    }

    public let url: URL

    public init(
        url: URL = UsageCache.defaultSupportDirectory
            .appendingPathComponent("usage-cache-v1.json")
    ) {
        self.url = url
    }

    public func load() -> UsageSnapshot? {
        guard let data = try? Data(contentsOf: url),
              let stored = try? JSONDecoder().decode(StoredSnapshot.self, from: data),
              stored.version == 1 else {
            return nil
        }
        return snapshot(from: stored)?.restoredFromCache()
    }

    public func save(_ snapshot: UsageSnapshot) throws {
        try snapshot.validate()
        let stored = StoredSnapshot(
            version: 1,
            sevenDayRemainingBasisPoints: snapshot.sevenDayRemainingBasisPoints,
            sevenDayResetAt: snapshot.sevenDayResetAt.map(epoch),
            updatedAt: epoch(snapshot.updatedAt),
            stale: snapshot.stale
        )
        let directory = url.deletingLastPathComponent()
        try FileManager.default.createDirectory(
            at: directory,
            withIntermediateDirectories: true,
            attributes: [.posixPermissions: 0o700]
        )
        try? FileManager.default.setAttributes([.posixPermissions: 0o700],
                                               ofItemAtPath: directory.path)
        let data = try JSONEncoder().encode(stored)
        try data.write(to: url, options: [.atomic])
        try? FileManager.default.setAttributes([.posixPermissions: 0o600],
                                               ofItemAtPath: url.path)
    }

    private func snapshot(from stored: StoredSnapshot) -> UsageSnapshot? {
        let snapshot = UsageSnapshot(
            sevenDayRemainingBasisPoints: stored.sevenDayRemainingBasisPoints,
            sevenDayResetAt: stored.sevenDayResetAt.map(date),
            updatedAt: date(stored.updatedAt),
            stale: stored.stale
        )
        return (try? snapshot.validate()).map { snapshot }
    }

    private func epoch(_ date: Date) -> Int64 {
        Int64(date.timeIntervalSince1970.rounded(.towardZero))
    }

    private func date(_ timestamp: Int64) -> Date {
        Date(timeIntervalSince1970: TimeInterval(timestamp))
    }

    public static var defaultSupportDirectory: URL {
        FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent(
                "Library/Application Support/MicroStick",
                isDirectory: true
            )
    }
}
