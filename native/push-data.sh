#!/bin/bash
# Push the client data the native build needs to the device.
#
# The shipped data is ~5.5 GB, so it does NOT go in the APK — it lives on
# external storage and android_main looks for it at /sdcard/ran.
#
#   ./push-data.sh            push everything the boot path needs (~5.5 GB, slow)
#   ./push-data.sh minimal    config + gui + glogic only (~400 MB) — enough to
#                             prove RANPARAM, the RCC index and the login path
#
# Editor trees and the *_BACKUP / *_RECOVERED / *_PRISTINE directories are never
# pushed: they are not read by the client and would double the transfer.
set -e
export MSYS_NO_PATHCONV=1   # keep /sdcard paths intact under Git Bash
win() { cygpath -w "$1"; }
HERE="$(cd "$(dirname "$0")" && pwd)"
CLIENT="$HERE/../../CLIENT"
ADB="${ADB:-/c/LDPlayer/LDPlayer14/adb.exe}"
#  The launcher keeps the data in the app-private external dir, which other
#  apps cannot reach. Overridable, because an adb-pushed tree at /sdcard/ran is
#  still accepted by the native loader as a fallback.
DEST=${DEST:-/sdcard/Android/data/com.ran.native/files}
MODE="${1:-full}"

command -v "$ADB" >/dev/null || { echo "[!] adb not found at $ADB (set ADB=...)"; exit 1; }

"$ADB" shell mkdir -p $DEST/data

echo "== config files =="
for f in config.ini param.ini option.ini comment.ini cVer.bin cFileList.bin; do
  [ -f "$CLIENT/$f" ] && "$ADB" push "$(win "$CLIENT/$f")" $DEST/ >/dev/null && echo "  $f"
done

push_dir() {
  local d="$1"
  [ -d "$CLIENT/data/$d" ] || { echo "  (skip $d — not present)"; return; }
  local mb; mb=$(du -sm "$CLIENT/data/$d" | cut -f1)
  echo "  $d (${mb} MB) ..."
  "$ADB" push "$(win "$CLIENT/data/$d")" $DEST/data/ >/dev/null
}

echo "== data (mode: $MODE) =="
push_dir gui
push_dir glogic
push_dir glogicserver
if [ "$MODE" != "minimal" ]; then
  push_dir animation
  push_dir map
  push_dir skin
  push_dir skinobject
  push_dir object
  push_dir piece
  push_dir skeleton
  push_dir effect
  push_dir help
fi

# Textures live at <root>/Textures (SUBPATH::TEXTURE_FILE_ROOT), NOT under data/.
push_tex() {
  local d="$1"
  [ -d "$CLIENT/textures/$d" ] || { echo "  (skip textures/$d - not present)"; return; }
  local mb; mb=$(du -sm "$CLIENT/textures/$d" | cut -f1)
  echo "  textures/$d ($mb MB) ..."
  "$ADB" push "$(win "$CLIENT/textures/$d")" $DEST/textures/ >/dev/null
}

echo "== textures =="
"$ADB" shell mkdir -p $DEST/textures
push_tex gui
push_tex lobi
if [ "$MODE" != "minimal" ]; then
  for d in char item mob effect club shadow map bike vehicle rank ranking worldbattle; do
    push_tex $d
  done
fi

echo "== done =="
"$ADB" shell du -sh $DEST 2>/dev/null || true
echo
echo "Then:  adb install -r out/RanOnlineV001.apk"
echo "       adb shell am start -n com.ran.native/android.app.NativeActivity"
echo "       adb logcat -s RanMain RanApp RanD3D RanD3DX RanSound RanShell"
