import Foundation
import XCTest
@testable import MicroStickUsageBluetooth

final class UsageBluetoothStateMachineTests: XCTestCase {
    func testConnectionAndDisconnectRecovery() {
        var machine = UsageBluetoothStateMachine()
        XCTAssertEqual(machine.handle(.startPoweredOn), [.startScan])
        XCTAssertEqual(machine.handle(.peripheralDiscovered), [.connect])
        XCTAssertEqual(machine.handle(.connected), [.discoverService])
        XCTAssertEqual(machine.handle(.serviceDiscovered), [.discoverCharacteristic])
        XCTAssertEqual(machine.handle(.characteristicDiscovered), [.becameReady])
        XCTAssertEqual(machine.state, .ready)
        XCTAssertEqual(machine.handle(.failed), [.cancel, .scheduleRetry(seconds: 1)])
        XCTAssertEqual(machine.state, .retrying(attempt: 1))
        XCTAssertEqual(machine.handle(.retryElapsed), [.startScan])
    }

    func testRetryBackoffIsBounded() {
        var machine = UsageBluetoothStateMachine()
        _ = machine.handle(.startPoweredOn)
        var lastDelay: TimeInterval = 0
        for _ in 0..<10 {
            let actions = machine.handle(.failed)
            if case let .scheduleRetry(seconds) = actions.last { lastDelay = seconds }
            _ = machine.handle(.retryElapsed)
        }
        XCTAssertEqual(lastDelay, 30)
    }

    func testPoweredOffCancelsAndPoweredOnRescans() {
        var machine = UsageBluetoothStateMachine()
        _ = machine.handle(.startPoweredOn)
        XCTAssertEqual(machine.handle(.poweredOff), [.cancel])
        XCTAssertEqual(machine.state, .unavailable)
        XCTAssertEqual(machine.handle(.poweredOn), [.startScan])
    }
}
