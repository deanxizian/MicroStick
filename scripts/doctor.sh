#!/usr/bin/env sh
set -u

SUPPORT_DIR="$HOME/Library/Application Support/MicroStick"
USAGE_APP="$HOME/Applications/MicroStickUsageSync.app"
USAGE_BINARY="$USAGE_APP/Contents/MacOS/MicroStickUsageSync"
PASS=0
WARN=0
FAIL=0

pass() { PASS=$((PASS + 1)); printf 'PASS %s\n' "$1"; }
warn() { WARN=$((WARN + 1)); printf 'WARN %s\n' "$1"; }
fail() { FAIL=$((FAIL + 1)); printf 'FAIL %s\n' "$1"; }

major_version="$(/usr/bin/sw_vers -productVersion | /usr/bin/cut -d. -f1)"
if [ "$major_version" -ge 14 ]; then
  pass "macOS 14 or newer"
else
  fail "macOS 14 or newer is required"
fi

if [ -d /Applications/ChatGPT.app ] || [ -d "$HOME/Applications/ChatGPT.app" ]; then
  pass "ChatGPT Desktop is installed"
else
  fail "ChatGPT Desktop was not found"
fi

CODEX_EXECUTABLE=""
for candidate in \
  "/Applications/ChatGPT.app/Contents/Resources/codex" \
  "$HOME/Applications/ChatGPT.app/Contents/Resources/codex" \
  "/Applications/Codex.app/Contents/Resources/codex" \
  "$HOME/Applications/Codex.app/Contents/Resources/codex"; do
  if [ -x "$candidate" ]; then
    CODEX_EXECUTABLE="$candidate"
    break
  fi
done
if [ -n "$CODEX_EXECUTABLE" ]; then
  pass "Codex active usage source is available"
else
  warn "Codex executable was not found; UsageSync can only retain cached usage"
fi

if [ -x "$USAGE_BINARY" ] && \
   /usr/bin/codesign --verify --deep --strict "$USAGE_APP" >/dev/null 2>&1 && \
   [ "$(/usr/bin/lipo -archs "$USAGE_BINARY" 2>/dev/null || true)" = "arm64" ]; then
  pass "Apple Silicon MicroStickUsageSync app is installed"
else
  fail "MicroStickUsageSync app is missing or invalid"
fi

if [ -x "$USAGE_BINARY" ] && [ "$("$USAGE_BINARY" --status 2>/dev/null || true)" = "enabled" ]; then
  pass "MicroStickUsageSync login item is enabled"
else
  warn "MicroStickUsageSync login item is not enabled"
fi

usage_pid="$(/usr/bin/pgrep -x MicroStickUsageSync 2>/dev/null | /usr/bin/head -n 1 || true)"
if [ -n "$usage_pid" ]; then
  usage_command="$(/bin/ps -p "$usage_pid" -o command= 2>/dev/null || true)"
  case "$usage_command" in
    "$USAGE_BINARY"*) pass "MicroStickUsageSync is running from the installed app" ;;
    *) warn "A MicroStickUsageSync process is running from an unexpected path" ;;
  esac
else
  warn "MicroStickUsageSync is not running"
fi

if [ "$(/usr/bin/plutil -extract CFBundleDisplayName raw "$USAGE_APP/Contents/Info.plist" 2>/dev/null || true)" = "MicroStickUsageSync" ]; then
  pass "Login item uses the MicroStickUsageSync product name"
else
  fail "Login item product metadata is invalid"
fi

if [ -f "$SUPPORT_DIR/usage-sync-status-v1.json" ]; then
  pass "UsageSync runtime status exists"
  codex_state="$(/usr/bin/plutil -extract codexState raw \
    "$SUPPORT_DIR/usage-sync-status-v1.json" 2>/dev/null || true)"
  case "$codex_state" in
    ready) pass "Codex active usage source is ready" ;;
    unavailable|retrying) warn "Codex active usage source is unavailable; cached usage may become stale" ;;
    *) warn "Codex active usage source has not reported ready yet" ;;
  esac
else
  warn "No usage snapshot has been delivered yet"
fi

if /usr/sbin/system_profiler SPBluetoothDataType 2>/dev/null | /usr/bin/grep -q 'Codex Micro'; then
  pass "Codex Micro appears in Bluetooth devices"
else
  warn "Codex Micro is not visible in the Bluetooth report"
fi

if /usr/sbin/system_profiler SPAudioDataType 2>/dev/null | /usr/bin/grep -q 'MicroStick Microphone'; then
  pass "MicroStick Microphone is available"
else
  warn "MicroStick Microphone is not currently connected"
fi

printf 'SUMMARY pass=%s warn=%s fail=%s\n' "$PASS" "$WARN" "$FAIL"
[ "$FAIL" -eq 0 ]
