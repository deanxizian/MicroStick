import Foundation

public final class CodexSessionScanner: @unchecked Sendable {
    public static let defaultTailBytes = 1_500_000
    public static let defaultMaximumSessions = 40

    private struct Fingerprint: Equatable {
        let modifiedAt: Date
        let size: UInt64
    }

    private struct Candidate {
        let url: URL
        let fingerprint: Fingerprint
    }

    private struct CachedSummary {
        let fingerprint: Fingerprint
        let rateLimit: ParsedRateLimit?
    }

    private let sessionsURL: URL
    private let tailBytes: Int
    private let maximumSessions: Int
    private let lock = NSLock()
    private var summaries: [URL: CachedSummary] = [:]
    private var classifications: [URL: (Fingerprint, Bool)] = [:]
    private var knownCandidates: [URL: Fingerprint] = [:]
    private var hasDiscoveredCandidates = false

    public init(
        sessionsURL: URL = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent(".codex/sessions", isDirectory: true),
        tailBytes: Int = defaultTailBytes,
        maximumSessions: Int = defaultMaximumSessions
    ) {
        self.sessionsURL = sessionsURL
        self.tailBytes = max(1, tailBytes)
        self.maximumSessions = max(1, maximumSessions)
    }

    public func latestSnapshot(
        now: Date = Date(),
        changedURLs: [URL]? = nil
    ) -> UsageSnapshot? {
        lock.lock()
        defer { lock.unlock() }
        let discovered = sessionCandidates(changedURLs: changedURLs)
        let discoveredURLs = Set(discovered.map(\.url))
        classifications = classifications.filter { discoveredURLs.contains($0.key) }
        // Stop reading session headers once the bounded set of newest root
        // sessions is full. Old histories remain indexed by metadata, but
        // their JSON content is never opened during the normal scan.
        let candidates = Array(discovered.lazy
            .filter { !self.isSubagent($0) }
            .prefix(maximumSessions))
        let liveURLs = Set(candidates.map(\.url))
        summaries = summaries.filter { liveURLs.contains($0.key) }

        var latest: ParsedRateLimit?
        for candidate in candidates {
            let parsed: ParsedRateLimit?
            if let cached = summaries[candidate.url],
               cached.fingerprint == candidate.fingerprint {
                parsed = cached.rateLimit
            } else {
                parsed = summarize(candidate.url, now: now)
                summaries[candidate.url] = CachedSummary(
                    fingerprint: candidate.fingerprint,
                    rateLimit: parsed
                )
            }
            if let parsed, latest == nil || parsed.timestamp > latest!.timestamp {
                latest = parsed
            }
        }
        return latest?.snapshot.markingStale(relativeTo: now)
    }

    private func sessionCandidates(changedURLs: [URL]?) -> [Candidate] {
        if !hasDiscoveredCandidates || changedURLs == nil ||
            changedURLs?.contains(where: {
                $0.standardizedFileURL == sessionsURL.standardizedFileURL
            }) == true {
            knownCandidates = Dictionary(
                uniqueKeysWithValues: discoverCandidates(at: sessionsURL).map {
                    ($0.url, $0.fingerprint)
                }
            )
            hasDiscoveredCandidates = true
        } else {
            for url in changedURLs ?? [] {
                refreshChangedURL(url.standardizedFileURL)
            }
        }
        return knownCandidates.map { Candidate(url: $0.key, fingerprint: $0.value) }
            .sorted(by: candidateSort)
    }

    private func discoverCandidates(at root: URL) -> [Candidate] {
        guard let enumerator = FileManager.default.enumerator(
            at: root,
            includingPropertiesForKeys: [.isRegularFileKey, .contentModificationDateKey, .fileSizeKey],
            options: [.skipsHiddenFiles]
        ) else { return [] }
        var result: [Candidate] = []
        for case let url as URL in enumerator where url.pathExtension == "jsonl" {
            if let candidate = candidate(at: url) { result.append(candidate) }
        }
        return result
    }

    private func candidate(at url: URL) -> Candidate? {
        guard url.pathExtension == "jsonl",
              let values = try? url.resourceValues(forKeys: [
                  .isRegularFileKey, .contentModificationDateKey, .fileSizeKey,
              ]), values.isRegularFile == true,
              let modifiedAt = values.contentModificationDate,
              let fileSize = values.fileSize, fileSize >= 0 else { return nil }
        return Candidate(
            url: url.standardizedFileURL,
            fingerprint: Fingerprint(modifiedAt: modifiedAt, size: UInt64(fileSize))
        )
    }

    private func refreshChangedURL(_ url: URL) {
        var isDirectory: ObjCBool = false
        if FileManager.default.fileExists(atPath: url.path, isDirectory: &isDirectory) {
            if isDirectory.boolValue {
                let discovered = discoverCandidates(at: url)
                let prefix = url.path.hasSuffix("/") ? url.path : url.path + "/"
                let discoveredURLs = Set(discovered.map(\.url))
                knownCandidates = knownCandidates.filter {
                    !$0.key.path.hasPrefix(prefix) || discoveredURLs.contains($0.key)
                }
                for candidate in discovered {
                    knownCandidates[candidate.url] = candidate.fingerprint
                }
            } else if let candidate = candidate(at: url) {
                knownCandidates[candidate.url] = candidate.fingerprint
            }
        } else {
            let prefix = url.path.hasSuffix("/") ? url.path : url.path + "/"
            knownCandidates = knownCandidates.filter {
                $0.key != url && !$0.key.path.hasPrefix(prefix)
            }
        }
    }

    private func candidateSort(_ left: Candidate, _ right: Candidate) -> Bool {
        if left.fingerprint.modifiedAt == right.fingerprint.modifiedAt {
            return left.url.path > right.url.path
        }
        return left.fingerprint.modifiedAt > right.fingerprint.modifiedAt
    }

    private func isSubagent(_ candidate: Candidate) -> Bool {
        if let cached = classifications[candidate.url], cached.0 == candidate.fingerprint {
            return cached.1
        }
        let classification = firstJSONLine(candidate.url).map(Self.classifySubagent) ?? false
        classifications[candidate.url] = (candidate.fingerprint, classification)
        return classification
    }

    private static func classifySubagent(_ line: Data) -> Bool {
        guard let object = try? JSONSerialization.jsonObject(with: line),
              let event = object as? [String: Any],
              event["type"] as? String == "session_meta",
              let payload = event["payload"] as? [String: Any] else { return false }
        if (payload["thread_source"] as? String)?.lowercased() == "subagent" { return true }
        if (payload["source"] as? String)?.lowercased() == "subagent" { return true }
        if let source = payload["source"] as? [String: Any] {
            return source.keys.contains("subagent")
        }
        return false
    }

    private func summarize(_ url: URL, now: Date) -> ParsedRateLimit? {
        var latest: ParsedRateLimit?
        for line in tailLines(url).reversed() {
            guard let parsed = RateLimitParser.parse(line: line, now: now) else { continue }
            if latest == nil || parsed.timestamp > latest!.timestamp {
                latest = parsed
            }
        }
        return latest
    }

    private func firstJSONLine(_ url: URL) -> Data? {
        guard let handle = try? FileHandle(forReadingFrom: url) else { return nil }
        defer { try? handle.close() }
        guard let data = try? handle.read(upToCount: 65_536), !data.isEmpty else {
            return nil
        }
        return data.firstIndex(of: 0x0a).map { data[..<$0] } ?? data
    }

    private func tailLines(_ url: URL) -> [Data] {
        guard let handle = try? FileHandle(forReadingFrom: url) else { return [] }
        defer { try? handle.close() }
        guard let size = try? handle.seekToEnd() else { return [] }
        let start = size > UInt64(tailBytes) ? size - UInt64(tailBytes) : 0
        do {
            try handle.seek(toOffset: start)
            guard var data = try handle.readToEnd(), !data.isEmpty else { return [] }
            if start > 0 {
                guard let newline = data.firstIndex(of: 0x0a) else { return [] }
                data = data[data.index(after: newline)...]
            }
            return [UInt8](data).split(separator: 0x0a).map { Data($0) }
        } catch {
            return []
        }
    }
}
