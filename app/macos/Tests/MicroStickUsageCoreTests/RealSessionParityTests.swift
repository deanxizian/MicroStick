import Foundation
import XCTest
@testable import MicroStickUsageCore

final class RealSessionParityTests: XCTestCase {
    func testRealCodexSessionsMatchExpectedSevenDayQuota() throws {
        let environment = ProcessInfo.processInfo.environment
        guard environment["MICROSTICK_REAL_SESSION_PARITY"] == "1" else {
            throw XCTSkip("Set MICROSTICK_REAL_SESSION_PARITY=1 for the local-only check.")
        }
        let snapshot = CodexSessionScanner().latestSnapshot()
        let expectedSevenDay = try expected("MICROSTICK_EXPECT_7D", in: environment)

        XCTAssertEqual(snapshot?.sevenDayRemainingPercent, expectedSevenDay)
        XCTAssertEqual(snapshot?.stale, environment["MICROSTICK_EXPECT_STALE"] == "1")

        // Deliberately print quota fields only. Session paths and conversation
        // data are neither retained by UsageSnapshot nor emitted by this test.
        print(
            "REAL_PARITY",
            snapshot?.sevenDayRemainingPercent.map(String.init) ?? "none",
            snapshot?.stale == true ? "stale" : "fresh"
        )
    }

    private func expected(
        _ key: String,
        in environment: [String: String]
    ) throws -> Int? {
        guard let raw = environment[key] else {
            throw ParityConfigurationError.missing(key)
        }
        if raw == "none" { return nil }
        guard let value = Int(raw) else {
            throw ParityConfigurationError.invalid(key)
        }
        return value
    }
}

private enum ParityConfigurationError: Error {
    case missing(String)
    case invalid(String)
}
