import Foundation
import XCTest
@testable import MicroStickUsageBluetooth

final class UsageDeliveryGateTests: XCTestCase {
    func testOnlyChangesAndHeartbeatsAreDelivered() {
        var gate = UsageDeliveryGate(heartbeatInterval: 300)
        let first = Data([1, 2, 3])
        let changed = Data([1, 2, 4])
        let start = Date(timeIntervalSince1970: 1_000)
        XCTAssertTrue(gate.shouldDeliver(first, now: start))
        gate.didDeliver(first, at: start)
        XCTAssertFalse(gate.shouldDeliver(first, now: start.addingTimeInterval(299)))
        XCTAssertTrue(gate.shouldDeliver(first, now: start.addingTimeInterval(300)))
        XCTAssertTrue(gate.shouldDeliver(changed, now: start.addingTimeInterval(1)))
    }

    func testReconnectForcesDelivery() {
        var gate = UsageDeliveryGate(heartbeatInterval: 300)
        let payload = Data([1])
        let now = Date()
        gate.didDeliver(payload, at: now)
        gate.resetConnection()
        XCTAssertTrue(gate.shouldDeliver(payload, now: now))
    }
}
