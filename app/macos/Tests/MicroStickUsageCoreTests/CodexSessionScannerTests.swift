import Foundation
import XCTest
@testable import MicroStickUsageCore

final class CodexSessionScannerTests: XCTestCase {
    private var directory: URL!

    override func setUpWithError() throws {
        directory = FileManager.default.temporaryDirectory
            .appendingPathComponent(UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
    }

    override func tearDownWithError() throws {
        try? FileManager.default.removeItem(at: directory)
    }

    func testSelectsLatestRootRateLimitAndSkipsSubagent() throws {
        try writeSession(name: "old", timestamp: "2026-01-01T10:00:00Z", used: 20)
        try writeSession(name: "new", timestamp: "2026-01-01T11:00:00Z", used: 35)
        try writeSession(name: "subagent", timestamp: "2026-01-01T12:00:00Z",
                         used: 99, subagent: true)
        let scanner = CodexSessionScanner(sessionsURL: directory)
        let now = try XCTUnwrap(ISO8601DateFormatter().date(from: "2026-01-01T11:10:00Z"))
        XCTAssertEqual(scanner.latestSnapshot(now: now)?.sevenDayRemainingPercent, 65)
    }

    func testDetectsRotationAndIgnoresIncompleteTail() throws {
        try writeSession(name: "first", timestamp: "2026-01-01T10:00:00Z", used: 20)
        let scanner = CodexSessionScanner(sessionsURL: directory)
        let now = try XCTUnwrap(ISO8601DateFormatter().date(from: "2026-01-01T12:00:00Z"))
        XCTAssertEqual(scanner.latestSnapshot(now: now)?.sevenDayRemainingPercent, 80)
        try writeSession(name: "rotated", timestamp: "2026-01-01T11:00:00Z", used: 55,
                         trailing: #"{"timestamp":"broken"#)
        XCTAssertEqual(scanner.latestSnapshot(now: now)?.sevenDayRemainingPercent, 45)
    }

    func testIncrementalEventRefreshesOnlyChangedSessionSet() throws {
        let first = try writeSession(name: "first", timestamp: "2026-01-01T10:00:00Z",
                                     used: 20)
        let scanner = CodexSessionScanner(sessionsURL: directory)
        let now = try XCTUnwrap(ISO8601DateFormatter().date(from: "2026-01-01T12:00:00Z"))
        XCTAssertEqual(scanner.latestSnapshot(now: now)?.sevenDayRemainingPercent, 80)

        let second = try writeSession(name: "second", timestamp: "2026-01-01T11:00:00Z",
                                      used: 55)
        XCTAssertEqual(scanner.latestSnapshot(now: now, changedURLs: [second])?
            .sevenDayRemainingPercent, 45)

        try FileManager.default.removeItem(at: second)
        XCTAssertEqual(scanner.latestSnapshot(now: now, changedURLs: [second])?
            .sevenDayRemainingPercent, 80)
        XCTAssertTrue(FileManager.default.fileExists(atPath: first.path))
    }

    func testIncrementalScanKeepsSubagentClassificationCache() throws {
        let root = try writeSession(name: "root", timestamp: "2026-01-01T10:00:00Z",
                                    used: 20)
        let subagent = try writeSession(name: "subagent",
                                        timestamp: "2026-01-01T12:00:00Z",
                                        used: 99, subagent: true)
        let originalDate = try XCTUnwrap(
            FileManager.default.attributesOfItem(atPath: subagent.path)[.modificationDate]
                as? Date
        )
        let scanner = CodexSessionScanner(sessionsURL: directory)
        let now = try XCTUnwrap(ISO8601DateFormatter().date(from: "2026-01-01T12:10:00Z"))
        XCTAssertEqual(scanner.latestSnapshot(now: now)?.sevenDayRemainingPercent, 80)

        var contents = try String(contentsOf: subagent, encoding: .utf8)
        contents = contents.replacingOccurrences(of: "subagent", with: "notagent")
        try contents.write(to: subagent, atomically: false, encoding: .utf8)
        try FileManager.default.setAttributes([.modificationDate: originalDate],
                                              ofItemAtPath: subagent.path)

        XCTAssertEqual(scanner.latestSnapshot(now: now, changedURLs: [root])?
            .sevenDayRemainingPercent, 80)
    }

    func testNoRateLimitReturnsNil() throws {
        let text = #"{"timestamp":"2026-01-01T10:00:00Z","type":"session_meta","payload":{"id":"empty"}}"#
        try Data((text + "\n").utf8).write(to: directory.appendingPathComponent("empty.jsonl"))
        XCTAssertNil(CodexSessionScanner(sessionsURL: directory).latestSnapshot())
    }

    @discardableResult
    private func writeSession(
        name: String,
        timestamp: String,
        used: Double,
        subagent: Bool = false,
        trailing: String? = nil
    ) throws -> URL {
        let source = subagent ? #"{"subagent":{"depth":1}}"# : #""cli""#
        let metadata = "{\"timestamp\":\"2026-01-01T09:00:00Z\",\"type\":\"session_meta\",\"payload\":{\"id\":\"\(name)\",\"source\":\(source)}}"
        let quota = "{\"timestamp\":\"\(timestamp)\",\"payload\":{\"type\":\"token_count\",\"rate_limits\":{\"limit_id\":\"codex\",\"primary\":{\"used_percent\":\(used),\"window_minutes\":10080,\"resets_at\":1767873600}}}}"
        var text = metadata + "\n" + quota + "\n"
        if let trailing { text += trailing }
        let url = directory.appendingPathComponent("\(name).jsonl")
        try Data(text.utf8).write(to: url)
        let eventDate = ISO8601DateFormatter().date(from: timestamp)!
        try FileManager.default.setAttributes([.modificationDate: eventDate],
                                              ofItemAtPath: url.path)
        return url
    }
}
