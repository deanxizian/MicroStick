# Roadmap

## v1.0 release gate

- Build all six portable firmware test suites and the ESP-IDF 5.5.2 release image.
- Run Swift build/tests, shell syntax checks, deterministic asset checks, and a real local 7D comparison.
- Sign, notarize, staple, package, and Gatekeeper-assess `MicroStickUsageSync`.
- Flash the exact release image and record its application/merged SHA-256 values.
- Recheck pairing, Mic, Send, cancellation, Agent selection, Control Center actions, USB removal, battery/charging presentation, Usage delivery, disconnect recovery, sleep/wake, and uninstall on real hardware.
- Run a combined BLE + USB UAC + Roxy soak and record task-stack/audio/display observations.

## Candidate follow-ups

- Replace the Escape cancellation fallback if a stable native Micro Stop action is verified.
- Expand host-state fixtures when additional `v.oai.thstatus` payloads are captured without sensitive content.
- Add a user-friendly firmware flashing path that does not enlarge or complicate the runtime app.
- Add automated screenshot comparison for all 135×240 UI states.
- Add another board only if it can preserve the same explicit board, audio, display, and power boundaries.

## Out of scope

- Intel Macs or macOS earlier than 14.
- Boards other than M5Stack StickS3 for v1.
- Network listeners, remote usage APIs, cloud speech services, stored recordings, telemetry, browser credential access, or Mac-side input injection.
- Claiming official OpenAI or Work Louder device status.
