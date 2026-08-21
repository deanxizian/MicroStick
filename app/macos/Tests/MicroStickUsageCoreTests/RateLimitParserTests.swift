import Foundation
import XCTest
@testable import MicroStickUsageCore

final class RateLimitParserTests: XCTestCase {
    func testFixtureParsesSevenDayQuota() throws {
        let lines = try fixtureLines("rate-limits-root")
        let now = try XCTUnwrap(ISO8601DateFormatter().date(from: "2026-01-01T10:10:00Z"))
        let parsed = try XCTUnwrap(lines.compactMap { RateLimitParser.parse(line: $0, now: now) }.last)

        // Remaining percent is rounded from the host's used-percent value.
        XCTAssertEqual(parsed.snapshot.sevenDayRemainingPercent, 42)
        XCTAssertEqual(parsed.snapshot.sevenDayRemainingBasisPoints, 4_200)
        XCTAssertFalse(parsed.snapshot.stale)
        XCTAssertEqual(parsed.snapshot.sevenDayResetAt?.timeIntervalSince1970, 1_767_873_600)
        XCTAssertFalse(String(describing: parsed).contains("PRIVATE_CONVERSATION"))
    }

    func testIncompleteJSONLRetainsLastCompleteRateLimit() throws {
        let lines = try fixtureLines("incomplete")
        let parsed = try XCTUnwrap(lines.compactMap { RateLimitParser.parse(line: $0) }.last)
        XCTAssertEqual(parsed.snapshot.sevenDayRemainingPercent, 90)
    }

    func testRejectsTokensBooleansNonFiniteAndOtherLimits() throws {
        XCTAssertNil(RateLimitParser.remainingBasisPoints(true))
        XCTAssertNil(RateLimitParser.remainingBasisPoints(Double.infinity))
        let data = Data(#"{"timestamp":"2026-01-01T10:00:00Z","payload":{"type":"token_count","rate_limits":{"limit_id":"model-special","primary":{"used_percent":1,"window_minutes":10080}}}}"#.utf8)
        XCTAssertNil(RateLimitParser.parse(line: data))

        let nonStringLimit = Data(#"{"timestamp":"2026-01-01T10:00:00Z","payload":{"type":"token_count","rate_limits":{"limit_id":42,"primary":{"used_percent":1,"window_minutes":10080}}}}"#.utf8)
        XCTAssertNil(RateLimitParser.parse(line: nonStringLimit))

        let stringWindow = Data(#"{"timestamp":"2026-01-01T10:00:00Z","payload":{"type":"token_count","rate_limits":{"limit_id":"codex","primary":{"used_percent":1,"window_minutes":"10080"}}}}"#.utf8)
        XCTAssertNil(RateLimitParser.parse(line: stringWindow))
    }

    func testStaleBoundaryIsStrictlyOlderThanFifteenMinutes() throws {
        let line = try XCTUnwrap(fixtureLines("rate-limits-root").last)
        let timestamp = try XCTUnwrap(ISO8601DateFormatter().date(from: "2026-01-01T10:00:00Z"))
        XCTAssertFalse(try XCTUnwrap(RateLimitParser.parse(
            line: line, now: timestamp.addingTimeInterval(900)
        )).snapshot.stale)
        XCTAssertTrue(try XCTUnwrap(RateLimitParser.parse(
            line: line, now: timestamp.addingTimeInterval(901)
        )).snapshot.stale)
    }

    func testResetBeforeUsageUpdateIsDiscarded() throws {
        let data = Data(#"{"timestamp":"2026-01-01T10:00:00Z","payload":{"type":"token_count","rate_limits":{"limit_id":"codex","primary":{"used_percent":25,"window_minutes":10080,"resets_at":1767257999}}}}"#.utf8)
        let parsed = try XCTUnwrap(RateLimitParser.parse(line: data))
        XCTAssertEqual(parsed.snapshot.sevenDayRemainingPercent, 75)
        XCTAssertNil(parsed.snapshot.sevenDayResetAt)
    }

    private func fixtureLines(_ name: String) throws -> [Data] {
        let url = try XCTUnwrap(Bundle.module.url(forResource: name, withExtension: "jsonl",
                                                  subdirectory: "Fixtures"))
        return try [UInt8](Data(contentsOf: url)).split(separator: 0x0a).map { Data($0) }
    }
}
