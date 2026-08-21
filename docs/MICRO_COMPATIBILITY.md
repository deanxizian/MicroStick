# Codex Micro compatibility

MicroStick implements an undocumented host protocol. The compatibility layer is isolated so product UI, input, audio, and usage code depend only on semantic APIs.

## BLE identity and reports

The current compatibility baseline is:

```text
Device name   Codex Micro
Manufacturer  Work Louder
Vendor ID     0x303A
Product ID    0x8360
Usage page    0xFF00
Report ID     6
Input report  63 bytes
Output report 63 bytes
```

The identity is used solely for host detection. MicroStick remains an independent, unofficial device.

Incoming JSON is reassembled in a bounded buffer with strict fragment-length checks. Oversized, malformed, or unknown requests do not reach UI state; unknown RPC methods receive an error response. BLE disconnect clears state and restarts advertising.

## Semantic actions

The firmware sends complete press/release pairs for click actions:

| Semantic action | Physical slot |
| --- | --- |
| Fast | `ACT06` |
| Approve | `ACT07` |
| Decline | `ACT08` |
| Fork | `ACT09` |
| Mic/PTT | `ACT10` |
| Send | `ACT12` |
| Agent 1–6 | `AG00`–`AG05` |

Mic is the exception to an immediate click pair: the device sends press when the 250 ms hold threshold is reached and release when the button is released or the action is cancelled. A stale press is never replayed after reconnect.

Navigation uses `v.oai.rad` and always returns the joystick to distance zero:

| Menu item | Angle | Press distance |
| --- | ---: | ---: |
| Plan | `0.75` | `1.0`, then `0` |
| Back | `0.50` | `1.0`, then `0` |
| Forward | `0.00` | `1.0`, then `0` |
| Sidebar | `0.25` | `1.0`, then `0` |

These labels assume the factory/default Micro layout. The firmware sends physical slots, not command names. User remapping can therefore change the action behind an on-device label.

## Host state

The compatibility codec handles the required host messages, including `v.oai.hid`, `v.oai.rad`, `v.oai.thstatus`, `v.oai.rgbcfg`, `sys.version`, `device.status`, `lights.preview`, and `host.focused_app`.

Six Agent slots preserve the host-provided ID, color, brightness, effect, and speed. Verified colors provide a conservative semantic fallback for Idle, Working, Complete/Unread, Awaiting input, Error, and Off. Unknown combinations remain Unknown while the raw light presentation is retained.

Optional serial diagnostics for `v.oai.thstatus` are disabled in release builds. When explicitly enabled, they log only the light-status RPC needed for compatibility fixtures, never prompts, responses, focused-app payloads, or session content.

## Cancellation limitation

No stable native Micro Stop action has been verified. A home-screen front-button double click therefore sends two complete standard BLE keyboard Escape pairs to the foreground macOS application. Current ChatGPT Desktop uses that sequence to expose and confirm cancellation.

This fallback:

- does not involve the Mac UsageSync process or Accessibility permission;
- is not a Micro Vendor action;
- can be consumed by the wrong foreground application;
- may stop working after a ChatGPT Desktop behavior change.

MicroStick does not claim a native Stop capability until a stable host action is observed and tested.
