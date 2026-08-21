import CoreFoundation
import Foundation

public struct ParsedRateLimit: Equatable, Sendable {
    public let timestamp: Date
    public let snapshot: UsageSnapshot

    public init(timestamp: Date, snapshot: UsageSnapshot) {
        self.timestamp = timestamp
        self.snapshot = snapshot
    }
}

public enum RateLimitParser {
    public static func parse(line: Data, now: Date = Date()) -> ParsedRateLimit? {
        guard let object = try? JSONSerialization.jsonObject(with: line),
              let event = object as? [String: Any],
              let timestamp = parseTimestamp(event["timestamp"]),
              let payload = event["payload"] as? [String: Any],
              payload["type"] as? String == "token_count",
              let rateLimits = payload["rate_limits"] as? [String: Any],
              acceptedLimitID(rateLimits["limit_id"]) else {
            return nil
        }

        var sevenDay: UInt16?
        var sevenDayReset: Date?
        for name in ["primary", "secondary"] {
            guard let window = rateLimits[name] as? [String: Any],
                  integer(window["window_minutes"]) == 10_080,
                  let remaining = remainingBasisPoints(window["used_percent"]) else {
                continue
            }
            sevenDay = remaining
            sevenDayReset = resetDate(window["resets_at"]).flatMap {
                $0 >= timestamp ? $0 : nil
            }
        }
        guard let sevenDay else { return nil }
        let snapshot = UsageSnapshot(
            sevenDayRemainingBasisPoints: sevenDay,
            sevenDayResetAt: sevenDayReset,
            updatedAt: timestamp,
            stale: now.timeIntervalSince(timestamp) > UsageSnapshot.staleAfter
        )
        return ParsedRateLimit(timestamp: timestamp, snapshot: snapshot)
    }

    public static func remainingBasisPoints(_ value: Any?) -> UInt16? {
        guard let number = number(value), number.isFinite else { return nil }
        let remainingPercent = min(
            100,
            max(0, (100 - number).rounded(.toNearestOrEven))
        )
        return UInt16(remainingPercent) * 100
    }

    private static func number(_ value: Any?) -> Double? {
        guard let value, !(value is Bool) else { return nil }
        if let number = value as? NSNumber {
            if CFGetTypeID(number) == CFBooleanGetTypeID() { return nil }
            return number.doubleValue
        }
        if let string = value as? String { return Double(string) }
        return nil
    }

    private static func integer(_ value: Any?) -> Int? {
        guard let value, !(value is Bool), let numeric = value as? NSNumber,
              CFGetTypeID(numeric) != CFBooleanGetTypeID() else { return nil }
        let number = numeric.doubleValue
        guard number.isFinite,
              number.rounded(.towardZero) == number else { return nil }
        return Int(exactly: number)
    }

    private static func acceptedLimitID(_ value: Any?) -> Bool {
        guard let value, !(value is NSNull) else { return true }
        guard let string = value as? String else { return false }
        return string.isEmpty || string == "codex"
    }

    private static func resetDate(_ value: Any?) -> Date? {
        if let seconds = number(value), seconds.isFinite {
            let rounded = seconds.rounded(.towardZero)
            guard rounded >= Double(UsageSnapshot.minimumTimestamp),
                  rounded <= Double(UsageSnapshot.maximumTimestamp) else {
                return nil
            }
            return Date(timeIntervalSince1970: rounded)
        }
        return parseTimestamp(value)
    }

    private static func parseTimestamp(_ value: Any?) -> Date? {
        guard let string = value as? String, !string.isEmpty else { return nil }
        let fractional = ISO8601DateFormatter()
        fractional.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
        if let date = fractional.date(from: string) { return date }
        let standard = ISO8601DateFormatter()
        standard.formatOptions = [.withInternetDateTime]
        return standard.date(from: string)
    }
}
