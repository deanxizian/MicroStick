import Foundation
import XCTest
@testable import MicroStickUsageCodex

final class CodexRateLimitsParserTests: XCTestCase {
    func testParsesExactCodexBucketAndIgnoresModelBucket() throws {
        let receivedAt = Date(timeIntervalSince1970: 1_787_453_000)
        let result: [String: Any] = [
            "rateLimits": bucket(used: 46, resetAt: 1_787_807_608),
            "rateLimitsByLimitId": [
                "codex_bengalfox": bucket(
                    limitID: "codex_bengalfox",
                    used: 99,
                    resetAt: 1_788_057_816
                ),
                "codex": bucket(used: 46, resetAt: 1_787_807_608),
            ],
        ]

        let snapshot = try XCTUnwrap(CodexRateLimitsParser.parse(
            result: result,
            receivedAt: receivedAt
        ))

        XCTAssertEqual(snapshot.sevenDayRemainingPercent, 54)
        XCTAssertEqual(snapshot.updatedAt, receivedAt)
        XCTAssertEqual(snapshot.sevenDayResetAt?.timeIntervalSince1970, 1_787_807_608)
        XCTAssertFalse(snapshot.stale)
    }

    func testParsesLegacySecondarySevenDayWindow() throws {
        let receivedAt = Date(timeIntervalSince1970: 1_767_258_000)
        let result: [String: Any] = [
            "rateLimits": [
                "limitId": "codex",
                "primary": ["usedPercent": 20, "windowDurationMins": 300],
                "secondary": [
                    "usedPercent": 58,
                    "windowDurationMins": 10_080,
                    "resetsAt": 1_767_873_600,
                ],
            ],
        ]

        let snapshot = try XCTUnwrap(CodexRateLimitsParser.parse(
            result: result,
            receivedAt: receivedAt
        ))
        XCTAssertEqual(snapshot.sevenDayRemainingPercent, 42)
    }

    func testRejectsWrongLimitInvalidWindowAndBooleanUsage() throws {
        let wrongLimit: [String: Any] = [
            "rateLimits": bucket(limitID: "model-special", used: 20),
        ]
        XCTAssertNil(CodexRateLimitsParser.parse(result: wrongLimit))

        let invalid: [String: Any] = [
            "rateLimits": [
                "limitId": "codex",
                "primary": [
                    "usedPercent": true,
                    "windowDurationMins": "10080",
                ],
            ],
        ]
        XCTAssertNil(CodexRateLimitsParser.parse(result: invalid))
    }

    func testExpiredResetIsDiscardedWithoutDiscardingUsage() throws {
        let receivedAt = Date(timeIntervalSince1970: 1_767_258_000)
        let result: [String: Any] = [
            "rateLimits": bucket(used: 25, resetAt: 1_767_257_999),
        ]

        let snapshot = try XCTUnwrap(CodexRateLimitsParser.parse(
            result: result,
            receivedAt: receivedAt
        ))
        XCTAssertEqual(snapshot.sevenDayRemainingPercent, 75)
        XCTAssertNil(snapshot.sevenDayResetAt)
    }

    func testRoundsAndClampsRemainingPercentage() throws {
        let cases: [(Any, Int)] = [
            (27.5, 72),
            (58.5, 42),
            (-5, 100),
            (105, 0),
            ("46", 54),
        ]

        for (used, expected) in cases {
            let result: [String: Any] = [
                "rateLimits": [
                    "limitId": "codex",
                    "primary": [
                        "usedPercent": used,
                        "windowDurationMins": 10_080,
                    ],
                ],
            ]
            let snapshot = try XCTUnwrap(CodexRateLimitsParser.parse(
                result: result,
                receivedAt: Date(timeIntervalSince1970: 1_767_258_000)
            ))
            XCTAssertEqual(snapshot.sevenDayRemainingPercent, expected)
        }

        let nonFinite: [String: Any] = [
            "rateLimits": bucket(used: Double.infinity),
        ]
        XCTAssertNil(CodexRateLimitsParser.parse(result: nonFinite))
    }

    private func bucket(
        limitID: String = "codex",
        used: Any,
        resetAt: Int? = nil
    ) -> [String: Any] {
        var window: [String: Any] = [
            "usedPercent": used,
            "windowDurationMins": 10_080,
        ]
        if let resetAt { window["resetsAt"] = resetAt }
        return ["limitId": limitID, "primary": window]
    }
}
