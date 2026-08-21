# Third-party audit

MicroStick is MIT licensed. The following external code or compatibility references require retained notices.

## Product foundations

MicroStick retains the MIT copyright notice for reusable board, display,
power, and product foundations by Gary Zhang. MicroStick-specific architecture,
protocol, UI, firmware, and UsageSync work adds the Zian Xi copyright line.
Keeping both lines is a license obligation, not a runtime dependency or shared
Git history.

## Codex Micro compatibility

- AgentMote by shenjingnan — MIT. Selected protocol, ESP-IDF BLE HID, and StickS3 concepts were adapted and then isolated behind MicroStick semantic interfaces. Full license: `firmware/sticks3/components/agentmote.LICENSE`.
- FreeMicro by Eli Benveniste — MIT. Used as an independent cross-check for physical slot identifiers and host messages. Full license: `firmware/sticks3/components/freemicro.LICENSE`.
- codex-micro-4-core2 by imliubo — MIT. Used as an additional compatibility reference. Full license: `firmware/sticks3/components/codex-micro-4-core2.LICENSE`.

The repository and release bundle retain all three licenses. The project NOTICE states that compatibility identifiers are not evidence of affiliation or authorization.

## USB audio

`firmware/sticks3/components/usb_device_uac` is an Espressif component under Apache License 2.0. Its `license.txt`, README, and upstream changelog remain with the component. Release bundles include its full license.

## Fonts

The generated Chinese LVGL subsets are derived from Source Han Sans CN Regular, Copyright 2014–2025 Adobe, under the SIL Open Font License 1.1. The full text is retained at `firmware/sticks3/components/source-han-sans.OFL.txt` and in the Mac release bundle.

The subset source boundary is reproducible through `firmware/sticks3/tools/generate_cn_font.py`. It includes printable ASCII, characters currently used by product UI, and a small declared reserve for future Chinese control labels.

## Managed ESP-IDF components

Versions are locked in `firmware/sticks3/dependencies.lock`, including ESP-IDF 5.5.2, LVGL 9.2.0, Espressif TinyUSB, `esp_codec_dev`, `i2c_bus`, and M5Stack M5PM1. Managed source is resolved by ESP-IDF at build time and is not committed in this repository.

## Project assets

The bundled Roxy frames are generated from the project owner's custom
`roxy-pixel` Codex pet, not from Codex's built-in pet catalog. The original
atlas is not committed or shipped. Its source checksum and derivation boundary
are recorded in [`ROXY_ASSET.md`](ROXY_ASSET.md).

The derived Roxy frames, MicroStick brand assets, layout previews, product
code, and generated glue are distributed under the repository license unless a
file-specific notice says otherwise.
