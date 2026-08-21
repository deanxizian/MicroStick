# Hardware and firmware

## Supported target

MicroStick supports M5Stack StickS3 only:

- ESP32-S3-PICO-1, 8 MB flash, 8 MB octal PSRAM.
- 135×240 ST7789V2 display.
- M5PM1 power manager and battery input.
- ES8311 codec with onboard MEMS microphone and speaker.
- Front blue button on GPIO 11 and side rectangular button on GPIO 12.
- USB device connection through the ESP32-S3 native USB peripheral.

The corner power/reset button is reserved for power, reset, and ROM-download handling; it is not an application input.

## Firmware configuration

- ESP-IDF is fixed at 5.5.2 through `dependencies.lock` and release scripts.
- CPU: 240 MHz; FreeRTOS tick: 1 kHz.
- Display: LVGL 9.2.0, RGB565, product-generated Roxy and bounded Chinese font assets.
- USB input: UAC 2.0, 48 kHz, 16-bit, mono, product name `MicroStick Microphone`.
- BLE: Codex Micro-compatible Vendor HID, standard keyboard collection for the cancellation fallback, Battery Service, and a separate encrypted Usage service.
- Partition table: 24 KiB NVS, 4 KiB PHY data, and one 3 MiB factory application partition.

The microphone stream is continuous while macOS has the input open. PTT is an independent BLE action; pressing Mic does not create a file. Speaker tones are stopped or dropped while PTT is active to avoid feedback into the microphone.

## Build

```bash
export IDF_PATH=/path/to/esp-idf-v5.5.2
./script/build_firmware_release.sh
```

The script builds a release configuration and merges these regions into `dist/firmware/MicroStick-StickS3.bin`:

```text
0x0000   bootloader
0x8000   partition table
0x10000  application
```

It also writes `MicroStick-StickS3.bin.sha256`. The merged image must be flashed at offset `0x0`; do not flash it again at the application offset.

## Flashing

Put StickS3 in ROM download mode, identify the serial port, then use an ESP-IDF 5.5.2 environment:

```bash
python -m esptool --chip esp32s3 --port /dev/cu.usbmodemXXXX \
  write_flash 0x0 MicroStick-StickS3.bin
```

For development builds:

```bash
./scripts/dev.sh firmware-flash /dev/cu.usbmodemXXXX
```

After a successful write, press the power/reset button once if the board remains in the ROM loader.

## Pairing and USB

BLE advertises as `Codex Micro` for ChatGPT Desktop compatibility. The compatible identity baseline uses Vendor ID `0x303A`, Product ID `0x8360`, Vendor Usage Page `0xFF00`, Report ID `6`, and 63-byte input/output reports. These identifiers are compatibility data and do not imply authorization by their owners.

The USB microphone uses VID `0x303A`, PID `0x8361`, manufacturer `MicroStick`, and a separate audio descriptor. USB and BLE are operationally independent. Physical VIN is the source of truth for the on-screen USB indicator, so an unplugged battery-powered device does not remain labeled USB because of stale TinyUSB state.

If a firmware change modifies any HID descriptor, forget the existing `Codex Micro` record in macOS Bluetooth settings and pair again.

## Battery behavior

The board reads VBAT and charge state from M5PM1. A bounded filter suppresses one-percent jitter and prevents implausible upward movement while discharging. USB/charge power can move the filtered value upward gradually. The percentage text remains white; charging is represented separately by the battery fill and lightning mark.

The same filtered percentage seeds Micro `device.status` before BLE advertising and updates both the standard Battery Service value and its notifications. ChatGPT and the StickS3 screen therefore consume the same reading instead of allowing the host to retain the compatibility layer's initial `100%` placeholder.
