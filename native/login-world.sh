#!/bin/bash
# Log in and enter the world, waiting on the client's own log rather than on
# pixels. The start button sits lower than the character list; the earlier
# coordinate landed above it and the world was never reached.
ADB="/c/Program Files/Unity/Hub/Editor/6000.5.8f1/Editor/Data/PlaybackEngines/AndroidPlayer/SDK/platform-tools/adb.exe"
D="${D:-emulator-5554}"
HERE="$(cd "$(dirname "$0")" && pwd)"
"$HERE/login-emu.sh" "${1:-charsel.png}" >/dev/null 2>&1 || true
"$ADB" -s "$D" shell input tap 2278 288; sleep 3
"$ADB" -s "$D" shell input tap 2432 710
for i in $(seq 1 45); do
  if "$ADB" -s "$D" logcat -d | grep -q "FRAME sections"; then break; fi
  sleep 4
done
"$ADB" -s "$D" exec-out screencap -p > "$HERE/out/${1:-world.png}"
echo "ready: out/${1:-world.png}"
