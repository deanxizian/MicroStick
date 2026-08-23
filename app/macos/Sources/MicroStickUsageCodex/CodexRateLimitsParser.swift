import CoreFoundation
import Foundation
import MicroStickUsageCore

/// Converts the bounded account-rate-limit response into MicroStick's semantic
/// usage snapshot. No account metadata, credit details, or unrelated buckets
/// leave this compatibility layer.
public enum CodexRateLimitsParser {
    public static func parse(
        result: Any,
        receivedAt: Date = Date()
    ) -> UsageSnapshot? {
        guard let result = result as? [String: Any] else { return nil }

        var candidates: [[String: Any]] = []
        if let byLimitID = result["rateLimitsByLimitId"] as? [String: Any],
           let codex = byLimitID["codex"] as? [String: Any] {
            candidates.append(codex)
        }
        if let legacy = result["rateLimits"] as? [String: Any] {
            candidates.append(legacy)
        }

        for candidate in candidates {
            if let snapshot = parse(candidate: candidate, receivedAt: receivedAt) {
                return snapshot
            }
        }
        return nil
    }

    private static func parse(
        candidate: [String: Any],
        receivedAt: Date
    ) -> UsageSnapshot? {
        guard acceptedLimitID(candidate["limitId"]) else { return nil }

        var remaining: UInt16?
        var resetAt: Date?
        for key in ["primary", "secondary"] {
            guard let window = candidate[key] as? [String: Any],
                  integer(window["windowDurationMins"]) == 10_080,
                  let parsedRemaining = remainingBasisPoints(
                      window["usedPercent"]
                  ) else {
                continue
            }
            remaining = parsedRemaining
            resetAt = validResetDate(window["resetsAt"], receivedAt: receivedAt)
        }
        guard let remaining else { return nil }

        let snapshot = UsageSnapshot(
            sevenDayRemainingBasisPoints: remaining,
            sevenDayResetAt: resetAt,
            updatedAt: receivedAt,
            stale: false
        )
        return (try? snapshot.validate()).map { snapshot }
    }

    private static func acceptedLimitID(_ value: Any?) -> Bool {
        guard let value, !(value is NSNull) else { return true }
        guard let string = value as? String else { return false }
        return string.isEmpty || string == "codex"
    }

    private static func integer(_ value: Any?) -> Int? {
        guard let value, !(value is Bool), let number = value as? NSNumber,
              CFGetTypeID(number) != CFBooleanGetTypeID() else { return nil }
        let double = number.doubleValue
        guard double.isFinite, double.rounded(.towardZero) == double else {
            return nil
        }
        return Int(exactly: double)
    }

    private static func number(_ value: Any?) -> Double? {
        guard let value, !(value is Bool) else { return nil }
        if let number = value as? NSNumber,
           CFGetTypeID(number) != CFBooleanGetTypeID() {
            return number.doubleValue
        }
        if let string = value as? String { return Double(string) }
        return nil
    }

    private static func remainingBasisPoints(_ value: Any?) -> UInt16? {
        guard let used = number(value), used.isFinite else { return nil }
        let remainingPercent = min(
            100,
            max(0, (100 - used).rounded(.toNearestOrEven))
        )
        return UInt16(remainingPercent) * 100
    }

    private static func validResetDate(
        _ value: Any?,
        receivedAt: Date
    ) -> Date? {
        guard let seconds = number(value), seconds.isFinite else { return nil }
        let resetAt = Date(timeIntervalSince1970: seconds.rounded(.towardZero))
        guard resetAt >= receivedAt else { return nil }
        let probe = UsageSnapshot(
            sevenDayRemainingBasisPoints: 0,
            sevenDayResetAt: resetAt,
            updatedAt: receivedAt
        )
        return (try? probe.validate()).map { resetAt }
    }
}
