#!/bin/bash
# Boot the client on LDPlayer and walk it into the world.
#
# login-emu.sh raced on this device: it force-stops and immediately `am start`s,
# and after an `install -r` the old process is still being reaped, so the start
# is swallowed and every later tap lands on an empty screen. Here the launch is
# retried until a pid actually exists, and each step waits on the client's own
# log rather than on a fixed sleep.
ADB="/c/Program Files/Unity/Hub/Editor/6000.5.8f1/Editor/Data/PlaybackEngines/AndroidPlayer/SDK/platform-tools/adb.exe"
D="${D:-127.0.0.1:5555}"
HERE="$(cd "$(dirname "$0")" && pwd)"

t() { "$ADB" -s "$D" shell input tap "$1" "$2"; sleep "${3:-1}"; }

# Stale log lines were matching every wait instantly, so the script tapped
# character-select before it existed and then timed out - which looked for all
# the world like the server dropping us.
#
# Filtering by --pid was the obvious fix and does not work here: this emulator's
# logcat rejects it with "pid out of range". What actually fixes it is the
# restart below - the log is cleared while the process is confirmed dead, so
# there is nothing stale left to match.
PID=""

wait_log() {                       # $1 = pattern, $2 = seconds
  local n=0
  while [ $n -lt "${2:-120}" ]; do
    "$ADB" -s "$D" logcat -d | grep -q "$1" && return 0
    sleep 3; n=$((n + 3))
  done
  echo "  timed out waiting for: $1"; return 1
}

# Kill it and CONFIRM it is dead before clearing the log.
#
# am force-stop returns immediately and does not always take, so the old
# process was still alive when the log was cleared - which wiped the boot
# marker of a run that had already booted, and then the wait for that marker
# timed out forever. Waiting on pidof going empty is the only reliable signal.
"$ADB" -s "$D" shell am force-stop com.ran.native
for i in $(seq 1 20); do
  [ -z "$("$ADB" -s "$D" shell pidof com.ran.native | tr -d "
")" ] && break
  "$ADB" -s "$D" shell am force-stop com.ran.native
  sleep 1
done
[ -n "$("$ADB" -s "$D" shell pidof com.ran.native | tr -d "
")" ] && { echo "  could not stop the old process"; exit 1; }

"$ADB" -s "$D" logcat -b all -c 2>/dev/null || "$ADB" -s "$D" logcat -c

for i in 1 2 3 4 5; do
  "$ADB" -s "$D" shell am start -n com.ran.native/com.ran.launcher.RanLauncher >/dev/null 2>&1
  sleep 4
  PID=$("$ADB" -s "$D" shell pidof com.ran.native | tr -d "
")
  [ -n "$PID" ] && break
  echo "  launch attempt $i did not stick, retrying"
done
[ -z "$PID" ] && { echo "  never started"; exit 1; }
echo "  running as pid $PID"

wait_log "=== boot complete ===" 240 || exit 1
sleep 20                                   # the outer GUI settles after boot

t 1229 708 4                               # server "YourServer"
t 1681 708 3                               # channel 0
t 1792 919 8                               # connect

t 1331 596 2                               # ID field
t 1114 1108; t 1114 1108; t 1508 982; t 1508 982
t 1331 653 2                               # Pass field
t 1448 982; t 1508 982; t 1569 982; t 1448 1024
t 1162 790 3                               # OK - the one login the server sees

wait_log "blended: verts" 180 || exit 1
t 2278 288 6                               # the character row
t 2432 710 3                               # start

wait_log "FRAME sections" 240 || exit 1
sleep 5
"$ADB" -s "$D" exec-out screencap -p > "$HERE/out/${1:-ld_world.png}"
echo "ready: out/${1:-ld_world.png}"
