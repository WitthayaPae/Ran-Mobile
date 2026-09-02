#!/bin/bash
# One scripted trip into the world on the LDPlayer emulator (2560x1440).
#
# Typing goes through key events rather than taps: the client's own on-screen
# keyboard is gone - the device raises its own - so the old coordinate taps
# landed on the Android keyboard and typed nonsense into the ID field.
ADB="/c/Program Files/Unity/Hub/Editor/6000.5.8f1/Editor/Data/PlaybackEngines/AndroidPlayer/SDK/platform-tools/adb.exe"
D="${D:-127.0.0.1:5555}"
t() { "$ADB" -s "$D" shell input tap "$1" "$2"; sleep "${3:-0.5}"; }
k() { "$ADB" -s "$D" shell input keyevent "$@"; sleep 1; }
clear_field() { for i in 1 2 3 4 5 6 7 8; do "$ADB" -s "$D" shell input keyevent 67; done; sleep 1; }
shot() { "$ADB" -s "$D" exec-out screencap -p > "out/$1"; }

wait_log() {                       # $1 = pattern, $2 = seconds
  local t=0
  while [ $t -lt "${2:-120}" ]; do
    if "$ADB" -s "$D" logcat -d | grep -q "$1"; then return 0; fi
    sleep 3; t=$((t + 3))
  done
  echo "  (timed out waiting for: $1)"
  return 1
}

"$ADB" -s "$D" shell am force-stop com.ran.native
sleep 2
"$ADB" -s "$D" logcat -c 2>/dev/null
"$ADB" -s "$D" shell am start -n com.ran.native/com.ran.launcher.RanLauncher > /dev/null

echo "waiting for the client to boot..."
wait_log "RanApp  : === RAN mobile boot" 180 || exit 1
sleep 25                                   # the server list needs the outer GUI up

t 1229 708 4      # server
t 1681 708 3      # channel
t 1792 919 8      # connect

t 1331 596 2      # ID field
clear_field
k 52 52 9 9       # x x 2 2
t 1331 653 2      # Pass field
clear_field
k 8 9 10 11       # 1 2 3 4
# Back dismisses the IME; while it is showing the system eats Back, so the
# game never sees it as Escape.
"$ADB" -s "$D" shell input keyevent 4; sleep 2
t 1162 790 20     # OK - the one login the server sees

echo "waiting for character select..."
wait_log "blended: verts" 180 || exit 1

t 2278 288 6      # the character row
if [ "${WORLD:-1}" = 1 ]; then
  t 2413 707 5    # start
  echo "waiting for the world..."
  wait_log "RanApp  : FRAME " 240 || exit 1
  sleep 20
fi
shot "${1:-emuworld.png}"
echo "screenshot: out/${1:-emuworld.png}"
