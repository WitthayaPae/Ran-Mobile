# iOS Port Plan

Written 2026-09-03, after the Android native port reached playable. Same principle as
`NATIVE-PORT-PLAN.md`: compile the real PC client so behaviour is the PC code by
construction, and keep every platform difference inside the shim.

**Read the last section first.** The engineering is the easy half; whether this can be
*shipped* is a separate question with a different answer.

## What already ports, measured

The shim was written against Win32/D3D9, not against Android. Most of it has no idea what
platform it is on.

| Layer | files | lines | Android-specific? |
|---|---|---|---|
| `shim/win` (Win32/MFC) | 89 | 5,940 | two spots, below |
| `shim/d3d` (D3D9 + D3DX) | 19 | 7,719 | one file |
| `shim/gl` (GLES renderer) | 4 | 4,392 | EGL, in one file |
| `shim/platform` (input, audio, splash, touch HUD) | 6 | 3,236 | logging only |
| `platform/android` (entry point, JNI) | 3 | 1,425 | all of it |

Counting the actual Android API surface across the whole native tree: **70** uses of
`__android_log`, EGL confined to `gl_context.cpp`, JNI confined to `android_main.cpp`,
7 uses of `ANativeWindow`. That is the entire dependency.

So the port is not "rewrite the client for iOS". It is **replace ~1,400 lines of entry
point and swap four things**.

## What has to be replaced

**1. Entry point and window — `platform/android/android_main.cpp` (1,425 lines).**
Rewrite as a UIKit `UIApplicationDelegate` + `CAEAGLLayer` (or Metal layer via ANGLE).
The file is mostly `android_app` lifecycle, touch dispatch into `RanTouch_*`, and the JNI
calls for the soft keyboard - each has a direct UIKit equivalent. The keyboard bridge
(`nativeCommitText` / `nativeBackspace` / `nativeEnter`) becomes a hidden `UITextField`
delegate calling the same three C entry points.

**2. EGL → EAGL/ANGLE — `shim/gl/gl_context.cpp`.**
Only this file knows about EGL. Two routes:

* **EAGL + OpenGL ES 3.0.** Least work. iOS caps at ES 3.0 - and the renderer *already*
  has that path: `g_haveAttribFormat` gates the ES 3.1 separate-attribute-format
  optimisation and falls back, with `/sdcard/ran/noattribformat` to force it. The fallback
  is already exercised. Risk: OpenGL ES has been deprecated on iOS since iOS 12. It still
  works, but it is a dead API and Apple can drop it.
* **ANGLE (GLES on Metal).** More setup, no deprecation exposure, and the same GLES code
  compiles unchanged. This is what Chrome and Unity do on iOS.

Recommendation: **EAGL first to get it on screen, ANGLE before shipping.**

**3. Image decode — `shim/d3d/image_decode_android.cpp`.**
`dlopen`s `libjnigraphics.so` for `AImageDecoder`. Replace with ImageIO, or with the DDS
path plus `stb_image` - the file is small and self-contained. DDS decoding is already ours
and portable.

**4. Font discovery — `shim/win/ttf_raster.cpp`.**
Scans `/system/fonts`. **iOS has no equivalent directory** - system fonts are not files an
app may open. Either bundle the faces in the app (check the licence for the Thai face -
this is the one that matters, see `thai-encoding-two-sources` and `ui-metrics-follow-named-font`)
or go through CoreText. The rasteriser itself is ours and portable; only discovery changes.

**5. Logging.** 70 `__android_log_print` calls behind one macro. An afternoon.

**Not a cost:** audio. `dsutil_mobile.cpp` is still silent on Android too - every call
succeeds and nothing plays. Whatever backend gets written (OpenSL/AAudio there,
AVAudioEngine here) is the same unwritten work on both, not an iOS tax.

## What the patcher becomes

The data half ports directly. The manifest format, the content-addressed blob store, the
P-256 signature check and the resume logic are all plain logic; the Java in `RanLauncher`
is ~1,000 lines that becomes Swift. Blobs are game data - models, textures, ini, rcc - and
downloading data is allowed.

**The APK self-updater does not port, and cannot.** iOS has no equivalent to
`PackageInstaller`: an app cannot install a new version of itself, and downloading
executable code is prohibited outright. So on iOS:

* data changes patch exactly as they do now;
* **code changes require an App Store (or TestFlight) release**, with review latency.

That inverts the working rhythm this project has settled into, where a `MAKE-PATCH` run
puts new code on devices in minutes. Batching code changes stops being good practice and
starts being mandatory.

## Sandbox and storage

The payload is 4.7 GB. On iOS that must live under Application Support (not Documents,
which is user-visible and iCloud-synced) and **must be flagged
`isExcludedFromBackupKey`** - otherwise iOS tries to back up 4.7 GB to iCloud, which is
both a bad experience and a documented rejection reason. The launcher's "adopt private
root" move has a direct analogue and the same one-off cost.

## Build and test

Requires **macOS and Xcode**, which this machine is not. Options: a Mac, a hosted Mac
runner, or a Mac mini as a build box. Nothing about the C++ needs a Mac; the toolchain and
the signing do.

Device testing loses `adb`. `ios-deploy` / `xcrun devicectl` cover install and logs;
screenshots are scriptable. The measurement habits this port depends on - frame budget
lines, the `/sdcard/ran/*` diagnostic flags - all keep working, but the flag directory
needs to move into the sandbox and be settable from somewhere (a debug menu, or a file
dropped via Files.app).

## Staged plan

1. **Compile.** Xcode project, arm64 device slice, all of `SOURCE/` plus the shim. Expect
   the same class of trouble the Android port had - alignment, `long` size, MSVC-isms - but
   far less of it, because those were already fixed once and LP64 is LP64.
2. **Boot headless.** Entry point + EAGL context + first `glClear`. Reuse the splash path.
3. **First frame.** The renderer is unchanged; if the context is right, the world should
   draw. This is the milestone that proves the thesis.
4. **Input.** Touch dispatch into the existing `RanTouch_*` overlay - it is pure GL and
   maths, and needs no changes. Keyboard bridge via `UITextField`.
5. **Fonts and images.** The two replacements above.
6. **Launcher.** Swift patcher against the same manifest and the same store.
7. **Audio.** Shared work with Android; do it once, behind the existing class shapes.
8. **ANGLE.** Before any submission.

Steps 1-3 are where the risk is, and they are also cheap to attempt. Everything after is
known work.

## The part that is not engineering

**The client is someone else's copyright.** The build prints
`Copyright 2005~2007 IPVG Corp` and `Copyright(c) 2005~2013 XinXere Edutainment Co,Ltd.`
on its own login screen. App Review requires the submitter to hold the rights to what they
ship, and a third-party upload of a commercial MMO client is the kind of thing that gets
rejected and can take the developer account with it.

This does not block a **private** iOS build: a personal development profile installs on
your own devices, and TestFlight distributes to invited testers - though TestFlight builds
still pass App Review, and expire after 90 days.

That is a decision to make *before* the work, not after:

* **personal / TestFlight only** - the plan above stands as written;
* **public App Store** - the licensing question has to be answered first, because no
  amount of engineering makes it go away.

Android has been forgiving here because the APK is distributed directly. iOS has no
equivalent door.

## Progress — 2026-09-03

Written on Windows. **None of the iOS code has been compiled**: there is no Mac here, so
every iOS file below is unverified and the first `build-ios.sh` run should be expected to
need fixing. What *is* verified is that none of it disturbs Android or PC — the Android
build was reconfigured from scratch and rebuilt for both ABIs after every change
(`errors: 0  failed: 0`), the APK repacked, installed on LDPlayer, and the game logged in
and played (world screenshot, 57 fps).

**Platform seams landed first** (these are real, compiled, device-verified changes; they
are what made an iOS entry point possible at all):

* `shim/platform/ran_plat.{h,cpp}` — diagnostic paths, logging, font directory. 60
  `/sdcard/ran/...` literals and 70 log calls across 24 files now go through it.
* `RanImage_DecodePlatform` — one declaration, one implementation per platform, each
  behind its own `#ifdef`.
* `RanAndroid_ImeInsetPerMille` → **`RanPlat_ImeInsetPerMille`**. It is called from
  `SOURCE/Lib_Client/DxGameStage.cpp` (inside `RAN_MOBILE`), so an Android name had leaked
  into shared client code. Verified after the rename: chat still raises the keyboard on
  LDPlayer (`mInputShown=true`). The inset *value* is still untested — the emulator has no
  on-screen keyboard, so it reports 0; that needs the Tab S9.

**New iOS files** (uncompiled):

| File | What it is |
|---|---|
| `platform/ios/ran_ios_main.mm` | The entry point: `UIApplicationMain`, EAGL ES3 context, `CADisplayLink` frame loop mirroring `android_main.cpp`, touch slots into `RanTouch_*`, `UIKeyInput` into `RanIME_InsertUtf8`/`RanIME_Backspace`, keyboard inset from `UIKeyboardWillChangeFrame`. |
| `platform/ios/ran_ios_plat.mm` | Data root (Application Support, excluded from backup), diag root, font dir. `RanIOS_InstallPlatformPaths()` runs before anything in the shim. |
| `platform/ios/Info.plist.in` | Landscape-only, fullscreen, ES3 required, and the ATS exception the plain-HTTP patch host needs. |
| `platform/ios/fonts/` | NotoSansThai Regular+Bold (OFL), Roboto Regular+Bold (Apache 2.0), with licence texts. Named exactly as the Android system files, because `RanFont_Resolve` picks by filename. |
| `shim/d3d/image_decode_ios.mm` | ImageIO decoder. BGRA via `kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little`, then un-premultiplied — `CGBitmapContext` will not take unpremultiplied 8-bit alpha at all. |
| `build-ios.sh` | Configure + build, or `--xcode` to generate a project. |

**CMake** grew one `if(RAN_IOS)` branch per Android-specific line — the shim glob (`.mm`
on iOS), the link libraries (frameworks instead of `log`/`android`/`EGL`/`GLESv3`), and the
app target (`add_executable(MACOSX_BUNDLE)` instead of the NativeActivity `.so`).
`RAN_IOS` is set only by `CMAKE_SYSTEM_NAME=iOS`, so the Android configure is unchanged.

`platform/android/ran_app.cpp` is **not** Android code — it is the boot driver
(`RanApp_Boot`/`Frame`/`Shutdown`). The iOS target names it explicitly rather than moving
it, so the Android file list stays exactly as it was.

### Still to do

1. First compile on a Mac. Everything above is a prediction until then.
2. Audio: `dsound` on iOS has no implementation yet (AudioQueue or AVAudioEngine).
3. Manifest gains per-platform build blocks (`apk` + `ios`, `minBuild`) before an iOS
   client is allowed to talk to the live server. Until then an iOS build would be offered
   an APK it cannot install.
4. Launcher/patch UI — the Java `RanLauncher` has no iOS counterpart yet.
5. `LaunchScreen` storyboard referenced by the plist does not exist yet.
6. ANGLE, before any submission: `OpenGLES` is deprecated on iOS 12+ and still works, but
   is not a foundation to ship on.

### Progress — 2026-09-03, second batch

The iOS entry point as first written could not have built, and finding out why
was the useful part of this batch. The method: take each call the new file
makes, open the file that answers it, and check what that file includes. Three
whole classes of problem came out of it — and one of them was breaking the
**PC** build, not iOS.

**What was actually wrong**

| Where | What | Effect |
|---|---|---|
| `shim/gl/gl_context.cpp` | EGL top to bottom, `<android/native_window.h>`, no guard | the whole GL context is Android-only |
| `gl_render.cpp`, `splash.cpp`, `touch_ui.cpp` | `<GLES3/gl3.h>`, `gl31.h`, `<EGL/egl.h>` | headers that do not exist on iOS |
| `win/malloc.h` | `#include_next <malloc.h>` | Darwin has `<malloc/malloc.h>`; pulled in by most of the tree |
| `win/win_impl.cpp` | `_SC_PHYS_PAGES`, `_SC_AVPHYS_PAGES` | Linux-only sysconf keys |
| `win/path_resolve.cpp` | `/proc/self/fd` | no `/proc` on iOS |
| `SOURCE`, 29 files | `<android/log.h>` + 54 `__android_log_print` | the logging seam had covered the shim only |
| **`SOURCE/Lib_ClientUI/Interface/SkillTrayTab.cpp`** | `<android/log.h>` and `touch_ui.h` **unguarded** | **this would have stopped the MSVC/PC build** |

The last one is the important one. A sweep of all 31 mobile-only includes in
`SOURCE` says it was the only unguarded one; the other 30 are behind
`#ifdef RAN_MOBILE` as they should be.

**What was added**

* `shim/gl/gl_context_ios.mm` — the same `RanGL_*` API on EAGL: context,
  framebuffer, colour/depth/stencil renderbuffers, present, the context handover
  the loading thread needs, and the same renderScale/UIScale arithmetic. It also
  gains `RanGL_SurfaceChanged`, which Android never needed because its window is
  fixed landscape.
* `shim/gl/gl_platform.h` — one place that says what "GL" means: the header
  path, `GL_APIENTRY`, and `RanGL_ProcAddress` in place of `eglGetProcAddress`.
  On iOS it returns NULL for everything, which is the truth rather than a stub:
  there is no ES 3.1 vertex-attrib-format, no `glBufferStorageEXT`, no disjoint
  timer query. Checked first that nothing calls an ES 3.1 entry point directly —
  all four go through resolved pointers — and that the EXT enums the renderer
  uses are already self-defined behind `#ifndef`.
* `platform/ios/ran_ios_patch.mm` — the patcher, mirroring `RanLauncher.java`
  step for step against the same signed store. No APK offer (iOS cannot install
  over itself); the build gate is `minIos`, and a manifest without one is
  refused rather than assumed safe.
* `platform/ios/icons/` — the 13 icon sizes, cut by `make-ios-icons.js` from the
  same 512px master the Android icons came from.

`RanGL_Init` owns the EAGL context now, not the view controller, so there is one
place that knows how a frame is presented — as on Android.

**Verified after every change**: Android reconfigured from scratch, both ABIs
rebuilt clean, APK reinstalled on LDPlayer, logged in and played. The converted
log lines come out with their tags and levels intact.

### What is left before an iOS build can run

1. First compile on a Mac. Everything here is still a prediction.
2. **Audio.** `dsutil_mobile.cpp` is a stub on *both* platforms — the game is
   silent on Android too. Shared work, not an iOS gap.
3. Publish a manifest with `minIos` (`make-manifest.js --min-ios <n>`) before
   pointing an iOS client at the live server.
4. Signing: a development profile, or TestFlight. See the copyright section
   above first — that decision comes before the work, not after.
