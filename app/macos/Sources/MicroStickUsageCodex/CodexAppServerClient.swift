import Foundation
import MicroStickUsageCore

public enum CodexAppServerState: Equatable, Sendable {
    case stopped
    case unavailable
    case connecting
    case ready
    case retrying
}

/// A narrow stdio client for the current Codex app-server rate-limit method.
/// Undocumented wire details are intentionally kept out of the controller and
/// usage parser. A private last-valid snapshot remains available when this
/// compatibility method changes or the network is unavailable.
public final class CodexAppServerClient: @unchecked Sendable {
    public var onSnapshot: (@Sendable (UsageSnapshot) -> Void)?
    public var onStateChange: (@Sendable (CodexAppServerState) -> Void)?
    public var onDiagnostic: (@Sendable (String) -> Void)?

    private let queue: DispatchQueue
    private let queueKey = DispatchSpecificKey<UInt8>()
    private let executableLocator: @Sendable () -> URL?
    private let requestTimeout: TimeInterval
    private let initialRetryDelay: TimeInterval

    private var process: Process?
    private var inputHandle: FileHandle?
    private var outputHandle: FileHandle?
    private var readBuffer = Data()
    private var running = false
    private var initialized = false
    private var pendingRefresh = false
    private var requestID = 0
    private var initializeRequestID: Int?
    private var rateLimitsRequestID: Int?
    private var timeoutWork: DispatchWorkItem?
    private var retryWork: DispatchWorkItem?
    private var retryAttempt = 0
    private var state: CodexAppServerState = .stopped

    public init(
        queue: DispatchQueue = DispatchQueue(
            label: "com.deanxizian.microstick.usage.codex-app-server",
            qos: .utility
        ),
        requestTimeout: TimeInterval = 20,
        initialRetryDelay: TimeInterval = 30,
        executableLocator: @escaping @Sendable () -> URL? = {
            CodexExecutableLocator.locate()
        }
    ) {
        self.queue = queue
        self.requestTimeout = max(1, requestTimeout)
        self.initialRetryDelay = max(1, initialRetryDelay)
        self.executableLocator = executableLocator
        queue.setSpecific(key: queueKey, value: 1)
    }

    public func start() {
        queue.async { [weak self] in
            guard let self, !self.running else { return }
            self.running = true
            self.pendingRefresh = true
            self.launchIfNeeded()
        }
    }

    public func refresh() {
        queue.async { [weak self] in
            guard let self, self.running else { return }
            self.pendingRefresh = true
            self.retryWork?.cancel()
            self.retryWork = nil
            if self.process == nil {
                self.launchIfNeeded()
            } else if self.initialized {
                self.requestRateLimitsIfNeeded()
            }
        }
    }

    public func recoverAfterWake() {
        queue.async { [weak self] in
            guard let self, self.running else { return }
            self.pendingRefresh = true
            self.closeProcess()
            self.launchIfNeeded()
        }
    }

    public func stop() {
        let work = { [self] in
            running = false
            pendingRefresh = false
            timeoutWork?.cancel()
            timeoutWork = nil
            retryWork?.cancel()
            retryWork = nil
            closeProcess()
            transition(to: .stopped)
        }
        if DispatchQueue.getSpecific(key: queueKey) != nil {
            work()
        } else {
            queue.sync(execute: work)
        }
    }

    private func launchIfNeeded() {
        guard running, process == nil else { return }
        guard let executableURL = executableLocator() else {
            transition(to: .unavailable)
            onDiagnostic?("Codex executable unavailable; keeping cached usage")
            return
        }

        let process = Process()
        let inputPipe = Pipe()
        let outputPipe = Pipe()
        process.executableURL = executableURL
        process.arguments = ["app-server", "--stdio"]
        process.currentDirectoryURL = URL(fileURLWithPath: "/", isDirectory: true)
        process.standardInput = inputPipe
        process.standardOutput = outputPipe
        process.standardError = FileHandle.nullDevice
        process.terminationHandler = { [weak self, weak process] _ in
            guard let self, let process else { return }
            self.queue.async { self.processTerminated(process) }
        }
        outputPipe.fileHandleForReading.readabilityHandler = { [weak self] handle in
            let data = handle.availableData
            self?.queue.async { self?.consume(data) }
        }

        do {
            try process.run()
        } catch {
            outputPipe.fileHandleForReading.readabilityHandler = nil
            transition(to: .unavailable)
            onDiagnostic?("Codex app-server could not start; keeping cached usage")
            scheduleRetry()
            return
        }

        self.process = process
        inputHandle = inputPipe.fileHandleForWriting
        outputHandle = outputPipe.fileHandleForReading
        initialized = false
        readBuffer.removeAll(keepingCapacity: true)
        transition(to: .connecting)
        sendInitialize()
    }

    private func sendInitialize() {
        let id = nextRequestID()
        initializeRequestID = id
        let version = Bundle.main.object(
            forInfoDictionaryKey: "CFBundleShortVersionString"
        ) as? String ?? "development"
        do {
            try send(CodexAppServerWire.initializeRequest(
                id: id,
                clientVersion: version
            ))
            armTimeout(requestID: id)
        } catch {
            failAndRetry("Codex app-server initialization write failed")
        }
    }

    private func requestRateLimitsIfNeeded() {
        guard initialized else { return }
        guard rateLimitsRequestID == nil else {
            pendingRefresh = true
            return
        }
        pendingRefresh = false
        let id = nextRequestID()
        rateLimitsRequestID = id
        do {
            try send(CodexAppServerWire.rateLimitsRequest(id: id))
            armTimeout(requestID: id)
        } catch {
            failAndRetry("Codex rate-limit request write failed")
        }
    }

    private func send(_ data: Data) throws {
        guard let inputHandle else { throw CocoaError(.fileNoSuchFile) }
        try inputHandle.write(contentsOf: data)
    }

    private func consume(_ data: Data) {
        guard running else { return }
        if data.isEmpty {
            if let process { processTerminated(process) }
            return
        }
        readBuffer.append(data)

        while let newline = readBuffer.firstIndex(of: 0x0a) {
            let lineLength = readBuffer.distance(
                from: readBuffer.startIndex,
                to: newline
            )
            guard lineLength <= CodexAppServerWire.maximumLineBytes else {
                readBuffer.removeAll(keepingCapacity: true)
                failAndRetry("Codex app-server emitted an oversized response")
                return
            }
            let line = Data(readBuffer[..<newline])
            readBuffer.removeSubrange(...newline)
            if !line.isEmpty { handle(line) }
        }

        guard readBuffer.count <= CodexAppServerWire.maximumLineBytes else {
            readBuffer.removeAll(keepingCapacity: true)
            failAndRetry("Codex app-server emitted an oversized response")
            return
        }
    }

    private func handle(_ line: Data) {
        guard let message = CodexAppServerWire.decode(line) else { return }

        if let method = message["method"] as? String,
           method == "account/rateLimits/updated" {
            pendingRefresh = true
            requestRateLimitsIfNeeded()
            return
        }

        guard let id = (message["id"] as? NSNumber)?.intValue else { return }
        if id == initializeRequestID {
            timeoutWork?.cancel()
            timeoutWork = nil
            initializeRequestID = nil
            guard message["error"] == nil, message["result"] != nil else {
                failAndRetry("Codex app-server initialization failed")
                return
            }
            initialized = true
            transition(to: .ready)
            requestRateLimitsIfNeeded()
            return
        }

        guard id == rateLimitsRequestID else { return }
        timeoutWork?.cancel()
        timeoutWork = nil
        rateLimitsRequestID = nil
        guard message["error"] == nil,
              let result = message["result"],
              let snapshot = CodexRateLimitsParser.parse(
                  result: result,
                  receivedAt: Date()
              ) else {
            failAndRetry("Codex rate-limit response was unavailable")
            return
        }

        retryAttempt = 0
        onSnapshot?(snapshot)
        if pendingRefresh { requestRateLimitsIfNeeded() }
    }

    private func armTimeout(requestID: Int) {
        timeoutWork?.cancel()
        let work = DispatchWorkItem { [weak self] in
            guard let self,
                  self.initializeRequestID == requestID ||
                  self.rateLimitsRequestID == requestID else { return }
            self.failAndRetry("Codex rate-limit request timed out")
        }
        timeoutWork = work
        queue.asyncAfter(deadline: .now() + requestTimeout, execute: work)
    }

    private func failAndRetry(_ diagnostic: String) {
        onDiagnostic?(diagnostic)
        closeProcess()
        scheduleRetry()
    }

    private func scheduleRetry() {
        guard running else { return }
        retryWork?.cancel()
        retryAttempt = min(retryAttempt + 1, 5)
        let multiplier = pow(2.0, Double(retryAttempt - 1))
        let delay = min(initialRetryDelay * multiplier, 5 * 60)
        transition(to: .retrying)
        let work = DispatchWorkItem { [weak self] in
            guard let self, self.running else { return }
            self.retryWork = nil
            self.pendingRefresh = true
            self.launchIfNeeded()
        }
        retryWork = work
        queue.asyncAfter(deadline: .now() + delay, execute: work)
    }

    private func processTerminated(_ terminated: Process) {
        guard process === terminated else { return }
        closeProcess()
        if running {
            onDiagnostic?("Codex app-server stopped; scheduling recovery")
            scheduleRetry()
        }
    }

    private func closeProcess() {
        timeoutWork?.cancel()
        timeoutWork = nil
        initializeRequestID = nil
        rateLimitsRequestID = nil
        initialized = false
        readBuffer.removeAll(keepingCapacity: true)

        outputHandle?.readabilityHandler = nil
        outputHandle = nil
        try? inputHandle?.close()
        inputHandle = nil

        guard let process else { return }
        self.process = nil
        process.terminationHandler = nil
        if process.isRunning { process.terminate() }
    }

    private func nextRequestID() -> Int {
        requestID += 1
        return requestID
    }

    private func transition(to next: CodexAppServerState) {
        guard next != state else { return }
        state = next
        onStateChange?(next)
    }
}
