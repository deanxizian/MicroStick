#!/usr/bin/env bash
# Build and sign the native MicroStickUsageSync application bundle.
set -euo pipefail

MODE="${1:---debug}"
USAGE_NAME="MicroStickUsageSync"
USAGE_BUNDLE_ID="com.deanxizian.microstick.usage-sync"
MIN_SYSTEM_VERSION="14.0"
APP_VERSION="${MICROSTICK_APP_VERSION:-1.1.0}"
APP_BUILD_VERSION="${MICROSTICK_APP_BUILD_VERSION:-1}"
PACKAGE_ARCHITECTURE="arm64"

if [[ ! "$APP_VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "MICROSTICK_APP_VERSION must use semantic version format." >&2
  exit 2
fi
if [[ ! "$APP_BUILD_VERSION" =~ ^[1-9][0-9]*$ ]]; then
  echo "MICROSTICK_APP_BUILD_VERSION must be a positive integer." >&2
  exit 2
fi
if [[ "$(/usr/bin/uname -m)" != "$PACKAGE_ARCHITECTURE" ]]; then
  echo "MicroStick supports Apple Silicon Macs only." >&2
  exit 2
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PACKAGE_DIR="$ROOT_DIR/app/macos"
OUTPUT_DIR="${MICROSTICK_USAGE_OUTPUT_DIR:-$ROOT_DIR/dist}"
USAGE_APP="$OUTPUT_DIR/$USAGE_NAME.app"
USAGE_CONTENTS="$USAGE_APP/Contents"
USAGE_MACOS="$USAGE_CONTENTS/MacOS"
USAGE_RESOURCES="$USAGE_CONTENTS/Resources"
USAGE_BINARY="$USAGE_MACOS/$USAGE_NAME"

case "$MODE" in
  --debug|debug)
    CONFIGURATION="debug"
    swift build --package-path "$PACKAGE_DIR" --product "$USAGE_NAME"
    BUILD_DIR="$(swift build --package-path "$PACKAGE_DIR" --show-bin-path)"
    ;;
  --package|package)
    CONFIGURATION="release"
    PACKAGE_TRIPLE="${PACKAGE_ARCHITECTURE}-apple-macosx${MIN_SYSTEM_VERSION}"
    swift build --package-path "$PACKAGE_DIR" --configuration release \
      --triple "$PACKAGE_TRIPLE" --product "$USAGE_NAME"
    BUILD_DIR="$(swift build --package-path "$PACKAGE_DIR" \
      --configuration release --triple "$PACKAGE_TRIPLE" --show-bin-path)"
    ;;
  --verify|verify)
    /usr/bin/codesign --verify --deep --strict "$USAGE_APP"
    /usr/bin/plutil -lint "$USAGE_CONTENTS/Info.plist"
    test "$(/usr/bin/lipo -archs "$USAGE_BINARY")" = "$PACKAGE_ARCHITECTURE"
    echo "Verified $USAGE_APP"
    exit 0
    ;;
  *)
    echo "usage: $0 [--debug|--package|--verify]" >&2
    exit 64
    ;;
esac

/bin/rm -rf "$USAGE_APP"
/bin/mkdir -p "$USAGE_MACOS" "$USAGE_RESOURCES/Licenses"
/bin/cp "$BUILD_DIR/$USAGE_NAME" "$USAGE_BINARY"
/bin/chmod 0755 "$USAGE_BINARY"
/bin/cp "$ROOT_DIR/LICENSE" "$USAGE_RESOURCES/Licenses/MicroStick-LICENSE"
/bin/cp "$ROOT_DIR/firmware/sticks3/components/agentmote.LICENSE" \
  "$USAGE_RESOURCES/Licenses/AgentMote-LICENSE"
/bin/cp "$ROOT_DIR/firmware/sticks3/components/freemicro.LICENSE" \
  "$USAGE_RESOURCES/Licenses/FreeMicro-LICENSE"
/bin/cp "$ROOT_DIR/firmware/sticks3/components/codex-micro-4-core2.LICENSE" \
  "$USAGE_RESOURCES/Licenses/codex-micro-4-core2-LICENSE"
/bin/cp "$ROOT_DIR/firmware/sticks3/components/usb_device_uac/license.txt" \
  "$USAGE_RESOURCES/Licenses/Espressif-usb_device_uac-LICENSE"
/bin/cp "$ROOT_DIR/firmware/sticks3/components/source-han-sans.OFL.txt" \
  "$USAGE_RESOURCES/Licenses/SourceHanSans-OFL-1.1.txt"
/bin/cp "$ROOT_DIR/NOTICE" "$USAGE_RESOURCES/NOTICE"

cat > "$USAGE_CONTENTS/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleDevelopmentRegion</key><string>zh_CN</string>
  <key>CFBundleDisplayName</key><string>MicroStickUsageSync</string>
  <key>CFBundleExecutable</key><string>$USAGE_NAME</string>
  <key>CFBundleIdentifier</key><string>$USAGE_BUNDLE_ID</string>
  <key>CFBundleName</key><string>MicroStickUsageSync</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>$APP_VERSION</string>
  <key>CFBundleVersion</key><string>$APP_BUILD_VERSION</string>
  <key>LSMinimumSystemVersion</key><string>$MIN_SYSTEM_VERSION</string>
  <key>LSUIElement</key><true/>
  <key>NSBluetoothAlwaysUsageDescription</key><string>用于将本机 Codex 的 7D 用量推送到已配对的 MicroStick。</string>
  <key>NSPrincipalClass</key><string>NSApplication</string>
</dict></plist>
PLIST
/usr/bin/plutil -lint "$USAGE_CONTENTS/Info.plist" >/dev/null

SIGNING_IDENTITY="${MICROSTICK_SIGNING_IDENTITY:-}"
if [[ -z "$SIGNING_IDENTITY" ]]; then
  SIGNING_IDENTITY="$(/usr/bin/security find-identity -v -p codesigning 2>/dev/null | /usr/bin/awk '/Developer ID Application:/ {print $2; exit}')"
fi
if [[ -z "$SIGNING_IDENTITY" ]]; then
  SIGNING_IDENTITY="$(/usr/bin/security find-identity -v -p codesigning 2>/dev/null | /usr/bin/awk '/Apple Development:/ {print $2; exit}')"
fi
if [[ -z "$SIGNING_IDENTITY" ]]; then
  SIGNING_IDENTITY="-"
fi

if [[ "$CONFIGURATION" == "release" && \
      "${MICROSTICK_ALLOW_NON_DISTRIBUTION_SIGNING:-0}" != "1" ]]; then
  identity_description=""
  if [[ "$SIGNING_IDENTITY" != "-" ]]; then
    identity_description="$(/usr/bin/security find-identity -v -p codesigning 2>/dev/null | \
      /usr/bin/grep "$SIGNING_IDENTITY" | /usr/bin/head -n 1 || true)"
  fi
  if [[ "$identity_description" != *"Developer ID Application:"* ]]; then
    echo "Release packaging requires a Developer ID Application identity." >&2
    echo "Set MICROSTICK_ALLOW_NON_DISTRIBUTION_SIGNING=1 only for local/CI validation." >&2
    exit 1
  fi
fi

if [[ "$SIGNING_IDENTITY" == "-" ]]; then
  /usr/bin/codesign --force --options runtime --sign - "$USAGE_APP" >/dev/null
else
  /usr/bin/codesign --force --options runtime --timestamp \
    --sign "$SIGNING_IDENTITY" "$USAGE_APP" >/dev/null
fi

/usr/bin/codesign --verify --deep --strict "$USAGE_APP"
test "$(/usr/bin/lipo -archs "$USAGE_BINARY")" = "$PACKAGE_ARCHITECTURE"
echo "Built $USAGE_APP"
