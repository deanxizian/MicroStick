#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST_DIR="$ROOT_DIR/dist"
VERSION="${MICROSTICK_APP_VERSION:-1.2.0}"
PACKAGE_NAME="MicroStickUsageSync-v${VERSION}-macos-arm64"
PACKAGE_DIR="${MICROSTICK_USAGE_PACKAGE_DIR:-$DIST_DIR/$PACKAGE_NAME}"
ARCHIVE_PATH="$DIST_DIR/$PACKAGE_NAME.zip"
archive_temp=""

cleanup_archive_temp() {
  if [[ -n "$archive_temp" ]]; then
    /bin/rm -f "$archive_temp"
  fi
}

trap cleanup_archive_temp EXIT

if [[ ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "MICROSTICK_APP_VERSION must use semantic version format." >&2
  exit 2
fi
case "$PACKAGE_DIR" in
  ""|/|"$HOME"|"$ROOT_DIR"|"$DIST_DIR")
    echo "Refusing to replace an unsafe package directory: $PACKAGE_DIR" >&2
    exit 2
    ;;
esac

/bin/rm -rf "$PACKAGE_DIR"
/bin/mkdir -p "$PACKAGE_DIR" "$DIST_DIR"
MICROSTICK_USAGE_OUTPUT_DIR="$PACKAGE_DIR" \
  "$ROOT_DIR/scripts/build_usage_sync.sh" --package
/bin/cp "$ROOT_DIR/scripts/install.sh" "$PACKAGE_DIR/install.sh"
/bin/cp "$ROOT_DIR/scripts/uninstall.sh" "$PACKAGE_DIR/uninstall.sh"
/bin/cp "$ROOT_DIR/scripts/doctor.sh" "$PACKAGE_DIR/doctor.sh"
/bin/cp "$ROOT_DIR/README.md" "$PACKAGE_DIR/README.md"
/bin/cp "$ROOT_DIR/README.en.md" "$PACKAGE_DIR/README.en.md"
/bin/mkdir -p "$PACKAGE_DIR/docs"
/bin/cp "$ROOT_DIR/docs/ARCHITECTURE.md" \
  "$ROOT_DIR/docs/PROTOCOLS.md" \
  "$ROOT_DIR/docs/DEVELOPMENT.md" \
  "$ROOT_DIR/docs/microstick-v1-product-render.png" \
  "$PACKAGE_DIR/docs/"
/bin/cp "$ROOT_DIR/LICENSE" "$PACKAGE_DIR/LICENSE"
/bin/cp "$ROOT_DIR/NOTICE" "$PACKAGE_DIR/NOTICE"
/bin/chmod 0755 "$PACKAGE_DIR/install.sh" "$PACKAGE_DIR/uninstall.sh" \
  "$PACKAGE_DIR/doctor.sh"

APP="$PACKAGE_DIR/MicroStickUsageSync.app"
/usr/bin/codesign --verify --deep --strict "$APP"
test "$(/usr/bin/lipo -archs "$APP/Contents/MacOS/MicroStickUsageSync")" = arm64
test "$(/usr/bin/plutil -extract CFBundleDisplayName raw "$APP/Contents/Info.plist")" = MicroStickUsageSync
test -f "$APP/Contents/Resources/Licenses/AgentMote-LICENSE"
test -f "$APP/Contents/Resources/Licenses/FreeMicro-LICENSE"
test -f "$APP/Contents/Resources/Licenses/codex-micro-4-core2-LICENSE"
test -f "$APP/Contents/Resources/Licenses/Espressif-usb_device_uac-LICENSE"
test -f "$APP/Contents/Resources/Licenses/SourceHanSans-OFL-1.1.txt"
test -f "$PACKAGE_DIR/docs/DEVELOPMENT.md"
/bin/sh -n "$PACKAGE_DIR/install.sh"
/bin/sh -n "$PACKAGE_DIR/uninstall.sh"
/bin/sh -n "$PACKAGE_DIR/doctor.sh"

archive_temp="$(mktemp "${TMPDIR:-/tmp}/microstick-usage-sync.XXXXXX")"
/usr/bin/ditto -c -k --sequesterRsrc --keepParent "$PACKAGE_DIR" "$archive_temp"
/bin/mv -f "$archive_temp" "$ARCHIVE_PATH"
archive_temp=""
archive_digest="$(/usr/bin/shasum -a 256 "$ARCHIVE_PATH" | /usr/bin/awk '{print $1}')"
printf '%s  %s\n' "$archive_digest" "$(basename "$ARCHIVE_PATH")" \
  > "$ARCHIVE_PATH.sha256"
/bin/chmod 0644 "$ARCHIVE_PATH" "$ARCHIVE_PATH.sha256"
echo "$(basename "$ARCHIVE_PATH") SHA-256 $archive_digest"
