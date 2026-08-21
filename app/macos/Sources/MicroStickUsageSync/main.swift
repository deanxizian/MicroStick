import AppKit
import Foundation
import ServiceManagement

#if !arch(arm64)
#error("MicroStick supports Apple Silicon Macs only.")
#endif

private func printStatus(_ status: SMAppService.Status) {
    let value: String
    switch status {
    case .notRegistered:
        value = "not-registered"
    case .enabled:
        value = "enabled"
    case .requiresApproval:
        value = "requires-approval"
    case .notFound:
        value = "not-found"
    @unknown default:
        value = "unknown"
    }
    print(value)
}

private func failRegistration(_ message: String, code: Int32 = 1) -> Never {
    FileHandle.standardError.write(Data("\(message)\n".utf8))
    exit(code)
}

private let loginItem = SMAppService.mainApp
private let command = CommandLine.arguments.dropFirst().first

switch command {
case "--register-only":
    do {
        switch loginItem.status {
        case .notRegistered, .notFound:
            try loginItem.register()
        case .enabled, .requiresApproval:
            break
        @unknown default:
            failRegistration("macOS returned an unknown login-item status.")
        }
        printStatus(loginItem.status)
        exit(loginItem.status == .enabled ? 0 : 3)
    } catch {
        failRegistration("Could not register MicroStickUsageSync: \(error.localizedDescription)")
    }
case "--unregister":
    do {
        switch loginItem.status {
        case .enabled, .requiresApproval:
            try loginItem.unregister()
        case .notRegistered, .notFound:
            break
        @unknown default:
            try loginItem.unregister()
        }
        printStatus(loginItem.status)
        exit(0)
    } catch {
        failRegistration("Could not unregister MicroStickUsageSync: \(error.localizedDescription)")
    }
case "--status":
    printStatus(loginItem.status)
    exit(0)
case nil:
    break
default:
    failRegistration("usage: MicroStickUsageSync [--register-only|--unregister|--status]", code: 64)
}

private final class UsageSyncApplicationDelegate: NSObject, NSApplicationDelegate {
    private let controller = UsageSyncController()

    func applicationDidFinishLaunching(_ notification: Notification) {
        controller.start()
    }

    func applicationWillTerminate(_ notification: Notification) {
        controller.stop()
    }
}

let application = NSApplication.shared
private let applicationDelegate = UsageSyncApplicationDelegate()
application.delegate = applicationDelegate
application.setActivationPolicy(.prohibited)
application.run()
