# Phase 2 — headless boot

**Built and linked; not yet run on a device.** The owner does device testing —
everything below is ready to install.

## What exists now

- **`out/ran-phase2.apk`** — 158.5 MB, signed, installable. `com.ran.native`,
  a `NativeActivity` with no Java code at all (`android:hasCode="false"`).
- **`libran.so`** — 156.8 MB. The whole PC client: Lib_Engine, Lib_Client,
  Lib_ClientUI, Lib_Network (client subset), Lib_Helper, plus the Win32/MFC and
  D3D9 shims. Links with **zero undefined symbols**, which is the phase-2 proof:
  nothing on the boot path is still missing.
- **`platform/android/android_main.cpp`** — the NativeActivity entry point.
- **`platform/android/ran_app.cpp`** — `RanMobileApp`, the mobile stand-in for
  `CGameClient2Wnd`. Same base class (`CD3DApplication`), same virtuals, same
  engine calls in the same order; only the HackShield threads, the desktop
  cursor and the IE web control are dropped.

## How to run it

```bash
cd MOBILE/native
./build.sh && ./build-apk.sh          # -> out/ran-phase2.apk
./push-data.sh minimal                # config + gui + glogic  (~400 MB)
#   ./push-data.sh                    # everything the boot path reads (~5.5 GB)

adb install -r out/ran-phase2.apk
adb shell am start -n com.ran.native/android.app.NativeActivity
adb logcat -s RanMain RanApp RanD3D RanD3DX RanSound RanShell
```

The game data is **not** in the APK — it is ~5.5 GB and lives at `/sdcard/ran`.
`android_main` refuses to guess: it checks for `data/gui` or `config.ini` and, if
neither is there, logs the exact `adb push` commands instead of failing somewhere
deep inside a loader.

Android 11+ needs *Files and media → Allow management of all files* granted to
the app by hand, or the data will not be readable.

## What the log should show

The boot mirrors `CGameClient2App::InitInstance` step for step:

```
RanMain: === RAN native client (phase 2: headless) ===
RanMain: window 2400x1080
RanMain: data root: /sdcard/ran/
RanApp : === RAN mobile boot === root=/sdcard/ran/ 2400x1080
RanApp : RANPARAM loaded — service=… screen=…x… lang=…
RanApp : Gui.rcc indexed
RanApp : game text loaded
RanD3D : Direct3DCreate9(0x…) — shim (phase 2: headless)
RanD3D : device created 2400x1080
RanApp : OneTimeSceneInit / CreateObjects / InitDeviceObjects
RanApp : === boot complete ===
RanD3D : frame 300 — draws …, clears …, tex … (… MB), vb …, ib …
```

Two kinds of line are the *interesting* output:

- **`RanD3DX: NOT IMPLEMENTED (phase 3): …`** — every D3DX loader the client
  actually reaches, logged once each. That list, in call order, is the phase-3
  work order measured rather than guessed.
- **`RanD3D: frame N — draws …`** — proves the frame loop is turning and shows
  how much geometry and texture the client is pushing per frame, which is the
  budget phase 3 has to hit.

## Deliberate limitations

| Area | Phase 2 behaviour | Becomes real in |
|---|---|---|
| Rendering | device accepts and counts every call, draws nothing | 3 |
| Textures / meshes / `.X` files | loaders log once, then fail cleanly | 3 |
| Fonts | `D3DXCreateFontIndirect` fails; the engine falls back to its atlas path | 3 |
| Sound | full sound *logic* runs (sets load, 3D positions track); silent | 4 |
| Input | no touch yet; DirectInput returns "no device" | 4 |
| IME text entry | a real in-memory edit buffer, no keyboard | 4 |
| Web windows | report "not created", so callers skip them | never |

The loaders fail rather than returning empty successes on purpose: a mesh loader
that reported D3D_OK with nothing in it would turn a clear "not implemented yet"
into a mystery crash further downstream.

## What phase 2 is actually testing

1. `RANPARAM::LOAD` reads the shipped `config.ini`/`param.ini` correctly on arm64.
2. The RCC container index (`CUnzipper::LOADFILE_RCC`) works — same reader, same
   encrypted archives.
3. `CGameTextMan` loads the game text XML.
4. `DxGlobalStage` reaches its first stage and the frame loop runs.
5. The network layer connects and the login exchange parses — this is where the
   packet-layout work from phase 1 gets its real test.
