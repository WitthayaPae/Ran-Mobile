#!/bin/bash
# Push client data to the wireless tablet.
#
# Same content as push-data.sh, with two differences that matter over Wi-Fi:
# only the maps actually visited are sent (the full map tree is 2.9 GB of which
# the login, character-select and school maps are all that is reachable today),
# and every step prints its size so a stalled transfer is obvious.
#
#   ADB=<adb> DEV=<serial> ./push-tablet.sh
set -e
export MSYS_NO_PATHCONV=1
win() { cygpath -w "$1"; }

HERE="$(cd "$(dirname "$0")" && pwd)"
CLIENT="$HERE/../../CLIENT"
ADB="${ADB:-/c/Program Files/Unity/Hub/Editor/6000.5.8f1/Editor/Data/PlaybackEngines/AndroidPlayer/SDK/platform-tools/adb.exe}"
DEV="${DEV:?set DEV to the device serial}"
DEST=/sdcard/ran

a() { "$ADB" -s "$DEV" "$@"; }

a shell mkdir -p $DEST/data $DEST/textures $DEST/data/map

echo "== config =="
for f in config.ini param.ini option.ini comment.ini cVer.bin cFileList.bin; do
  [ -f "$CLIENT/$f" ] && a push "$(win "$CLIENT/$f")" $DEST/ >/dev/null && echo "  $f"
done

push_dir() {
  local d="$1"
  [ -d "$CLIENT/data/$d" ] || { echo "  (skip $d)"; return; }
  local mb; mb=$(du -sm "$CLIENT/data/$d" | cut -f1)
  echo "  data/$d (${mb} MB) ..."
  a push "$(win "$CLIENT/data/$d")" $DEST/data/ >/dev/null
}

push_tex() {
  local d="$1"
  [ -d "$CLIENT/textures/$d" ] || { echo "  (skip textures/$d)"; return; }
  local mb; mb=$(du -sm "$CLIENT/textures/$d" | cut -f1)
  echo "  textures/$d (${mb} MB) ..."
  a push "$(win "$CLIENT/textures/$d")" $DEST/textures/ >/dev/null
}

echo "== data =="
for d in gui glogic glogicserver skeleton object piece skinobject skin effect help animation; do
  push_dir "$d"
done

echo "== maps (only the ones the client can reach today) =="
for m in log_in cha_select w_school_01; do
  for f in "$CLIENT"/data/map/$m.*; do
    [ -f "$f" ] || continue
    a push "$(win "$f")" "$DEST/data/map/$(basename "$f")" >/dev/null
  done
  echo "  $m"
done

echo "== textures =="
for d in gui lobi char item mob effect club shadow map bike vehicle rank ranking worldbattle; do
  push_tex "$d"
done

echo "== done =="
a shell du -sh $DEST 2>/dev/null || true
