# Protocols

MicroStick keeps the undocumented Codex Micro compatibility layer separate from its product-specific Usage service. Neither protocol is exposed directly to UI, input, audio, or usage parsing code.

## Codex Micro compatibility

### BLE identity and reports

The current host-detection baseline is:

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

These identifiers are compatibility data only. MicroStick is an independent, unofficial implementation and does not claim authorization by OpenAI or Work Louder.

Incoming JSON is reassembled in a bounded buffer with fragment-length checks. Oversized or malformed messages cannot reach UI state, and unknown RPC methods receive an error response. BLE disconnect clears pressed state and restarts advertising.

### Semantic actions

| Semantic action | Physical slot |
| --- | --- |
| Fast | `ACT06` |
| Approve | `ACT07` |
| Decline | `ACT08` |
| Fork | `ACT09` |
| Mic/PTT | `ACT10` |
| Send | `ACT12` |
| Agent 1–6 | `AG00`–`AG05` |

Click actions send complete press/release pairs. Mic sends press when the front-button 250 ms hold threshold is reached and release on physical release, cancellation, mode reset, or disconnect. A stale press is never replayed.

Navigation uses `v.oai.rad` and always returns distance to zero:

| Menu item | Angle | Press distance |
| --- | ---: | ---: |
| Plan | `0.75` | `1.0`, then `0` |
| Back | `0.50` | `1.0`, then `0` |
| Forward | `0.00` | `1.0`, then `0` |
| Sidebar | `0.25` | `1.0`, then `0` |

Labels assume the factory Micro layout. Firmware sends physical slots rather than command names, so user remapping can make an on-device label differ from the host action.

### Host state

The compatibility codec handles the required messages, including `v.oai.hid`, `v.oai.rad`, `v.oai.thstatus`, `v.oai.rgbcfg`, `sys.version`, `device.status`, `lights.preview`, and `host.focused_app`.

Six Agent slots retain host-provided ID, color, brightness, effect, and speed. Verified combinations provide conservative semantic fallbacks for Idle, Working, Complete/Unread, Awaiting input, Error, and Off. Unknown combinations remain Unknown while their raw light presentation is retained. A complete six-slot all-off batch produced by host lighting sleep does not erase the previous assignment snapshot.

The current voice-lighting compatibility observations are:

| Color | Effect | Semantic state |
| --- | ---: | --- |
| `#2E8B57` | `2` | Recording |
| `#FFFFFF` | `2` | Processing |
| `#FFFFFF` | `1` | Completed |
| `#000000` | `0` | Idle |

These are observed host behaviors, not a published protocol guarantee. Optional `v.oai.thstatus` diagnostics are disabled in release builds and never log prompts, responses, focused-app data, or session content.

Firmware displays `已写入` only after it has observed Recording or Processing for the current local voice sequence and then receives Completed. A 30-second Processing timeout returns the UI to Idle with `未确认`; it never synthesizes Completed. ChatGPT can still complete successfully with an empty transcript, so the signal confirms the host pipeline state rather than the presence of non-empty text.

### Cancellation limitation

No stable native Micro Stop action has been verified. A home-screen front-button double click sends two complete standard BLE keyboard Escape pairs to the foreground macOS application. The fallback does not use UsageSync or Accessibility permission, but it can target the wrong foreground app and may stop working after a ChatGPT Desktop update.

## Usage GATT protocol v1

The product-specific Usage service is independent of the Micro HID report map:

```text
Service UUID          BE1E47D1-4C59-4DAB-80CF-E26202B981D8
Write characteristic 27A7328B-193D-4961-9C85-CC44006E7E0D
```

The characteristic supports write-with-response and requires an encrypted connection from a bonded peer. Clients discover by service UUID rather than device name.

### Snapshot payload

Every integer is little-endian. Percentages use integer basis points from `0` through `10000`; `0xFFFF` means unavailable. Timestamps are signed Unix seconds in the inclusive range 2000-01-01 through 2100-01-01. Optional reset time uses `0` when unavailable.

| Offset | Size | Type | Meaning |
| ---: | ---: | --- | --- |
| 0 | 1 | `u8` | Protocol version, currently `1`. |
| 1 | 1 | flags | Bit 0 = stale; all other bits must be zero. |
| 2 | 2 | `u16 LE` | 7D remaining basis points or `0xFFFF`. |
| 4 | 8 | `i64 LE` | 7D reset timestamp or `0`. |
| 12 | 8 | `i64 LE` | Snapshot update timestamp. |

The payload is exactly 20 bytes. Unknown versions or flags, out-of-range percentages or timestamps, reset times earlier than the update time, and incorrect lengths are rejected. Protocol v1 is frozen; an incompatible future layout must use a new version and an explicit decoder strategy.

### ATT frames

The payload is split for the common 20-byte ATT value limit. Each frame has a 9-byte header and at most 11 payload bytes:

| Offset | Size | Meaning |
| ---: | ---: | --- |
| 0 | 2 | Magic `0x56 0x55`. |
| 2 | 1 | Frame version, currently `1`. |
| 3 | 1 | Rolling message ID. |
| 4 | 1 | Zero-based fragment index. |
| 5 | 1 | Fragment count; exactly `2` for v1. |
| 6 | 1 | Total payload length; exactly `20`. |
| 7 | 2 | CRC-16/CCITT-FALSE of the complete payload, little-endian. |
| 9 | 1–11 | Fragment bytes. |

Fragments may arrive out of order. The receiver accepts at most two fragments, requires consistent metadata and exact chunk lengths, rejects mixed messages, and updates state only after full CRC and payload validation. A new message ID resets an incomplete assembly.

### 7D source semantics

The only live source is the local Codex App Server method `account/rateLimits/read`. UsageSync launches the Codex executable embedded in ChatGPT Desktop over bounded stdio, initializes a private process, and selects `rateLimitsByLimitId.codex` or the backward-compatible `rateLimits` bucket. It never reads Codex authentication storage, `~/.codex/sessions`, or task content and does not send a direct HTTP request. The method is a current compatibility surface rather than a published REST API, so all fields remain isolated in `MicroStickUsageCodex`; failure preserves only the last valid quota-only cache.

The seven-day window is the `primary` or `secondary` entry whose numeric `windowDurationMins` is `10080`; its `usedPercent` and optional `resetsAt` are converted into the device snapshot. Other limit IDs, model-specific buckets, credits, plan data, and account metadata are ignored. A successful network observation uses receipt time as `updated_at`.

UsageSync requests a full snapshot at startup, every five minutes, after wake, and when `account/rateLimits/updated` is observed. Responses and individual lines are bounded, requests time out, process failures use bounded retry, and an unavailable or changed App Server method does not erase the last valid value.

The displayed remaining value is:

```text
remaining_percent = round_to_nearest_even(clamp(100 - used_percent, 0, 100))
remaining_basis_points = remaining_percent * 100
```

Finite string and numeric `usedPercent` values are accepted. Boolean, non-finite, malformed, or fractional `windowDurationMins` values are rejected. Reset time is retained only when valid and not earlier than the snapshot receipt time.

### Freshness and cache

- UsageSync and firmware mark data stale after 15 minutes without a successful active observation.
- Cache restoration always starts stale regardless of the stored flag.
- Stale data remains visible but dimmed and is never represented as live.
- Changes are delivered immediately; unchanged state is confirmed by a five-minute heartbeat.
