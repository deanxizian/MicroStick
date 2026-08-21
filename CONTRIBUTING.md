# Contributing

MicroStick has two product areas:

- `firmware/sticks3/`: ESP-IDF 5.5.2 firmware for M5Stack StickS3.
- `app/macos/`: Apple Silicon Swift package for 7D usage synchronization.

Before opening a change, run the relevant tests:

```bash
swift test --package-path app/macos

export IDF_PATH=/path/to/esp-idf-v5.5.2
./script/build_firmware_release.sh
```

Portable C tests live beside their components and run in CI. Keep protocol parsing bounded, avoid logging session paths or conversation content, and add fixtures for every compatibility inference. Changes to the BLE HID descriptor require macOS to forget and re-pair the device. Changes to the Usage payload require an explicit protocol-version decision and updates on both Swift and firmware sides.

MicroStick supports only Apple Silicon, macOS 14+, and M5Stack StickS3. Do not add network services, cloud speech dependencies, telemetry, credential access, or Mac-side input injection.

Contributions derived from external projects must retain their license and NOTICE requirements. Do not include real Codex sessions, secrets, recordings, or account data.

Roxy generation additionally requires Pillow and the exact custom atlas listed
in `docs/ROXY_ASSET.md`:

```bash
python3 -m pip install Pillow
python3 firmware/sticks3/tools/generate_roxy_assets.py \
  --qa-dir docs/ui-previews \
  --preview-font /path/to/SourceHanSansCN-Regular.otf
```

The source atlas and full font are deliberately not committed. Do not replace
either asset without documenting provenance, checksum, and redistribution
terms first.
