#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FIRMWARE_DIR="$ROOT_DIR/firmware/sticks3"
BUILD_DIR="${MICROSTICK_FIRMWARE_BUILD_DIR:-$FIRMWARE_DIR/build-release-5.5.2}"
OUTPUT_DIR="${MICROSTICK_FIRMWARE_OUTPUT_DIR:-$ROOT_DIR/dist/firmware}"
OUTPUT_IMAGE="$OUTPUT_DIR/MicroStick-StickS3.bin"
EXPECTED_IDF_VERSION="5.5.2"

if [[ -z "${IDF_PATH:-}" || ! -f "$IDF_PATH/export.sh" ]]; then
  echo "Set IDF_PATH to an ESP-IDF ${EXPECTED_IDF_VERSION} checkout." >&2
  exit 1
fi

# shellcheck disable=SC1090
source "$IDF_PATH/export.sh" >/dev/null
actual_version="$(idf.py --version | /usr/bin/awk '{print $NF}')"
case "$actual_version" in
  "v${EXPECTED_IDF_VERSION}"*|"${EXPECTED_IDF_VERSION}"*) ;;
  *)
    echo "ESP-IDF ${EXPECTED_IDF_VERSION} is required; found ${actual_version}." >&2
    exit 1
    ;;
esac

mkdir -p "$BUILD_DIR" "$OUTPUT_DIR"
idf.py -C "$FIRMWARE_DIR" -B "$BUILD_DIR" \
  -D "SDKCONFIG=$BUILD_DIR/sdkconfig.release" build

python -m esptool --chip esp32s3 merge_bin \
  --flash_mode dio --flash_freq 80m --flash_size 8MB \
  -o "$OUTPUT_IMAGE" \
  0x0 "$BUILD_DIR/bootloader/bootloader.bin" \
  0x8000 "$BUILD_DIR/partition_table/partition-table.bin" \
  0x10000 "$BUILD_DIR/microstick_sticks3.bin"

image_digest="$(/usr/bin/shasum -a 256 "$OUTPUT_IMAGE" | /usr/bin/awk '{print $1}')"
printf '%s  %s\n' "$image_digest" "$(basename "$OUTPUT_IMAGE")" \
  > "$OUTPUT_IMAGE.sha256"

echo "Firmware image: $OUTPUT_IMAGE"
echo "SHA-256: $image_digest"
