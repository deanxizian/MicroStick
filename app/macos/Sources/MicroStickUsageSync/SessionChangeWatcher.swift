import CoreServices
import Foundation

private let sessionChangeCallback: FSEventStreamCallback = {
    _, context, eventCount, eventPaths, eventFlags, _ in
    guard let context else { return }
    let watcher = Unmanaged<SessionChangeWatcher>.fromOpaque(context)
        .takeUnretainedValue()
    let pathArray = Unmanaged<CFArray>.fromOpaque(eventPaths)
        .takeUnretainedValue() as? [String] ?? []
    let rescanMask = FSEventStreamEventFlags(
        kFSEventStreamEventFlagMustScanSubDirs |
        kFSEventStreamEventFlagUserDropped |
        kFSEventStreamEventFlagKernelDropped |
        kFSEventStreamEventFlagEventIdsWrapped |
        kFSEventStreamEventFlagRootChanged
    )
    var forceFullScan = pathArray.count != eventCount
    for index in 0..<eventCount where eventFlags[index] & rescanMask != 0 {
        forceFullScan = true
    }
    watcher.receiveEvents(pathArray.map(URL.init(fileURLWithPath:)),
                          forceFullScan: forceFullScan)
}

final class SessionChangeWatcher {
    private let url: URL
    private let queue: DispatchQueue
    private let handler: @Sendable ([URL], Bool) -> Void
    private var stream: FSEventStreamRef?

    init(url: URL, queue: DispatchQueue,
         handler: @escaping @Sendable ([URL], Bool) -> Void) {
        self.url = url
        self.queue = queue
        self.handler = handler
    }

    @discardableResult
    func startIfAvailable() -> Bool {
        guard stream == nil,
              FileManager.default.fileExists(atPath: url.path) else { return stream != nil }
        var context = FSEventStreamContext(
            version: 0,
            info: Unmanaged.passUnretained(self).toOpaque(),
            retain: nil,
            release: nil,
            copyDescription: nil
        )
        let flags = FSEventStreamCreateFlags(kFSEventStreamCreateFlagFileEvents |
                                             kFSEventStreamCreateFlagUseCFTypes |
                                             kFSEventStreamCreateFlagNoDefer)
        guard let created = FSEventStreamCreate(
            kCFAllocatorDefault,
            sessionChangeCallback,
            &context,
            [url.path] as CFArray,
            FSEventStreamEventId(kFSEventStreamEventIdSinceNow),
            0.75,
            flags
        ) else { return false }
        stream = created
        FSEventStreamSetDispatchQueue(created, queue)
        if !FSEventStreamStart(created) {
            stop()
            return false
        }
        return true
    }

    func receiveEvents(_ urls: [URL], forceFullScan: Bool) {
        handler(urls, forceFullScan)
    }

    func stop() {
        guard let stream else { return }
        FSEventStreamStop(stream)
        FSEventStreamInvalidate(stream)
        FSEventStreamRelease(stream)
        self.stream = nil
    }

    deinit { stop() }
}
