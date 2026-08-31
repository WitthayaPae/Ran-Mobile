#!/bin/bash
# Boot the client on LDPlayer and walk it into the world.
#
# The old login-ld.sh drove the client's own on-screen keypad by tapping digit
# positions, which stopped matching after a layout change and silently produced
# an empty password. The emulator accepts `input text` straight into the focused
# edit box, so the credentials are typed instead of aimed at.
set -e
A="/c/Program Files/Unity/Hub/Editor/6000.5.8f1/Editor/Data/PlaybackEngines/AndroidPlayer/SDK/platform-tools/adb.exe"
D="${D:-127.0.0.1:5555}"
HERE="$(cd "$(dirname "$0")" && pwd)"
USER="${RANUSER:-xx11}"
PASS="${RANPASS:-1234}"

t() { "$A" -s "$D" shell input tap "$1" "$2"; sleep "${3:-1}"; }

wait_log() {
  local n=0
  while [ $n -lt "${2:-180}" ]; do
    "$A" -s "$D" logcat -d | grep -q "$1" && return 0
    sleep 3; n=$((n + 3))
  done
  echo "  timed out waiting for: $1"; return 1
}

"$A" -s "$D" shell am force-stop com.ran.native
for i in $(seq 1 20); do
  [ -z "$("$A" -s "$D" shell pidof com.ran.native | tr -d '\r\n')" ] && break
  sleep 1
done
"$A" -s "$D" logcat -c

for i in 1 2 3 4 5; do
  "$A" -s "$D" shell am start -n com.ran.native/android.app.NativeActivity >/dev/null 2>&1
  sleep 4
  [ -n "$("$A" -s "$D" shell pidof com.ran.native | tr -d '\r\n')" ] && break
done

wait_log "=== boot complete ===" 240 || exit 1
sleep 18

t 1229 708 4                 # server row
t 1681 708 3                 # channel
t 1792 919 8                 # connect

t 1340 595 1                 # ID field
"$A" -s "$D" shell input text "$USER"; sleep 1
t 1340 645 1                 # Pass field
"$A" -s "$D" shell input text "$PASS"; sleep 1
t 1180 788 6                 # OK

wait_log "blended: verts" 180 || exit 1
t 2278 275 4                 # character row
t 2428 705 3                 # start

wait_log "FRAME sections" 240 || exit 1
sleep 6
"$A" -s "$D" exec-out screencap -p > "$HERE/out/${1:-ld_world.png}"
echo "ready: out/${1:-ld_world.png}"
