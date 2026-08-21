# MicroStick macOS components

This Swift package targets Apple Silicon and macOS 14+.

- `MicroStickUsageCore`: bounded Codex session scanner, 7D rate-limit parser, snapshot/frame codec, and private cache.
- `MicroStickUsageBluetooth`: service discovery, encrypted write-with-response delivery, bounded retry, reconnect, and heartbeat logic.
- `MicroStickUsageSync`: change watcher, sleep/wake recovery, runtime status, and native login-item lifecycle.

UsageSync has no window, Dock icon, network service, audio path, input injection, Agent observer, or cloud request.

```bash
swift build
swift test
../../script/build_usage_sync.sh --debug
../../script/build_usage_sync_release.sh
```

The package contains fixture-only tests for malformed JSONL, file rotation, root/subagent selection, stale boundaries, cache permissions, payload encoding, fragment reassembly, delivery gating, and Bluetooth recovery. The real-session check is opt-in and prints only the expected 7D percentage and freshness:

```bash
MICROSTICK_REAL_SESSION_PARITY=1 \
MICROSTICK_EXPECT_7D=75 \
MICROSTICK_EXPECT_STALE=0 \
swift test --filter RealSessionParityTests
```

Never attach or commit a real session file.
