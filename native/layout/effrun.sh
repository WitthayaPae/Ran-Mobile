#!/bin/bash
# Dump record layouts for the network structs on both ABIs and diff the sizes.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
N="$HERE/.."
SRC="$N/../../SOURCE"
U="/c/Program Files/Unity/Hub/Editor/6000.5.8f1/Editor/Data/PlaybackEngines/AndroidPlayer"
CLANG="$U/NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe"

INC=(-I"$N/shim/win" -I"$N/shim/d3d" -I"$SRC/Tik/DXInclude" -I"$N/third_party/minilzo-2.10"
     -I"$N/third_party/lua-5.0.3/include" -I"$SRC/Tik/Include"
     -I"$SRC/Lib_Client" -I"$SRC/Lib_Client/G-Logic" -I"$SRC/Lib_Client/NpcTalk"
     -I"$SRC/Lib_Engine" -I"$SRC/Lib_Engine/Common" -I"$SRC/Lib_Engine/DxBase"
     -I"$SRC/Lib_Engine/DxCommon" -I"$SRC/Lib_Engine/DxCommon9" -I"$SRC/Lib_Engine/DxEffect"
     -I"$SRC/Lib_Engine/DxEffect/Char" -I"$SRC/Lib_Engine/DxEffect/EffAni"
     -I"$SRC/Lib_Engine/DxEffect/EffKeep" -I"$SRC/Lib_Engine/DxEffect/EffProj"
     -I"$SRC/Lib_Engine/DxEffect/Single" -I"$SRC/Lib_Engine/DxFrame" -I"$SRC/Lib_Engine/dxframe"
     -I"$SRC/Lib_Engine/DxOctree" -I"$SRC/Lib_Engine/Meshs" -I"$SRC/Lib_Engine/DxSound"
     -I"$SRC/Lib_Engine/G-Logic" -I"$SRC/Lib_Engine/GUInterface" -I"$SRC/Lib_Engine/Hash"
     -I"$SRC/Lib_Engine/NaviMesh" -I"$SRC/Lib_Engine/TextTexture" -I"$SRC/Lib_Engine/Utils"
     -I"$SRC/Lib_Helper" -I"$SRC/Lib_ClientUI/Interface" -I"$SRC/Lib_ClientUI"
     -I"$SRC/Lib_Network" -I"$SRC/Lib_ZLib")

FLAGS=(-std=gnu++14 -fms-extensions -fms-compatibility -fms-compatibility-version=19.30
       -fdelayed-template-parsing -Wno-everything
       -DWIN32 -D_WINDOWS -D_LIB -DNDEBUG -DRAN_MOBILE=1
       -D_CRT_SECURE_NO_WARNINGS -D_WINSOCK_DEPRECATED_NO_WARNINGS
       -DBOOST_SP_USE_PTHREADS -DBOOST_DISABLE_WIN32
       -include "$SRC/Lib_Client/stdafx.h")

dump () { # $1 = triple, $2 = output
  "$CLANG" --target="$1" "${FLAGS[@]}" "${INC[@]}" \
      -Xclang -fdump-record-layouts -fsyntax-only "$HERE/effprobe.cpp" > "$2" 2> "$2.err" || true
  grep -E "^ *[0-9]+ \| (struct|class) |^ *\| \[sizeof=" "$2" > /dev/null || true
}

echo "== dumping aarch64 (mobile) =="
dump ${RAN_LAYOUT_TARGET:-aarch64-linux-android24} "$HERE/efflayout-arm64.txt"
echo "== dumping i686 (32-bit ABI = server word sizes) =="
dump i686-linux-android24 "$HERE/efflayout-x86.txt"

# extract "Name size" pairs
extract () { awk -f "$HERE/extract.awk" "$1"; return; }
unused_extract () {
  awk '
    /^ *[0-9]+ \| (struct|class) / { name=$0; sub(/.*(struct|class) /,"",name); sub(/ .*/,"",name); next }
    /\[sizeof=/ { if (name!="") { s=$0; sub(/.*\[sizeof=/,"",s); sub(/,.*/,"",s); print name, s; name="" } }
  ' "$1" | sort -u
}
extract "$HERE/efflayout-arm64.txt" > "$HERE/effsizes-arm64.txt"
extract "$HERE/efflayout-x86.txt"   > "$HERE/effsizes-x86.txt"
echo "arm64 records: $(wc -l < "$HERE/effsizes-arm64.txt")   x86 records: $(wc -l < "$HERE/effsizes-x86.txt")"
join "$HERE/effsizes-x86.txt" "$HERE/effsizes-arm64.txt" | awk '$2!=$3 {print $1, "x86="$2, "arm64="$3}' > "$HERE/effmismatches.txt"
echo "MISMATCHES: $(wc -l < "$HERE/effmismatches.txt")"
head -40 "$HERE/effmismatches.txt"
