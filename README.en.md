# MicroStick

MicroStick v1.3.0 turns an M5Stack StickS3 into an unofficial Codex Micro-compatible controller for ChatGPT Desktop. Buttons and six Agent slots communicate directly over BLE Vendor HID. The built-in microphone appears on macOS as a USB UAC input. The only Mac background component, `MicroStickUsageSync`, actively reads current 7D remaining usage through the local Codex App Server and sends it to the device over an encrypted, product-specific BLE GATT service.

> MicroStick is an independent open-source compatibility implementation. It is not affiliated with, authorized by, or endorsed by OpenAI or Work Louder. The Codex Micro protocol is undocumented and may change with ChatGPT Desktop updates.

[中文](README.md)

![MicroStick product render](docs/microstick-v1-product-render.png)

## Features

- Codex Micro-compatible BLE HID for Mic, Send, Approve, Decline, Fast, Fork, Agents 1–6, and four navigation actions.
- Six host-driven Agent slots with native color/effect data, `AG1–AG6` labels, and aggregated Roxy animations.
- A 48 kHz, 16-bit, mono USB input named `MicroStick Microphone`.
- Local battery/charging, BLE/USB state, Roxy animation, tones, and 7D usage.
- Private usage caches on the Mac and in StickS3 NVS; expired values remain visible but dimmed.
- Apple Silicon and macOS 14+ only; M5Stack StickS3 is the only supported board.

The runtime opens no network port, uploads no recording, calls no cloud ASR, injects no text, and requires no Accessibility permission. Quota reads are performed by the already signed-in local Codex process against OpenAI; ChatGPT Desktop owns speech recognition, transcription, and Codex input.

## Controls

On the home screen:

| Input | Action |
| --- | --- |
| Front button short press | Send after the 250 ms double-click window |
| Front button double click | Send two Escape key pairs from the device to request cancellation in the foreground ChatGPT app; this is not a native Micro action |
| Front button held for 250 ms | Mic press; release sends Mic release |
| Side button short press | Select the next assigned Agent |
| Side button held for 500 ms | Open Control Center |

Control Center order is `Approve / Decline / Fast / Fork / Agents / Navigation / Usage / Device`. Front short/long selects the next/previous item; side short executes; side long goes back. Decline opens a confirmation screen: press the side button again to confirm, or press the front button/wait for timeout to cancel. Menus close after eight seconds of inactivity.

Navigation exposes Plan, Back, Forward, and Sidebar from the factory Micro layout. Reset the layout in ChatGPT → Settings → Codex Micro before use. Custom remapping can make on-device labels differ from host actions.

## Installation

Each GitHub Release contains two independent artifacts:

- `MicroStick-StickS3.bin`: merged bootloader, partition table, and application image.
- `MicroStickUsageSync-v1.3.0-macos-arm64.zip`: signed and notarized Apple Silicon background component.

1. Put StickS3 in ROM download mode and flash `MicroStick-StickS3.bin` at offset `0x0`. Press power/reset once after flashing if needed.
2. Pair `Codex Micro` in macOS Bluetooth settings. Forget an older pairing first if its HID descriptor differs.
3. Confirm that ChatGPT Desktop is signed in. Extract the UsageSync ZIP and run `./install.sh` in Terminal. Enable `MicroStickUsageSync` under System Settings → General → Login Items and allow Bluetooth access if prompted.
4. Reset the Codex Micro layout in ChatGPT. Select `MicroStick Microphone` in ChatGPT when USB audio is desired.

BLE controls continue to work after USB is unplugged; ChatGPT can use the currently selected Mac microphone instead.

Run `./doctor.sh` from the extracted release directory for diagnostics. Uninstall with `./uninstall.sh`; add `--purge` to remove the private support directory and usage cache.

## Building from source

Use an Apple Silicon Mac with macOS 14+, Swift 5.9+, and ESP-IDF 5.5.2:

```bash
swift test --package-path app/macos
./scripts/build_usage_sync.sh --debug

export IDF_PATH=/path/to/esp-idf-v5.5.2
./scripts/build_firmware_release.sh
```

End-user installation does not download ESP-IDF; the toolchain is only needed for firmware development.

## Security and privacy

- UsageSync reads current limits through the local Codex App Server method `account/rateLimits/read`; it never reads or stores Codex credentials, cookies, tokens, account metadata, or reset-credit details.
- UsageSync does not read `~/.codex/sessions` or any task content. If active reads are unavailable, it keeps only the last valid usage snapshot and dims it after expiry.
- Usage writes require a bonded, encrypted BLE connection and use write-with-response, bounded fragments, length validation, and CRC.
- No direct calls to unpublished HTTP endpoints, telemetry, or stored audio. Active quota networking is delegated to the user's already authenticated Codex process.
- The Escape cancellation fallback targets the foreground application; use it only while ChatGPT is frontmost.

Developer documentation is consolidated into [Architecture](docs/ARCHITECTURE.md), [Protocols](docs/PROTOCOLS.md), and [Development](docs/DEVELOPMENT.md). See [LICENSE](LICENSE) and [NOTICE](NOTICE) for licensing.
