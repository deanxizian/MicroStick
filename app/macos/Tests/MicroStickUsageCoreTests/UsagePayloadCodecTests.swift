import Foundation
import XCTest
@testable import MicroStickUsageCore

final class UsagePayloadCodecTests: XCTestCase {
    private let snapshot = UsageSnapshot(
        sevenDayRemainingBasisPoints: 4_200,
        sevenDayResetAt: Date(timeIntervalSince1970: 1_767_873_600),
        updatedAt: Date(timeIntervalSince1970: 1_767_258_000),
        stale: false
    )

    func testPayloadRoundTripAndLittleEndianLayout() throws {
        let encoded = try UsagePayloadCodec.encode(snapshot)
        XCTAssertEqual(encoded.count, 20)
        XCTAssertEqual(Array(encoded.prefix(4)), [1, 0, 0x68, 0x10])
        XCTAssertEqual(try UsagePayloadCodec.decode(encoded), snapshot)
    }

    func testUnknownPercentAndBoundaries() throws {
        var unknown = snapshot
        unknown.sevenDayRemainingBasisPoints = nil
        let data = try UsagePayloadCodec.encode(unknown)
        XCTAssertEqual(Array(data[2..<4]), [0xff, 0xff])
        XCTAssertNil(try UsagePayloadCodec.decode(data).sevenDayRemainingBasisPoints)

        var invalid = snapshot
        invalid.sevenDayRemainingBasisPoints = 10_001
        XCTAssertThrowsError(try UsagePayloadCodec.encode(invalid))
    }

    func testVersionFlagsAndTimestampValidation() throws {
        var data = try UsagePayloadCodec.encode(snapshot)
        data[0] = 2
        XCTAssertThrowsError(try UsagePayloadCodec.decode(data))
        data = try UsagePayloadCodec.encode(snapshot)
        data[1] = 0x80
        XCTAssertThrowsError(try UsagePayloadCodec.decode(data))

        var invalid = snapshot
        invalid.updatedAt = Date(timeIntervalSince1970: 1)
        XCTAssertThrowsError(try UsagePayloadCodec.encode(invalid))

        invalid = snapshot
        invalid.sevenDayResetAt = invalid.updatedAt.addingTimeInterval(-1)
        XCTAssertThrowsError(try UsagePayloadCodec.encode(invalid))
    }

    func testFramesReassembleOutOfOrderAndRejectCorruption() throws {
        let payload = try UsagePayloadCodec.encode(snapshot)
        let frames = try UsageFrameCodec.frames(payload: payload, messageID: 42)
        XCTAssertEqual(frames.count, 2)
        XCTAssertTrue(frames.allSatisfy { $0.count <= 20 })
        var reassembler = UsageFrameReassembler()
        XCTAssertNil(try reassembler.accept(frames[1]))
        XCTAssertEqual(try reassembler.accept(frames[0]), snapshot)

        var corrupted = frames
        corrupted[0][9] ^= 0xff
        reassembler.reset()
        XCTAssertNil(try reassembler.accept(corrupted[0]))
        XCTAssertThrowsError(try reassembler.accept(corrupted[1]))
    }
}
