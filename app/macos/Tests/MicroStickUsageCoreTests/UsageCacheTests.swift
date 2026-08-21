import Foundation
import XCTest
@testable import MicroStickUsageCore

final class UsageCacheTests: XCTestCase {
    func testCacheRestoreAlwaysMarksSnapshotStale() throws {
        let directory = try temporaryDirectory()
        defer { try? FileManager.default.removeItem(at: directory) }
        let cache = UsageCache(url: directory.appendingPathComponent("cache.json"))
        let snapshot = UsageSnapshot(sevenDayRemainingBasisPoints: 4_000,
                                     updatedAt: Date(timeIntervalSince1970: 1_767_258_000))
        try cache.save(snapshot)
        XCTAssertTrue(try XCTUnwrap(cache.load()).stale)
    }

    func testOlderObservationCannotRollBackLastValidSnapshot() {
        let current = UsageSnapshot(
            sevenDayRemainingBasisPoints: 4_100,
            updatedAt: Date(timeIntervalSince1970: 1_767_258_000)
        )
        let older = UsageSnapshot(
            sevenDayRemainingBasisPoints: 9_900,
            updatedAt: Date(timeIntervalSince1970: 1_767_257_000)
        )
        let now = current.updatedAt.addingTimeInterval(UsageSnapshot.staleAfter + 1)

        let selected = current.acceptingIfNotOlder(older, relativeTo: now)

        XCTAssertEqual(selected.sevenDayRemainingBasisPoints, 4_100)
        XCTAssertEqual(selected.updatedAt, current.updatedAt)
        XCTAssertTrue(selected.stale)
    }

    func testCacheCreatesPrivateDirectoryAndFile() throws {
        let parent = try temporaryDirectory()
        defer { try? FileManager.default.removeItem(at: parent) }
        let directory = parent.appendingPathComponent("private", isDirectory: true)
        let cache = UsageCache(url: directory.appendingPathComponent("cache.json"))
        try cache.save(UsageSnapshot(
            sevenDayRemainingBasisPoints: 4_200,
            updatedAt: Date(timeIntervalSince1970: 1_767_258_000)
        ))

        let directoryMode = try XCTUnwrap(
            FileManager.default.attributesOfItem(atPath: directory.path)[.posixPermissions]
                as? NSNumber
        ).intValue
        let fileMode = try XCTUnwrap(
            FileManager.default.attributesOfItem(atPath: cache.url.path)[.posixPermissions]
                as? NSNumber
        ).intValue
        XCTAssertEqual(directoryMode & 0o777, 0o700)
        XCTAssertEqual(fileMode & 0o777, 0o600)
    }

    private func temporaryDirectory() throws -> URL {
        let url = FileManager.default.temporaryDirectory
            .appendingPathComponent(UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        return url
    }
}
