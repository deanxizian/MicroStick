import Foundation

enum CodexAppServerWire {
    static let maximumLineBytes = 1_048_576

    static func initializeRequest(id: Int, clientVersion: String) throws -> Data {
        try line([
            "id": id,
            "method": "initialize",
            "params": [
                "clientInfo": [
                    "name": "microstick-usage-sync",
                    "title": "MicroStickUsageSync",
                    "version": clientVersion,
                ],
            ],
        ])
    }

    static func rateLimitsRequest(id: Int) throws -> Data {
        try line([
            "id": id,
            "method": "account/rateLimits/read",
        ])
    }

    static func decode(_ line: Data) -> [String: Any]? {
        guard line.count <= maximumLineBytes,
              let object = try? JSONSerialization.jsonObject(with: line) else {
            return nil
        }
        return object as? [String: Any]
    }

    private static func line(_ object: [String: Any]) throws -> Data {
        var data = try JSONSerialization.data(withJSONObject: object)
        data.append(0x0a)
        return data
    }
}
