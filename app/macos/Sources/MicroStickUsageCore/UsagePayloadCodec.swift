import Foundation

public enum UsagePayloadCodec {
    public static let payloadSize = 20
    public static let unknownBasisPoints = UInt16.max

    public static func encode(_ snapshot: UsageSnapshot) throws -> Data {
        try snapshot.validate()
        var output = Data(capacity: payloadSize)
        output.append(UsageSnapshot.protocolVersion)
        output.append(snapshot.stale ? 0x01 : 0x00)
        output.appendLittleEndian(
            snapshot.sevenDayRemainingBasisPoints ?? unknownBasisPoints
        )
        output.appendLittleEndian(timestamp(snapshot.sevenDayResetAt))
        output.appendLittleEndian(timestamp(snapshot.updatedAt))
        precondition(output.count == payloadSize)
        return output
    }

    public static func decode(_ data: Data) throws -> UsageSnapshot {
        guard data.count == payloadSize else {
            throw UsageValidationError.invalidLength
        }
        guard data[0] == UsageSnapshot.protocolVersion else {
            throw UsageValidationError.invalidVersion
        }
        guard data[1] & ~UInt8(0x01) == 0 else {
            throw UsageValidationError.invalidFlags
        }
        let snapshot = UsageSnapshot(
            sevenDayRemainingBasisPoints: try percentage(
                data.readLittleEndianUInt16(at: 2)
            ),
            sevenDayResetAt: try optionalDate(
                data.readLittleEndianInt64(at: 4)
            ),
            updatedAt: try requiredDate(data.readLittleEndianInt64(at: 12)),
            stale: data[1] & 0x01 != 0
        )
        try snapshot.validate()
        return snapshot
    }

    private static func percentage(_ value: UInt16) throws -> UInt16? {
        if value == unknownBasisPoints { return nil }
        guard value <= UsageSnapshot.maximumBasisPoints else {
            throw UsageValidationError.invalidPercentage
        }
        return value
    }

    private static func timestamp(_ date: Date?) -> Int64 {
        guard let date else { return 0 }
        return Int64(date.timeIntervalSince1970.rounded(.towardZero))
    }

    private static func requiredDate(_ value: Int64) throws -> Date {
        guard UsageSnapshot.minimumTimestamp...UsageSnapshot.maximumTimestamp ~= value else {
            throw UsageValidationError.invalidTimestamp
        }
        return Date(timeIntervalSince1970: TimeInterval(value))
    }

    private static func optionalDate(_ value: Int64) throws -> Date? {
        if value == 0 { return nil }
        return try requiredDate(value)
    }
}

private extension Data {
    mutating func appendLittleEndian<T: FixedWidthInteger>(_ value: T) {
        var littleEndian = value.littleEndian
        Swift.withUnsafeBytes(of: &littleEndian) { append(contentsOf: $0) }
    }

    func readLittleEndianUInt16(at offset: Int) -> UInt16 {
        UInt16(self[offset]) | UInt16(self[offset + 1]) << 8
    }

    func readLittleEndianInt64(at offset: Int) -> Int64 {
        var bits: UInt64 = 0
        for index in 0..<8 {
            bits |= UInt64(self[offset + index]) << UInt64(index * 8)
        }
        return Int64(bitPattern: bits)
    }
}
