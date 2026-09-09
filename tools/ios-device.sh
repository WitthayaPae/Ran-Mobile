#!/bin/bash
#  The iOS half of tools/device.sh: what adb does for Android, as far as iOS
#  allows it.
#
#  iOS has no adb and no shell. What it does have is a set of lockdown services
#  that pymobiledevice3 speaks over USB, and between them they cover most of the
#  loop this port actually uses:
#
#      adb logcat                  ->  syslog live
#      adb pull /sdcard/ran/x      ->  apps pull   (the app's Documents)
#      adb push /sdcard/ran/x      ->  apps push
#      adb shell screencap         ->  developer dvt screenshot
#      /data/tombstones            ->  crash pull / crash parse-latest
#      adb install                 ->  NOT here. An .ipa must be signed first,
#                                      and signing is Sideloadly's job.
#
#  Documents is reachable at all only because Info.plist sets
#  UIFileSharingEnabled, and the diagnostic root was moved there for exactly
#  this reason - see RanIOS_DiagRoot in platform/ios/ran_ios_plat.mm.
#
#  Requires: Apple Devices (or iTunes) for the USB transport, and the phone
#  paired - plug it in, unlock, tap Trust.
#
#      ./tools/ios-device.sh list
#      ./tools/ios-device.sh log                 # follow, like logcat
#      ./tools/ios-device.sh log RanGL           # filtered
#      ./tools/ios-device.sh pull                # the whole ran/ diag folder
#      ./tools/ios-device.sh flag audiolog       # set a diagnostic flag
#      ./tools/ios-device.sh unflag audiolog
#      ./tools/ios-device.sh crash               # crash reports, newest first
#      ./tools/ios-device.sh shot out.png
set -e
PY="/c/Users/tapnu/AppData/Local/Programs/Python/Python312/python.exe"
#  Sideloadly appends the team id on iOS 16+, so the installed app is
#  com.ran.launcher.<TEAMID>, not com.ran.launcher. Found rather than assumed:
#  it changes with the signing account. Override with RAN_BUNDLE=... if needed.
BUNDLE="${RAN_BUNDLE:-}"
if [ -z "$BUNDLE" ]; then
  BUNDLE=$("$PY" -m pymobiledevice3 apps list --userspace 2>/dev/null |
           grep -oE '"com.ran.launcher[^"]*"' | head -1 | tr -d '"')
fi
[ -n "$BUNDLE" ] || BUNDLE=com.ran.launcher
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/../native/out/ios-device"
pmd() { "$PY" -m pymobiledevice3 "$@"; }

case "${1:-list}" in

  list)
    pmd usbmux list
    ;;

  #  Everything the app logs goes to the system log, because RanPlat_Log writes
  #  to stderr on every platform that is not Android. It ALSO writes ran.log
  #  into Documents - use `pull` for that when the interesting part is already
  #  over, and this when it is happening now.
  log)
    if [ -n "$2" ]; then pmd syslog live --match "$2"
    else pmd syslog live --process ran
    fi
    ;;

  pull)
    mkdir -p "$OUT"
    pmd apps pull "$BUNDLE" ran/ran.log "$OUT/ran.log" 2>/dev/null \
      && echo "ran.log -> $OUT/ran.log" \
      || echo "no ran/ran.log yet - has the app run?"
    ;;

  #  A flag is a file whose NAME is the switch; the contents only matter for the
  #  few that take a number. RanPlat_DiagExists just tests for it.
  flag)
    [ -n "$2" ] || { echo "which flag?"; exit 1; }
    T="$(mktemp)"; printf '%s' "${3:-1}" > "$T"
    pmd apps push "$BUNDLE" "$T" "ran/$2"
    rm -f "$T"
    echo "set $2${3:+ = $3}"
    ;;

  unflag)
    [ -n "$2" ] || { echo "which flag?"; exit 1; }
    pmd apps rm "$BUNDLE" "ran/$2" && echo "cleared $2"
    ;;

  #  The nearest thing to a tombstone. iOS writes a full report with a symbolised
  #  backtrace, which is more than logcat gives on Android.
  crash)
    mkdir -p "$OUT/crash"
    pmd crash pull "$OUT/crash" >/dev/null 2>&1 || true
    pmd crash parse-latest . 2>/dev/null | head -60 || ls -t "$OUT/crash" | head
    ;;

  shot)
    mkdir -p "$OUT"
    pmd developer dvt screenshot "$OUT/${2:-shot.png}" && echo "$OUT/${2:-shot.png}"
    ;;

  *)
    sed -n '/^#      \.\//p' "$0"
    ;;
esac
