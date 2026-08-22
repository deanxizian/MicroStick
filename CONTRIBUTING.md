# Contributing

MicroStick has two product areas: ESP-IDF 5.5.2 firmware under `firmware/sticks3` and an Apple Silicon Swift UsageSync package under `app/macos`. Read [Development](docs/DEVELOPMENT.md) before changing either area.

Run the relevant checks before opening a change:

```bash
swift test --package-path app/macos

export IDF_PATH=/path/to/esp-idf-v5.5.2
./scripts/build_firmware_release.sh
```

Keep protocol parsing bounded, add redacted fixtures for compatibility inferences, and preserve the semantic boundary between product code and undocumented Micro identifiers. HID descriptor changes require macOS to forget and re-pair the device; Usage payload changes require an explicit version decision on both Swift and firmware sides.

MicroStick supports only Apple Silicon, macOS 14+, and M5Stack StickS3. Do not add network services, cloud speech, telemetry, credential access, stored recordings, or Mac-side input injection.

Report security issues privately to the repository owner rather than opening a public issue. Never attach or commit a real Codex session, secret, recording, account identifier, prompt, or response.

Externally derived code and assets must retain their license and NOTICE requirements. Do not replace Roxy, fonts, or compatibility sources without documenting provenance, checksum, and redistribution terms as described in [Development](docs/DEVELOPMENT.md#generated-assets).
