import Foundation
import XCTest
@testable import MicroStickUsageCodex

final class CodexAppServerWireTests: XCTestCase {
    func testBuildsBoundedInitializeAndRateLimitRequests() throws {
        let initialize = try CodexAppServerWire.initializeRequest(
            id: 7,
            clientVersion: "1.4.0"
        )
        XCTAssertEqual(initialize.last, 0x0a)
        let initializeObject = try XCTUnwrap(CodexAppServerWire.decode(
            Data(initialize.dropLast())
        ))
        XCTAssertEqual(initializeObject["id"] as? Int, 7)
        XCTAssertEqual(initializeObject["method"] as? String, "initialize")
        let params = try XCTUnwrap(initializeObject["params"] as? [String: Any])
        XCTAssertNil(params["capabilities"])

        let request = try CodexAppServerWire.rateLimitsRequest(id: 8)
        let requestObject = try XCTUnwrap(CodexAppServerWire.decode(
            Data(request.dropLast())
        ))
        XCTAssertEqual(requestObject["id"] as? Int, 8)
        XCTAssertEqual(
            requestObject["method"] as? String,
            "account/rateLimits/read"
        )
    }

    func testRejectsOversizedResponse() {
        XCTAssertNil(CodexAppServerWire.decode(Data(
            repeating: 0x20,
            count: CodexAppServerWire.maximumLineBytes + 1
        )))
    }
}
