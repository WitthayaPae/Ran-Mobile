#!/bin/bash
# Log into the emulator, waiting on the client's own log rather than on pixels.
#
# Pixel sampling was fooled by the sky; these markers are exact: the renderer
# announces itself at boot, character pieces announce their skin conversion at
# character select, and the game stage's section timings only appear in world.
ADB="/c/Program Files/Unity/Hub/Editor/6000.5.8f1/Editor/Data/PlaybackEngines/AndroidPlayer/SDK/platform-tools/adb.exe"
D="${D:-emulator-5554}"
HERE="$(cd "$(dirname "$0")" && pwd)"

tap()  { "$ADB" -s "$D" shell input tap "$1" "$2"; sleep "${3:-1}"; }

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
"$ADB" -s "$D" logcat -c
"$ADB" -s "$D" shell am start -n com.ran.native/android.app.NativeActivity > /dev/null

echo "waiting for the client to boot..."
wait_log "RanApp  : === RAN mobile boot" 180 || exit 1
sleep 25                                   # the server list needs the outer GUI up

tap 1229 708 4
tap 1681 708 3
tap 1792 919 8

tap 1331 596 2
tap 1114 1108 1; tap 1114 1108 1; tap 1508 982 1; tap 1508 982 1
tap 1331 653 2
tap 1448 982 1; tap 1508 982 1; tap 1569 982 1; tap 1448 1024 1
tap 1162 790 3

echo "waiting for character select..."
wait_log "blended: verts" 180 || exit 1

tap 2278 288 6
if [ "${WORLD:-0}" = 1 ]; then
  tap 2473 500 5
  echo "waiting for the world..."
  wait_log "FRAME sections" 240 || exit 1
fi

"$ADB" -s "$D" exec-out screencap -p > "$HERE/out/${1:-emu_state.png}"
echo "ready: out/${1:-emu_state.png}"
