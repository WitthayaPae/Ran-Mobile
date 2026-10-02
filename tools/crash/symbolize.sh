#!/bin/bash
# Turn a crash report's "libran.so+0x..." frames into function, file and line.
#
#   tools/crash/symbolize.sh report.txt
#
# Reports come from players' phones (ran-legacy-m.com/crash/, written by
# RanCrash in shim/platform/ran_plat.cpp). The shipped libran.so is stripped,
# so the frames are offsets; this finds the debug info for the exact build the
# report names (build-id), which build-apk.sh files under
# native/out/symbols/<build-id>/<abi>/libran.debug, and asks llvm-symbolizer.
#
# Android only for now: an iOS report has the frames but not a build-id, and
# its symbols are in the CI build's dSYM.
set -e
REPORT="$1"
[ -f "$REPORT" ] || { echo "usage: $0 report.txt"; exit 1; }

HERE="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="/c/Program Files/Unity/Hub/Editor/6000.5.8f1/Editor/Data/PlaybackEngines/AndroidPlayer/NDK/toolchains/llvm/prebuilt/windows-x86_64/bin"

BID=$(sed -n 's/^build-id: \([0-9a-f]*\).*/\1/p' "$REPORT" | head -1)
ABI=$(sed -n 's/^platform: android .* \([a-z0-9_-]*\)$/\1/p' "$REPORT" | head -1)
[ -n "$BID" ] || { echo "no build-id in the report"; exit 1; }
[ -n "$ABI" ] || { echo "not an Android report (no ABI on the platform line)"; exit 1; }

SYM="$HERE/native/out/symbols/$BID/$ABI/libran.debug"
if [ ! -f "$SYM" ]; then
  #  The build on disk may be the one that crashed, if it was never archived.
  CUR="$HERE/native/out/$ABI/libran.so"
  CURBID=$("$BIN/llvm-readelf.exe" -n "$(cygpath -w "$CUR")" 2>/dev/null | awk '/Build ID:/ {print $3; exit}')
  if [ "$CURBID" = "$BID" ]; then SYM="$CUR"
  else echo "no symbols for build-id $BID ($ABI) - was this APK built on this machine?"; exit 1
  fi
fi
echo "symbols: $SYM"
echo

#  Every frame line, in report order, with its offset resolved underneath.
grep -E '^  #[0-9]+ pc |^(fault frame|backtrace|frame chain):$' "$REPORT" | while IFS= read -r line; do
  echo "$line"
  off=$(echo "$line" | sed -n 's/.*libran\.so+\(0x[0-9a-f]*\).*/\1/p')
  [ -n "$off" ] || continue
  "$BIN/llvm-symbolizer.exe" --obj="$(cygpath -w "$SYM")" --demangle --inlines "$off" |
    sed '/^$/d; s/^/        /'
done
