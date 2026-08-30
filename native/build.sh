#!/bin/bash
# Native Android build of the PC client. Usage: ./build.sh [target] [--clean]
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
U="/c/Program Files/Unity/Hub/Editor/6000.5.8f1/Editor/Data/PlaybackEngines/AndroidPlayer"
NDK="$U/NDK"; CM="$U/SDK/cmake/3.22.1/bin"

#  ASAN=1 builds with AddressSanitizer. The runtime and a wrap.sh have to go
#  in the APK too - build-apk.sh does that when ASAN=1 is set there as well.
ASAN_ARGS="-DRAN_ASAN=OFF -DRAN_BOUNDS=OFF"
if [ "${BOUNDS:-0}" = 1 ]; then
  ASAN_ARGS="-DRAN_ASAN=OFF -DRAN_BOUNDS=ON"
  echo "(array-bounds trapping build)"
fi
if [ "${ASAN:-0}" = 1 ]; then
  ASAN_ARGS="-DRAN_ASAN=ON -DRAN_BOUNDS=OFF"
  echo "(AddressSanitizer build)"
fi
ABI=${ABI:-arm64-v8a}
OUT="$HERE/out/$ABI"
[ "$2" = "--clean" ] && rm -rf "$OUT"
mkdir -p "$OUT"
"$CM/cmake.exe" -S "$HERE" -B "$OUT" -G Ninja \
  -DCMAKE_MAKE_PROGRAM="$CM/ninja.exe" \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  $ASAN_ARGS \
  -DANDROID_ABI=$ABI -DANDROID_PLATFORM=android-24 -DANDROID_STL=c++_shared \
  -DCMAKE_BUILD_TYPE=${BUILD_TYPE:-RelWithDebInfo} > "$OUT/configure.log" 2>&1 || { tail -40 "$OUT/configure.log"; exit 1; }
"$CM/ninja.exe" -C "$OUT" -k 0 ${1:-} 2>&1 | tee "$OUT/build.log" | grep -E 'error:|FAILED|warning: .*(implicit|deprecated)' | head -${MAXERR:-60}
echo "--- errors: $(grep -c 'error:' "$OUT/build.log")  failed: $(grep -c '^FAILED' "$OUT/build.log")"
