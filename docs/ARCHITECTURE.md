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
                                                                  `- local Codex app-server
                                                                           |
                                                                           `- OpenAI quota read
```

ChatGPT Desktop owns speech recognition, transcription, Codex input, Micro actions, and the six Agent slots. Firmware owns physical input, BLE and USB transports, display, Roxy, power state, tones, and the device-side usage cache. `MicroStickUsageSync` owns only active 7D acquisition, bounded parsing, private caching, and BLE delivery. It never reads Codex task sessions.

## Responsibility boundaries

| Area | Responsibility |
| --- | --- |
| `app/micro` | Semantic Micro actions and snapshots; UI and input never handle raw action strings or RPC fields. |
| `components/codex_control` | Bounded JSON/RPC compatibility codec and unknown-method responses. |
| `components/codex_transport_ble_espidf` | BLE HID transport, advertising, bonding, reconnect, Vendor reports, and the keyboard cancellation fallback. |
| `app/audio` | ES8311/I2S capture, USB UAC input, tone queue, and PTT tone suppression. |
| `app/input` and `components/two_button_input` | Deterministic button recognition and product bindings. |
| `app/usage` | Encrypted GATT service, frame reassembly, validation, expiry, rollback protection, and NVS cache. |
| `components/microstick_state_model` | Agent semantics, Roxy aggregation, layout invariants, battery filtering, backlight policy, and formatting. |
| `app/ui` | LVGL rendering from semantic state only. |
| `app/board` | StickS3 display, PMIC, GPIO, I2C, codec, and power detection. |

`app_main.cpp` initializes these modules and copies semantic snapshots between them. Undocumented compatibility details remain inside the Micro codec and transport layers. See [Protocols](PROTOCOLS.md) for the wire formats.

The Swift package has four targets:

- `MicroStickUsageCore`: usage snapshots, frame codecs, freshness rules, and private cache.
- `MicroStickUsageBluetooth`: CoreBluetooth discovery, reconnect/backoff, write-with-response delivery, and heartbeat gating.
- `MicroStickUsageCodex`: Codex executable discovery, bounded stdio JSON, active account-rate-limit parsing, timeout, and recovery.
- `MicroStickUsageSync`: five-minute refresh, freshness enforcement, sleep/wake recovery, diagnostics, and the native login-item lifecycle.

UsageSync is a windowless `LSUIElement` app registered through `SMAppService.mainApp`. It has no Dock icon, audio path, input injection, or HTTP listener. It does not handle credentials or implement a direct OpenAI HTTP client: the authenticated local Codex App Server owns the outbound account-rate-limit request.

## State ownership and recovery

- Host Agent state is authoritative. Unknown host payloads remain unknown instead of being assigned a guessed meaning.
- A complete all-off lighting batch caused by host inactivity is presentation sleep, not an Agent assignment update; firmware preserves the last valid six-slot snapshot.
- The selected Agent follows the last successfully sent device selection and an unambiguous host selection inferred from a complete six-slot effect frame; ambiguous host effects leave the previous selection unchanged.
- Battery percentage, charging, and USB presence come from StickS3 hardware and are independent of BLE and UsageSync. Battery percentage uses the M5Unified-compatible voltage estimate before MicroStick filtering.
- A valid usage snapshot is cached on the Mac and in NVS. Restored data remains visible but starts stale until a fresh delivery arrives.
- BLE disconnect releases pressed actions, cancels local input state, and restarts advertising. A stale Mic press is never replayed after reconnect.
- USB removal stops the USB indicator and audio stream without disabling BLE control.
- Invalid, overlong, out-of-order, unbonded, unencrypted, or checksum-failing Usage writes cannot update NVS or UI.

## Product input and UI

### Home screen and power

The 135×240 home screen combines connection state, local battery, Roxy, the selected `AG1–AG6` slot, six host-colored status dots, active count, and 7D remaining usage. USB replaces the BLE label only while physical USB power is present. The Usage detail page exposes freshness; the home screen keeps expired values dimmed without an extra stale caption.

On battery, backlight starts at 100%, falls to 50% after one minute without a physical button press, and falls to 20% after five minutes. A device button press restores 100%; host Agent, lighting, Usage, BLE, or USB updates do not reset the idle timer. External power keeps the backlight at 100%.

### Buttons

| Input | Home-screen behavior |
| --- | --- |
| Front button short press | Send after the 250 ms double-click window. |
| Front button double click | Send two complete Escape pairs as the cancellation fallback. |
| Front button held for 250 ms | Send Mic press; release sends Mic release. |
| Side button short press | Select the next assigned Agent. |
| Side button held for 500 ms | Open Control Center at `Approve`. |

Control Center order is `Approve / Decline / Fast / Fork / Agents / Navigation / Usage / Device`. Front short/long selects the next/previous item; side short executes; side long returns. Decline requires a second side-button press; the front button or timeout cancels. Menus close after eight seconds of inactivity.

### Agent and Roxy state

The six dots retain the host-provided color, brightness, effect, and speed. Unassigned slots are hollow and dark, the selected slot has an outline, and only an awaiting-approval/response slot breathes. Active count includes Working, Awaiting approval, and Awaiting response, but excludes Idle, Off, and Complete/Unread.

Roxy aggregates all six slots in this order:

```text
Error > Awaiting input > Working > Complete/Unread > Idle > BLE offline
```

Complete is held briefly so it remains visible, then follows later host state.

### Voice feedback

| UI state | Source and meaning |
| --- | --- |
| `正在准备` | Local state immediately after a successful Mic press; ChatGPT has not confirmed recording yet. |
| `正在聆听` | Shown only after a recognized ChatGPT Recording lighting payload. |
| `正在识别` | Entered locally after Mic release and also refreshed by a recognized host Processing payload. |
| `已写入` | Entered only after a recognized host Completed payload, following host-confirmed Recording or Processing. |

Completed feedback is held for one second. If no terminal host state arrives within 30 seconds of Processing, firmware returns to Idle and displays `未确认`; a timeout never claims that text was written. While a sequence is preparing, recording, processing, or completing, another Mic hold is rejected with `请等待处理完成`; its later release is ignored. Double-click Escape remains available for cancellation. PTT suppresses active and queued local tones, and MicroStick never stores audio or performs transcription itself.

### Tones

- BLE connection: short rising two-tone cue.
- Successful command: short high cue.
- Cancellation, unavailable command, or failure: short low cue.

## Security and privacy boundaries

- Usage writes require a bonded, encrypted BLE connection and bounded, versioned frames.
- UsageSync projects only the `codex` 7D window from the App Server response. It never reads credentials, `~/.codex/sessions`, or task content, and never logs response bodies, account metadata, or reset-credit details.
- When active lookup is unavailable, the last valid quota-only cache remains visible and becomes stale after 15 minutes.
- MicroStick stores no recording, opens no network listener, sends no telemetry, and reads no browser cookie or ChatGPT credential.
- The undocumented Micro protocol is treated as untrusted input: report lengths, fragments, JSON fields, and unknown RPC methods are validated before reaching product state.
- The Escape fallback targets the foreground application and is not a native Micro Stop acknowledgement.
