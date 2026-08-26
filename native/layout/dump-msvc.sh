#!/bin/bash
# Dump record layouts using the TRUE MSVC x86 ABI (long long aligns to 8, MSVC
# packing rules) — this is the server's layout, so it is the reference.
HERE="$(cd "$(dirname "$0")" && pwd)"; N="$HERE/.."; SRC="$N/../../SOURCE"
U="/c/Program Files/Unity/Hub/Editor/6000.5.8f1/Editor/Data/PlaybackEngines/AndroidPlayer"
SYSROOT="$U/NDK/toolchains/llvm/prebuilt/windows-x86_64/sysroot"
CLANG="$U/NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe"
"$CLANG" --target=i686-pc-windows-msvc19.30 -nostdinc++ -D_LIBCPP_NO_VCRUNTIME \
  -isystem "$SYSROOT/usr/include/c++/v1" -isystem "$SYSROOT/usr/include/i686-linux-android" -isystem "$SYSROOT/usr/include" \
  -std=gnu++14 -fms-extensions -fms-compatibility -fms-compatibility-version=19.30 -fdelayed-template-parsing -Wno-everything \
  -DWIN32 -D_WINDOWS -D_LIB -DNDEBUG -DRAN_MOBILE=1 -D_CRT_SECURE_NO_WARNINGS -D_WINSOCK_DEPRECATED_NO_WARNINGS \
  -DBOOST_SP_USE_PTHREADS -DBOOST_DISABLE_WIN32 -include "$SRC/Lib_Client/stdafx.h" \
  -I"$N/shim/win" -I"$N/shim/d3d" -I"$SRC/Tik/DXInclude" -I"$N/third_party/minilzo-2.10" \
  -I"$N/third_party/lua-5.0.3/include" -I"$SRC/Tik/Include" \
  -I"$SRC/Lib_Client" -I"$SRC/Lib_Client/G-Logic" -I"$SRC/Lib_Client/NpcTalk" -I"$SRC/Lib_Engine" \
  -I"$SRC/Lib_Engine/Common" -I"$SRC/Lib_Engine/DxBase" -I"$SRC/Lib_Engine/DxCommon" -I"$SRC/Lib_Engine/DxCommon9" \
  -I"$SRC/Lib_Engine/DxEffect" -I"$SRC/Lib_Engine/DxEffect/Char" -I"$SRC/Lib_Engine/DxEffect/EffAni" \
  -I"$SRC/Lib_Engine/DxEffect/EffKeep" -I"$SRC/Lib_Engine/DxEffect/EffProj" -I"$SRC/Lib_Engine/DxEffect/Single" \
  -I"$SRC/Lib_Engine/DxFrame" -I"$SRC/Lib_Engine/dxframe" -I"$SRC/Lib_Engine/DxOctree" -I"$SRC/Lib_Engine/Meshs" \
  -I"$SRC/Lib_Engine/DxSound" -I"$SRC/Lib_Engine/G-Logic" -I"$SRC/Lib_Engine/GUInterface" -I"$SRC/Lib_Engine/Hash" \
  -I"$SRC/Lib_Engine/NaviMesh" -I"$SRC/Lib_Engine/TextTexture" -I"$SRC/Lib_Engine/Utils" -I"$SRC/Lib_Helper" \
  -I"$SRC/Lib_ClientUI/Interface" -I"$SRC/Lib_ClientUI" -I"$SRC/Lib_Network" -I"$SRC/Lib_ZLib" \
  -Xclang -fdump-record-layouts -fsyntax-only "$HERE/probe.cpp" > "$HERE/layout-msvc.txt" 2> "$HERE/layout-msvc.err"
echo "msvc errors: $(grep -c 'error:' "$HERE/layout-msvc.err")"
grep -m2 "error:" "$HERE/layout-msvc.err"
