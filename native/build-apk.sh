#!/bin/bash
# Package libran.so into an installable APK. No Gradle: aapt2 + zipalign +
# apksigner straight from the SDK that ships with Unity.
#
#   ./build.sh && ABI=x86_64 ./build.sh && ./build-apk.sh   -> out/RanOnline.apk
# MAKE-PATCH.bat passes NAME=RanOnlineV<nnn>, from android:versionName.
#
# Every ABI that has been built is included: arm64-v8a for real devices, x86_64
# for the LDPlayer emulator (which reports x86_64, not ARM).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
U="/c/Program Files/Unity/Hub/Editor/6000.5.8f1/Editor/Data/PlaybackEngines/AndroidPlayer"
BT="$U/SDK/build-tools/36.0.0"
PLATFORM="$U/SDK/platforms/android-34/android.jar"
JAVA="$U/OpenJDK/bin/java.exe"
JAVAC="$U/OpenJDK/bin/javac.exe"
ABIS="${ABIS:-arm64-v8a x86_64}"
OUT="$HERE/out/apk"
NAME="${NAME:-RanOnline}"

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

  #  An ASan build needs its runtime beside the library, and a wrap.sh, which
  #  Android runs in place of the app for a debuggable APK. That is the only
  #  way in without root.
  if [ "${ASAN:-0}" = 1 ] && [ "$A" = arm64-v8a ]; then
    RT="$(ls "$U/NDK/toolchains/llvm/prebuilt/windows-x86_64/lib/clang/"*/lib/linux/libclang_rt.asan-aarch64-android.so | head -1)"
    cp "$RT" "$OUT/lib/$A/"
    #  LF endings, or /system/bin/sh will not run it.
    tr -d "" < "$HERE/android/wrap.sh" > "$OUT/lib/$A/wrap.sh"
    chmod +x "$OUT/lib/$A/wrap.sh"
    echo "  + asan runtime + wrap.sh"
  fi
  printf "  + %-12s %.1f MB\n" "$A" "$(stat -c%s "$SO" | awk '{print $1/1048576}')"
  HAVE="$HAVE $A"
done
[ -n "$HAVE" ] || { echo "[!] nothing built — run ./build.sh first"; exit 1; }

# The Java launcher -> classes.dex.
#
# The APK was hasCode="false" and pure NativeActivity until the patcher needed
# somewhere to live. javac then d8 straight from the SDK, same as everything
# else here - no Gradle.
echo "== java =="
JSRC="$HERE/android/java"
if [ -d "$JSRC" ]; then
  JOUT="$OUT/classes"
  mkdir -p "$JOUT"
  #  javac and d8 are Windows tools: they need Windows paths, and this tree
  #  lives under "DEV EP9", a directory with a space in it. An @argfile of
  #  bare POSIX paths splits on that space and javac reports
  #  "invalid flag: /c/Users/.../DEV". Quoted paths in the argfile, and a
  #  jar handed to d8 rather than a list of .class files, keep every path a
  #  single argument.
  find "$JSRC" -name '*.java' | while read -r f; do
    printf '"%s"\n' "$(cygpath -m "$f")"
  done > "$OUT/java.list"
  "$JAVAC" -source 8 -target 8 -nowarn -encoding UTF-8 \
      -bootclasspath "$(cygpath -w "$PLATFORM")" \
      -classpath "$(cygpath -w "$PLATFORM")" \
      -d "$(cygpath -w "$JOUT")" "@$(cygpath -w "$OUT/java.list")" 2>&1 | grep -v '^Note:' || true
  CLASSES=$(find "$JOUT" -name '*.class' | wc -l)
  [ "$CLASSES" -gt 0 ] || { echo "[!] javac produced no classes"; exit 1; }
  "$U/OpenJDK/bin/jar.exe" cf "$(cygpath -w "$OUT/classes.jar")" -C "$(cygpath -w "$JOUT")" .
  "$JAVA" -cp "$(cygpath -w "$BT/lib/d8.jar")" com.android.tools.r8.D8 \
      --min-api 24 --lib "$(cygpath -w "$PLATFORM")" \
      --output "$(cygpath -w "$OUT")" "$(cygpath -w "$OUT/classes.jar")"
  printf "  + %s classes -> classes.dex %.1f KB\n" "$CLASSES" \
      "$(stat -c%s "$OUT/classes.dex" | awk '{print $1/1024}')"
else
  echo "  (no java sources)"
fi

# resources (the splash window background) -> flat archive, then link
"$BT/aapt2.exe" compile --dir "$(cygpath -w "$HERE/android/res")" -o "$(cygpath -w "$OUT/res.zip")"

# manifest + resources -> base APK
# The shipped manifest is not debuggable. DEBUGGABLE=1 puts the flag back, on a
# copy, so a debugger can be attached without that ever being the default.
MANIFEST="$HERE/android/AndroidManifest.xml"
if [ "${DEBUGGABLE:-0}" = 1 ]; then
  MANIFEST="$OUT/AndroidManifest.debuggable.xml"
  sed 's|<application|<application android:debuggable="true"|' \
      "$HERE/android/AndroidManifest.xml" > "$MANIFEST"
  echo "  (debuggable build)"
fi

"$BT/aapt2.exe" link -o "$OUT/base.apk" -I "$PLATFORM" \
  --manifest "$MANIFEST" --min-sdk-version 24 --target-sdk-version 34 "$(cygpath -w "$OUT/res.zip")"

# aapt2 link cannot add arbitrary files, so the lib/ tree goes in with a plain
# zip update — STORED, because Android loads .so straight out of the APK.
WINAPK="$(cygpath -w "$OUT/base.apk")"
WINLIB="$(cygpath -w "$OUT/lib")"
WINDEX="$(cygpath -w "$OUT/classes.dex")"
powershell.exe -NoProfile -Command "
  Add-Type -A System.IO.Compression.FileSystem
  \$zip = [System.IO.Compression.ZipFile]::Open('$WINAPK','Update')
  foreach (\$f in Get-ChildItem -Recurse -File '$WINLIB') {
    \$rel = 'lib/' + \$f.Directory.Name + '/' + \$f.Name
    [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(\$zip, \$f.FullName, \$rel, [System.IO.Compression.CompressionLevel]::NoCompression)
  }
  if (Test-Path '$WINDEX') {
    [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(\$zip, '$WINDEX', 'classes.dex', [System.IO.Compression.CompressionLevel]::Optimal)
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
