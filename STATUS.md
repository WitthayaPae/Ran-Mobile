# RAN mobile — STATUS

**This is the living document. It is updated at the end of every working session.**
If anything here disagrees with another file, this file wins.

- **Last updated:** 2026-08-25 (session 3)
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
| `MOBILE/native/PHASE1-LINK-REPORT.md` | the 173-symbol work-list for phases 3–4 | live |
| `MOBILE/native/PHASE2-BOOT.md` | how to install/run the headless boot APK and what its log means | live |
| `MOBILE/native/layout/LAYOUT-REPORT.md` | packet-layout gate: method, findings, how to re-run | live |
| `MOBILE/reference/` | decoded PC file formats and asset pipeline — **still true**, engine-independent | reference |
| `MOBILE/archive-unity/` | superseded Unity-rewrite docs, kept only as history | dead |
| `MOBILE/unity/` | the frozen Unity project (fallback / asset source) | frozen |
| `SOURCE/SOURCE_*.md` | maps of the original C++ codebase | reference |

Root-level `*.md` (EP1/EP7 ports, GM_COMMANDS, plan.md …) are the **server/content**
workstream, unrelated to this port. Untouched.

---

## Update rule

At the end of any session that changes something: update **Last updated**, the phase table,
and add an entry to the log below. Then update the status log in `NATIVE-PORT-PLAN.md` if
the change was structural.

## Log

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
