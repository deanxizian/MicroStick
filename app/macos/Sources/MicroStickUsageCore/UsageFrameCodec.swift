import Foundation

public enum UsageFrameCodec {
    public static let frameVersion: UInt8 = 1
    public static let headerSize = 9
    public static let maximumFrameSize = 20
    public static let chunkSize = maximumFrameSize - headerSize
    public static let maximumFragments = 2

    public static func frames(payload: Data, messageID: UInt8) throws -> [Data] {
        guard !payload.isEmpty, payload.count <= UsagePayloadCodec.payloadSize else {
            throw UsageValidationError.invalidLength
        }
        let count = (payload.count + chunkSize - 1) / chunkSize
        guard count <= maximumFragments else { throw UsageValidationError.invalidFrame }
        let checksum = crc16(payload)
        return (0..<count).map { index in
            let start = index * chunkSize
            let end = min(start + chunkSize, payload.count)
            var frame = Data([0x56, 0x55, frameVersion, messageID, UInt8(index),
                              UInt8(count), UInt8(payload.count),
                              UInt8(checksum & 0xff), UInt8(checksum >> 8)])
            frame.append(payload[start..<end])
            return frame
        }
    }

    public static func crc16(_ data: Data) -> UInt16 {
        var crc: UInt16 = 0xffff
        for byte in data {
            crc ^= UInt16(byte) << 8
            for _ in 0..<8 {
                crc = crc & 0x8000 != 0 ? (crc << 1) ^ 0x1021 : crc << 1
            }
        }
        return crc
    }
}

public struct UsageFrameReassembler: Sendable {
    private var messageID: UInt8?
    private var fragmentCount = 0
    private var totalLength = 0
    private var checksum: UInt16 = 0
    private var fragments: [Int: Data] = [:]

    public init() {}

    public mutating func reset() {
        self = UsageFrameReassembler()
    }

    public mutating func accept(_ frame: Data) throws -> UsageSnapshot? {
        guard frame.count >= UsageFrameCodec.headerSize,
              frame.count <= UsageFrameCodec.maximumFrameSize,
              frame[0] == 0x56, frame[1] == 0x55,
              frame[2] == UsageFrameCodec.frameVersion else {
            reset()
            throw UsageValidationError.invalidFrame
        }
        let incomingID = frame[3]
        let index = Int(frame[4])
        let incomingCount = Int(frame[5])
        let incomingLength = Int(frame[6])
        let incomingChecksum = UInt16(frame[7]) | UInt16(frame[8]) << 8
        let expectedCount = (incomingLength + UsageFrameCodec.chunkSize - 1) /
            UsageFrameCodec.chunkSize
        guard incomingLength > 0, incomingLength <= UsagePayloadCodec.payloadSize,
              incomingCount == expectedCount,
              incomingCount <= UsageFrameCodec.maximumFragments,
              index < incomingCount else {
            reset()
            throw UsageValidationError.invalidFrame
        }
        let expectedChunk = min(UsageFrameCodec.chunkSize,
                                incomingLength - index * UsageFrameCodec.chunkSize)
        guard frame.count == UsageFrameCodec.headerSize + expectedChunk else {
            reset()
            throw UsageValidationError.invalidLength
        }
        if messageID == nil || messageID != incomingID {
            reset()
            messageID = incomingID
            fragmentCount = incomingCount
            totalLength = incomingLength
            checksum = incomingChecksum
        } else if fragmentCount != incomingCount || totalLength != incomingLength ||
                    checksum != incomingChecksum {
            reset()
            throw UsageValidationError.invalidFrame
        }
        fragments[index] = frame.dropFirst(UsageFrameCodec.headerSize)
        guard fragments.count == fragmentCount else { return nil }
        var payload = Data(capacity: totalLength)
        for fragmentIndex in 0..<fragmentCount {
            guard let fragment = fragments[fragmentIndex] else { return nil }
            payload.append(fragment)
        }
        defer { reset() }
        guard payload.count == totalLength,
              UsageFrameCodec.crc16(payload) == checksum else {
            throw UsageValidationError.checksumMismatch
        }
        return try UsagePayloadCodec.decode(payload)
    }
}
