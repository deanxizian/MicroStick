#!/usr/bin/env bash
# Submit, staple, and repackage a signed UsageSync release.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST_DIR="$ROOT_DIR/dist"
VERSION="${MICROSTICK_APP_VERSION:-1.3.0}"
PROFILE="${MICROSTICK_NOTARY_PROFILE:-}"
PACKAGE_NAME="MicroStickUsageSync-v${VERSION}-macos-arm64"
PACKAGE_DIR="$DIST_DIR/$PACKAGE_NAME"
ARCHIVE_PATH="$DIST_DIR/$PACKAGE_NAME.zip"
APP="$PACKAGE_DIR/MicroStickUsageSync.app"
archive_temp=""

cleanup_archive_temp() {
  if [[ -n "$archive_temp" ]]; then
    /bin/rm -f "$archive_temp"
  fi
}

trap cleanup_archive_temp EXIT

if [[ -z "$PROFILE" ]]; then
  echo "Set MICROSTICK_NOTARY_PROFILE to a notarytool Keychain profile." >&2
  exit 2
fi
if [[ ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "MICROSTICK_APP_VERSION must use semantic version format." >&2
  exit 2
fi
if [[ ! -d "$APP" || ! -f "$ARCHIVE_PATH" ]]; then
  echo "Build the release package before notarizing it." >&2
  exit 1
fi

/usr/bin/codesign --verify --deep --strict "$APP"
/usr/bin/xcrun notarytool submit "$ARCHIVE_PATH" \
  --keychain-profile "$PROFILE" --wait
/usr/bin/xcrun stapler staple "$APP"
/usr/bin/xcrun stapler validate "$APP"
/usr/bin/codesign --verify --deep --strict "$APP"
/usr/sbin/spctl --assess --type execute --verbose=2 "$APP"

archive_temp="$(mktemp "${TMPDIR:-/tmp}/microstick-notarized.XXXXXX")"
/usr/bin/ditto -c -k --sequesterRsrc --keepParent "$PACKAGE_DIR" "$archive_temp"
/bin/mv -f "$archive_temp" "$ARCHIVE_PATH"
archive_temp=""

/usr/bin/unzip -t "$ARCHIVE_PATH" >/dev/null
archive_digest="$(/usr/bin/shasum -a 256 "$ARCHIVE_PATH" | /usr/bin/awk '{print $1}')"
printf '%s  %s\n' "$archive_digest" "$(basename "$ARCHIVE_PATH")" \
  > "$ARCHIVE_PATH.sha256"
/bin/chmod 0644 "$ARCHIVE_PATH" "$ARCHIVE_PATH.sha256"

echo "Notarized package: $ARCHIVE_PATH"
echo "SHA-256: $archive_digest"
