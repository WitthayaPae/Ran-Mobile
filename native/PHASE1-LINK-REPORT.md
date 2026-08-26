# Phase 1 link report — `libran.so`, 2026-08-24

The full PC client now **compiles clean for Android arm64** (0 errors, 0 failed TUs) and
links into `out/arm64-v8a/libran.so` (163 MB, every object force-included via
`--whole-archive` so nothing is silently dropped).

Built: Lib_Engine 415 TUs · Lib_Client 239 · Lib_ClientUI 472 · Lib_Network 26 (client
subset) · Lib_Helper 4 · zlib · lua 5.0.3 · minilzo · ogg · vorbis · ranshim.

## Undefined symbols: 476 total, 173 real

303 are ordinary libc / libc++ / C++ ABI symbols (`strlen`, `std::runtime_error`,
`__cxxabiv1::*`, iostream vtables …). Those resolve at load time from `libc.so` and
`libc++_shared.so` — they are not work.

The remaining **173 are the phase 3/4 work-list**, and they land in exactly the buckets the
audit predicted:

### 1. D3DX — 68 functions (phase 3)
- **Math, 33 fns** — `D3DXMatrix{Inverse,LookAtLH,Multiply,OrthoLH,PerspectiveFovLH,
  Rotation*,Scaling,Translation,Transpose}`, `D3DXQuaternion{Inverse,Multiply,Normalize,
  Rotation*,Slerp,Squad,SquadSetup}`, `D3DXVec{2Normalize,3CatmullRom,3Normalize,3Project,
  3TransformCoord,3TransformNormal,4Transform}`, `D3DXPlane*`. Pure arithmetic — a
  header/implementation pair, no GPU involvement. **Do these first**, they unblock
  everything that merely does maths.
- **Textures, 12 fns** — `D3DXCreateTexture(FromFile{Ex,InMemory,W})`,
  cube/volume variants, `D3DXSaveSurfaceToFileA`, `D3DXLoadSurfaceFromSurface`,
  `D3DXGetImageInfoFromFileW`. Needs DDS/TGA/BMP/JPG decode + GLES upload.
- **Meshes, 15 fns** — `D3DXLoadMeshFrom{X,XInMemory,Xof}`, `D3DXLoadMeshHierarchyFrom*`,
  `D3DXCreateMeshFVF`, `D3DXComputeNormals`, `D3DXComputeTangentFrameEx`,
  `D3DXGeneratePMesh`, `D3DXWeldVertices`, `D3DXCleanMesh`, `D3DXValidMesh`,
  `D3DXFrameDestroy`, `D3DXFrameCalculateBoundingSphere`.
- **X-file reader** — `D3DXFileCreate`, `DirectXFileCreate`, `TID_D3DRM*` GUIDs
  (`Frame`, `FrameTransformMatrix`, `Mesh`, `Animation`, `AnimationKey`, `AnimationSet`),
  `IID_IDirectXFileData*`. This is the `.X`/`.chr` model loader — a self-contained parser.
- `D3DXCreateSprite`, `D3DXGetFVFVertexSize`, `D3DXGetDeclLength`,
  `D3DXComputeBoundingSphere`.

### 2. The files deliberately excluded from the build (phase 3/4)
Their symbols show up because callers remain; each needs a mobile implementation:
- **Fonts / text** — `CD3DFontX`, `CD3DFontPar`, `DxFontMan`, `COMMENT::FONTSYS`.
  GDI text today; FreeType on mobile.
- **Sound** — `CSoundManager`, `CSound`, `SSound`, `DxBgmSound`, `DirectSoundCreate8`,
  `DS3DALG_HRTF_*`. DirectSound today; OpenSL ES / AAudio on mobile.
- **Input** — `DirectInput8Create`, `IID_IDirectInput8A`, `GUID_SysKeyboard`,
  `GUID_SysMouse`, `c_dfDIKeyboard`, `c_dfDIMouse2`. DirectInput today; touch on mobile.
- **IME / edit** — `CIMEEdit`, `DXInputString`. Windows IME today; soft keyboard on mobile.
- **Window frame** — `CD3DApplication::{ChangeDeviceMode,FindDeviceMode}`. The mobile
  surface owns resolution, so these become no-ops over EGL.
- **Web** — `CCommonWeb`. Dead on mobile; the stub header is in place, the .cpp still needs
  an inert implementation (or its callers guarded).
- **JPEG** — `ijlInit`, `ijlFree` (Intel JPEG Library). Replace with stb_image.
- Small odds: `CConsoleMessage::GetInstance`, `CHCStart`/`CHCEnd` (HackShield hooks — make
  them no-ops), `GLCLUB`, `Get/SetCheck_Flags`, `DXUTFindDXSDKMediaFileCch`.

### 3. Not in the list — notable
`Direct3DCreate9` never appears. The engine obtains its device solely through
`DXUTGetD3DDevice()` / `DXUTGetD3DObject()`, which the shim already owns
(`shim/d3d/dxut_compat.cpp`). So the GLES backend plugs in at exactly one place:
`DXUTSetD3D(pD3D, pDevice)` at startup. That is a much better seam than expected.

## Order of work

1. **D3DX math** (33 fns, pure arithmetic) — unblocks the widest surface.
2. **D3D9 device shim over GLES 3** — implement `IDirect3D9`/`IDirect3DDevice9` and hand it
   to `DXUTSetD3D`. Fixed-function state → uber-shader, per the audit.
3. Texture loaders → GLES textures; X-file/mesh loaders → VB/IB.
4. Fonts (FreeType), sound (OpenSL), input (touch).
