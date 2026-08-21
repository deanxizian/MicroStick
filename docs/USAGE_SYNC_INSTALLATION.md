# MicroStickUsageSync installation

`MicroStickUsageSync` is the only MicroStick process on macOS. It is a windowless, Apple Silicon `LSUIElement` application registered as a native login item. It reads local 7D rate-limit fields and sends a compact snapshot over BLE; it does not handle audio, actions, Agent state, or ChatGPT credentials.

## Release artifact

The release archive is named:

```text
MicroStickUsageSync-v1.0.0-macos-arm64.zip
```

It contains the signed/notarized app, `install.sh`, `uninstall.sh`, documentation, license, and notices. Firmware is a separate release artifact.

## Install

```bash
unzip MicroStickUsageSync-v1.0.0-macos-arm64.zip
cd MicroStickUsageSync-v1.0.0-macos-arm64
./install.sh
```

The transactional installer:

1. requires Apple Silicon and verifies the app signature and architecture;
2. stages the app in `~/Applications`;
3. registers `MicroStickUsageSync.app` through `SMAppService.mainApp`;
4. launches the app without a Dock icon;
5. restores the previous MicroStickUsageSync app if replacement, registration, or launch fails.

If macOS reports `requires-approval`, enable `MicroStickUsageSync` in System Settings → General → Login Items. The app may also prompt once for Bluetooth access. Its visible login-item name and actual process name are both `MicroStickUsageSync`.

The app stores private files in:

```text
~/Library/Application Support/MicroStick/
```

The directory is mode `0700`; cache files are mode `0600`.

## Pairing and first delivery

1. Pair the firmware's `Codex Micro` BLE device in macOS.
2. Keep Bluetooth enabled and run `./doctor.sh` from the extracted release.
3. UsageSync discovers the dedicated MicroStick service UUID, attaches to the existing connection when possible, writes two frames with response, and records delivery status.
4. A restored cache is stale until the first current snapshot is delivered.

UsageSync reacts to session file changes with a 750 ms debounce. It also performs a five-minute safety scan/heartbeat and recovers after Bluetooth loss, device restart, Mac sleep, and wake.

## Diagnostics

```bash
./doctor.sh
```

The doctor checks macOS/architecture, ChatGPT Desktop, app signature, login-item state, exact process path, delivery-status file, BLE presence, and USB audio presence. Bluetooth or USB can legitimately be a warning while the device is disconnected.

Unified logs use the subsystem:

```text
com.deanxizian.microstick.usage-sync
```

Diagnostics never include a Codex session path, prompt, response, account identifier, or payload body.

## Uninstall

```bash
./uninstall.sh
./uninstall.sh --purge
```

Default uninstall unregisters the login item, stops the process, removes the app, and retains usage cache/status files. `--purge` additionally removes the complete MicroStick support directory.

## Signing and notarization

Release builds require a `Developer ID Application` identity and hardened runtime:

```bash
./script/build_usage_sync_release.sh
MICROSTICK_NOTARY_PROFILE=YOUR_PROFILE \
  ./script/notarize_usage_sync_release.sh
```

The notarization script submits the signed ZIP, staples the accepted ticket,
rebuilds the archive around the stapled app, refreshes its SHA-256 file, and
validates it with `codesign`, Gatekeeper, `stapler`, and `unzip`.
