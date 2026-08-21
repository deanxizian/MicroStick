# MicroStick Usage protocol v1

The Usage service is independent of the Codex Micro HID report map.

```text
Service UUID        BE1E47D1-4C59-4DAB-80CF-E26202B981D8
Write characteristic 27A7328B-193D-4961-9C85-CC44006E7E0D
```

The characteristic supports write-with-response and requires an encrypted connection from a bonded peer. Clients discover by service UUID, not by device name.

## Snapshot payload

Every integer is little-endian. Percentages use integer basis points: `0`–`10000`; `0xFFFF` means unavailable. Timestamps are signed Unix seconds in the inclusive range 2000-01-01 through 2100-01-01. Optional reset time uses `0` for unavailable.

| Offset | Size | Type | Meaning |
| ---: | ---: | --- | --- |
| 0 | 1 | `u8` | protocol version, currently `1` |
| 1 | 1 | flags | bit 0 = stale; all other bits must be zero |
| 2 | 2 | `u16 LE` | 7D remaining basis points or `0xFFFF` |
| 4 | 8 | `i64 LE` | 7D reset timestamp or `0` |
| 12 | 8 | `i64 LE` | snapshot update timestamp |

Total payload size is exactly 20 bytes. A reset timestamp earlier than the update timestamp is invalid. Unknown versions, unknown flags, invalid percentages, invalid timestamps, and incorrect payload lengths are rejected.

Compatibility rule: protocol v1 is frozen. A future incompatible layout uses a new protocol version and a documented decoder strategy; existing fields are never silently reordered or reinterpreted.

## ATT frames

The payload is split for the common 20-byte ATT value limit. Each frame is at most 20 bytes and has a 9-byte header plus up to 11 payload bytes.

| Offset | Size | Meaning |
| ---: | ---: | --- |
| 0 | 2 | magic `0x56 0x55` |
| 2 | 1 | frame version, currently `1` |
| 3 | 1 | rolling message ID |
| 4 | 1 | zero-based fragment index |
| 5 | 1 | fragment count; exactly `2` for the v1 snapshot |
| 6 | 1 | total payload length; exactly `20` |
| 7 | 2 | CRC-16/CCITT-FALSE of the complete payload, little-endian |
| 9 | 1–11 | fragment bytes |

Fragments may arrive out of order. The receiver accepts at most two fragments, requires consistent metadata, validates the exact expected chunk length, rejects mixed messages, and updates state only after complete CRC and payload validation. A new message ID resets an incomplete assembly.

## 7D source semantics

UsageSync considers only root Codex JSONL sessions. It ignores subagent sessions, scans bounded tails of at most the newest 40 eligible files, and selects the newest complete `token_count` event whose `rate_limits.limit_id` is absent, empty, or `codex`.

The seven-day window is the `primary` or `secondary` entry whose numeric `window_minutes` equals `10080`. Token totals are ignored. The displayed value is:

```text
remaining_percent = round_to_nearest_even(clamp(100 - used_percent, 0, 100))
remaining_basis_points = remaining_percent * 100
```

String and numeric `used_percent` values are accepted when finite. Boolean, non-finite, malformed, or fractional `window_minutes` values are rejected. Reset time is retained only when valid and not earlier than the event timestamp.

## Freshness and cache

- UsageSync marks data stale when its update time is more than 15 minutes old.
- Firmware applies the same local expiry if no fresh write arrives.
- Cache restoration always starts stale, regardless of the stored flag.
- Stale data remains visible but dimmed; it is never represented as live.
- Data changes are sent immediately. Unchanged data is confirmed by a five-minute heartbeat.
