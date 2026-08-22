# Changelog

## v1.1.0 — 2026-08-22

- Preserve Agent assignment when ChatGPT puts the six-slot lighting model to sleep, and decode the current numeric lighting effects.
- Make only awaiting-input slots breathe and keep host activity from waking the display.
- Add 100%/50%/20% backlight levels for active, one-minute idle, and five-minute idle states.
- Keep the backlight off until the first LVGL frame is ready to prevent startup artifacts.
- Add host-confirmed voice preparation, recording, processing, and completion feedback; reject overlapping voice requests and never synthesize `已写入` from a local timer.
- Consolidate release tooling under `scripts/` and reduce project documentation to architecture, protocols, and development references.

## v1.0.0 — 2026-08-21

- Initial MicroStick release for M5Stack StickS3 and Apple Silicon Macs.
- Add Codex Micro-compatible BLE Vendor HID actions and six host-driven Agent slots.
- Add 48 kHz, 16-bit, mono USB UAC microphone support.
- Add the unified Roxy home screen, battery/charging state, Agent status, Control Center, voice overlay, and 7D usage display.
- Add two-button input for Send, cancellation fallback, PTT, Agent selection, Approve, Decline confirmation, Fast, Fork, and navigation.
- Add the native `MicroStickUsageSync` login item with bounded Codex session scanning, private cache, encrypted BLE GATT delivery, reconnect, sleep/wake recovery, and heartbeat.
- Add versioned 20-byte 7D usage payloads with two-frame reassembly, CRC, validation, NVS recovery, and local expiry protection.
- Add Apple Silicon release packaging, Developer ID signing/notarization support, ESP-IDF 5.5.2 firmware builds, CI, portable firmware tests, Swift tests, documentation, and license notices.
