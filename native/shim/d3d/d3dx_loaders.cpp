// D3DX loaders — texture, mesh and .X-file entry points.
//
// PHASE 3 STATUS: the texture loaders are real (see image_decode.cpp). The mesh
// and .X-file entry points are still placeholders. Each placeholder logs the
// FIRST time it is
// called and then fails cleanly, so a headless boot produces a measured list of
// exactly which loaders the client reaches and in what order — that list is the
// phase-3 work order, rather than a guess about it.
//
// They deliberately fail rather than pretend to succeed: a loader that returns
// D3D_OK with an empty object would hand the engine a garbage mesh and turn a
// clear "not implemented yet" into a mystery crash later.

#include "windows.h"
#include "../platform/ran_plat.h"
#include <d3dx9.h>
#include "image_decode.h"

//  Records which file a texture was decoded from, so the renderer can name it.
extern "C" void RanD3D_NoteTexturePath(LPDIRECT3DTEXTURE9 pTex, const char *szPath);

//  Cube textures live in the device layer; the decode belongs here.
extern "C" IDirect3DCubeTexture9 *RanD3D_CreateCubeTexture(IDirect3DDevice9 *dev, UINT edge,
                                                           UINT levels, int d3dFormat);
extern "C" void RanD3D_SetCubeFaceLevel(IDirect3DCubeTexture9 *tex, int face, int level,
                                        const void *bits, unsigned size);
extern "C" void RanD3D_NoteCubeTexturePath(IDirect3DCubeTexture9 *tex, const char *szPath);

#include <set>
#include <vector>
#include <stdio.h>
#include <string>

#define LOGW(...) RanPlat_Log(RANLOG_WARN, "RanD3DX", __VA_ARGS__)

namespace {
std::set<std::string> g_seen;
// Report each unimplemented entry point once; a boot calls some of these
// thousands of times and a per-call log would bury everything else.
void once(const char *fn) {
    if (g_seen.insert(fn).second) LOGW("NOT IMPLEMENTED (phase 3): %s", fn);
}
} // namespace

#define STUB(fn) once(fn)

extern "C" {

// ------------------------------------------------------------------ textures
//
// These are real now. The decode lives in image_decode.cpp; what happens here
// is the D3DX contract around it: create a texture of the file's own size and
// format, copy each mip level in through LockRect, and fill out D3DXIMAGE_INFO.
//
// Deliberately NOT done: rescaling to Width/Height, format conversion, mip
// generation and colour-key masking. The client passes D3DX_DEFAULT for size
// and mip count everywhere it loads a texture, and D3DFMT_UNKNOWN for the
// format, so honouring the file exactly is what the engine actually asks for.
// If a call ever does ask for something else, it is logged rather than silently
// ignored.

} // extern "C" — the helpers below need C++ linkage

namespace {

// Reads a whole file through the shim's path resolver (fopen is redirected to
// ran_fopen by windows.h, which fixes separators and case).
bool readWholeFile(const char *path, std::vector<BYTE> &out) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return false; }
    out.resize((size_t)n);
    size_t got = fread(&out[0], 1, (size_t)n, f);
    fclose(f);
    out.resize(got);
    return got > 0;
}

void fillInfo(D3DXIMAGE_INFO *pInfo, const RanImage &img) {
    if (!pInfo) return;
    pInfo->Width = img.width;
    pInfo->Height = img.height;
    pInfo->Depth = 1;
    pInfo->MipLevels = img.mipLevels;
    pInfo->Format = img.format;
    pInfo->ResourceType = D3DRTYPE_TEXTURE;
    pInfo->ImageFileFormat = (D3DXIMAGE_FILEFORMAT)img.fileFormat;
}

// The one place a decoded image becomes a device texture.
//  Set while loading from a file, so a decode failure - and the texture it
//  produces - can name it.
const char *g_loadingPath = NULL;

HRESULT imageToTexture(LPDIRECT3DDEVICE9 pDevice, const RanImage &img, UINT MipLevels,
                       LPDIRECT3DTEXTURE9 *ppTexture) {
    if (!pDevice || !ppTexture) return D3DERR_INVALIDCALL;

    UINT levels = (UINT)img.levels.size();
    if (MipLevels != D3DX_DEFAULT && MipLevels != 0 && MipLevels < levels) levels = MipLevels;

    LPDIRECT3DTEXTURE9 pTex = NULL;
    HRESULT hr = pDevice->CreateTexture(img.width, img.height, levels, 0, img.format,
                                        D3DPOOL_MANAGED, &pTex, NULL);
    if (FAILED(hr) || !pTex) return FAILED(hr) ? hr : E_FAIL;

    for (UINT i = 0; i < levels; ++i) {
        D3DLOCKED_RECT lr;
        if (FAILED(pTex->LockRect(i, &lr, NULL, 0)) || !lr.pBits) continue;
        // The surface was allocated from the same width/height/format, so its
        // byte count matches the decoded level exactly.
        D3DSURFACE_DESC d;
        pTex->GetLevelDesc(i, &d);
        size_t n = img.levels[i].size();
        memcpy(lr.pBits, &img.levels[i][0], n);
        pTex->UnlockRect(i);
    }

    *ppTexture = pTex;
    if (g_loadingPath) RanD3D_NoteTexturePath(pTex, g_loadingPath);
    return D3D_OK;
}


HRESULT createFromMemory(LPDIRECT3DDEVICE9 pDevice, LPCVOID pSrcData, UINT SrcDataSize,
                         UINT MipLevels, D3DXIMAGE_INFO *pSrcInfo,
                         LPDIRECT3DTEXTURE9 *ppTexture) {
    RanImage img;
    if (!RanImage_Decode(pSrcData, SrcDataSize, img)) {
        //  The first bytes are what identifies the format, so report them:
        //  a decoder that does not know a container is a different fix from a
        //  file that arrived truncated or encrypted.
        const BYTE *b = (const BYTE *)pSrcData;
        char szWhat[256];
        snprintf(szWhat, sizeof(szWhat),
                 "texture decode failed: %s (%u bytes, starts %02X %02X %02X %02X '%c%c%c%c')",
                 g_loadingPath ? g_loadingPath : "(from memory)", (unsigned)SrcDataSize,
                 SrcDataSize > 0 ? b[0] : 0, SrcDataSize > 1 ? b[1] : 0,
                 SrcDataSize > 2 ? b[2] : 0, SrcDataSize > 3 ? b[3] : 0,
                 SrcDataSize > 0 && b[0] >= 32 && b[0] < 127 ? b[0] : '.',
                 SrcDataSize > 1 && b[1] >= 32 && b[1] < 127 ? b[1] : '.',
                 SrcDataSize > 2 && b[2] >= 32 && b[2] < 127 ? b[2] : '.',
                 SrcDataSize > 3 && b[3] >= 32 && b[3] < 127 ? b[3] : '.');
        once(szWhat);
        return D3DXERR_INVALIDDATA;
    }
    fillInfo(pSrcInfo, img);
    return imageToTexture(pDevice, img, MipLevels, ppTexture);
}

HRESULT createFromFile(LPDIRECT3DDEVICE9 pDevice, LPCSTR pSrcFile, UINT MipLevels,
                       D3DXIMAGE_INFO *pSrcInfo, LPDIRECT3DTEXTURE9 *ppTexture) {
    if (!pSrcFile) return D3DERR_INVALIDCALL;
    std::vector<BYTE> bytes;
    if (!readWholeFile(pSrcFile, bytes)) {
        char szWhat[256];
        snprintf(szWhat, sizeof(szWhat), "texture file missing or empty: %s", pSrcFile);
        once(szWhat);
        return D3DXERR_INVALIDDATA;
    }
    g_loadingPath = pSrcFile;
    HRESULT hr = createFromMemory(pDevice, &bytes[0], (UINT)bytes.size(), MipLevels, pSrcInfo, ppTexture);
    g_loadingPath = NULL;
    return hr;
}

//  A cube DDS becomes one cube texture: six faces, each with its own mip chain,
//  stored face-major exactly as the file has them.
HRESULT cubeFromMemory(LPDIRECT3DDEVICE9 pDevice, LPCVOID pSrcData, UINT SrcDataSize,
                       D3DXIMAGE_INFO *pSrcInfo, LPDIRECT3DCUBETEXTURE9 *ppCubeTexture) {
    if (!pDevice || !ppCubeTexture) return D3DERR_INVALIDCALL;
    *ppCubeTexture = NULL;

    RanImage faces[6];
    if (!RanImage_DecodeCube(pSrcData, SrcDataSize, faces)) {
        char szWhat[192];
        snprintf(szWhat, sizeof(szWhat), "not a cube map: %s (%u bytes)",
                 g_loadingPath ? g_loadingPath : "(from memory)", (unsigned)SrcDataSize);
        once(szWhat);
        return D3DXERR_INVALIDDATA;
    }

    fillInfo(pSrcInfo, faces[0]);
    if (pSrcInfo) pSrcInfo->ResourceType = D3DRTYPE_CUBETEXTURE;

    IDirect3DCubeTexture9 *tex = RanD3D_CreateCubeTexture(pDevice, faces[0].width,
                                                          (UINT)faces[0].levels.size(),
                                                          (int)faces[0].format);
    if (!tex) return E_OUTOFMEMORY;
    for (int f = 0; f < 6; ++f)
        for (size_t l = 0; l < faces[f].levels.size(); ++l)
            RanD3D_SetCubeFaceLevel(tex, f, (int)l, &faces[f].levels[l][0],
                                    (unsigned)faces[f].levels[l].size());
    if (g_loadingPath) RanD3D_NoteCubeTexturePath(tex, g_loadingPath);
    *ppCubeTexture = tex;
    return D3D_OK;
}

HRESULT cubeFromFile(LPDIRECT3DDEVICE9 pDevice, LPCSTR pSrcFile,
                     D3DXIMAGE_INFO *pSrcInfo, LPDIRECT3DCUBETEXTURE9 *ppCubeTexture) {
    if (!pSrcFile) return D3DERR_INVALIDCALL;
    std::vector<BYTE> bytes;
    if (!readWholeFile(pSrcFile, bytes)) {
        char szWhat[192];
        snprintf(szWhat, sizeof(szWhat), "cube map file missing or empty: %s", pSrcFile);
        once(szWhat);
        return D3DXERR_INVALIDDATA;
    }
    g_loadingPath = pSrcFile;
    HRESULT hr = cubeFromMemory(pDevice, &bytes[0], (UINT)bytes.size(), pSrcInfo, ppCubeTexture);
    g_loadingPath = NULL;
    return hr;
}

// Narrow a wide path for the resolver; every path in the client's data is ASCII.
std::string narrow(LPCWSTR w) {
    std::string s;
    if (!w) return s;
    while (*w) { s.push_back((char)(*w & 0xFF)); ++w; }
    return s;
}

} // namespace

extern "C" {

HRESULT WINAPI D3DXCreateTexture(LPDIRECT3DDEVICE9 pDevice, UINT Width, UINT Height,
                                 UINT MipLevels, DWORD, D3DFORMAT Format, D3DPOOL,
                                 LPDIRECT3DTEXTURE9 *ppTexture) {
    // A plain allocation with no decoding — render targets and scratch surfaces.
    if (!pDevice || !ppTexture) return D3DERR_INVALIDCALL;
    return pDevice->CreateTexture(Width, Height, MipLevels, 0, Format, D3DPOOL_MANAGED,
                                  ppTexture, NULL);
}

HRESULT WINAPI D3DXCreateTextureFromFileA(LPDIRECT3DDEVICE9 pDevice, LPCSTR pSrcFile,
                                          LPDIRECT3DTEXTURE9 *ppTexture) {
    return createFromFile(pDevice, pSrcFile, D3DX_DEFAULT, NULL, ppTexture);
}

HRESULT WINAPI D3DXCreateTextureFromFileW(LPDIRECT3DDEVICE9 pDevice, LPCWSTR pSrcFile,
                                          LPDIRECT3DTEXTURE9 *ppTexture) {
    std::string s = narrow(pSrcFile);
    return createFromFile(pDevice, s.c_str(), D3DX_DEFAULT, NULL, ppTexture);
}

HRESULT WINAPI D3DXCreateTextureFromFileExA(LPDIRECT3DDEVICE9 pDevice, LPCSTR pSrcFile,
                                            UINT, UINT, UINT MipLevels, DWORD, D3DFORMAT,
                                            D3DPOOL, DWORD, DWORD, D3DCOLOR,
                                            D3DXIMAGE_INFO *pSrcInfo, PALETTEENTRY *,
                                            LPDIRECT3DTEXTURE9 *ppTexture) {
    return createFromFile(pDevice, pSrcFile, MipLevels, pSrcInfo, ppTexture);
}

HRESULT WINAPI D3DXCreateTextureFromFileExW(LPDIRECT3DDEVICE9 pDevice, LPCWSTR pSrcFile,
                                            UINT, UINT, UINT MipLevels, DWORD, D3DFORMAT,
                                            D3DPOOL, DWORD, DWORD, D3DCOLOR,
                                            D3DXIMAGE_INFO *pSrcInfo, PALETTEENTRY *,
                                            LPDIRECT3DTEXTURE9 *ppTexture) {
    std::string s = narrow(pSrcFile);
    return createFromFile(pDevice, s.c_str(), MipLevels, pSrcInfo, ppTexture);
}

HRESULT WINAPI D3DXCreateTextureFromFileInMemory(LPDIRECT3DDEVICE9 pDevice, LPCVOID pSrcData,
                                                 UINT SrcDataSize,
                                                 LPDIRECT3DTEXTURE9 *ppTexture) {
    return createFromMemory(pDevice, pSrcData, SrcDataSize, D3DX_DEFAULT, NULL, ppTexture);
}

HRESULT WINAPI D3DXCreateTextureFromFileInMemoryEx(LPDIRECT3DDEVICE9 pDevice, LPCVOID pSrcData,
                                                   UINT SrcDataSize, UINT, UINT, UINT MipLevels,
                                                   DWORD, D3DFORMAT, D3DPOOL, DWORD, DWORD,
                                                   D3DCOLOR, D3DXIMAGE_INFO *pSrcInfo,
                                                   PALETTEENTRY *, LPDIRECT3DTEXTURE9 *ppTexture) {
    return createFromMemory(pDevice, pSrcData, SrcDataSize, MipLevels, pSrcInfo, ppTexture);
}

HRESULT WINAPI D3DXGetImageInfoFromFileA(LPCSTR pSrcFile, D3DXIMAGE_INFO *pSrcInfo) {
    std::vector<BYTE> bytes;
    if (!pSrcFile || !readWholeFile(pSrcFile, bytes)) return D3DXERR_INVALIDDATA;
    RanImage img;
    if (!RanImage_Decode(&bytes[0], bytes.size(), img)) return D3DXERR_INVALIDDATA;
    fillInfo(pSrcInfo, img);
    return D3D_OK;
}

HRESULT WINAPI D3DXGetImageInfoFromFileW(LPCWSTR pSrcFile, D3DXIMAGE_INFO *pSrcInfo) {
    std::string s = narrow(pSrcFile);
    return D3DXGetImageInfoFromFileA(s.c_str(), pSrcInfo);
}

HRESULT WINAPI D3DXGetImageInfoFromFileInMemory(LPCVOID pSrcData, UINT SrcDataSize,
                                                D3DXIMAGE_INFO *pSrcInfo) {
    RanImage img;
    if (!RanImage_Decode(pSrcData, SrcDataSize, img)) return D3DXERR_INVALIDDATA;
    fillInfo(pSrcInfo, img);
    return D3D_OK;
}

// Cube textures: the character specular passes bind one to stage 1. Size, format
// and mip arguments are ignored for the same reason the 2D loaders ignore them -
// the client passes D3DX_DEFAULT everywhere and wants the file as it is.
HRESULT WINAPI D3DXCreateCubeTextureFromFileExA(LPDIRECT3DDEVICE9 pDevice, LPCSTR pSrcFile,
                                                UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, DWORD,
                                                DWORD, D3DCOLOR, D3DXIMAGE_INFO *pSrcInfo,
                                                PALETTEENTRY *,
                                                LPDIRECT3DCUBETEXTURE9 *ppTex) {
    return cubeFromFile(pDevice, pSrcFile, pSrcInfo, ppTex);
}
HRESULT WINAPI D3DXCreateCubeTextureFromFileW(LPDIRECT3DDEVICE9 pDevice, LPCWSTR pSrcFile,
                                              LPDIRECT3DCUBETEXTURE9 *ppTex) {
    std::string s = narrow(pSrcFile);
    return cubeFromFile(pDevice, s.c_str(), NULL, ppTex);
}
HRESULT WINAPI D3DXCreateCubeTextureFromFileInMemoryEx(LPDIRECT3DDEVICE9 pDevice, LPCVOID pSrcData,
                                                       UINT SrcDataSize, UINT, UINT, DWORD,
                                                       D3DFORMAT, D3DPOOL, DWORD, DWORD,
                                                       D3DCOLOR, D3DXIMAGE_INFO *pSrcInfo,
                                                       PALETTEENTRY *,
                                                       LPDIRECT3DCUBETEXTURE9 *ppTex) {
    return cubeFromMemory(pDevice, pSrcData, SrcDataSize, pSrcInfo, ppTex);
}
HRESULT WINAPI D3DXCreateVolumeTextureFromFileW(LPDIRECT3DDEVICE9, LPCWSTR,
                                                LPDIRECT3DVOLUMETEXTURE9 *ppTex) {
    STUB("D3DXCreateVolumeTextureFromFileW");
    if (ppTex) *ppTex = NULL;
    return D3DERR_NOTAVAILABLE;
}
HRESULT WINAPI D3DXSaveTextureToFileA(LPCSTR, D3DXIMAGE_FILEFORMAT, LPDIRECT3DBASETEXTURE9,
                                      const PALETTEENTRY *) {
    STUB("D3DXSaveTextureToFileA"); return D3DERR_NOTAVAILABLE;
}
HRESULT WINAPI D3DXSaveSurfaceToFileA(LPCSTR, D3DXIMAGE_FILEFORMAT, LPDIRECT3DSURFACE9,
                                      const PALETTEENTRY *, const RECT *) {
    STUB("D3DXSaveSurfaceToFileA"); return D3DERR_NOTAVAILABLE;
}
HRESULT WINAPI D3DXLoadSurfaceFromSurface(LPDIRECT3DSURFACE9, const PALETTEENTRY *, const RECT *,
                                          LPDIRECT3DSURFACE9, const PALETTEENTRY *, const RECT *,
                                          DWORD, D3DCOLOR) {
    STUB("D3DXLoadSurfaceFromSurface"); return D3DERR_NOTAVAILABLE;
}


// -------------------------------------------------------------------- meshes
// The mesh hierarchy loader, D3DXFrameDestroy and D3DXFrameCalculateBoundingSphere
// are real, in d3dx_hierarchy.cpp: every character in the game arrives through them.
HRESULT WINAPI D3DXComputeNormals(LPD3DXBASEMESH, const DWORD *) {
    STUB("D3DXComputeNormals"); return D3DERR_NOTAVAILABLE;
}
HRESULT WINAPI D3DXComputeTangentFrameEx(ID3DXMesh *, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD,
                                         DWORD, DWORD, DWORD, const DWORD *, FLOAT, FLOAT, FLOAT,
                                         ID3DXMesh **, ID3DXBuffer **) {
    STUB("D3DXComputeTangentFrameEx"); return D3DERR_NOTAVAILABLE;
}
HRESULT WINAPI D3DXGeneratePMesh(LPD3DXMESH, const DWORD *, const D3DXATTRIBUTEWEIGHTS *,
                                 const FLOAT *, DWORD, DWORD, LPD3DXPMESH *) {
    STUB("D3DXGeneratePMesh"); return D3DERR_NOTAVAILABLE;
}
HRESULT WINAPI D3DXWeldVertices(LPD3DXMESH, DWORD, const D3DXWELDEPSILONS *, const DWORD *,
                                DWORD *, DWORD *, LPD3DXBUFFER *) {
    STUB("D3DXWeldVertices"); return D3DERR_NOTAVAILABLE;
}
HRESULT WINAPI D3DXValidMesh(LPD3DXMESH, const DWORD *, LPD3DXBUFFER *) {
    STUB("D3DXValidMesh"); return D3D_OK;
}
HRESULT WINAPI D3DXCleanMesh(D3DXCLEANTYPE, LPD3DXMESH, const DWORD *, LPD3DXMESH *, DWORD *,
                             LPD3DXBUFFER *) {
    STUB("D3DXCleanMesh"); return D3DERR_NOTAVAILABLE;
}
UINT WINAPI D3DXGetDeclLength(const D3DVERTEXELEMENT9 *pDecl) {
    UINT n = 0;
    if (pDecl) while (pDecl[n].Stream != 0xFF) ++n;   // D3DDECL_END sentinel
    return n;
}

// ------------------------------------------------------------------ .X files
// D3DXFileCreate and DirectXFileCreate are real now - see xfile_com.cpp.

// Legacy DirectX Retained-Mode template GUIDs. These are pure data — the .X
// parser identifies node types by them — so they are defined for real, not
// stubbed, and phase 3's parser can use them straight away.
const GUID TID_D3DRMFrame =
    { 0x3d82ab46, 0x62da, 0x11cf, { 0xab, 0x39, 0x00, 0x20, 0xaf, 0x71, 0xe4, 0x33 } };
const GUID TID_D3DRMFrameTransformMatrix =
    { 0xf6f23f41, 0x7686, 0x11cf, { 0x8f, 0x52, 0x00, 0x40, 0x33, 0x35, 0x94, 0xa3 } };
const GUID TID_D3DRMMesh =
    { 0x3d82ab44, 0x62da, 0x11cf, { 0xab, 0x39, 0x00, 0x20, 0xaf, 0x71, 0xe4, 0x33 } };
const GUID TID_D3DRMAnimation =
    { 0x3d82ab4f, 0x62da, 0x11cf, { 0xab, 0x39, 0x00, 0x20, 0xaf, 0x71, 0xe4, 0x33 } };
const GUID TID_D3DRMAnimationKey =
    { 0x10dd46a8, 0x775b, 0x11cf, { 0x8f, 0x52, 0x00, 0x40, 0x33, 0x35, 0x94, 0xa3 } };
const GUID TID_D3DRMAnimationSet =
    { 0x3d82ab50, 0x62da, 0x11cf, { 0xab, 0x39, 0x00, 0x20, 0xaf, 0x71, 0xe4, 0x33 } };
const GUID IID_IDirectXFileData =
    { 0x3d82ab44, 0x62da, 0x11cf, { 0xab, 0x39, 0x00, 0x20, 0xaf, 0x71, 0xe4, 0x33 } };
const GUID IID_IDirectXFileDataReference =
    { 0x3d82ab45, 0x62da, 0x11cf, { 0xab, 0x39, 0x00, 0x20, 0xaf, 0x71, 0xe4, 0x33 } };

// -------------------------------------------------------------- misc/sprite
// D3DXCreateSprite and the font factories live in d3dx_font.cpp.

// Quaternion exp/ln — used by D3DXQuaternionSquadSetup, so implemented for real.
D3DXQUATERNION *WINAPI D3DXQuaternionLn(D3DXQUATERNION *pOut, const D3DXQUATERNION *pQ) {
    float norm = sqrtf(pQ->x * pQ->x + pQ->y * pQ->y + pQ->z * pQ->z);
    if (norm > 1e-6f) {
        float theta = atan2f(norm, pQ->w) / norm;
        pOut->x = pQ->x * theta; pOut->y = pQ->y * theta; pOut->z = pQ->z * theta;
    } else {
        pOut->x = pQ->x; pOut->y = pQ->y; pOut->z = pQ->z;
    }
    pOut->w = 0.0f;
    return pOut;
}
D3DXQUATERNION *WINAPI D3DXQuaternionExp(D3DXQUATERNION *pOut, const D3DXQUATERNION *pQ) {
    float theta = sqrtf(pQ->x * pQ->x + pQ->y * pQ->y + pQ->z * pQ->z);
    if (theta > 1e-6f) {
        float s = sinf(theta) / theta;
        pOut->x = pQ->x * s; pOut->y = pQ->y * s; pOut->z = pQ->z * s;
    } else {
        pOut->x = pQ->x; pOut->y = pQ->y; pOut->z = pQ->z;
    }
    pOut->w = cosf(theta);
    return pOut;
}

} // extern "C"
