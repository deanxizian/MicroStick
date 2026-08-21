# Security policy

Security fixes target the current MicroStick v1 release.

Report vulnerabilities privately to the repository owner before opening a public issue. Include the affected commit, reproduction steps, and whether physical access, a paired BLE peer, or local user access is required. Do not attach real Codex sessions, credentials, recordings, or conversation content.

## Trust boundaries

- Codex Micro HID and Usage GATT share the StickS3 BLE peripheral, but Usage writes require an encrypted connection from a bonded peer.
- `MicroStickUsageSync` reads the current user's `~/.codex/sessions` and extracts only the newest eligible 7D rate-limit fields.
- UsageSync makes no network request, exposes no listener, sends no telemetry, and handles no audio.
- The USB microphone streams live audio to the host and does not persist it.
- The front-button cancellation fallback is a standard BLE keyboard Escape sequence delivered to the foreground app; it is not a verified Micro Vendor action.

## Sensitive-data rules

- Never log prompt, response, account, session path, or full JSONL content.
- Never commit a real session fixture or signing credential.
- Keep cache and runtime-status files private to the current user.
- Treat the undocumented Micro protocol as untrusted input: enforce report size, fragment count, JSON bounds, field types, and unknown-RPC responses.
