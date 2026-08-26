# Native Port Plan — compile the PC client (SOURCE/) for Android / iOS

Decision (owner, 2026-08-24): stop the Unity rewrite as the main line. Build the real PC
client C++ for mobile so behaviour is the PC code by construction. Unity project stays on
disk as a fallback / asset reference only.

## Audit of SOURCE/ (measured 2026-08-24)

| Library | files | LOC | raw `pd3dDevice->` calls | D3DX uses | Win32/MFC files |
|---|---|---|---|---|---|
| Lib_Engine | 828 | 302k | 5,963 (42 files hold the device ptr) | 12.5k | 46 |
| Lib_Client | 443 | 293k | 71 | 2.8k | 19 |
| Lib_ClientUI | 1062 | 225k | 504 | 913 | 4 |
| GameClient2 | 9 | 2.3k | 14 | 21 | 3 |
| Lib_Network | 174 | 70k | 0 | 5 | 16 |
| Lib_Helper | 53 | 14k | 0 | 0 | 16 |

Rendering profile (71 distinct `IDirect3DDevice9` methods):
- **~90 % fixed-function**: SetRenderState 1865, SetTextureStageState 911, SetTransform 325,
  SetSamplerState 318, SetTexture 307, state blocks 437, lights/material 373, SetFVF 180,
  DrawPrimitiveUP 81 / DrawIndexedPrimitive 78 / DrawPrimitive 38 / DrawIndexedPrimitiveUP 16,
  render-target / depth surface switches 177 (shadow, water reflection, post-process).
- **Shaders**: 20 CreateVertexShader/PixelShader sites, all vs_1_1 / ps_1_x / ps_2_0 **assembly
  strings inside .cpp** (DxShader{Bump,Rain,Reflect,Reflection,Shadow,Specular,Toon}.cpp,
  DxEffectRiver/Water2/Nature/Fur/Dot3/PostProcess/ParticleSys). No .fx / HLSL files. Each is
  a small hand-port to GLSL ES.
- **D3DX**: math (D3DXVECTOR/MATRIX/Quaternion — ~3k call sites, header-only reimplement),
  `ID3DXMesh` in 20 files (DXUT mesh, octree map mesh, land, frame mesh, cloth/grass/spore
  effects), `ID3DXFont`/`ID3DXSprite` (D3DFontX, DxFontMan — GDI text → needs FreeType),
  `ID3DXEffect` 7 refs (DXUT only, unused by game path — verify), D3DXCreateTextureFromFile
  (DDS/BMP/TGA/JPG loaders), D3DXComputeNormals, D3DXMatrixAABBTransform etc.
- Input: DirectInput (DxInputDevice.cpp), IME (IMEEdit.cpp, DXInputString.cpp).
- Sound: DirectSound (DxSoundMan, BgmSound*, dxframe/dsutil) + ogg/vorbis.
- Misc Win32: webbrowser2 (IE embed — drop), GammaControl (drop), DxCursor (touch — drop),
  CommonWeb, HPro.
- MFC: `CString` 3,691 lines / 788 files; CTime 551, CRect 145, CStringArray 142, CPoint 132,
  CTimeSpan 113, CFile 38, CSize 22, CList 13, CArray 4. → write a small `mfc_compat.h`.
- Threads: 39 files use CreateThread/_beginthread/CRITICAL_SECTION; TBB in 2 files.
- Network: 21 winsock/IOCP files (Lib_Network s_NetClient) → BSD sockets + select/epoll.
- `__asm` in 6 files (DebugSet.h, DxBoneCollector, DxSkeletonManager, ExceptionHandler,
  GameClient2WndD3d, s_CSystemInfo) → C equivalents. SEH `__try` in 6 files → remove.
- Third-party in Tik/: lzo, ogg/vorbis, boost 1.34 (foreach/function/unordered_map/regex/
  format/signals — replace with std/C++17), ijl (Intel JPEG → stb_image), Lua, TBB.
  Drop: HShield, nProtect, FCI/FDI, BugTrap.
- **LP64 hazard**: packet structs in `Lib_Client/G-Logic/GLContrl*Msg.h` (27 `#pragma pack`
  headers, 758 DWORD/long lines). Windows: `long`/`DWORD` = 4 bytes; Android arm64: `long` =
  8. Must typedef `DWORD/LONG/ULONG` to 32-bit in the shim and audit bare `long` in every
  packed struct (use `tools/layout-probe` to diff sizeof vs MSVC x86). Server is x86 MSVC —
  wire layout is law.

Toolchain already on disk (Unity 6000.5.8f1): NDK r27c, SDK build-tools 36, platforms
android-34/36, CMake 3.22, OpenJDK, Gradle — `…/PlaybackEngines/AndroidPlayer/{NDK,SDK,OpenJDK}`.

## Architecture

```
MOBILE/native/
  shim/win/      windows.h, mfc_compat.h (CString/CTime/CRect…), tchar, winsock → BSD,
                 threads (CRITICAL_SECTION → pthread mutex), file IO (CFile → fopen)
  shim/d3d9/     IDirect3D9 / IDirect3DDevice9 / textures / VB / IB / surfaces / state blocks
                 implemented over OpenGL ES 3.x (fixed-function emulated with an uber-shader
                 driven by the render-state/texture-stage state; asm shaders ported to GLSL)
  shim/d3dx/     math (header-only), texture loaders (DDS/TGA/BMP/JPG), ID3DXMesh (VB/IB +
                 attribute table), ID3DXFont via FreeType, D3DXCompute*/AABB helpers
  platform/android/  NativeActivity (android_main), EGL, touch → mouse/keyboard messages,
                     asset/OBB file access, OpenSL/AAudio sound, soft keyboard → IME path
  platform/ios/      (later) UIView + GLES-on-Metal (ANGLE) or MoltenVK, same shim
  CMakeLists.txt     builds Lib_Engine, Lib_Client, Lib_ClientUI, Lib_Network, Lib_Helper,
                     Lib_ZLib + third-party as static libs, links into libran.so
```

Touch UI: the PC UI XML (800x600-anchored) renders unchanged; a touch layer maps taps to
WM_LBUTTONDOWN/UP, drags to mouse move, adds virtual joystick + quick-slot overlay emitting
the same key/mouse messages the PC window receives. Mobile-specific GUI = overlay, not a
rewrite.

## Phases

1. **Compile** — CMake + NDK, shim headers, get all libs compiling for arm64-v8a with stub
   D3D9 (no rendering). Exit: `libran.so` links; layout-probe confirms packet sizeof parity.
2. **Boot headless** — android_main → CGameClient2App equivalent (no MFC), config, file
   packs (RCC) from OBB/asset dir, connect to login server, reach char-select in logs.
3. **Render** — d3d9 shim over GLES3: clear/present, DrawPrimitiveUP + UI textures (login
   screen visible), then FVF/VB/IB + fixed-function lighting (world/characters), state
   blocks, render targets (shadow/water), asm shader ports, fonts.
4. **Input/Sound** — touch → mouse/keys, soft keyboard, virtual joystick overlay; OpenSL.
5. **Polish** — performance (state-change batching, texture format ETC2/ASTC transcode),
   iOS build.

Status log:
- 2026-08-24: audit done, phase 1 started.
- 2026-08-24 (cont.): phase-1 build harness live.
  - `MOBILE/native/` created: `CMakeLists.txt`, `build.sh` (NDK r27c, arm64-v8a, Ninja),
    `iterate.sh` (build + distinct-error histogram), `cmake/sources_*.cmake` generated from
    the .vcxproj file lists (Lib_Engine 434, Lib_ClientUI 472, Lib_Client 239,
    Lib_Network 112, Lib_Helper 26, Lib_ZLib 16, GameClient2 4).
  - Third-party sources fetched into `native/third_party/`: lua 5.0.3, minilzo 2.10,
    libogg 1.3.5, libvorbis 1.3.7 (replacing the Windows .lib files in Tik/Library).
  - Win32 shim written: `shim/win/windows.h` (types with 32-bit DWORD/LONG, COM macros,
    kernel32/user32/GDI/registry/IMM/crypto surface), `mfc_compat.h` (CString, CTime,
    CTimeSpan, CRect/CPoint/CSize, CFile/CStdioFile, CFileFind, CArray/CList/CMap/
    CStringArray, CCriticalSection/CSingleLock, inert CWnd/CDC/CEdit family),
    `win_impl.cpp` (pthreads, events, file IO, dir globbing, ini parse/write, codepage,
    time), `winsock2.h` (BSD sockets), plus ~50 stub headers (afx*, io, process, imm,
    mmsystem, strsafe, dxerr9, usp10, shlobj, msxml2, psapi, …).
  - **The real DirectX 9 SDK headers from `Tik/DXInclude` parse under the shim** — d3d9.h,
    d3dx9*.h, dinput.h, dsound.h compile unmodified, so the shim can implement the exact
    COM interfaces the engine calls.
  - Toolchain notes: clang needs `-fms-extensions -fms-compatibility
    -fms-compatibility-version=19.30 -fdelayed-template-parsing`; C++14 (the code uses
    std::auto_ptr / bind2nd / random_shuffle, removed in C++17); each lib force-includes its
    own stdafx.h to emulate MSVC /Yu.
  - SOURCE edits (all guarded by `#ifdef RAN_MOBILE`, PC build unchanged):
    DebugSet.h rdtsc → clock_gettime, s_CSystemInfo.cpp rdtsc → timeGetTime,
    basestream.h / SerialFile.h / ByteStream.h / SerialFile.cpp: the `UINT` overloads of
    `operator<< / >>` are disabled because on arm64 UINT and DWORD are the same type
    (on MSVC x86 they are `unsigned int` vs `unsigned long`). `__try/__except` become
    `if(1)/else if(0)`.
  - Files dropped from the mobile build: ExceptionHandler.cpp, BlockProg.cpp (anti-cheat),
    webbrowser2.cpp, CommonWeb.cpp, iowin32.c.
  - **Status: Lib_Engine 339 / 437 translation units compile** (from 0). Remaining ~100 are
    a long tail: DirectInput device code, GDI text/bitmap paths, MSXML (`RanXML.h` uses
    IXMLDOMNode — needs a real XML parser, this is the UI-layout loader so it matters),
    MSVC-only reference binding to temporaries, DXUT leftovers.
  - Known real work still ahead in phase 1: replace MSXML with a bundled XML parser, decide
    per-file whether DirectInput/GDI paths get stubs or mobile implementations, then
    Lib_Network / Lib_Client / Lib_ClientUI, then the packet-layout probe.
  - Later 2026-08-24: **Lib_Engine 354 / 432 TUs compile, 235 errors left (was 437 failing).**
    Done since the first checkpoint: DXUT sample framework replaced by `shim/d3d/dxut_compat.*`
    (the engine only used `DXUTGetD3DDevice/Object` + `D3DUtil_InitLight/InitMaterial`);
    `shim/d3d/dxstdafx.h` now stands in for `DxCommon9/dxstdafx.h` (keeps the `…Q` type
    aliases and the TSTRING family); MSXML dropped — `RanXML.cpp` is dead code, the UI XML
    actually goes through the hand-written `CRanXMLParser`, so no XML dependency after all;
    unqualified `max(`/`min(` rewritten to `__max`/`__min` (MSVC gets them as macros from
    windows.h, but macros break libc++'s `<algorithm>`); MSVC's "non-const ref binds to a
    temporary" extension does not exist in clang, so the ~27 sites were fixed properly by
    constifying the callees (`CFileFindTree::FindPathName/FindPathNameNoExtension/CreateTree`,
    `DxViewPort::GetMouseTargetPosWnd`, `DxAfterRender::AfterRender`, `DXInputString::
    Create/Move`) — those are improvements for the PC build too.
    Remaining classes: DirectInput `case DIMOFS_BUTTON0` (FIELD_OFFSET isn't a constant
    expression in clang), GDI text/bitmap paths (D3DFontX, DxFontMan, TextTexture — these get
    a FreeType implementation in phase 3 anyway), DirectSound (DxSoundMan/BgmSound → OpenSL in
    phase 4), and per-file odds and ends.

## PHASE 1 COMPLETE — 2026-08-24

All three phase-1 exit criteria met:

1. **Compiles.** Lib_Engine (415 TUs) + Lib_Client (239) + Lib_ClientUI (472) +
   Lib_Network (26, client subset) + Lib_Helper (4) + zlib/lua/minilzo/ogg/vorbis/shim —
   `./build.sh` reports **0 errors, 0 failed TUs** for arm64-v8a.
2. **Links.** `out/arm64-v8a/libran.so`, 163 MB with `--whole-archive` (nothing dropped).
   Undefined symbols: 476 total, of which 303 are ordinary libc/libc++ resolved at load.
   The remaining **173 are the phase 3/4 work-list** — see `PHASE1-LINK-REPORT.md`.
   Best news there: `Direct3DCreate9` never appears — the engine takes its device solely
   from `DXUTGetD3DDevice()`, which the shim already owns, so the GLES backend plugs in at
   a single call: `DXUTSetD3D(pD3D, pDevice)`.
3. **Packet layout verified against the real server ABI.** 1,288 message structs compared
   against ground truth from the actual MSVC x86 toolchain (VS 2022) — not an inference.
   **1,286 identical.** One real bug found and fixed: MFC's `CTime` aligns to 4 on x86, the
   shim's aligned to 8, which shifted `SONEMAPWEATHER` and broke `SNETPC_MAPWEATHER` (+128
   bytes), `SNETPC_MAPWHIMSICALWEATHER` and `SNETDROP_PC`. Fixed with `#pragma pack(push,4)`
   around `CTime`/`CTimeSpan`. Details + how to re-run: `layout/LAYOUT-REPORT.md`.

### Phase 2 next — boot headless
`platform/android/main.cpp` currently only anchors the link. Next: a real `android_main`
that starts the app object without MFC, points the file layer at the unpacked data root,
loads the RCC packs, connects to the login server, and reaches character-select in logcat —
with no rendering at all.

## REMAINING WORK TO PARITY — opened 2026-08-25

Everything below is measured against the PC client, not guessed. Phase 3 owns the first two
groups, phase 4 the third.

### A. Rendering gaps still visible on screen

| # | Gap | What is known | State |
|---|-----|---------------|-------|
| A1 | Character-select map drew black | portrait panel `SetRenderTarget` + black `Clear` hit the visible frame | ✅ fixed — FBO render targets |
| A2 | No characters anywhere | `D3DXLoadMeshHierarchyFromX*` was a stub | ✅ fixed — hierarchy loader + `ID3DXSkinInfo` + vertex blending |
| A3 | Map textures soft/shimmering | only mip level 0 uploaded, everything clamped+bilinear | ✅ fixed — full mip chain + `D3DSAMP_*` |
| A4 | Objects/effects missing in places | was missing data on the test device (data/effect, most maps, sounds); zero failed opens after pushing them | ✅ fixed |
| A8 | In-world ground/terrain invisible | it was being face-culled: winding, see the log entry | ✅ fixed |
| A9 | In-game HUD draws nothing visible | same cause; 2D UI is no longer culled at all | ✅ fixed |
| A5 | Effect passes (`DxEffGroupPlayer`, `OPTMManager`, `DxGlowMan`, `DxCubeMap`, weather) | never exercised yet; they need render targets, which now exist | ⬜ open |
| A6 | Shadows (`DxShadowMap::RenderShadowCharMob`) | same — depends on render targets | ⬜ open |
| A7 | Asm/HLSL shader paths (`DxMaterialHLSL`, normal-map pieces) | engine falls back to fixed function today | ⬜ open |
| A10 | Arms stretched, glove detached, while torso and legs pose correctly | measured 2026-08-25: animation load, bone binding, key lookup, skeleton update and the matrices sent to the GPU are all correct, and skin conversion never prunes (`pruned 0`, `maxInfl 2`). Remaining suspect is the attachment path — `SetupBoneMatrixPointersOnMesh`'s else branch, where a piece is placed by frame local x parent frame x bone rather than skinned | ⬜ open, next |
| A11 | Frame rate | palette 5 + best-fit packing: skinned draws 152 -> 54, frame 54.7 -> 33 ms on LDPlayer. Emulator cost is ~0.2 ms per draw call (its GL translation), so further gains there mean fewer calls; a real-device number is still needed | 🔶 improved |

### B. The GUI is still a desktop GUI

The client renders its outer/in-game GUI at the real panel resolution (2560x1440 on the test
tablet), so every control is drawn at PC pixel sizes — legible on a monitor, far too small
under a finger. The layout itself is correct and PC-faithful; only the scale and the input
affordances are wrong.

| # | Gap | Approach | State |
|---|-----|----------|-------|
| B1 | Everything is half the size it should be on a tablet | run the client at a logical resolution (~1280x720) and upscale the whole frame; one scale factor also divides touch coordinates | ✅ done — `RanGL_UIScale`, logical 1280x720 |
| B2 | Touch targets smaller than a fingertip even after B1 | measure the real control rects from the GUI XML, widen the hit test where the art allows | ⬜ open |
| B3 | No virtual joystick / movement affordance | overlay drawn by the shim, feeding the same DirectInput device the client already reads | ⬜ open |
| B4 | Text entry depends on the client's own on-screen keyboard | works for login; a soft-keyboard bridge is still wanted for chat | ⬜ open |

### C. Platform work

| # | Gap | State |
|---|-----|-------|
| C1 | Audio (DirectSound → OpenSL/AAudio) | ⬜ open |
| C5 | Game loop runs on the activity main thread, so a map load trips Android's ANR watchdog on a real device | ⬜ open |
| C6 | Real devices need MANAGE_EXTERNAL_STORAGE granted (or the data moved app-private) before the client can read /sdcard/ran | ⬜ open — documented in push-tablet.sh |
| C2 | Texture transcode to ETC2/ASTC for memory | ⬜ open |
| C3 | State-change batching in the GL backend | 🟨 mostly done — client-owned VBOs, streaming ring buffer, GL state/uniform/vertex-layout caches, rendering at the logical size, and UI draw batching (210→44 UI draws a frame). Emulator frame time is now ~0.25 ms per draw call, which is its GL translation; world-geometry batching is the only lever left and needs a real-device measurement first |
| C4 | iOS build off the same shim | ⬜ open |
