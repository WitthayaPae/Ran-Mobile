#!/bin/bash
# One scripted trip into the world on the Galaxy Tab S9.
#
# The tablet is 2560x1600 and the client's logical size is 1280x800, so the
# outer GUI sits differently from the 16:9 emulator - these coordinates were
# measured from the tablet's own screen, not derived from the other device's.
ADB="/c/Program Files/Unity/Hub/Editor/6000.5.8f1/Editor/Data/PlaybackEngines/AndroidPlayer/SDK/platform-tools/adb.exe"
D="${D:-adb-R52Y109PD8R-nvmo4D._adb-tls-connect._tcp}"
t() { "$ADB" -s "$D" shell input tap "$1" "$2"; sleep "${3:-0.5}"; }
shot() { "$ADB" -s "$D" exec-out screencap -p > "out/$1"; }

"$ADB" -s "$D" shell am force-stop com.ran.native
"$ADB" -s "$D" logcat -c
"$ADB" -s "$D" shell am start -n com.ran.native/com.ran.launcher.RanLauncher > /dev/null
sleep "${BOOT:-55}"

t 1232 788 3      # server "YourServer"
t 1688 786 2      # channel 0
t 1790 996 9      # connect

# Typed with key events, not by tapping a keyboard.
#
# This used to tap the coordinates of the client's own on-screen keyboard. That
# keyboard is gone - the device raises its own now - so those taps landed on the
# Android keyboard instead and typed nonsense into the ID field.
k() { "$ADB" -s "$D" shell input keyevent "$@"; sleep 1; }
clear_field() { for i in 1 2 3 4 5 6 7 8; do "$ADB" -s "$D" shell input keyevent 67; done; sleep 1; }

t 1340 675 2      # ID field
clear_field
k 52 52 9 9       # x x 2 2
t 1340 725 2      # Pass field
clear_field
k 8 9 10 11       # 1 2 3 4
# Dismiss the keyboard with Back, not by tapping the dialog.
#
# The keyboard and its clipboard strip cover the bottom of the screen, and a tap
# aimed past them lands on the keyboard toolbar - which opened the clipboard
# panel instead. While the IME is showing the system eats Back to hide it, so
# the game never sees it as Escape.
"$ADB" -s "$D" shell input keyevent 4; sleep 2
t 1184 866 20     # OK - the one login the server sees

if [ "${WORLD:-1}" = 1 ]; then
  t 2240 288 3    # the character row
  t 2383 707 50   # start
fi
shot "${1:-tabworld.png}"
echo "screenshot: out/${1:-tabworld.png}"
