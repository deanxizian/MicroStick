#!/usr/bin/env sh
set -eu
umask 077

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PROJECT_DIR="$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)"
APPLICATIONS_DIR="$HOME/Applications"
SUPPORT_DIR="$HOME/Library/Application Support/MicroStick"
DESTINATION_APP="$APPLICATIONS_DIR/MicroStickUsageSync.app"
DESTINATION_BINARY="$DESTINATION_APP/Contents/MacOS/MicroStickUsageSync"

if [ -n "${MICROSTICK_USAGE_SYNC_APP:-}" ]; then
  SOURCE_APP="$MICROSTICK_USAGE_SYNC_APP"
elif [ -d "$SCRIPT_DIR/MicroStickUsageSync.app" ]; then
  SOURCE_APP="$SCRIPT_DIR/MicroStickUsageSync.app"
elif [ -d "$PROJECT_DIR/dist/MicroStickUsageSync.app" ]; then
  SOURCE_APP="$PROJECT_DIR/dist/MicroStickUsageSync.app"
else
  printf '%s\n' "MicroStickUsageSync.app was not found." >&2
  printf '%s\n' "Build it with ./script/build_usage_sync.sh --package or use the release ZIP." >&2
  exit 1
fi

if [ "$(/usr/bin/uname -m)" != "arm64" ]; then
  printf '%s\n' "MicroStickUsageSync supports Apple Silicon Macs only." >&2
  exit 2
fi
if ! /usr/bin/codesign --verify --deep --strict "$SOURCE_APP" >/dev/null 2>&1; then
  printf '%s\n' "MicroStickUsageSync.app has an invalid code signature." >&2
  exit 1
fi
if [ "$(/usr/bin/lipo -archs "$SOURCE_APP/Contents/MacOS/MicroStickUsageSync")" != "arm64" ]; then
  printf '%s\n' "MicroStickUsageSync.app is not an Apple Silicon-only build." >&2
  exit 1
fi

/bin/mkdir -p "$APPLICATIONS_DIR" "$SUPPORT_DIR"
/bin/chmod 0700 "$SUPPORT_DIR"

STAGING_APP="$APPLICATIONS_DIR/.MicroStickUsageSync.app.new.$$"
BACKUP_APP="$APPLICATIONS_DIR/.MicroStickUsageSync.app.backup.$$"
had_modern_app=0
install_in_progress=0
backup_created=0
new_app_installed=0

cleanup_staging() {
  /bin/rm -rf "$STAGING_APP"
}

rollback_install() {
  if [ -x "$DESTINATION_BINARY" ]; then
    "$DESTINATION_BINARY" --unregister >/dev/null 2>&1 || true
  fi
  stop_usage_sync >/dev/null 2>&1 || true
  if [ "$backup_created" -eq 1 ] && [ -d "$BACKUP_APP" ]; then
    /bin/rm -rf "$DESTINATION_APP"
    /bin/mv "$BACKUP_APP" "$DESTINATION_APP"
  elif [ "$new_app_installed" -eq 1 ]; then
    /bin/rm -rf "$DESTINATION_APP"
  fi
  if [ "$had_modern_app" -eq 1 ] && [ -x "$DESTINATION_BINARY" ]; then
    "$DESTINATION_BINARY" --register-only >/dev/null 2>&1 || true
    /usr/bin/open -gj "$DESTINATION_APP" >/dev/null 2>&1 || true
  fi
}

fail_install() {
  printf '%s\n' "$1" >&2
  exit 1
}

stop_usage_sync() {
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
  ! /usr/bin/pgrep -x MicroStickUsageSync >/dev/null 2>&1
}

cleanup_on_exit() {
  exit_code=$?
  trap - EXIT HUP INT TERM
  cleanup_staging
  if [ "$install_in_progress" -eq 1 ]; then
    set +e
    rollback_install
  fi
  exit "$exit_code"
}

trap cleanup_on_exit EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

install_in_progress=1
if [ -x "$DESTINATION_BINARY" ]; then
  had_modern_app=1
  "$DESTINATION_BINARY" --unregister >/dev/null 2>&1 || true
fi
if ! stop_usage_sync; then
  fail_install "The existing MicroStickUsageSync process could not be stopped."
fi

/usr/bin/ditto "$SOURCE_APP" "$STAGING_APP"
/usr/bin/codesign --verify --deep --strict "$STAGING_APP"
if [ -d "$DESTINATION_APP" ]; then
  /bin/rm -rf "$BACKUP_APP"
  /bin/mv "$DESTINATION_APP" "$BACKUP_APP"
  backup_created=1
fi
/bin/mv "$STAGING_APP" "$DESTINATION_APP"
new_app_installed=1

registration_status=""
registration_code=0
approval_required=0
registration_status="$("$DESTINATION_BINARY" --register-only 2>&1)" || registration_code=$?
if [ "$registration_code" -ne 0 ] || [ "$registration_status" != "enabled" ]; then
  if [ "$registration_status" = "requires-approval" ]; then
    approval_required=1
  else
    fail_install "MicroStickUsageSync login-item registration failed: $registration_status"
  fi
fi

if ! /usr/bin/open -gj "$DESTINATION_APP"; then
  fail_install "MicroStickUsageSync registered but macOS could not launch it."
fi
started=0
attempt=0
while [ "$attempt" -lt 20 ]; do
  if /usr/bin/pgrep -x MicroStickUsageSync >/dev/null 2>&1; then
    started=1
    break
  fi
  attempt=$((attempt + 1))
  /bin/sleep 0.25
done
if [ "$started" -ne 1 ]; then
  fail_install "MicroStickUsageSync registered but did not start."
fi

install_in_progress=0
/bin/rm -rf "$BACKUP_APP"

trap - EXIT HUP INT TERM
cleanup_staging

printf '%s\n' "MicroStickUsageSync installed and running."
if [ "$approval_required" -eq 1 ]; then
  /usr/bin/open "x-apple.systempreferences:com.apple.LoginItems-Settings.extension" \
    >/dev/null 2>&1 || true
  printf '%s\n' "Enable MicroStickUsageSync in System Settings > General > Login Items." >&2
  exit 3
fi
printf '%s\n' "macOS now owns it as the MicroStickUsageSync login item."
