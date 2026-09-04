# RAN mobile — STATUS

**This is the living document. It is updated at the end of every working session.**
If anything here disagrees with another file, this file wins.

- **Last updated:** 2026-09-03
- **Approach:** compile the real PC client (`SOURCE/`) for mobile. Decided 2026-08-24.
- **Current phase:** 1 complete · 2 complete · **3 in progress — login works end to end; character-select scene and models remain**
- **Builds:** `cd MOBILE/native && ./build.sh` → 0 errors, produces `out/arm64-v8a/libran.so`
- **On device:** renders on the x86_64 test device (Adreno 750, GLES 3.1) at a steady 60 fps.
  APKs: `out/ran-phase3.apk` (current), `out/ran-phase2.apk` (headless, kept for comparison).

---

## Where we are

| # | Phase | Delivers | State |
|---|---|---|---|
| 1 | **Compile** | Whole client builds + links for arm64; packet layout matches the server | ✅ **done 2026-08-24** |
| 2 | **Boot headless** | `android_main` starts the client, reads the RCC packs, runs the frame loop. No rendering. | ✅ **done 2026-08-24** |
| 3 | **Render** | D3D9→GLES3 device shim; login screen, then world and characters; asm shaders → GLSL; fonts | 🟨 **in progress** — login scene, UI, text, lighting, fog, render targets and skinned characters all draw; effect passes remain |
| 4 | **Input + sound** | Touch → mouse/key messages, soft keyboard, virtual joystick overlay, OpenSL/AAudio | 🟨 **started** — touch reaches the UI; soft keyboard, joystick and audio remain |
| 5 | **Polish + iOS** | State-change batching, ETC2/ASTC textures, then iOS off the same code | ⬜ |

Phase 3 is the bulk of the remaining work. **The first pixels arrived on 2026-08-24**, and by
the end of that session the server-select page renders with **correct Thai and Latin text** —
the real engine, the real GUI XML, the real game-text tables, drawn through the GLES backend.
The 3D login scene now draws too — tower, trees, buildings, foliage — through the same GLES
backend. What is still missing inside phase 3 is the terrain ground surface, lighting and fog,
the character models (`.X` mesh + animation loaders), and the effect/shader passes.

---

## Phase 1 — what was actually achieved (2026-08-24)

1. **Compiles.** Lib_Engine 415 TUs · Lib_Client 239 · Lib_ClientUI 472 · Lib_Network 26
   (client subset) · Lib_Helper 4, plus zlib / lua 5.0.3 / minilzo / ogg / vorbis / shim.
   0 errors, 0 failed TUs.
2. **Links.** `libran.so`, 163 MB with `--whole-archive`. 476 undefined symbols, of which
   303 are ordinary libc/libc++ resolved at load; the other **173 are the phase 3/4
   work-list** → `native/PHASE1-LINK-REPORT.md`.
3. **Packet layout verified against the real server ABI** — 1,288 message structs compared
   against ground truth from the actual MSVC x86 toolchain, **1,286 identical**, and the
   comparison caught a genuine bug (see below) → `native/layout/LAYOUT-REPORT.md`.

### Three findings worth remembering

- **The GLES backend has exactly one seam.** `Direct3DCreate9` is never called; the engine
  takes its device solely from `DXUTGetD3DDevice()`. The shim owns that
  (`native/shim/d3d/dxut_compat.cpp`), so phase 3 plugs in at `DXUTSetD3D(pD3D, pDevice)`.
- **MSXML and DXUT are dead code.** The UI XML goes through the hand-written
  `CRanXMLParser`, and DXUT was used only for two device accessors — both were removed
  rather than ported.
- **`CTime` alignment was a real protocol bug.** MFC lays `CTime` out with 4-byte alignment
  on x86; the natural arm64 layout is 8. `SONEMAPWEATHER` embeds one, so
  `SNETPC_MAPWEATHER` came out **128 bytes too large** — silent desync, no crash. Fixed
  with `#pragma pack(push,4)`. **Any shim type that can appear inside a packet struct must
  reproduce the MSVC x86 layout**, and the layout gate must be re-run after such a change.

---

## How to work on this

```bash
cd MOBILE/native
./build.sh                 # build everything for arm64-v8a
./iterate.sh Lib_Engine 20 # build one target, print a histogram of distinct errors
./layout/run.sh            # arm64 record-layout dump
../tools/layout-probe/msvcsizes.cmd   # MSVC x86 ground truth (needs VS 2022, installed)
```

Toolchain facts: clang needs `-fms-extensions -fms-compatibility
-fms-compatibility-version=19.30 -fdelayed-template-parsing`; the code is C++14 (uses
`auto_ptr`, `bind2nd`); each library force-includes its own `stdafx.h` to emulate MSVC `/Yu`.
NDK r27c / SDK / CMake all ship with Unity 6000.5.8f1 — no separate install.

Edits to `SOURCE/` are either `#ifdef RAN_MOBILE`-guarded or const-correctness fixes that
also compile under MSVC, so the PC build stays intact.

---

## Documentation map

| Where | What | Status |
|---|---|---|
| `MOBILE/STATUS.md` | **this file** — current state, updated every session | live |
| `MOBILE/NATIVE-PORT-PLAN.md` | the port plan: audit numbers, architecture, phases, status log | live |
| `MOBILE/PATCHING.md` | how to publish an update: the two jobs, server layout, store maintenance, traps | live |
| `MOBILE/native/PHASE1-LINK-REPORT.md` | the 173-symbol work-list for phases 3–4 | live |
| `MOBILE/native/PHASE2-BOOT.md` | how to install/run the headless boot APK and what its log means | live |
| `MOBILE/native/layout/LAYOUT-REPORT.md` | packet-layout gate: method, findings, how to re-run | live |
| `MOBILE/reference/` | decoded PC file formats and asset pipeline — **still true**, engine-independent | reference |
| `MOBILE/archive-unity/` | superseded Unity-rewrite docs, kept only as history | dead |
| `MOBILE/client/`, `MOBILE/spike/` | the JS protocol spike that preceded the port | **deleted 2026-09-01** |
| `MOBILE/unity/`, `MOBILE/assets/`, `MOBILE/build/` | the Unity project and its extracted assets | **deleted 2026-09-01** |
| `SOURCE/SOURCE_*.md` | maps of the original C++ codebase | reference |

Root-level `*.md` (EP1/EP7 ports, GM_COMMANDS, plan.md …) are the **server/content**
workstream, unrelated to this port. Untouched.

---

## Update rule

At the end of any session that changes something: update **Last updated**, the phase table,
and add an entry to the log below. Then update the status log in `NATIVE-PORT-PLAN.md` if
the change was structural.

## Open work

Kept at the top because it is the list that matters. Ordered by what blocks
what, not by when it was found.

### iOS

1. **First compile.** Every file under `native/platform/ios/`, plus
   `shim/gl/gl_context_ios.mm` and `shim/d3d/image_decode_ios.mm`, is written
   against documented APIs and has never seen a compiler. No Mac hardware is
   needed for this — a GitHub Actions `macos-14` runner has Xcode and the iOS
   SDK, and `build-ios.sh` is what it would run. Both repos are on GitHub
   already. What a Mac (or a signing identity) *is* needed for is putting the
   build on a device.
2. **Audio — DONE 2026-09-04 on Android; iOS needs only a sink.** The game had
   no sound at all, on either platform, and the missing backend was only the
   last of three reasons:

   * `CWnd::m_hWnd` was NULL in the shim, and `DxSoundMan::OneTimeSceneInit`
     returns before doing anything when the handle is null — so the sound layer
     was never entered.
   * `timeSetEvent` was a stub returning 0. `BgmSound` drives its streaming
     thread with a periodic multimedia timer that sets an event, so the thread
     waited forever and not one block of music was ever decoded.
   * The device's `option.ini` had every volume at `DSBVOLUME_MIN`. The engine
     defaults are 0, which is `DSBVOLUME_MAX`.

   Built: `audio_mix.cpp` (the portable mixer — WAV clips at 8/16/24/32-bit,
   voices in DirectSound's own volume and pan units, ring voices for streaming,
   an int accumulator so overlapping sounds do not clip against each other),
   `audio_opensl.cpp` (the Android sink, two 20 ms buffers), `dsound_mobile.cpp`
   (IDirectSound/IDirectSoundBuffer over the mixer — the music path needs a real
   ring with a real play cursor), and a real `dsutil_mobile.cpp`.

   Measured in the world on LDPlayer: 5 voices, peak 25972–32768 of 32767, the
   ring taking 183 MB of non-zero decoded PCM. Silence on every build before it.

   **iOS sink written** (`audio_audioqueue.mm`, uncompiled like the rest of the
   iOS work) with the same background-pause behaviour; nothing above the sink
   is platform-specific. Two known gaps on both platforms: no headroom
   (the mix reaches full scale with music at default volume, as the PC does),
   and nothing mutes audio when the app loses focus (`RanAudio_SetMuted` exists
   and is not wired).

3. **`minIos` in the manifest.** `make-manifest.js --min-ios <n>`. The iOS
   patcher refuses a manifest without it, deliberately, so this has to be
   published before an iOS client may talk to the live server.
4. **No S3TC on Apple GPUs.** The shipped textures are DXT1/3/5, and no Apple
   GPU has ever exposed `GL_EXT_texture_compression_s3tc`. `haveS3TC()` in
   `gl_render.cpp` already detects this by extension string and falls back to
   decoding every block on the CPU, so iOS will *work* the day it builds - but
   at 4x the texture memory and a CPU cost per load. The real answer is
   transcoding the store to ASTC, which every iOS device since the A8
   supports; that is a patch-store change, not a client one. Expect this to be
   the first thing the simulator shows.

5. **Signing.** A development profile, TestFlight, or sideloading. Read the
   copyright section of `IOS-PORT-PLAN.md` first: that decision comes before the
   work, not after.

### Android

6. **Riding: the specular repaint is fixed, the model is not.** RESOLVED
   2026-09-04 for the repaint. It was **not** the navmesh slope test I first
   suspected - that never entered the top ten. It was `EMECF_SPECULAR2`, the
   multipass cube-map specular, redrawing every vehicle piece whole:

   ```
   before  12.4 fps  80.8 ms   veh:parts 54.2ms  part:chareff 27.5ms  alpha 106,440 verts
   after   17.2 fps  58.2 ms   veh:parts 30.6ms  part:chareff  3.5ms  alpha  29,577 verts
   ```

   The repaint now has a 4000-triangle budget (`specbudget` overrides it, 0
   restores PC behaviour). **What remains is the model**: the BMW S1000RR body
   is 26,580 triangles for one piece, and drawing it once is still ~24 ms here.
   That is a LOD question, and `USE_SKINMESH_LOD` is still a stub - `g_dwLOD`
   is set and never read. Fixing it would pay back on every character, not just
   vehicles.

7. **Tab S9 verification.** Everything since V016 has been checked on LDPlayer
   only; the tablet has been off adb. The keyboard inset
   (`RanPlat_ImeInsetPerMille`) in particular cannot be verified on the
   emulator, which has no on-screen keyboard and reports 0.

8. **Intermittent SIGSEGV** in `RanTexture::LockRect` by way of
   `RanD3DXFont::glyphFor`. Unattributed, no tombstone kept. The font atlas is
   locked and written from whichever thread is drawing, and the loading screen
   draws from its own — a race there fits the shape, but nothing is measured
   yet. Keep the next tombstone.

9. **Publish the pending patch.** The store at `native/out/launcher_mobile` is
   version 412 (APK V025, versionCode 42); the working build is well past it.

---

## Log


- **2026-09-03 (later)** — **The platform seams, and the iOS files they made possible.**

  Everything Android-specific that shared code was reaching for now goes through a seam,
  and an iOS target exists that uses those seams. **No iOS code has been compiled** —
  there is no Mac here — so every file under `platform/ios/` and
  `shim/d3d/image_decode_ios.mm` is a prediction until the first `build-ios.sh` run.

  What *is* verified is that Android is untouched by it: after every change the Android
  build was reconfigured from scratch and rebuilt for **both ABIs**
  (`errors: 0  failed: 0`), the APK repacked and installed on LDPlayer, and the game
  logged in and played at 57 fps.

  | Seam | Was | Now |
  |---|---|---|
  | Diagnostic flag files | 60 `/sdcard/ran/...` literals | `RanPlat_DiagPath` / `DiagExists` / `DiagOpen` |
  | Logging | 70 `__android_log_print` calls in 24 files | `RanPlat_Log` |
  | Fonts | `/system/fonts` hardcoded | `RanPlat_SetFontDir` |
  | Image decode | `AImageDecoder` called directly | `RanImage_DecodePlatform`, one per platform |
  | Keyboard inset | **`RanAndroid_ImeInsetPerMille`**, called from `SOURCE` | **`RanPlat_ImeInsetPerMille`** |

  The last one mattered: an Android name had leaked into shared client code
  (`Lib_Client/DxGameStage.cpp`, inside `RAN_MOBILE`). Renamed, rebuilt and re-verified
  on device — tapping the chat line still raises the keyboard (`mInputShown=true`). The
  inset **value** is still unverified: LDPlayer has no on-screen keyboard, so it reports
  0. That needs the Tab S9.

  New iOS files: `ran_ios_main.mm` (UIApplicationMain, EAGL ES3 context, CADisplayLink
  frame loop mirroring `android_main.cpp`, touch slots into `RanTouch_*`, UIKeyInput into
  `RanIME_InsertUtf8` / `RanIME_Backspace`, keyboard inset from
  `UIKeyboardWillChangeFrame`), `ran_ios_plat.mm`, `Info.plist.in`,
  `image_decode_ios.mm` (ImageIO), `build-ios.sh`, and four redistributable fonts in the
  bundle (NotoSansThai OFL, Roboto Apache 2.0) named exactly as the Android system files,
  because `RanFont_Resolve` picks by filename. CMake grew one `if(RAN_IOS)` branch per
  Android-specific line, and `RAN_IOS` is set only by `CMAKE_SYSTEM_NAME=iOS`. Full
  detail and what remains: `IOS-PORT-PLAN.md`.

  **Damage report, honestly:** the tree-wide `sed -i` used for the rename also rewrote
  the build artefacts under `native/out/` — including one published patch blob (the V025
  APK, patch v412). sed is not binary-safe and the round trip is lossy: 4,169 bytes were
  lost to line-ending translation. The build artefacts were regenerated; the blob was
  re-downloaded from the patch host and its SHA-256 verified against its own filename.
  Nothing was lost and nothing on the server was touched. The lesson: never run
  `sed -i` across a tree that holds build output.

  While there, `shim/platform/ran_plat.cpp` turned out to hold a **literal NUL byte**
  inside what should have been a `'\0'` character literal — legal C++, compiled fine, but
  it made the file binary to every text tool. Fixed to the two-character escape.


- **2026-09-02** — **One flag moved every label and armed a crash: the outline fix.**

  Reported as mob names sitting left of their mob, on a build where they had been
  fine. It was a regression of mine, and the cause was a single change - not any of
  the things I altered afterwards while chasing it.

  Making `GetVersionEx` report the truth (Windows 7, rather than the unknown
  version `GetWinVer` derived from an unfilled `wProductType`) was what switched
  the black text outline on. It also flips `CD3DFontX::m_bWindows98` from TRUE to
  FALSE, and that flag chooses between **two different implementations of text
  measurement**:

      m_bWindows98 : m_pd3dxFont->DrawTextW( NULL, ..., DT_CALCRECT )
      else         : GetTextExtentPoint32W( m_hd3dxDC, ... )

  This port has always laid out against the first. The switch was harmless at the
  time only because `GetTextExtentPoint32W` was a stub: it left `SIZE` at zero and
  the `if ( Size.cx == 0 )` fallback quietly put the D3DX path back. **Implementing
  that stub later removed the fallback** and put the GDI numbers into use, and they
  disagree with what is actually drawn - by more the longer the string:

      gdi=111  d3dx=102        gdi=128  d3dx=101        gdi=150  d3dx=116

  Layout centres a box on the measured width, so 34 pixels of over-measure puts the
  visible text 17 pixels left of the mob. Mobile now measures with the call that
  draws.

  The same flag also switched on `CTextUtil`. Its `FrameMove` runs from
  `CUIMan::Render` whatever the font path does, and it took the process down:

      signal 11 (SIGSEGV) ... RanTexture::LockRect
      CTextTexture::FrameMove -> CTextUtil::FrameMove -> CUIMan::Render

  `m_bUsage` has to stay TRUE - it is what gates the outline - so the cache is idle
  on mobile instead. Using it for real means implementing the GDI it builds its
  textures with (`ExtTextOutW`, `GetTextExtentPoint32W`, `FillRect`,
  `CreateSolidBrush`, now written but unused), which is separate work and would
  also buy back the 1.6 ms a frame the outline costs.

  Also found and fixed while reading this path: the string table returns mob names
  with a trailing blank (`Little Vulgarian` is 17 bytes, the last `0x20`; player
  names have none), and `CNameDisplay` never resized its name box with the control,
  so a centred draw inside a 20-pixel box holding 126 pixels of text started at the
  box origin.

  **What this cost:** a long stretch of the session spent changing things and
  re-testing instead of finding the one cause. The lesson is recorded in memory:
  when something worked before, the first move is to list my own changes that touch
  the affected subsystem - shared machinery like text metrics moves everything that
  depends on it.


- **2026-09-01 (evening)** — **Four interface faults, each traced to the PC mechanism first.**

  **NPCs showed a health bar.** `MobileTargetTick` called `SetTargetInfo` for whatever
  was latched, which is the *mob* panel - it shows health and floats a bar over the
  target's head. The client never does that for an NPC, and the reason is a gate
  rather than a branch: `SetTargetInfo` is only reached when

      (emCrow==CROW_PC && emACTAR==EMACTAR_PC_PVP) || emACTAR==EMACTAR_MOB
      || (bBRIGHTEVENT && emACTAR!=EMACTAR_NPC)
      || (emCrow==CROW_SUMMON && emACTAR==EMACTAR_SUMMON_ATTACK)

  which excludes `EMACTAR_NPC` outright; NPCs reach `SetTargetInfoNpc`, handed a name
  and nothing else. The tick makes the same three-way split now.

  It also had **two owners**. The PC dispatch runs off `m_sACTAR`, the pick under the
  pointer: a mouse re-picks every frame so there is only ever one panel, but a
  finger leaves the pointer where it last touched, so that code showed a panel for
  whatever was last passed over while the tick showed one for the latched target -
  two panels stacked. Those calls are `#ifndef RAN_MOBILE` now.

  **No black outline on any text.** The outline needs `CTextUtil::m_bUsage &&
  m_iOutLine`, and both were off because the client believed it was on Windows 98.
  `GetWinVer` asks with a ZeroMemory'd `OSVERSIONINFOEX`; its 6.1 arm switches on
  `wProductType` with no else, the shim never filled that field, so the zero matched
  neither arm and `nVersion` kept `WUNKNOWN` (0) - below `WNTFIRST` (101), the test
  everything downstream reads as "9x". The shim reports `VER_NT_WORKSTATION` now.

  Fixing that alone **deleted every glyph in the interface**: `m_bUsage` also routes
  text through `CTextUtil`'s texture cache, which builds textures with `FillRect` and
  `ExtTextOutW` - stubs in the port that report success and draw nothing. Mobile
  takes the immediate font path unconditionally now: outline kept, cache skipped.
  Measured cost, `interface` **0.7 ms -> 2.3 ms** a frame, being eight extra passes
  per string. Implementing those GDI calls would buy it back.

  **Presses fell through open windows** to the pad underneath. This hit test has now
  been wrong three ways, and the third is the one to remember:

  | test | fails on |
  |---|---|
  | `IsVisible()` | GENDER_CHANGE_WINDOW - visible flag set, nothing drawn, killed the pad's toggles |
  | `IsNoRender()` on the top control | a window is a CUIGroup that paints nothing itself; whole windows became click-through |
  | tree walk without pruning | a closed window's children keep their own visible flags; blocked the world and the pad |

  It is now a virtual that recurses and stops dead at a hidden group. Verified with
  the settings window dragged over the camera-lock button, using the HUD cache as the
  detector - a press changes a button's `down` state, forcing a rebuild:

      PK button, nothing over it     82128 -> 82968 verts, 7 -> 9 draws   toggles
      world drag                     83.5% of sampled world pixels changed
      settings window over the lock  82128 verts, 7 draws                 blocked

  **Camera lock followed the walk direction.** It used `GetDirectionVector()`, so the
  view swung round on every course change and the target slid off screen. It now
  aims at `m_sMobileTarget`, reading the position fresh from the copy list rather
  than the `STARGETID` (whose position is the one it had when selected). With nothing
  selected the camera is left alone.

### A dying target is dropped in 52 ms, not 521 (measured 2026-09-02)

The complaint was that a killed mob keeps its bar and stays hittable. Two probe
runs, one line per frame, same mob type and same server.

Dropping on `GLAT_DIE` alone:

    09:37:11.513  hp=1/180 die=0      <- health stops here
    ...  about twenty frames ...
    09:37:12.034  hp=1/180 die=1      <- 521 ms later

Adding `GLAT_FALLING`:

    09:53:07.245  hp=1/180 die=0 fall=0
    09:53:07.271  hp=1/180 die=0 fall=0
    09:53:07.297  hp=1/180 die=0 fall=1   <- 52 ms

`FALLING` arrives while `DIE` is still 0, and that is the whole difference. The
client's own continuation logic already treated either action as target-gone;
`MobileTargetIsLive` now tests the same pair.

**The client's health for a mob never reaches zero** - it stops at 1 and the
server sends no final update, only the death action. So the zero-health test
added earlier in the session is dead code for mobs. It is kept only because a
player's bar does reach zero.

**Two things in this area were my own regressions, both now removed:** a
fallback in the nearest-mob search that re-picked with `FindClosedCrow` (no
state test) exactly when every candidate was dead, handing the corpse straight
back; and the belief that auto-select was broken, which came from pressing the
wrong pad button.

### Dead targets, measured (2026-09-02)

Killing something now drops it everywhere at once. Instrumented across a kill:

    find: 4 candidates, 0 dead, best=27.4 chose=one
    attack: auto-selected a target
    live: target at 0/180 - dropping
    find: 3 candidates, 0 dead, best=37.1 chose=one

* the zero-health drop fires on the killing blow, before the death action
  arrives - that was the window in which the corpse was still attackable
* the corpse is not offered again; the next search picks a different live mob
* auto-select works. The earlier note claiming the corpse filter had broken it
  was **wrong**

**The pad button map was read backwards, and it invalidated several results.**
The right column is laid out upward from the attack button, so top to bottom it
is CAMLOCK (eye), PK (blades), AUTO (crosshair) - not the reverse. Every
"camlock" test before this was toggling AUTO instead. That also retires the
"button lit while its flag reads 0" item below: there is no divergence, it was
the wrong button being read.

**The camera lock maths is right.** `DxViewPort::CameraRotation` (bFrom=FALSE)
was simulated offline against the easing: from 90 degrees off, the shipped sign
converges (-1.5708 to -0.146 over twelve frames) and the opposite sign diverges
to 180. The lock does nothing without a live target, which is by design and is
what the probe kept showing. Still unconfirmed on a device - the emulator would
not boot again after this session.

### Settings never persisted — same root cause, no new fix (2026-09-02)

Reported as: uncheck **Classic Name** in ตัวเลือก, press ตกลง, and it is back
next session.

Not a separate bug. `RANPARAM::SAVE_GAMEOPTION` was always working — it goes
through `CFile::Open`, which resolves paths correctly. The **load** was the
broken half: `LOAD_GAMEOPTION` gates on
`PathFileExists( <root>\option.ini )` before it reads a single key, and that
check was answering "missing" (see the entry above). So every option in
`[GAME OPTION]`, `[SCREEN OPTION]`, `[SOUND OPTION]` and `[GRAPHIC OPTION]` was
written to disk correctly and then ignored at every start, leaving the whole
settings window on compiled-in defaults.

Verified end to end on device, decoding the file each time
(`node tools/rcc-extract/gamecrypt.js` decodes option.ini):

| step | file | UI after restart |
|---|---|---|
| start | `bClassicNameDisplay = 1` | checked |
| uncheck + ตกลง | `= 0` | unchecked |
| check + ตกลง, restart | `= 1` | **checked** |

The last row is the one that proves it: the flag's compiled-in default is
`FALSE`, so it can only come back checked by being read out of `option.ini`.
Left at `0` (off), which is what was asked for.

### PathFileExists bypassed the path resolver, and it cost the whole GAME_FEATURE block (2026-09-02)

Reported as: long-press another player, choose **ดูข้อมูลส่วนตัว** (view personal
info) — nothing happens, no window, no error message.

**Cause: `PathFileExistsA` in `shim/win/windows.h` called plain `fopen`.** The
`#define fopen ran_fopen` sits further down that same header, so the call there
resolved to raw libc `fopen` and got the client's Windows path verbatim —
backslash separators, wrong case. It always answered "missing" for a file that
is present.

One client caller gates on it, and the damage is out of all proportion:

    RANPARAM::LOAD_GAMEOPTION()          RANPARAM_OPTION.cpp:22
        if ( !PathFileExists( <root>\option.ini ) ) return FALSE;

`RANPARAM::LOAD` runs `LOAD_PARAM` -> `LOAD_GAMEOPTION` -> `LOAD_FEATURE` and
aborts the chain on the first FALSE. So **`LOAD_FEATURE` never ran and every
`[GAME_FEATURE]` flag in Config.ini kept its compiled-in default**, most of them
`FALSE`. `bFeatureViewCharInfo` is one of them, and `RequestCharacterInfo`
returns silently when it is off — hence a button that does nothing and says
nothing. The CP bar in the HUD was another casualty; there will be more.

Measured, at the click:

    before:  RequestCharacterInfo id=27 feature=0 timer=5.02/5.00     (no packet)
    after:   RequestCharacterInfo id=27 feature=1 timer=5.01/5.00
             sent SNETPC_REQ_CHARINFO
             REQ_CHARINFO_FB emFB=2                                   (WEARINFO)

and at boot, after the fix:

    LOAD_FEATURE: opened <root>\Config.ini
    bFeatureViewCharInfo=1  bFeatureStudentRecord=1  bFeatureProduct=0

Fix: `PathFileExistsA` calls `ran_fopen` directly, declared immediately above it
so the macro's position in the header stops mattering. Verified on a clean
probe-free build: the character info window opens with test02's equipment, 3D
model, stats, school and guild.

**Ruled out along the way**, each by measurement rather than argument: the click
delivery (the whisper button in the same menu works and fills `@test02` into the
chat input), the request timer (`5.02/5.00`, past the 5s gate), the UI keyword
(`RAN_ANOTHER_CHAR_WINDOW 506,0 480x528` is present in the uicfg), the message
routing (`NET_MSG_GCTRL_REQ_CHARINFO_FB` is in the `DxGlobalStage` switch), and
the file itself (`Config.ini` decodes to `bFeatureViewCharInfo = 1`, and opens on
device with no `CIniLoader::open` error in the client's own ErrorLog).

**Worth re-testing generally.** Anything that reads `[GAME_FEATURE]` has been
running on defaults for the whole port, so features may appear that were never
seen working here before.

### The interface turning to garbage: root cause (2026-09-02)

Reproduced on LDPlayer and fixed. Trigger: **use a `กล่อง POWER UP` from the
inventory**. The buff draws its "POWER UP" banner, and from that moment every
string in the game renders as a solid white block — glyph quads in the right
places, at the right widths, with Thai marks stacked correctly, but filled flat.
Icons, world and window frames stay perfect.

**Cause: `g_gl.program` and `g_variantKey` are two halves of one piece of state,
and only one of them was being invalidated.**

`RanGLR_InvalidateStateCache()` calls `g_gl.reset()`, which zeroes the cached
program. It did **not** reset `g_variantKey`. `useVariant()` opened with
`if (key == g_variantKey) return;` — so after the touch HUD drew with its own GL
program and invalidated the cache, the next engine draw asking for that same
variant returned early, never issued `glUseProgram`, and rendered under the HUD's
program. Everything stayed wrong until some other variant happened to be asked
for. The banner is what pins the UI pass to a single variant, which is why the
POWER UP box makes it permanent rather than a one-frame flicker.

That is also the long-unexplained sticky `glErr=0x0502`: drawing with the wrong
or no program is exactly `GL_INVALID_OPERATION`.

Measured across the fault, at the text draw itself:

    before the box:  FONTDRAW ... prog=33
    after the box:   FONTDRAW ... prog=0          <- white text
    after the fix:   FONTDRAW cached prog=33  actually bound=33  variantKey=00000401

Fix, both halves: `useVariant` re-asserts `useProgram` even when the key has not
changed (`useProgram` is itself cached, so it is free when nothing moved), and
`RanGLR_InvalidateStateCache` clears `g_variantKey`. Verified on a clean
probe-free build: box used, all text correct, `glErr=0x0000`.

**What this was NOT.** Ruled out by measurement, each ruling out a theory that
looked right: the glyph atlas (read back off the GPU through an FBO mid-fault —
`alive=1 levels=2 minf=GL_LINEAR`, texels carrying real coverage `00 32 2A 23 40`,
not opaque white); the texture upload paths; `glGenerateMipmap`; atlas-full; the
text outline; the fixed-function stage-0 ops (`colorop=4 arg1=2 arg2=0` right
through the fault); render-target volume (identical, ~7000 draws/s, in working
and broken sessions).

### Two real bugs found on the way, both fixed, neither of them this one

**The unit-0 bind cache could disagree with GL.** `RanGLR_Draw` skips
`glBindTexture` when `g_gl.texture2D` already names the texture it wants, but
`RanGLR_UploadTextureLevel`, `RanGLR_UpdateTextureRect`, `RanGLR_FinishTexture`
and the render-target path all bound unit 0 without updating that field, and
`RanGLR_DeleteTexture` deleted a bound texture (GL unbinds it) without clearing
it. A draw could then sample the wrong texture *and* have
`RanGLR_ApplySampler` write its `glTexParameteri` onto that wrong texture, and
memoise the result against the right one. One `bindTex2D()` helper is now the
only way unit 0 is bound, and `RanGLR_DeleteTexture` forgets the name plus its
`g_texLevels` / `g_texDims` entries, which were otherwise inherited by whatever
id GL recycled next.

**`ATLAS_W`/`ATLAS_H` were file-scope and grew under fonts that had already
allocated.** A font holding a 1024 atlas would then pack and compute UVs against
2048, writing past the end of its locked bits. Per-font `m_atlasW`/`m_atlasH`
now; the global only sizes the next atlas.

### Test-rig notes from this session

* `login-ld.sh` was stale — it targeted `android.app.NativeActivity`, which the
  launcher split replaced, and typed credentials by tapping a client-drawn keypad
  that no longer exists. **`ld-login.sh` is the working one**: it starts
  `com.ran.launcher.RanActivity` and types through the IME. The dead component
  name is fixed in all the login scripts.
* Skipping the server-row/channel/connect taps and typing straight into the login
  box hangs forever with **no socket open at all** (`/proc/net/tcp` for the app's
  uid is empty). Check that before blaming the server.
* `adb shell input keyevent 37` (KEYCODE_I) opens the inventory — letters map to
  DirectInput scan codes in `scanCodeFor`. Far more reliable than hunting the
  icon row, whose contents shift as windows open.
* `กล่องของขวัญ POWER UP` is the *pack*; `กล่อง POWER UP` inside it is the item
  that triggers the bug. Using the pack proves nothing.


### Shipping: code now travels by patch too (2026-09-02)

Detail in `PATCHING.md`; this is the shape of it.

Native code cannot ride the data payload — since Android 10 an app targeting
API 29+ may not `dlopen` a library out of its own writable storage, and this one
targets 34. So there is no equivalent of dropping a new `MiniA.exe` in. Instead
the APK goes into the store as a blob, the manifest names it, and the launcher
installs it after the player confirms. The hash comes from the signed manifest
and the bytes stream straight into a `PackageInstaller` session, so they are
never a file anything could swap between the check and the install. Proven by
flipping one byte on the server: `checksum failed for the apk`, session
abandoned, player left on the working build.

`MAKE-PATCH.bat` is now the whole job. It compiles both ABIs, and only if
something the APK carries actually changed does it bump the version and
repackage — so a data-only patch never offers anyone a 320 MB reinstall of an
identical binary. Then it sweeps `out/` and the store down to what ships and
writes the manifest. Publishing an APK nobody would receive is a hard error, not
a warning, in both of the ways that happen silently: `versionCode` not bumped,
and code rebuilt without repackaging.

The APK is `RanMobile.apk` now - one name every release, so a link to it never
has to be reissued. `versionCode` stays a private counter that
only goes up, because Android compares it and refuses to install over a higher
one; `versionName` is the release label, and the file is named after it.

`option.ini` is seeded rather than shipped — installed when a player has none,
never overwritten after. It is the one file in the list the client writes, so
shipping it normally reset everyone's settings on every patch, silently undoing
the settings fix above.

**One-time:** the APK players hold has no updater in it, so it ignores the
manifest's `apk` block. That group needs `RanMobile.apk` by hand once.
Everything after is a patch.

**Both keys are single points of failure and are gitignored.**
`native/android/debug.keystore` is the app's identity — a different key means no
player can ever upgrade, only uninstall and re-download 1.7 GB.
`tools/patch/keys/manifest-signing-key.pem` signs the manifest — without it no
patch can ship at all. Back both up off the build machine.

### The `.ini` files, and a correction (2026-09-02)

**Correction.** An earlier version of this entry said the packs contain no
`.ini` at all, and that `Rank.ini` therefore cannot load on PC either. That was
wrong, and wrong because of a bad measurement: the check called a method the
rcc reader does not have, got array indices back instead of names, and counted
zero `.ini` in a pack that holds seventeen. `GLogic.rcc` contains `Rank.ini`,
`attendance.ini`, `busstation.ini`, `comment.ini`, `emoticon.ini`,
`pandorabox.ini`, `colortable.ini` and ten more. Whatever `LOADRANK` is
complaining about, "the file is not in the pack" is not it.

**What ships**, six files:

| | |
|---|---|
| `config.ini` | the `[GAME_FEATURE]` flags |
| `param.ini` | the game server address |
| `comment.ini` | read from the pack, but the PC client carries a root copy too, so it ships for parity |
| `option.ini` | seeded - installed when absent, never overwritten |
| `data/skin/desktop.ini`, `textures/item/desktop.ini` | Windows Explorer folder settings that the PC install happens to contain. A kilobyte between them, and present in `Ran/`, so they ship rather than open a hole in `--verify` |

**What does not**, and should not: the other 63 `.ini` in `CLIENT/`. They are
server-side or already inside `GLogic.rcc`, which is where the client reads them
from - `bGLOGIC_ZIPFILE` is always on and `gltexfile::open` has no loose
fallback. `Hackshield/*.ini` is skipped with the rest of that directory.

**One trap this exposed.** The duplicate check matches on the bare filename,
and an archive stands for the directory it lives in - so the root `comment.ini`
was dropped because `GLogic.rcc`, which is `data/glogic/`, holds an entry of
that name. Two different destinations agreeing on a filename. Root files are now
never deduplicated: there are four, they are a kilobyte each, and getting one
wrong costs more than shipping all of them.

### Security review of the delivery path (2026-09-02)

Full read of the fetch, verify, write and install paths. Two things fixed, the
rest recorded as checked so the next review starts from evidence.

**Fixed: all-files access no longer gates startup.** `onCreate` refused to run
without `MANAGE_EXTERNAL_STORAGE` - permission to read and write every file on
the device - even though the data root is this app's own external files
directory, which needs no permission at all. The only remaining use for it is
spotting a pre-private-root install under `/sdcard/ran` and moving it, saving
that player a 1.7 GB re-download; nobody installing fresh has any use for it.

It is now asked for once, only from someone who might benefit (no data in the
private root, permission not already held), and the run continues whatever they
answer. Measured with the permission set to `deny`: no prompt, boots, logs in,
reaches the world, `glErr=0x0000`.

**Fixed: `RanActivity` was `exported="true"`.** Any app on the device could
start the game directly, skipping the launcher and with it the update check and
the signature-verified manifest. Now `exported="false"`; `am start` on it
returns `Permission Denial: ... not exported from uid 10074`. The login scripts
go through `RanLauncher` instead, which is the path a player takes anyway.

**Checked and sound**, each read rather than recalled:

* the manifest is verified before it is parsed, fail-closed on missing,
  malformed or wrong signature (P-256 ECDSA, key compiled into the APK)
* `safeDest` blocks traversal twice - syntactic (absolute, drive, backslash,
  `..`) and canonical-path containment
* anti-rollback on both halves: the data version cannot go backwards, and only a
  strictly newer `versionCode` is offered
* the APK never exists as a file - streamed into a `PackageInstaller` session,
  hashed in flight, abandoned on mismatch. Proven by flipping one byte on the
  server
* blobs: size ceiling enforced mid-stream, hash checked before the atomic
  rename, temp file in the private root
* APK signed with v2 and v3 schemes, one signer, `CN=RAN Debug, O=RAN, C=TH` -
  a unique key, not the well-known public Android debug key
* `allowBackup="false"`, `debuggable` absent, cleartext scoped per host
* the native loader has no `/sdcard/ran` fallback for game data, so the private
  root really is the only place the C++ parsers read from

**Residual, accepted:** both signing keys are protected by file secrecy alone -
the keystore password is `android` in `build-apk.sh` and the manifest key is an
unencrypted PEM. Anyone holding `debug.keystore` can sign an APK that installs
over the real app through any channel, which is the argument for backing them up
*securely* rather than merely backing them up. Redirects are followed, which
cannot inject content since everything is hash- or signature-checked. A local
actor with root can write a high `.patchver` to stall updates.

### The patch payload was missing 3 GB, and no fresh install could have worked (2026-09-02)

Found while looking for the loading art. The payload carried `data/` and nothing
else. Against the shipped PC client:

| in `Ran/` | size | files | was in the manifest |
|---|---|---|---|
| `data/` | 2.4 G | 6,408 | yes |
| `textures/` | 2.8 G | 16,206 | **no** |
| `sounds/` | 256 M | 864 | **no** |
| `cVer.bin` | — | — | **no** |

Every item icon, all interface art, the loading screen, every sound, and the
version file the login compares. Nobody had hit it because every device so far -
the Tab S9 and LDPlayer included - was seeded by `push-data.sh`, a manual full
push. The patcher had never once provisioned a device on its own, which is
exactly what handing the APK to a new player would have done.

`--verify` missed it because it only walked `Ran/data`. It now walks the whole
client root, with a skip list for the parts of a PC install that have no
business on a phone (`.exe`, `.dll`, `GMTool`, `Hackshield`, `Logs`,
`cFileList.bin`, `Launcher.URS`). It passes clean.

`cache/` is still not shipped, and should not be: it is the font cache, created
and written by the client at runtime.

**Loose files already inside a pack are dropped.** Matched on the bare name -
which is how the reader resolves an entry - and then confirmed by comparing the
bytes, because a name collision between two different files would otherwise
silently drop one. 74 of 80 matches were byte-identical (14.2 MB, mostly `.x`
models also in SkinObject.rcc); the other 6 are kept, same name and different
content.

Payload is 1.68 GB -> 4.72 GB, 8,263 -> 25,259 files.

**Proven from scratch**, which is the only test that counts here: device data
root deleted entirely, then the launcher pointed at a local store. It fetched
all 4,722 MB, and the client booted on it - server select with full art, correct
Thai, the RAN mark. Before this it would have had no textures at all.

### The patch screen is the game's loading screen now (2026-09-02)

The player used to meet a bare dark panel, then a moment later the client's
loading art: two screens for one wait. The launcher now draws the same lobby art
(`loading_002.dds`), the RAN mark from the login page (`LOGIN_MARK`, taken from
the ui config rather than eyeballed), and the progress along the bottom. The
in-game splash drops the HINT badge and the corner spinner to match.

The art is a drawable in the APK, and has to be: on a first install this screen
is painted *while* the data root that holds it is still downloading.
`extract-launcher-art.js` regenerates both PNGs from the client textures.

**A regression of my own, caught on device.** Asking for storage permission on a
fresh install opened the Settings screen, which put the launcher in the
background exactly as the download started - and the permission change then
killed the process: `Killing com.ran.native (adj 900): MANAGE_EXTERNAL_STORAGE
changed`. The one case the prompt was meant to help, it broke. Nothing asks for
storage now: an updating player keeps the grant from the old build and gets the
migration, a fresh install has nothing to migrate. The launcher also holds
`FLAG_KEEP_SCREEN_ON`, since a 4.7 GB download dies with the screen.

### The payload mirrors the shipped client now, not the dev tree (2026-09-02)

`CLIENT/` carries client *and* server data, so "it is in CLIENT" was never a
reason to ship a file. Measured against `Ran/`, the real shipped client, the
payload held **2,061 files it does not have** - and four of its directories were
the reason:

| the shipped client has | our dev tree has |
|---|---|
| `quest/Quest.rcc` | 838 loose `.qst` |
| `npctalk/NpcTalk.rcc` | 665 loose `.ntk` |
| `level/Level.rcc` | 266 loose `.lev` |
| `effect/char/EffectChar.rcc` | 200 loose `.effskin_a` |

Those four archives were already being shipped - the directory walk picks them
up - but so was every loose source beside them: quest script, NPC dialogue and
level data handed to players as readable files, which the PC client has never
done.

The duplicate check only indexed the packs named in `SHIP`, so these four, which
arrive through the walk, were invisible to it. It now indexes **every `.rcc` in
the payload**. 2,035 of 2,041 name matches were confirmed byte-identical and
dropped; the 6 that differ are kept, which is the whole point of comparing bytes
rather than trusting names.

`.bak-`, Explorer's Thai "สำเนา" copies and a stray `test.effskin` are excluded
too - three dev leftovers that were being published.

23,294 files, 4,679 MB. What remains that `Ran/` lacks is 96 `.enm` and one
`.mxf`: costume entries newer than the reference install, so genuinely content
rather than leftovers.

`MOBILE/native/out/PAYLOAD.txt` lists every one of them — size, path, and an `S`
on the seeded entry — and is rewritten on every publish, so "what does a player
actually get" never needs a JSON reader on a 3.7 MB manifest. `PATCHING.md` has
the breakdown by weight.

**Checked functionally, not just by hash.** The loose sources were deleted off
the device, leaving only the archives, and the client was taken into the world:
`glErr=0x0000`, HUD, mobs, NPCs, Thai chat all correct. `QUEST load fail : 1307`
appears in that run's log - and in the two runs *before* the deletion, 12 times
in each. Pre-existing, and not caused by this.

### Still open from this session

* **The camera lock is not verified on a device.** Maths checked offline (see
  above); it no-ops without a live target by design. Needs one run on hardware:
  target a mob, lock (TOP icon), circle it - the mob should stay in front.
* Text costs 1.6 ms a frame more than it did. `ExtTextOutW`, `GetTextExtentPoint32W`,
  `FillRect` and `CreateSolidBrush` are the four stubs standing between the port and
  the text-texture cache that would remove it.


- **2026-09-01 (cleanup)** — **The Unity path is gone, and the port is the only client.**

  17.1 GB removed. `MOBILE/` was 23.6 GB and is now 6.5 GB, effectively all of it
  `native/`.

  | Removed | Size |
  |---|---|
  | `unity/` — the Unity project and its 7.6 GB of imported art | 8.9 GB |
  | `assets/` — rcc-extract staging, 16,072 textures | 3.7 GB |
  | 1,089 test screenshots in `native/out/` | 3.7 GB |
  | `ran-phase2.apk` and its idsig, superseded | 339 MB |
  | `build/mapobj` — extracted map objects | 29 MB |
  | `client/`, `spike/` — the JS protocol spike that preceded the port | 166 KB |
  | stray logs, a pulled `simpleperf`, `orphan-ui-atlases.txt` | — |

  None of it was in git: `unity/`, `assets/` and `build/` are all gitignored, so this
  is not recoverable, and it was chosen deliberately. Nothing had been touched
  since the native-port decision on 2026-08-24.

  **43 screenshots were kept** — the ones cited by name in this file and the other
  docs as evidence for past findings. Deleting those would have left the write-ups
  pointing at nothing; the list was built by scanning all 20 markdown files rather
  than guessing.

  Two things that looked like findings and were not, both checked before acting:
  every `.cpp` under `shim/` appears unlisted in `CMakeLists.txt` because it uses
  `file(GLOB_RECURSE SHIM_SRC ...)` — there is no dead source there; and `unity/`
  refused to delete with "Device or resource busy" from a transient handle, not an
  open Editor.

  `reference/` stays: the decoded file formats are engine-independent and still
  true. `archive-unity/` stays as history, 228 KB.

  Verified after: the payload signature still verifies, 8,017/8,017 blobs present,
  the release APK intact.

- **2026-09-01 (later)** — **The patcher audited, and six of seven findings closed.**

  The client verified every blob against a SHA-256 that came **out of the manifest**,
  and fetched the manifest over plain HTTP to a bare IP. So the check caught
  corruption and stopped no attacker at all: whoever writes the manifest decides
  what lands on the device, and that was anyone on the network path.

  | # | Finding | State |
  |---|---|---|
  | 1 | `manifest.json`'s `path` used as a destination with no validation | **fixed** |
  | 2 | Response bodies written until EOF, no size cap | **fixed** |
  | 3 | `REQUEST_INSTALL_PACKAGES` declared but never used | **removed** |
  | 4 | Manifest unauthenticated | **signed**; transport still cleartext |
  | 5 | `.patchbase` redirect readable by any app | **fixed** |
  | 6 | Version compared with `!=`, so downgrades accepted | **fixed** |
  | 7 | Data root readable/writable by any app with storage permission | **fixed** |

  **Path traversal (1).** `new File(rootDir, e.getString("path"))` with nothing
  checking it: `../../../../x` wrote outside the data root, and this app holds
  `MANAGE_EXTERNAL_STORAGE`. The download path also deletes the destination before
  renaming over it, so a hostile manifest could delete as well as create. Now
  rejected on both passes, and proven with a hostile manifest served over an adb
  reverse tunnel: `path escapes the data root: ../../../../sdcard/Download/...`,
  no file written, and the server log shows it never even asked for the blob.

  **Signing (4).** `manifest.json` is signed with a P-256 key; the client verifies it
  against the public half compiled into the APK, **before parsing the JSON** — a
  parser is the first thing an attacker reaches, so it must not run on unverified
  bytes. Fails closed, all three cases measured:

      valid signature                    Up to date  |  version 366
      one byte changed, sig kept         manifest signature does not verify
      manifest.sig removed from server   no manifest signature on the server

  An attacker who cannot sign cannot publish, whatever they do to the transport
  or to the host. **The private key is gitignored and must be backed up like the
  release keystore** — the public half is baked into every installed APK, so
  losing it means no further patch can reach existing installs at all.

  **Rollback (6).** A signature cannot stop an old manifest being replayed; it stays
  validly signed forever. Any version below the installed one is now refused. To
  ship old content deliberately, republish it under a higher number.

  **The data root (7).** `/sdcard/ran` is shared storage, and the client's C++ loaders
  are not hardened against hostile input — another app editing a `.rcc` in place
  was a route into this process. The root is now the app's own external files
  directory, unreachable by other apps on Android 11+, needing no permission, and
  still visible over adb. The native loader already tried that location; it just
  tried shared storage first, so the order is flipped and the old path stays as a
  fallback for adb-pushed test trees. That also moved `.patchbase` out of reach,
  which closes (5).

  Migration is the awkward part. Renaming would be instant and is tried first, but
  **Android refuses a rename from shared storage into `Android/data/<package>`
  whatever permissions are held** — measured with `MANAGE_EXTERNAL_STORAGE` granted,
  0 of 33 entries moved. So it copies, which took about five minutes for a real
  9.2 GB install on the emulator. `config.ini` is copied last and marks the tree
  complete, so an interrupt restarts rather than leaving half a tree; the old files
  go only once the new tree is good; any failure leaves the old root in charge.
  Verified end to end: migrated, booted from the private root, logged in, rendered.

  **What is left.** The transport is still cleartext HTTP. With the manifest signed
  that costs confidentiality, not integrity — nobody can change what is published,
  they can only watch it go past, and there is nothing secret in the payload. HTTPS
  needs a certificate on the patch host, which is not something this side can do:
  the client already speaks it, so it is `BASE_DEFAULT` plus removing the cleartext
  exception from `network_security_config.xml`. `MANAGE_EXTERNAL_STORAGE` is also
  still requested — needed now only to migrate an old install and to read the debug
  switches, and droppable once both are gone.

  Written up in `MOBILE/PATCHING.md`.


- **2026-09-01** — **The frame rate drop was the new HUD rebuilding itself sixty times a second.**

  Reported as "back to 35 fps on the tablet". Two things were true at once, and only
  one of them was a regression.

  **The 120 was never a tablet in-world number.** The 105-120 fps in this log is the
  emulator; the only in-world Tab S9 measurements ever taken are the 20.6 ms
  (~48 fps) in the character-shadow work below. So the honest comparison is
  ~48 fps against the reported 31, not 120 against 31.

  **The regression was real, and it was mine.** On the tablet the frame was 30.3 ms
  with GPU sections at 0.0 and swap at 0.5 - CPU-bound, not fill-bound - and the
  named sections only accounted for 9.3 ms of the 26.5 ms of engine CPU. Fifteen
  milliseconds a frame had no timer on them.

  It hid from both of the usual instruments. `/sdcard/ran/nulldraw` removes every GL
  call a draw makes and took the frame from 30.3 ms only to 25.0; the shim's
  `submit` timer reported 3.3 ms. Both only see draws issued through the renderer,
  and the touch overlay has its own program, VAO and buffer - so both reported a
  frame that was cheap while it was not. Five calls in `DxGameStage::Render` had no
  `RAN_SECTION` at all, `RanTouch_Render` among them; they have one now.

  With a timer on it, on LDPlayer:

  | | frame | of which submit | engine cpu |
  |---|---|---|---|
  | overlay drawn | 46.5 ms (21.5 fps) | 24 ms | 20.5 ms |
  | `/sdcard/ran/nohud` | 28.4 ms (35.2 fps) | 25 ms | 2.4 ms |

  The controls cost **18 ms a frame**, and were generating **94,086 vertices** to do
  it. Almost none of that changes between frames: the buttons do not move, and
  their faces, bevels, glosses and glyphs are identical. It was rebuilt every
  frame because there was nowhere to keep it.

  **Two fixes, and the first one alone made it worse.** Batching every shape into one
  draw took the overlay from a few hundred `glBufferSubData`+`glDrawArrays` pairs to
  7 a frame - and `touch-hud` went *up*, 8 ms to 18 ms, because expanding fans and
  strips to triangle lists tripled the vertex traffic. That is the measurement worth
  keeping: **the draw calls were never the cost.** Building the geometry was.

  So the static half is now built once into its own buffer and replayed from there,
  and rebuilt only when something it depends on changes - a button going down, a
  toggle lighting, the skill arc being rearranged, the window resizing. A 64-bit
  signature over exactly those inputs decides. The stick and the recharge wipes
  genuinely move every frame and are still built live; they are small. The segment
  list carries the blend mode, because that is the one piece of state the vertices
  cannot.

  **Result**, same scene, same build, toggled with `/sdcard/ran/nohud`:

  | | frame | of which submit | engine cpu |
  |---|---|---|---|
  | overlay drawn | 34.1 ms (29.3 fps) | 24.3 ms | 7.9 ms |
  | `/sdcard/ran/nohud` | 28.0 ms (35.9 fps) | 18.4 ms | 7.7 ms |

  **18 ms to 6 ms**, and the engine CPU is now identical with the overlay on and off -
  what remains is the driver drawing 94k cached vertices, which is LDPlayer being
  slow at vertex processing and should be a fraction of that on the tablet. The
  report says `0 rebuilds/s`; tapping PK produces exactly 2 (down, then toggled) and
  the lit state adds its bloom segment, 7 draws to 9.

  The tablet's missing 15 ms was CPU, and the CPU rebuild is what has been removed,
  so this should land there too - **but it has not been measured on the Tab S9**,
  which went offline mid-session. That is the one thing left to confirm.

  New: `/sdcard/ran/nohud` (live, re-read once a second) and a `touch-hud:` line
  reporting draws, vertices, rebuilds and cache size. Both join the list of debug
  switches to compile out for release.


- **2026-08-25 (evening)** — **The frame was being spent re-uploading vertex buffers, not drawing.**

  13 fps on the tablet reproduced exactly on the emulator, so the cause was not device-specific.
  Added a frame report (`FRAME` lines in logcat, `RAN_MOBILE`-guarded) that splits every frame into
  update / render / present, then splits render again into swap, draw submission and engine CPU, and
  finally times each section of the game stage. What it found:

  | | before | after |
  |---|---|---|
  | buffer uploads per frame | 94 (**7.5 MB**) | 86 (**390 KB**) |
  | time in those uploads | **45 ms** | **1.8 ms** |
  | in-game GUI section | 25 ms | 1.3 ms |
  | frame (same spot, emulator) | 77 ms (13 fps) | 49 ms (20 fps) |

  **Cause.** Every `Unlock` on a client vertex or index buffer re-specified the *entire* buffer with
  `glBufferData`, however little the client had written. The client locks a slice — a batch of UI
  quads, a patch of terrain, a run of particles — hundreds of times a frame. `glBufferData` also
  orphans the old storage, so the driver had to find new memory each time.

  **Fix.** D3D says what it is about to do at `Lock` time, so use it: a `D3DLOCK_READONLY` lock
  dirties nothing, every other lock records its byte range, and the range (not the buffer) goes up
  with `glBufferSubData`. A write that never told us a range still sends the whole buffer, so no path
  can leave GL holding nothing.

  **Two traps found on the way.**
  - The element-array binding lives *inside* the VAO. Routing uploads through the bind cache meant a
    later draw skipped the real bind as "already bound", so the draw had no index buffer; the
    emulator's GL encoder then treated the index offset as a host pointer and dereferenced null. The
    cache now forgets the element binding whenever the VAO changes or an upload runs outside a draw.
  - `glDeleteBuffers` frees the *name*, and `glGenBuffers` hands the same one back. The cached vertex
    layout is described against a buffer name, so a recycled name could skip re-specifying attribute
    pointers. Deleting a buffer now invalidates the layout.

  Also: textures no longer re-upload whole (a lock records its rectangle, and a partial update skips
  mip regeneration) — the font atlas was re-uploading and re-mipping for every glyph added.

  **Where the frame goes now (emulator, busy town):** swap 1.3 ms, engine CPU 10-19 ms, and draw
  submission 40-60 ms — of which `Render_MobItem` is nearly all of it, at ~222 GL draws a frame. That
  per-call cost is LDPlayer's GL translation (~0.3 ms/call); a real driver is roughly twenty times
  cheaper, so the tablet's profile will look different and needs its own `FRAME` line.

- **2026-08-25 (later)** — **Frame time halved again; the emulator had been running a stale ABI all along.**

  **LDPlayer runs the x86_64 slice, not arm64.** `./build.sh` alone rebuilds only arm64, so every
  emulator test since the LP64 fixes had been running an `x86_64/libran.so` from 00:23 — which is why
  world entry still died in `SHELPNODE::LoadFile` and the GUI still drew at desktop scale there. Both
  ABIs are now built before packaging (`./build.sh && ABI=x86_64 ./build.sh && ./build-apk.sh`), and
  with the current slice the emulator shows the real login scene, the mobile-scaled GUI and the world.

  **Five-matrix blend palette.** `ConvertToBlendedMesh` splits each skinned piece into groups whose
  bones fit the palette, and every group is one draw call. Raising the palette from four matrices to
  five (the ceiling for non-indexed blending: `D3DFVF_XYZB4` carries four weights and the fifth matrix
  takes the remainder) plus best-fit rather than first-fit packing cut the hand piece from 11 groups to
  6 and the body from 6 to 4. Measured in the same spot in town:

  | | before | after |
  |---|---|---|
  | skinned draws / frame | 152 | 54 |
  | frame time | 54.7 ms | 33 ms |

  Touched: `kMaxPalette`, the FVF choice, `MaxVertexBlendMatrices`, the shader (`aBlend` is a vec4,
  `uWorldM[5]`), the FVF walk (`XYZB4` now means four weights) and the device palette copy.

  **Win32 `clock()`.** The client measures its ping as a difference of `clock()` values and prints it
  as milliseconds — Win32 semantics (wall time, `CLOCKS_PER_SEC` 1000). Bionic gives processor time
  with `CLOCKS_PER_SEC` 1000000, so the ping display sat pinned at its 1000 ms clamp. The shim now
  provides `RanWin_clock()` and redefines both, so everything dividing by `CLOCKS_PER_SEC` stays
  consistent.

  **Characters: what the T-pose is and is not.** Instrumented the whole animation path on device
  (`ANIPROBE` lines, all `RAN_MOBILE`-guarded). Measured, in order:

  - animation `.bin`s load and go valid (`valid=1 ani=52 up=57`), so the RCC extraction path works;
  - every animation binds to a skeleton bone (`nobone=0`), and ~3300 quaternion keys are applied a second;
  - the key lookup advances with the clock (`count=62 last=9760 global=5557 -> key=35`);
  - the bones themselves move — spine, neck, upper arm and thigh all differ from bind;
  - the matrices reaching the GPU differ per bone and per frame.

  So the animation system is **not** the fault. What is visibly wrong is narrower: the torso and legs
  pose correctly while the arms stay stretched outward and a glove sits detached near the hip — the
  signature of the *attachment* path (`ppBoneMatrixPtrs[0..2]` = frame local x parent x bone in
  `SetupBoneMatrixPointersOnMesh`), not of vertex blending. Skin conversion was ruled out by
  measurement too: `pruned 0`, `maxInfl 2` — no face ever needs more bones than the palette holds.

  (Also added: `MOBILE/tools/pngcrop.js`, a dependency-free PNG crop/zoom so a 120-pixel character can
  actually be looked at; the guarded probes are still in `SOURCE/` for the next session.)

- **2026-08-25** — **World-geometry batching: tried, measured, reverted.**

  The UI batcher cut 210 UI draws a frame to 44, so the obvious next step was the same trick
  for world geometry. Two shapes were built and instrumented:

  1. **Index-range merging** for `DrawIndexedPrimitive` — consecutive draws out of the same
     vertex/index buffer, texture, state and transform whose index ranges follow one another
     become one draw, with no vertex copying. Result over 300 frames: `tried=14400 merged=0`.
     Nothing merged, and no rejection reason fired either — a state change lands between
     every pair of draws, so each one starts a fresh batch.
  2. **Extending the vertex batcher to non-UI `DrawPrimitiveUP`** (world geometry uses the UP
     path heavily), keyed on texture, state and transform, skipping skinned draws. Frame time
     was unchanged: 89.4 ms against 90.2 ms without it.

  Both are out again. The reason they cannot help is structural: the engine emits one draw per
  texture per mesh, and consecutive draws genuinely differ in texture or in world matrix.
  Merging them would need a texture atlas or bindless textures — a different project, and one
  that changes what the client draws rather than how the shim submits it.

  **Draw-path census** (per frame, in a busy town): `DrawPrimitiveUP` 202, indexed UP 235,
  `DrawIndexedPrimitive` 48, `DrawPrimitive` 31 — about 516 client calls, 330 GL draws after
  UI batching.

  So the emulator number stays where it was (~90 ms a frame in a dense area, ~0.25 ms per draw
  call through LDPlayer's GL translation). The next honest step for performance is a
  measurement on the tablet, not more shim work.

  Unrelated but done today: `SOURCE/` is committed. It was already a git repo (60 commits) —
  the `.git` lives in `SOURCE/`, not at the repo root, which is why an earlier check said
  otherwise — but the port's changes were uncommitted. There is now a baseline commit plus a
  `.gitignore` for the 2 GB of `_Build` intermediates, linker maps and build logs.

- **2026-08-25** — **Missing objects were missing data; UI batching cut the frame in half.**

  **"Some objects did not load" was the test device, not the port.** The emulator had never
  received `data/effect` (4,296 files — every skill, muzzle flash and lamp glow), `data/help`,
  the sound tree, or any map beyond the three pushed by hand, so anything referencing them
  drew nothing and the log filled with `.egp`/`.wld` open failures. After pushing them the
  count of failed opens for effects, sounds and maps is **zero**.

  What remains in the log is 378 lines of `item ran option setting file load fail` and
  `QUEST load fail`. Those are not a port defect: the PC client's own error log
  (`CLIENT/Logs/ErrorLog/`) contains the same 378 lines against the same data.

  **`CreateDirectory` did not go through the path layer.** The client asked for
  `\Data\Map\RanMapZipTemp\`, and Android created a single directory whose *name*
  contained backslashes. `RanPath_MakeDir` now normalises separators, matches existing
  components case-insensitively and creates the parents.

  **UI batching.** The client emits one draw per glyph, per icon, per bar segment: 210 of the
  233 draws in a world frame were pre-transformed UI. Consecutive UI draws that share a
  texture and a state epoch are now merged into one triangle list, flushed whenever anything
  they depend on changes (texture, render/texture-stage/sampler state, render target, clear,
  a world draw, end of frame). UI draws per frame: **210 → 44**. Frame time in the same spot:
  **~70 ms → 26 ms**.

  Also removed a heap allocation that ran on every draw (the light block was compared through
  a `std::vector`; it is a fixed buffer now).

  **What the emulator can still tell us: nothing more.** With the per-draw timer on, frame
  time tracks draw count almost exactly — 9.5 ms at 60 draws, 26 ms at 100, 87-95 ms at ~300
  — i.e. ~0.25 ms *per draw call*, which is LDPlayer's GL translation, not work the shim does.
  A real device does not pay that, so the next performance decision needs a tablet number;
  the tablet dropped off Wi-Fi before it could be re-measured with these changes.

  (The frame-budget line's draw timing is compile-time now — `RAN_TIME_DRAWS` in
  `native/CMakeLists.txt`, off by default, since it costs two clock reads a draw.)

- **2026-08-25** — **It runs on the real tablet, and it is faster.**

  **Galaxy Tab S9 (SM-X710, Adreno 740, Android 16, native arm64).** Two things were wrong
  there that the emulator never showed:

  1. **Scoped storage.** Every read of `/sdcard/ran` failed with `EACCES` and the client quit
     before the first frame. `MANAGE_EXTERNAL_STORAGE` has to be granted —
     `adb shell appops set com.ran.native MANAGE_EXTERNAL_STORAGE allow` — or the data has to
     live in the app-private directory. Noted in `push-tablet.sh`, which pushes the ~5.3 GB
     the client needs (only the maps it can actually reach).
  2. **The driver lied about S3TC.** Adreno advertises `GL_EXT_texture_compression_s3tc` and
     then answers `GL_INVALID_OPERATION` to the DXT uploads; the foliage came out as flat
     black and white slabs. The upload now checks `glGetError` on the first compressed
     texture and falls back to CPU decoding for the rest of the run, so no driver allow-list
     is needed.

  **The flicker was the swap.** The client draws like a D3D9 app with `D3DSWAPEFFECT_COPY`:
  it clears only its viewport rectangles and leaves the rest of the frame standing. EGL makes
  no such promise — after `eglSwapBuffers` the new back buffer holds whatever the rotating
  buffer had two frames ago. The surface now asks for `EGL_BUFFER_PRESERVED`, and where a
  driver refuses, partial clears are promoted to full-screen ones.

  **Performance work, measured rather than guessed.** A frame-budget line (total ms, and how
  much of it is inside draw submission) said two thirds of a 77 ms world frame was the shim
  submitting ~440 draws — about 125 µs each, which is two orders of magnitude off. Four fixes,
  in the order they mattered:

  | Change | Why it was slow |
  |--------|-----------------|
  | The client's vertex/index buffers get real GL buffer objects, refilled on unlock | every draw re-uploaded its whole slice through a streaming VBO |
  | `DrawPrimitiveUP` streams through a ring buffer | `glBufferData` per draw respecifies the store, so the driver allocates each time |
  | GL state cache: program, VAO, buffers, texture, blend/depth/cull | all of it was re-sent per draw, and consecutive draws share nearly all of it |
  | Vertex layout cache: attribute pointers only when FVF/stride/base/buffer change | ~10 GL calls a draw that almost never differed |
  | Uniform cache (flat array) + transform-derived values (MVP, view-projection, camera position) computed per pass instead of per draw | a matrix inverse and three multiplies ran on every draw |

  **And the frame is drawn at the size it is designed for.** The client runs at a logical
  ~1280x720; the frame used to be rendered at panel resolution and stretched by the GL
  viewport, shading four times the pixels for no extra detail. The window now asks for a
  logical-size buffer and the display compositor does the scaling, which is free.

  **Where that landed (LDPlayer, 2560x1440 panel):** login screen 105 ms → 9.5 ms a frame
  (~10 fps → ~105); in the world ~110 ms → ~70 ms (~9 fps → ~14). The world is still bound by
  GL call count, which on an emulator crosses a translation layer per call — the number that
  matters is a real-device one, and the tablet has not been re-measured since these changes.

  **Open on the tablet:** Android shows an ANR ("RAN isn't responding") while a map loads,
  because the load runs on the same thread that services input. The game keeps rendering
  behind the dialog; the fix is to move the game loop off the activity's main thread.

- **2026-08-25** — **The game renders: world, ground and full HUD.** Screenshot
  `native/out/final_world.png` — school grounds, other players with their names, the
  quickslot bars, HP/MP/SP/EXP, minimap compass, chat with its Thai quest lines, menu bar.
  Character-select is equally complete (`native/out/verify_cs5.png`).

  Three bugs stood between the last entry and this one, and all three came from the same
  place: **triangle winding**.

  1. **The ground and the whole HUD were being face-culled.** Disabling culling proved it in
     one build: everything appeared at once. The shim mapped `D3DRS_CULLMODE` to a GL front
     face on the theory that D3D and GL window spaces have opposite handedness, so the front
     face is the winding D3D culls. Measurement disagrees with the theory in this client:
     the world ground declares `D3DCULL_CCW`, the character-select ground declares
     `D3DCULL_CW`, and **both** are only visible with GL front = `CW`. One of the two maps
     disagrees with its own declared mode, which the PC build never notices because terrain
     draws unlit, where a back face is indistinguishable. So either winding now culls the
     same side, and `D3DCULL_NONE` still means none — the distinction that carries meaning.
  2. **UI geometry must not be culled at all.** The in-game HUD and the outer GUI emit
     opposite windings for their 2D quads; whichever front face was chosen, one of them
     vanished. Pre-transformed draws now skip culling entirely, which is what a cull mode
     means for a screen-space quad anyway.
  3. **`Clear` ignored its rectangle list.** `CD3DViewport::BeginScene` clears only its own
     rect; the shim cleared the whole screen every time, so a viewport that cleared late wiped
     the frame drawn before it. Clears are now scissored per rect.

  **A regression this session caught before it shipped:** freeing a texture's CPU copy after
  upload (the fix for running out of memory on the first world map) also wiped the font
  atlas, because the client re-locks that surface to add every new glyph — most of the game's
  text went blank. The copy is now freed only for compressed textures, which are never
  written to in place; that keeps essentially all of the saving, since the map set is DXT.

  **Measured now:** ~430 draws a frame in the world, `glErr=0x0000`, no allocation tripwire
  hits, login → character-select → world runs start to finish with `native/login.sh`.

  **Open:** frame rate in the LDPlayer emulator is ~9 fps at 2560x1440 (unbatched GL state
  and an ARM-translating emulator both contribute — needs a real device number before any
  optimisation work), effect/shadow/glow passes are still not exercised, and the virtual
  joystick and audio remain.

- **2026-08-25** — **The client is in the world.** Character-select → Start now loads the map,
  and the game renders school grounds, buildings, other players and this character, with the
  GUI at a size a finger can hit. Screenshot `native/out/world10.png`.

  **The GUI is a mobile GUI now.** The client was drawing its PC-sized controls at the panel’s
  2560x1440, so everything was half the size a touch needs. It now runs at a logical 1280x720
  and the frame is stretched to the panel (`RanGL_UIScale`, chosen so the logical width lands
  near 1280): every control, glyph and hit target doubles at once, touches divide by the same
  factor, and the viewport scales with them. It also costs a quarter of the fragments.
  Before/after: `native/out/mip1.png` vs `native/out/scale2.png`.

  **Four LP64 record traps, all the same shape.** These maps and data files were written by a
  32-bit build, so any record holding a pointer or a `size_type` is narrower on disk than
  `sizeof()` says on arm64. Reading one whole desynchronises everything after it:

  | Record | On disk vs arm64 | Symptom |
  |--------|------------------|---------|
  | `HELPNODE_SIZE` (`std::list::size_type`) | 4 vs 8 bytes | entering the world aborted on `malloc(8389754676365913933)` — the ASCII of `"Movement"` read as a size |
  | `POINTEX` (leads with `POINTA*`) | 24 vs 32 | map effect list walked off the end of the file |
  | `DXMATERIAL_MULTITEX` (holds `LPDIRECT3DTEXTURE9`) | 332 vs 344 | same, 12 bytes per material |
  | (audit) | — | `tools/layout-probe/audit-serialized-structs.js` now sweeps SOURCE for the pattern; it reports clean after these fixes |

  Each fix reads the record field by field and is byte-identical on MSVC. The one that found
  them is `shim/win/alloc_guard.cpp`: a tripwire on any allocation over 512 MB that logs the
  size, the size as text, and a symbolised backtrace — that turned a bare `std::bad_alloc` into
  `SHELPNODE::LoadFile` in two minutes.

  **Also landed:**
  - `CreateAdditionalSwapChain` is real (a view onto the device back buffer). `CD3DViewport`
    gives up on the whole in-game viewport when it fails, and the stub returned NULL.
  - `SHGetSpecialFolderPath` returns the data root, so per-character `.gameopt` and the error
    log stop resolving to `/Logs/...` at the filesystem root; the app creates those folders.
  - JPEG textures decode (`shim/d3d/image_decode_android.cpp`): 35 map textures are JPEG,
    `black.jpg` among them, and the terrain that references one drew untextured. Uses
    `AImageDecoder` resolved with `dlopen`, so the API-24 floor stands.
  - A texture’s decoded CPU copy is released once it is on the GPU — it was being kept for the
    life of the process on top of the GL copy.

  **Still open in the world:** the ground/terrain is loaded (92 octree nodes, 36,932 vertices,
  measured) but does not appear, and the in-game HUD draws nothing visible although ~229 UI
  draws a frame are issued. Both are next.

  RESOLVED (2026-08-26): both render. Terrain and the full HUD are visible in
  every in-world screenshot from the window sweep.

- **2026-08-25** — **Map textures now filter like the PC client.** The shim uploaded only mip
  level 0, filtered bilinear and clamped every texture, so the ground shimmered and blurred at
  distance. It now uploads the whole chain the DDS ships (every level already reaches
  `RanTexture` through `LockRect`), synthesises mips for single-level uncompressed textures, and
  honours stage 0's `D3DSAMP_MINFILTER` / `MAGFILTER` / `MIPFILTER`, `ADDRESSU/V` and
  `MAXANISOTROPY` — including the WRAP default, which had been clamped. Screenshot
  `native/out/mip1.png`.

  Also fixed while comparing against the PC client: with `D3DTSS_ALPHAOP = D3DTOP_DISABLE` the
  fixed-function pipeline takes alpha from the lit material rather than 1.0 — that is how
  `DxPieceQuickSort2::RenderPickAlpha` fades the objects between camera and player. The shader
  returned 1.0. A correctness fix; it did not change this screenshot.

  **Still open on the map:** flat sky-coloured gaps where some building geometry should be,
  visible in `native/out/band2.png`. Not fog and not a missing texture — no loader errors and
  `glErr=0x0000` — so the next step is finding which pass drops those pieces.

- **2026-08-25** — **Characters render.** `D3DXLoadMeshHierarchyFromX*` was a stub, so every
  skinned `.x` failed to load, every `DxSkinPiece` ended up with a NULL mesh container
  (measured: `DxCharPart::Render` bailed on `!m_pmcMesh` for 26000 of 26000 calls) and no
  character could draw. Four pieces were needed, all in `shim/d3d/d3dx_hierarchy.cpp`
  unless noted:

  1. **The hierarchy loader** — walks the parsed `.x` tree, drives the client's own
     `ID3DXALLOCATEHIERARCHY` (`CreateFrame` / `CreateMeshContainer`), links frames
     child/sibling, plus real `D3DXFrameDestroy` and `D3DXFrameCalculateBoundingSphere`.
  2. **`ID3DXSkinInfo`**, including `ConvertToBlendedMesh`: faces are grouped so no group
     needs more than four bones, vertices are duplicated per group and carry their weights
     in that group's palette-slot order, the bone-combination table names the original
     material per group, and subset ids are group indices — which is what
     `DxSkinMesh9_NORMAL::DrawMeshContainer` draws with `DrawSubset(iAttrib)`.
  3. **Mesh fixes** (`d3dx_mesh.cpp`): `MeshNormals` carries its own index list and rarely
     matches the vertex count (a 176-vertex piece ships 528 normals), so corner normals are
     folded onto vertices; a triangulated polygon now inherits its parent face's material
     instead of consuming one attribute slot per polygon.
  4. **Fixed-function vertex blending in the GLES backend** — `D3DTS_WORLDMATRIX(0..3)` plus
     `D3DRS_VERTEXBLEND` reach the shader, which blends position and normal per vertex.
     Blending applies only to draws whose FVF actually carries weights: the client leaves
     `D3DRS_VERTEXBLEND` set after a character.

  **The bug that cost the most time was in the patch, not the design:** the script that was
  supposed to add the blend branch to the shader matched nothing and said nothing, so the
  uniforms, the attribute and the device plumbing all existed while the shader still had no
  blending. Skinned meshes drew rigidly with palette slot 0, which looks exactly like a
  skinning bug — the body (spine bones, near-identity rotations) looked right while the
  forearms and hands flew off. Patch scripts now fail loudly when a replacement misses.

  **Verified against independent ground truth**: `tools/rcc-extract/xmesh.js` reads
  `CLIENT/data/skin/s_m_bs.x` as 253 frames and four mesh containers — 176v/220f/26 bones,
  256/358/5, 264/342/12, 108/100/6. The loader on device reports exactly those numbers, no
  piece reports a missing mesh container, and the character draws correctly posed with
  clothing, hands and face. Screenshot `native/out/clean1.png`.

- **2026-08-25** — **Render targets are real, and the character-select map draws.**
  The map was never missing: `CViewFrameMeshComponent::SetRenderState` (the character
  portrait panel) does `SetRenderTarget(0, m_pTargetSurface)` followed by
  `Clear(TARGET|ZBUFFER, 0x00000000)`, and the shim recorded the target without switching
  GL away from the default framebuffer — so that off-screen pass's black clear wiped the
  frame the scene had just drawn, every frame. The login screen has no portrait panel,
  which is why only this stage looked broken.

  `SetRenderTarget` now binds a cached FBO per target texture (colour attachment = the
  parent `RanTexture`'s GL texture, plus a depth renderbuffer), and switching back to the
  back buffer restores framebuffer 0 and the window viewport. Two consequences had to be
  handled: a texture rendered into must never be re-uploaded from its stale CPU bits
  (`m_isRenderTarget`), and GL texture rows run bottom-up, so an off-screen pass mirrors
  Y against the screen pass — one `uFlipY` uniform, with the front face reversed to match.
  Screenshot `native/out/rt1.png`.

  Diagnostics added while chasing this (kept for now): armed per-draw dumps of world draws
  (`RanDraw`), UI draws (`RanUI`) and texture uploads (`RanUp`), plus
  `native/login.sh`, which drives a cold start to character-select in one command.

- **2026-08-25** — **Login works end to end.** The client connects, authenticates, and reaches
  the character-select page with the account’s character listed. Screenshot
  `native/out/shot_login5.png`.

  Four bugs, in the order they surfaced:

  1. **Selecting a server aborted the process.** `CNetClient::CloseConnect` waits on its
     network thread and then closes the handle — join followed by detach. A `pthread_t` is
     valid exactly once, and bionic ABORTS on the second use. The shim now records whether a
     thread has been consumed.
  2. **The LZO wrapper wrote 8 bytes into a 4-byte int.** `MinLzo.cpp` casts `&int` to
     `lzo_uint*`; `lzo_uint` is pointer-sized, so the first compressed packet from the server
     smashed the stack canary (`__stack_chk_fail` in `CRcvMsgBuffer::getMsg`). Fixed with a
     properly typed local — identical behaviour on Win32, where the two types are the same
     width.
  3. **`printf("%s", someCString)` printed the object, not the text.** MSVC passes a class
     through varargs BY VALUE, which is what makes that MFC idiom work; the Itanium ABI clang
     uses passes it by invisible reference. The client’s version-file path arrived at
     `fopen` as `"/sdcard/ran/cVer.bin"` — libc++’s short-string header printed as text —
     so the client declared its own install corrupt and refused to log in. No layout trick
     fixes this: **248 call sites** now pass `GetString()` / `c_str()`, found and verified by
     `-Wnon-pod-varargs`, which is now on permanently. All of them compile unchanged on MSVC.
  4. The same investigation added the missing `RanOpen` failure detail (resolved path, errno,
     and a hex dump of the bytes) — that hex dump is what exposed bug 3.

  **Data on device now** (`push-data.sh`): map, piece, object, skeleton, skinobject, skin,
  animation and all texture trees — about 4 GB.

  **Known next problem:** the character model itself is not yet on screen at
  character-select (the map behind it now draws — see the entry above this one).

- **2026-08-24 (session 3, part 7)** — **The client reaches the login server.** The
  server-select page now lists the server the login server replies with, instead of showing
  a connection-error dialog.

  Two shim gaps, both in the same path:

  1. **A non-blocking connect reported the wrong error.** POSIX sets `EINPROGRESS`; Windows
     reports `WSAEWOULDBLOCK` for exactly the same situation, and `CNetClient::Connect`
     treats anything except `WSAEWOULDBLOCK` as a hard failure. The shim was translating the
     errno literally (10036 instead of 10035), so every attempt failed before a single byte
     was sent - visible in the log as `connect error:Code 10036`.
  2. **The socket-event layer was a no-op.** `WSAEventSelect`, `WSAEnumNetworkEvents` and
     `WaitForMultipleObjects` were stubs, and the last one only ever looked at the FIRST
     handle. The client’s network thread waits on three at once (kill, work, socket) and
     dispatches `FD_READ`/`FD_WRITE`/`FD_CLOSE`, so nothing would ever have been received
     even if the connect had succeeded. `shim/win/net_events.cpp` implements them for real:
     a socket-to-event binding table, `poll(2)` for readiness, `SO_ERROR` for the connect
     result, and a wait that returns the INDEX of the handle that fired. `FD_WRITE` is armed
     rather than level-reported, matching Windows - otherwise the network thread spins.

  The login address is read from the shipped (encrypted) `param.ini` and is now logged at
  boot, so a wrong address is visible immediately rather than looking like a network fault.

  **Next, and now the blocker for an actual login:** the soft keyboard. The ID and password
  fields cannot be typed into yet.

- **2026-08-24 (session 3, part 6)** — **Touch input reaches the UI.**

  Rather than divert the engine’s input code, the DirectInput device it asks for is now
  provided (`shim/platform/dinput_mobile.cpp`) and Android events are posted into it. The
  whole engine-side path - buffered `GetDeviceData`, key repeat, click timing, the outer UI’s
  hit testing - runs exactly as it does on PC. Touch maps to the left mouse button, moving
  the pointer before the press because the UI hit-tests on the button event and a touch
  delivers both at once. Android key codes map to DIK scan codes for the keys a login screen
  needs.

  One thing that was not obvious from the header: **the absolute pointer position does not
  come from DirectInput.** `DxInputDevice::UpdateMouseState` only integrates `DIMOFS_X/Y` when
  it owns a *device* mouse; otherwise it reads `GetCursorPos`. Feeding only the DirectInput
  queue produced button events at position (0,0) - the UI saw clicks that hit nothing. The
  shim’s `GetCursorPos` now reports the touch position.

  **Verified on device:** tapping the connection-error dialog’s button changed the dialog to
  the next state (“exit the game?”) and made the client retry its login connection - the
  click reached the real UI and ran the real handler. The app was stopped immediately
  afterwards; the standing rule about not repeatedly connecting to the live server applies to
  this test too, so it was run once.

  **Next:** the soft keyboard, so the ID and password fields can actually be typed into
  (`DXInputString` / `CIMEEdit` is the engine-side API), and a virtual joystick for the game
  stage. Character models still need a session, so they wait behind login.

- **2026-08-24 (session 3, part 5)** — **Lighting, fog, and the `.x` reader.**

  *Lighting and fog.* The fixed-function light, material and fog state was stored by the
  device and then dropped on the floor. The GLES backend now evaluates it: directional and
  point lights (range + attenuation), global ambient, material ambient/diffuse/emissive, and
  linear/EXP/EXP2 fog, all per pixel. Lighting is done in world space, which is where the
  engine’s lights live, so the world matrix and camera position go to the shader too. The
  login scene is now shaded and has atmospheric depth instead of being flat-lit.

  *The `.x` reader.* Skeletons, skins and map objects are all DirectX `.x` files, in three
  flavours that ship side by side: `bin` (binary token stream), `bzip` (the same stream
  MSZip-compressed) and `txt`. All three are read now:
  - `shim/d3d/xfile_parse.cpp` — the container: header, MSZip (each block deflated against
    the previous block’s output, fed in as a prepended stored block), binary tokeniser,
    text parser, and the standard D3DRM template GUIDs. Members are packed in stream order,
    which is the layout D3DX hands back from `Lock()` and what the engine casts.
  - `shim/d3d/xfile_com.cpp` — both enumeration APIs over one tree: `ID3DXFile*` (used by
    `DxFrameMesh`) and the legacy `IDirectXFile*` (used by `DxBoneCollector`).
  - `shim/d3d/d3dx_mesh.cpp` — `ID3DXMesh` with real vertex/index/attribute buffers, plus
    `D3DXLoadMeshFromXof` / `FromXInMemory` / `FromX{A,W}`, `D3DXCreateMeshFVF`,
    `D3DXCreateBuffer` and `D3DXDeclaratorFromFVF`. Faces with more than three corners are
    triangulated as a fan; MeshNormals / MeshTextureCoords / MeshMaterialList decide the FVF.

  **Verified against independent ground truth**, not just “it didn’t crash”: parsed one file of
  each flavour on the device and compared the object count with the JS reader in
  `tools/rcc-extract/xfile.js`. `b_13.x` (bin): 106 objects both sides. `b_2017_victors_wings.x`
  (MSZip): 10 both sides. `boxbox.x` (txt) parses to a Frame root. The self-test was removed
  afterwards.

  One trap worth recording: **a duplicate definition in a static library is not a link error.**
  The old stubs in `d3dx_loaders.cpp` and the new real loaders both defined the same symbols;
  the linker kept whichever object it pulled first, so the new code silently never ran. The
  stubs had to be deleted, not just superseded.

  **Next:** character models end to end (`DxBoneCollector` + `DxSkinMesh`) on the
  character-select page, which is what will exercise the mesh path for real.

- **2026-08-24 (session 3, part 4)** — **The login scene is complete: ground, grass, road, sky.**
  Screenshot `native/out/shot24.png`. Two more backend gaps, both found by A/B on the device
  rather than by reading code:

  1. **Texture stage 0’s combiner was ignored.** The shader always multiplied texture by
     vertex colour; the terrain selects the TEXTURE alone (`D3DTOP_SELECTARG1`) and carries
     black vertex colours, so the ground drew black. The stage’s colour/alpha op and both
     args are now uniforms, covering SELECTARG1/2, MODULATE/2X/4X, ADD and DISABLE, with
     `D3DTA_COMPLEMENT` / `D3DTA_ALPHAREPLICATE`.
  2. **The world path’s front face was inverted.** Disabling culling entirely brought the
     whole scene back, which is what identified it: solid ground was being culled while the
     two-sided foliage survived, so it looked like a missing ground mesh. Both paths use the
     same front face - the UI’s Y flip and the world path’s lack of one cancel against
     GL’s bottom-left window origin.

  Measured: `draws=70056 (ui=30756 textured=70056)`, 36.2M verts per 300 frames,
  `glErr=0x0000`, 60 fps. Diagnostics used to find these (per-mesh extent census, terrain
  node load counts, blend/cull A/B switches) were removed afterwards.

  **Next:** lighting and fog are still not applied (the scene is flat-lit), and character
  models need the `.X` mesh + animation loaders.

- **2026-08-24 (session 3, part 3)** — **The 3D login scene renders.** Screenshot
  `native/out/shot20.png`: the login map's tower, trees, buildings and foliage behind the
  server-select UI, at 60 fps, `glErr=0x0000`.

  Five bugs, found by measuring rather than guessing — each one hid the entire scene on its
  own, so they could only be found one at a time:

  1. **`D3DX_ALIGN16` was silently dropped.** The DX SDK applies `__declspec(align(16))` to a
     *typedef* (`D3DXMATRIXA16`); MSVC honours that, clang ignores it. `sizeof(SMatrixKey)`
     came out 68 instead of 80, so every animation-key array in a `.wld` read at the wrong
     stride — a desync that ended in a null-name `strcmp` crash. Fixed with the
     `__attribute__((aligned(16)))` spelling under `RAN_MOBILE`. (No packet struct uses
     `D3DXMATRIXA16`, so the layout gate is unaffected — re-checked.)
  2. **The frame loop was missing two thirds of itself.** `CGameClient2Wnd::FrameMove` calls
     `DxGlobalStage`, `DxResponseMan` **and** `DxViewPort`; the mobile app called only the
     first. Without `DxViewPort::FrameMove` the camera's clip volume was never computed, and
     since every renderer culls against it, **the whole world was discarded before drawing**.
     `Render()` now mirrors the PC one as well (light/camera shader constants, identity world
     matrix, MODULATE stage state).
  3. **`LOADINGDATALIST` is `std::list<DWORD>` holding pointers.** Fine on Win32, truncated on
     LP64 — the static-mesh loader thread dereferenced a half-pointer the moment culling
     started letting work through. Pointer-sized under `RAN_MOBILE`.
  4. **The MVP was transposed.** D3D is row-vector and stores rows contiguously; GL is
     column-vector and reads columns contiguously — the same bytes. Transposing applied the
     convention change twice and fed the shader the translation row where the projection
     column belongs: the scene drew as a fan of stretched triangles with `clip.w` in the
     hundreds of thousands. Copying instead of transposing gives `clip.w ≈ 609` for a vertex
     ~600 units away, which is what the camera says it should be.
  5. **The world path must not flip Y.** Only the UI path (screen pixels, Y down) needs the
     flip; flipping both drew the scene upside down. Because the two paths now disagree on
     winding, the front face is chosen per draw.

  Also fixed: `SetStreamSource`'s `OffsetInBytes` was ignored (the engine packs several
  meshes into one buffer and distinguishes them by it), and indexed draws used one number for
  both the index count and the vertex-buffer upload size — they are unrelated.

  Data: `push-data.sh` now also pushes `textures/` and the login maps. The engine-pack mode
  follows what is actually on the device — `Map.rcc` present means zip mode, otherwise loose
  files (a 548 MB pack plus a per-load scratch copy is not worth it on a phone).

  Diagnostics kept (they earn their place): engine `ToLogFile` mirrored to logcat (`RanEngine`),
  every failed file open (`RanOpen`), map load results (`RanLand`), static-mesh counts
  (`RanMesh`), per-interval draw census (`RanGL`). The one-shot traces used to find the above
  (cull decisions, `.wld` section offsets, per-draw dumps) were removed.

  **Next:** the terrain ground surface is still missing, and the scene has no lighting or fog
  yet. After that: character models, which need the `.X` mesh and animation loaders.

- **2026-08-24 (session 3, later)** — **Text works: the server-select page renders with real
  Thai.** Screenshot `native/out/shot12.png` shows `< เลือกเซิร์ฟเวอร์ >`, the connection-error
  modal with its ตกลง / ยกเลิก / ออก buttons, and the Latin copyright footer — all drawn by the
  engine's own font layer.

  The client draws text two ways and both are now real:
  - **ID3DXFont** (`shim/d3d/d3dx_font.cpp`) — the path `CD3DFontX` uses. Real
    `DrawTextW`/`DrawTextA` with `DT_CALCRECT` measurement, a 1024x1024 glyph atlas filled on
    demand, drawn as pre-transformed quads through the same device path as the UI. Also
    `ID3DXSprite`, `D3DXCreateFontIndirect{A,W}`, `D3DXCreateFontA`, `D3DXCreateSprite`.
  - **GDI** (`shim/win/gdi_text.cpp`) — the path `CD3DFont`/`CTextTexture` use:
    `CreateCompatibleDC`, `CreateDIBSection`, `CreateFont`, `SelectObject`, `SetTextColor`,
    `SetBkMode`, `GetTextExtentPoint32`, `ExtTextOut`, `GetDeviceCaps`.

  Both sit on `shim/win/ttf_raster.cpp`, a dependency-free TrueType reader and rasteriser
  written for this (cmap 0/4/6/12, simple + composite glyphs, quadratic flattening, nonzero
  scanline fill with 4x vertical supersampling and analytic horizontal coverage, synthetic
  bold/italic). The NDK ships no font library; glyphs come from the device's own
  `/system/fonts`.

  Three findings, each of which produced a *specific* wrong picture:
  1. **Every glyph was .notdef.** The device's Thai font (`NotoSansThai-Regular.ttf`) is
     21 KB and Thai-only — it has no Latin at all. Added a second face
     (`DroidSans.ttf`) consulted whenever the primary returns glyph 0.
  2. **CP_ACP was taken literally.** The engine passes codepage 0 to
     `MultiByteToWideChar` in several places. On the Thai Windows install it is built for,
     0 *is* 874; treating it as Latin-1 turned every Thai string into boxes. The shim now
     resolves 0/1 to the codepage `CHARSET::SetCodePage` last set.
  3. **The shipped text is UTF-8, not the legacy codepage the engine names.** Converting it
     as CP874 produced real Thai glyphs in the wrong order — readable as nonsense. The
     conversion now detects valid UTF-8 and decodes it as such, falling back to the codepage
     table otherwise. That is what turned the soup into `< เลือกเซิร์ฟเวอร์ >`.
     Also fixed `IsDBCSLeadByteEx`, which claimed lead bytes for single-byte codepages —
     under CP874 it made every Thai character swallow the next one.

  The client reaches the login server, fails to connect (expected — nothing was pointed at
  the live server) and shows the engine's own error modal. The app was force-stopped rather
  than left retrying, per the standing rule about the live server.

  **Next:** the 3D login scene. `DxLobyStage` loads `log_in.wld` and draws it before the UI;
  that needs the `.wld` terrain path plus the `.X` mesh/animation loaders
  (`D3DXFileCreate`, `D3DXLoadMeshFromXof`, `D3DXLoadMeshHierarchyFromX*`), which are still
  the stubs listed in the link report.

- **2026-08-24 (session 3)** — **Phase 3 first pixels: the login screen renders.** Built the
  GLES3 backend and wired the D3D9 shim to it end to end.

  What was added:
  - `shim/gl/gl_context.cpp` — EGL/GLES3 context on the frame-loop thread (ES3, depth24 +
    stencil8, 16-bit fallback, vsync).
  - `shim/gl/gl_render.cpp` — the fixed-function translation: one uber shader covering the
    pre-transformed (XYZRHW) UI path and the world path, D3D render state → GL state
    (blend / depth / cull / alpha test), FVF walking → attribute pointers, primitive-type
    mapping, texture upload including **DXT1/3/5 — uploaded natively when the GPU exposes
    S3TC, decoded on the CPU when it does not** (the test device has no S3TC).
  - `shim/d3d/image_decode.cpp` — real DDS / TGA / BMP / PNG decoding. DXT data is kept
    compressed on purpose: `TextureManager` keys its alpha handling off the created
    texture's format, so expanding it here would change how the game looks.
  - `shim/d3d/d3dx_loaders.cpp` — `D3DXCreateTextureFromFile{,Ex}{A,W}`,
    `...FromFileInMemory{,Ex}` and `D3DXGetImageInfoFrom*` are now real.
  - `RanDevice::Clear/Present/SetViewport` and all four draw entry points now reach GL.

  Four bugs found by running it, three of them invisible on Windows:
  1. **`CFileFind` never resolved its directory.** It called `opendir` on the raw Win32
     path, so `TextureManager`'s recursive scan of `\Textures` indexed **0 files** and
     every texture lookup silently missed — a blank screen with no error anywhere.
  2. **`"*.*"` was matched literally.** Win32 treats it as *everything*, including names
     with no dot — which is how the engine enumerates SUBDIRECTORIES. Matching it literally
     stopped every recursive scan at the top level. Fixing both took the texture tree from
     0 to **594 files**, and the frame from 0 textured draws to ~4.5 per frame.
  3. **`CFileFind::FindNextFile` had the wrong MFC semantics.** MFC returns FALSE on the
     LAST entry while that entry is still current; a readdir-as-you-go version returned
     TRUE for it and then a blank FALSE, which inserted an empty filename into the tree.
  4. **Back-face culling hid the entire UI.** Two inversions compose between D3D and GL —
     D3D names the winding that is *culled* while GL names the winding that is *front*, and
     flipping Y for GL's bottom-left origin reverses every triangle's apparent winding.
     Getting only one of them right culls everything that is drawn.

  Also fixed: vertex colours were read as RGBA when D3DCOLOR is B,G,R,A in memory (shader
  now swizzles), and `RanGLR_ApplyState` set uniforms with no program bound, so the
  alpha-test state was silently dropped.

  **Measured on device:** `Textures: 594 files` · `draws=1356 (ui=1356 textured=1356)` per
  300 frames · `glErr=0x0000` · 60 fps · screenshot `native/out/shot4.png` shows the RAN
  Online logo, the login window and the server-select panel. Data push now includes
  `CLIENT/textures` (`push-data.sh` gained a `push_tex` stage — textures live at
  `<root>/Textures`, not under `data/`).

  **Next in phase 3:** text (the font layer reaches `D3DXCreateSprite` and the GDI text
  path), then the 3D login scene — `.wld` terrain plus the `.X` mesh/animation loaders,
  which are still the stubs listed in the link report.

- **2026-08-24** — Decided to abandon the Unity rewrite and compile the PC client instead.
  Audited `SOURCE/` (renderer profile, MFC surface, LP64 hazards). Built `MOBILE/native/`:
  CMake + NDK harness, Win32/MFC shim, D3D9 headers reused verbatim. Got all five libraries
  to compile and link. Verified packet layout against real MSVC x86 and fixed the `CTime`
  alignment bug. Cleaned MOBILE from ~210 GB to ~13 GB (old APKs, logs, screenshots, Unity
  Library cache). Reorganised documentation into the map above.
- **2026-08-24 (session 2)** — Phase 2 built. Wrote the D3D9 shim (`Direct3DCreate9`, device,
  textures, VB/IB, state blocks — 325 COM methods, generated from the SDK header by
  `shim/d3d/gen-d3d9-impl.js` so no vtable can drift), all 33 out-of-line D3DX math
  functions, a silent DirectSound backend that keeps the real sound *logic* running, and
  inert stubs for IME/web/DirectInput. Re-enabled the real `CD3DApplication` framework and
  the font layer. Wrote `RanMobileApp` (mirrors `CGameClient2Wnd`) and `android_main`.
  **`libran.so` links with zero undefined symbols**, and `out/ran-phase2.apk` (158.5 MB,
  signed) is ready to install — see `native/PHASE2-BOOT.md`. Also fixed two more LP64 bugs
  found on the way: `long` vs `LONG` in the sound volume/pan calls, and `GLCLUB::GetMember`
  declared `inline` in a .cpp (MSVC emitted it, clang did not).
- **2026-08-24 (session 2, later)** — **PHASE 2 DONE: the real PC client boots and runs on
  Android.** Verified on the LDPlayer emulator (x86_64 — so an x86_64 build was added
  alongside arm64; the packet-layout gate was re-run for it and passes identically).
  Clean boot: RANPARAM read from the shipped `param.ini` (lang=5 Thai), **Gui.rcc indexed**
  (the encrypted RCC archives open and read on Android), game text parsed, D3D device
  created at 2560x1440, `OneTimeSceneInit` → `InitDeviceObjects` → `CreateObjects` →
  frame loop running at ~3.5 draws/frame with 13 textures (17.6 MB), 31 VBs, 16 IBs.
  Only one unimplemented D3DX entry point is reached at this stage: `D3DXCreateSprite`.

  Six real bugs found and fixed by running it — every one invisible on Windows:
  1. **`ran_fopen` recursed into itself.** The force-included `StdAfx.h` pulls in
     `windows.h` before the file's own guard line, so the `#define fopen` applied inside
     the redirect itself → stack overflow. Fixed with `#undef fopen` at the definition.
  2. **Windows paths.** The client's `SUBPATH` tables use `\Data\GUI\` and rely on a
     case-insensitive filesystem; the shipped tree is `data/gui`. Added
     `shim/win/path_resolve.cpp`: separator fix plus per-component case-insensitive
     resolution with a cache, wired into every file entry point.
  3. **minizip bypassed it.** The `.rcc` archives are opened by zlib's C code, which never
     includes `windows.h` — the resolver is now force-included into that target too. This
     was the difference between "GUI will be empty" and `Gui.rcc indexed`.
  4. **Null `CWnd`.** `DxResponseMan::OneTimeSceneInit` dereferences `pWndApp->m_hWnd`;
     passing NULL crashed at `0x8`. It now gets a real (inert) shim window object.
  5. **Device enumeration.** Advertising a fake 1280x720 for four pixel formats produced a
     mode list nothing matched, leaving `dwCurrentMode` pointing at uninitialised memory.
     The shim now advertises exactly one mode — the real surface, X8R8G8B8 — and the app
     asks for that size rather than RANPARAM's desktop resolution.
  6. **`CreateObjects` override dropped the base class's work.** Mirroring only the font
     half of `CGameClient2Wnd::CreateObjects` meant `InitDeviceObjects`/
     `RestoreDeviceObjects` never ran, so `DxGlobalStage` was uninitialised and the first
     frame null-derefed.

  Also hardened the generator: methods returning an interface through an out-parameter now
  null it and return failure instead of reporting success with the caller's pointer
  untouched — that pattern put the crash far from its cause twice.

- **2026-08-26 — the white sheet over the character, and what it really was.**
  The character at character select was drawn with a large white polygon across its
  torso, and the natural reading — that the skinning was wrong — was wrong. Chasing it
  by measurement rather than by patching produced four fixes, three of them general.

  **1. `D3DRS_VERTEXBLEND` is an enum, not a count.** An earlier optimisation raised the
  bone palette to five matrices, which made the engine set `D3DRS_VERTEXBLEND = 4` — a
  value D3D9 does not define — and the shim mapped anything outside `D3DVBF_1..3WEIGHTS`
  to "no blending", so those groups were drawn rigidly on their first bone. The palette is
  back to four (`kMaxPalette`, `MaxVertexBlendMatrices`, and the shader's `uWorldM[4]`),
  and an out-of-range mode is now logged rather than silently ignored. Verified against
  `DxSkinMesh9_NORMAL::DrawMeshContainer`, which computes `NumBlend` as the index of the
  last valid `BoneId` — so four matrices is the ceiling.

  **2. The draw batcher deferred draws past a state restore.** This was the white sheet.
  `DxSimpleMesh::Render` handles a subset whose texture failed to load by switching stage
  0 to `D3DTOP_SELECTARG2`, drawing, and restoring `D3DTOP_MODULATE`. The shim's UI batch
  flushed *after* that restore, so the subset was drawn modulating a texture that was
  never bound — which samples opaque white. `SetRenderState`, `SetTextureStageState`,
  `SetSamplerState`, `SetTexture` and `SetTransform` now end an open batch when the value
  actually changes. Draw count is unchanged (24168 per 5s window before and after), because
  the batch would have been flushed on the next draw anyway — only earlier.

  **3. `D3DTA_TFACTOR` was not implemented.** `argValue` in the fragment shader treated
  every argument that was not `D3DTA_TEXTURE` as the diffuse colour, so
  `D3DRS_TEXTUREFACTOR` — which the engine reaches for exactly when there is no texture to
  sample, and which `DxShadowMap` uses for the shadow circle — silently became the vertex
  colour. It is now a real uniform, defaulting to opaque white as D3D does.

  **4. Zeroed caps switched off every off-screen texture.** `DxSurfaceTex::InitDeviceObjects`
  clears `m_bDeviceEnable` unless `D3DDEVCAPS2_CAN_STRETCHRECT_FROM_TEXTURES` and
  `D3DPTFILTERCAPS_MINFPOINT` are both reported, and `RestoreDeviceObjects` then releases
  `m_pWaveTex` — taking refraction, glow, reflection and the post-process chain with it.
  `StretchRect` was a no-op returning `D3D_OK`; it is now a real `glBlitFramebuffer`
  between render-target FBOs (`RanGLR_BlitTexture`), and the two caps are reported.
  Measured after: `surface textures: device=1 option=1 wave=0x7dce7834e520`.

  Two smaller ones alongside: a render-target texture sampled before anything has been
  drawn into it now reads as transparent black rather than as no texture at all, and
  texture load failures name the file (path, byte count and magic) instead of sharing one
  anonymous "image decode failed" line.

  **What the measurement chain was**, since the wrong reading cost most of the time: a
  per-draw dump behind `/sdcard/ran/drawdump` reporting each draw's clip-space box, walked
  through *this draw's own indices* (walking the whole vertex buffer mixed every bone group
  together and produced the same four centroids for every draw — that artefact is what made
  the hand piece look exploded when it was correct); the engine naming each skinned draw
  through `RanGLR_DiagTag`; and `RanDiag_Backtrace`, which reduces return addresses to
  library offsets that `llvm-symbolizer` resolves against the unstripped `libran.so`. The
  backtrace is what turned "some quad" into `DxEffectBlurSys::RenderBlur` in one step.

  Verified: character select draws the full character — shirt, arms, hands, face, trousers,
  shoes — with the aura effect as ribbons rather than a slab; the world loads with map,
  water and fence textures correct and no skin piece missing a texture. `decomp_affine`
  was cleared by an off-device round-trip test (200000 rotations, worst error 0.0000), so
  the matrix decomposition is not a suspect.

  **Still open:** cube textures are unimplemented (`D3DXCreateCubeTextureFromFileExA`), so
  RESOLVED later the same day — cube textures are implemented; see the hair
  shading entry below.
  **(original note)**
  `LobbyCube.dds` never loads and character specular reflections are absent. This costs a
  reflection, not a white surface — nothing draws wrong because of it.

- **2026-08-26 (later) — hair, weapons and mob parts were being dropped by a four-byte
  record.** Reported as "some object did not load, some skin has no texture, some items
  don't show, hair doesn't show". All four were one bug.

  `DXMATERIAL_CHAR_EFF` contains an `LPDIRECT3DTEXTURE9`. The record the 32-bit client
  wrote is 596 bytes; this build compiles 600. Every
  `SFile.ReadBuffer( m_pMaterials, sizeof(DXMATERIAL_CHAR_EFF)*n )` in the character-effect
  loaders therefore consumed four bytes per material of whatever followed. Nothing
  complained about textures. Instead the effect loop in `DxSkinPiece::LoadPiece_02xx` read
  a *file version* where a type id belonged — `CreateEffInstance TypeID:512` is `0x0200`,
  `258` is `0x0102` — got NULL back, and dropped **the whole piece**. Hair, the sword and
  parts of some mobs simply never appeared.

  Fixed with `RanReadMaterialCharEff` / `RanReadMaterialCharEff100` in `DxMaterial.cpp`,
  which read the on-disk field layout one field at a time and discard the four-byte
  pointer. All 25 call sites across seven `DxEffChar*.cpp` files are `#ifdef RAN_MOBILE`
  guarded, so MSVC still compiles the original line.

  Found by logging each effect's TypeID **with the stream offset it was read at**: the
  effect *count* was sane, which ruled out everything before the loop and pointed at one
  effect's `LoadFile` consuming the wrong number of bytes. Measured after: zero piece
  failures, zero map-object failures, hair and weapon visible at character select and in
  world (`native/out/charselQ.png`, `native/out/worldA.png`).

- **2026-08-26 — `D3DRS_DEPTHBIAS` implemented.** `SMeshContainer::SetMaterial` packs a
  float into the state DWORD to lift decals, trims and effect layers off the surface they
  share; the shim ignored it, so those layers z-fought — "some skin, some texture is
  overlapping each other". Now mapped to `glPolygonOffset`, scaled by the depth buffer's
  resolution (`RanGL_DepthBits()`, 24 or 16 on the fallback config) because GL expresses
  the offset in smallest-resolvable-depth units while D3D adds it to depth directly. The
  sign carries through unchanged — the engine passes a negative bias to pull a layer
  toward the camera, and negative units do the same in GL.

  **Still open:** the loading screen never appears because `NLOADINGTHREAD::StartThreadLOAD`
  RESOLVED — the loading thread now hands the EGL context over; see the
  loading-screen entry below.
  **(original note)**
  renders it from a background thread, and the EGL context is current on the main thread
  only, so its GL calls go nowhere. Needs either a shared context made current on that
  thread or the loading frames pumped from the main thread between map-load steps.

- **2026-08-26 — item icons were a missing texture format.** Reported as "the icon of items
  did not show". They were drawing as solid black squares in every inventory and equipment
  slot.

  `RanGLR_UploadTextureLevel` had no case for `D3DFMT_R8G8B8`. Those fell into a `default:`
  that returned without uploading, on the reasoning that leaving the texture untouched beat
  uploading garbage. That reasoning is wrong on GLES: a texture object with no image data is
  incomplete, and an incomplete texture samples as **opaque black**. So the icon atlases —
  `textures/gui/School_Uniform.dds`, `Public_Weapon.dds`, `commercialset.dds`,
  `costume_GUI.dds`, all 512x256 24-bit uncompressed — produced perfect black rectangles
  while nothing failed anywhere: the DDS decoded, the texture object existed, it had a GL
  name, and `glErr` stayed `0x0000`.

  Added `D3DFMT_R8G8B8` (B,G,R in memory, expanded to RGBA) plus `A8B8G8R8`/`X8B8G8R8`, and
  made the `default:` name the format once instead of staying silent — which immediately
  surfaced a second unhandled format (`D3DFMT_A8B8G8R8`, a 128x128 texture) that nobody had
  noticed. Measured after: zero unhandled formats, icons visible for the uniform, trousers,
  shoes, weapon and bag contents (`native/out/inv9.png`).

  **How it was found, since three earlier guesses were wrong:** by following the PC path
  instead of the symptom. `CItemImage::SetItem` takes the atlas name from
  `pItemData->GetInventoryFile()` and the cell from `sBasicOp.sICONID`; logging both showed
  the names were correct and the cells in range, and `RanD3D_NoteTexturePath` (new — it
  records which file a GL texture id came from, through both the file and the RCC-memory
  loaders) showed the atlases loading fine. That left only the upload, and reading the DDS
  headers directly showed 24-bit uncompressed — a format the switch did not have.

  Also landed while chasing this: `SetDepthStencilSurface(NULL)` now really disables depth
  testing. The engine uses it to render into off-screen textures that have no depth of their
  own (`DxMeshTexMan`, and the glow and burn passes in `DxSurfaceTex`), while every
  render-target FBO in the shim keeps a depth attachment that nothing clears — so those
  passes were being depth-rejected. It was not what caused the black icons, but it is a real
  gap and it is fixed.

- **2026-08-26 — two more texture faults, both silent.**

  **24-bit and ABGR uploads were missing.** `RanGLR_UploadTextureLevel` had no case for
  `D3DFMT_R8G8B8`; those fell into a `default:` that uploaded nothing, and GLES samples a
  texture object with no image as opaque black. That was every item icon (the atlases are
  512x256 24-bit uncompressed), and it also cost the lamp glows, the buff row and the
  quick-slot icons. `D3DFMT_A8B8G8R8` / `X8B8G8R8` were missing too — found only because the
  `default:` now names the format once instead of staying quiet.

  **Textures uploaded from the loading thread were lost permanently.** The client renders its
  loading screen from a background thread, which has no EGL context: `glGenTextures` there
  produces nothing, and clearing the dirty flag made the loss permanent — the texture stayed
  blank even once the render thread reached it. Six textures were stuck at GL id 0, all of
  them loading-screen art (`loading_054.dds`, `ld_top`, `ld_back`, `ld_under`) plus
  `hinticon.dds` and `mapnameback.dds`. `RanGLR_OnRenderThread()` now guards `GlTexture()`,
  which leaves the texture dirty so the render thread uploads it properly. Measured after:
  no texture is left at GL id 0.

  Note this does **not** make the loading screen appear — that thread's draw calls still go
  nowhere. It makes its textures work for whoever draws them next.

  **Effects:** no defect measured in the starting area. The refraction pass reports
  `skipped, nothing asked to refract`, which is correct — `SetWave*` is what raises the flag
  and there is no water or refracting effect in view there. Ambient effects (lamp glows,
  the character aura, weapon trails) render. A concrete effect that looks wrong is needed to
  take this further.

- **2026-08-26 — hair shading and the white item on other players.**

  **Hair shading is a known gap with a name now.** The hair piece loads two character
  effects: `EMEFFCHAR_USERCOLOR` (25) and `EMEFFCHAR_SPECULAR2` (19). The sheen comes from
  `DxEffCharSpecular2::Render_Cube`, which binds a cube map to **texture stage 1** and
  redraws the mesh as a second pass with `pmtrlSpecular` supplying the effect texture. The
  shim binds one texture at a time and has no cube textures, so that pass runs without its
  cube map and the hair comes out flat. This is the first user-visible consequence of the
  stage-1 and cube-texture gaps already on the remaining-work list — they are what the hair
  needs, in that order.

  **The white item on other players is narrowed, not solved.** It is a skinned draw
  (`fvf=0x11A` = `XYZB3|NORMAL|TEX1`) with no texture bound, from the piece `blackcatyb_m`.
  Measured: the `.x` material has no `TextureFilename` node at all (the X loader reports it,
  and reports nothing else in the scene), and the `.cps` piece material's override string is
  empty too — so neither of the two sources `SMeshContainer::SetMaterial` normally uses has
  anything. The third source is `pmtrlSpecular[n].pEffTex`, which comes from
  `DXMATERIAL_CHAR_EFF::szEffTex` — the same records whose layout was wrong until today's
  LP64 fix. That is where to look next.

- **2026-08-26 — cube textures and texture stage 1, and the hair shading they were for.**

  Reported as "the hair does not look like the PC version, it has more colour shading".

  **Verified before building anything**, because the obvious answer was wrong. The natural
  read was that `DxEffCharSpecular2` needed fixed-function specular lighting, which the shim
  does not implement. Logging the branch said:

      specular2 [m_hair00[Mesh]]: cube path (flag=0x00000001, skinDetail=2, realSpecular=0, cubeTex=0x0)

  `Render()` picks its path from `m_dwFlag & EFFCHARSPEC2_CUBE` — effect data, not a cap — so
  the hair takes the **cube** path, and `realSpecular=0` means the vertex path would have
  returned early regardless. Specular lighting would have been wasted work.

  What the cube pass actually asks for, read out of the state block it applies
  (`m_pDrawCubeSB`): additive blending (`SRCBLEND=ONE`, `DESTBLEND=ONE`), stage 0
  `SELECTARG1` on the effect texture, stage 1 `MODULATE` with a cube map, and stage 1
  `TEXCOORDINDEX = D3DTSS_TCI_CAMERASPACENORMAL` with `D3DTTFF_COUNT3`. So: the effect
  texture times a cube map addressed by the camera-space normal, added on top.

  Implemented exactly that:
  - **Cube DDS decode** — `RanImage_DecodeCube` reads the six faces, each a full mip chain,
    face-major, which is how `lobbycube.dds` (256x256 DXT1, 9 levels, `caps2=0xFE00`) is laid
    out.
  - **`RanCubeTexture`** — a real `IDirect3DCubeTexture9`, uploading through
    `RanGLR_UploadCubeFaceLevel` to `GL_TEXTURE_CUBE_MAP` and honouring the render-thread
    rule, with the same S3TC-or-decode fallback the 2D path uses.
  - **The three `D3DXCreateCubeTexture*` entry points**, which were stubs returning
    `D3DERR_NOTAVAILABLE`.
  - **Texture stage 1** — `RanGLR_SetStage1` binds the cube map to texture unit 1, and the
    fragment shader modulates `texture(uTexCube, normalize(mat3(uView) * vNormal))` into the
    stage 0 result. Only the configuration the engine uses is implemented; any other stage-1
    op or texcoord source is reported once rather than drawn wrong.

  Measured after: `cube texture 6199 = /sdcard/ran/textures/effect/lobbycube.dds (256x256,
  9 levels)`, the hair effect's `cubeTex` is non-null, no stage-1 warnings, `glErr=0x0000`,
  and the hair renders with its shading instead of flat white. This also clears
  `D3DXCreateCubeTextureFromFileExA`, which was the last unimplemented entry point being
  reached.

- **2026-08-26 — the white item was two missing pieces of the fixed-function pipeline.**

  **Material diffuse was never applied to lighting.** `uMatDiffuse` was declared in the
  shader, given a uniform location and uploaded every draw — and never read in the shader
  body. D3D scales each light by the material (`light.Diffuse * material.Diffuse * N·L`, and
  `light.Ambient * material.Ambient`); without it every lit surface took the *light's* colour
  instead of its own. A piece with no texture therefore came out white. Fixed; the white
  signpost became grey metal and the white blob on the other player became a shaped figure.

  **Texture stage 1 did not exist.** Two configurations are used by the engine and both are
  now implemented, with anything else reported once rather than drawn wrong:
  - `MODULATE` with a **cube map** and `TEXCOORDINDEX = TCI_CAMERASPACENORMAL` — the
    character specular pass (hair sheen).
  - `MODULATE` of **`D3DTA_TFACTOR` with `D3DTA_CURRENT` and no texture at all** — how
    `DxEffCharAmbient::SettingState` tints a whole piece by a colour. This is what the
    guardian/pet pieces use, and it was being dropped entirely.

  The second one is worth noting for method: the first implementation required a stage-1
  texture, so the tint case fell through the guard silently. The "not implemented" log added
  alongside it reported `stage 1 with no texture, op 4 args 1/3`, which showed both that the
  case existed and that the arguments arrive as `(CURRENT, TFACTOR)` rather than the
  `(TFACTOR, CURRENT)` the engine source sets — `MODULATE` is commutative, so both orders are
  now accepted. Measured after: zero unimplemented stage-1 configurations, `glErr=0x0000`.

  **Still open on this piece:** `blackcatyb_m` and `december_guardian` genuinely carry no
  texture in the `.x` material or the `.cps` override — confirmed with per-frame naming after
  the first diagnostic proved unreliable (every mesh under a frame is unnamed, so it could
  only ever report the first one). They now draw with their material colour and tint rather
  than white, which is what the fixed-function pipeline gives them; whether that matches the
  PC exactly still needs a side-by-side.

  **Correction — the material diffuse change above darkened everything, and is now
  conditional.** Applying `uMatDiffuse` to the light term unconditionally was wrong: D3D takes
  the diffuse material from `D3DMCS_COLOR1` by default, meaning the *vertex colour* when the
  vertex has one, and the material only when it does not. The vertex colour is already
  multiplied in by the texture stage, so the material was being counted twice and the whole
  scene — GUI included — went dark. It now applies only when the vertex format carries no
  diffuse (`uHasVertexColor`, set per draw from the FVF, since it is a property of the vertex
  format rather than of any state block). Brightness measured back to where it was, and the
  untextured-material case still gets its colour.

- **2026-08-26 — off-screen passes verified, log noise cut, loading screen brought up.**

  **Off-screen chain (was: enabled but never looked at).** Five targets now render each
  frame — 1280x720, 512x512, 512x384, 256x256, 128x128 — so the glow and surface-texture
  passes do run, with `glErr=0x0000` and no visual break. Post-process stays inert, but by
  accident rather than design: `DxPostProcess::RestoreDeviceObjects` returns early when
  `CreatePixelShader` fails **without** clearing `m_bDeviceEnable`, and it is `m_pSufTEMP`
  being left NULL that keeps `m_bEnable` false.

  **Hazard recorded, deliberately not changed:** the shim reports
  `PixelShaderVersion = D3DPS_VERSION(2,0)` and `VertexShaderVersion = 1.1` while
  implementing no shaders at all — the mirror image of the caps bug fixed earlier today.
  Several systems branch on those (`DxShadowMap`, `DxCubeMap`, `DxEffectMan`), and
  `RenderDevice.cpp:454` uses them to decide whether the device is acceptable at all.
  Lowering them without a device to verify on risks breaking the boot, so it waits.

  **Log noise.** Two of my own diagnostics were flooding: the surface-texture state line ran
  every frame (3937 lines a session, now once per change), and the diagnostic flag files were
  probed once a second through the logging `fopen`, so every absent flag printed a failed
  open (`access()` now, 3949 -> 589). Removed the traces that had served their purpose — the
  per-effect TypeID trace, per-texture queue/complete logging, the per-part material dump,
  the specular2 path log, the item icon logs, and the old ANIPROBE matrix dump. Kept every
  once-per-name failure signal, each of which has already caught a real bug.

  **Loading screen now renders.** `NLOADINGTHREAD::LoadingThread` is a full
  `Clear`/`BeginScene`/draw/`Present` loop on a thread the client starts while the main
  thread loads the map, and EGL binds a context to one thread — so all of it went nowhere.
  The context is handed over for the duration: released on the main thread before
  `_beginthreadex`, acquired at the top of the loading thread, released again before it
  exits, re-acquired in `EndThread`. "The render thread" now means *the thread holding the
  context* (`RanGLR_OnRenderThread` -> `RanGL_HasContext`), without which uploads made while
  the loading screen is up would be skipped and the screen would stay blank.

  Verified on screen: loading art, the map-name plate, the hint icon, the step indicator and
  the copyright line all draw (`native/out/ldx3.png`); no `eglMakeCurrent` errors; no texture
  left at GL id 0; the game renders normally after the handover back.

- **2026-08-26 — the loading spinner, and text sitting off the line.**

  **`ID3DXSprite::Draw` was a stub** returning `D3D_OK` and drawing nothing, so the loading
  screen's step indicator was a blank circle where the PC shows an animated ring. It is now
  a real screen-space quad: the source rect picks the cell out of `loading_st.dds` (512x128,
  four 105x105 frames), `pCenter` is the point placed at `pPosition`, the sprite transform
  applies, the colour modulates, and `Begin`/`End` save and restore the device state D3DX
  touches — with the half-texel offset that stops an atlas cell bleeding into its
  neighbours. Verified: the segmented ring with its progress arc and "Loading.." draws, and
  `loading_st.dds` finally reaches the GPU. This fixes every sprite in the client, not just
  this one.

  **Text was about a fifth too tall and off-centre in its line.** `D3DXFONT_DESC.Height`
  follows `LOGFONT.lfHeight`: positive means the **cell** height, so Windows picks a face
  whose ascent + descent comes to that many pixels; negative means the em box.
  `CD3DFontX::RestoreDeviceObjects` passes a positive `m_iHeightScreen`, and the shim scaled
  by the em box regardless. `fontScale()` now divides by `AscenderUnits() + DescenderUnits()`
  for a positive height and by `UnitsPerEm()` for a negative one, and `GetDC` converts back
  to an em size before `CreateFontA` so the DC the client measures with describes the font
  actually being drawn. `CreateFontA` in `gdi_text.cpp` was already right — the client only
  ever passes a negative height there.

  Measured after: chat tab labels sit centred in their buttons, and the HUD's `7.01%` and
  `Lv. 4` fit their boxes instead of clipping.

## Text sat low in every button — substituted font metrics (fixed 2026-08-26)

**Symptom.** Button captions ("เริ่ม", "ยกเลิก", "ออก") sat low in their buttons
with a wide gap above; "สร้าง(1)" clipped at the bottom.

**Not a centring bug.** `CBasicTextButton::CreateButtonImage` sets
`TEXT_ALIGN_CENTER_X` only — vertical placement is top-alignment inside a caption
box whose rect comes from `CLIENT/data/gui/uiextcfg.xml`:

    BASIC_TEXT_BUTTON_IMAGE_TEXTBOX18   Y=3  H=15   (button 18 tall)
    BASIC_TEXT_BUTTON_IMAGE_TEXTBOX22   Y=4  H=18   (button 22 tall)

So the baseline is `box.top + tmAscent`. Nothing centres anything; the layout was
measured against one specific font's metrics.

**Cause.** `_DEFAULT_FONT` is **Tahoma** (`DxFontMan.cpp:13`), asked for at em
size 12 (`CD3DFontX` does `-MulDiv(9, 96, 72)`). The shim resolves that to
`/system/fonts/NotoSansThai-Regular.ttf` and then took ascent/descent from
*that* face. Noto's ascender is 1.16 em against Tahoma's 1.0005:

    face                 ascent  descent  lineH   (at em 12)
    Tahoma (GDI)             12        2     14
    NotoSansThai (was)       14        4     18

Two pixels of extra ascent in a caption box only 15 px tall is the whole caption
dropping toward the bottom edge — and `H=15` boxes could not hold an 18 px line,
which is what clipped "สร้าง(1)".

**Fix.** `RanFont_WinMetrics` / `RanFont_WinEmForCellHeight` in
`shim/win/ttf_raster.cpp`: a table of the OS/2 winAscent/winDescent of the faces
this client names (Tahoma, Verdana, Arial, Microsoft Sans Serif), rounded the way
GDI rounds. `gdi_text.cpp` and `d3dx_font.cpp` take ascent/descent/lineH from the
*named* face; glyphs still rasterise from the substituted face at the same em
size. Unknown face names fall back to the real face's metrics.

Rounding matters: GDI rounds each metric to nearest independently
(12.006 -> 12, 2.478 -> 2, tmHeight 14). Rounding the sum, or ceiling either
half, moves the baseline a pixel.

**Verified.**
* `GetTextMetricsA` on Windows 11, Tahoma `lfHeight=-12` -> ascent 12, descent 2,
  height 14. The shim now logs exactly `lineH 14 ascent 12`.
* PC client screenshot, "ออก" button: button top 820, ink rows 828..834,
  baseline +15, x-height 7.
* Mobile after the fix, same button: baseline +15, x-height 7 — identical.
* Char-select "ยกเลิก": button 739..780, ink 750..769 -> 11 px above, 11 below.
  Was ink 757..773 (18 above, 7 below).
* "สร้าง(1)" no longer clips.

**Still open, seen in the same shot:** the top-left char-select button renders
`ź` instead of its label — a codepage/text bug, unrelated to layout.

RESOLVED (2026-08-26) — that was the CP874/UTF-8 decoder guessing on
well-formedness alone. See "Thai text: short labels decoded as the wrong script".

## Thai text: short labels decoded as the wrong script (fixed 2026-08-26)

**Symptom.** The character-select delete button read `ź`. Other short Thai labels
were affected the same way; long strings were fine.

**Cause.** `looksUtf8()` in `shim/win/gdi_text.cpp` decided UTF-8 vs CP874 by
well-formedness alone. Both encodings are in play — the loose GUI XML is UTF-8,
the game-text tables inside `Gui.rcc` are CP874 — so a guess is unavoidable, but
well-formedness is not enough to make it:

    "ลบ"  = CP874  C5 BA
                 = UTF-8  U+017A  "ź"

CP874 Thai occupies 0xA1..0xFB, which overlaps UTF-8's continuation range
0x80..0xBF, so short Thai words are frequently valid UTF-8 by accident. Long
strings almost always trip over an invalid byte and fall back correctly — which
is exactly why only the shortest labels broke.

**Fix.** Decide on plausibility, not just validity. A real UTF-8 Thai string
decodes into the Thai block; CP874 misread as UTF-8 lands in Latin Extended-A,
Gujarati, Malayalam. `looksUtf8` now does a full validating decode and rejects
the run unless every multi-byte sequence yields Thai (U+0E00..U+0E7F) or one of
CP874's own nine non-Thai high characters. It also rejects overlong forms
(0xC0/0xC1 leads, and short 3/4-byte encodings) and surrogates, which kills a
whole family of two-byte Thai pairs on its own.

**Verified.**
* Offline sweep of every distinct Thai byte-run in `Gui.rcc`: strings the old
  rule misread 22946, the new rule 17. The residual 17 are the genuinely
  ambiguous `E0 B8/B9 xx` pattern (CP874 "เธ_"/"เน_" vs UTF-8 Thai), which
  resolves to UTF-8 — the right call.
* Round-trip: genuine UTF-8 Thai still detected and decoded; the same words in
  CP874 bytes are no longer mistaken for UTF-8.
* On screen: the button reads `ลบ`. Character select and in-world chat, tabs,
  NPC names and window labels all render correct Thai.

**Also checked**
* Glyph coverage: all 87 CP874 Thai codepoints are present in
  `/system/fonts/NotoSansThai-Regular.ttf`; all printable ASCII in the Roboto
  fallback (the Thai face has 1/95, so the fallback is load-bearing).
* Only the D3DX text path is live (`SetFontSys` forces `EMFONT_D3DX`), and both
  it and the GDI path share the corrected decoder.
* `IsDBCSLeadByteEx` correctly reports 874 as single-byte. The unqualified
  `IsDBCSLeadByte` is wrong for Thai but is only reached from BugTrap.

### Known remaining gap: no Thai shaping

`NotoSansThai-Regular.ttf` carries `GSUB: ccmp` and `GPOS: mark, mkmk`. The
rasteriser applies none of them — glyphs are laid out linearly by advance, and
combining marks land wherever their default outline puts them.

In practice the common cases come out right, because the default outlines are
drawn at heights that suit them: tone-mark-over-upper-vowel stacks correctly in
"เริ่ม", "ขึ้น", "แล้ว". What `ccmp` exists for is the tall consonants
ป ฝ ฟ ฬ, whose ascender the mark must clear via a substituted lowered form.
Without it a mark can collide with the ascender.

On the PC this is done by Uniscribe behind `ID3DXFont` (which is why
`D3DFontX.cpp` deliberately loads `d3dx9_35.dll` "which shapes Thai correctly via
Uniscribe"). Closing it means implementing the `ccmp` substitution and the
`mark`/`mkmk` attachment lookups — not hand-tuned offsets.

NOT MEASURED: how often this occurs in real game text. Extracting the string
table from the packed `Gui.rcc` with a byte scan pulls too much binary noise to
give an honest frequency, and I have not written a real `.rcc` reader for it.

SUPERSEDED (2026-08-26): the frequency question turned out not to be the point.
The real gap was GSUB `ccmp`, without which a tone mark is never positioned by
anything at all. Shaping is now implemented — see "Thai shaping implemented".

## Inventory tooltip crash — not the keyevent (2026-08-26)

**Symptom.** Sending keyevents to open the inventory/character/skill windows
killed the client. It looked like an input-handling crash; it was not.

**What it actually was.** Tombstone, symbolised against the matching BuildId:

    NS_ITEMINFO::LOAD          UIItemInfoLoader.cpp:1715  <- SIGSEGV
      inlined: std::string::c_str -> __is_long
    CInnerInterface::SHOW_ITEM_INFO   InnerInterfaceSimple.cpp:1625
    CInventoryWindow::TranslateUIMessage  InventoryWindow.cpp:175

Line 1715 col 101 is `COMMENT::BLOW[emBLOW].c_str()`. `COMMENT::BLOW` is
`std::string[EMBLOW_SIZE]` (9), and every use in that block indexes it with
`emTYPE` unchecked, so an out-of-range value reads a `std::string` out of
arbitrary memory and dereferences it. Opening the inventory showed a tooltip;
the tooltip did the rest.

Frame #0's symbol read `NS_ITEMINFO::LOAD+27706`, which is only the nearest
preceding exported symbol — the real location came from `llvm-symbolizer
--inlines`, and the inlined `c_str`/`__is_long` frames are what identified the
expression.

**Measured, in this order:**
1. Guarded the index and logged the value: `blow type -1, outside 0..8`.
   The client no longer dies, and all three windows open.
2. Suspected the LP64 record trap, so checked it instead of assuming:
   `sizeof(ITEM::SSUIT)` matches the size on disk, and a per-chunk trace of
   `SITEM::LoadFile` shows every header-carrying chunk (FILE_SBASIC..
   FILE_SGENERATE) reading exactly its stated length. **The item file is read
   correctly** — this is not a layout bug.
   (First cut of that trace also flagged chunks 7..14 and 100. Those are false
   positives: `FILE_BOX` and above carry no version/size header and hand the
   stream to a sub-loader, so `dwVer`/`dwSize` were stale. The probe now only
   checks the chunks that have a header.)
3. Logged what was asked for versus what came back:
   `asked for item 0/0, got record 0/0`.

**Where it stands.** The request is for item **0/0**, which is the value of a
*zeroed* `SITEMCUSTOM`, not a default-constructed one — `SITEMCUSTOM::SITEMCUSTOM`
initialises `sNativeID(false)`, i.e. ID_NULL/ID_NULL, and
`CItemSlot::ResetItemImage` explicitly sets `NATIVEID_NULL()`. The guard in
`InventoryWindow.cpp:174` tests `!= NATIVEID_NULL()`, which 0/0 passes. So a slot
somewhere is being filled from an `SINVENITEM` whose memory was zeroed rather
than constructed.

`GLItemMan::GetItem` then returns non-NULL because `m_ppItem` is zeroed on
allocation and entry [0][0] is genuinely populated — so a real record comes back,
and its blow type is -1.

NOT YET FOUND: which path puts a zeroed `SITEMCUSTOM` into a slot. The candidates
are `CInventoryPage::LoadItemPage` and the inventory packet parse
(`GLInventory::SETITEM_BYBUFFER`). The bounds guard makes this survivable and
self-reporting in the meantime.

FOUND (2026-08-26): neither candidate. `SINVENITEM` keeps `sItemCustom` in an
anonymous union, so `SITEMCUSTOM`'s constructor never runs and the slot is simply
uninitialised — which reads as 0/0 on Android's zero-filled pages. See "Empty
inventory slots read as item 0/0".

## %I64d printed literally (fixed 2026-08-26)

Found while checking the above: the character window showed `EXP I64d /I64d` and
`แต้มผลงาน I64d`. `%I64d` is MSVC's spelling of a 64-bit conversion; bionic does
not know `%I`, drops the conversion and prints the rest literally, so every
64-bit number in the UI was missing. There are 158 such format strings in SOURCE.

Fixed in one place instead of 158: `shim/win/fmt_msvc.h` rewrites `%I64` to `%ll`
and `%I32` to plain, preserving flags/width/precision and leaving `%%` alone.
`CString::FormatV` and `StringCchVPrintfA`/`StringCchPrintfA` route through it,
so the SOURCE call sites stay byte-identical to the PC build.

Verified by compiling the header for android-x86_64 and running it on the device,
so bionic's own printf is what was tested:

    EXP %I64d /%I64d  -> EXP %lld /%lld       %-8I64u|      -> %-8llu|
    %.2f and %I64x    -> %.2f and %llx        %%I64d literal-> %%I64d literal
    %I32d plain       -> %d plain             rendered: "EXP 164"

## Thai shaping implemented (2026-08-26)

The rasteriser laid glyphs out linearly and applied none of the font's layout
tables, so combining marks landed wherever their default outline put them. The
visible symptom was tone marks floating well above the line — `ร์` in
"เซิร์ฟเวอร์" sat a third of an em too high.

**Why that happens.** `NotoSansThai-Regular.ttf` gives a tone mark two forms and
picks between them in GSUB, then positions it in GPOS. Read out of the font:

    ccmp lookup 2  (unconditional)     93..97 -> 73..77   the LOW forms
    ccmp lookup 3  (chained context)   73..77 -> 93..97   but only when a vowel
                                                          precedes - the HIGH forms
    GPOS 'mark'    covers 54, 57-63, 72-79, 105-107       attaches to a base
    GPOS 'mkmk'    stacks a mark on a mark

The cmap always yields the high form (93..97), and GPOS `mark` does not cover
that range at all. So without `ccmp` a tone mark is never repositioned by
anything — it just floats. `ccmp` is not an optional refinement here; it is what
moves the glyph into the range GPOS can attach.

**Implemented** in `shim/win/ttf_layout.cpp`: GDEF glyph classes, GPOS lookup
types 4 (MarkBase) and 6 (MarkMark), GSUB types 1 (single), 2 (multiple) and 6
format 3 (chained context), plus coverage formats 1/2, class definitions 1/2 and
anchor formats 1-3. `d3dx_font.cpp` now shapes a run before laying it out:
cmap -> ccmp -> rasterise by glyph id -> attach marks. The glyph cache is keyed
by glyph id rather than codepoint, because after `ccmp` the glyph for a
codepoint depends on its neighbours.

Marks also stopped advancing the pen, and the measured width (`DT_CALCRECT`,
the centring width, and `GetTextExtentPoint32A`) skips them to match — a
measured width that disagrees with the drawn width shows up as text sitting off
-centre.

**Verified.**
* The table walk was checked against the real font in node before building:
  GDEF classes correct, `mark` gives ก dx=1284 / ฬ dx=1263 dy=303 (the tall
  consonant raises the mark), `mkmk` stacks at dy=500.
* Placement was simulated end to end and compared old vs new before building,
  which showed the GPOS-only change was tiny — correctly, since the real fix was
  `ccmp`.
* On screen: "< เลือกเซิร์ฟเวอร์ >" now renders with `ร์` sitting on its
  consonant instead of floating. Login, character select, in-world chat and ten
  windows all render correct Thai with no crash.

One earlier reading was wrong and worth recording: I first read the floating
marks as a regression from my own GPOS change. Cropping the same title from a
build made *before* that change showed them floating identically — the bug
predated it. Comparing against the previous build's screenshot, rather than
against expectation, is what caught it.

## Inventory 0/0: the layout theory is refuted (2026-08-26)

`GLInventory::SETITEM_BYBUFFER` reads `SINVENITEM_SAVE` as a raw struct blob
straight off the network stream, with `GASSERT(dwSize==sizeof(...))` compiled out
in release — the same shape as the item-file read, and the obvious suspect for a
32-bit/LP64 mismatch.

It is not that. A loud check now compares the server's `dwSize` against this
build's `sizeof(SINVENITEM_SAVE)` on every load, and it stays silent: **the
record sizes agree**. Combined with the earlier item-file result (every
header-carrying chunk reads its exact stated length), two separate raw-blob paths
have now been measured and cleared.

So the `0/0` is not a desync. What remains is either an inventory record the
server genuinely sends zeroed, or a field-offset difference that happens to
preserve the total size. Not chased further this session.

What is fixed regardless: the tooltip no longer crashes on it, and it reports
itself. Both size checks stay in as permanent, silent-when-healthy integrity
checks — this is a class of bug that otherwise corrupts data with no signal at
all.

## Window sweep (2026-08-26)

All 17 menu hotkeys from `RANPARAM::MenuShotcut` opened and closed, in world:
inventory, character, skill, party, quest, club, friend, map, chat macro, item
bank, item shop, run, and the five unlabelled ones. **No crashes.** Thai is
correct in every window that opened, and `%I64d` numbers now render.

Two notes from the sweep:

* A first pass reported "no window opens" for all 17. That was the measurement,
  not the game: the baseline screenshot it diffed against already had windows
  open. Re-run from a verified-clean state, ten windows opened with 8-34% of the
  frame changing.
* The Assist, Club and item-bank panels look see-through. Measured rather than
  assumed: the body of a `CBasicLineBox` is a single 1x1 texel of
  `interface_main.dds` stretched to fill, and those texels carry alpha 132-153
  out of 255. **The translucency is by design**, not a decode bug — so it has
  been left alone. Confirming it matches the PC needs a side-by-side of the same
  window on the PC client, which has not been done.

## Empty inventory slots read as item 0/0 — root cause found (2026-08-26)

The tooltip crash's real cause, traced end to end this session.

`SINVENITEM` keeps `sItemCustom` inside an **anonymous union**:

    union {
        struct { SINVENITEM_SAVE sSaveData; };
        struct { WORD wPosX; WORD wPosY; SITEMCUSTOM sItemCustom; };
    };

A union has no active member to construct, so `SITEMCUSTOM`'s constructor — the
one that sets `sNativeID(false)`, i.e. ID_NULL — **never runs**. And
`SINVENITEM`'s own constructor initialises only `wBackX`/`wBackY`.

On Windows this goes unnoticed: fresh heap holds debris, and debris does not
look like a valid item id. Android hands out **zeroed** pages, so an untouched
slot reads as item **0/0** — which is a legal id, and therefore slips past every
"is this slot empty" test in the client, all of which compare against
`NATIVEID_NULL` (0xFFFF/0xFFFF). `InventoryWindow.cpp:174` then asks for a
tooltip on an empty cell, and `NS_ITEMINFO::LOAD` indexes `COMMENT::BLOW` with a
garbage blow type and segfaults.

**Fix:** `SINVENITEM::ClearSlot()`, called from both constructors, sets the
position and both native ids to `SNATIVEID(false)`.

Note this is **not** `#ifdef RAN_MOBILE` guarded. It initialises memory that was
previously left uninitialised, which is a strict improvement on MSVC too and
keeps the two builds behaving identically; guarding it would leave the PC on
undefined behaviour. Flagged rather than done silently.

**How it was found** (four wrong theories, each killed by measurement):
1. LP64 layout mismatch on the item file — refuted, every header-carrying chunk
   reads its exact stated length.
2. LP64 mismatch on the inventory packet — refuted, `SINVENITEM_SAVE` matches
   the size the server sends.
3. `CItemSlot::SetItemImage` bailing on an unknown id — refuted, it is never
   called; the live class is reached through `CInventoryPage`, and
   `LoadItemPage` reported **0 items**, which is correct: the character's
   inventory really is empty.
4. Only then did "empty inventory + slot reads 0/0" point at the constructor.

**Verified:** hovering empty slots no longer requests a tooltip, no
`blow type -1` report, client stable. The bounds guard stays in as defence.

## The caps lie is inert — corrected (2026-08-26)

Earlier notes here said the false `PixelShaderVersion = D3DPS_VERSION(2,0)` was
gating real subsystems (DxCubeMap, DxPostProcess, DxShadowMap, DxEffectMan).
Measured, that is **wrong**, and the correction matters:

* `GetCreationParameters` reports `D3DCREATE_HARDWARE_VERTEXPROCESSING`, not
  MIXED, so `DxEffectMan::InitDeviceObjects` sets `m_bUseMIXED = FALSE`.
* That leaves `m_dwUseSwShader` at its non-zero initial value, and every
  `!dwUseSwShader && PixelShaderVersion >= ...` branch is already off.
* `m_bPixelShader`, `m_bPixelShader_1_4` and `m_bPixelShader_2` are set only
  inside the `m_bUseMIXED` block, so they stay FALSE.
* The one branch with no SwShader guard is `m_bBorder` — and
  `DxEffectMan::IsBorder()` has only commented-out callers.

So the shader-version caps have no live consumer and were left alone. What was
fixed is the one cap that is a real promise we break: `D3DPTADDRESSCAPS_BORDER`
is no longer advertised, because the sampler maps `D3DTADDRESS_BORDER` to
`GL_CLAMP_TO_EDGE` (ES has no border colour).

## Still open after this session

* **Item tooltips never render.** `SHOW_ITEM_INFO` runs and `ShowGroupTop
  (INFO_DISPLAY)` is called, and *skill* tooltips do render — so the machinery
  works and something item-specific fails. Not yet found.
* Sky-coloured geometry gaps — not reproduced at the one camera angle tried.
* Fixed-function gaps: `TCI_CAMERASPACEPOSITION` + `D3DTTFF_PROJECTED`,
  `SetClipPlane`, vertex specular.
* Panel translucency vs the PC — needs a side-by-side with MiniA.
* Gameplay past the inventory: NPC dialogue, shops, trade, quest accept, skill
  use, combat, death, zone change.

## va_list reuse: a real bug, but not the one reported (2026-08-26)

Chasing "the tyranny end notice shows 68 นาที where it should show 1", I found
that `CInnerInterface::PrintConsoleTextDlg` (and 82 other places) format the same
`va_list` two or three times with no `va_copy`:

    va_start(argList, szFormat);
    StringCbVPrintf(szBuffer,  ..., szFormat,   argList);   // consumes it
    strCombine.Format("%s", szFormat);
    StringCbVPrintf(szBuffer2, ..., strCombine, argList);   // reuses it - UB
    va_end(argList);

`szBuffer2` is what gets displayed. On 32-bit MSVC a `va_list` is a bare `char*`
passed by value, so a callee's walk leaves the caller's copy alone and this works
by accident — which is what the code was written against.

**Measured, on device, rather than assumed:**

* x86_64: the two-argument case really does break — "2 hours 30 minutes" comes
  out as "-1 hours 0 minutes". The one-argument case survives.
* **arm64 (Tab S9): nothing breaks.** Not one argument, not two, not a string
  plus an int — with the intermediate `CString::Format` call in place, exactly as
  the real function has it.

So this is a genuine latent bug that is fixed, but it is **not** the cause of the
reported 68. Both call sites of that string pass a literal `1`
(`GLGaeaClient.cpp:3533`, `GLPVPTyrannyClient.cpp:95`), so the value cannot be
wrong at the source. Still unexplained.

**Fixed anyway**, in the shim rather than at 83 call sites: `StringCchVPrintfA`
and `CString::FormatV` now work on a `va_copy` and never consume the caller's
list, restoring the by-value semantics every caller assumes.

## First arm64 verification (2026-08-26)

Everything this session had been checked only on LDPlayer x86_64. On the Tab S9
(SM_X710, arm64-v8a, 2560x1600), booted from the same APK:

* Thai shaping is correct — "< เลือกเซิร์ฟเวอร์ >" renders with ร์ sitting on
  its consonant, not floating. The `ccmp` work holds on the real device.
* Button captions are centred — the "ออก" button matches the PC reference.
* Boots clean, no fatal, no missing-face or atlas-full reports.
* The scene renders markedly better than the emulator (real GPU): foliage,
  sky and terrain all correct.

NOT verified on arm64: anything behind the login — the `%I64d` numbers, the
inventory slot fix, item tooltips. `option.ini` on the device is encrypted so
there is no saved id to read, and guessing credentials risks the server's
auto-ban. Needs the account details, or the user logged in.

## Exploded, flickering characters on the tablet — streaming ring fence (2026-08-26)

**Symptom (Tab S9 only, never on the emulator):** in world, the player character
rendered as huge stretched polygon shards radiating from its position, changing
every frame. Character select was clean.

**Cause.** The streaming vertex/index ring in `gl_render.cpp` guards its wrap
with a single fence, in the wrong order:

    if (cursor + size > capacity) {
        waitForLap();        // fence inserted at the START of this lap
        cursor = 0;
        markLap();           // fence for the next lap
    }

Wrapping reuses memory the GPU may still be reading. The draws that read this
lap were submitted *during* the lap — after the fence being waited on. So the
wait proved only that work from *before* the lap had finished, and the memcpy
then overwrote vertices a queued draw was still sourcing.

Two things hid it:
* The emulator serialises the GPU, so the race cannot express itself.
* It needs enough streaming volume to wrap. Character select streams little and
  was clean; the world streams ~717 KB/frame at 61 fps, wrapping the 16 MB ring
  a few times a second.

**Fix:** fence *after* the lap's last draw, then wait, then reset.

    markLap();
    waitForLap();
    cursor = 0;

One stall per wrap. At ~22 frames per lap that is not measurable.

**Verified on the Tab S9:** character renders correctly in world, and four
frames sampled two seconds apart differ by 1.7-3.3% — idle animation, not the
per-frame churn of the corruption. This was also what the user was describing as
"skin texture overlapping" and "flickering": one bug, not two.

## The dark GUI: gamma ramp is a no-op (diagnosed, not yet fixed)

`GammaControl::Apply` builds a 3x256 ramp from the user's gamma, contrast and
overbright settings and applies it with **GDI** `SetDeviceGammaRamp`. The shim
has:

    inline BOOL SetDeviceGammaRamp(HDC, LPVOID) { return FALSE; }

So every brightness/contrast setting the player chooses does nothing, and the
whole frame — GUI included — renders darker than the PC, which is exactly the
report. (The D3D `IDirect3DDevice9::SetGammaRamp` is a no-op too, but this
client uses the GDI one.)

Fix not yet written. The faithful approach is to capture the ramp and apply it
as the last step of the uber fragment shader via a 256-entry LUT texture, since
every pixel the client draws goes through that shader — which makes it
equivalent to a display ramp without needing an off-screen pass.

## State-block recording leaked onto the live device (2026-08-26)

The cause of the "overlapping texture", the mismatched ground, and much of the
darkness — one bug behind all three.

D3D9 semantics: between `BeginStateBlock()` and `EndStateBlock()`, `SetRenderState`
and friends are **captured into the block and do not touch the device**. The shim
captured *and* applied:

    if (m_recording) m_recording->m_rs.push_back({State, Value});
    m_renderState[State] = Value;          // <- applied as well. Wrong.

So every state block the engine builds at startup permanently leaked its contents
onto the live device, and nothing ever took them back. `DxRenderStates::Init`
alone (`DxRenderStates.cpp:419`) builds its shadow blocks with:

    D3DRS_DEPTHBIAS         -0.0002
    D3DRS_ZWRITEENABLE      FALSE
    D3DRS_LIGHTING          FALSE
    D3DRS_ALPHABLENDENABLE  TRUE
    D3DRS_FOGCOLOR          0
    stage 0 D3DTSS_COLOROP  D3DTOP_SELECTARG1

Every one of those was left applied to the whole scene. In particular:

* **DEPTHBIAS -0.0002** becomes a **-3355 unit** polygon offset. The bias is in
  depth units and `glPolygonOffset` counts smallest-resolvable steps, so the
  conversion multiplies by 2^24 — correct arithmetic applied to a value that
  should never have reached the device. Everything was drawn pulled hard toward
  the camera, so surfaces punched through each other: the overlapping textures.
* **COLOROP = SELECTARG1** on stage 0 selects one argument instead of modulating
  the texture, which is why patches of ground rendered as flat vertex-lit colour
  with no texture — the "ground that mismatch".
* **LIGHTING FALSE / FOGCOLOR 0 / ALPHABLENDENABLE TRUE** account for much of the
  overall darkness.

This is also why implementing `D3DRS_DEPTHBIAS` earlier appeared to fix the
overlap and then appeared to regress: before that work the state was ignored
entirely, so the leak was harmless; implementing it correctly is what gave the
leaked value teeth.

**Fix:** `SetRenderState`, `SetTextureStageState` and `SetSamplerState` now
return immediately after capturing when `m_recording` is set — no device state,
no batch flush, no epoch bump, because nothing actually changes.

**Verified** on the emulator, login background, same crop before and after: the
flat olive ground is gone and the grass texture reaches the bottom of the frame;
the whole scene is markedly brighter and the GUI panel legible.
(`scratchpad/ldgnd.png` before, `scratchpad/sbgnd.png` after.)

---

## Touch controls: the skill arc, and two movement bugs

### Reusing the client's slots instead of drawing new ones

The first cut of the touch pad drew its own skill buttons — ten circles and a
pair of page arrows — and pressed the matching keys. That was wrong twice over:
the drawn circles were blank, so nothing showed which skill was where or whether
it was on cooldown, and the client's own quick-skill tray was still on screen
down the left edge, so there were two sets of slots.

`CSkillTrayTab::MobileArrangeArc` (guarded, `RAN_MOBILE`) moves the real tray
instead. Its ten slots go onto two quarter arcs around the attack button, five
inner and five outer; the overlay keeps only the attack button and reports its
position as fractions of the surface so the client can lay the arcs out in its
own coordinate space.

Because they are still the client's controls they keep their icons, cooldown
sweeps, tooltips and drag-to-assign, and `CBasicSkillTray::TranslateUIMessage`
already runs `ReqSkillRunSet` on `UIMSG_LB_UP` — the same call the number keys
make. Nothing new had to understand skills.

Paging came free as well. Only the current page's tab button is visible at a
time and clicking it advances to the next, so all four stack in one spot beside
the arc and act as both indicator and switch. The page arrows were deleted.

### The stick was dropping a click marker per step

`GLCharacter::ActionMoveTo` fires `NewClickEff` whenever `RANPARAM::bClickEffect`
is set. The stick reaches its destination by issuing a GOTO several times a
second, so walking laid a trail of "you clicked here" markers across the ground —
which is what it looked like: the screen being tapped over and over.

`GLCharacter::SuppressClickEff` is set around the stick's own calls only. Real
clicks still show the marker.

### The stick stopped short of edges and slopes

Two causes, both in how the destination was probed.

`ActionMoveTo` casts a vertical ray from `vFromPt` down to `vTargetPt` to find
the ground under the destination, and returns FALSE — no movement at all — if it
misses. The stick was passing a **±5 unit** window around the character's own
height. Any ground ahead that rose or fell more than that (a ramp, a stair, a
kerb) fell outside the ray and the character stopped for no visible reason. Now
±600.

The second cause is why it showed up worst near edges: aiming a fixed 260 units
ahead means walking toward a wall or the rim of the walkmesh eventually puts the
destination past the edge, where there is no ground at any ray height. The stick
now retries at 55%, 25% and 10% of the reach, so the character slides along the
edge instead of halting a stride short of it.

**Verified on the emulator.** Holding the stick forward walked the character
across a bridge and up a flight of steps in one unbroken move, with no click
marker anywhere on the ground; releasing it returned the idle pose within a
frame. (`out/mv_hold.png`, `out/mv_hold2.png`, `out/mv_rel.png`.)

### Naming the corner controls, and two wasted cycles

Two passes at the corner layout listed the controls by GUID and both silently
missed some. The cause is worth recording: the control census prints the XML
**keyword**, the code needs the **enum** name, they differ, and the
keyword-shaped spelling usually exists too as a neighbouring control:

    census keyword          id    enum name          keyword-as-enum
    PARTY_FINDER_BUTTON     206   FINDER_BUTTON      PARTYFINDER_BUTTON = 208
    AUCTION_ALERT           239   AUCTION_BUTTON     AUCTION_ALERT      = 240

So the wrong name compiles, FindControl returns a real control, and an invisible
one gets moved while the button you meant stays in the corner. Nothing errors.

The fix was to resolve the ids rather than guess: parse `InnerInterfaceGuid.h`
positionally (entries run from `NO_ID + 1`) and cross-reference every name
against the census id before using it. The arrangement now carries those ids in
its comments.

A recursive census dumped *after* the arrangement settled the last one: the
envelope still sitting in the corner belongs to `CItemShopIconMan`, which anchors
an icon over the head of any player with a personal shop open. It was never HUD
furniture, just another player standing there.

Both diagnostics have been removed now the layout is settled.

### Deflection picks the gait

How far the stick is pushed now chooses walk or run, the way a console stick
does: ease it over and the character walks, push it out to the ring and it runs.
The overlay already clamped the stick's magnitude to exactly 1.0 once the knob
reaches the ring, so "at the ring" is a real reachable value rather than
something the player has to feel for.

Two thresholds, not one: run above 0.98, walk below 0.90, and hold whatever the
current state is in between. A single threshold at 1.0 would flip the gait every
frame while a thumb rests on the edge, and every flip is a `SNETPC_MOVESTATE`
the server is told about.

The switch goes through `GLCharacter::ReqToggleRun` rather than setting
`EM_ACT_RUN` directly - that call also flips the game menu's run button, retimes
the pet's movement, and sends the state message. Setting the flag by hand would
desync all three.

**Verified:** at half deflection the character walks upright with a short stride;
pushed to the ring it leans forward, swings wider, and covers several times the
distance in the same five seconds. (`out/gait_walk.png`, `out/gait_run.png`.)

### The rows sit against the compass rose

Right-aligning the two rows to the minimap group's left edge left them stranded
in the middle of the screen. The group is 340 wide but the rose is only the
square at its right-hand end; the rest is empty space that the date and clock are
drawn over.

The rose is square and flush to the group's right edge, so its left edge is one
group-height in from that: `rcMap.left + rcMap.sizeX - rcMap.sizeY`. At 1280x720
the group is 940,0 340x120 and that gives 1160, which is where the rose measures.
Both rows now end hard against it. (`out/arcEtr.png`.)

### The attack button was drinking a potion

Verifying the touch controls turned up a real bug rather than confirming one.
The attack button sent `DIK_Q`, on the assumption that Q was a basic attack. It
is not: `RANPARAM::QuickSlot[0]` is `DIK_Q`, the first **item** slot on the
top-left bar. Every press was using a consumable.

RAN has no attack key at all, because attacking is clicking a target - so there
was nothing to synthesise a keypress for and the whole approach was wrong.

`GLCharacter::MobileAttackNearest` (guarded) does what a click does instead:
picks the target with `FindNearTarget(..., EMFIND_TAR_ENEMY, false)` - the same
call the bot path uses - then runs `MobReaction` or `PvPReaction` with the
`DXKEY_DOWNED|DXKEY_UP` a completed click leaves behind. When the reaction
reports the target is out of reach it hands back a destination, so the same tall
ground probe and click-marker suppression the stick uses apply here too.

`bcontinue` is passed true. On a mouse that is ctrl-click, "keep attacking this
target"; a finger cannot hold a button down while doing anything else, so one tap
engaging until the target dies is the only thing that works on a touch screen.

**Verified:** pressing it puts the character into the attack animation, and two
seconds later it is in a different frame of the swing - it engages and keeps
swinging. (`out/atk1c.png`, `out/atk2c.png`.)

### What the touch controls have actually been tested for

| control | verified | how |
|---|---|---|
| stick, movement | yes | crossed a bridge and steps unbroken |
| stick, stop on release | yes | idle pose returns within a frame |
| stick, walk vs run | yes | stride and distance differ by deflection |
| no click marker | yes | ground clean through every move frame |
| skill slot hit test | yes | slot highlights, tooltip appears |
| skill tray click path | yes | page button cycles F1-F4 and the icons change |
| attack button | yes | attack animation, continuing across frames |
| pinch zoom | **no** | needs two real fingers; `adb input` cannot synthesise it |

Firing an individual skill from a slot was inconclusive rather than verified -
slot 1 is an SP refill at full SP and slot 2 did not fire for game reasons. The
click path through the moved tray is proven by the page button, which is the same
path through the same group.

### Back used to quit the game

`AKEYCODE_BACK` was not in `scanCodeFor`, so the handler returned 0, the system
took the key, and `NativeActivity` finished the activity. Back dropped the
player out of the game instantly, mid-session, with no confirmation - and on a
tablet it is a gesture you hit by accident.

It now maps to Escape, which opens the client's own menu. Quitting is still there
as "ออกจากเกม", but it is a choice rather than a slip. **Verified:** the menu
opens and the process survives. (`out/back1c.png`.)

### One finger, two mouse buttons

Right-click does real work in RAN - it uses or equips an item from the inventory,
clears a quick slot, drives context actions - and a touch screen has no second
button. A long press now stands in for it.

The press cannot be sent on touch-down, because by the time a hold is long enough
to count, a left click has already happened. So it is deferred: a finger that
moves is a drag and presses left as soon as it passes the slop threshold, a
finger that lifts early presses left then releases, and a finger that stays put
presses right at 450ms. The pointer still moves on touch-down, so hover and
tooltips are unaffected.

**Verified:** a long press on quick slot 2 cleared it, which is
`ReqSkillQuickReSet` on `UIMSG_RB_UP`. (`out/rc0c.png`, `out/rc1c.png`.)

### Clicks had to become frame-safe first, and that took three attempts

The UI only turns a press into a click when it sees the button down on one poll
and up on a later one, so both halves have to be visible to a poll. Deferring the
press made that fragile, and the first two attempts each broke the login.

1. **A latch that held the release until the press had been read.** Correct for
   one click, wrong for two: a second press cleared the first's pending release,
   so a double click collapsed into a single long press and picking a server from
   the list stopped working.

2. **An ordered queue, drained inside `GetDeviceState`.** That ties input
   delivery to the client happening to call one particular API. The outer stages
   read the mouse a different way, so the queue never drained there and the
   server list stopped responding to clicks at all.

3. **The same queue, pumped once per frame from the main loop** -
   `RanInput_PumpButtons`. Independent of how any stage reads its input, and
   every down and every up is visible to at least one frame, in order. A double
   click takes four frames, about 130ms at 30fps, well inside the client's
   double-click window.

This also fixes a pre-existing problem rather than just enabling the new one: a
fast tap could always deliver both halves between two polls and be lost. That is
why `adb shell input tap` on a skill slot did nothing and the slots looked
broken, when the hit test had been right the whole time.

**Still missing: camera rotation.** Long-press-drag delivers a right drag, but
that does not rotate the view - the camera was unchanged across the gesture
(`out/rot0c.png`, `out/rot1c.png`), so whatever rotates the RAN camera is not
right-drag and has not been found yet.

## Round controls, a repeating sweep, and auto-target

### The corner sweep had to stop being one-shot

The quest icon kept sitting under the attack button however many names were added
to the move list, because it does not exist when the list is applied. Half the
corner furniture is like that - the quest alarm, the pet and vehicle status
boxes, the booster bar are all created or shown once you are in the world.

`MobileArrangeInterface` now runs on a one-second timer instead of once, and a
bounded rect sweep adopts anything small still sitting in the bottom-right
corner. Bounded matters: an earlier unbounded version swallowed the 231-wide
booster bar and shoved the whole icon row into the middle of the screen, so the
sweep now ignores anything wider than 64. Adopted ids are remembered, because
after the move the control is no longer in the corner for the next sweep to find.

Two related fixes fell out of the same pass:

* **The row is bottom-aligned, not centred.** The two notification buttons are 59
  tall against 35 for the rest, and they draw their icon at the *bottom* of the
  box with the space above reserved for a banner. Centring the boxes left those
  two icons visibly low; aligning the boxes by their bottom edge lines the
  artwork up.
* **Hidden controls no longer reserve space.** Laying one out anyway left a hole
  in the row - which is what put the gap between the quest icon and the rest.

### Round skill slots

The slots are still the client's own square controls; the overlay draws an opaque
rim over each one, thick enough to cover the corners of the square underneath,
which leaves a circular window onto the icon. It has to be an overdraw rather
than a mask because the overlay renders after the client - there is no way to get
anything behind the icon.

Two things went wrong on the way:

* `SetTexture(NULL)` looked like the way to drop the square frame. A control with
  no texture does not draw nothing, it draws an *untextured quad*, so every slot
  became a flat white disc. The frames are back and the rim covers them.
* The rim is wider than the slot, so the arc had to open up to match - sized off
  the slot alone, the rims overlapped. The spacing that matters is the rim's.

The rim also starts slightly inside the slot's half-width. Starting it exactly at
the edge left the square's four sides tangent to the circle and still visible.

Page arrows now sit outboard of the attack button and the tray's own page button
is hidden - a thumb already resting on attack can reach them, which a label off
the far end of the arc could not.

### Auto-target and PK

Two toggles up the right edge, mutually exclusive, lit in their own colour when
on - PK red, because it is the one you do not want left on by accident. Each
re-engages on a half-second timer rather than every frame.

**`FindNearTarget`'s plain overload cannot be used for this, and the attack
button had been getting away with it by luck.** That overload searches around
`m_vBotPos` and measures distance from `m_vBotPos` - the anchor the bot parks
itself on. For a player who is not botting that is a stale point, so the search
happens somewhere the player is not. The skill overload gets it right and uses
`m_vPos`; `MobileFindNearestMob` is that branch without the running-skill
machinery, and `MobileFindNearestPvP` is the same for players, keeping the
`IsPK_TAR` check that decides who may legally be attacked.

**Verified:** auto-target closed on a mob, attacked it, produced damage numbers
and left loot on the ground (`out/auto5c.png`, `out/tsel0c.png`); PK lights red
and switches auto-target off (`out/pk2c.png`); the quest icon is in the row and
all eight icons share a baseline (`out/c2tr.png`); the slots are round and no
longer overlap (`out/c3br.png`).

**Not working: the target name and health panel.** The six `TARGETINFO_*` and
`CROW_TARGET_INFO*` controls are moved to the top centre, but no panel has yet
been seen there after selecting a target - so either those are not the controls
that display it, or something else gates them. Unresolved.

---

# Current state — 2026-08-27, end of the touch-UI session

Written after a long session on the Galaxy Tab S9. Everything below is either
measured on a device or explicitly marked as unverified. Where I got something
wrong, that is recorded too, because the wrong turns cost more than the fixes.

## Verified working

Confirmed by watching the screen or reading a log, not by reasoning:

| thing | how it was confirmed |
|---|---|
| Stick movement, stop on release | crossed a bridge and steps unbroken; idle pose returns within a frame |
| Walk vs run by stick deflection | stride and distance differ; measured over 5s |
| No click marker while steering | ground clean across every movement frame |
| Attack button | attack animation, continuing across frames |
| Target selection sticks | three presses, same mob, killed it; two others untouched |
| Auto-target selects without fighting | mobs adjacent, character idle |
| PK toggle | lights red, switches auto-target off |
| Skill fires once per press | `PRESS` then one `CAST` in the log, 8s with no repeat |
| Pick-up button | box vanished from the ground; later found in the bag |
| Round skill slots, page arrows | on screen, no overlap |
| Corner icons in one bottom-aligned row | eight icons, quest icon included |
| Rows sit against the compass rose | measured `rcMap.left + sizeX - sizeY` = 1160 |
| Back opens the ESC menu | menu appears, process survives |
| Long press = right click | cleared a quick slot (`ReqSkillQuickReSet`) |
| Windows draw over the touch controls | inventory covers the skill ring |
| Close buttons close | tapped the X on อุปกรณ์สวมใส่ |
| Money amount prints | "เก็บเงินได้ '8' เหรียญ" — was "Id" |
| Fullscreen | status bar gone, game's own bar at the top edge |
| Device keyboard rises and stays up | `mInputShown=true` on both ID and Pass |
| Typing reaches the edit box | sent `x x 2 2`, field read `xx22` |

## Broken, with the evidence

### Item and money drops render as blank white quads

Seen on the tablet, in the world, two drops in frame — "ขนม…" and
"น้ำยาเพิ่ม MP (เล็ก)" — both drawn as **white untextured boxes**, with a black
blob above the character. White-untextured is the same signature as a control
drawing with no texture bound, so the suspicion is that the drop object's texture
never reaches the draw. Not yet traced.

### The loading screen never starts

`RanLoad` logged **nothing** while genuinely in the world.
`DxGlobalStage::ChangeStage` calls `StartThreadLOAD` unconditionally, and the
shim's `_beginthreadex` is a real `pthread_create`. The instrumentation added
this session sits *inside* `LoadingThread()`, which is too late to tell apart:

* `ChangeStage` is not on the path that entered the world, or
* `pthread_create` fails and the `E_FAIL` return is ignored.

**Next step:** log in `StartThreadLOAD` itself and on the `pthread_create`
result. One run answers it.

### Camera rotate / zoom "overlap"

Reported, not yet reproduced by me. One real cause was found and fixed - the
overlay left blending enabled while `RanGLR_InvalidateStateCache` memsets the
cache to "blend off", so the next `setBlend(false)` compared 0 with 0 and never
issued the `glDisable`, leaving opaque geometry blending. Whether that was the
whole of it is **unconfirmed**.

### Tapping outside an edit box does not close the keyboard

Cause found, not fixed. `EndEdit` is only called when switching between edit
boxes (`CUIEditBoxMan::StartEDIT`) or on OK/Cancel. Nothing ends editing when a
tap lands outside every box. Invisible on PC; on mobile the keyboard stays up.
Needs a mobile-only "tap outside the active box ends editing" rule.

### ANIPROBE instrumentation is still in the shipping build

Roughly 20 log lines a second from `DxSkinAniControl`, `SAnimation` and
`DxSkinAniManThread` - nine `#ifdef RAN_MOBILE` blocks left over from the
animation work. Each is rate-limited to one line a second, but there are several
per skeleton. Should come out.

## Fixed this session, with the mechanism

These are worth keeping because the mechanism was the surprising part:

* **`ShowCursor` returning a constant hung the game.** `CCursor::SetShowCursor`
  loops `while (nShow > -1) nShow = ShowCursor(FALSE);` against Win32's cursor
  *counter*. The shim returned 0 always, so it spun at 100% until Android killed
  the app. It only bit on camera rotation, because the middle-drag branch is the
  one place that asks for the cursor to be **hidden**.
* **The overlay corrupted the client's VAOs.** `glVertexAttribPointer` records
  into whichever VAO is bound; the overlay had none of its own, so it pointed the
  client's VAO at its two-float buffer. Harmless while the overlay drew last -
  moving it under the interface armed it. Then, with a VAO but no
  `glBindBuffer`, `glBufferSubData` wrote into the client's bound buffer,
  because **the `GL_ARRAY_BUFFER` binding point is not VAO state**.
* **`%Id` is an MSVC format too.** The rewriter only handled `%I64`, and its
  guard tested `strstr(fmt, "I64")`, so `PICKUP_MONEY` was never rewritten.
  See [[msvc-format-specifiers]].
* **`m_sRunSkill` is never cleared after a cast**, so a level-triggered check
  re-fired the skill every frame. It is edge-triggered off `ReqSkillRunSet` now.
* **`FindNearTarget`'s plain overload searches around `m_vBotPos`**, the bot's
  parking anchor - useless for a player who is not botting. Replaced with
  `MobileFindNearestMob` / `MobileFindNearestPvP`, anchored on `m_vPos`.
* **`STARGETID` carries a position** that goes stale; both `MobReaction` and
  `SkillReaction` measure range from it, so every press aimed at where the target
  had been. Refreshed each tick.
* **The shim's `CIMEEdit` had no input path at all** - `g_imeText` was only ever
  written by the client seeding a field. The on-screen keyboard was the only way
  text ever got in, so removing it made login impossible until
  `RanIME_InsertUtf8` / `RanIME_Backspace` were added.
* **`ANativeActivity_showSoftInput` does nothing under NativeActivity** - there
  is no View to focus. Goes through `InputMethodManager` by JNI now. The first
  working version used `toggleSoftInputFromWindow`, which *toggles*: moving from
  ID to Pass dismissed it.

## Still not started

* Thai text input. The keycode table covers ASCII only; Thai needs the composing
  IME, which is the real remainder of that job.
* Camera lock button (follow the character's facing). Free look works.
* Item tooltips do not render.
* Projected shadow texcoords.
* Gameplay sweep past the inventory: NPC dialogue, shops, trade, quest turn-in,
  death, zone change.
* The `68 นาที` number.
* Nothing committed since the GitHub push.

## Process notes — where the time actually went

Most of this session went into the test harness rather than the game, and much of
that was self-inflicted. Recording it so it is not repeated:

* **I broke `login-tab.sh` myself.** It tapped the coordinates of the client's
  on-screen keyboard; removing that keyboard meant those taps hit the *Android*
  keyboard and typed "gguu" into the ID field. It types with key events now, and
  dismisses the IME with Back before pressing OK.
* **`logcat -c` does not reliably clear every buffer**, so waits matched stale
  lines from the previous run and the script tapped character-select before it
  existed. `--pid` looked like the fix and is rejected on this device
  ("pid out of range"); what actually works is confirming the process is dead
  before clearing.
* **`am force-stop` does not always take**, so the log was cleared *after* boot
  and the boot marker never reappeared.
* **I misread my own logs.** I grepped with `-avE "FRAME "` and then counted
  `FRAME sections`, concluded the client had never reached the world, and spent
  several cycles on the login flow while the game was sitting in the world the
  whole time.
* **A patch script corrupted `android_main.cpp`.** `'$'` in a replacement string
  is `$'` - a JavaScript `String.replace` special pattern meaning "everything
  after the match" - which spliced the whole file tail into the middle. Caught in
  the build errors and repaired. Escape `$` or use a function replacement.

The pattern behind all of these: I acted on a plausible theory before measuring.
The fixes that landed cleanly are the ones where I read the mechanism first -
`SetShowCursor`, the VAO binding, `m_sRunSkill`, `m_vBotPos`. See
[[no-guessing-verify-facts]] and [[port-methodology-master-pc-source-first]].

---

# 2026-08-29 — camera separation, and the white drops

Everything here was measured on the LDPlayer emulator (x86_64, 2560x1440) with
the shipping APK, not reasoned about.

## Item and money drops render as blank white quads — FIXED

**Cause: a use-after-free in the shim's `.x` loader.**

`D3DXMATERIAL::pTextureFilename` pointed into the parsed `XFile`'s string
storage, and `loadFromBytes` did `delete file` before returning — so every
texture name was dangling the moment the caller got it:

```cpp
HRESULT hr = meshFromNode ( mesh, ... , ppMaterials, ... );
delete file;                 // "the mesh copied what it needs" — it had not
return hr;
```

Whether it mattered came down to the allocator. Read the freed block soon
enough and the old bytes were still there and the texture loaded; read it once
the block had been reused and `strlen` found a `0`. `DxSimMesh::Create` then
copied that **empty, non-null** string, `LoadTexture` failed, and
`DxSimMesh::RenderItem` — which unlike `Render` has no no-texture guard — drew
the subset with no texture bound. The shader's `tex = vec4(1.0)` fallback
modulated against a white material: a flat white box.

That intermittency is why it looked like a data problem. It is not: the data
is fine. `money_1.x` carries `coin_tex_a.dds`, and `coin_tex_a.dds` is a clean
128x128 DXT1 with 8 mips.

**Fix:** the names now live *inside* the returned material buffer, laid out
after the `D3DXMATERIAL` array — which is what real D3DX does, and why callers
may hold those pointers for as long as they hold the buffer.

**How it was found.** Three steps, each ruling something out:

1. Built the shim's own `xfile_parse.cpp` into a standalone Android binary and
   ran it against the real `money_1.x`. It printed `coin_tex_a.dds`. The parser
   was not the problem.
2. Swept the whole corpus on the device with the same binary — 941 files in
   `data/object` (1598 materials) and 191 in `data/skinobject` (7242). Result:
   `empty=0`, `shortData=0`. 514 and 6249 materials respectively have no
   `TextureFilename` node at all, which is legitimate for effect meshes. **No
   file anywhere produces an empty name.** So an empty name at runtime had to be
   created at runtime.
3. Made the runtime probe name the mesh instead of deduping by texture name —
   the old probe collapsed every failure into one useless `[]` line. One run
   then said it outright:
   `untextured draw: mesh=[Money_1.X] subset=0 of 1 tex=[]`
   The same file that scanned clean offline. That gap is only explicable by
   lifetime, and `delete file` was two calls up.

**Verified:** killed mobs until money and items dropped. Coin pile and treasure
chest render with their real textures; `RanTex` silent across the whole run.
Pick-up then cleared them, and the chat read `เก็บ ขนมปัง ได้`,
`เก็บเงินได้ '6' เหรียญ`, `เก็บ น้ำยาเพิ่ม MP (เล็ก) ได้`.

## Zooming also rotated the camera — FIXED

A pinch is two fingers moving. The gesture layer read that movement as a drag,
and a drag with no control under it presses the **middle** button, which is the
client's camera-rotate binding (`DxViewPort::FrameMoveMAX`, `dwMOUSEKEY_M &
DXKEY_DRAG`). So every zoom turned the view at the same time.

`RanTouch_IsPinching()` now reports the pinch, and the gesture layer refuses to
start a drag during one — and releases a drag already in flight when the second
finger lands, or the rotation continues through the whole gesture.

**Verified with numbers, not pixels.** A temporary probe logged the camera's
own inputs and yaw per frame. Across a full pinch:

```
dx=7 dy=0 dz=120  M=0x1  vRot=(0.0000,0.0000) zoom=3.4560  yaw 1.571 -> 1.571  (d=0.0000)
...
dx=7 dy=0 dz=240  M=0x1  vRot=(0.0000,0.0000) zoom=14.7718 yaw 1.571 -> 1.571  (d=0.0000)
```

Zoom ran 3.4 to 14.8; yaw did not move at all. And the converse, a one-finger
drag:

```
dx=49  dy=0 dz=0  M=0x8  vRot=(0.0000,0.1539) zoom=0.0000  yaw  1.571 -> 1.417
dx=195 dy=0 dz=0  M=0x8  vRot=(0.0000,1.0668) zoom=0.0000  yaw  0.310 -> -0.756
```

Rotates (`M=0x8` is `DXKEY_DRAG`), and `zoom` stays exactly `0.0000`. The two
gestures are now cleanly separated in both directions. The probe has been
removed.

Note for anyone re-testing this: **the compass rose is not a yaw indicator.**
It is perfectly static frame to frame (measured: 0.00 difference over 3s with
no input), which makes it look like a good one, but it also changes on zoom.
Reading it as yaw says "the zoom still rotates" when the yaw number says it
does not. Measure the yaw.

## The loading screen — it was always working

Captured it this session: full art, the map name `< สถาบัน SG >`, the HINT
badge, the spinner and the copyright line, between character select and the
world. `RanLoad` traces the whole path — `ChangeStage entered, to=2`,
`StartThreadLOAD called tex=[loading_054.dds]`, `thread handle = 0x...`,
`context acquired, rendering`. Nothing to fix.

## Test harness

`login-emu.sh` was still tapping the coordinates of the client's own on-screen
keyboard, which no longer exists — the same breakage `login-tab.sh` had. It
types with key events now. Its start-button coordinate was also wrong
(`2473,500`; the button is at `2413,707`), and it waited on a log marker
(`FRAME sections`) that no longer exists, so it "timed out" while sitting
happily at character select.

Two throwaway tools proved useful enough to keep in mind: an Android binary
built straight from `shim/d3d/xfile_parse.cpp` for reading real `.x` files
offline, and `rectdiff`, which compares a rectangle between two raw
`screencap` dumps. Both live in the scratchpad.

## Camera lock button — ADDED

There is nothing to port here: RAN’s camera is free-look only, turned by
dragging the middle button, and it never follows the character. So the lock is a
mobile addition — but expressed in the client’s own terms rather than beside
them. It feeds `CameraRotation` exactly the way a drag does, so zoom, the
collision pull-in and the pitch limits all keep working untouched.

A seventh touch button joins the toggle stack up the right edge, blue so it
reads apart from green auto-target and red PK. While it is on, each frame takes
the signed angle from the camera’s heading to the character’s — through a cross
product, so it lands in (-pi,pi] with no wrap-around case to get wrong — and
eases the camera along it. The easing is frame-rate independent
(`1 - exp(-6*dt)`), because this runs anywhere from 8 to 60 fps on the devices
in hand.

**Verified.** With the lock off the follow code never runs at all (no trace
lines). With it on:

```
camlock: camYaw=1.571 charYaw=0.844 delta=-0.727 step=0.4593
camlock: camYaw=0.844 charYaw=0.844 delta=-0.000 step=0.0001
camlock: camYaw=-0.050 charYaw=-0.052 delta=-0.003 step=0.0014
camlock: camYaw=-0.052 charYaw=-0.052 delta=-0.000 step=0.0000
```

It converges onto the character’s heading, holds there, and re-acquires when the
character turns again. The probe has been removed.

## Full sweep on the shipping APK

One pass with everything on: camera lock, auto-target, 40 attacks, five
pick-ups, a pinch each way and a drag rotate. Result: 0 untextured draws, 0
`FATAL`/`signal 11`, `glErr=0x0000`, 31 fps, process alive. Chat read
`เก็บเงินได้ '8' เหรียญ` and `เก็บ ขนมปัง ได้`.

## Item tooltips — they were never broken

The standing note said item tooltips do not render. They do. Hovering a bread
stack in the inventory brings up the full panel in correct Thai — `ข้อมูลสำคัญ`,
`ชื่อ:ขนมปัง`, `จำนวน:4/999`, `ต้องมีเลเวล:1`, `HP Recovery:60`, the
can-sell / can-drop / can-store list, and the CTRL+Mouse-R chat-link hint.

It persists correctly too: `UIMSG_MOUSEIN` is purely positional
(`CUIControl::MouseUpdate` is a rect test), and the shim leaves the pointer
where the finger left it, so after a tap the tooltip stays up on its own. It was
still on screen after six seconds with nothing touching the display.

What made it look broken was the measurement, twice over. A press held long
enough to take a screenshot crosses the 450 ms long-press threshold, and the
right-click that follows replaces the tooltip — so every attempt to photograph
it destroyed it. And the diagnostic left in `SHOW_ITEM_INFO` was capped at
eight lines, which the press alone exhausted; its silence afterwards read as
"not called" when it was only "not logged". Uncapping it showed the call running
every frame while idle. Both probes are now removed.

## Backspace did nothing in a text box that already had text — FIXED

Reported from the tablet: characters could not be deleted in an edit box.

`CIMEEdit` kept the caret in a file-static `g_imeCaret` in the shim. But
`IMEEdit.h` defines the getter **inline** — `int GetInsertPos() { return
m_xCaretPos; }` — so the client read a member the shim never wrote. It was
always 0.

That 0 did not stay harmless. `CUIEditBox::FrameMove` reads `GetInsertPos()`
into `m_xInsertPos`, and `BeginEdit` pushes it straight back:

```cpp
SET_STRING_BUFFER ( m_strText );   // shim: text set, caret = end
DXInputString::GetInstance().OnInput ();
SetInsertPos();                    // pushes m_xInsertPos (0) back -> caret = 0
```

So focusing a field that already held text pinned the caret to the front, where
backspace correctly has nothing before it to delete and typed characters go in
at the start. An **empty** field worked perfectly, which is exactly why every
earlier test passed — the login boxes were always typed into from empty.

Two changes, both needed:

* The caret now lives in `m_xCaretPos`, the member the inline getter exposes,
  so reads and writes agree. The `RanIME_*` free functions reach it through the
  public accessors on the `CIMEEdit` that last touched the buffer.
* `BeginEdit` puts the caret at the end of the existing text under
  `RAN_MOBILE`. On a desktop the caret starting at 0 is harmless because
  clicking into the text positions it; with a finger and a soft keyboard there is
  no such gesture, so it has to start where a keyboard user expects.

**Verified on the emulator against the exact failing case:** typed `abcd` into
the ID field, moved focus to Pass, tapped back into the now-filled ID field, and
pressed backspace twice. Field read `ab`. Before the fix that deleted nothing.

## Touch UI pass — five items from the tablet

**The item tray collapse arrow is gone.** It was the small arrow alone against
the left edge. Finding it took a control census logged from
`MobileArrangeInterface`: at logical 1280x720 it sits inside id=3
`LEFTTOP_CONTROL_GROUP` (rect `(0,41) 41x415`), whose children are the potion
tray, the level display and `QUICK_POTION_TRAY_OPEN_BUTTON` — the arrow.
`CUILeftTopGroup::Update` now keeps it hidden and the tray open under
`RAN_MOBILE`. Collapsing a tray buys screen back for a mouse user; on a touch
screen it is one more thing to mis-tap, hiding something that has to stay
reachable.

The same census also showed `QUICK_SKILL_TRAY_OPEN_BUTTON` still listed as
visible after `SetVisibleSingle(FALSE)` — `MobileCollectIn` skips anything
invisible, so that flag is plainly not what `IsVisible()` reads. It goes
through `HideGroup` now, which is what the client uses for that button itself.

**The stick is smaller.** `g_stick.radius` is `g_unit * 0.72f` instead of
`g_unit`. The ring is only a hint of where the thumb rests — it re-centres
under the finger anyway — so a big one just covered the world without steering
any better. Its centre did not move, so it stays level with the attack button.

**Both thumb clusters and the chat are off the bottom edge.** The last strip of
the screen belongs to the system: the gesture handle sits there even in
immersive mode, and the band above it is a system gesture inset, so touches are
taken for back/home before the app ever sees them. The chat tabs are the bottom
row of that window, which is exactly what could not be tapped. `layout()` now
subtracts a `g_height * 0.04f` bottom-safe margin, and the chat is placed
`fH * 0.05f` clear of the edge.

**The controls are drawn in the game's own idiom.** RAN's windows and buttons
are a dark, slightly blue charcoal panel behind a thin bright rim, with a darker
line outside it. Every control is built from exactly that now, at whatever
radius it happens to be — one `frame()` helper — so the stick, its knob, the
attack button, the mode toggles, the page arrows and the skill slot rims read as
one set, and as the same set as the MENU button and the window frames beside
them. The glyphs moved into a `glyph()` helper so the lit and unlit paths
cannot drift into drawing different marks, and a lit toggle keeps its frame and
only changes what fills it.

**Verified together on the emulator in one run:** arrow gone from the left edge,
stick visibly smaller, chat clear of the bottom, every control wearing the new
frame.

### Startup loading screen — wired, not yet confirmed

There is no way to draw one from inside the game. `m_pd3dDevice` is still NULL
right through `DxGlobalStage::OneTimeSceneInit`, and the lobby stage is entered
by assigning `m_emThisStage = EM_STAGE_LOBY` directly rather than going through
`ChangeStage`, so `StartThreadLOAD` never runs at startup — confirmed by
`RanLoad` logging nothing at all across a whole boot. The black period is
before any device exists.

So it is an Android window background instead. `loading_002.dds` — the image
`DxGlobalStage` already names for the lobby stage — was decoded to PNG with a
small DXT1 decoder written for the job, and is now
`res/drawable-nodpi/splash.png` behind a `RanSplash` theme; `build-apk.sh`
gained an `aapt2 compile` step for it. The resource is in the APK and the
manifest resolves the theme, but I could not see it on the emulator: the window
is covered there within half a second. It should show on the tablet, where
startup is slower. **Unconfirmed — needs a look on the device.**

## Touch controls restyled to the Claude palette

The engine-matched grey-metal look was rejected. The controls now use Claude's
design language: warm neutrals with a single terracotta accent, spent only where
it carries meaning rather than as decoration.

| control | treatment |
|---|---|
| Attack | solid terracotta `#D97757` — the primary action, the only control wearing the accent by default |
| Stick | warm-black well, cream knob; the knob takes the accent while it is actually being moved |
| Skill slots | warm-black rim with a cream ring |
| Page arrows, pick-up | warm-black discs, cream glyphs |
| Auto-target lit | terracotta |
| PK lit | deep clay `#AA5135`, the one colour that reads as "careful" |
| Camera lock lit | muted slate `#5C7A99` — a view setting, not a combat one |

### The light palette did not survive contact with the game

The first pass used Claude's *light* surfaces: cream fills, warm-grey rims, dark
ink glyphs. Correct on a page, wrong over game footage. On the pale market
pavement the buttons washed out almost entirely — the mode toggles were barely
findable in the screenshot, and a control you cannot see is worse than an ugly
one.

The fix was to take the dark half of the same palette rather than abandon it:
warm-black `#262624` surfaces at 0.66 alpha, with cream promoted from fill to
rim and ink. Same identity, same accent, same hierarchy — but it holds against a
bright street and a dark interior alike, which is the only test that matters
when the background is arbitrary.

Both states were confirmed in one frame: bright pavement and dark brick in the
same screenshot, with auto-target and camera lock switched on so a lit toggle
and an unlit one could be compared side by side.

## Gameplay sweep — first results

Taken on the emulator in one session, with the restyled controls in place.

| thing | result |
|---|---|
| Zone change through a portal | works — market to the SG institute interior, geometry and lighting correct |
| NPC dialogue | renders and responds; the OK button drove the zone change |
| NPC hover tooltip | renders (`รถจิ๋ว [Npc Type]`) |
| Area messages on entry | arrive in chat (tax rate lines) |
| Untextured draws across the whole sweep | 0 |
| Crashes | 0 |

**One thing to look at next:** on zone entry a second, largely empty dark panel
appears across the top centre carrying the area message
(`อัตราภาษีของพื้นที่นี้คือ 5.00`). It is not obviously wrong — it may be the
region notice banner behaving normally — but it is big, mostly empty, and sits
where nothing else does. It was not present before the zone change. Worth
identifying before deciding whether it needs moving like the rest of the corner
furniture.

Still unswept: shops, trade, quest turn-in, death.

## Tablet session — everything below was verified on the Tab S9 itself

The lesson of this round: the emulator is 2560x1440 and the tablet 2560x1600,
and they are **not** the same layout. A fix confirmed on the emulator and
reported as done was still visibly broken on the device. Nothing here is claimed
without a capture or a log line from the tablet.

### Tapping chat killed the game — FIXED

`SIGABRT` in `onAppCmd`, an ART abort, i.e. a JNI error:

```
F com.ran.native: runtime.cc:761] "Signal Catcher" ...
  native: #13 ... ((anonymous namespace)::onAppCmd+432)
F libc: Fatal signal 6 (SIGABRT), code -1 (SI_QUEUE)
```

`goFullscreen` calls `setSystemUiVisibility`, which throws on this device - a
View method off the UI thread. The newly added `WindowInsetsController` block
then called `GetObjectClass` **with that exception still pending**, and ART
aborts the process the moment any JNI function is entered in that state. It
fired on every focus change, which is exactly what raising the soft keyboard
does - so tapping chat killed the game.

Clearing the pending exception between the two blocks fixes it. Verified: open
the chat input, type, send, tap the box - process alive throughout, zero
`Fatal signal`.

### The bars are gone — FIXED

The legacy `setSystemUiVisibility` path never worked here (see above: it
throws). `Window.setDecorFitsSystemWindows(false)` plus
`WindowInsetsController.hide(systemBars())` and
`setSystemBarsBehavior(BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE)` does. The gesture
pill is absent from every capture after the fix. Exceptions are logged now
instead of silently swallowed - swallowing them is why this failed invisibly for
so long.

### The left-edge arrow — FIXED, and it was never a tray collapse

It is `MINIPARTY_OPEN` (id 10), at logical `(0,440) 41x16`. Every earlier
attempt hid the skill tray and potion tray arrows, which is why it survived them
all: it was neither.

Finding it needed a census that could see **child** controls.
`CInnerInterface::MobileCollectIn` walks only the top-level container, and the
arrow is a group child, so it was invisible to every sweep built on it.
`CUIControl::MobileCollectTree` (virtual, overridden in `CUIGroup`) walks the
whole tree; one run on the tablet named the id outright.

Also worth keeping: `SetVisibleSingle(FALSE)` does **not** change what
`IsVisible()` returns - a census taken immediately after that call still listed
the control as visible. Guarding a hide on `IsVisible()` therefore makes it a
no-op. Both hides are unconditional now, every sweep.

### The boot screen — ADDED, then made to match, then made to animate

Three separate problems, in order:

1. **Nothing was drawn at all.** No loading screen exists at startup by design:
   `m_pd3dDevice` is NULL right through `DxGlobalStage::OneTimeSceneInit`, and
   the lobby stage is entered by assigning `m_emThisStage` directly rather than
   through `ChangeStage`, so `StartThreadLOAD` never runs - `RanLoad` logs
   nothing across a whole boot. An Android window background does not work
   either: with a NativeActivity the surface is created and painted black
   immediately, and a capture 0.7s after launch was black with the theme
   correctly linked and the drawable packaged. It is drawn directly on the GL
   surface now, before `RanApp_Boot`.
2. **It looked like a different screen.** It draws the same pieces in the same
   1024x768 virtual layout as `NLOADINGTHREAD`: `ld_top`, the lobby art,
   `ld_under`, the HINT badge and the `ld_back` ring.
3. **The spinner did not move.** `loading_st.dds` is four 105x105 frames along a
   512x128 sheet, indexed exactly as the client does it
   (`left = (step %% 4) * 105`). The first attempt drew all four at once because
   a shader edit had silently failed to apply - `String.replace` does not error
   when the pattern does not match, so the `uUV` uniform never existed and the
   quad sampled the whole sheet.

   It advances on real progress rather than a timer, which is what
   `LOADINGSTEP::SETSTEP` does for the in-game screen. The first attempt ticked
   only between the coarse steps in `RanApp_Boot`, which all finish in the first
   second - measured: the spinner region was pixel-identical between 2s and 5s.
   Ticks inside `DxGlobalStage::OneTimeSceneInit`, where the time actually goes,
   took it from 5 frames to 9 across a 12s boot, with the spinner region
   measurably changing (13.3%% of pixels between 4s and 6s).

### Chat follows the keyboard — ADDED

`windowSoftInputMode=adjustNothing`, deliberately: letting Android resize the
window churns the surface, and the client is not built to be resized mid-frame.
So the chat asks how tall the keyboard is and moves itself, every frame rather
than on the one-second sweep, or it would lurch up a second late.

The height comes back as a **fraction of the window**, not pixels. The first
attempt returned device pixels and scaled by `RanGL_Height()` - which reports
the client's *logical* size, not the panel's - so the two disagreed by the UI
scale factor and the chat flew off the top of the screen. A ratio has no units
to get wrong.

Verified both ways on the tablet: keyboard up, the chat sits directly above it
with its tabs and input line visible; keyboard down, it returns to the bottom.

## Particle effects: RAND_MAX is not 32767 everywhere

`RANDOM_01` in `DxVertexFVF.h` was `((FLOAT)rand())*0.000031f`. That literal is
`1/32767` — MSVC's `RAND_MAX` — and the author's own derivation is still sitting
one line above it, commented out. bionic's `RAND_MAX` is `2^31-1`, so on Android
the expression returned up to ~66000 rather than 0..1.

Every particle built from it came out thousands of times too large: sizes of
20000-85000 units instead of ~2, so a single billboard spanned the whole view.
Drawn additively with a near-black colour, that is the full-screen magenta wash.
Lifetimes were scaled the same way, so `m_fRate = m_fTime/m_fLife` stayed at 0 —
the alpha ramp never rose and particles never expired.

Found by bisecting the frame with `/sdcard/ran/drawlimit` down to draw #118, then
logging quad spans at the point each of the five effect classes writes its
vertices, which named `DxEffCharParticle`. The fog colour, the sky dome, DXT1
decoding and the buffer-upload path were each eliminated by measurement first,
and every one of them was a wrong guess.

Fixed under `RAN_MOBILE` by dividing by the `RAND_MAX` actually in force. The
Tab S9 went from 15-19 fps to ~55: those quads were overdrawing the whole screen
every frame.

## Drawing at panel resolution

How large to lay the GUI out, and how many pixels to draw it with, are two
questions that used to share one answer: the frame buffer was shrunk to 1280x800
and stretched by the display, making everything on screen a 2x nearest-neighbour
blow-up. That was the blur.

Now the buffer is the full panel and `RanGL_UIScale` carries the ratio — the
viewport and scissor paths already multiplied by it, and touch already divides by
`InputScale`, the same number. The client still lays out at 1280x800, so the GUI
stays finger-sized. Measured after the particle fix: 16-19 ms a frame at
2560x1600, so it costs nothing that was not already being wasted.

## Skill press during an attack

`MobileCastRunSkill` cleared the press flag before testing `IsACTION(GLAT_ATTACK)`,
so a skill pressed mid-swing was eaten with nothing to retry it. Tapping attack
repeatedly holds `GLAT_ATTACK` almost continuously, so skills stopped coming out
at all with nothing on cooldown. The press now opens a 0.75 s window, mid-swing
counts as busy rather than refused, and the attack button stands aside while a
skill is queued. Not yet confirmed on device — the test character has no skills
in its quick slots.

## Data on the tablet

`push-data.sh minimal` had left `data/map` at 12 of 450 files and `sounds` at
zero, which is why most maps did not load. Both are now pushed in full (3.06 GB
and 266 MB).

`adb push` needs `MSYS2_ARG_CONV_EXCL='*'` set, or the destination `/sdcard/...`
is rewritten to a Windows path and the push reports success while moving nothing.
That is the opposite of `build-apk.sh`, which breaks when the variable *is* set.

The `.enm` skin misses in the log are harmless: those files are not in the PC
client either.

## Four moons: the second texture coordinate set was never read

`moon.dds` holds four phases in a 2x2 grid. `DxSkyManDayNight` binds the same
texture to stages 0 and 1, and picks tonight's phase by writing quadrant UVs into
`vTex02` — coordinate **set 1**. Stage 0 samples set 0 for colour and stage 1
passes that colour straight through (`SELECTARG2`/`CURRENT`), contributing only
alpha: `MODULATE(TEXTURE, DIFFUSE)` sampled with set 1. That alpha is the mask
that cuts the sheet down to one phase.

The shim only ever read the first coordinate set off a vertex — there was no
second UV attribute at all — so the mask was the whole sheet and all four moons
showed at once.

Added `aUV2`/`vUV2` (attribute 5, fed from the second set when the FVF declares
one, in both the attribute-format and classic layout paths) and a stage-1 mode 4:
alpha comes from the stage 0 texture sampled at set 1. Matched narrowly in
`d3d9_impl` on one texture bound to both stages with exactly that op pair, so it
cannot catch anything else.

Also fixed while in there: `RanGLR_SetStage1` mapped only modes 1 and 2, so mode
3 — the camera-space reflection addressing — silently became 0 and never reached
the shader branch that implements it.

Not yet confirmed on device: the moon only draws at night (`m_fAlpha_Night`), and
in-game time was 07:20.

## Sharpness is a setting

> Later measurement: resolution is NOT what costs frames on the Tab S9 - at
> renderscale 2 it still runs 31-33 fps against ~30 at full res. The setting is
> a taste control only; there is no speed to buy by turning it down. See "What
> the frame rate is actually made of" below.

Full-panel rendering makes geometry and text sharper but magnifies art authored
at 1024x768 further, which can read as softer. Rather than pick for the player,
`/sdcard/ran/renderscale` chooses: 1 draws at the full panel (default), 2 draws
at half and lets the display stretch it, which is what the build did before. The
GUI is laid out at the same size either way, so only sharpness changes.

Verified by launching and reading the boot line, which needs no login:

    renderscale 1 -> drawing 2560x1600, laid out 1280x800 (UI scale 2)
    renderscale 2 -> drawing 1280x800, laid out 1280x800 (UI scale 1)

Measured against the worry that the scene might be rendering into an off-screen
target and being magnified back up regardless: about a quarter of the frame's
draws go into render targets, but the largest is 512x512 (character composition),
not the scene. So drawing at panel resolution does reach the 3D.

## Text: glyphs were rasterised at the size they are laid out, not drawn

The client asks for text in its own logical pixels. With the frame at the full
panel every glyph quad is magnified by `RanGL_UIScale` on the way to the screen,
so rasterising at the logical size and magnifying is what made all text soft.

`d3dx_font.cpp` now rasterises each glyph at `scale * RanGL_UIScale()` and draws
it into a quad of the same logical size as before, which puts the bitmap at
roughly 1:1 with panel pixels. Only the bitmap changed: `advance` still comes
from `TtfFace::Advance` at the logical scale rather than from the oversized
bitmap, because that number is what every label in the interface is measured
with and moving it would move the whole GUI. `w/h/bearing` became floats so the
division does not quantise glyph boxes to whole logical pixels.

The atlas grows with the factor (1024 -> 2048 at 2x) or it fills and later
glyphs draw blank; the client only creates one font object, so that is cheap.

Measured on the server-select title, same text both ways: edge energy 2.48 with
the old pipeline against 3.84 with the new one, and the difference is obvious at
3x zoom. Verified at the login screen, which needs no login.

## Icons: bilinear magnification is what made them mushy

Icons are authored small — an item icon is around 32 texels — and are drawn two
to three times that size on the panel. With plain bilinear sampling almost every
output pixel of them is a blend between two texels, which is exactly what "the
icons look blurry" means. Nothing was being downscaled: every GUI texture was
checked against its file and all upload at full resolution.

The fix is a sharper reconstruction rather than a different resolution. The
interpolation ramp is squeezed into roughly one output pixel, so a texel edge
still ramps where it genuinely falls between output pixels and is flat
everywhere else. Nearest sampling would also be crisp, but it puts hard
stair-steps back — which is the complaint this started from.

Applied in two places, because icons reach the screen by two different routes:

* the client's interface path, in the main fragment shader, gated on
  `uPreTransformed` so world geometry (as often minified as magnified) is left
  alone. Texture sizes are recorded at upload in `g_texDims` and fed to the
  shader per draw, memoised on the texture name.
* the touch overlay's own icon shader, which draws the arc's skill icons and
  never went through the renderer's shader at all. It derives the magnification
  from the actual disc radius and UV span rather than assuming a factor.

`/sdcard/ran/nouisharp` turns the client-path filter off live for comparison.

Two shader traps on the way in, both caught by reading the log rather than the
screen: `uPreTransformed` had to be declared in the fragment stage too, and then
qualified `highp` — the vertex stage defaults an int to highp and the fragment
stage to mediump, and a uniform shared by both stages must agree or the program
will not link.

Measuring this needs care. Total variation across an edge is invariant to how
wide the blur is, so mean edge energy showed nothing; the difference is obvious
at 4x zoom on the item tray, where the slot borders go from soft grey ramps to
clean lines.

## Frame rate in a crowd: characters were being drawn three times

Measured on the emulator with the census the shim prints every 300 frames, in a
populated town:

    per frame: opaque 41 | alpha 22 | skinned 335 | ui 289
      of which off-screen: 140 skinned a frame

**Character drawing is the frame, and 40% of it was off-screen** — every
character rendered again into 512x512 targets. Two passes were doing it:

* **The water reflection.** `DxEnvironment::RenderRefelctChar` is the one place
  every character, pet and summon reflects through. It is also wrong here: the
  pass uses `SetClipPlane` to cut the reflection at the water surface and this
  shim has no clip planes, so what it drew was never clipped to the water. Now
  skipped; `/sdcard/ran/reflectchars` puts it back. Worth ~11 draws a frame -
  the player and pets, since only those reflect.

* **The shadow buffer**, which is the real cost. `DxShadowMap::
  RenderShadowCharMob` is the chokepoint for the player, other players, mobs,
  pets and summons alike, and each caster is a second full pass over the
  character. Now only the first few casters of a frame get a shadow. The client
  renders the player before the crowd, so the player keeps its shadow and the
  crowd gives theirs up.

The budget is read from `/sdcard/ran/shadowcount` (default 6, 0 disables), and
it behaves proportionally — measured at about ten draws per caster:

    cap=1 -> 11 off-screen skinned a frame
    cap=2 -> 14
    cap=4 -> 30
    cap=6 -> 58
    uncapped, same scene, no cap -> grows with every character on screen

In a crowd of thirty that is roughly 300 draws against 58. With character
shadows off entirely the emulator went from 74.0 ms to 60.6 ms a frame in a
scene that was not even busy.

Two caveats. The emulator's GPU is not the tablet's, so the *draw counts* here
transfer and the millisecond figures do not — this needs confirming on the Tab
S9. And `RAN_TIME_DRAWS` still has to be defined at build time before the
"submitting draws" line in the budget means anything, so it is not yet known
whether the remaining cost is submission or fill.

## Text input follows the keyboard layout

Key events were mapped through a hard-coded US-ASCII table, so only the
characters an account name is made of could ever be typed. They now go through
`KeyCharacterMap.get` for the device's actual layout first, and the codepoint is
encoded as UTF-8 for `RanIME_InsertUtf8`, which the edit boxes already take. The
table stays as the fallback for when the platform says nothing.

That covers any layout that sends key events. It is **not** all of Thai input: a
keyboard that composes — which most Thai IMEs do — commits through an
`InputConnection`, and a plain `NativeActivity` has none to commit to. That
needs a Java Activity of our own and a dex step in the build, which this port
does not have (`android:hasCode="false"`, no .java anywhere).

Regression-checked by logging in: the account name and password still type.

## Security: what the APK was shipping

* **`android:debuggable="true"` was in the shipped manifest.** Anyone with the
  APK could `run-as` the package, read everything it stores and attach a
  debugger. Removed; `DEBUGGABLE=1 ./build-apk.sh` puts it back on a temporary
  copy when a debugger is actually wanted. Verified: `run-as` now answers
  `package not debuggable`.
* **`allowBackup` was unset**, so it defaulted to on and `adb backup` could pull
  the app's private data off an unrooted device. Now `false`.
* **Unbounded accumulation in the receive buffer.** `CRcvMsgBuffer::addRcvMsg`
  bounded the single packet at `MAX_PACKET_SIZE` (2048) but never checked the
  running total against `m_pRcvBuffer`, which is 16384. Eight arrivals that have
  not been consumed fill it and the ninth `memcpy` writes past the allocation —
  reachable whenever the client stalls while the server keeps sending. Now
  bounded, dropping the packet the way the caller already handles.

  This one is **not** `RAN_MOBILE`-guarded, unlike the rest of the SOURCE
  changes. It changes MSVC behaviour only in the case that is currently a heap
  overflow, and leaving that in the PC build to preserve byte-identical
  behaviour seemed the wrong trade. Say if you want it guarded.

Noted, not changed:

* The whole data tree and the logs live under `/sdcard`, readable by any app
  with storage access. That is inherent to shipping several GB outside the APK.
* The `/sdcard/ran/*` diagnostic switches let any app with storage access change
  how the client renders. Harmless in itself, but they should be compiled out of
  a build meant for other people.
* 43 raw `strcpy`/`sprintf` calls remain in the client logic against 711 safe
  `StringCch*` ones. None were traced to a network-controlled source in this
  pass; that trace is still to do.
* The game protocol itself is unencrypted, which is how the original works.


## What the frame rate is actually made of — measured on the Tab S9

Two things I had been reporting were wrong, and both are corrected here.

**My own frame-budget line was reading half the truth.** It timed the interval
between `Present` calls and divided by 300 — but the client was calling `Present`
*twice* per rendered frame, so it reported ~18 ms while the game was really
taking ~37 ms. Wall-clock from the client's own counter is what settles it:
300 rendered frames in 11.03 s = **27 fps**, matching the on-screen readout.
Anything earlier in this document quoting a "frame budget" figure is per-swap,
not per-frame.

The double swap came from `CD3DApplication::Present`, which on PC presents the
four regions *around* the GUI rectangle so the GUI area is not copied again.
There are no partial presents here — every one is a full `eglSwapBuffers`, and
on a tiled GPU each resolves and flushes the tile buffer. Now one swap a frame
under `RAN_MOBILE`. Worth 27 -> 30 fps: real, but not the main cost.

**Resolution is not the bottleneck, so the sharpness/speed trade offered earlier
does not exist.** At `renderscale=2`, a quarter of the pixels, the tablet still
runs 31-33 fps against ~30 at full resolution. This is CPU-bound, not fill-bound.
Keep full resolution; it is not costing frames.

Where the time really goes, from the client's own profiler:

    FRAME 30.9 fps | 32.3 ms = update 2.0 + render 18.1 + present 12.1
    FRAME sections: world 12.5ms  w:mobitem 9.6ms  interface 2.7ms
                    w:land 1.2ms  w:chars 0.8ms  world-eff 0.7ms
    FRAME gl calls: 3750/frame = uniform 1536, texture 192, attrib 1240,
                    draw 708, buffer 57
    FRAME draws: 708 per frame, 14 us each

`w:mobitem` — `CLandManClient::Render_MobItem`, which draws every mob, other
player and dropped item — is 9.6 ms of the 12.5 ms of world time. `w:chars`, the
local character, is 0.8 ms. That section *is* the "many mobs and players" case.

And 708 draws at 14 us each is 9.9 ms, which accounts for essentially all of it.
So the cost is **draw submission**, not pixels: about 2.5 GL state calls per draw
(1536 uniform + 1240 attrib for 708 draws) on a driver where each one is not
free. That makes reducing the *number* of draws the right lever, which is what
the shadow cap does, and it is why cutting resolution changes nothing.

Frame times cluster at 30-33 ms, which is two vsync periods: the frame misses
the 16.6 ms budget and drops to half rate. Getting under it means roughly halving
per-frame submission work.

Next, in order of likely return:

* **Batch character pieces.** Every equipment piece and bone-combination
  attribute group is its own `DrawIndexedPrimitive`. Merging groups that share a
  material is the single biggest reduction available in `Render_MobItem`.
* **Cull by distance in `Render_MobItem`** before submitting, not after. A mob
  across the map still costs its draws today.
* **The interface is 399 UI draws a frame** in a busy scene (2.7 ms). The shim
  already batches UI quads; worth checking why so many survive batching.
* Per-draw state: 2.5 GL calls per draw is the multiplier on all of the above.


## The frame rate: measured cause, and why 100+ entities breaks it

Per-entity costs on the Tab S9, from the client's own profiler with counters
added to `Render_MobItem`:

    mi:mob-list 16/frame      mobs the client knows about
    mi:mob-seen 13/frame      mobs that survive the frustum test
    mi:mob-draw 11.9 ms       drawing those 13
    mi:mob-namecast 3.0 ms    the camera-to-entity raycasts for name display
    mi:pc-seen 0/frame        no other players present in this test

    796 draws a frame at ~20 us each

So **one visible character costs about 0.92 ms to draw and 0.23 ms for its name
raycast**, and takes roughly 40 draw calls. That is the number that matters,
because it multiplies:

    200 entities x 0.92 ms  = 184 ms a frame -> about 5 fps
    200 entities x 40 draws = 8000 draws     -> 160 ms of submission alone

A hundred mobs and a hundred players will not fit in a frame as this stands.
That is arithmetic from measured per-entity cost, not a guess.

### What is already correct, and was checked rather than assumed

* **Frustum culling works.** `GLCrowClient::Render` returns early on
  `IsVisibleDetect` and `IsCollisionVolume` before touching the device. The
  outer loop in `Render_MobItem` calls `Render` unconditionally, which looks
  wrong but is not - the cull is inside.
* **Resolution is not the cost.** At `renderscale=2`, a quarter of the pixels,
  the tablet still runs 31-33 fps against ~30. This is submission-bound.
* **`USE_SKINMESH_LOD` is not the answer.** The engine has a character LOD flag,
  commented out in `DxSkinDefine.h`, with distance and "more than ten already
  drawn at high detail" rules already written. But its only four read sites are
  `if (g_dwLOD == 0) ++g_dwHIGHDRAW_NUM;` - it counts, and never selects a
  cheaper mesh. Turning it on would change nothing.

### The fix, and why it has not been done yet

The cost is ~40 draw calls per character, submitted at ~20 us each. Everything
else is downstream of that. Three routes, in order of expected return:

1. **Merge attribute groups that share a material.** A skinned mesh is drawn one
   `DrawIndexedPrimitive` per bone-combination attribute group
   (`DxSkinMesh9_NORMAL.cpp`), and per equipment piece on top. Groups that share
   a material and fit one bone palette can be one draw. This is where the 40
   comes from and where it can most honestly be reduced.
2. **Finish the LOD the engine started.** The selection logic exists; what is
   missing is a cheaper thing to select. A distant character drawn as body only,
   without separate equipment pieces, would cut most of its draws.
3. **Drop the name raycast for distant entities.** 0.23 ms each, purely to
   decide whether a name is occluded. It is the cheapest win and needs no mesh
   work: skip it beyond a distance, or spread it across frames.

This has not been implemented. The measurement was the work of this session and
the refactor in (1) is not something to start without room to verify it - a
half-finished merge of attribute groups would be worse than the current state.

## The crash on entering a map: a bone list read past its own array (fixed 2026-08-30)

Reported three times: entering some maps killed the client. Every tombstone
landed on the same instruction,

    DxSkinMesh9_NORMAL::DrawMeshContainer+452
      ldr x2, [x9, x8, lsl #3]        ; ppBoneMatrixPtrs[iMatrixIndex]

with `x8` - the bone id - holding `0x74786554`, which is `"Text"`. A bone id is a
small index into `ppBoneMatrixPtrs`; that value is a fragment of some other
allocation, so the first two attempts chased the wrong thing:

1. A null `ppBoneMatrixPtrs[i]` (real, guarded, not this crash).
2. A dangling mesh container - a `DxCharPart` borrows `m_pmcMesh` from a shared
   `DxSkinPiece`, and `DeletePiece` frees the piece under everyone else. A live
   container registry did **not** stop the crash, because the piece is deleted
   and reloaded straight back into the same address: the pointer looked alive
   because a new container sat exactly where the old one had.

What settled it was refusing to guess a third time. A check in
`DrawMeshContainer` compared every id in the bone combination against
`pSkinInfo->GetNumBones()` and named the mesh when one did not fit:

    bad bone combination: mesh[Plane01[Mesh]] group 1/5 infl 16 bones 36 id0 0xd

The container is intact - name readable, group count sane - so nothing had been
freed. The bone list itself was wrong, and it is the shim that builds it.
`shim/d3d/d3dx_hierarchy.cpp` prunes a face that names more bones than the
palette holds:

    if (need.size() > kMaxInfluences) {          // 4
        ...
        for (size_t i = 0; i < kMaxPalette; ++i) // 16
            need.push_back(strength[i].second);

The test used the per-vertex influence limit (4) and the copy used the palette
size (16), so every face naming five to fifteen bones went down this path and
then read up to eleven entries past the end of `strength` - whatever followed it
on the heap, written into the bone combination table as bone ids. It faults only
when one of those values lands outside a mapped page, which is why it read as a
map-change bug: a map change is simply when new meshes load.

The fix is the constant and a clamp: prune against `kMaxPalette`, copy
`min(kMaxPalette, strength.size())`, and never write a group's bone ids past its
own row of the table. The guard stays in as a cheap net, and still logs.

Verified on the Tab S9 (xx11, GameMaster Lv150, the prison map, about forty mobs
on screen). Before the fix the guard fired on `Plane01[Mesh]` every frame and the
client died within a minute of entering; after it, two world entries, a four
minute soak and a round trip out to server select produced zero
`bad bone combination` lines and zero entries in `logcat -b crash`.

Two ownership fixes made while chasing this are kept, because both are real
even though neither was the crash:

* `DxSkinPieceContainer::DeletePiece` (and the delete inside `ReleasePiece`) now
  tell every `DxCharPart` and `DxAttBoneData` that borrowed the piece to drop it,
  instead of leaving them pointing at freed memory.
* `CreateMeshContainer` / `DestroyMeshContainer` keep a live-container set that
  `DxCharPart::Render` checks before drawing.

## Frame rate in a crowd: 27 fps -> 48 fps (2026-08-30)

Measured on the Tab S9 at 2560x1600, in the prison map with about forty mobs on
screen and roughly twenty of them visible. Every step below was measured on its
own, and the frame is unchanged pixel for pixel.

    start                          27 fps   35.7 ms   swap 14.3
    dynamic buffers via the ring   37 fps   26.3 ms   swap 12.6
    swap preserved only in loading 39 fps   25.6 ms   swap 11.9
    lighting and fog per vertex    39 fps   25.0 ms   swap 11.3
    specialised shaders            45 fps   21.7 ms   swap  8.5
    pose once a frame              48 fps   20.8 ms   swap  8.5

**The dynamic vertex buffers were stalling.** `DxDynamicVB` is a rolling pool -
it appends with `D3DLOCK_NOOVERWRITE` and starts over with `D3DLOCK_DISCARD` -
and the shim honoured neither, patching a buffer the GPU was still reading. That
cost 10 ms a frame for 295 KB, which is not a copy, it is a stall. The slices now
go into the persistently mapped streaming ring and the draw is pointed at them:
0.1 ms. Two things were tried first and are worse, so do not re-try them: sending
the whole buffer each time (15 MB a frame), and an unsynchronised
`glMapBufferRange`, which costs the same 120 us a call as `glBufferSubData` on
this driver.

**Preserving the swap is only for the loading screen.** The game frame redraws
every pixel; preservation makes a tiled GPU reload the whole colour buffer into
tile memory first, 16 MB at this resolution. It now follows the context handover,
so the loading screen still gets what it needs.

**Lighting and fog belong in the vertex shader.** D3D fixed function computes
both per vertex, so this also matches the PC client - per pixel it was eight
lights, a `pow` and a `distance` for every one of four million pixels.

**One shader per state, not one shader for every state.** This was the big one,
and the measurement that found it is worth keeping: `/sdcard/ran/plainfs` makes
the fragment shader return straight after the texture fetch, and that took the
swap from 13.4 ms to 8.1 ms - while `/sdcard/ran/fsprobe`, which pins any single
feature to its cheap path, changed nothing at all. A long shader costs even when
its branches are not taken, because fewer waves fit on the GPU and there is less
work to hide memory latency behind. So the state that changes the shader's shape
now picks a program compiled with those uniforms as constants. Twenty-two cover
the game frame.

**A character was posed twice a frame.** Every character is rendered once for its
shadow and once for itself, and each pass recomputed the skeleton - 74 us a
character, measured. Worse, `UpdateTime` advances the animation clock, so the
animation was being stepped twice as well. It is now done once a frame per
transform.

Ruled out along the way, so they need not be re-checked: the EGL config is not
multisampled (it is logged at startup now); per-section GPU timer queries
attribute nothing on a tiled GPU, because the fragment work all happens at the
flush; and character shadows, capped at six casters, are worth about 1 ms.

### What is left, and what it would take

The frame is now 20.8 ms: about 8.5 ms of GPU and about 12 ms of CPU. It is no
longer dominated by one thing.

* **60 fps** needs roughly 4 ms off each side. On the GPU that means less fill -
  the honest lever is rendering the world at less than the panel's 2560x1600
  while keeping the interface at full size, which is invisible where it matters
  and would roughly halve the fill. On the CPU it means the client's own scene
  walk (`world` 7.0 ms, `w:mobitem` 3.7 ms at twenty mobs).
* **120 fps** would also need the surface to run at 120 Hz; it is presently
  handed 60, and the panel supports 120.
* **The crowd case still has to be proven.** Twenty visible characters is not a
  hundred, let alone the three hundred an event brings. What the per-character
  numbers say is that posing is the cost that scales, and it is now half what it
  was; the next measurement to take is a real crowd, not another quiet map.

## The loading screen flicker was a second Present (fixed 2026-08-30)

Reported as "the loading page is flickering and does not disappear". It is not
the loading screen at all: the game presents the frame twice, and the second
swap puts up whatever buffer comes next in the chain - which still held the
loading screen, for as long as it took to cycle out.

Finding it took measurement rather than reading, because every intuition about
it was wrong:

* Screenshots taken from the host looked like the world and the loading screen
  alternating, but stills cannot tell a real alternation from a capture
  artefact. Sampling one pixel out of `screencap`'s raw output, twenty-four
  frames in a row, showed it was real: `34 50 58` (the loading art) and
  `38 37 29` (the ground) alternating irregularly, half a minute after the map
  had finished loading.
* It was not the preserved swap. Forced off for the whole run, the alternation
  stayed.
* It was not two threads presenting. The loading thread hands the context over
  cleanly - it presents frames 10118 to 10134, the game presents from 10135 -
  and only one load phase ever starts.
* It was not empty frames: every presented frame had drawn something, and the
  frame report showed a full ~350 draws each time.

Logging every present with its thread and the frame's draw count said it in one
line:

    PRESENT 13191 tid=14877 draws=349
    PRESENT 13192 tid=14877 draws=349   <- 1 ms later, nothing new drawn

`CD3DApplication::Render3DEnvironment` calls `Render()` and then `Present()`,
and `RanMobileApp::Render` - the override - was also presenting at its end. Two
swaps for every frame drawn. Removing the one in `Render` is the fix.

It was also the single most expensive thing in the frame, because each swap
resolves and flushes the whole tile buffer:

    before   45 fps   21.7 ms   swap 8.5 ms
    after    86 fps   11.5 ms   swap 1.3 ms

Verified on the Tab S9 in the same crowded prison map: twenty raw-frame samples
after the fix contain no loading-screen pixel at all, and the frame report reads
84-87 fps at 2560x1600 with about thirty mobs on screen.

## The crashes were a stray write, found with trapping bounds checks (2026-08-30)

Three crashes in unrelated places - `RanTexture::Release`, `DxSkinAniMan::DoInterimClean`,
and the font glyph cache - all faulted on a pointer of the same shape:

    fault addr 0xb400007000000000
    fault addr 0x0000007000000008

A live heap pointer here looks like `0xb400007X_XXXXXXXX`. These have the top
half intact and the low half zeroed, which is not a random value: it is a
four-byte zero written **onto** a pointer. One stray write, three victims.

### Getting a tool onto the device

AddressSanitizer was the obvious instrument and it does not work here.
Its runtime has to be loaded before the first allocation, which on a non-rooted
device means `wrap.sh` inside a debuggable APK - and this tablet never runs it
(a marker written from the script never appeared). Loaded late, as a dependency
of libran.so, ASan SIGILLs inside its own `AsanInitInternal`. `setprop
wrap.com.ran.native` is refused on a user build.

What did work: **trapping bounds checks**, `BOUNDS=1 ./build.sh`. Those need no
runtime and no wrap script - the compiler plants a trap at the offending access,
so the tombstone points straight at the line. They only see arrays whose size
the compiler knows, which is exactly the shape of the bug being hunted.

Four traps fired, one after another, all before the game even reached the world:

1. **`CRijndael::Initialize`** - `m_Ke` is declared `[MAX_ROUNDS]` and the key
   schedule fills and reads `m_Ke[0]` to `m_Ke[m_iROUNDS]` inclusive, which is 14
   for this client's 32-byte key. The last round key was written one row past the
   array, onto `m_Kd[0]`. Contained inside the object, but a 32-byte overrun on
   every encrypted file the client opens.

2. **`SlangFilter::addSlang`** - `sizeof` used as a character count on a
   `wchar_t` array. `wchar_t` is four bytes here and two on Windows, so
   `sizeof(buf)` is 1028 for a 257-character buffer: `_snwprintf` was told it
   could write 1027 characters, and the terminator went to `buf[1027]` - a
   four-byte zero written 770 characters past a stack buffer, for every word in
   the slang list at startup.

3. **`CPartyFinderSlot`** - `m_pClassImg[GLCI_NUM_6CLASS]` (12) filled to
   `GLCI_NUM_7CLASS` (14): two pointers written past the array, over the party
   data behind it, for every slot and on every device reset.

4. **`CInventoryPage::ResetAllItemSlotRender`** - and this is the one.
   `m_pItemSlotArray` holds `EM_INVENSIZE_Y` slots (eleven, since the row count
   was bumped from ten) and both callers still say `ResetAllItemSlotRender(10,20)`,
   a literal left from when the count was the twenty of the *dummy* array. The
   loop walked nine entries past the end, read whatever was there as a
   `CItemSlot*` and called `SetVisibleSingle(FALSE)` on it - a virtual call
   through a wild pointer, which then writes a four-byte zero at an arbitrary
   address. That is the corruption, and that is its signature.

   It is a family, not one site: ten classes carry their own `m_pItemSlotArray`
   with their own callers passing 20 and 50. All 41 call sites are now clamped
   inside the walkers, so no caller can do it again.

A fifth came out of the release build afterwards, in the `.x` loader: a string
member is stored as a pointer inside the node's byte blob, and `stringMember`
read one at an assumed offset. A node packed any other way - a different
exporter, an extra leading array - had eight bytes of float data handed to
`strlen`. The parser now records which offsets hold string pointers and the
reader refuses any other, so a layout it does not recognise loses a name instead
of crashing.

### Verified

Under the bounds build: startup, world entry, the whole HUD button row, the item
shop, storage, and a return to server select and back - no traps.

On the release build: three world entries and a two-minute soak, no entries in
`logcat -b crash`, 105-120 fps.

## Still open

### 1. Confirm the frame-rate work on the tablet — measured, partly

Measured on the Tab S9 in a quiet spot (about five characters visible), at
2560x1600 with the shipped defaults:

    uncapped              12 off-screen skinned a frame   20.6 ms
    capped at 6           42                              21.7 ms
    no character shadows   0                              17.1 ms

So character shadows cost roughly 3.5-4.5 ms a frame for about five casters -
call it 0.7-0.9 ms each, on the real GPU. Uncapped in a crowd of thirty that is
in the region of 25 ms of shadows alone, which is the reported lag; the cap
holds it near 5 ms however many characters are on screen.

What is *not* yet shown is the crowd case on the tablet: the spot tested had
fewer than the cap, so capped and uncapped are the same there (and the small
difference above is the scene moving between samples, not the change). Take one
reading in a busy town to close this properly, and tune the cap against it.

Still worth doing: build once with RAN_TIME_DRAWS defined to find out whether
what remains is draw submission or fill, which decides whether batching
character pieces (one draw per bone-combination attribute group today) is the
next thing worth attempting.

The draw-count reductions are measured and proportional, but on the emulator.
Confirm on the Tab S9 with a real crowd, and tune `/sdcard/ran/shadowcount`
against it. Build once with `RAN_TIME_DRAWS` defined to find out whether what is
left is draw submission or fill, which decides whether batching character pieces
(one draw per bone-combination attribute group today) is worth doing next.

### 2. Security: the parts not yet looked at

* Trace the 43 raw `strcpy`/`sprintf` in the client logic to see whether any
  takes a server-supplied string.
* The file parsers reached through `/sdcard` data: the DDS/TGA/BMP decoders, the
  .x reader and the .rcc extractor. They parse files a user can replace.
* Compile the `/sdcard/ran/*` switches out of a distribution build.

### 3. Confirm every function in the game works

Not started. Overlaps with the sweep below.

### 4. Thai text input: the composing IME

The layout half is done (above). What remains is a Java Activity with an
`InputConnection` so a composing keyboard has somewhere to commit to, plus
javac/d8 in `build-apk.sh` and `android:hasCode="true"`.

### 5. Gameplay sweep past the inventory

Deliberately not attempted this session: it needs many trips into the world and
the server drops a session on every reconnect.

Walk each on the tablet, in this order, logging what breaks rather than fixing
as you go:

* **NPC dialogue** — page through, take and decline a branch.
* **Shops** — buy, sell, the quantity prompt.
* **Trade** — offer, change it, both confirm, cancel midway.
* **Quest turn-in** — accept, track, complete, hand in, reward pick.
* **Death** — the prompt, resurrect in town and on the spot.
* **Zone change** — a portal and a teleport card; watches the loading screen
  hand the EGL context over and back.

### 6. Smaller things

* **Projected shadow texcoords** — `TCI_CAMERASPACEPOSITION` with
  `D3DTTFF_PROJECTED` is not implemented in the fixed-function translation.
* **`SetClipPlane` is not implemented** — which is why the character reflection
  was never clipped to the water. Implementing it would let reflections come
  back, if they are ever worth the draws.
* **The `68 นาที` number** — a duration that does not match the PC client.
* **Skill press during an attack is unverified** — the test character has no
  skills slotted. Slot one, spam attack, then press it.
* **The moon is unverified** — the four-phase fix only shows at night.

## The dead mob kept its name and health bar (2026-08-30)

Reported as "the target mob is delay to disappear the name the hp after I kill
them so the target is not change". Two separate defects looked like one:

**The drop itself was on time.** A temporary log in `MobileTargetTick` printed
what the target looked like at the moment it was dropped:

    target dropped: id 346 copy yes hp 2 dying 1

`dying 1` is `IsACTION(GLAT_DIE)` — the target is released the frame the death
action starts, which is the earliest the client knows. Note `hp 2`, not 0: the
server announces the death as an action and the last damage packet never brings
the bar to zero, so anything keyed on `HP == 0` would never have fired at all.

**Nothing hid the display.** Dropping the target only stops `SetTargetInfo`
being called each frame; the groups it had shown stay up. The client's own path
never shows this because it re-picks from the cursor every frame and the pick
simply stops matching — with a finger there is no pick to stop.

There are *two* groups, and hiding one leaves the other:

* `CROW_TARGET_INFO` — the fixed panel above the touch pad
  (`ResetTargetInfoCrow` / `…Npc` / `…Player`).
* `TARGETINFO_DISPLAY` — the name and health bar drawn over the target's own
  head (`ResetTargetInfo` / `…Npc` / `…Player`), which is the one actually seen
  sitting on a corpse.

`MobileTargetTick` now calls all six when the target stops being live.

**Measured on the Tab S9** (`out/k3.png` … `out/k5.png`): with a mob targeted the
panel reads `Lv.167 ไอ้ค้อนใหญ่ (นักโทษ) 30000/30000 (100%)` and a red bar sits
over its head; at `2/30000` both are still up; three seconds later, with the
loot on the ground, both are gone and no bar is left on the corpse.

## "Classic Name" did nothing: two defects (2026-08-30)

**The touch pad ate the window's buttons.** `RanTouch_PointerDown` claimed any
press landing on one of its own buttons without asking what else was on screen.
The client's windows are movable and several open into the lower right - the
options window does - so their buttons sit under the pad. Measured: the tick
flipped (`opt flip: classic=1`) but `GamePlayOption_OK` never ran, and dragging
the window's title bar out of the corner only flipped the pad's skill page to
`4/4`. `CUIMan::IsPointInControl(x,y)` (new, mobile-only; walks top/focus/bottom
in draw order for a visible control covering the point) is exported as
`RanUI_PointInControl` and consulted before the pad claims a press. This unblocks
every window whose buttons land under the pad, not just the options one.

**The plate never followed the option.** `CNameDisplay::SetName` is the only
place that shows or hides `m_pNameLineBox`, and it runs when a name display is
handed out - so turning the option on left every name already on screen without
a plate. Measured at render time: `box vis=0` on every display seconds after
`opt OK: classic=1`. `CNameDisplay::Render` now tracks the flag directly.

Verified on the Tab S9: after ticking Classic Name and pressing ตกลง the window
closes and every mob name gains the dark plate (`out/f2c.png`); the plate art is
black at alpha 177 in `interface_main.dds` at (315,460), which is what the PC
draws too.

### Still open: touch item interaction

The PC item model is mouse-shaped: 27 distinct item gestures, four of them
behind ALT/CTRL and so unreachable on a tablet (preview, box contents, chat
link, buy-without-confirm). Worse, carrying an item makes a stray tap on the
terrain drop it on the ground with no confirm, and a long press in the world
cancels a trade offer. Design written up in `MOBILE/ITEM-TOUCH-PLAN.md`: a per-slot
action sheet, a touch count sheet for split/buy/sell, bigger slot hit areas.
Not implemented - waiting on four decisions listed at the end of that file.

## Touch item interaction, phases 1-5 and 9 (2026-08-31)

Built to `MOBILE/ITEM-TOUCH-PLAN.md` and verified on LDPlayer (the tablet's
wireless adb drops mid-session). **LDPlayer runs `lib/x86_64/libran.so`**, so
every emulator test needs `ABI=x86_64 ./build.sh` as well - an hour went into
probes that were compiled into the arm64 library while the emulator ran the
previous day's x86_64 one.

**Phase 1 - carrying is visible and cannot lose an item.**
`GLCharacter.cpp`'s world-click drop is compiled out on mobile: a tap on the
ground no longer throws the carried item away, and a long press out in the world
no longer cancels a trade offer (both were reachable by accident). `CItemMove`
parks the carried icon at the top centre instead of following the pointer -
snap, which normally parks it on the hovered slot, is a hover effect and hid the
icon inside the bag grid, so a full hand looked like an empty one. Tapping that
icon calls the new `GLCharacter::MobilePutHeldBack`, which finds a free cell the
way an unequip does. Measured: an item stuck in `SLOT_HOLD` from an earlier
session was invisible until this landed.

**Phases 2-3 - the action sheet.** `CMobileItemSheet` (new, mobile-only) lists
what can be done to the tapped item: equip/use, move, split, preview, box
contents, chat link, enhance, drop, close. Rows come from the client's own
tests (`sSuitOp.emSuit` for wearable, `IsInvenSplitItem`, `PreviewItemCheckSimple`,
`ITEM_BOX`/`sRandomBox`), and each row calls the same `Req*` the PC calls. Bag,
worn gear, the quick tray and storage route into it; a full hand still places,
swaps and splits on release, and a drag still lifts an item.

**Phase 5 - the count sheet.** `CMobileCountSheet` replaces the number modal for
split (`-1 / +1 / 1/2 / All`, then `ReqInvenSplit`). Verified: a 600 stack split
599 off, server accepted.

**Phase 9 - the enhance window.** `CMobileEnhanceWindow`: pick target, pick
material, press อัพเกรด. It does the carrying itself - `ReqInvenTo` to fill the
hand, wait for the server, `ReqInvenDrug` on the target, then empty the hand
whatever happened - which is exactly the PC's carry-and-right-click, with no
carry state exposed to the player. The picker reuses the action sheet rather
than drawing a second list. Verified end to end with a cleanser.

Text lives in `gameword.xml` (`MOBILE_ITEM_SHEET`, `MOBILE_ENHANCE`) and
`gameintext.xml` (`MOBILE_DROP_CONFIRM`), so it translates with the rest -
which meant writing `MOBILE/tools/rcc-extract/rcc-pack.js`, since the GUI ships
inside `Gui.rcc` and the loose XML is unreachable on device.

**Still open on this piece:** shop and trade grids still use the PC path (both
already confirm, so nothing is silently destructive); mix/rebuild/transfer/
garbage work through the sheet's "move" row; the latched tooltip with a close
button is not built (the pointer stays parked on the tapped slot, so the tooltip
already behaves, and the sheet suppresses it while open). Slot hit-padding from
the plan is **dropped**: cells in these grids are adjacent, so padding one cell
only steals from its neighbour - bigger targets need a different cell layout,
which is a data change.

### The touch panels now wear the client's own skin (2026-08-31)

First pass built them out of the combo-box back and a text list, which read as
bolted on. Rebuilt against what the client already ships:

* **Item sheet** - the ESC menu's frame (`CreateBaseBoxESCMenu`) with one
  `SIZE22` text button per row, shadowed font and all, because that menu is
  exactly this menu with different rows.
* **Count sheet and enhance window** - real `CUIWindowEx` windows built with
  `CreateBaseWindowLightGray`, so they have the game's title bar, its close
  button, its frame and its buttons. Their control rects live in
  `uiinnercfg02.xml` like every other window's.

Two engine rules came out of it, both now in memory:

1. **A derived window must start its control ids at `ET_CONTROL_NEXT`.**
   `CUIWindow` numbers title/focus-title/close/body from `NO_ID+1`, so ids
   starting at 1 collide with the frame: the colliding controls never draw, and
   the container's destructor later double-frees one - a SIGSEGV in `je_free`
   from `~CBasicTextBox`, and a bogus pointer in the font path on the loading
   thread. Three of the four steppers were invisible until this was found.
2. **A window lays its children out from their LOCAL rects every time it moves**,
   so the layout has to be the local rect - a global position holds only until
   the next move.

### Touch item UX, second pass (2026-08-31)

Four changes after seeing it in use, each verified on the emulator:

* **The panel opens beside the item's own detail**, not on top of it and not
  under the thumb: `MobileOpenItemSheet` reads `INFO_DISPLAY`'s rect and puts
  the panel to its left, or its right when there is no room. The tooltip is no
  longer suppressed while the panel is up - the two are read together.
* **The panel wears the tooltip's skin.** `CreateBaseBoxVarTextBox`, the same
  frame `CBasicVarTextBox` builds itself from, so the pair share a background
  and an outline. Stretching it is `AlignSubControl` + `SetLocalPos`, not a
  ratio resize - the ratio drifted as the row count changed and left the frame
  short of its own rows.
* **Moving is hold, drag, release** - and the item follows the finger, because
  the lift now happens on the DOWN edge of the long press while the finger is
  still on the glass. Lifting on the release instead meant the icon only
  appeared once the finger had gone. `CItemMove` is back on the client's own
  path (follow the pointer, honour snap); the parked top-centre icon is gone.
* **The enhance window has real slots.** Two `CItemImage` slots show what is in
  them; dropping a carried item on one puts it there, and with the window open
  the item sheet grows an **ใส่** row that sends a bag cell straight into the
  slot it belongs in.

Also: the first row now reads **ใช้งาน** rather than สวมใส่.

### Upgrade row honesty, and Auto Potion start/stop (2026-08-31)

**The upgrade row appeared on items that cannot be upgraded.** The rule was
"any suit, or any material", which is far looser than the client's. Each
material's real target test is now mirrored from the request that enforces it:

| material | target must be |
|---|---|
| grinding stone | a suit with `sSuitOp.wReModelNum > 0` |
| cleanser | something carrying a disguise (`nidDISGUISE`) |
| disjunction | not a wrapped item |
| random-option card | a suit with `sRandomOpt.bPVPItem` |
| non-drop card | a suit that is not GM-generated |
| skill reform card | a held weapon that has a skill |
| wrapper | something not already exchangeable |
| disguise card | a suit that is not itself a disguise |

and the row is offered only when the bag actually holds a material that takes
this item (or the item is itself a material) - `CMobileEnhanceWindow::
CanEnhanceCell`. What is deliberately left to the client to say are the checks
that depend on state rather than the pair: grade caps, stone counts, class
match, cost. Verified: a potion no longer offers the row.

**Auto Potion now starts and stops.** OK/Cancel are gone on mobile: **เริ่ม**
saves the three thresholds and turns auto-potting on, **หยุด** turns it off,
and the window stays open because the next thing anyone does after stopping is
change a threshold and start again. The X still closes it. Measured: the
client's own `Auto-pots start` / `Auto-pots stop` lines appear in the console.

### Icon strip: duplicates out, mini-party back (2026-08-31)

The corner has two rows: standalone buttons on top, the collapsible MENU strip
below. Quest and item shop existed in both, so the strip's copies are gone on
mobile - `CBasicGameMenu::CreateSubControl` hides `MENU_QUEST_BUTTON` and
`MENU_ITEMSHOP_BUTTON`, slides the eight icons to their right along by 25 px
each and shortens the strip by 50, so there are no holes and the frame ends
where the icons do. The outer pair is what remains, and the quest one there
still blinks when something is waiting, which the strip's never did.

`MINIPARTY_OPEN` is no longer force-hidden: it is one of the PC client's own
icons and it now sits in the top row with the rest instead of as a stray tab
against the left edge.

Identifying which control was which took a temporary id probe in the row
layout, because the ids in the comments come from an old census and do not
match the enum. Both probes are removed. Verified on the emulator: the strip
lost exactly two icons, nothing else moved, and tapping the shifted icons opens
the window that now sits there.

### Windows would not move, and the mini-party panel now opens middle-left (2026-08-31)

Dragging a window by its title bar did nothing on touch. The cause was in the
input shim, not the UI: a pointer position is applied the instant it arrives,
while button events wait in a queue that drains one per frame. On a drag the
press was therefore delivered a frame or more after the finger had moved on, so
`CUIWindow::TranslateUIMessage` saw a press at a point outside the title and
never started the drag - and the faster the drag, the further outside it was.

Each queued button event now carries the position it happened at
(`shim/platform/dinput_mobile.cpp`). Draining one applies its position for that
frame and hands the real position back on the next pump - a straight assignment
with no delta and no `DIMOFS` event, because the motion between the two points
was already accumulated when it arrived; counting it twice would have spun the
camera. `RanInput_PointerMove` writes to whichever of the two the borrow leaves
live, and `RanInput_WarpPointer` (the client pinning the cursor) cancels a
borrow outright.

Two smaller fixes went with it: `CMobileEnhanceWindow::Update` was calling
`CUIGroup::Update` and so skipped the drag branch that lives in
`CUIWindow::Update`; and the touch layer chose its drag button from
`RanUI_MouseInControl()`, a stale hover flag, instead of a fresh
`RanUI_PointInControl` hit test at the point the finger went down - over a
window that made the drag a camera drag.

The mini-party panel is placed against the middle of the left edge on first
appearance (`DxGameStage::MobileArrangeInterface`). Its own place is the top
left, where the health, level and experience bars already are. Placement happens
once per appearance, so a panel the player drags elsewhere stays there.

Verified on the emulator (screenshots `native/out/k2.png`, `k6.png`, `k7.png`):
the inventory window and the upgrade window both follow a title drag, and the
mini-party panel opens centred on the left edge. Probes removed, both ABIs
rebuilt, `out/ran-phase3.apk` repacked.

### The action sheet: detail stays up, no move row, honest use row (2026-08-31)

Three things were wrong with the sheet a tap opens.

**The item detail vanished behind it.** `SHOW_ITEM_INFO` refuses to draw while
the pointer is inside a control, which is a hover rule - and the sheet is under
the finger that opened it, so the detail the player is deciding from was thrown
away the moment the rows appeared. `CInnerInterface::MobilePinItemInfo` is the
sheet's own hover: called every frame the sheet is open, it steps around that one
rule and pins the box at a fixed place instead of at the pointer, so tapping a row
cannot drag the detail (and with it the panel) out from under the finger. The box
only knows its size once it has been laid out, so the pin clamps against the live
rect each frame, and `CMobileItemSheet::PlaceAt` keeps the panel beside it as it
settles. `MobileUnpinItemInfo` restores the cursor gap on close, or the next
hover would draw its tooltip wherever the sheet left it.

**ย้าย is gone.** A long press already lifts the item and carries it; the row was a
second way to do what the finger does.

**ใช้งาน is offered only when something would happen.** It was gated on the suit
switch alone - and `SUIT_HEADGEAR` is zero, so every card, potion and megaphone
in the bag read as a hat and got an equip row that failed silently. The real gate
is `CHECKSLOT_ITEM`, which refuses anything that is not one of seven item types
before it looks at the suit; `IsWearable` now runs that test first. Beside it,
`IsUsable` mirrors `ReqInvenDrug`'s own type switch (a cure only counts with a
drug effect on it), and `IsStorageUsable` mirrors `ReqStorageDrug`, which takes
only a drink, a skill book and a pet skill book. One label for both branches, since
an item is either put on or used up, never both.

Verified on the emulator: sword ใช้งาน/อัพเกรด/ลิงก์ในแชท/ทิ้ง/ปิด with its detail
beside the panel (`native/out/q2.png`), megaphone แยก/ลิงก์ในแชท/ปิด with no use row
(`q1.png`), recall card ใช้งาน (`r1.png`), talisman and skill book both keeping
theirs. Both ABIs rebuilt, `out/ran-phase3.apk` repacked.

### The use row now agrees with the line above it (2026-08-31)

An item whose detail said [ไม่สามารถใช้ได้] - gear for another class, or a sword
past this character's Dex - still carried a ใช้งาน row. Type and suit were the only
tests; the requirements were not checked at all.

Both paths are gated now, each by its own test, which are not the same test:
equipping runs `ACCEPT_ITEM` (class, school, brightness, level, stats, skill) -
that is the very call whose result prints the red line in the detail, so the row
and the line can no longer disagree - and using runs `SIMPLE_CHECK_ITEM` (class,
school, level), which is what `ReqInvenDrug` and `ReqStorageDrug` check before
they do anything. A worn item's disguise id picks the two-argument form, as
`ReqInvenToWear` does.

Verified on the emulator: BaiYou Sword, detail [ไม่สามารถใช้ได้], rows อัพเกรด /
ลิงก์ในแชท / ทิ้ง / ปิด with no use row (`native/out/s1.png`); recall card and
talisman, both [สามารถใช้ได้], keep theirs (`s2.png`, `s3.png`).

### A hold on another player opens their menu (2026-08-31)

Shift + left click is how the PC opens the P2P menu on another player - trade,
whisper, party, club, friend, view gear. There is no Shift on a tablet, so that
whole menu was unreachable.

The 450 ms hold is now that gesture. In `GLCharacter::FindActionTarget`,
`bODER_P2P` - which is what makes the function return a player as
`EMACTAR_PC_P2P` (someone to deal with) rather than `EMACTAR_PC_PVP` (someone to
hit) - is set by the right mouse button on mobile, which is what the hold
produces. The state is tested with `DOWNED|UP|DUP`, not `DOWNED` alone: the frame
the menu opens on is the frame the finger lifts, and by then the button is no
longer held.

The reaction runs **ahead of** the skill branch in `PlayerUpdate`, not as another
arm after it. The same right button casts a skill, and a skill is nearly always in
hand, so as the last arm the hold never ran once - it reached the skill branch,
which has nothing to do with a P2P target, and stopped there.

One regression prevented while testing: the mobile target latch (which stores what
a click landed on so the attack button and the skill arc know what to fight) would
have stored the P2P actor, and neither `MobileAttackNearest` nor
`MobileSkillAtTarget` accepts one - so a hold on an enemy would have left the
player unable to attack or cast until they tapped again. The latch now skips
`EMACTAR_PC_P2P`.

Verified on the emulator: hold on test02 opens the six-icon P2P menu titled with
their name (`native/out/v1.png`); a plain tap still selects them, red HP bar and
all (`v2.png`). The menu's own buttons work through touch - whisper put `@test02`
into the chat box and printed the whisper hint (`w1c.png`). The magnifier (view
character) does nothing, on mobile and on the PC alike: `RequestCharacterInfo`
returns immediately unless `RANPARAM::bFeatureViewCharInfo` is set, and it
defaults FALSE and is off in this config. Both ABIs rebuilt, `out/ran-phase3.apk`
repacked.

### The touch overlay is Gunmetal now (2026-08-31)

The on-screen controls were warm cream with a terracotta accent - the one pale
warm thing on a screen full of the client's own dark steel windows, so they read
as bolted on. They are now the same gunmetal, lit from the top left, with colour
reserved for state: amber when an action is available, cyan when a system is on,
crimson for PK.

**The renderer change everything rides on: a per-vertex colour attribute.** The
overlay carried one flat colour per draw and faked a gradient by stacking up to
thirty filled fans. That was the banding on the stick, the visible polygon
corners on every disc, and most of the draw calls. With `aColor` a gradient is
one fan and a feathered edge is a ring of transparent vertices, so it is better
looking *and* cheaper. On top of that: an additive pass for blooms, a dark halo
under each control, a gloss over the face, a catchlight on the top edge, and
`drawArcFade` - an arc whose alpha ramps to nothing at both ends, which is what
removes the hard notch where the lit and shadowed halves of a bevel meet.

**The glyphs are painted, and measured rather than drawn from memory.** A sword
in steel with a lit edge and gold furniture; the same sword crossed for PK; a
wooden chest with iron straps for loot. All three were rasterised from their
references on game-icons.net and measured:

- lorc's *broadsword*: -45 degrees from vertical, length/width 2.46, guard/blade
  5.8 - about twice as slender as the one drawn by eye, which had looked like a
  toy.
- delapouite's *chest*: the lid is a flat-topped trapezoid at 36% of the height,
  not a dome at 25%; four vertical gaps at +/-0.52 and +/-0.78 are where the
  bands go; the lock plate is large and straddles the seam with the keyhole cut
  through it.

Painted art cannot also carry state colour - a crimson sword is not a sword - so
state moved outward to the chrome, and the art only ever dims.

**Also changed:** the stick's well went from a near-opaque black disc to 30% with
eight ticks and a heading wedge on the rim, so the world shows through and the
control finally shows direction; empty skill slots keep a dimmed frame instead of
being stripped to nothing, which had read as holes where buttons should be.

**Two process notes**, both of which cost several rounds before they stuck:

1. *Judge at true size.* Every glyph that looked right blown up failed at the
   size it is actually drawn - a hand became an arrow, an arrow's shaft vanished,
   a chest became a house. The preview renders every candidate twice, and only
   the small row decides anything.
2. *Measure the reference, do not copy it by eye.* Checking the chest's aspect
   ratio passed (1.29 against 1.37) while every internal proportion was wrong.
   Extracting the mask and reading coordinates took two minutes and settled what
   several rounds of taste could not.

Tooling that came out of it and is worth keeping, in the session scratchpad:
`svglib.js` (SVG path parser plus mask rasteriser), `measure.js` (principal axis
and width profile of a reference icon), `art.js` (shaded glyph preview at true
button size), `svgview.js` (rasterise reference SVGs to look at).

Verified on the emulator: `native/out/hud3_r.png`. Both ABIs rebuilt,
`out/ran-phase3.apk` repacked.

**Still open on this piece:** auto-loot on a hold of the loot button
(`GLCharacter::m_bAutoLoot` already exists and is only ever set by the
auto-pilot); the attack button dimming when nothing is targeted; the skill
ready-flash and the radial cooldown sweep; and the page arrows are still 76 px
against a 92 px minimum touch target.

### Three dead toggles: a window nobody can see was eating the presses (2026-08-31)

The auto-target, PK and camera-lock buttons did nothing. A probe in
`RanTouch_PointerDown` logging any press the client's hit test swallowed showed
it was exactly those three and nothing else:

    padeat: slot -4 at 1188,439      auto
    padeat: slot -5 at 1188,368      PK
    padeat: slot -7 at 1188,298      camera lock

A second probe inside `PointInList` named the culprit:

    padblock: id 114 rect 1080,200 200x300

Id 114 is `GENDER_CHANGE_WINDOW` - parked at (1080,200) 200x300, drawing
nothing, with its visible flag set. It sits directly over the pad's mode-toggle
column, so those three buttons had been dead since the day the hit test was
added (in this session, for the Classic Name fix - the pad used to eat presses
meant for the client's own windows, and this was the cure).

`CUIMan::IsPointInControl` filtered on `IsVisible()`, which this window passes.
It now filters on `IsNoRender()` instead: **a control the player cannot see must
not take a press away from one they can.** The loot button and the page arrows
were never affected, which is why the failure looked arbitrary.

Verified: the probe logs nothing, and all three toggles light - camera lock cyan,
PK crimson, auto correctly switching itself off when PK comes on, which is the
client's own mutual exclusion. Both probes removed.

**Also in this pass**

- **The page readout is gunmetal.** It was the client's own cream chamfered
  plate, which was right while the overlay was cream and was the one bright
  rectangle on the screen once it was not. Dark chamfer, steel rim, and the page
  number in amber because the page you are on is a state, and amber is what
  state is drawn in everywhere else on the pad. Verified stepping 1 -> 3 with
  the arrows (`native/out/pg2c.png`).
- **Attack and loot moved inboard**, from `g_unit * 1.35` to `g_unit * 1.68`
  from the right edge. The arrows and mode toggles stack *outboard* of the
  attack button, so measuring the inset to the attack button alone put it right
  of the middle of its own cluster and it read as shoved into the corner.

- **Attack and loot moved inboard again**, `g_unit * 1.68` -> `g_unit * 2.00`. The skill
  arc follows, because the client derives it from `RanTouch_GetAttackCircle`.
- **The page plate lost its "/ 4".** The total never changes, so it was a
  constant occupying a third of the plate to say nothing, and it kept the
  figure small. The page number now has the whole well and is set larger
  (`0.68` of the well height, up from `0.52`). Verified on the emulator:
  `native/out/hud7_c.png`.
- **The page plate was overlapping the attack ring**, by 2.7 logical px - 5 on
  device. The arrow column was positioned relative to `attackX`, so pulling the
  attack button inboard dragged the column in with it, and the plate is wider
  than the arrows (`0.46` of a module against `0.34`) so the plate is what
  reached back and collided. The column is now anchored to the screen edge
  (`g_width - g_unit * 0.42`), which decouples it: the action buttons can move
  without it following. Measured clear by 79 logical px, 19 px right margin.
  Screenshot `native/out/hud8_c.png`.

### How far the attack button can actually move: 1.48 modules (2026-08-31)

Pulling the attack button inboard was pushing the skill arc over the chat panel.
The client hangs the arc off this button, so the button cannot be placed on its
own - and the numbers were guessed twice before being measured.

Measured off a running build rather than assumed:

- a skill slot is **41 logical px**, not the 33 an old comment implies. Taken
  from the rendered rim: it is 26.5 px in radius and the overlay draws it at
  `1.30` of the slot half-width.
- so `fInner = 159.0`, `fOuter = 232.6`, rim `26.7` - the arc reaches
  **259 px left of the attack centre**.
- the chat panel's right edge is at **x = 862**, found by walking a brightness
  profile across row 1260 of a screenshot until the panel's step up to the world
  appears.

That gives a hard ceiling of **1.48 modules** from the right edge. Settled at
`g_unit * 1.45`, which leaves 13 px between the outermost slot and the chat.

Both intermediate values were already wrong when they shipped: 1.68 overlapped by
11 px and 1.80 by 23, which is why this only surfaced as "the skill slot overlaps
the chat" after two rounds of moving it.

**This is most of the room there is.** Going further in means narrowing the chat
panel or tightening the arc, and the arc is near its own minimum already:
neighbours need 1.6 slot widths between centres, spacing along a quarter arc is
`0.393 * fInner`, and the inner radius only just provides it.

Verified on the emulator: `native/out/edge2.png`, a crop of the boundary itself
rather than the whole screen.

## The mobile patcher (2026-08-31)

The PC client is patched by a third-party incremental updater over
`http://143.14.11.244:1521/launcher/`, which publishes 2,300 `.ken` and 194
`.eiei` files plus an encrypted `incupdate.idx`. None of that is reusable on
Android: the index format is opaque, and the payload is the *loose* file tree
while mobile reads `.rcc` packs. Mobile gets its own subtree,
`/launcher_mobile/`, published from the same host.

### What ships

`MOBILE/tools/patch/make-manifest.js` builds the payload:

    manifest.json          version, minApk, one entry per file (path/size/sha256)
    blobs/<sha256>         content-addressed, immutable, append-only

Content addressing means uploads only ever ADD, so there is no cache to
invalidate and no window where a client can fetch a half-replaced file;
rollback is republishing an older manifest. It also deduped 246 byte-identical
files for free.

**8,263 files, 1,679.6 MB** - against the 5.7 GB currently on the test device.
The difference is loose copies of already-packed content plus 352 MB of
`.bak_pre_*` files that `push-data.sh` carried across because it copies whole
directories. The generator ships from an explicit allowlist instead.

### Working out what actually ships, and one bug it caught

Verified in both directions rather than assumed:

- `skin`, `piece`, `object`, `skeleton`, `help` are in no pack (only 79 of
  skin's 2,883 files appear in SkinObject.rcc), so they ship loose - 722 MB.
- every loose file inside a packed directory *is* in its pack, so excluding
  those copies is safe. The one apparent exception,
  `_bowstring_ready - +_+.egp`, is in the pack under a mojibake name - an
  encoding mismatch in the comparison, not a gap.

That check missed something anyway: it only looked at files directly inside each
directory and never recursed. `data/glogic` has four **subdirectories** -
`quest`, `npctalk`, `level`, `activity` - plus `data/effect/char`, none of them
in any pack. **1,968 files, 54 MB**, and losing them is silent: quests and NPC
dialogue simply stop working.

The fix is structural. The allowlist was derived from `CLIENT/`, a development
tree where inclusion means nothing; `Ran/` is a *working install* and therefore
ground truth. `--verify` now walks the shipped PC client and reports anything it
has that the manifest does not:

    verify: every file under .../Ran/data is in the manifest.

`data/glogicserver` was dropped for the same reason: the shipped PC client has
no such directory.

**End to end:** with only the manifest's files present (1,738 MB) the client
boots, logs in, and renders world, characters, NPCs and mobs. The 392
"file not found" lines in logcat are probes for random-option tables like
`contri_rv.bin` that do not exist in the PC client either.

### The launcher

The APK was `hasCode="false"` - pure NativeActivity, no Java at all. It now has
a launcher Activity, `com.ran.launcher.RanLauncher`, which patches and then
starts the game. `javac` and `d8` run from the same SDK as everything else; no
Gradle. **This is also the layer the Thai composing IME needs.**

Boot flow, with the one ordering rule that makes it crash-safe:

    fetch manifest -> compare to /sdcard/ran/.patchver
                   -> reconcile against .patchindex, hashing only what moved
                   -> download to .tmp, verify sha256, rename over
                   -> rewrite .patchindex
                   -> write .patchver LAST
                   -> start NativeActivity

Killed part way through, `.patchver` still names the old version, so the next
launch reconciles again and finishes.

Measured on the emulator: a full reconcile of 8,263 files takes ~10 s, and a
second launch with the version already current takes **35 ms** to check and
**378 ms** to reach the game. Without `.patchindex` every launch would hash
1.7 GB.

Four bugs found by running it, three of them mine:

1. `package com.ran.native` does not compile - **`native` is a Java reserved
   word**. The class lives in `com.ran.launcher`; the application id is
   unchanged, because that is an Android identifier rather than a Java one.
2. `Intent(Context, Class)` builds a ComponentName immediately, so passing a
   null class throws before `setComponent` can replace it.
3. `onCreate` started the patch thread without claiming the guard `onResume`
   checks, so both ran and the game was launched twice.
4. **Android 9+ refuses cleartext HTTP**, and the patch host is plain `http://`.
   Every fetch failed and the launcher fell through to "could not reach the
   patch server" - which would have happened to every player. Fixed with a
   network security config scoped to that host rather than
   `usesCleartextTraffic` application-wide.

`/sdcard/ran/.patchbase` overrides the base URL when present, so a patch can be
tested against a local server over `adb reverse` without rebuilding the APK.

### Still open on this piece

- **The patch host is plain HTTP on a bare IP.** A patcher trusts what it
  downloads, and over cleartext an attacker on the same network can substitute
  the payload *and* the manifest, so hash checking does not help. The box
  already has OpenSSL. A hostname also matters: if `143.14.11.244` changes,
  every installed client is orphaned and needs a new APK to find the new address.
- **Back up `native/android/debug.keystore`.** It is gitignored and exists only
  on one machine, and `build-apk.sh` silently regenerates a *different* key if
  it goes missing - after which no update will install over an existing app.
- Apache 2.4.41 / OpenSSL 1.0.2s / PHP 7.1.33 on the patch host are all
  end-of-life since 2019.
- `minApk` is 1 and `versionCode` is 1; give the APK a real numbering scheme
  before relying on the out-of-date gate.

---

## Text-format `.x` files were parsed with two whole features missing (2026-09-03)

**Symptom.** `กล่อง PVP รางวัล` - the PVP reward chest NPC - did not appear at all.
Other pieces (wings, bikes, some costumes, several weapons) had the same shape of
failure. The device log gave the chain in three steps:

```
RanPiece: skin mesh s_gbox_01.x (skeleton b_gbox_01.X) failed to load
RanSkin : s_gbox_01.x: SetupBoneMatrixPointers failed 0x80004005
RanSkin : s_gbox_01.x: bone 0/1 "" not in skeleton b_gbox_01.x
```

The bone name came back **empty**, so `DxBoneCollector::FindBone` could not match
it and the whole piece was dropped.

**Cause 1 - string members were invisible.** `xfile_parse.cpp` has two parsers.
The binary one records, for every string member it writes, the byte offset at
which the pointer lands, in `XNode::stringOffsets`. The text one never did.
`stringMember()` in `d3dx_hierarchy.cpp` refuses to dereference an offset the
parser did not declare - deliberately, because a blob whose members pack
differently would otherwise hand eight bytes of float data to `strlen`. The
result was that **every string member of every text-format `.x` read as absent**:
bone names in `SkinWeights`, filenames in `TextureFilename`. 133 text-format
skins in `data/skin` were affected. One line in the text parser's string branch
fixes it.

**Cause 2 - `{ Name }` references were never resolved.** With the bones fixed the
chest rendered, but flat. A text export writes each material once at file scope
and points every user at it:

```
Material Material__5376 { ... TextureFilename { "G_Box_base.dds"; } }
...
MeshMaterialList { 1; 32; 0,0,...; { Material__5376 } }
```

Both parsers emitted those `{ Name }` children as bare `__reference` nodes and
nothing ever bound them - `XNode::reference` was a declared-but-never-assigned
field. So `MeshMaterialList` looked like it had no `Material` child at all.
`XFile_Parse` now runs a `resolveReferences()` pass once the whole file is in
(the target may not be parsed yet at the point the reference is read), and
`XNode_Deref()` in the header is what any child walk has to step through.

**Cause 3 - `DeclData` was not read.** The chest was textured after that, but
still drew as one flat colour. `s_gbox_01.x` ships **no `MeshNormals` and no
`MeshTextureCoords`**: its per-vertex normals and UVs live in a `DeclData` block,
a `D3DVERTEXELEMENT9` array followed by one packed record per vertex. Nothing in
the shim read it, so the mesh had no UVs and every pixel sampled texel 0 - which
looks exactly like a missing texture, not like missing UVs. `meshFromNode` now
falls back to `DeclData` for whichever of normals/UVs the named blocks did not
supply. **27 text-format `.x` under `data/skin` and `data/object` have `DeclData`
and no `MeshTextureCoords`.**

**The lesson worth keeping.** All three were the same shape: a text-format `.x`
feature the binary parser had and the text parser did not. Only ~130 of the
several thousand skins are text format, which is why this survived so long, and
why the symptom looked like content damage - a missing NPC, a white mesh, a flat
mesh - rather than a parser gap.

**Verified on device** (LDPlayer x86_64, 2026-09-03): the chest renders with its
`g_box_base.dds` metal-chest texture; `RanD3D: texture 3086 = g_box_base.dds
(512x512, 2 levels)` appears and no `RanXH: ... has no TextureFilename` line
does. Screenshot `MOBILE/native/out/gbox_zoom.png`.

**Still open:** `s_m_bs_leg.X` still fails to load on that device. The manifest
ships it again (it was dropped by an earlier over-broad pack-dedup rule), but the
emulator cannot reach the patch host, so its data is still the pre-fix copy.
Expected to clear on the next successful patch.

## The patch page appeared to vanish and be replaced by a loading screen (2026-09-03)

Both activities use `@style/RanSplash`, whose `android:windowBackground` was
`@drawable/splash` - the bare 1024x512 art, **without the RAN mark**. A window
background is drawn before either activity's own views are, and a drawable there
can only be *stretched* to fill: there is no CENTER_CROP for a drawable. So the
boot ran

    splash.png stretched 2:1 -> 16:9, no mark      (RanLauncher window background)
    ran_loading cover-fit + mark + status band     (RanLauncher's own views)
    splash.png stretched again, mark gone          (RanActivity window background)
    ran_loading cover-fit + mark                   (the native GL splash)

The mark disappearing and the photo jumping crop, twice, is what read as "the
patch page disappears and then a loading page shows".

`MOBILE/tools/rcc-extract/compose-splash.js` now composes `splash.png` as the
cover-fit result *plus* the mark at 16:9, so stretching it to fill is a no-op on
a 16:9 panel and a couple of percent on 16:10. All four frames are then the same
picture.

**Measured, not asserted.** A throwaway build with a 2.5 s sleep before
`setContentView` exposed the window-background frame on its own for capture
(`wbg_1.png`); on a normal boot the frames after the launcher's page are
byte-identical to it (`md5 12a9009a...`, v_03 through v_05). The activity could
not simply be started directly to capture it - `am start` on RanActivity is
refused, `not exported from uid 10074`, which is the export hardening working.

Note for anyone repeating this: there is no ffmpeg on this machine and
`screenrecord` has no raw-frame output, so `screencap` bursts are the only frame
source, and they are far too slow to catch a sub-second transition by luck. Slow
the app down and capture deliberately instead.

### Correction: the flash was a black frame, not a mismatched picture

The composed `splash.png` above was the right thing to do but it was **not** the
reported bug, and claiming it was fixed on one sampled boot was wrong - a
`screencap` burst samples about every 400 ms and cannot see an 80 ms event.

Captured properly (raw `screencap` in a device-side loop at 640x360, ~10 fps,
90 frames per boot), the boot is:

    launcher page   mean 126 / 12% dark
    BLACK           mean   0 / 100% dark      <- one frame, every boot
    boot art        mean 189 /  0% dark

and logcat gives its length:

    +0 ms    START RanActivity
    +20 ms   Displayed RanActivity        <- starting window handed back
    +72 ms   GLES renderer ready
    +99 ms   RanSplash: boot screen up    <- first pixels on the surface

**~80 ms of an opaque window with nothing drawn in it.** Three fixes were tried
and measured, and two of them did nothing:

- `windowBackground` on RanActivity - belongs to the starting window, which is
  exactly what has already been taken away. 5/5 boots still black.
- a full-screen `ImageView` added in `onCreate` - a NativeActivity window is
  rendered by the native side through `ANativeWindow`, not by the View
  hierarchy, so it never reaches the surface. "Displayed" stayed at +20 ms.
  5/5 boots still black.
- the platform SplashScreen API's `setOnExitAnimationListener`, to hold the
  splash until told - the Android 12+ splash screen only appears on a cold start
  from the launcher icon, never on an activity-to-activity switch inside the
  app, so the listener never fires. 5/5 boots still black.

What works is to stop the window being opaque before it has anything in it.
RanActivity's theme is `windowIsTranslucent`, so the launcher underneath shows
through for those 80 ms; RanLauncher no longer finishes at `startActivity` (nor
in `onStop`, which a translucent activity on top does not even trigger) but
waits to be dismissed. On its first present the native side calls
`RanAndroid_BootScreenUp` -> `RanActivity.ranBootScreenUp`, which puts the
window back to `PixelFormat.OPAQUE` - a translucent window would otherwise make
SurfaceFlinger blend every frame for the whole session - and dismisses the
launcher.

**Measured after:** 0 black frames in 8 boots (5 at 640x360, 3 at 1280x720),
against 5/5 black before. In-world afterwards: 34 fps at the same spot that gave
32 fps earlier the same day, so the translucent window costs nothing measurable
on the emulator. Not yet checked on the Tab S9 - it was offline.

**Unattributed:** one SIGSEGV during a login run, in
`RanTexture::LockRect` (`d3d9_impl.cpp:238`) reached from
`RanD3DXFont::glyphFor` while `CInventoryUI_TradeInven::CreateSubControl`
measured text. It did not reproduce on the next run and nothing changed in that
path, but it is not explained.

### Correction again: the translucent window was wrong, and unnecessary

The previous entry is superseded. It was arrived at by trying mechanisms and
measuring each one, instead of reading how this codebase's window and blending
actually work first. Two things in our own code rule the approach out:

- **`setBlend` uses `glBlendFunc`, not `glBlendFuncSeparate`**, and nothing masks
  the alpha channel (`shim/gl/gl_render.cpp:696`). `SRC_ALPHA/ONE_MINUS_SRC_ALPHA`
  therefore blends alpha as well as colour, so every semi-transparent panel -
  chat box, tooltips, the name plates - drives destination alpha below 1. The
  client clears with `D3DCOLOR_XRGB`, which is `D3DCOLOR_ARGB(0xff,...)`, so the
  frame *starts* opaque and is eroded from there. Against a translucent window
  those pixels composite as see-through. That would have shipped.
- **The shim cannot survive a window-surface recreation**, which is what
  `setFormat(PixelFormat.OPAQUE)` does. `RanGL_Init` opens with
  `if (g_ready) return 1;` (`shim/gl/gl_context.cpp:97`) and `APP_CMD_TERM_WINDOW`
  only sets `st->ready = false` (`platform/android/android_main.cpp:864`) - no
  `eglDestroySurface`, no re-create. A second `APP_CMD_INIT_WINDOW` does nothing
  and the stale EGLSurface stays bound to a dead window. That is the 550 ms of
  nothing measured on the Tab S9 after `setFormat`, which was misread at the
  time as an animation.

And there was nothing to cover. On the Tab S9:

    Displayed RanActivity   02:17:13.229
    boot screen up          02:17:13.241     <- 12 ms

Twelve milliseconds, under one frame. The 80 ms hole is the emulator's EGL init
(51 ms of it), not the hardware players run on.

So all of it is reverted - translucency, `windowDisablePreview`, the Java cover
view, the SplashScreen hold, `setFormat`, the deferred launcher dismissal and the
`RanAndroid_BootScreenUp` callback. What remains is the one change the evidence
supports: **the composed `splash.png`**. The build the tablet was actually
running (versionCode 18, V001) drew the old asset - the bare 2:1 art, no RAN
mark, stretched to fill - for RanActivity's starting window, so a different
picture appeared between the patch page and the boot art.

**Measured after the revert, on the Tab S9:** 4/4 boots go
home (54) -> patch page (176) -> boot art (189) -> login (81), with no dark frame
and no dip.

**Method note.** `screencap` bursts over adb sample ~every 400 ms and cannot see
an 80 ms event; one clean burst is not evidence. Capture with a device-side raw
`screencap` loop (8-10 fps) writing to `/data/local/tmp`, repeat at least three
times, and read the mean-luminance trace rather than eyeballing single frames.

### The boot screen is now the patch page itself

Requested: show the patch page, and when patching finishes go straight to the
login page - no separate loading screen. The ~2.5 s of `RanApp_Boot` cannot be
removed, only covered, and it was being covered by a bare art screen with no
band, which is the "loading page" that should not exist.

Rather than rebuild the band and its text in GL - before the client has a font,
and destined to drift from the launcher's layout - **RanLauncher rasterises its
own view hierarchy** just before `startActivity` and leaves it in
`<root>/cache/bootcover.bin` (`"RANC"`, width, height, then RGBA). `RanSplash`
draws that verbatim over the whole surface, and falls back to composing art +
mark if the file is not there (game started without the launcher, or the write
failed). Half resolution, so ~4 MB and a sub-frame write; `cache/` because the
patch manifest does not list it, so a patch never fights over the file.

The launcher also had to take the same immersive flags the game sets in
`goFullscreen`: laid out inside the navigation bar its page is 1568 px tall
against the surface's 1600, and the handover stretched it by 2%.

**Measured on the Tab S9, 3 boots:** home (54) -> patch page (179) -> boot
screen (180) -> login (81). Patch page against boot screen, pixel for pixel:
mean absolute difference 2.5 of 765, with 1.0% of pixels differing by more than
60 - all of it text antialiasing from the half-resolution capture. Before this,
the boot screen read 189 against the page's 176.

In world afterwards on the Tab S9: 120 fps, and the PVP reward chest renders
with its texture.

## The chat resize grip was unhittable, not broken (2026-09-03)

`CHAT_LEFT_BAR_TOP` - the little arrow at the chat's top-left - is what resizes
the chat: it posts `UIMSG_MOUSEIN_LEFTBAR_DRAG`, and `CBasicChat::Update` then
moves the window's top edge to follow the pointer. It is **19x15 layout units**.
The client lays out at half the panel width, so on a Tab S9 that is 38x30
physical pixels, about 18x14dp against the 48dp a finger needs - and it has to
be dragged, not tapped.

The drag was never broken. `adb shell input swipe` from the middle of the grip
expands the chat exactly as on PC, verified before changing anything.

`CUIControl::SetTouchPad` (RAN_MOBILE only) widens just the rectangle
`MouseUpdate` tests, leaving the artwork alone. It defaults to zero, so every
other control keeps the identical test.

**Both gates have to be widened.** Padding the grip alone looked like it did
nothing. The probe said why:

    from inside the old rect:  LEFT_BAR drag msg=0x01000042 mousein=1  -> 22 drag frames
    from the padded zone:      LEFT_BAR drag msg=0x01000082 mousein=0  ->  0 drag frames

The grip posted the message and `CBasicChat::TranslateUIMessage` dropped it,
because that case also requires the **LEFT_BAR group** to report `UIMSG_MOUSEIN`
and the group is the same 19 units wide. Padding both to 18 units gives a
55x51-unit target, a little over 48dp each way.

Downwards it reaches y=33 inside the left bar and the chat-state buttons there
start at y=41, so nothing else loses a tap - confirmed by tapping the chat-mode
button afterwards and watching the icon still cycle.

**Measured on LDPlayer**, collapsed mean luminance 69 against expanded 33:

    from (875,1145) right+below:  70 -> 32   expands
    from (845,1160) below:        73 -> 32   expands
    from (890,1115) right:        76 -> 33   expands
    from (875,1075) above:        70 -> 44   expands
    from (808,1115) left:         71 -> 103  no - that is off the chat window
                                             entirely and the tap goes to the world

**Not yet checked on the Tab S9** - it dropped off adb again before this could be
run there.

## Rebranded to Ran Legacy M (2026-09-03)

One square logo (1254x1254 JPEG, gold artwork flattened onto black) drives the
launcher icon, the patch page, the boot screen and the client's own login mark.

**Keying.** The source has no alpha and the logo's own metal is nearly black in
places, so a luminance threshold punches holes through the middle of it. The
black is removed by **flood-filling inwards from the border** instead - only
black connected to the edge is background. 45% of the image comes out
transparent and the internal darks survive. `tools/rcc-extract/make-icons.js`.

**Where it went.**

- `res/mipmap-{m,h,xh,xxh,xxxh}dpi/ic_launcher.png` - the legacy icon, logo
  composited back onto its black square.
- `res/mipmap-*/ic_launcher_foreground.png` + `mipmap-anydpi-v26/ic_launcher.xml`
  - adaptive, artwork inside the 66-of-108 safe zone over a black background
  layer, so a launcher can mask it to any shape without clipping.
- `res/drawable-nodpi/ran_mark.png` - the mark on the patch page. The launcher
  drew the old wide wordmark at 230dp; a square logo at 230dp is a third of the
  page, so that is now 170dp, and `compose-splash.js` matches it at 13.7% of
  the width.
- `textures/gui/ranlegacy_mark.dds` + `LOGIN_MARK` in `uioutercfg.xml`, repacked
  into `Gui.rcc` - the logo on the login and server-select screens, 150x150
  where the old wordmark was 177x96.

**Why DXT5, and why that needed writing.** There was no DDS *encoder* in the
tree, only decoders. It could not be skipped: `TextureManager` chooses how a
texture is drawn from the format it comes back as, and `D3DFMT_A8R8G8B8` lands
in the `EMTT_ALPHA_HARD` case - alpha test, no blending - which would cut the
logo's glow into a jagged edge. DXT5 is `EMTT_ALPHA_SOFT`, what every other UI
mark uses. `tools/rcc-extract/dds-encode.js` writes DXT5 with a full mip chain;
fully transparent texels are left out of the colour fit so the black they were
keyed from cannot drag the visible edge muddy.

**Name.** `android:label` was "RAN" in three places, now "Ran Legacy M". There
was no `android:icon` at all before this - the app was showing the stock Android
robot.

**Verified on LDPlayer:** the home screen shows the logo and "Ran Legacy M"; the
patch page and the boot screen carry it; the server-select screen draws it with
its glow blending against the sky, which is the DXT5 path doing its job.

**Trap for next time.** `adb push` with an absolute device path needs
`MSYS_NO_PATHCONV=1` under Git Bash. Without it the destination is rewritten to
`C:/Program Files/Git/storage/...`; the push *reports success* and the device
keeps the old file. The first Gui.rcc push did exactly that and the login screen
was still showing the old wordmark for it.

## The patch page is now the game's loading screen (2026-09-03)

New background art (`ran_old_film.dds`, 4096x2828 DXT5, the sepia class group
shot) and the two panels the client's own loading screen uses.

The proportions are not invented. `LoadingThread.cpp` lays that screen out in a
1024x768 virtual space:

    ld_top.dds   @ 0,0   1024x140   drawn 1024x128 at (0,0)
    the art                         drawn 1024x512 at (0,128)
    ld_under.dds @ 0,7   1024x140   drawn 1024x128 at (0,640)

so each band is **128/768 of the height** whatever the panel is, and the art has
the middle two thirds. The launcher page and `compose-splash.js` both build to
exactly that, which is the point of it: the player sees this screen and then,
moments later, the client's map loader draws the same one.

The bands stretch to width (`FIT_XY`) because they are a frame, not a picture -
their ends have to meet the edges of the screen - while the art is cover-cropped
into the middle.

**The logo moved into the top band.** Below it, it landed on the group's heads
and read as clutter. The band is empty by design; it is where the client puts
the map name. It is sized at 82% of the band rather than in dp, so it keeps its
margin on any panel.

**The status band lost its scrim.** It used to paint `#B4000000` behind the text;
over `ld_under` that was a second dark rectangle on a dark panel and showed as a
seam. The text and bar now sit directly on the band.

`ran_old_film.dds` lives in `MOBILE/art/`, not `CLIENT/`: it is drawn from the
APK - the launcher paints this screen *while* provisioning the data root, so
nothing it needs can come from there - and putting it in `CLIENT/` would ship
11.5 MB to every player for a file the game never reads.

`extract-launcher-art.js` no longer writes `ran_mark.png`. `make-icons.js` owns
it now that the mark is the Ran Legacy logo instead of a crop of a client sheet,
and leaving the old job in would have quietly put the RAN ONLINE wordmark back
the next time anyone ran the extractor.

## Ride/dismount button beside the chat (2026-09-03)

No keyboard on a phone, and the vehicle is otherwise only reachable by opening
the equipment window and double-clicking the slot every time.

`GLCharacter::ReqSetVehicle(bool)` is the request the PC path already uses and
`m_bVehicle` is the current state, so the button is a straight toggle of the
two. It re-checks nothing on purpose: that function already refuses when the map
forbids vehicles, the battery is flat, or the character is attacking, casting or
falling - and prints the reason itself.

The control is a child of `CBasicChat`, positioned **every frame** against the
chat's outside edge rather than laid out once, because the chat is dragged and
resized and a button left where the chat used to be is worse than no button.
Groups do not clip their children, so sitting outside the parent's rect draws
and hit-tests normally. The press also raises
`UIMSG_MOUSEIN_BLOCK_CHARACTER_MOVE` so it cannot double as a tap on the ground
behind the chat, and it takes the same `SetTouchPad` as the chat grip - 35x35
layout units is about 33dp, under the 48 a finger wants, at the screen edge
where a thumb is least precise.

The art is the game's own vehicle equip slot (`GUI_Inven_Slots.dds` @70,35),
added to `uiinnercfg01.xml` as `MOBILE_VEHICLE_BUTTON` / `_F` and repacked into
`Gui.rcc`. Only RAN_MOBILE code names those ids, so the entries are inert for
the PC build.

**Verified:** the button draws in the right place, parked on the chat's bottom
outside corner (`out/veh_z.png`).

**Not verified:** that it actually mounts. The test character has no vehicle
equipped, and with none `ReqSetVehicle(true)` returns early at
`!m_sVehicle.IsActiveValue()` *without* a message - so a tap is correctly
silent and proves nothing either way. Needs a character with a bike.

## Auto-target prefers what the character is facing (2026-09-03)

`MobileFindNearestMob` and `MobileFindNearestPvP` both took the strict nearest,
which is often not what the player means: standing between two mobs, the one
behind wins by a step and the character spins round to fight it.

Both now track **two** answers in the same pass - the nearest thing in front and
the nearest thing at all - and return the second, unchanged, when nothing is in
front. So the change can only alter which target is picked when one is being
faced; with none it is the old behaviour exactly, including all the dead-crow
and `IsPK_TAR` filtering.

Facing comes from `GLCharacter::m_vDir`, which is a persistent world heading -
it keeps its value while standing still, so the preference works when stationary
and not only while running.

Two judgement calls, both in the code as comments:

* **Flattened to XZ.** `m_vDir` is a heading, not a look direction. A mob up a
  slope or down a stairwell is still in front, and comparing the full 3D vectors
  would drop it for no reason the player can see.
* **90 degree window** (`cos(45)` either side). Wider starts choosing things off
  the shoulder that do not look aimed at; much narrower is hard to satisfy while
  moving, and then the preference never fires and it is the old behaviour with
  extra arithmetic.

A target standing on top of the character counts as faced: there is no direction
to test, and answering "not facing" would push the player at something further
away.

**Verified:** builds for both ABIs, and auto-target still selects in world -
`Lv.2 Little Vulgarian 180/180` on the panel with the bar over the mob beside
the character (`out/auto_test.png`).

**Not verified:** that it prefers the faced one over a nearer one behind. That
needs a controlled position between two candidates, which is what the player
will see immediately in normal play.

## Vehicle button, and skill icons that would not centre or respond (2026-09-03)

**The toggle did nothing.** The button called `ReqSetVehicle` directly. That
looked equivalent to pressing V and is not: `InnerInterface`'s `DIK_V` handler
refuses while a trade is open, enforces a one second cooldown through
`m_fVehicleDelay` - printing `VEHICLE_SET_FB_DELAY` when it bites - and resets
that timer afterwards. Going straight to the request skipped all three, so a
press inside the cooldown did nothing *and said nothing*. It now taps DIK_V
(0x2F) through `RanInput_KeyTap`, which is the pattern the other mobile buttons
already use: express the action as the key press the client is written to react
to, and nothing downstream has to know a finger did it.

**The icon was an empty socket.** The first cut used the vehicle equip slot out
of `GUI_Inven_Slots.dds`, which is a socket outline rather than a picture of
anything. `tools/rcc-extract/make-vehicle-button.js` now composes the game's own
motorcycle icon onto a round face in the touch-button idiom - dark chrome, a lit
rim, amber when pressed - and writes `mobile_vehicle.dds` / `_f.dds` as DXT5.

The sheet coordinates were measured, not eyeballed: `sc_bike_gui.dds` has 32x32
cells whose borders in the bike row sit at x=418/421 and 453/456, so the red
sportbike's content is x 422..452, y 213..241. The first attempt guessed 413 and
pulled in the border and half the neighbouring cell.

**Skill icons rode low, and taps missed.** The icons were centred on
`QUICK_SKILL_IMAGE` while their ring is drawn from the slot, and the two boxes
do not share a centre: `BASIC_QUICK_SKILL_TRAY_SLOT0` is **41x38** and
`QUICK_SKILL_IMAGE` is **35x35 at (3,3)**, so the image centre is 1.5 units
below the slot centre. Every icon sat low in its ring - and since the slot rect
is what receives the press, a tap aimed at the art could land outside it, which
is why tapping "did nothing". Both the ring and the icon now come from the slot.

**Verified on LDPlayer:** icons sit centred in their rings; tapping slot 1 puts
it on cooldown, raises the skill tooltip and casts (world changes by 91/pixel).
The motorcycle button draws correctly beside the chat.

**Not verified:** that the toggle mounts. The test character has no vehicle, and
`ReqSetVehicle` returns early with no message when `m_sVehicle.IsActiveValue()`
is false. It now takes exactly the PC path, so if a bike is equipped and the map
allows it, it behaves as V does on PC - including the cooldown message.

### Why the vehicle button drew but did not respond

Measured, not reasoned. The chain, probe by probe:

1. the touch reaches the overlay at **393,685** with `inClientUI=1` - the UI
   works in logical pixels, half the 786,1371 the screenshot showed, and the
   overlay correctly yields to the client;
2. the chat group dispatches `VEHICLE_BUTTON` every frame, so the control is
   registered and reachable;
3. the message was **0x80 MOUSEOUT** while the button's own logged rect was
   **370,664..412,706** - which contains 393,685.

Rect right, answer wrong. The only thing that explains both is *when* the rect
is set: `CBasicChat::Update` placed the button **after** calling
`CUIGroup::Update`, and that call is where children run their own
`MouseUpdate`. So the hit test used the rect from before the move while the draw
used the rect after it - tested in one place, painted in another. The placement
now runs above `CUIGroup::Update`.

Then it works in both directions, confirmed on screen and in the log:

    DIK_V seen: active=1 riding=1 delay=17.76 trade=0
    ReqSetVehicle(0) -> 0x00000000      (dismount)
    DIK_V seen: active=1 riding=0 delay=28.32
    ReqSetVehicle(1) -> 0x00000000      (mount, character is on the bike)

**Lesson worth keeping:** a control repositioned from its parent's `Update` has
to be moved *before* the base-class call, or its hit rect trails its art by a
frame - and if the control sits outside the parent, "a frame behind" means "a
whole button away".

**LDPlayer note:** `com.android.vending` and `com.android.ld.appstore` were
disabled with `pm disable-user` on the emulator. Both kept stealing the
foreground mid-test, and one whole round of "the button does not work" was
actually taps landing in the Play Store. Re-enable with `pm enable` if the
emulator is wanted for anything else.

### The ride button is now one of the pad's own buttons

It was a client UI control with a texture, so it could only ever look
*approximately* like the joystick, the attack ring and the mode toggles - those
are drawn by the overlay in GL, from the same palette and the same primitives.
Approximately similar is exactly what it looked like: first a socket outline,
then a 30 px item sprite stretched on a flat disc.

It is now `RANTOUCH_SLOT_VEHICLE`, an overlay button with the same face, rim and
press behaviour as auto-target and PK, and a motorcycle drawn with the same
primitives `artSword` and `artChest` use - `drawRing`, `drawCapsule`, `artPoly`,
in `kSteel` / `kBlade` / `kEdge` with `kGold` accents.

The split: **the client says where, the overlay says how big and how it looks.**
The button belongs beside the chat and the chat is dragged, so only the centre
can come from the client; taking the radius and the drawing from the overlay is
what makes it identical to its neighbours by construction instead of by eye.

Two attempts it took to make the glyph read at button size, both worth keeping:

* dark tyres on a dark face vanish - the wheels have to be the *light* part, or
  it is a blob with two smudges;
* filling the body pale is the same mistake - only the top edges are lit, and
  the shape is three pieces (a tail humping over the rear wheel, a tank, a
  fairing dropping to the front) because two pieces read as a bicycle.

The press is handled where every other pad button is, as `nKey = DIK_V`. This
also retires the client-side control, its two XML entries, its two DDS textures
and their generator - and with them the ordering trap where a control positioned
after `CUIGroup::Update` drew in one place and hit-tested in another.

**Verified on LDPlayer:** the button sits beside the chat in the pad's own
livery, and pressing it puts the character on the bike.

## Riding cost half the frame rate, and it was the vehicle's shadow (2026-09-03)

**Measured first, on the emulator, standing still in one spot.**

| | fps | frame | opaque verts | alpha verts | off-screen draws |
|---|---|---|---|---|---|
| on foot | 34 | 32.6 ms | 71,538 | 27,558 | 6 |
| mounted | 9 | 112.8 ms | 249,426 | 119,510 | 16 |
| mounted, no vehicle shadow | 13 | **54 ms** | 108,498 | 64,660 | 6 |

**Where the time went**, established before changing anything:

* submitting draws is **2% of the frame in both states**, so it is not the CPU;
* `/sdcard/ran/plainfs` - drop everything the fragment shader does after the
  texture fetch - changed nothing, so it is not shading;
* `/sdcard/ran/nulldraw` - skip the GL work of every draw - took the mounted
  frame from **113 ms to 16.7 ms**. So ~96 ms of it is the GPU chewing geometry.

The shadow pass draws the whole vehicle a second time into the shadow target,
and a vehicle is a far heavier mesh than a character. Cutting it halves the cost
of riding.

**The engine's own LOD is a stub.** `USE_SKINMESH_LOD` is commented out in
`DxSkinDefine.h`, and enabling it would achieve nothing: `g_dwLOD` is set - to 1
for shadows, and by distance - but **nothing anywhere reads it to choose a
mesh**. It is only ever set and counted into `g_dwHIGHDRAW_NUM`. Worth knowing
before anyone reaches for it again.

**Still open.** Riding is 54 ms against 32.6 on foot, so the vehicle's own
on-screen geometry still costs ~21 ms: +37k opaque and +37k alpha vertices over
being on foot. The alpha half is the suspicious part - ~37k alpha-blended
vertices for a motorbike suggests its parts are going through the blended path
rather than the opaque one, which would follow from DXT5 textures
(`TextureManager` maps DXT5 to `EMTT_ALPHA_SOFT`). Not chased yet, and not
guessed at: it needs the same measurement treatment.

**Not verified on the Tab S9** - it has been off adb throughout. The emulator's
GPU is not the tablet's, and the ratio may differ there.

### Reverted: auto-target no longer prefers what the character is facing

Tried in play and it did not feel right, so it is out. `git revert` of the
facing commit, and the diff against the version before that change is empty -
`MobileFindNearestMob` and `MobileFindNearestPvP` are byte-identical to what
they were, nearest-only, with all the dead-crow and `IsPK_TAR` filtering intact.

Verified after the revert: pressing auto-target still selects - `Lv.2 Little
Vulgarian 180/180 (100%)` on the panel - and the world runs at 36 fps.

Worth keeping in mind if it ever comes up again: the idea is sound on paper and
the implementation was cheap (two candidates tracked in one pass, falling back
to nearest when nothing is in front). What made it wrong was play feel, not
correctness, and that is not something the code can tell you.
