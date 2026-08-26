// D3D9 shim — the object model the engine talks to.
//
// Phase 2 (now): every call is accepted and recorded, nothing is drawn. That is
// enough to boot the real client end to end — it creates its device, loads the
// world, talks to the server — with no GPU involved, which isolates data and
// protocol bugs from rendering bugs.
//
// Phase 3: the same classes gain an OpenGL ES 3 backend. The render/texture
// state kept here is exactly what that backend needs, so it is tracked properly
// now rather than discarded: RenderState/TextureStageState/SamplerState arrays,
// the transform stack, bound textures and streams. Nothing here is throwaway.
//
// The COM boilerplate (238 methods that just return D3D_OK) comes from
// d3d9_gen.h, generated from the SDK header — see gen-d3d9-impl.js.

#include "windows.h"
#include <unwind.h>
#include <dlfcn.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <android/log.h>
#include <vector>
#include <string.h>

#include "d3d9_gen.h"
#include "dxut_compat.h"
#include "../gl/gl_context.h"
#include "../gl/gl_render.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "RanD3D", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  "RanD3D", __VA_ARGS__)

namespace {

// One counter set so a boot can be summarised in a line instead of a log flood.
struct Stats {
    unsigned long frames = 0, draws = 0, clears = 0;
    unsigned long texturesCreated = 0, vbCreated = 0, ibCreated = 0;
    unsigned long texturesFreedCPU = 0;      // textures whose decoded copy was released
    unsigned long long textureBytes = 0, vbBytes = 0, ibBytes = 0;
} g_stats;

//  What the frame's draws actually are, so optimisation aims at the big bucket.
struct DrawBuckets {
    unsigned long skinned, uiQuads, alphaBlended, opaqueWorld;
    unsigned long vertsSkinned, vertsAlpha, vertsOpaque;
} g_buckets = {0,0,0,0,0,0,0};



//  Wall clock spent between presents, against which the draw time is compared.
double g_frameSeconds = 0.0, g_lastPresent = 0.0;


int bytesPerPixel(D3DFORMAT f) {
    switch (f) {
        case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: case D3DFMT_A8B8G8R8:
        case D3DFMT_X8B8G8R8: case D3DFMT_A2R10G10B10:                    return 4;
        case D3DFMT_R8G8B8:                                               return 3;
        case D3DFMT_R5G6B5: case D3DFMT_A1R5G5B5: case D3DFMT_X1R5G5B5:
        case D3DFMT_A4R4G4B4: case D3DFMT_A8L8:                           return 2;
        case D3DFMT_A8: case D3DFMT_L8: case D3DFMT_P8:                   return 1;
        default:                                                          return 4;
    }
}
// DXT blocks are 4x4; DXT1 is 8 bytes per block, DXT3/5 are 16.
bool isCompressed(D3DFORMAT f) {
    return f == D3DFMT_DXT1 || f == D3DFMT_DXT2 || f == D3DFMT_DXT3
        || f == D3DFMT_DXT4 || f == D3DFMT_DXT5;
}
UINT surfaceBytes(UINT w, UINT h, D3DFORMAT f) {
    if (isCompressed(f)) {
        UINT bw = (w + 3) / 4, bh = (h + 3) / 4;
        UINT blockBytes = (f == D3DFMT_DXT1) ? 8 : 16;
        return bw * bh * blockBytes;
    }
    return w * h * bytesPerPixel(f);
}

class RanTexture;
class RanCubeTexture;

// --------------------------------------------------------------- surface
class RanSurface : public IDirect3DSurface9 {
public:
    //  Set when this surface is one mip level of a texture: rendering into the
    //  surface has to end up in that texture's GL object, since that is what
    //  the UI samples afterwards.
    RanTexture *m_owner = NULL;
    unsigned    m_standaloneTex = 0;   // for CreateRenderTarget surfaces
    LONG m_ref = 1;
    UINT m_width, m_height;
    D3DFORMAT m_format;
    std::vector<BYTE> m_bits;
    IDirect3DDevice9 *m_device;

    RanSurface(IDirect3DDevice9 *dev, UINT w, UINT h, D3DFORMAT fmt)
        : m_width(w), m_height(h), m_format(fmt), m_device(dev) {
        m_bits.resize(surfaceBytes(w, h, fmt));
    }

    HRESULT QueryInterface(REFIID, void **ppv) override { *ppv = this; AddRef(); return S_OK; }
    ULONG AddRef() override { return (ULONG)++m_ref; }
    ULONG Release() override { LONG r = --m_ref; if (r <= 0) { delete this; return 0; } return (ULONG)r; }

    HRESULT GetDesc(D3DSURFACE_DESC *pDesc) override {
        if (!pDesc) return D3DERR_INVALIDCALL;
        memset(pDesc, 0, sizeof(*pDesc));
        pDesc->Format = m_format;
        pDesc->Type = D3DRTYPE_SURFACE;
        pDesc->Pool = D3DPOOL_MANAGED;
        pDesc->Width = m_width;
        pDesc->Height = m_height;
        return D3D_OK;
    }
    //  Storage is dropped once the pixels reach GL; a lock means the caller is
    //  about to write new ones, so it comes back empty rather than stale.
    void ensureBits() {
        if (m_bits.empty()) m_bits.assign(surfaceBytes(m_width, m_height, m_format), 0);
    }

    //  The region a lock said it was going to write, unioned across locks and
    //  cleared when the pixels reach GL. Empty width means "nothing dirty";
    //  a rect covering the whole surface is treated as a full upload.
    int m_dirtyX0 = 0, m_dirtyY0 = 0, m_dirtyX1 = 0, m_dirtyY1 = 0;

    void markDirty(const RECT *pRect) {
        int x0 = 0, y0 = 0, x1 = (int)m_width, y1 = (int)m_height;
        if (pRect && !isCompressed(m_format)) {
            x0 = (int)pRect->left;  y0 = (int)pRect->top;
            x1 = (int)pRect->right; y1 = (int)pRect->bottom;
            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            if (x1 > (int)m_width)  x1 = (int)m_width;
            if (y1 > (int)m_height) y1 = (int)m_height;
        }
        if (x1 <= x0 || y1 <= y0) return;
        if (m_dirtyX1 <= m_dirtyX0) { m_dirtyX0 = x0; m_dirtyY0 = y0; m_dirtyX1 = x1; m_dirtyY1 = y1; return; }
        if (x0 < m_dirtyX0) m_dirtyX0 = x0;
        if (y0 < m_dirtyY0) m_dirtyY0 = y0;
        if (x1 > m_dirtyX1) m_dirtyX1 = x1;
        if (y1 > m_dirtyY1) m_dirtyY1 = y1;
    }
    void clearDirty() { m_dirtyX0 = m_dirtyY0 = m_dirtyX1 = m_dirtyY1 = 0; }
    bool dirtyIsWholeSurface() const {
        return m_dirtyX0 <= 0 && m_dirtyY0 <= 0 &&
               m_dirtyX1 >= (int)m_width && m_dirtyY1 >= (int)m_height;
    }

    HRESULT LockRect(D3DLOCKED_RECT *pLocked, const RECT *pRect, DWORD) override {
        if (!pLocked) return D3DERR_INVALIDCALL;
        const bool hadBits = !m_bits.empty();
        ensureBits();
        //  Storage that had to be re-created holds nothing the GPU has seen,
        //  so the whole surface counts as rewritten however small the lock is.
        markDirty(hadBits ? pRect : NULL);
        UINT pitch = isCompressed(m_format)
                   ? surfaceBytes(m_width, 4, m_format) / 1
                   : m_width * bytesPerPixel(m_format);
        pLocked->Pitch = (INT)pitch;
        UINT offset = 0;
        if (pRect && !isCompressed(m_format))
            offset = pRect->top * pitch + pRect->left * bytesPerPixel(m_format);
        pLocked->pBits = m_bits.empty() ? NULL : (m_bits.data() + offset);
        return D3D_OK;
    }
    HRESULT UnlockRect() override { return D3D_OK; }

    //  Defined out of line: it needs RanTexture, which is declared below.
    unsigned RenderTargetTexture();

    RAN_D3D9_STUBS_IDIRECT3DSURFACE9
};

// --------------------------------------------------------------- texture
class RanTexture : public IDirect3DTexture9 {
public:
    LONG m_ref = 1;
    UINT m_levels;
    D3DFORMAT m_format;
    std::vector<RanSurface *> m_surfaces;    // one per mip level
    IDirect3DDevice9 *m_device;

    //  What the client asked for. A render target sampled before anything has
    //  been drawn into it needs different treatment from a texture that simply
    //  has not finished loading.
    DWORD m_usage = 0;
    //  Where this texture came from. A GL texture id on its own cannot be
    //  matched to a file, which is what every texture question eventually asks.
    std::string m_srcPath;

    RanTexture(IDirect3DDevice9 *dev, UINT w, UINT h, UINT levels, D3DFORMAT fmt)
        : m_format(fmt), m_device(dev) {
        if (levels == 0) {                    // 0 means "full chain"
            levels = 1;
            UINT mw = w, mh = h;
            while (mw > 1 || mh > 1) { mw = mw > 1 ? mw / 2 : 1; mh = mh > 1 ? mh / 2 : 1; ++levels; }
        }
        m_levels = levels;
        UINT mw = w, mh = h;
        for (UINT i = 0; i < levels; ++i) {
            m_surfaces.push_back(new RanSurface(dev, mw ? mw : 1, mh ? mh : 1, fmt));
            m_surfaces.back()->m_owner = this;
            g_stats.textureBytes += surfaceBytes(mw ? mw : 1, mh ? mh : 1, fmt);
            mw = mw > 1 ? mw / 2 : 1;
            mh = mh > 1 ? mh / 2 : 1;
        }
    }
    ~RanTexture() {
        if (m_glTex) { RanGLR_ForgetRenderTarget(m_glTex); RanGLR_DeleteTexture(m_glTex); }
        for (auto *s : m_surfaces) s->Release();
    }

    HRESULT QueryInterface(REFIID, void **ppv) override { *ppv = this; AddRef(); return S_OK; }
    ULONG AddRef() override { return (ULONG)++m_ref; }
    ULONG Release() override { LONG r = --m_ref; if (r <= 0) { delete this; return 0; } return (ULONG)r; }

    DWORD GetLevelCount() override { return m_levels; }
    HRESULT GetLevelDesc(UINT Level, D3DSURFACE_DESC *pDesc) override {
        if (Level >= m_surfaces.size()) return D3DERR_INVALIDCALL;
        return m_surfaces[Level]->GetDesc(pDesc);
    }
    HRESULT GetSurfaceLevel(UINT Level, IDirect3DSurface9 **ppSurf) override {
        if (Level >= m_surfaces.size() || !ppSurf) return D3DERR_INVALIDCALL;
        m_surfaces[Level]->AddRef();
        *ppSurf = m_surfaces[Level];
        return D3D_OK;
    }
    HRESULT LockRect(UINT Level, D3DLOCKED_RECT *pLocked, const RECT *pRect, DWORD Flags) override {
        if (Level >= m_surfaces.size()) return D3DERR_INVALIDCALL;
        return m_surfaces[Level]->LockRect(pLocked, pRect, Flags);
    }
    // Uploading on unlock (not on every bind) means a texture that is written
    // once and drawn thousands of times costs one upload.
    unsigned m_glTex = 0;
    bool     m_dirty = true;
    //  Once the client has rendered into this texture, its CPU bits are stale
    //  by definition — re-uploading them would paint over the rendered image.
    bool     m_isRenderTarget = false;

    HRESULT UnlockRect(UINT Level) override {
        if (Level >= m_surfaces.size()) return D3DERR_INVALIDCALL;
        if (Level == 0) m_dirty = true;
        return m_surfaces[Level]->UnlockRect();
    }

    //  Give out a GL texture to render into, allocating one if the texture has
    //  never been uploaded.
    unsigned RenderTargetTexture() {
        m_isRenderTarget = true;
        m_dirty = false;
        if (!m_glTex) m_glTex = RanGLR_CreateEmptyTexture();
        return m_glTex;
    }

    unsigned GlTexture() {
        //  The client draws its loading screen from a background thread, which has
        //  no EGL context: glGenTextures there produces nothing and the upload is
        //  lost. Clearing the dirty flag would make that loss permanent, leaving
        //  the texture blank even once the render thread reached it - which is
        //  why the loading art, the hint icon and the map-name plate never
        //  appeared. Leave it dirty and upload later, on the right thread.
        if (!RanGLR_OnRenderThread()) return m_glTex;

        //  A render target nothing has drawn into yet. The port does not run
        //  every one of the engine's off-screen passes, so the refraction and
        //  post-process targets are sampled while still empty; answering with
        //  no texture at all makes the stage sample opaque white, and an effect
        //  that modulates by it paints a solid white slab over the scene. An
        //  empty target is transparent black, which is what D3D leaves behind
        //  and what makes those effects disappear rather than dominate.
        if (!m_glTex && (m_usage & D3DUSAGE_RENDERTARGET)) {
            static const BYTE zero[4] = { 0, 0, 0, 0 };
            m_glTex = RanGLR_UploadTextureLevel(0, 0, 1, 1, (int)D3DFMT_A8R8G8B8, zero, 4);
            RanGLR_FinishTexture(m_glTex, 1, (int)D3DFMT_A8R8G8B8);
            m_isRenderTarget = true;
            m_dirty = false;
            LOGW("render target %ux%u sampled before anything drew into it -> gl %u",
                 m_surfaces.empty() ? 0 : m_surfaces[0]->m_width,
                 m_surfaces.empty() ? 0 : m_surfaces[0]->m_height, m_glTex);
            return m_glTex;
        }

        //  A surface the client rewrote in part, on a texture GL already has:
        //  send just that rectangle and leave the mip chain alone. This is the
        //  font atlas's path, and it runs many times a frame.
        if (m_dirty && !m_isRenderTarget && m_glTex && m_surfaces.size() == 1 &&
            !isCompressed(m_format)) {
            RanSurface *s = m_surfaces[0];
            if (!s->m_bits.empty() && s->m_dirtyX1 > s->m_dirtyX0 && !s->dirtyIsWholeSurface()) {
                const UINT bpp = bytesPerPixel(m_format);
                const UINT pitch = s->m_width * bpp;
                const BYTE *origin = s->m_bits.data() +
                                     (size_t)s->m_dirtyY0 * pitch + (size_t)s->m_dirtyX0 * bpp;
                RanGLR_UpdateTextureRect(m_glTex, s->m_dirtyX0, s->m_dirtyY0,
                                         s->m_dirtyX1 - s->m_dirtyX0,
                                         s->m_dirtyY1 - s->m_dirtyY0,
                                         (int)m_format, origin, pitch);
                s->clearDirty();
                m_dirty = false;
                return m_glTex;
            }
        }
        if (m_dirty && !m_isRenderTarget && !m_surfaces.empty()) {
            //  The whole chain, not just level 0: the shipped DDS files carry
            //  their mips and the map shimmers without them.
            for (size_t i = 0; i < m_surfaces.size(); ++i) {
                RanSurface *s = m_surfaces[i];
                if (s->m_bits.empty()) break;
                m_glTex = RanGLR_UploadTextureLevel(m_glTex, (int)i, (int)s->m_width,
                                                    (int)s->m_height, (int)m_format,
                                                    s->m_bits.data(),
                                                    (unsigned)s->m_bits.size());
            }
            RanGLR_FinishTexture(m_glTex, (int)m_surfaces.size(), (int)m_format);
            if (!m_srcPath.empty())
                LOGI("texture %u = %s (%ux%u, %u levels)", m_glTex, m_srcPath.c_str(),
                     m_surfaces[0]->m_width, m_surfaces[0]->m_height,
                     (unsigned)m_surfaces.size());
            for (size_t i = 0; i < m_surfaces.size(); ++i) m_surfaces[i]->clearDirty();

            //  The GPU has them now, and holding the decoded copy as well is
            //  what ran the device out of memory on the first world map — but
            //  only for compressed textures. An uncompressed one may be a
            //  surface the client keeps writing to: the font atlas is locked
            //  again for every new glyph, and dropping its pixels meant each
            //  glyph re-uploaded an otherwise empty atlas, which blanked most
            //  of the text in the game.
            if (isCompressed(m_format)) {
                for (size_t i = 0; i < m_surfaces.size(); ++i) {
                    std::vector<BYTE> empty;
                    m_surfaces[i]->m_bits.swap(empty);
                }
                ++g_stats.texturesFreedCPU;
            }
            m_dirty = false;
        }
        return m_glTex;
    }

    RAN_D3D9_STUBS_IDIRECT3DTEXTURE9
};

//  The loaders know the path; the texture object is where it has to live.
extern "C" void RanD3D_NoteTexturePath(IDirect3DTexture9 *pTex, const char *szPath) {
    if (pTex && szPath) ((RanTexture *)pTex)->m_srcPath = szPath;
}

// ---------------------------------------------------------- cube texture
//  The hair and armour specular passes bind one of these to stage 1 and sample
//  it with the camera-space normal. Only what those passes need is real: six
//  faces of decoded levels, uploaded once, and a GL name to bind.
class RanCubeTexture : public IDirect3DCubeTexture9 {
public:
    LONG m_ref = 1;
    IDirect3DDevice9 *m_device;
    UINT m_edge = 0, m_levels = 0;
    D3DFORMAT m_format = D3DFMT_UNKNOWN;
    std::string m_srcPath;
    //  face -> level -> bytes, kept until the upload happens on the render thread.
    std::vector<std::vector<BYTE> > m_faces[6];
    unsigned m_glTex = 0;
    bool m_dirty = true;

    explicit RanCubeTexture(IDirect3DDevice9 *dev) : m_device(dev) {
        if (m_device) m_device->AddRef();
    }
    ~RanCubeTexture() { if (m_device) m_device->Release(); }

    HRESULT QueryInterface(REFIID, void **ppv) override { *ppv = this; AddRef(); return S_OK; }
    ULONG AddRef() override { return (ULONG)++m_ref; }
    ULONG Release() override { LONG r = --m_ref; if (r <= 0) { delete this; return 0; } return (ULONG)r; }
    DWORD GetLevelCount() override { return m_levels; }
    D3DRESOURCETYPE GetType() override { return D3DRTYPE_CUBETEXTURE; }
    HRESULT GetLevelDesc(UINT Level, D3DSURFACE_DESC *pDesc) override {
        if (!pDesc) return D3DERR_INVALIDCALL;
        memset(pDesc, 0, sizeof(*pDesc));
        UINT e = m_edge >> Level;
        pDesc->Width = pDesc->Height = e ? e : 1;
        pDesc->Format = m_format;
        pDesc->Type = D3DRTYPE_CUBETEXTURE;
        pDesc->Pool = D3DPOOL_MANAGED;
        return D3D_OK;
    }

    //  Uploads on first use, from the thread that owns the context.
    unsigned GlTexture() {
        if (!RanGLR_OnRenderThread()) return m_glTex;
        if (!m_dirty) return m_glTex;
        for (int f = 0; f < 6; ++f) {
            UINT e = m_edge;
            for (size_t l = 0; l < m_faces[f].size(); ++l) {
                if (m_faces[f][l].empty()) break;
                m_glTex = RanGLR_UploadCubeFaceLevel(m_glTex, f, (int)l, (int)(e ? e : 1),
                                                     (int)(e ? e : 1), (int)m_format,
                                                     &m_faces[f][l][0],
                                                     (unsigned)m_faces[f][l].size());
                e = e > 1 ? e / 2 : 1;
            }
        }
        RanGLR_FinishCubeTexture(m_glTex, (int)m_levels);
        if (m_glTex) {
            m_dirty = false;
            for (int f = 0; f < 6; ++f) {
                std::vector<std::vector<BYTE> > empty;
                m_faces[f].swap(empty);
            }
            if (!m_srcPath.empty())
                LOGI("cube texture %u = %s (%ux%u, %u levels)", m_glTex, m_srcPath.c_str(),
                     m_edge, m_edge, m_levels);
        }
        return m_glTex;
    }

    RAN_D3D9_STUBS_IDIRECT3DCUBETEXTURE9
};

//  Built by the D3DX cube loaders, which own the decode.
extern "C" IDirect3DCubeTexture9 *RanD3D_CreateCubeTexture(IDirect3DDevice9 *dev, UINT edge,
                                                           UINT levels, int d3dFormat) {
    RanCubeTexture *t = new RanCubeTexture(dev);
    t->m_edge = edge;
    t->m_levels = levels;
    t->m_format = (D3DFORMAT)d3dFormat;
    return t;
}

extern "C" void RanD3D_SetCubeFaceLevel(IDirect3DCubeTexture9 *tex, int face, int level,
                                        const void *bits, unsigned size) {
    if (!tex || face < 0 || face > 5 || !bits || !size) return;
    RanCubeTexture *c = (RanCubeTexture *)tex;
    if ((int)c->m_faces[face].size() <= level) c->m_faces[face].resize(level + 1);
    c->m_faces[face][level].assign((const BYTE *)bits, (const BYTE *)bits + size);
}

extern "C" void RanD3D_NoteCubeTexturePath(IDirect3DCubeTexture9 *tex, const char *szPath) {
    if (tex && szPath) ((RanCubeTexture *)tex)->m_srcPath = szPath;
}

extern "C" unsigned RanD3D_CubeGlTexture(IDirect3DCubeTexture9 *tex) {
    return tex ? ((RanCubeTexture *)tex)->GlTexture() : 0;
}

unsigned RanSurface::RenderTargetTexture() {
    if (m_owner) return m_owner->RenderTargetTexture();
    if (!m_standaloneTex) m_standaloneTex = RanGLR_CreateEmptyTexture();
    return m_standaloneTex;
}

// ---------------------------------------------------- vertex / index buffers
class RanVertexBuffer : public IDirect3DVertexBuffer9 {
public:
    LONG m_ref = 1;
    UINT m_length; DWORD m_fvf, m_usage;
    std::vector<BYTE> m_data;

    //  The same bytes on the GPU, refreshed when the client unlocks. Created
    //  lazily so a buffer that is never drawn costs nothing.
    unsigned m_glBuffer = 0;
    bool     m_glDirty = true;

    RanVertexBuffer(UINT len, DWORD usage, DWORD fvf)
        : m_length(len), m_fvf(fvf), m_usage(usage) { m_data.resize(len); }
    ~RanVertexBuffer() { if (m_glBuffer) RanGLR_DeleteBuffer(m_glBuffer); }

    //  The byte range the client said it was writing, unioned across locks.
    UINT m_dirtyBegin = 0, m_dirtyEnd = 0;
    bool m_glCreated = false;

    void markRange(UINT offset, UINT size, DWORD flags) {
        if (flags & D3DLOCK_READONLY) return;   // nothing will change
        if (size == 0 || offset + size > m_length) { size = m_length - (offset < m_length ? offset : m_length); }
        if (!size) return;
        if (m_dirtyEnd <= m_dirtyBegin) { m_dirtyBegin = offset; m_dirtyEnd = offset + size; }
        else {
            if (offset < m_dirtyBegin) m_dirtyBegin = offset;
            if (offset + size > m_dirtyEnd) m_dirtyEnd = offset + size;
        }
        m_glDirty = true;
    }

    unsigned GlBuffer() {
        if (m_data.empty()) return 0;
        if (!m_glBuffer) m_glBuffer = RanGLR_CreateBuffer();
        if (m_glBuffer && m_glDirty) {
            if (!m_glCreated) {
                //  First time: the buffer has to be sized before anything can
                //  be written into part of it.
                RanGLR_UpdateBuffer(m_glBuffer, 0, m_data.data(), (unsigned)m_data.size());
                m_glCreated = true;
            } else if (m_dirtyEnd > m_dirtyBegin) {
                RanGLR_UpdateBufferRange(m_glBuffer, 0, m_dirtyBegin,
                                         m_data.data() + m_dirtyBegin,
                                         (unsigned)(m_dirtyEnd - m_dirtyBegin));
            }
            m_dirtyBegin = m_dirtyEnd = 0;
            m_glDirty = false;
        }
        return m_glBuffer;
    }

    HRESULT QueryInterface(REFIID, void **ppv) override { *ppv = this; AddRef(); return S_OK; }
    ULONG AddRef() override { return (ULONG)++m_ref; }
    ULONG Release() override { LONG r = --m_ref; if (r <= 0) { delete this; return 0; } return (ULONG)r; }

    HRESULT Lock(UINT Offset, UINT SizeToLock, void **ppbData, DWORD Flags) override {
        if (!ppbData) return D3DERR_INVALIDCALL;
        *ppbData = m_data.empty() ? NULL : (m_data.data() + Offset);
        markRange(Offset, SizeToLock, Flags);
        return D3D_OK;
    }
    //  The range recorded at Lock is what goes up; a read-only lock leaves the
    //  GPU copy alone entirely. Anything that wrote without telling us a range
    //  still gets the whole buffer, so no path can end up with GL holding
    //  nothing.
    HRESULT Unlock() override {
        if (m_glDirty && m_dirtyEnd <= m_dirtyBegin) { m_dirtyBegin = 0; m_dirtyEnd = m_length; }
        return D3D_OK;
    }
    HRESULT GetDesc(D3DVERTEXBUFFER_DESC *pDesc) override {
        if (!pDesc) return D3DERR_INVALIDCALL;
        memset(pDesc, 0, sizeof(*pDesc));
        pDesc->Format = D3DFMT_VERTEXDATA; pDesc->Type = D3DRTYPE_VERTEXBUFFER;
        pDesc->Usage = m_usage; pDesc->Pool = D3DPOOL_MANAGED;
        pDesc->Size = m_length; pDesc->FVF = m_fvf;
        return D3D_OK;
    }

    RAN_D3D9_STUBS_IDIRECT3DVERTEXBUFFER9
};

class RanIndexBuffer : public IDirect3DIndexBuffer9 {
public:
    LONG m_ref = 1;
    UINT m_length; D3DFORMAT m_format; DWORD m_usage;
    std::vector<BYTE> m_data;

    unsigned m_glBuffer = 0;
    bool     m_glDirty = true;

    RanIndexBuffer(UINT len, DWORD usage, D3DFORMAT fmt)
        : m_length(len), m_format(fmt), m_usage(usage) { m_data.resize(len); }
    ~RanIndexBuffer() { if (m_glBuffer) RanGLR_DeleteBuffer(m_glBuffer); }

    UINT m_dirtyBegin = 0, m_dirtyEnd = 0;
    bool m_glCreated = false;

    void markRange(UINT offset, UINT size, DWORD flags) {
        if (flags & D3DLOCK_READONLY) return;   // nothing will change
        if (size == 0 || offset + size > m_length) { size = m_length - (offset < m_length ? offset : m_length); }
        if (!size) return;
        if (m_dirtyEnd <= m_dirtyBegin) { m_dirtyBegin = offset; m_dirtyEnd = offset + size; }
        else {
            if (offset < m_dirtyBegin) m_dirtyBegin = offset;
            if (offset + size > m_dirtyEnd) m_dirtyEnd = offset + size;
        }
        m_glDirty = true;
    }

    unsigned GlBuffer() {
        if (m_data.empty()) return 0;
        if (!m_glBuffer) m_glBuffer = RanGLR_CreateBuffer();
        if (m_glBuffer && m_glDirty) {
            //  Index buffers go up whole. They are small, they change
            //  rarely, and a partial write into a buffer that is bound as the
            //  vertex array object's element source is exactly what the
            //  emulator's GL encoder mishandles - it then treats the draw as
            //  client-side arrays and dereferences the index offset as a
            //  pointer. The traffic worth cutting is in the vertex buffers.
            RanGLR_UpdateBuffer(m_glBuffer, 1, m_data.data(), (unsigned)m_data.size());
            m_glCreated = true;
            m_dirtyBegin = m_dirtyEnd = 0;
            m_glDirty = false;
        }
        return m_glBuffer;
    }

    HRESULT QueryInterface(REFIID, void **ppv) override { *ppv = this; AddRef(); return S_OK; }
    ULONG AddRef() override { return (ULONG)++m_ref; }
    ULONG Release() override { LONG r = --m_ref; if (r <= 0) { delete this; return 0; } return (ULONG)r; }

    HRESULT Lock(UINT Offset, UINT SizeToLock, void **ppbData, DWORD Flags) override {
        if (!ppbData) return D3DERR_INVALIDCALL;
        *ppbData = m_data.empty() ? NULL : (m_data.data() + Offset);
        markRange(Offset, SizeToLock, Flags);
        return D3D_OK;
    }
    HRESULT Unlock() override {
        if (m_glDirty && m_dirtyEnd <= m_dirtyBegin) { m_dirtyBegin = 0; m_dirtyEnd = m_length; }
        return D3D_OK;
    }
    HRESULT GetDesc(D3DINDEXBUFFER_DESC *pDesc) override {
        if (!pDesc) return D3DERR_INVALIDCALL;
        memset(pDesc, 0, sizeof(*pDesc));
        pDesc->Format = m_format; pDesc->Type = D3DRTYPE_INDEXBUFFER;
        pDesc->Usage = m_usage; pDesc->Pool = D3DPOOL_MANAGED; pDesc->Size = m_length;
        return D3D_OK;
    }

    RAN_D3D9_STUBS_IDIRECT3DINDEXBUFFER9
};

class RanDevice;

//  Return addresses of the callers, as offsets inside this library. Printing
//  raw addresses would be useless once the loader has relocated us, so each one
//  is reduced to a file offset that llvm-symbolizer can resolve directly.
static _Unwind_Reason_Code ranTraceStep(struct _Unwind_Context *ctx, void *arg);

struct RanTraceState { void **frames; int count, max; };

static _Unwind_Reason_Code ranTraceStep(struct _Unwind_Context *ctx, void *arg) {
    RanTraceState *st = (RanTraceState *)arg;
    const uintptr_t pc = _Unwind_GetIP(ctx);
    if (pc) {
        if (st->count >= st->max) return _URC_END_OF_STACK;
        st->frames[st->count++] = (void *)pc;
    }
    return _URC_NO_REASON;
}

extern "C" void RanDiag_Backtrace(char *out, size_t cap);
void RanDiag_Backtrace(char *out, size_t cap) {
    void *frames[16];
    RanTraceState st = { frames, 0, 16 };
    _Unwind_Backtrace(&ranTraceStep, &st);

    size_t at = 0;
    out[0] = 0;
    for (int i = 0; i < st.count && at + 24 < cap; ++i) {
        Dl_info info;
        if (!dladdr(frames[i], &info) || !info.dli_fname) continue;
        //  Only frames inside this library can be symbolized against it.
        if (!strstr(info.dli_fname, "libran.so")) continue;
        const uintptr_t off = (uintptr_t)frames[i] - (uintptr_t)info.dli_fbase;
        at += (size_t)snprintf(out + at, cap - at, " %lx", (unsigned long)off);
    }
}

// State blocks record every state touched between Begin/EndStateBlock and
// replay it on Apply — the engine uses 146 of them, so this has to work even
// headless or its render state diverges.
class RanStateBlock : public IDirect3DStateBlock9 {
public:
    LONG m_ref = 1;
    RanDevice *m_device;
    struct RS { D3DRENDERSTATETYPE t; DWORD v; };
    struct TSS { DWORD stage; D3DTEXTURESTAGESTATETYPE t; DWORD v; };
    struct SS { DWORD sampler; D3DSAMPLERSTATETYPE t; DWORD v; };
    std::vector<RS> m_rs;
    std::vector<TSS> m_tss;
    std::vector<SS> m_ss;

    explicit RanStateBlock(RanDevice *d) : m_device(d) {}

    HRESULT QueryInterface(REFIID, void **ppv) override { *ppv = this; AddRef(); return S_OK; }
    ULONG AddRef() override { return (ULONG)++m_ref; }
    ULONG Release() override { LONG r = --m_ref; if (r <= 0) { delete this; return 0; } return (ULONG)r; }
    HRESULT Capture() override;
    HRESULT Apply() override;

    RAN_D3D9_STUBS_IDIRECT3DSTATEBLOCK9
};

// ---------------------------------------------------------------- device
//  One screen, so a swap chain is a thin view onto the device's back buffer.
class RanSwapChain : public IDirect3DSwapChain9 {
public:
    LONG m_ref;
    IDirect3DDevice9 *m_device;
    D3DPRESENT_PARAMETERS m_params;

    RanSwapChain(IDirect3DDevice9 *dev, const D3DPRESENT_PARAMETERS *pp)
        : m_ref(1), m_device(dev) {
        memset(&m_params, 0, sizeof(m_params));
        if (pp) m_params = *pp;
        if (m_device) m_device->AddRef();
    }
    ~RanSwapChain() { if (m_device) m_device->Release(); }

    HRESULT __stdcall QueryInterface(REFIID, void **ppv) { *ppv = this; AddRef(); return S_OK; }
    ULONG   __stdcall AddRef() { return (ULONG)++m_ref; }
    ULONG   __stdcall Release() { LONG r = --m_ref; if (r <= 0) { delete this; return 0; } return (ULONG)r; }

    //  The device presents the frame; a per-viewport present would flip twice.
    HRESULT __stdcall Present(const RECT *, const RECT *, HWND, const RGNDATA *, DWORD) {
        return D3D_OK;
    }
    HRESULT __stdcall GetFrontBufferData(IDirect3DSurface9 *) { return D3DERR_NOTAVAILABLE; }
    HRESULT __stdcall GetBackBuffer(UINT i, D3DBACKBUFFER_TYPE type, IDirect3DSurface9 **ppBB) {
        if (!m_device) return D3DERR_INVALIDCALL;
        return m_device->GetBackBuffer(0, i, type, ppBB);
    }
    HRESULT __stdcall GetRasterStatus(D3DRASTER_STATUS *pStatus) {
        if (pStatus) { pStatus->InVBlank = FALSE; pStatus->ScanLine = 0; }
        return D3D_OK;
    }
    HRESULT __stdcall GetDisplayMode(D3DDISPLAYMODE *pMode) {
        if (!pMode) return D3DERR_INVALIDCALL;
        RECT r; GetClientRect(NULL, &r);
        pMode->Width = (UINT)(r.right - r.left);
        pMode->Height = (UINT)(r.bottom - r.top);
        pMode->RefreshRate = 60;
        pMode->Format = D3DFMT_X8R8G8B8;
        return D3D_OK;
    }
    HRESULT __stdcall GetDevice(IDirect3DDevice9 **ppDevice) {
        if (!ppDevice) return D3DERR_INVALIDCALL;
        *ppDevice = m_device;
        if (m_device) m_device->AddRef();
        return D3D_OK;
    }
    HRESULT __stdcall GetPresentParameters(D3DPRESENT_PARAMETERS *pp) {
        if (!pp) return D3DERR_INVALIDCALL;
        *pp = m_params;
        return D3D_OK;
    }
};

class RanDevice : public IDirect3DDevice9 {
public:
    LONG m_ref = 1;
    IDirect3D9 *m_d3d;
    D3DPRESENT_PARAMETERS m_pp;

    // Fixed-function state — this is what the GLES backend will consume.
    DWORD m_renderState[256];
    DWORD m_textureStageState[8][33];
    DWORD m_samplerState[16][14];
    D3DMATRIX m_transform[512];
    //  Bumped by SetTransform/MultiplyTransform; everything derived from the
    //  transforms is rebuilt against it.
    DWORD m_transformGeneration = 1;
    IDirect3DBaseTexture9 *m_texture[16];
    D3DLIGHT9 m_light[16];
    BOOL m_lightEnabled[16];
    D3DMATERIAL9 m_material;
    D3DVIEWPORT9 m_viewport;
    DWORD m_fvf = 0;
    IDirect3DVertexBuffer9 *m_stream0 = NULL;
    UINT m_stream0Stride = 0;
    UINT m_stream0Offset = 0;
    IDirect3DIndexBuffer9 *m_indices = NULL;
    bool m_inScene = false;
    RanStateBlock *m_recording = NULL;

    RanDevice(IDirect3D9 *d3d, const D3DPRESENT_PARAMETERS &pp) : m_d3d(d3d), m_pp(pp) {
        memset(m_renderState, 0, sizeof(m_renderState));
        memset(m_textureStageState, 0, sizeof(m_textureStageState));
        memset(m_samplerState, 0, sizeof(m_samplerState));
        memset(m_texture, 0, sizeof(m_texture));
        memset(m_light, 0, sizeof(m_light));
        memset(m_lightEnabled, 0, sizeof(m_lightEnabled));
        memset(&m_material, 0, sizeof(m_material));
        for (int i = 0; i < 512; ++i) D3DXMatrixIdentity((D3DXMATRIX *)&m_transform[i]);
        memset(&m_viewport, 0, sizeof(m_viewport));
        m_viewport.Width = pp.BackBufferWidth;
        m_viewport.Height = pp.BackBufferHeight;
        m_viewport.MaxZ = 1.0f;

        // D3D9 defaults the engine relies on being right after device creation.
        m_renderState[D3DRS_ZENABLE] = D3DZB_TRUE;
        m_renderState[D3DRS_CULLMODE] = D3DCULL_CCW;
        m_renderState[D3DRS_LIGHTING] = TRUE;
        m_renderState[D3DRS_ALPHABLENDENABLE] = FALSE;
        m_renderState[D3DRS_SRCBLEND] = D3DBLEND_ONE;
        m_renderState[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
        //  D3D starts this at opaque white; a stage that selects TFACTOR before
        //  the engine sets one must not read zero.
        m_renderState[D3DRS_TEXTUREFACTOR] = 0xFFFFFFFF;
        makeFrameBuffers();
        LOGI("device created %ux%u", pp.BackBufferWidth, pp.BackBufferHeight);
    }

    HRESULT QueryInterface(REFIID, void **ppv) override { *ppv = this; AddRef(); return S_OK; }
    ULONG AddRef() override { return (ULONG)++m_ref; }
    ULONG Release() override { LONG r = --m_ref; if (r <= 0) { delete this; return 0; } return (ULONG)r; }

    HRESULT GetDirect3D(IDirect3D9 **ppD3D9) override {
        if (!ppD3D9) return D3DERR_INVALIDCALL;
        m_d3d->AddRef();
        *ppD3D9 = m_d3d;
        LOGI("GetDirect3D -> %p", (void *)m_d3d);
        return D3D_OK;
    }
    HRESULT TestCooperativeLevel() override { return D3D_OK; }
    HRESULT GetDeviceCaps(D3DCAPS9 *pCaps) override;

    HRESULT Reset(D3DPRESENT_PARAMETERS *pPP) override {
        if (pPP) m_pp = *pPP;
        makeFrameBuffers();
        LOGI("device reset %ux%u", m_pp.BackBufferWidth, m_pp.BackBufferHeight);
        return D3D_OK;
    }

    // The framework asks for the back buffer immediately after CreateDevice and
    // reads its description, so these are real surfaces from the start rather
    // than stubs. Phase 3 backs them with the EGL surface and an FBO.
    RanSurface *m_backBuffer = NULL;
    RanSurface *m_depthBuffer = NULL;
    RanSurface *m_renderTarget = NULL;

    void makeFrameBuffers() {
        if (m_backBuffer) { m_backBuffer->Release(); m_backBuffer = NULL; }
        if (m_depthBuffer) { m_depthBuffer->Release(); m_depthBuffer = NULL; }
        UINT w = m_pp.BackBufferWidth ? m_pp.BackBufferWidth : 1;
        UINT h = m_pp.BackBufferHeight ? m_pp.BackBufferHeight : 1;
        D3DFORMAT bf = m_pp.BackBufferFormat ? m_pp.BackBufferFormat : D3DFMT_X8R8G8B8;
        D3DFORMAT df = m_pp.AutoDepthStencilFormat ? m_pp.AutoDepthStencilFormat : D3DFMT_D24S8;
        m_backBuffer = new RanSurface(this, w, h, bf);
        m_depthBuffer = new RanSurface(this, w, h, df);
        m_renderTarget = m_backBuffer;
    }

    HRESULT CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS *pParams,
                                      IDirect3DSwapChain9 **ppSwapChain) override {
        if (!ppSwapChain) return D3DERR_INVALIDCALL;
        *ppSwapChain = new RanSwapChain(this, pParams);
        return D3D_OK;
    }
    HRESULT GetBackBuffer(UINT, UINT, D3DBACKBUFFER_TYPE, IDirect3DSurface9 **ppBB) override {
        if (!ppBB) return D3DERR_INVALIDCALL;
        if (!m_backBuffer) makeFrameBuffers();
        m_backBuffer->AddRef();
        *ppBB = m_backBuffer;
        return D3D_OK;
    }
    HRESULT GetRenderTarget(DWORD idx, IDirect3DSurface9 **ppRT) override {
        if (!ppRT || idx != 0) return D3DERR_INVALIDCALL;
        if (!m_renderTarget) makeFrameBuffers();
        m_renderTarget->AddRef();
        *ppRT = m_renderTarget;
        return D3D_OK;
    }
    HRESULT SetRenderTarget(DWORD idx, IDirect3DSurface9 *pRT) override {
        if (idx != 0) return D3DERR_INVALIDCALL;
        flushUIBatch();
        if (!m_backBuffer) makeFrameBuffers();
        RanSurface *s = pRT ? (RanSurface *)pRT : m_backBuffer;
        m_renderTarget = s;
        if (s == m_backBuffer) {
            RanGLR_SetRenderTargetTexture(0, 0, 0);
        } else {
            //  Which off-screen passes actually run. Enabling the surface-texture
            //  chain turned several of these on for the first time, and a pass
            //  that renders is the difference between an effect and a blank.
            {
                static std::set<unsigned> s_said;
                const unsigned key = (s->m_width << 16) | s->m_height;
                if (s_said.size() < 24 && s_said.insert(key).second)
                    LOGI("off-screen pass renders into a %ux%u target", s->m_width, s->m_height);
            }
            RanGLR_SetRenderTargetTexture(s->RenderTargetTexture(),
                                          (int)s->m_width, (int)s->m_height);
        }
        return D3D_OK;
    }
    HRESULT GetDepthStencilSurface(IDirect3DSurface9 **ppDS) override {
        if (!ppDS) return D3DERR_INVALIDCALL;
        if (!m_depthBuffer) makeFrameBuffers();
        m_depthBuffer->AddRef();
        *ppDS = m_depthBuffer;
        return D3D_OK;
    }
    //  A NULL depth surface turns depth testing off for everything that follows
    //  - it is how the engine draws into an off-screen texture that has no depth
    //  of its own. The shim keeps a depth attachment on every render-target FBO,
    //  and nothing clears it, so ignoring this rejected the draw and left the
    //  texture black: item previews and the glow and burn passes all render this
    //  way, which is why item icons came out as black squares.
    HRESULT SetDepthStencilSurface(IDirect3DSurface9 *pDS) override {
        const bool none = (pDS == NULL);
        if (none != m_noDepthSurface) {
            flushUIBatch();
            m_noDepthSurface = none;
            ++m_stateEpoch;
        }
        return D3D_OK;
    }
    bool m_noDepthSurface = false;

    //  D3D StretchRect. The engine builds its off-screen chain out of these -
    //  refraction, glow, reflection - and it asks the caps whether they work
    //  before it keeps any of those surfaces at all, so a copy that quietly
    //  does nothing is worse than none: it costs the whole feature.
    //  DxSurfaceTex reads this off the stack, so an untouched struct is a bug
    //  waiting to be blamed on something else.
    //  TEMPORARY probe: does anything set a user clip plane? Water reflection
    //  clipping is the suspected user, but the generated stub has always
    //  swallowed these silently.
    HRESULT SetClipPlane(DWORD Index, CONST float *pPlane) override {
        static DWORD seen = 0;
        if (Index < 32 && !(seen & (1u << Index))) {
            seen |= (1u << Index);
            LOGI("FFPROBE: SetClipPlane %lu = [%.3f %.3f %.3f %.3f]",
                 (unsigned long)Index,
                 pPlane ? pPlane[0] : 0.0f, pPlane ? pPlane[1] : 0.0f,
                 pPlane ? pPlane[2] : 0.0f, pPlane ? pPlane[3] : 0.0f);
        }
        return D3D_OK;
    }

    HRESULT GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS *pParameters) override {
        if (!pParameters) return D3DERR_INVALIDCALL;
        pParameters->AdapterOrdinal = 0;
        pParameters->DeviceType = D3DDEVTYPE_HAL;
        pParameters->hFocusWindow = NULL;
        pParameters->BehaviorFlags = D3DCREATE_HARDWARE_VERTEXPROCESSING;
        return D3D_OK;
    }

    HRESULT StretchRect(IDirect3DSurface9 *pSrc, const RECT *pSrcRect,
                        IDirect3DSurface9 *pDst, const RECT *pDstRect,
                        D3DTEXTUREFILTERTYPE Filter) override {
        return StretchRectImpl(pSrc, pSrcRect, pDst, pDstRect, Filter);
    }

    HRESULT StretchRectImpl(IDirect3DSurface9 *pSrc, const RECT *pSrcRect,
                            IDirect3DSurface9 *pDst, const RECT *pDstRect,
                            D3DTEXTUREFILTERTYPE Filter) {
        if (!pSrc) return D3DERR_INVALIDCALL;
        RanSurface *src = (RanSurface *)pSrc;
        RanSurface *dst = pDst ? (RanSurface *)pDst : m_backBuffer;
        if (!dst) { makeFrameBuffers(); dst = m_backBuffer; }
        if (!dst) return D3DERR_INVALIDCALL;

        flushUIBatch();

        const int sw = (int)src->m_width, sh = (int)src->m_height;
        const int dw = (int)dst->m_width, dh = (int)dst->m_height;
        int sx0 = 0, sy0 = 0, sx1 = sw, sy1 = sh;
        int dx0 = 0, dy0 = 0, dx1 = dw, dy1 = dh;
        if (pSrcRect) { sx0 = pSrcRect->left; sy0 = pSrcRect->top; sx1 = pSrcRect->right; sy1 = pSrcRect->bottom; }
        if (pDstRect) { dx0 = pDstRect->left; dy0 = pDstRect->top; dx1 = pDstRect->right; dy1 = pDstRect->bottom; }

        //  D3D counts rows from the top and GL from the bottom.
        const int syA = sh - sy1, syB = sh - sy0;
        const int dyA = dh - dy1, dyB = dh - dy0;

        const unsigned dstTex = (dst == m_backBuffer) ? 0u : dst->RenderTargetTexture();
        RanGLR_BlitTexture(src->RenderTargetTexture(), sx0, syA, sx1, syB,
                           dstTex, dx0, dyA, dx1, dyB,
                           Filter == D3DTEXF_LINEAR ? 1 : 0);
        return D3D_OK;
    }
    HRESULT CreateRenderTarget(UINT w, UINT h, D3DFORMAT fmt, D3DMULTISAMPLE_TYPE, DWORD,
                               BOOL, IDirect3DSurface9 **ppSurf, HANDLE *) override {
        if (!ppSurf) return D3DERR_INVALIDCALL;
        *ppSurf = new RanSurface(this, w, h, fmt);
        return D3D_OK;
    }
    HRESULT CreateDepthStencilSurface(UINT w, UINT h, D3DFORMAT fmt, D3DMULTISAMPLE_TYPE, DWORD,
                                      BOOL, IDirect3DSurface9 **ppSurf, HANDLE *) override {
        if (!ppSurf) return D3DERR_INVALIDCALL;
        *ppSurf = new RanSurface(this, w, h, fmt);
        return D3D_OK;
    }
    HRESULT CreateOffscreenPlainSurface(UINT w, UINT h, D3DFORMAT fmt, D3DPOOL,
                                        IDirect3DSurface9 **ppSurf, HANDLE *) override {
        if (!ppSurf) return D3DERR_INVALIDCALL;
        *ppSurf = new RanSurface(this, w, h, fmt);
        return D3D_OK;
    }
    HRESULT GetDisplayMode(UINT, D3DDISPLAYMODE *pMode) override {
        if (!pMode) return D3DERR_INVALIDCALL;
        pMode->Width = m_pp.BackBufferWidth;
        pMode->Height = m_pp.BackBufferHeight;
        pMode->RefreshRate = 60;
        pMode->Format = m_pp.BackBufferFormat ? m_pp.BackBufferFormat : D3DFMT_X8R8G8B8;
        return D3D_OK;
    }

    HRESULT BeginScene() override { m_inScene = true; return D3D_OK; }
    HRESULT EndScene() override {
        //  Last ordering point in the frame: nothing queued may outlive it.
        flushUIBatch();
        m_inScene = false;
        return D3D_OK;
    }
    HRESULT Clear(DWORD Count, const D3DRECT *pRects, DWORD Flags, D3DCOLOR Color,
                  float Z, DWORD Stencil) override {
        flushUIBatch();
        ++g_stats.clears;
        //  Count == 0 means the whole target; anything else clears just those
        //  rectangles, which is how a viewport clears its own area without
        //  erasing what the rest of the frame already drew.
        if (!Count || !pRects) {
            RanGLR_Clear(Flags, Color, Z, Stencil);
            return D3D_OK;
        }
        for (DWORD i = 0; i < Count; ++i) {
            RanGLR_ClearRect((int)pRects[i].x1, (int)pRects[i].y1,
                             (int)(pRects[i].x2 - pRects[i].x1),
                             (int)(pRects[i].y2 - pRects[i].y1));
            RanGLR_Clear(Flags, Color, Z, Stencil);
        }
        RanGLR_ClearRectOff();
        return D3D_OK;
    }
    HRESULT Present(const RECT *, const RECT *, HWND, const RGNDATA *) override {
        flushUIBatch();
        ++g_stats.frames;
        {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            const double now = (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
            if (g_lastPresent > 0.0) g_frameSeconds += now - g_lastPresent;
            g_lastPresent = now;
        }
        RanGL_Present();
        // One census line every 5s at 60Hz — enough to see the scene change.
        if ((g_stats.frames % 300) == 0) {
            RanGLR_LogStats();
            LOGI("per frame: opaque %lu (%lu verts) | alpha %lu (%lu) | skinned %lu (%lu) | ui %lu",
                 g_buckets.opaqueWorld / 300, g_buckets.vertsOpaque / 300,
                 g_buckets.alphaBlended / 300, g_buckets.vertsAlpha / 300,
                 g_buckets.skinned / 300, g_buckets.vertsSkinned / 300,
                 g_buckets.uiQuads / 300);
            memset(&g_buckets, 0, sizeof(g_buckets));
            const double draws = RanGLR_TakeDrawSeconds();
            LOGI("frame budget: %.1f ms total, %.1f ms submitting draws (%.0f%%)"
                 "  [draw timing needs RAN_TIME_DRAWS]",
                 g_frameSeconds * 1000.0 / 300.0, draws * 1000.0 / 300.0,
                 g_frameSeconds > 0.0 ? draws * 100.0 / g_frameSeconds : 0.0);
            g_frameSeconds = 0.0;
        }
        if ((g_stats.frames % 100000) == 0)
            LOGI("frame %lu — draws %lu, clears %lu, tex %lu (%.1f MB), vb %lu, ib %lu",
                 g_stats.frames, g_stats.draws, g_stats.clears, g_stats.texturesCreated,
                 g_stats.textureBytes / 1048576.0, g_stats.vbCreated, g_stats.ibCreated);
        return D3D_OK;
    }

    // ---- render state ----
    //  A batch may only span draws that share every piece of state, so any
    //  change to that state ends it. Deferring past a change is not a
    //  reordering the client can see coming: DxSimpleMesh switches stage 0 to
    //  SELECTARG2 for a subset whose texture is missing, draws it, and puts
    //  MODULATE back - and a batch flushed after that restore drew the subset
    //  modulating a texture that was never bound, as a sheet of white.
    HRESULT SetRenderState(D3DRENDERSTATETYPE State, DWORD Value) override {
        //  TEMPORARY probe: is vertex specular ever turned on?
        if (State == D3DRS_SPECULARENABLE && Value) {
            static bool said = false;
            if (!said) { said = true; LOGI("FFPROBE: D3DRS_SPECULARENABLE turned ON"); }
        }
        //  Recording captures without touching the device — see the note on
        //  BeginStateBlock. Nothing is flushed and no epoch moves, because no
        //  state actually changes.
        if (m_recording) {
            if (State < 256) m_recording->m_rs.push_back({State, Value});
            return D3D_OK;
        }
        if (m_uiBatch.active && m_renderState[State < 256 ? State : 0] != Value) flushUIBatch();
        ++m_stateEpoch;
        if (State < 256) m_renderState[State] = Value;
        return D3D_OK;
    }
    HRESULT GetRenderState(D3DRENDERSTATETYPE State, DWORD *pValue) override {
        if (!pValue) return D3DERR_INVALIDCALL;
        *pValue = (State < 256) ? m_renderState[State] : 0;
        return D3D_OK;
    }
    HRESULT SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) override {
        if (m_recording) {
            if (Stage < 8 && Type < 33) m_recording->m_tss.push_back({Stage, Type, Value});
            return D3D_OK;
        }
        //  TEMPORARY probe: which texture-coordinate generation and transform
        //  flags the engine actually asks for. Only CAMERASPACENORMAL is
        //  implemented; the question is whether anything else is ever used.
        if (Type == D3DTSS_TEXCOORDINDEX && (Value & 0xFFFF0000)) {
            static DWORD seen = 0;
            if (!(seen & (1u << ((Value >> 16) & 31)))) {
                seen |= (1u << ((Value >> 16) & 31));
                LOGI("FFPROBE: stage %lu TEXCOORDINDEX gen 0x%lX",
                     (unsigned long)Stage, (unsigned long)(Value & 0xFFFF0000));
            }
        }
        if (Type == D3DTSS_TEXTURETRANSFORMFLAGS && Value) {
            static DWORD seenT = 0;
            if (!(seenT & (1u << (Value & 31)))) {
                seenT |= (1u << (Value & 31));
                LOGI("FFPROBE: stage %lu TEXTURETRANSFORMFLAGS 0x%lX%s",
                     (unsigned long)Stage, (unsigned long)Value,
                     (Value & D3DTTFF_PROJECTED) ? "  (PROJECTED)" : "");
            }
        }
        if (m_uiBatch.active && Stage < 8 && Type < 33 &&
            m_textureStageState[Stage][Type] != Value) flushUIBatch();
        ++m_stateEpoch;
        if (Stage < 8 && Type < 33) m_textureStageState[Stage][Type] = Value;
        return D3D_OK;
    }
    HRESULT GetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD *pValue) override {
        if (!pValue) return D3DERR_INVALIDCALL;
        *pValue = (Stage < 8 && Type < 33) ? m_textureStageState[Stage][Type] : 0;
        return D3D_OK;
    }
    HRESULT SetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value) override {
        if (m_recording) {
            if (Sampler < 16 && Type < 14) m_recording->m_ss.push_back({Sampler, Type, Value});
            return D3D_OK;
        }
        if (m_uiBatch.active && Sampler < 16 && Type < 14 &&
            m_samplerState[Sampler][Type] != Value) flushUIBatch();
        ++m_stateEpoch;
        if (Sampler < 16 && Type < 14) m_samplerState[Sampler][Type] = Value;
        return D3D_OK;
    }
    HRESULT GetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD *pValue) override {
        if (!pValue) return D3DERR_INVALIDCALL;
        *pValue = (Sampler < 16 && Type < 14) ? m_samplerState[Sampler][Type] : 0;
        return D3D_OK;
    }

    // ---- transforms ----
    HRESULT SetTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX *pMat) override {
        if (pMat && (DWORD)State < 512) {
            if (memcmp(&m_transform[State], pMat, sizeof(D3DMATRIX)) != 0) {
                if (m_uiBatch.active) flushUIBatch();
                        m_transform[State] = *pMat;
                ++m_transformGeneration;
            }
        }
        return D3D_OK;
    }
    HRESULT GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX *pMat) override {
        if (!pMat) return D3DERR_INVALIDCALL;
        if ((DWORD)State < 512) *pMat = m_transform[State];
        else D3DXMatrixIdentity((D3DXMATRIX *)pMat);
        return D3D_OK;
    }
    HRESULT MultiplyTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX *pMat) override {
        if (pMat && (DWORD)State < 512) ++m_transformGeneration;
        if (pMat && (DWORD)State < 512)
            D3DXMatrixMultiply((D3DXMATRIX *)&m_transform[State],
                               (const D3DXMATRIX *)&m_transform[State], (const D3DXMATRIX *)pMat);
        return D3D_OK;
    }

    // ---- bindings ----
    HRESULT SetTexture(DWORD Stage, IDirect3DBaseTexture9 *pTex) override {
        if (m_uiBatch.active && Stage < 16 && m_texture[Stage] != pTex) flushUIBatch();
        if (Stage < 16) m_texture[Stage] = pTex;
        return D3D_OK;
    }
    HRESULT GetTexture(DWORD Stage, IDirect3DBaseTexture9 **ppTex) override {
        if (!ppTex) return D3DERR_INVALIDCALL;
        *ppTex = (Stage < 16) ? m_texture[Stage] : NULL;
        if (*ppTex) (*ppTex)->AddRef();
        return D3D_OK;
    }
    HRESULT SetFVF(DWORD FVF) override { m_fvf = FVF; return D3D_OK; }
    HRESULT GetFVF(DWORD *pFVF) override { if (pFVF) *pFVF = m_fvf; return D3D_OK; }
    HRESULT SetStreamSource(UINT n, IDirect3DVertexBuffer9 *pVB, UINT OffsetInBytes, UINT Stride) override {
        // The byte offset is NOT optional: the engine packs several meshes into one
        // buffer and distinguishes them by it. Ignoring it reads another mesh.
        if (n == 0) {
            if (m_stream0 != pVB || m_stream0Stride != Stride || m_stream0Offset != OffsetInBytes)
                    m_stream0 = pVB; m_stream0Stride = Stride; m_stream0Offset = OffsetInBytes;
        }
        return D3D_OK;
    }
    HRESULT SetIndices(IDirect3DIndexBuffer9 *pIB) override { m_indices = pIB; return D3D_OK; }

    HRESULT SetViewport(const D3DVIEWPORT9 *pVp) override {
        if (pVp) {
            m_viewport = *pVp;
            RanGLR_SetViewport((int)pVp->X, (int)pVp->Y, (int)pVp->Width, (int)pVp->Height);
        }
        return D3D_OK;
    }
    HRESULT GetViewport(D3DVIEWPORT9 *pVp) override {
        if (!pVp) return D3DERR_INVALIDCALL;
        *pVp = m_viewport; return D3D_OK;
    }
    HRESULT SetLight(DWORD Index, const D3DLIGHT9 *pLight) override {
        if (Index < 16 && pLight) m_light[Index] = *pLight;
        ++m_stateEpoch;
        return D3D_OK;
    }
    HRESULT GetLight(DWORD Index, D3DLIGHT9 *pLight) override {
        if (!pLight) return D3DERR_INVALIDCALL;
        if (Index < 16) *pLight = m_light[Index];
        return D3D_OK;
    }
    HRESULT LightEnable(DWORD Index, BOOL Enable) override {
        if (Index < 16) m_lightEnabled[Index] = Enable;
        ++m_stateEpoch;
        return D3D_OK;
    }
    HRESULT GetLightEnable(DWORD Index, BOOL *pEnable) override {
        if (!pEnable) return D3DERR_INVALIDCALL;
        *pEnable = (Index < 16) ? m_lightEnabled[Index] : FALSE;
        return D3D_OK;
    }
    HRESULT SetMaterial(const D3DMATERIAL9 *pMat) override {
        if (pMat) m_material = *pMat;
        ++m_stateEpoch;
        return D3D_OK;
    }
    HRESULT GetMaterial(D3DMATERIAL9 *pMat) override {
        if (!pMat) return D3DERR_INVALIDCALL;
        *pMat = m_material; return D3D_OK;
    }

    // ---- draws (counted now, submitted in phase 3) ----
    // The world*view*proj the fixed-function pipeline would have applied.
    // Row-vector D3D order: world, then view, then projection.
    //  world * view * projection, the view-projection alone, and the camera
    //  position all follow from the transforms; rebuild them only when one of
    //  those changed rather than once per draw.
    DWORD      m_derivedGeneration = 0;
    D3DXMATRIX m_mvp, m_viewProj;
    float      m_cameraPos[3];

    void refreshDerived() {
        if (m_derivedGeneration == m_transformGeneration) return;
        m_derivedGeneration = m_transformGeneration;

        D3DXMatrixMultiply(&m_mvp, (const D3DXMATRIX *)&m_transform[D3DTS_WORLD],
                                   (const D3DXMATRIX *)&m_transform[D3DTS_VIEW]);
        D3DXMatrixMultiply(&m_mvp, &m_mvp, (const D3DXMATRIX *)&m_transform[D3DTS_PROJECTION]);

        D3DXMatrixMultiply(&m_viewProj, (const D3DXMATRIX *)&m_transform[D3DTS_VIEW],
                                        (const D3DXMATRIX *)&m_transform[D3DTS_PROJECTION]);

        D3DXMATRIX invView;
        m_cameraPos[0] = m_cameraPos[1] = m_cameraPos[2] = 0.0f;
        if (D3DXMatrixInverse(&invView, NULL, (const D3DXMATRIX *)&m_transform[D3DTS_VIEW])) {
            m_cameraPos[0] = invView._41;
            m_cameraPos[1] = invView._42;
            m_cameraPos[2] = invView._43;
        }
    }

    void currentMVP(float *out16) {
        refreshDerived();
        const D3DXMATRIX &m = m_mvp;
        // NO transpose. D3D is row-vector (v * M) and stores rows contiguously;
        // GL is column-vector (M * v) and reads columns contiguously. Those are
        // the SAME bytes, which is what makes v * M_d3d equal M_gl * v.
        // Transposing here applies the convention change twice and hands the
        // shader the translation row where the projection column belongs — the
        // scene then draws as a fan of stretched triangles.
        memcpy(out16, &m, sizeof(float) * 16);
    }

    unsigned boundGlTexture() {
        if (!m_texture[0]) {
            //  Only worth reporting when the stage is actually going to sample.
            if (m_textureStageState[0][D3DTSS_COLORARG1] == D3DTA_TEXTURE ||
                m_textureStageState[0][D3DTSS_COLORARG2] == D3DTA_TEXTURE) {
                if (RanGLR_DiagArmed()) {
                    char szTrace[512];
                    RanDiag_Backtrace(szTrace, sizeof(szTrace));
                    LOGW("draw samples stage 0 with no texture bound "
                         "(colorop %lu arg1 %lu arg2 %lu) at%s",
                         (unsigned long)m_textureStageState[0][D3DTSS_COLOROP],
                         (unsigned long)m_textureStageState[0][D3DTSS_COLORARG1],
                         (unsigned long)m_textureStageState[0][D3DTSS_COLORARG2],
                         szTrace);
                }
            }
            return 0;
        }
        // Only 2D textures are wired for now; cube/volume come with the world pass.
        const unsigned gl = ((RanTexture *)m_texture[0])->GlTexture();
        if (!gl && RanGLR_DiagArmed()) {
            LOGW("texture %p is bound but has no GL name", (void *)m_texture[0]);
        }
        return gl;
    }

    // Elements an indexed draw consumes for a given primitive type.
    static UINT indexCountFor(D3DPRIMITIVETYPE type, UINT primCount) {
        switch (type) {
            case D3DPT_POINTLIST:     return primCount;
            case D3DPT_LINELIST:      return primCount * 2;
            case D3DPT_LINESTRIP:     return primCount + 1;
            case D3DPT_TRIANGLESTRIP:
            case D3DPT_TRIANGLEFAN:   return primCount + 2;
            default:                  return primCount * 3;
        }
    }

    // A DWORD render state that actually holds a float (fog start/end/density).
    static float asFloat(DWORD v) { float f; memcpy(&f, &v, sizeof(f)); return f; }

    //  What prepareDraw last pushed. Rebuilding the light block, the fog, the
    //  texture stage and the render state for a draw whose inputs have not
    //  changed is pure CPU, and the client issues hundreds of draws a frame
    //  between actual state changes - it costs the same on every device, which
    //  is why the emulator and the tablet were equally slow.
    DWORD m_pushedStateEpoch = 0, m_pushedTransformGen = 0;

    void pushLightingAndFog() {
        // Lighting is evaluated in world space, where the engine's lights live.
        float world[16];
        memcpy(world, &m_transform[D3DTS_WORLD], sizeof(world));

        // The camera position is the inverse of the view transform's translation.
        refreshDerived();
        const float *cam = m_cameraPos;

        DWORD amb = m_renderState[D3DRS_AMBIENT];
        float globalAmbient[3] = { ((amb >> 16) & 0xFF) / 255.0f,
                                   ((amb >> 8) & 0xFF) / 255.0f,
                                   (amb & 0xFF) / 255.0f };

        float matDiffuse[3]  = { m_material.Diffuse.r,  m_material.Diffuse.g,  m_material.Diffuse.b };
        float matAmbient[3]  = { m_material.Ambient.r,  m_material.Ambient.g,  m_material.Ambient.b };
        float matEmissive[3] = { m_material.Emissive.r, m_material.Emissive.g, m_material.Emissive.b };

        RanGlLight lights[8];
        int n = 0;
        for (int i = 0; i < 16 && n < 8; ++i) {
            if (!m_lightEnabled[i]) continue;
            const D3DLIGHT9 &L = m_light[i];
            RanGlLight &g = lights[n++];
            g.type = (int)L.Type;
            g.diffuse[0] = L.Diffuse.r; g.diffuse[1] = L.Diffuse.g; g.diffuse[2] = L.Diffuse.b;
            g.ambient[0] = L.Ambient.r; g.ambient[1] = L.Ambient.g; g.ambient[2] = L.Ambient.b;
            g.position[0] = L.Position.x; g.position[1] = L.Position.y; g.position[2] = L.Position.z;
            g.range = L.Range;
            g.direction[0] = L.Direction.x; g.direction[1] = L.Direction.y; g.direction[2] = L.Direction.z;
            g.atten[0] = L.Attenuation0; g.atten[1] = L.Attenuation1; g.atten[2] = L.Attenuation2;
        }

        { static int diag = 0; static int lastOn = -1;
          int on = m_renderState[D3DRS_LIGHTING] ? 1 : 0;
          static int lastN = -1;
          if (diag < 40 && (on != lastOn || n != lastN)) { ++diag; lastOn = on; lastN = n;
            __android_log_print(ANDROID_LOG_INFO, "RanLight", "lighting=%d lights=%d ambient=%.2f,%.2f,%.2f matDiff=%.2f,%.2f,%.2f matAmb=%.2f,%.2f,%.2f",
              on, n, globalAmbient[0], globalAmbient[1], globalAmbient[2],
              matDiffuse[0], matDiffuse[1], matDiffuse[2], matAmbient[0], matAmbient[1], matAmbient[2]); } }
        RanGLR_SetLighting(m_renderState[D3DRS_LIGHTING] ? 1 : 0, world, cam,
                           globalAmbient, matDiffuse, matAmbient, matEmissive, lights, n);

        // Table fog (per pixel) if the device asks for it, else vertex fog. Both
        // are evaluated per pixel here, which only ever looks better.
        DWORD mode = m_renderState[D3DRS_FOGTABLEMODE];
        if (mode == D3DFOG_NONE) mode = m_renderState[D3DRS_FOGVERTEXMODE];
        DWORD fc = m_renderState[D3DRS_FOGCOLOR];
        float fogColor[3] = { ((fc >> 16) & 0xFF) / 255.0f,
                              ((fc >> 8) & 0xFF) / 255.0f,
                              (fc & 0xFF) / 255.0f };
        { static int diag = 0; if (diag < 4) { ++diag;
          __android_log_print(ANDROID_LOG_INFO, "RanFog", "enable=%u mode=%u color=%.2f,%.2f,%.2f start=%.1f end=%.1f density=%.3f",
            (unsigned)m_renderState[D3DRS_FOGENABLE], (unsigned)mode, fogColor[0], fogColor[1], fogColor[2],
            asFloat(m_renderState[D3DRS_FOGSTART]), asFloat(m_renderState[D3DRS_FOGEND]), asFloat(m_renderState[D3DRS_FOGDENSITY])); } }
        RanGLR_SetFog(m_renderState[D3DRS_FOGENABLE] ? 1 : 0, (int)mode, fogColor,
                      asFloat(m_renderState[D3DRS_FOGSTART]),
                      asFloat(m_renderState[D3DRS_FOGEND]),
                      asFloat(m_renderState[D3DRS_FOGDENSITY]));
    }

    //  Skinned characters are drawn with the fixed-function blender: the bone
    //  palette arrives as D3DTS_WORLDMATRIX(0..3) and the weight count as
    //  D3DRS_VERTEXBLEND, so both have to reach the shader before the draw.
    void pushVertexBlend() {
        DWORD mode = m_renderState[D3DRS_VERTEXBLEND];
        int weights = 0;
        if (mode >= 1 && mode <= 3) {
            weights = (int)mode;                           // D3DVBF_1..3WEIGHTS
        } else if (mode != 0 && mode != D3DVBF_0WEIGHTS && mode != D3DVBF_TWEENING) {
            //  Not a mode D3D9 defines. Blending on three weights is the
            //  closest thing to right, and saying so once is what turns this
            //  from geometry quietly collapsing onto one bone into a bug report.
            static bool said = false;
            if (!said) {
                said = true;
                LOGW("D3DRS_VERTEXBLEND set to %lu, which D3D9 does not define "
                     "(the maximum is D3DVBF_3WEIGHTS = four matrices)", (unsigned long)mode);
            }
            weights = 3;
        }

        float palette[64];
        for (int i = 0; i < 4; ++i)
            memcpy(palette + i * 16, &m_transform[(DWORD)D3DTS_WORLDMATRIX(i)], sizeof(float) * 16);

        refreshDerived();
        RanGLR_SetVertexBlend(weights, palette, (const float *)&m_viewProj);
    }

    //  Stage 0's sampler: the client sets these from its own graphics options.
    void pushSampler() {
        const DWORD *s = m_samplerState[0];
        RanGLR_SetSampler(s[D3DSAMP_MINFILTER] ? s[D3DSAMP_MINFILTER] : 2,
                          s[D3DSAMP_MAGFILTER] ? s[D3DSAMP_MAGFILTER] : 2,
                          s[D3DSAMP_MIPFILTER],
                          s[D3DSAMP_ADDRESSU] ? s[D3DSAMP_ADDRESSU] : 1,
                          s[D3DSAMP_ADDRESSV] ? s[D3DSAMP_ADDRESSV] : 1,
                          s[D3DSAMP_MAXANISOTROPY]);
    }

    //  Consecutive pre-transformed draws that share a texture and a state epoch
    //  are accumulated here and submitted as one triangle list.
    struct UIBatch {
        std::vector<BYTE> verts;
        DWORD    fvf = 0;
        UINT     stride = 0;
        unsigned texture = 0;
        DWORD    epoch = 0;
        bool     active = false;
        float    mvp[16];
    } m_uiBatch;

    //  Bumped by anything a batched draw depends on.
    DWORD m_stateEpoch = 1;

    static bool isTriangleType(D3DPRIMITIVETYPE type) {
        return type == D3DPT_TRIANGLELIST || type == D3DPT_TRIANGLESTRIP ||
               type == D3DPT_TRIANGLEFAN;
    }

    //  Append a primitive as loose triangles, so batches of different primitive
    //  types can still merge.
    void appendTriangles(D3DPRIMITIVETYPE type, UINT primCount, const void *verts,
                         UINT stride, const void *indices, UINT indexBits) {
        const BYTE *v = (const BYTE *)verts;
        std::vector<BYTE> &out = m_uiBatch.verts;

        const UINT corners = primCount * 3;
        out.reserve(out.size() + (size_t)corners * stride);

        for (UINT i = 0; i < primCount; ++i) {
            UINT idx[3];
            switch (type) {
                case D3DPT_TRIANGLESTRIP:
                    //  Every other triangle is wound the other way; emit them in
                    //  a consistent order so one list draws them all.
                    idx[0] = (i & 1) ? i + 1 : i;
                    idx[1] = (i & 1) ? i : i + 1;
                    idx[2] = i + 2;
                    break;
                case D3DPT_TRIANGLEFAN:
                    idx[0] = 0; idx[1] = i + 1; idx[2] = i + 2;
                    break;
                default:
                    idx[0] = i * 3; idx[1] = i * 3 + 1; idx[2] = i * 3 + 2;
                    break;
            }
            for (int c = 0; c < 3; ++c) {
                UINT vi = idx[c];
                if (indices && indexBits == 16) vi = ((const WORD *)indices)[vi];
                else if (indices && indexBits == 32) vi = ((const DWORD *)indices)[vi];
                const BYTE *src = v + (size_t)vi * stride;
                out.insert(out.end(), src, src + stride);
            }
        }
    }

    //  Everything queued goes out as one draw.
    void flushUIBatch() {
        if (!m_uiBatch.active || m_uiBatch.verts.empty()) { m_uiBatch.active = false; return; }
        const UINT corners = (UINT)(m_uiBatch.verts.size() / m_uiBatch.stride);
        m_uiBatch.active = false;
        if (corners >= 3) {
            ++g_stats.draws;
            prepareDraw();
            RanGLR_Draw(D3DPT_TRIANGLELIST, corners / 3, m_uiBatch.verts.data(),
                        m_uiBatch.stride, m_uiBatch.fvf, m_uiBatch.texture, m_uiBatch.mvp,
                        NULL, 0, 0, corners);
        }
        m_uiBatch.verts.clear();
    }

    //  True when the draw was taken into the batch and needs no submission.
    bool batchUIDraw(D3DPRIMITIVETYPE type, UINT primCount, const void *verts, UINT stride,
                     DWORD fvf, const void *indices, UINT indexBits) {
        if (!primCount || !verts || !stride) return false;
        if (!isTriangleType(type)) return false;
        //  A skinned draw carries its own bone palette, so it cannot share a
        //  submission with anything else.
        if (m_renderState[D3DRS_VERTEXBLEND] != 0) return false;

        const unsigned tex = boundGlTexture();
        //  World geometry batches too, and it is not pre-transformed: two draws
        //  only merge if they are under the same matrix.
        float mvp[16];
        currentMVP(mvp);

        if (m_uiBatch.active &&
            (m_uiBatch.fvf != fvf || m_uiBatch.stride != stride ||
             m_uiBatch.texture != tex || m_uiBatch.epoch != m_stateEpoch ||
             memcmp(m_uiBatch.mvp, mvp, sizeof(mvp)) != 0)) {
            flushUIBatch();
        }

        if (!m_uiBatch.active) {
            m_uiBatch.fvf = fvf;
            m_uiBatch.stride = stride;
            m_uiBatch.texture = tex;
            m_uiBatch.epoch = m_stateEpoch;
            memcpy(m_uiBatch.mvp, mvp, sizeof(mvp));
            m_uiBatch.active = true;
            m_uiBatch.verts.clear();
        }

        appendTriangles(type, primCount, verts, stride, indices, indexBits);

        //  A batch that grows without bound would stall on the upload; a few
        //  thousand quads is far more than any single HUD run.
        if (m_uiBatch.verts.size() > (1u << 20)) flushUIBatch();
        return true;
    }

    //  Classify a draw for the census. Cheap: three state reads.
    void countDraw(DWORD fvf, UINT verts) {
        if ((fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW) { ++g_buckets.uiQuads; return; }
        if (m_renderState[D3DRS_VERTEXBLEND] != 0) { ++g_buckets.skinned; g_buckets.vertsSkinned += verts; return; }
        if (m_renderState[D3DRS_ALPHABLENDENABLE]) { ++g_buckets.alphaBlended; g_buckets.vertsAlpha += verts; return; }
        ++g_buckets.opaqueWorld; g_buckets.vertsOpaque += verts;
    }

    void prepareDraw() {
        //  Lighting is evaluated in world space, so a new transform means the
        //  block has to go again even when no state was touched.
        if (m_pushedStateEpoch == m_stateEpoch &&
            m_pushedTransformGen == m_transformGeneration) {
            pushVertexBlend();          // cheap, and the blend count changes per subset
            return;
        }
        m_pushedStateEpoch = m_stateEpoch;
        m_pushedTransformGen = m_transformGeneration;

        RanGLR_SetMaterialAlpha(m_material.Diffuse.a);
        pushSampler();
        pushVertexBlend();
        pushLightingAndFog();
        // Stage 0's combiner decides whether the texture, the vertex colour or a
        // product of them reaches the frame buffer. The terrain selects the
        // texture alone with black vertex colours, so ignoring this drew a black
        // ground under a scene that was otherwise correct.
        //  Stage 1. The character specular passes set it to MODULATE with the
        //  texture coordinates generated from the camera-space normal, and bind a
        //  cube map there. That one configuration is implemented; anything else is
        //  reported once rather than silently drawn wrong.
        {
            int mode = 0;
            unsigned cube = 0;
            const DWORD op   = m_textureStageState[1][D3DTSS_COLOROP];
            const DWORD arg1 = m_textureStageState[1][D3DTSS_COLORARG1];
            const DWORD arg2 = m_textureStageState[1][D3DTSS_COLORARG2];
            if (op != D3DTOP_DISABLE) {
                if (m_texture[1] && m_texture[1]->GetType() == D3DRTYPE_CUBETEXTURE) {
                    const DWORD tci = m_textureStageState[1][D3DTSS_TEXCOORDINDEX];
                    if (op == D3DTOP_MODULATE && tci == D3DTSS_TCI_CAMERASPACENORMAL) {
                        cube = RanD3D_CubeGlTexture((IDirect3DCubeTexture9 *)m_texture[1]);
                        mode = cube ? 1 : 0;
                    } else {
                        static bool said = false;
                        if (!said) {
                            said = true;
                            LOGW("stage 1 cube map with op %lu and texcoord source 0x%08lX "
                                 "is not implemented (only MODULATE with the camera-space "
                                 "normal is)", (unsigned long)op, (unsigned long)tci);
                        }
                    }
                } else if (!m_texture[1] && op == D3DTOP_MODULATE &&
                           ((arg1 == D3DTA_TFACTOR && arg2 == D3DTA_CURRENT) ||
                            (arg1 == D3DTA_CURRENT && arg2 == D3DTA_TFACTOR))) {
                    //  A flat tint with no texture: the ambient character effect
                    //  colours a whole piece this way.
                    mode = 2;
                } else if (!m_texture[1]) {
                    static bool said = false;
                    if (!said) {
                        said = true;
                        LOGW("stage 1 with no texture, op %lu args %lu/%lu is not implemented",
                             (unsigned long)op, (unsigned long)arg1, (unsigned long)arg2);
                    }
                }
            }
            RanGLR_SetStage1(mode, cube, (const float *)&m_transform[D3DTS_VIEW]);
        }

        RanGLR_SetTextureStage(m_textureStageState[0][D3DTSS_COLOROP],
                               m_textureStageState[0][D3DTSS_COLORARG1],
                               m_textureStageState[0][D3DTSS_COLORARG2],
                               m_textureStageState[0][D3DTSS_ALPHAOP],
                               m_textureStageState[0][D3DTSS_ALPHAARG1],
                               m_textureStageState[0][D3DTSS_ALPHAARG2],
                               m_renderState[D3DRS_TEXTUREFACTOR]);
        if (m_noDepthSurface) {
            //  Same states, with depth testing and writes forced off.
            DWORD rs[256];
            memcpy(rs, m_renderState, sizeof(rs));
            rs[D3DRS_ZENABLE] = 0;
            rs[D3DRS_ZWRITEENABLE] = 0;
            RanGLR_ApplyState(rs);
        } else {
            RanGLR_ApplyState(m_renderState);
        }
    }

    HRESULT DrawPrimitive(D3DPRIMITIVETYPE Type, UINT StartVertex, UINT PrimitiveCount) override {
        flushUIBatch();
        ++g_stats.draws;
        if (!m_stream0 || !m_stream0Stride) return D3D_OK;
        RanVertexBuffer *vb = (RanVertexBuffer *)m_stream0;
        if (vb->m_data.empty()) return D3D_OK;
        countDraw(m_fvf ? m_fvf : vb->m_fvf, PrimitiveCount * 3);
        prepareDraw();
        float mvp[16]; currentMVP(mvp);
        const unsigned glVB = vb->GlBuffer();
        const UINT base = m_stream0Offset + (UINT)((size_t)StartVertex * m_stream0Stride);
        if (glVB) {
            RanGLR_DrawVBO(Type, PrimitiveCount, glVB, base, m_stream0Stride,
                           m_fvf ? m_fvf : vb->m_fvf, boundGlTexture(), mvp,
                           0, 0, 0, 0, 0);
        } else {
            RanGLR_Draw(Type, PrimitiveCount, vb->m_data.data() + base,
                        m_stream0Stride, m_fvf ? m_fvf : vb->m_fvf, boundGlTexture(), mvp,
                        NULL, 0, 0, 0);
        }
        return D3D_OK;
    }

    HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE Type, INT BaseVertexIndex,
                                 UINT MinVertexIndex, UINT NumVertices,
                                 UINT StartIndex, UINT PrimitiveCount) override {
        flushUIBatch();
        ++g_stats.draws;
        if (!m_stream0 || !m_indices || !m_stream0Stride) return D3D_OK;
        RanVertexBuffer *vb = (RanVertexBuffer *)m_stream0;
        RanIndexBuffer *ib = (RanIndexBuffer *)m_indices;
        if (vb->m_data.empty() || ib->m_data.empty()) return D3D_OK;
        UINT idxBits = (ib->m_format == D3DFMT_INDEX16) ? 16 : 32;
        UINT idxCount = indexCountFor(Type, PrimitiveCount);
        // Indices are relative to BaseVertexIndex, so the vertex slice starts
        // there; MinVertexIndex + NumVertices is how far into it they reach.
        const UINT vbBase = m_stream0Offset +
            (UINT)((size_t)(BaseVertexIndex > 0 ? BaseVertexIndex : 0) * m_stream0Stride);
        const unsigned glVB = vb->GlBuffer();
        const unsigned glIB = ib->GlBuffer();
        {
            static int s_n = 0;
            if (s_n < 8 && (!glIB || !ib->m_glCreated)) { ++s_n; LOGI("IB draw without storage: glIB=%u created=%d len=%u dirty=%d", glIB, (int)ib->m_glCreated, ib->m_length, (int)ib->m_glDirty); }
        }

        countDraw(m_fvf ? m_fvf : vb->m_fvf, idxCount);
        prepareDraw();
        float mvp[16]; currentMVP(mvp);
        const UINT ibBase = (UINT)((size_t)StartIndex * (idxBits / 8));
        if (glVB && glIB) {
            //  The dump needs the vertices, and a buffer draw only carries a
            //  name; the copy the upload came from is still here.
            if (RanGLR_DiagArmed()) {
                RanGLR_DiagVerts(vb->m_data.data() + vbBase);
                RanGLR_DiagIndices(ib->m_data.data() + ibBase, idxBits);
            }
            RanGLR_DrawVBO(Type, PrimitiveCount, glVB, vbBase, m_stream0Stride,
                           m_fvf ? m_fvf : vb->m_fvf, boundGlTexture(), mvp,
                           glIB, ibBase, idxBits, idxCount, MinVertexIndex + NumVertices);
        } else {
            RanGLR_Draw(Type, PrimitiveCount, vb->m_data.data() + vbBase,
                        m_stream0Stride, m_fvf ? m_fvf : vb->m_fvf, boundGlTexture(), mvp,
                        ib->m_data.data() + ibBase, idxBits,
                        idxCount, MinVertexIndex + NumVertices);
        }
        return D3D_OK;
    }

    HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE Type, UINT PrimitiveCount,
                            const void *pVertexData, UINT Stride) override {
        ++g_stats.draws;
        if (!pVertexData || !Stride) return D3D_OK;
        countDraw(m_fvf, PrimitiveCount * 3);
        if (batchUIDraw(Type, PrimitiveCount, pVertexData, Stride, m_fvf, NULL, 0))
            return D3D_OK;
        prepareDraw();
        float mvp[16]; currentMVP(mvp);
        RanGLR_Draw(Type, PrimitiveCount, pVertexData, Stride, m_fvf, boundGlTexture(),
                    mvp, NULL, 0, 0, 0);
        return D3D_OK;
    }

    HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE Type, UINT MinVertexIndex,
                                   UINT NumVertices, UINT PrimitiveCount,
                                   const void *pIndexData, D3DFORMAT IndexFormat,
                                   const void *pVertexData, UINT Stride) override {
        ++g_stats.draws;
        if (!pVertexData || !pIndexData || !Stride) return D3D_OK;
        UINT idxBits = (IndexFormat == D3DFMT_INDEX16) ? 16 : 32;
        UINT idxCount = indexCountFor(Type, PrimitiveCount);
        countDraw(m_fvf, idxCount);
        if (batchUIDraw(Type, PrimitiveCount, pVertexData, Stride, m_fvf, pIndexData, idxBits))
            return D3D_OK;
        prepareDraw();
        float mvp[16]; currentMVP(mvp);
        RanGLR_Draw(Type, PrimitiveCount, pVertexData, Stride, m_fvf, boundGlTexture(),
                    mvp, pIndexData, idxBits, idxCount, MinVertexIndex + NumVertices);
        return D3D_OK;
    }

    // ---- resources ----
    HRESULT CreateTexture(UINT w, UINT h, UINT levels, DWORD Usage, D3DFORMAT fmt, D3DPOOL,
                          IDirect3DTexture9 **ppTex, HANDLE *) override {
        if (!ppTex) return D3DERR_INVALIDCALL;
        RanTexture *tex = new RanTexture(this, w, h, levels, fmt);
        tex->m_usage = Usage;
        *ppTex = tex;
        ++g_stats.texturesCreated;
        return D3D_OK;
    }
    HRESULT CreateVertexBuffer(UINT len, DWORD usage, DWORD fvf, D3DPOOL,
                               IDirect3DVertexBuffer9 **ppVB, HANDLE *) override {
        if (!ppVB) return D3DERR_INVALIDCALL;
        *ppVB = new RanVertexBuffer(len, usage, fvf);
        ++g_stats.vbCreated; g_stats.vbBytes += len;
        return D3D_OK;
    }
    HRESULT CreateIndexBuffer(UINT len, DWORD usage, D3DFORMAT fmt, D3DPOOL,
                              IDirect3DIndexBuffer9 **ppIB, HANDLE *) override {
        if (!ppIB) return D3DERR_INVALIDCALL;
        *ppIB = new RanIndexBuffer(len, usage, fmt);
        ++g_stats.ibCreated; g_stats.ibBytes += len;
        return D3D_OK;
    }

    // ---- state blocks ----
    HRESULT BeginStateBlock() override {
        if (m_recording) return D3DERR_INVALIDCALL;
        m_recording = new RanStateBlock(this);
        return D3D_OK;
    }
    HRESULT EndStateBlock(IDirect3DStateBlock9 **ppSB) override {
        if (!m_recording) return D3DERR_INVALIDCALL;
        if (ppSB) *ppSB = m_recording; else m_recording->Release();
        m_recording = NULL;
        return D3D_OK;
    }

    RAN_D3D9_STUBS_IDIRECT3DDEVICE9
};

HRESULT RanStateBlock::Capture() {
    // Re-read the device's current values for everything this block covers.
    for (auto &r : m_rs)  m_device->GetRenderState(r.t, &r.v);
    for (auto &t : m_tss) m_device->GetTextureStageState(t.stage, t.t, &t.v);
    for (auto &s : m_ss)  m_device->GetSamplerState(s.sampler, s.t, &s.v);
    return D3D_OK;
}
HRESULT RanStateBlock::Apply() {
    RanStateBlock *save = m_device->m_recording;
    m_device->m_recording = NULL;             // applying must not re-record
    for (auto &r : m_rs)  m_device->SetRenderState(r.t, r.v);
    for (auto &t : m_tss) m_device->SetTextureStageState(t.stage, t.t, t.v);
    for (auto &s : m_ss)  m_device->SetSamplerState(s.sampler, s.t, s.v);
    m_device->m_recording = save;
    return D3D_OK;
}

// Caps are reported generously: the engine branches on them to pick render
// paths, and phase 3 targets GLES3, which covers everything claimed here.
HRESULT RanDevice::GetDeviceCaps(D3DCAPS9 *pCaps) {
    if (!pCaps) return D3DERR_INVALIDCALL;
    memset(pCaps, 0, sizeof(*pCaps));
    pCaps->DeviceType = D3DDEVTYPE_HAL;
    pCaps->Caps2 = D3DCAPS2_DYNAMICTEXTURES;
    pCaps->DevCaps = D3DDEVCAPS_HWTRANSFORMANDLIGHT | D3DDEVCAPS_HWRASTERIZATION
                   | D3DDEVCAPS_DRAWPRIMTLVERTEX | D3DDEVCAPS_TEXTURENONLOCALVIDMEM;
    pCaps->PrimitiveMiscCaps = D3DPMISCCAPS_CULLNONE | D3DPMISCCAPS_CULLCW | D3DPMISCCAPS_CULLCCW
                             | D3DPMISCCAPS_MASKZ | D3DPMISCCAPS_BLENDOP;
    pCaps->RasterCaps = D3DPRASTERCAPS_ANISOTROPY | D3DPRASTERCAPS_MIPMAPLODBIAS
                      | D3DPRASTERCAPS_ZTEST | D3DPRASTERCAPS_FOGVERTEX | D3DPRASTERCAPS_FOGTABLE;
    pCaps->ZCmpCaps = pCaps->AlphaCmpCaps = 0xFF;
    //  DxSurfaceTex asks for exactly these two before it keeps its off-screen
    //  textures. Reporting zero made it release the refraction target, and the
    //  effects that sample it then drew with no texture at all - a solid white
    //  slab over the character instead of a refraction ribbon.
    pCaps->DevCaps2 = D3DDEVCAPS2_CAN_STRETCHRECT_FROM_TEXTURES;
    pCaps->StretchRectFilterCaps = D3DPTFILTERCAPS_MINFPOINT | D3DPTFILTERCAPS_MAGFPOINT
                                 | D3DPTFILTERCAPS_MINFLINEAR | D3DPTFILTERCAPS_MAGFLINEAR;
    pCaps->TextureCaps = D3DPTEXTURECAPS_ALPHA | D3DPTEXTURECAPS_PERSPECTIVE
                       | D3DPTEXTURECAPS_MIPMAP | D3DPTEXTURECAPS_CUBEMAP;
    pCaps->TextureFilterCaps = D3DPTFILTERCAPS_MINFLINEAR | D3DPTFILTERCAPS_MAGFLINEAR
                             | D3DPTFILTERCAPS_MIPFLINEAR | D3DPTFILTERCAPS_MINFANISOTROPIC;
    //  No BORDER: the sampler maps D3DTADDRESS_BORDER to GL_CLAMP_TO_EDGE
    //  because ES has no border colour, so advertising it would be a promise we
    //  silently break. Nothing live reads it today (DxEffectMan::IsBorder has
    //  only commented-out callers), but a cap that lies is how a whole feature
    //  goes quietly wrong later.
    pCaps->TextureAddressCaps = D3DPTADDRESSCAPS_WRAP | D3DPTADDRESSCAPS_MIRROR
                              | D3DPTADDRESSCAPS_CLAMP;
    pCaps->MaxTextureWidth = pCaps->MaxTextureHeight = 4096;
    pCaps->MaxAnisotropy = 16;
    pCaps->MaxActiveLights = 8;
    pCaps->MaxTextureBlendStages = 8;
    pCaps->MaxSimultaneousTextures = 8;
    pCaps->VertexProcessingCaps = D3DVTXPCAPS_DIRECTIONALLIGHTS | D3DVTXPCAPS_POSITIONALLIGHTS
                                | D3DVTXPCAPS_LOCALVIEWER | D3DVTXPCAPS_MATERIALSOURCE7;
    //  D3DVBF_3WEIGHTS is the largest non-indexed blend D3D9 defines, and it
    //  means four matrices. Claiming more makes the engine ask for a blend mode
    //  that does not exist.
    pCaps->MaxVertexBlendMatrices = 4;
    pCaps->MaxVertexIndex = 0xFFFFF;
    pCaps->MaxStreams = 8;
    pCaps->VertexShaderVersion = D3DVS_VERSION(1, 1);
    pCaps->PixelShaderVersion  = D3DPS_VERSION(2, 0);
    pCaps->MaxVertexShaderConst = 256;
    pCaps->NumSimultaneousRTs = 1;
    return D3D_OK;
}

// ------------------------------------------------------------- IDirect3D9
class RanD3D9 : public IDirect3D9 {
public:
    LONG m_ref = 1;

    HRESULT QueryInterface(REFIID, void **ppv) override { *ppv = this; AddRef(); return S_OK; }
    ULONG AddRef() override { return (ULONG)++m_ref; }
    ULONG Release() override { LONG r = --m_ref; if (r <= 0) { delete this; return 0; } return (ULONG)r; }

    UINT GetAdapterCount() override { return 1; }

    HRESULT GetAdapterIdentifier(UINT, DWORD, D3DADAPTER_IDENTIFIER9 *pId) override {
        LOGI("GetAdapterIdentifier(this=%p)", (void *)this);
        if (!pId) return D3DERR_INVALIDCALL;
        memset(pId, 0, sizeof(*pId));
        strcpy(pId->Driver, "ran-gles");
        strcpy(pId->Description, "RAN mobile D3D9-over-GLES shim");
        strcpy(pId->DeviceName, "\\\\.\\DISPLAY1");
        pId->VendorId = 0x1010; pId->DeviceId = 0x0001;
        return D3D_OK;
    }
    // Exactly one mode is advertised, and it is the real surface at 32-bit.
    //
    // The engine filters modes below 800x600, de-duplicates by format, and then
    // demands a mode matching RANPARAM's requested size. Advertising a fake
    // 1280x720 for four different formats produced a mode list nothing matched,
    // which left dwCurrentMode pointing at an uninitialised D3DModeInfo — the
    // wild pointer that crashed Initialize3DEnvironment. One honest mode, only
    // for the format we actually present in, keeps that search consistent.
    UINT GetAdapterModeCount(UINT, D3DFORMAT Format) override {
        return (Format == D3DFMT_X8R8G8B8) ? 1 : 0;
    }
    HRESULT EnumAdapterModes(UINT, D3DFORMAT Format, UINT, D3DDISPLAYMODE *pMode) override {
        if (!pMode) return D3DERR_INVALIDCALL;
        if (Format != D3DFMT_X8R8G8B8) return D3DERR_NOTAVAILABLE;
        RECT r; GetClientRect(NULL, &r);
        pMode->Width = (UINT)(r.right - r.left);
        pMode->Height = (UINT)(r.bottom - r.top);
        pMode->RefreshRate = 60;
        pMode->Format = D3DFMT_X8R8G8B8;
        return D3D_OK;
    }
    HRESULT GetAdapterDisplayMode(UINT, D3DDISPLAYMODE *pMode) override {
        if (!pMode) return D3DERR_INVALIDCALL;
        RECT r; GetClientRect(NULL, &r);
        pMode->Width = (UINT)(r.right - r.left);
        pMode->Height = (UINT)(r.bottom - r.top);
        pMode->RefreshRate = 60;
        pMode->Format = D3DFMT_X8R8G8B8;
        return D3D_OK;
    }
    HRESULT CheckDeviceType(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, BOOL) override { return D3D_OK; }
    HRESULT CheckDeviceFormat(UINT, D3DDEVTYPE, D3DFORMAT, DWORD Usage, D3DRESOURCETYPE, D3DFORMAT CheckFormat) override {
        // Depth/stencil: claim only D24S8 and D16, the two GLES3 always has.
        if (Usage & D3DUSAGE_DEPTHSTENCIL)
            return (CheckFormat == D3DFMT_D24S8 || CheckFormat == D3DFMT_D16) ? D3D_OK : D3DERR_NOTAVAILABLE;
        return D3D_OK;
    }
    HRESULT CheckDeviceMultiSampleType(UINT, D3DDEVTYPE, D3DFORMAT, BOOL, D3DMULTISAMPLE_TYPE t, DWORD *pQ) override {
        if (pQ) *pQ = 1;
        return (t == D3DMULTISAMPLE_NONE) ? D3D_OK : D3DERR_NOTAVAILABLE;
    }
    HRESULT CheckDepthStencilMatch(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, D3DFORMAT) override { return D3D_OK; }

    HRESULT GetDeviceCaps(UINT, D3DDEVTYPE, D3DCAPS9 *pCaps) override {
        D3DPRESENT_PARAMETERS pp; memset(&pp, 0, sizeof(pp));
        RanDevice tmp(this, pp);
        return tmp.GetDeviceCaps(pCaps);
    }

    HRESULT CreateDevice(UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *pPP,
                         IDirect3DDevice9 **ppDev) override {
        if (!ppDev || !pPP) return D3DERR_INVALIDCALL;
        RanDevice *dev = new RanDevice(this, *pPP);
        *ppDev = dev;
        DXUTSetD3D(this, dev);      // the engine's single device accessor
        return D3D_OK;
    }

    RAN_D3D9_STUBS_IDIRECT3D9
};

} // namespace

extern "C" IDirect3D9 *WINAPI Direct3DCreate9(UINT SDKVersion) {
    LOGI("Direct3DCreate9(0x%X) — shim (phase 2: headless)", SDKVersion);
    return new RanD3D9();
}

// Lets the platform layer print a one-line summary instead of tailing a log.
extern "C" void RanD3D_LogStats(void) {
    LOGI("D3D stats — frames %lu, draws %lu, clears %lu | textures %lu (%.1f MB), "
         "VB %lu (%.1f MB), IB %lu (%.1f MB)",
         g_stats.frames, g_stats.draws, g_stats.clears,
         g_stats.texturesCreated, g_stats.textureBytes / 1048576.0,
         g_stats.vbCreated, g_stats.vbBytes / 1048576.0,
         g_stats.ibCreated, g_stats.ibBytes / 1048576.0);
}
