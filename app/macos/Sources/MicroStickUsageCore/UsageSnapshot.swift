import Foundation

public struct UsageSnapshot: Equatable, Sendable {
    public static let protocolVersion: UInt8 = 1
    public static let staleAfter: TimeInterval = 15 * 60
    public static let maximumBasisPoints: UInt16 = 10_000

    public var sevenDayRemainingBasisPoints: UInt16?
    public var sevenDayResetAt: Date?
    public var updatedAt: Date
    public var stale: Bool

    public init(
        sevenDayRemainingBasisPoints: UInt16?,
        sevenDayResetAt: Date? = nil,
        updatedAt: Date,
        stale: Bool = false
    ) {
        self.sevenDayRemainingBasisPoints = sevenDayRemainingBasisPoints
        self.sevenDayResetAt = sevenDayResetAt
        self.updatedAt = updatedAt
        self.stale = stale
    }

    public var sevenDayRemainingPercent: Int? {
        sevenDayRemainingBasisPoints.map { Int($0) / 100 }
    }

    public func markingStale(relativeTo now: Date) -> UsageSnapshot {
        var copy = self
        if now.timeIntervalSince(updatedAt) > Self.staleAfter {
            copy.stale = true
        }
        return copy
    }

    public func restoredFromCache() -> UsageSnapshot {
        var copy = self
        copy.stale = true
        return copy
    }

    public func acceptingIfNotOlder(
        _ candidate: UsageSnapshot,
        relativeTo now: Date
    ) -> UsageSnapshot {
        guard candidate.updatedAt >= updatedAt else {
            return markingStale(relativeTo: now)
        }
        return candidate
    }
}

public enum UsageValidationError: Error, Equatable, Sendable {
    case invalidVersion
    case invalidPercentage
    case invalidTimestamp
    case invalidFlags
    case invalidLength
    case invalidFrame
    case checksumMismatch
}

extension UsageSnapshot {
    static let minimumTimestamp: Int64 = 946_684_800
    static let maximumTimestamp: Int64 = 4_102_444_800

    public func validate() throws {
        if let value = sevenDayRemainingBasisPoints,
           value > Self.maximumBasisPoints {
            throw UsageValidationError.invalidPercentage
        }
        let required = Int64(updatedAt.timeIntervalSince1970.rounded(.towardZero))
        guard Self.minimumTimestamp...Self.maximumTimestamp ~= required else {
            throw UsageValidationError.invalidTimestamp
        }
        if let sevenDayResetAt {
            let timestamp = Int64(
                sevenDayResetAt.timeIntervalSince1970.rounded(.towardZero)
            )
            guard Self.minimumTimestamp...Self.maximumTimestamp ~= timestamp,
                  timestamp >= required else {
                throw UsageValidationError.invalidTimestamp
            }
        }
    }
}
