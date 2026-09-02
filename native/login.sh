#!/bin/bash
# One scripted trip from cold start into the world.
#
# Coordinates are PANEL pixels on a 2560x1440 screen. The client's own outer GUI
# is laid out for 1280x720 and drawn 1:1, so every control sits where the PC
# client puts it, offset by the centring the outer GUI does itself — these are
# the measured on-screen values, re-derived 2026-08-25 after the GUI scale work
# moved them.
ADB="/c/Program Files/Unity/Hub/Editor/6000.5.8f1/Editor/Data/PlaybackEngines/AndroidPlayer/SDK/platform-tools/adb.exe"
D="${D:-127.0.0.1:5555}"
t() { "$ADB" -s "$D" shell input tap "$1" "$2"; sleep "${3:-0.5}"; }
shot() { "$ADB" -s "$D" exec-out screencap -p > "out/$1"; }

"$ADB" -s "$D" shell am force-stop com.ran.native
"$ADB" -s "$D" logcat -c
"$ADB" -s "$D" shell am start -n com.ran.native/com.ran.launcher.RanActivity > /dev/null
sleep "${BOOT:-45}"

t 1229 708 3      # server "YourServer" in the list
t 1681 708 2      # channel 0
t 1792 919 9      # connect

t 1331 596        # ID field
t 1114 1108; t 1114 1108; t 1508 982; t 1508 982        # x x 2 2
t 1331 653        # Pass field
t 1448 982; t 1508 982; t 1569 982; t 1448 1024         # 1 2 3 4
t 1162 790 18     # OK — the one login the server sees

if [ "${WORLD:-1}" = 1 ]; then
  t 2278 288 2    # the character row
  t 2432 701 45   # start
fi
shot "${1:-world.png}"
echo "screenshot: out/${1:-world.png}"
