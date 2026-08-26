#!/bin/bash
# Install an APK on the wireless test device and launch it.
#
#   tools/device.sh install <apk> [logfilter]
#   tools/device.sh log [filter]
#   tools/device.sh shot <out.png>
#   tools/device.sh tap <x> <y>
#
# Exists because a Unity batchmode build restarts the adb server and drops the
# wireless TLS session, so a plain `adb install` right after a build fails with
# "device offline" roughly every time. Reconnecting is mDNS discovery, not a
# fixed address — the port the device SHOWS is not always the port it listens
# on, so it must be looked up rather than remembered.
set -uo pipefail

ADB="/c/Program Files/Unity/Hub/Editor/6000.5.8f1/Editor/Data/PlaybackEngines/AndroidPlayer/SDK/platform-tools/adb.exe"
# Unity 2021.3 also ships an adb (v32) whose mDNS discovery returns nothing.
# The v36 one from 6000.5.8f1 finds the device immediately.
export ADB_MDNS_OPENSCREEN=1
export MSYS_NO_PATHCONV=1        # keep /sdcard/... from becoming a Windows path
PKG=com.ran.mobile

[ -x "$ADB" ] || { echo "adb not found at $ADB" >&2; exit 1; }

SERIAL=""

# Pick ONE transport.
#
# mDNS registers the device by service name and auto-connect can add an ip:port
# entry for the same phone, so every unqualified adb call dies with "more than
# one device/emulator". Note the `tr -d '\r'`: adb.exe emits CRLF, which makes a
# `device$` anchor never match under bash and silently yields an empty serial —
# which then looks exactly like "no device".
pick_serial() {
  SERIAL=$("$ADB" devices 2>/dev/null | tr -d '\r' \
           | awk '$2 == "device" { print $1; exit }')
  [ -n "$SERIAL" ]
}

connect() {
  for _ in 1 2 3 4 5; do
    if pick_serial; then keep_awake; return 0; fi
    "$ADB" kill-server >/dev/null 2>&1
    sleep 2
    "$ADB" start-server >/dev/null 2>&1
    sleep 5
    addr=$("$ADB" mdns services 2>/dev/null | tr -d '\r' \
           | awk '/_adb-tls-connect/ { print $3; exit }')
    [ -n "${addr:-}" ] && "$ADB" connect "$addr" >/dev/null 2>&1
    sleep 2
  done
  pick_serial && keep_awake
}

# The tablet drops wireless debugging when its screen sleeps, which it does
# during every multi-minute Unity build — that is the actual reason the device
# is "gone" after each build and needs its toggle cycled. Keeping the screen on
# while plugged in (svc power stayon) outlives our session and stops the cycle.
keep_awake() {
  "$ADB" -s "$SERIAL" shell svc power stayon true >/dev/null 2>&1 || true
}

a() { "$ADB" -s "$SERIAL" "$@"; }

case "${1:-}" in
  install)
    apk="${2:?usage: device.sh install <apk> [logfilter]}"
    connect || { echo "device did not come online" >&2; exit 1; }
    a install -r "$apk" 2>&1 | tail -1
    a logcat -c
    a shell am force-stop "$PKG"
    a shell monkey -p "$PKG" -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
    sleep "${LAUNCH_WAIT:-20}"
    a logcat -d -s Unity:V 2>&1 | grep -E "${3:-RanRender}" | head -30
    ;;
  log)
    connect || exit 1
    a logcat -d -s Unity:V 2>&1 | grep -E "${2:-.}" | head -40
    ;;
  shot)
    out="${2:?usage: device.sh shot <out.png>}"
    connect || exit 1
    a shell screencap -p /sdcard/ranshot.png
    a pull /sdcard/ranshot.png "$out" >/dev/null 2>&1
    echo "$out"
    ;;
  tap)
    connect || exit 1
    a shell input tap "${2:?x}" "${3:?y}"
    ;;
  *)
    echo "usage: device.sh {install <apk> [filter] | log [filter] | shot <png> | tap <x> <y>}" >&2
    exit 2
    ;;
esac
