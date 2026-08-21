# Architecture

MicroStick has three independent runtime paths:

```text
                                  BLE Vendor HID
M5Stack StickS3  <---------------------------------------------->  ChatGPT Desktop
  firmware             semantic Micro actions and host state
      |
      +---------------- USB UAC microphone -------------------->  macOS audio
      |
      +---------------- encrypted Usage GATT <-----------------  MicroStickUsageSync
                                                                  |
                                                                  `- ~/.codex/sessions
```

ChatGPT Desktop owns speech recognition, transcription, Codex input, Micro actions, and six Agent slots. The firmware owns physical input, BLE/USB transports, display, Roxy, power state, tones, and the device-side usage cache. UsageSync owns only local 7D parsing and BLE delivery.

## Firmware boundaries

| Area | Responsibility |
| --- | --- |
| `app/micro` | Semantic action API and semantic snapshots; UI/input never handle raw action strings or RPC fields. |
| `components/codex_control` | Bounded JSON/RPC compatibility codec and unknown-method responses. |
| `components/codex_transport_ble_espidf` | BLE HID transport, advertising, bonding, reconnect, Vendor reports, and keyboard cancellation fallback. |
| `app/audio` | ES8311/I2S capture, USB UAC input, tone queue, and PTT tone suppression. |
| `app/input` + `components/two_button_input` | Deterministic button recognizer and product bindings. |
| `app/usage` | Encrypted GATT write service, frame reassembly, payload validation, rollback guard, expiry, and NVS cache. |
| `components/microstick_state_model` | Agent semantics, Roxy aggregation, layout invariants, battery filtering, and formatting. |
| `app/ui` | LVGL rendering from semantic state only. |
| `app/board` | StickS3 display, PMIC, GPIO, I2C, codec, and power detection. |

`app_main.cpp` initializes the modules and copies snapshots between them. No UI path parses HID JSON, and no input path depends directly on physical `ACTxx` strings.

## Mac boundaries

The Swift package contains:

- `MicroStickUsageCore`: bounded session discovery, root/subagent classification, 7D rate-limit parsing, payload/frame codecs, and private cache.
- `MicroStickUsageBluetooth`: CoreBluetooth discovery by service UUID, reconnect/backoff state machine, write-with-response delivery, and heartbeat gate.
- `MicroStickUsageSync`: FSEvents-style change watching, low-frequency safety scan, sleep/wake recovery, native login-item lifecycle, and privacy-safe diagnostics.

UsageSync is an `LSUIElement` app registered through `SMAppService.mainApp`. It has no window or Dock icon. Idle work is event-driven; the periodic safety scan and heartbeat run every five minutes.

## State ownership

- Host Agent state remains authoritative. Unknown host payloads stay unknown rather than being assigned a guessed meaning.
- The selected Agent is a local view of the last successfully sent selection.
- Battery and USB presence come from StickS3 hardware, independent of BLE and UsageSync.
- A newly received valid usage snapshot is cached on both peers. A restored cache is always stale until fresh data arrives.
- An older usage observation cannot overwrite a newer valid snapshot.

## Failure behavior

- BLE disconnect clears pressed Micro actions and resumes advertising.
- A Mic press is always paired with release on normal release, mode cancellation, or disconnect; it is never replayed after reconnect.
- USB disconnect stops displaying USB and leaves BLE control available.
- UsageSync retries with bounded backoff and forces delivery after reconnect or wake.
- Invalid, overlong, out-of-order, unbonded, unencrypted, or checksum-failing Usage writes do not update NVS or UI.
- Audio is streamed live and is never persisted by MicroStick.
