#!/bin/bash
# One trip into the world on the Galaxy Tab S9, with the account in .login.
#
# login-tab.sh types a fixed test account and sleeps a fixed 55 seconds before
# its first tap. That sleep is the problem: a debuggable build, a cold start or
# a patch check pushes the outer GUI past it, the taps land on whatever is
# under them - usually the exit confirmation - and the run is wasted. Every
# wasted run is another login the live server sees, which is the one thing this
# project must not do casually.
#
# So this waits for the client to say it is up, and checks its own work.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ADB="${ADB:-/c/Program Files/Unity/Hub/Editor/6000.5.8f1/Editor/Data/PlaybackEngines/AndroidPlayer/SDK/platform-tools/adb.exe}"
D="${D:-adb-R52Y109PD8R-nvmo4D._adb-tls-connect._tcp}"

CRED="$HERE/.login"
USER="${RANUSER:-}"
PASS="${RANPASS:-}"
if [ -z "$USER" ] || [ -z "$PASS" ]; then
  [ -f "$CRED" ] || { echo "  no $CRED - two lines: id, then password"; exit 1; }
  USER=$(sed -n 1p "$CRED"); PASS=$(sed -n 2p "$CRED")
fi

t() { "$ADB" -s "$D" shell input tap "$1" "$2"; sleep "${3:-0.5}"; }
k() { "$ADB" -s "$D" shell input keyevent "$1"; }
clear_field() { for i in $(seq 8); do k 67; done; sleep 1; }

wait_log() {
  local n=0
  while [ $n -lt "${2:-180}" ]; do
    "$ADB" -s "$D" logcat -d 2>/dev/null | grep -q "$1" && return 0
    sleep 3; n=$((n + 3))
  done
  echo "  timed out waiting for: $1"; return 1
}

"$ADB" -s "$D" shell am force-stop com.ran.native
"$ADB" -s "$D" logcat -c
"$ADB" -s "$D" shell am start -n com.ran.native/com.ran.launcher.RanLauncher > /dev/null

#  The client says when it is up; do not guess.
wait_log "=== boot complete ===" 240 || exit 1
#  ...and the outer GUI still has to build itself after that.
sleep 12

#  A stray earlier tap may have raised the exit confirmation. Cancelling is a
#  no-op when it is not there.
t 1531 865 3

t 1232 788 4      # server row
t 1688 786 3      # channel
t 1790 996 12     # connect

clear_field; "$ADB" -s "$D" shell input text "$USER"; sleep 1
t 1338 730 2
clear_field; "$ADB" -s "$D" shell input text "$PASS"; sleep 1
#  Back only when the keyboard is actually up.
#
#  Back with no keyboard is "exit the game?" on the outer GUI, and the run then
#  taps its way around a modal that should not be there. Ask the system whether
#  the IME is showing rather than assuming it.
if "$ADB" -s "$D" shell dumpsys input_method 2>/dev/null | grep -q "mInputShown=true"; then
  k 4; sleep 2
fi
t 1184 866 25     # OK - the one login the server sees

t 2240 288 4      # the character row
t 2383 707 65     # start

#  Prove it: the frame log only runs in the world.
if "$ADB" -s "$D" logcat -d 2>/dev/null | grep -q "FRAME sections"; then
  echo "in the world"
else
  echo "  did NOT reach the world - screenshot in out/${1:-tabworld.png}"
fi
"$ADB" -s "$D" exec-out screencap -p > "$HERE/out/${1:-tabworld.png}"
echo "screenshot: out/${1:-tabworld.png}"
