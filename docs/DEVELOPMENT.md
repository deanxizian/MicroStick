# Development

MicroStick supports Apple Silicon Macs running macOS 14 or later and M5Stack StickS3 hardware. End users install prebuilt release artifacts; ESP-IDF is required only for firmware development.

## Repository layout

```text
app/macos/             Swift UsageSync package and tests
firmware/sticks3/      ESP-IDF firmware, portable tests, and generators
scripts/               Build, flash, package, install, diagnose, and uninstall tools
docs/                  Architecture, protocols, and this development guide
```

Run `./scripts/dev.sh --help` for the supported local entry points.

## StickS3 hardware and firmware

### Supported target

- ESP32-S3-PICO-1 with 8 MB flash and 8 MB octal PSRAM.
- 135×240 ST7789V2 display.
- M5PM1 power manager and battery input.
- ES8311 codec, onboard MEMS microphone, and speaker.
- Front blue button on GPIO 11 and side rectangular button on GPIO 12.
- Native ESP32-S3 USB device peripheral.

The corner power/reset button is reserved for power, reset, and ROM-download handling; it is not an application input.

ESP-IDF is fixed at 5.5.2 through `dependencies.lock` and release scripts. Firmware uses a 240 MHz CPU, a 1 kHz FreeRTOS tick, LVGL 9.2.0 with RGB565 output, a 48 kHz/16-bit/mono UAC 2.0 input named `MicroStick Microphone`, and one 3 MiB factory application partition alongside NVS and PHY data.

The microphone streams only while macOS consumes the input. PTT is an independent BLE action and never creates a recording file. Speaker tones are suppressed while PTT is active.

### Build and test

```bash
export IDF_PATH=/path/to/esp-idf-v5.5.2
./scripts/build_firmware_release.sh
```

The release script builds the ESP-IDF application and produces:

```text
dist/firmware/MicroStick-StickS3.bin
dist/firmware/MicroStick-StickS3.bin.sha256
```

Portable component tests run separately in CI and should also be run locally when their components change.

The merged image contains bootloader at `0x0`, partition table at `0x8000`, and application at `0x10000`; flash the merged file only at `0x0`.

### Flash

Put StickS3 in ROM download mode and use either the development helper:

```bash
./scripts/dev.sh firmware-flash /dev/cu.usbmodemXXXX
```

or an ESP-IDF 5.5.2 environment directly:

```bash
python -m esptool --chip esp32s3 --port /dev/cu.usbmodemXXXX \
  write_flash 0x0 MicroStick-StickS3.bin
```

Press power/reset once if the board remains in the ROM loader. After any HID descriptor change, forget the old `Codex Micro` record in macOS and pair again.

The BLE compatibility identity uses PID `0x8360`; the independent USB microphone uses PID `0x8361` and manufacturer `MicroStick`. Physical VIN, rather than stale TinyUSB state, controls the on-screen USB indicator. Battery filtering supplies the same percentage to the LCD, standard BLE Battery Service, and Micro `device.status`.

## macOS UsageSync

The Swift package contains `MicroStickUsageCore`, `MicroStickUsageBluetooth`, `MicroStickUsageCodex`, and the windowless `MicroStickUsageSync` application. Build and test it with:

```bash
swift build --package-path app/macos
swift test --package-path app/macos
./scripts/build_usage_sync.sh --debug
```

Tests use synthetic account-rate-limit objects and cover active App Server parsing and process I/O, percentage and timestamp boundaries, freshness, cache permissions, payload encoding, fragment reassembly, delivery gating, and Bluetooth recovery. Tests never open a real Codex task session.

### Runtime installation and diagnostics

The release ZIP contains the signed/notarized arm64 app plus `install.sh`, `doctor.sh`, and `uninstall.sh`. The transactional installer stages the app in `~/Applications`, registers `SMAppService.mainApp`, launches it without a Dock icon, and restores the previous app if replacement fails.

Runtime cache and status files live under `~/Library/Application Support/MicroStick` with directory mode `0700` and file mode `0600`. The login item and process are both named `MicroStickUsageSync`.

```bash
./doctor.sh
./uninstall.sh
./uninstall.sh --purge
```

Default uninstall keeps the private cache; `--purge` removes the support directory. Diagnostics may warn when BLE or USB is intentionally disconnected and never print prompt text, responses, account identifiers, or payload bodies.

UsageSync starts a bounded stdio connection to the Codex executable embedded in ChatGPT Desktop. It requests `account/rateLimits/read` at startup, every five minutes, after wake, and after Codex rate-limit notifications. The compatibility client uses timeouts and bounded exponential recovery and never logs response bodies. It never reads `~/.codex/sessions`; when active lookup is unavailable, the last valid private cache becomes stale after 15 minutes. BLE keeps its five-minute heartbeat and reconnect behavior.

## Release packaging, signing, and notarization

Build the Apple Silicon package with a semantic version:

```bash
MICROSTICK_APP_VERSION=1.1.0 ./scripts/build_usage_sync_release.sh
```

Distribution builds require a `Developer ID Application` identity and hardened runtime. After storing App Store Connect credentials in a Keychain profile:

```bash
MICROSTICK_APP_VERSION=1.1.0 \
MICROSTICK_NOTARY_PROFILE=YOUR_PROFILE \
  ./scripts/notarize_usage_sync_release.sh
```

The notarization script submits the signed ZIP, staples the accepted ticket, rebuilds the archive around the stapled app, refreshes its SHA-256 file, and validates it with `codesign`, Gatekeeper, `stapler`, and `unzip`.

Release acceptance belongs in the GitHub Release notes and CI artifacts rather than a version-specific document on `main`. Record artifact hashes, automated test counts, notarization result, and any physical checks that could not be automated.

## Generated assets

### Roxy

Roxy frames are derived from the repository owner's custom Codex pet package:

```text
pet ID                  roxy-pixel
display name            Roxy Pixel
source atlas            ~/.codex/pets/roxy-pixel/spritesheet.webp
source atlas SHA-256    f88a7e1140a2d540d6703716981e4c715b6e5ffb1c26fac6413a8b6f07f15f7e
```

The original atlas is neither committed nor shipped. The generator validates the exact hash, crops the declared animation rows, scales them to 96×104, quantizes the palette, and emits bounded compressed frames:

```bash
python3 -m pip install Pillow
python3 firmware/sticks3/tools/generate_roxy_assets.py \
  --qa-dir docs/ui-previews \
  --preview-font /path/to/SourceHanSansCN-Regular.otf
```

The generated frames are distributed under the MicroStick MIT license. Roxy is a MicroStick product character and is not presented as an OpenAI or Work Louder built-in asset or mark.

### Fonts and previews

`firmware/sticks3/tools/generate_cn_font.py` generates bounded 12 px and 16 px LVGL subsets from Source Han Sans CN Regular. The full font is not committed; its SIL Open Font License is retained with the firmware and release package. Files under `docs/ui-previews` are deterministic development references for the 135×240 layout and do not replace physical LCD verification.

## Dependencies and notices

Managed ESP-IDF dependencies are locked in `firmware/sticks3/dependencies.lock` and resolved at build time; `managed_components` is not committed. The vendored Espressif UAC component keeps its upstream README, changelog, and Apache-2.0 license beside the source.

Compatibility work references AgentMote, FreeMicro, and codex-micro-4-core2 under MIT licenses. Source Han Sans uses the SIL Open Font License. Copyright, license locations, product-foundation attribution, and the unofficial compatibility statement are maintained in the root [NOTICE](../NOTICE) and the component license files.
