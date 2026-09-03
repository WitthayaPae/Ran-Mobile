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
