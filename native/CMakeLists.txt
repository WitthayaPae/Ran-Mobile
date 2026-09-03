cmake_minimum_required(VERSION 3.22)
project(RanNative CXX C)

#  AddressSanitizer, for hunting memory corruption on the device.
#
#  -DRAN_ASAN=ON from build.sh (ASAN=1). The runtime .so and a wrap.sh have to
#  be packaged as well, which build-apk.sh does with the same switch.
option(RAN_ASAN "Build with AddressSanitizer" OFF)
#  Trapping bounds checks, for hunting a stray write.
#
#  -DRAN_BOUNDS=ON from build.sh (BOUNDS=1). Unlike AddressSanitizer this needs
#  no runtime library and no wrap.sh: the compiler plants a trap at the offending
#  access, so the tombstone points straight at the line. It only sees arrays
#  whose size the compiler knows, which is the shape of the bug being hunted -
#  a four-byte write landing on somebody else's pointer.
option(RAN_BOUNDS "Build with trapping array-bounds checks" OFF)
if(RAN_BOUNDS)
    add_compile_options(-fsanitize=array-bounds -fsanitize-trap=array-bounds -g)
    message(STATUS "array-bounds trapping: on")
endif()
if(RAN_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address)
    message(STATUS "AddressSanitizer: on")
endif()

#  Which mobile platform. Android is the default because it is what build.sh
#  drives; iOS is selected by the toolchain (-DCMAKE_SYSTEM_NAME=iOS), so
#  nothing about the Android build changes.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(RAN_IOS ON)
  message(STATUS "target: iOS")
endif()

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

set(RAN_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../SOURCE")
set(SEMI ";")
set(SHIM "${CMAKE_CURRENT_SOURCE_DIR}/shim")
set(TP "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

# ---- global flags: old MSVC code base -----------------------------------------
add_compile_options(
  -fms-extensions -fms-compatibility -fdelayed-template-parsing
  -fno-strict-aliasing -fsigned-char -fwrapv
  -frtti -fexceptions -fms-compatibility-version=19.30
  -Wno-everything
  #  Kept on deliberately: MSVC passes a class BY VALUE through varargs (which is
  #  what makes MFC's printf("%s", someCString) idiom work), while the Itanium ABI
  #  clang uses passes it by invisible reference - so "%s" prints the object's
  #  internals instead of the text. This warning is the only thing that finds them.
  -Wnon-pod-varargs
)
add_compile_definitions(
  WIN32 _WINDOWS _LIB NDEBUG
  RAN_MOBILE=1
  _CRT_SECURE_NO_WARNINGS _CRT_NON_CONFORMING_SWPRINTFS
  _WINSOCK_DEPRECATED_NO_WARNINGS
  UNICODE_SHIM=0
  BOOST_SP_USE_PTHREADS BOOST_DISABLE_WIN32
)

# shim first, then real DX headers (d3d9.h/d3dx9*.h/dinput.h/dsound.h reused verbatim)
set(RAN_COMMON_INCLUDES
  ${SHIM}/win
  ${SHIM}/d3d
  ${RAN_SRC}/Tik/DXInclude
  ${TP}/minilzo-2.10
  ${TP}/lua-5.0.3/include
  ${RAN_SRC}/Tik/Include
  ${TP}/ogg-1.3.5/include
  ${TP}/vorbis-1.3.7/include
)

set(ENGINE_INCLUDES
  ${RAN_SRC}/Lib_Engine ${RAN_SRC}/Lib_Engine/Common ${RAN_SRC}/Lib_Engine/DxBase
  ${RAN_SRC}/Lib_Engine/DxCommon ${RAN_SRC}/Lib_Engine/DxCommon9 ${RAN_SRC}/Lib_Engine/DxEffect
  ${RAN_SRC}/Lib_Engine/DxEffect/Char ${RAN_SRC}/Lib_Engine/DxEffect/EffAni
  ${RAN_SRC}/Lib_Engine/DxEffect/EffKeep ${RAN_SRC}/Lib_Engine/DxEffect/EffProj
  ${RAN_SRC}/Lib_Engine/DxEffect/Single ${RAN_SRC}/Lib_Engine/DxFrame ${RAN_SRC}/Lib_Engine/dxframe
  ${RAN_SRC}/Lib_Engine/DxOctree ${RAN_SRC}/Lib_Engine/Meshs ${RAN_SRC}/Lib_Engine/DxSound
  ${RAN_SRC}/Lib_Engine/G-Logic ${RAN_SRC}/Lib_Engine/GUInterface ${RAN_SRC}/Lib_Engine/Hash
  ${RAN_SRC}/Lib_Engine/NaviMesh ${RAN_SRC}/Lib_Engine/TextTexture ${RAN_SRC}/Lib_Engine/Utils
  ${RAN_SRC}/Lib_Helper ${RAN_SRC}/Lib_ClientUI/Interface
)
set(CLIENT_INCLUDES
  ${RAN_SRC}/Lib_Client ${RAN_SRC}/Lib_Client/G-Logic ${RAN_SRC}/Lib_Client/NpcTalk
  ${RAN_SRC}/Lib_Network ${RAN_SRC}/Lib_ClientUI
)

# ---- third party ---------------------------------------------------------------
include(cmake/sources_Lib_ZLib.cmake)
add_library(ranzlib STATIC ${Lib_ZLib_SOURCES})
target_include_directories(ranzlib PUBLIC ${RAN_SRC}/Lib_ZLib)

add_library(minilzo STATIC ${TP}/minilzo-2.10/minilzo.c)
target_include_directories(minilzo PUBLIC ${TP}/minilzo-2.10)

file(GLOB LUA_SRC ${TP}/lua-5.0.3/src/*.c ${TP}/lua-5.0.3/src/lib/*.c)
add_library(lua50 STATIC ${LUA_SRC})
target_include_directories(lua50 PUBLIC ${TP}/lua-5.0.3/include PRIVATE ${TP}/lua-5.0.3/src)

file(GLOB OGG_SRC ${TP}/ogg-1.3.5/src/*.c)
add_library(ogg STATIC ${OGG_SRC})
target_include_directories(ogg PUBLIC ${TP}/ogg-1.3.5/include)

set(VORBIS_SRC mdct.c smallft.c block.c envelope.c window.c lsp.c lpc.c analysis.c synthesis.c
  psy.c info.c floor1.c floor0.c res0.c mapping0.c registry.c codebook.c sharedbook.c
  lookup.c bitrate.c vorbisfile.c)
list(TRANSFORM VORBIS_SRC PREPEND ${TP}/vorbis-1.3.7/lib/)
add_library(vorbis STATIC ${VORBIS_SRC})
target_include_directories(vorbis PUBLIC ${TP}/vorbis-1.3.7/include PRIVATE ${TP}/vorbis-1.3.7/lib)
target_link_libraries(vorbis PUBLIC ogg)

# ---- shim runtime (Win32 emulation, D3D9-on-GLES, D3DX) ------------------------
#
#  On iOS the platform files are Objective-C++ (.mm) - ImageIO and UIKit have no
#  C interface worth using. Each is guarded by its own #ifdef, exactly as the
#  Android ones are, so the glob can stay indiscriminate.
file(GLOB_RECURSE SHIM_SRC ${SHIM}/*.cpp)
if(RAN_IOS)
  enable_language(OBJCXX)
  file(GLOB_RECURSE SHIM_SRC_MM ${SHIM}/*.mm)
  list(APPEND SHIM_SRC ${SHIM_SRC_MM})
endif()
add_library(ranshim STATIC ${SHIM_SRC})
# Temporary: split the frame between engine time and draw submission.
# Per-draw timing for the frame-budget line. Two clock reads per draw, so it
# stays off unless a performance question is actually being asked:
target_compile_definitions(ranshim PRIVATE RAN_TIME_DRAWS)
target_include_directories(ranshim PUBLIC ${RAN_COMMON_INCLUDES} ${ENGINE_INCLUDES})
target_compile_options(ranshim PRIVATE -include ${RAN_SRC}/Lib_Engine/StdAfx.h)
if(RAN_IOS)
  #  The iOS equivalents of log/android/EGL/GLESv3/jnigraphics: the GL context
  #  comes from EAGL (OpenGLES), the image decoder from ImageIO, and logging
  #  from NSLog in Foundation.
  target_link_libraries(ranshim PUBLIC
    "-framework OpenGLES" "-framework QuartzCore" "-framework CoreGraphics"
    "-framework ImageIO" "-framework Foundation" "-framework UIKit"
    "-framework AudioToolbox")
else()
  target_link_libraries(ranshim PUBLIC log android EGL GLESv3 jnigraphics)
endif()

# ---- game libraries -------------------------------------------------------------
function(ran_lib name)
  include(cmake/sources_${name}.cmake)
  add_library(${name} STATIC ${${name}_SOURCES})
  target_include_directories(${name} PUBLIC ${RAN_COMMON_INCLUDES} ${RAN_SRC}/${name} ${ENGINE_INCLUDES} ${CLIENT_INCLUDES})
  target_link_libraries(${name} PUBLIC ranshim ranzlib minilzo lua50 vorbis)
  # every TU gets the lib's stdafx.h like MSVC's /Yu did
  if(EXISTS ${RAN_SRC}/${name}/stdafx.h)
    target_compile_options(${name} PRIVATE -include ${RAN_SRC}/${name}/stdafx.h)
  elseif(EXISTS ${RAN_SRC}/${name}/StdAfx.h)
    target_compile_options(${name} PRIVATE -include ${RAN_SRC}/${name}/StdAfx.h)
  endif()
endfunction()

ran_lib(Lib_Helper)
ran_lib(Lib_Network)
ran_lib(Lib_Engine)
ran_lib(Lib_Client)
ran_lib(Lib_ClientUI)

# ---- app ------------------------------------------------------------------------
# The NativeActivity: android_main + the boot driver. native_app_glue ships with
# the NDK and has to be compiled into the app itself.
file(GLOB APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/platform/android/*.cpp)
if(RAN_IOS)
  #  ran_app.cpp - the boot driver, RanApp_Boot/Frame/Shutdown - is not Android
  #  code; it only lives under platform/android because that was the only
  #  platform. Named explicitly here rather than moved, so the Android build
  #  keeps compiling exactly the same file list.
  file(GLOB IOS_SRC ${CMAKE_CURRENT_SOURCE_DIR}/platform/ios/*.mm
                    ${CMAKE_CURRENT_SOURCE_DIR}/platform/ios/*.cpp)
  add_executable(ran MACOSX_BUNDLE
    ${IOS_SRC} ${CMAKE_CURRENT_SOURCE_DIR}/platform/android/ran_app.cpp)
  target_include_directories(ran PRIVATE
    ${RAN_COMMON_INCLUDES} ${ENGINE_INCLUDES} ${CLIENT_INCLUDES})
  target_compile_options(ran PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-include${SEMI}${RAN_SRC}/Lib_Client/stdafx.h>)
  target_link_libraries(ran PRIVATE
    Lib_ClientUI Lib_Client Lib_Engine Lib_Network Lib_Helper ranshim
    ranzlib minilzo lua50 vorbis ogg m)
  #  Fonts ship in the bundle, because iOS will not let an app open the system
  #  faces as files and RanFont_Resolve works by scanning a directory. The
  #  filenames matter: the resolver picks by name ("Thai", "Roboto-", "Bold"),
  #  so these are named exactly as the Android system files it was written
  #  against. Both licences are redistributable - OFL and Apache 2.0 - and the
  #  licence texts ship beside them.
  file(GLOB IOS_FONTS ${CMAKE_CURRENT_SOURCE_DIR}/platform/ios/fonts/*)
  target_sources(ran PRIVATE ${IOS_FONTS})
  set_source_files_properties(${IOS_FONTS} PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources/fonts)

  set_target_properties(ran PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER com.ran.launcher
    MACOSX_BUNDLE_BUNDLE_NAME "Ran Legacy M"
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/platform/ios/Info.plist.in
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2")
elseif(APP_SRC)
  set(GLUE "${CMAKE_ANDROID_NDK}/sources/android/native_app_glue")
  add_library(ran SHARED ${APP_SRC} ${GLUE}/android_native_app_glue.c)
  target_include_directories(ran PRIVATE
    ${RAN_COMMON_INCLUDES} ${ENGINE_INCLUDES} ${CLIENT_INCLUDES} ${GLUE})
  # stdafx force-include is C++ only — native_app_glue.c must not get it.
  target_compile_options(ran PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-include${SEMI}${RAN_SRC}/Lib_Client/stdafx.h>)
  # No whole-archive: link only what the boot path actually pulls in, so a clean
  # link IS the proof that no platform code is still missing.
  target_link_libraries(ran PRIVATE
    Lib_ClientUI Lib_Client Lib_Engine Lib_Network Lib_Helper ranshim
    ranzlib minilzo lua50 vorbis ogg c++_shared log android EGL GLESv3 m)
  # -u keeps ANativeActivity_onCreate (referenced only by the OS) from being stripped.
  target_link_options(ran PRIVATE
    -Wl,--error-limit=0 -Wl,--no-dependent-libraries -u ANativeActivity_onCreate)
endif()

# third-party C libraries must not see the Win32 emulation defines
foreach(t ranzlib minilzo lua50 ogg vorbis)
  target_compile_options(${t} PRIVATE -UWIN32 -U_WINDOWS -U_LIB)
endforeach()

# minizip opens the .rcc archives with plain fopen and never includes windows.h,
# so it needs the path resolver injected explicitly.
target_compile_options(ranzlib PRIVATE $<$<COMPILE_LANGUAGE:C>:-include${SEMI}${SHIM}/win/ran_path_c.h>)
target_include_directories(ranzlib PRIVATE ${SHIM}/win)
