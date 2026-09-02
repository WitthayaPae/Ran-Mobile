#!/bin/bash
# One scripted trip into the world on the Galaxy Tab S9, xx11 (GameMaster).
#
# Coordinates re-measured on 2026-08-30 from the tablet's own screen. The ID
# field already has focus when the login dialog opens, so the first keys go
# straight in - tapping it again only raised the clipboard strip.
ADB="/c/Program Files/Unity/Hub/Editor/6000.5.8f1/Editor/Data/PlaybackEngines/AndroidPlayer/SDK/platform-tools/adb.exe"
t() { "$ADB" shell input tap "$1" "$2"; sleep "${3:-0.5}"; }
k() { for a in "$@"; do "$ADB" shell input keyevent "$a"; sleep 0.6; done; }

"$ADB" shell am force-stop com.ran.native
"$ADB" logcat -c
"$ADB" shell am start -n com.ran.native/com.ran.launcher.RanActivity > /dev/null
sleep "${BOOT:-55}"

t 1232 788 3      # server "YourServer"
t 1690 790 2      # channel 0
t 1792 998 9      # connect

k 52 52 8 8       # x x 1 1
t 1335 730 2      # Pass field
k 8 9 10 11       # 1 2 3 4
"$ADB" shell input keyevent 4; sleep 2   # hide the IME (eaten by the system)
t 1184 868 20     # OK - the one login the server sees

if [ "${WORLD:-1}" = 1 ]; then
  t 2240 288 3    # the character row
  t 2388 705 50   # start
fi
"$ADB" exec-out screencap -p > "out/${1:-tabworld.png}"
echo "screenshot: out/${1:-tabworld.png}"
