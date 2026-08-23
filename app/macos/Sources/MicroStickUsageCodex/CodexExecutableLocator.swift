import Foundation

public enum CodexExecutableLocator {
    public static func locate(
        fileManager: FileManager = .default,
        homeDirectory: URL = FileManager.default.homeDirectoryForCurrentUser
    ) -> URL? {
        let relativePath = "Contents/Resources/codex"
        let candidates = [
            URL(fileURLWithPath: "/Applications/ChatGPT.app")
                .appendingPathComponent(relativePath),
            homeDirectory.appendingPathComponent("Applications/ChatGPT.app")
                .appendingPathComponent(relativePath),
            URL(fileURLWithPath: "/Applications/Codex.app")
                .appendingPathComponent(relativePath),
            homeDirectory.appendingPathComponent("Applications/Codex.app")
                .appendingPathComponent(relativePath),
        ]
        return candidates.first { fileManager.isExecutableFile(atPath: $0.path) }
    }
}
