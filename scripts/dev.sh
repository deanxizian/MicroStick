#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FIRMWARE_DIR="$ROOT_DIR/firmware/sticks3"
BUILD_DIR="${MICROSTICK_FIRMWARE_BUILD_DIR:-$FIRMWARE_DIR/build-release-5.5.2}"

usage() {
  echo "usage: scripts/dev.sh [swift-test|usage-build|usage-package|firmware-build|firmware-flash PORT|doctor]"
}

case "${1:-}" in
  swift-test)
    exec swift test --package-path "$ROOT_DIR/app/macos"
    ;;
  usage-build)
    exec "$ROOT_DIR/script/build_usage_sync.sh" --debug
    ;;
  usage-package)
    exec "$ROOT_DIR/script/build_usage_sync_release.sh"
    ;;
  firmware-build)
    exec "$ROOT_DIR/script/build_firmware_release.sh"
    ;;
  firmware-flash)
    port="${2:?firmware-flash requires a serial PORT}"
    if [[ -z "${IDF_PATH:-}" || ! -f "$IDF_PATH/export.sh" ]]; then
      echo "Set IDF_PATH to ESP-IDF 5.5.2." >&2
      exit 1
    fi
    # shellcheck disable=SC1090
    source "$IDF_PATH/export.sh" >/dev/null
    exec idf.py -C "$FIRMWARE_DIR" -B "$BUILD_DIR" -p "$port" flash monitor
    ;;
  doctor)
    exec "$ROOT_DIR/scripts/doctor.sh"
    ;;
  -h|--help|"")
    usage
    ;;
  *)
    usage >&2
    exit 64
    ;;
esac
