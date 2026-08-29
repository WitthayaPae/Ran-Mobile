#!/bin/bash
# Package libran.so into an installable APK. No Gradle: aapt2 + zipalign +
# apksigner straight from the SDK that ships with Unity.
#
#   ./build.sh && ABI=x86_64 ./build.sh && ./build-apk.sh   -> out/ran-phase2.apk
#
# Every ABI that has been built is included: arm64-v8a for real devices, x86_64
# for the LDPlayer emulator (which reports x86_64, not ARM).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
U="/c/Program Files/Unity/Hub/Editor/6000.5.8f1/Editor/Data/PlaybackEngines/AndroidPlayer"
BT="$U/SDK/build-tools/36.0.0"
PLATFORM="$U/SDK/platforms/android-34/android.jar"
JAVA="$U/OpenJDK/bin/java.exe"
ABIS="${ABIS:-arm64-v8a x86_64}"
OUT="$HERE/out/apk"
NAME="${NAME:-ran-phase2}"

rm -rf "$OUT"; mkdir -p "$OUT/res"

echo "== native libs =="
HAVE=""
for A in $ABIS; do
  SO="$HERE/out/$A/libran.so"
  if [ ! -f "$SO" ]; then echo "  (skip $A — not built)"; continue; fi
  case "$A" in
    arm64-v8a)   TRIPLE=aarch64-linux-android ;;
    x86_64)      TRIPLE=x86_64-linux-android ;;
    armeabi-v7a) TRIPLE=arm-linux-androideabi ;;
    x86)         TRIPLE=i686-linux-android ;;
    *)           echo "  (unknown ABI $A)"; continue ;;
  esac
  STL="$U/NDK/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/$TRIPLE/libc++_shared.so"
  mkdir -p "$OUT/lib/$A"
  cp "$SO" "$OUT/lib/$A/"
  cp "$STL" "$OUT/lib/$A/"
  printf "  + %-12s %.1f MB\n" "$A" "$(stat -c%s "$SO" | awk '{print $1/1048576}')"
  HAVE="$HAVE $A"
done
[ -n "$HAVE" ] || { echo "[!] nothing built — run ./build.sh first"; exit 1; }

# resources (the splash window background) -> flat archive, then link
"$BT/aapt2.exe" compile --dir "$(cygpath -w "$HERE/android/res")" -o "$(cygpath -w "$OUT/res.zip")"

# manifest + resources -> base APK
"$BT/aapt2.exe" link -o "$OUT/base.apk" -I "$PLATFORM" \
  --manifest "$HERE/android/AndroidManifest.xml" --min-sdk-version 24 --target-sdk-version 34 "$(cygpath -w "$OUT/res.zip")"

# aapt2 link cannot add arbitrary files, so the lib/ tree goes in with a plain
# zip update — STORED, because Android loads .so straight out of the APK.
WINAPK="$(cygpath -w "$OUT/base.apk")"
WINLIB="$(cygpath -w "$OUT/lib")"
powershell.exe -NoProfile -Command "
  Add-Type -A System.IO.Compression.FileSystem
  \$zip = [System.IO.Compression.ZipFile]::Open('$WINAPK','Update')
  foreach (\$f in Get-ChildItem -Recurse -File '$WINLIB') {
    \$rel = 'lib/' + \$f.Directory.Name + '/' + \$f.Name
    [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(\$zip, \$f.FullName, \$rel, [System.IO.Compression.CompressionLevel]::NoCompression)
  }
  \$zip.Dispose()"

# debug keystore so the APK can be installed
KS="$HERE/android/debug.keystore"
if [ ! -f "$KS" ]; then
  "$U/OpenJDK/bin/keytool.exe" -genkeypair -keystore "$(cygpath -w "$KS")" -storepass android \
    -keypass android -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 \
    -dname "CN=RAN Debug,O=RAN,C=TH" > /dev/null 2>&1 || true
fi

"$BT/zipalign.exe" -f -p 4 "$WINAPK" "$(cygpath -w "$OUT/aligned.apk")"
"$JAVA" -jar "$(cygpath -w "$BT/lib/apksigner.jar")" sign \
  --ks "$(cygpath -w "$KS")" --ks-pass pass:android --key-pass pass:android \
  --out "$(cygpath -w "$HERE/out/$NAME.apk")" "$(cygpath -w "$OUT/aligned.apk")"

printf "\n%s  %.1f MB   ABIs:%s\n" "out/$NAME.apk" \
  "$(stat -c%s "$HERE/out/$NAME.apk" | awk '{print $1/1048576}')" "$HAVE"
echo "install:  adb install -r out/$NAME.apk"
echo "data:     ./push-data.sh minimal   (the client data does NOT go in the APK)"
