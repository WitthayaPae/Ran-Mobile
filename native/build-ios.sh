#!/bin/sh
#  Configure and build the client for iOS. Runs on a Mac only.
#
#  The Android build (build.sh) is untouched by this: the two share one
#  CMakeLists.txt, and every iOS-specific line in it sits behind if(RAN_IOS),
#  which only CMAKE_SYSTEM_NAME=iOS turns on.
#
#      ./build-ios.sh              device build (arm64), Release
#      SIM=1 ./build-ios.sh        simulator build
#      ./build-ios.sh --xcode      generate an Xcode project instead
#
#  NOT YET RUN - written on Windows, where no part of it can be exercised.
#  Expect the first run on a Mac to need fixing.
set -e
cd "$(dirname "$0")"

BUILD=out/ios
SYSROOT=iphoneos
ARCH=arm64
[ -n "$SIM" ] && { BUILD=out/ios-sim; SYSROOT=iphonesimulator; ARCH="$(uname -m)"; }

GEN="Unix Makefiles"
for a in "$@"; do [ "$a" = "--xcode" ] && GEN=Xcode; done

cmake -S . -B "$BUILD" -G "$GEN" \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT="$SYSROOT" \
  -DCMAKE_OSX_ARCHITECTURES="$ARCH" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH=NO

[ "$GEN" = "Xcode" ] && { echo "open $BUILD/RanNative.xcodeproj"; exit 0; }

cmake --build "$BUILD" -j "$(sysctl -n hw.ncpu)"
echo "built: $BUILD/ran.app"
