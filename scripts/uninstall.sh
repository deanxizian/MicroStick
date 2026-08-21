#!/usr/bin/env sh
set -eu
umask 077

SUPPORT_DIR="$HOME/Library/Application Support/MicroStick"
APPLICATIONS_DIR="$HOME/Applications"
USAGE_APP="$APPLICATIONS_DIR/MicroStickUsageSync.app"
USAGE_BINARY="$USAGE_APP/Contents/MacOS/MicroStickUsageSync"
PURGE=0

case "${1:-}" in
  "") ;;
  --purge) PURGE=1 ;;
  -h|--help)
    printf '%s\n' "Usage: scripts/uninstall.sh [--purge]"
    printf '%s\n' "Default: remove the login item and executables; retain usage cache/status files."
    exit 0
    ;;
  *)
    printf '%s\n' "Usage: scripts/uninstall.sh [--purge]" >&2
    exit 64
    ;;
esac

if [ "$#" -gt 1 ]; then
  printf '%s\n' "Usage: scripts/uninstall.sh [--purge]" >&2
  exit 64
fi

if [ -x "$USAGE_BINARY" ]; then
  "$USAGE_BINARY" --unregister >/dev/null 2>&1 || true
fi
/usr/bin/pkill -TERM -x MicroStickUsageSync >/dev/null 2>&1 || true

attempt=0
while /usr/bin/pgrep -x MicroStickUsageSync >/dev/null 2>&1 && \
      [ "$attempt" -lt 20 ]; do
  attempt=$((attempt + 1))
  /bin/sleep 0.1
done

if /usr/bin/pgrep -x MicroStickUsageSync >/dev/null 2>&1; then
  /usr/bin/pkill -KILL -x MicroStickUsageSync >/dev/null 2>&1 || true
  /bin/sleep 0.1
fi
if /usr/bin/pgrep -x MicroStickUsageSync >/dev/null 2>&1; then
  printf '%s\n' "MicroStickUsageSync could not be stopped; the app was not removed." >&2
  exit 1
fi

/bin/rm -rf "$USAGE_APP"

if [ "$PURGE" -eq 1 ]; then
  /bin/rm -rf "$SUPPORT_DIR"
  printf '%s\n' "Removed MicroStickUsageSync, its login item, cache, logs, and runtime status."
else
  printf '%s\n' "Removed MicroStickUsageSync and its login item."
  printf '%s\n' "Retained usage cache/status files in $SUPPORT_DIR; use --purge to remove them."
fi
