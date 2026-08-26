#!/bin/bash
# One scripted trip into the world on the Galaxy Tab S9.
#
# The tablet is 2560x1600 and the client's logical size is 1280x800, so the
# outer GUI sits differently from the 16:9 emulator - these coordinates were
# measured from the tablet's own screen, not derived from the other device's.
ADB="/c/Program Files/Unity/Hub/Editor/6000.5.8f1/Editor/Data/PlaybackEngines/AndroidPlayer/SDK/platform-tools/adb.exe"
D="${D:-192.168.1.184:36763}"
t() { "$ADB" -s "$D" shell input tap "$1" "$2"; sleep "${3:-0.5}"; }
shot() { "$ADB" -s "$D" exec-out screencap -p > "out/$1"; }

"$ADB" -s "$D" shell am force-stop com.ran.native
"$ADB" -s "$D" logcat -c
"$ADB" -s "$D" shell am start -n com.ran.native/android.app.NativeActivity > /dev/null
sleep "${BOOT:-55}"

t 1232 788 3      # server "YourServer"
t 1688 786 2      # channel 0
t 1790 996 9      # connect

t 1340 675        # ID field
t 1109 1195; t 1109 1195; t 1509 1062; t 1509 1062      # x x 2 2
t 1340 730        # Pass field
t 1446 1062; t 1509 1062; t 1569 1062; t 1446 1108      # 1 2 3 4
t 1185 870 20     # OK - the one login the server sees

if [ "${WORLD:-1}" = 1 ]; then
  t 2394 448 2    # the character row
  t 2473 861 45   # start
fi
shot "${1:-tabworld.png}"
echo "screenshot: out/${1:-tabworld.png}"
