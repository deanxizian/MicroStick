import Foundation
import XCTest
@testable import MicroStickUsageCodex

final class CodexAppServerClientTests: XCTestCase {
    func testLaunchesAppServerAndDeliversFreshSnapshot() throws {
        let directory = FileManager.default.temporaryDirectory
            .appendingPathComponent(UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(
            at: directory,
            withIntermediateDirectories: true
        )
        defer { try? FileManager.default.removeItem(at: directory) }
        let executable = directory.appendingPathComponent("fake-codex")
        let script = #"""
        #!/bin/sh
        while IFS= read -r line; do
          case "$line" in
            *'"method":"initialize"'*)
              printf '%s\n' '{"id":1,"result":{"userAgent":"fake"}}'
              ;;
            *'rateLimits'*)
              printf '%s\n' '{"id":2,"result":{"rateLimits":{"limitId":"codex","primary":{"usedPercent":46,"windowDurationMins":10080,"resetsAt":4100000000}}}}'
              ;;
          esac
        done
        """#
        try Data(script.utf8).write(to: executable)
        try FileManager.default.setAttributes(
            [.posixPermissions: 0o755],
            ofItemAtPath: executable.path
        )

        let delivered = expectation(description: "network usage delivered")
        let client = CodexAppServerClient(
            requestTimeout: 2,
            initialRetryDelay: 2,
            executableLocator: { executable }
        )
        client.onSnapshot = { snapshot in
            XCTAssertEqual(snapshot.sevenDayRemainingPercent, 54)
            XCTAssertFalse(snapshot.stale)
            delivered.fulfill()
        }
        client.start()
        wait(for: [delivered], timeout: 4)
        client.stop()
    }

    func testMissingExecutableReportsUnavailableState() {
        let unavailable = expectation(description: "unavailable state")
        let client = CodexAppServerClient(executableLocator: { nil })
        client.onStateChange = { state in
            if state == .unavailable { unavailable.fulfill() }
        }
        client.start()
        wait(for: [unavailable], timeout: 2)
        client.stop()
    }
}
