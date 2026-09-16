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
#  Credentials come from native/.login, which is gitignored and which this
#  script never prints - the same file login-ld.sh reads. Two lines: id, then
#  password.
#
#  They used to sit here as defaults. That is a live account on the live server
#  written into every commit, and it only has to be read once by anyone who can
#  see the repository. RANUSER/RANPASS still override, for a one-off run.
CRED="$HERE/.login"
USER="${RANUSER:-}"
PASS="${RANPASS:-}"
if [ -z "$USER" ] || [ -z "$PASS" ]; then
  if [ ! -f "$CRED" ]; then
    echo "  no $CRED - create it with two lines: id, then password"
    exit 1
  fi
  USER=$(sed -n 1p "$CRED")
  PASS=$(sed -n 2p "$CRED")
fi
[ -n "$USER" ] && [ -n "$PASS" ] || { echo "  $CRED needs two non-empty lines"; exit 1; }

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
  "$A" -s "$D" shell am start -n com.ran.native/com.ran.launcher.RanLauncher >/dev/null 2>&1
  sleep 4
  [ -n "$("$A" -s "$D" shell pidof com.ran.native | tr -d '\r\n')" ] && break
done

wait_log "=== boot complete ===" 240 || exit 1
sleep 18

#  No server row, no channel, no connect: the mobile build picks the emptiest
#  server and a channel that is not full by itself and goes straight to the
#  login page (CSelectServerPage::MobileAutoEnter). Tapping here now lands on
#  the login page instead, which typed the id into nothing.

t 1340 595 1                 # ID field
"$A" -s "$D" shell input text "$USER"; sleep 1
t 1340 645 1                 # Pass field
"$A" -s "$D" shell input text "$PASS"; sleep 1
t 1180 788 6                 # OK

wait_log "blended:" 180 || exit 1
t 2278 275 4                 # character row
t 2428 705 3                 # start

wait_log "FRAME sections" 240 || exit 1
sleep 6
"$A" -s "$D" exec-out screencap -p > "$HERE/out/${1:-ld_world.png}"
echo "ready: out/${1:-ld_world.png}"
