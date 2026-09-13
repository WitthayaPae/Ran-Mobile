// ID3DXFont and ID3DXSprite, backed by the glyph rasteriser in shim/win.
//
// The client draws almost all of its text through ID3DXFont: CD3DFontX
// (Lib_Engine/DxCommon/D3DFontX.cpp) creates one per size, measures with
// DrawTextW(NULL, ..., DT_CALCRECT) and draws with DrawTextW(sprite, ...). The
// text-texture path (Lib_Engine/TextTexture) measures through the same object,
// so with no ID3DXFont every string measures 0x0 and nothing is drawn — which is
// exactly what a working UI with no text looks like.
//
// Implementation: one shared glyph atlas per font object, filled on demand, and
// text drawn as pre-transformed quads through the device — the same path the UI
// uses, so it inherits the GLES backend without special cases.

#include "windows.h"
#include "../platform/ran_plat.h"
#include <d3d9.h>
#include <d3dx9.h>

#include "../win/ttf_raster.h"

#include <map>
#include <string>
#include <vector>

#define LOGI(...) RanPlat_Log(RANLOG_INFO,  "RanD3DXFont", __VA_ARGS__)
#define LOGE(...) RanPlat_Log(RANLOG_ERROR, "RanD3DXFont", __VA_ARGS__)

// From gdi_text.cpp - the same codepage the GDI text path uses.
extern "C" int RanText_GetCodePage(void);

namespace {

// Faces are shared between font objects; several sizes of the same face are
// common and re-reading the file each time is pure waste.
std::map<std::string, TtfFace *> s_faces;

// One quad vertex, matching the UI's own format so the device path is identical.
struct FontVertex {
    float x, y, z, rhw;
    D3DCOLOR color;
    float u, v;
};
const DWORD FONT_FVF = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

//  Glyphs are rasterised larger than they are drawn.
//
//  The client asks for text in its own logical pixels, and the frame is now the
//  full panel, so every glyph quad is magnified by RanGL_UIScale on its way to
//  the screen. Rasterising at the logical size and then magnifying is exactly
//  what made all the text soft. Rasterising at the drawn size instead and
//  keeping the quad the same logical size costs nothing at draw time and is the
//  whole difference between blurry and sharp text.
//
//  Only the bitmap changes. Every metric the interface lays out with - advance,
//  ascent, descent, line height - stays in logical units, because moving those
//  would move every label in the game.
extern "C" int RanGL_UIScale(void);

int fontSuperSample() {
    int ss = RanGL_UIScale();
    if (ss < 1) ss = 1;
    if (ss > 4) ss = 4;          // the atlas is square-law in this
    return ss;
}

//  Sized on first use: a 2x atlas holds a quarter as many glyphs per side, so
//  it has to grow with the factor or it fills up and later glyphs draw blank.
int ATLAS_W = 1024;
int ATLAS_H = 1024;

struct Glyph {
    float u0, v0, u1, v1;
    //  In logical pixels - the size the quad is drawn at, not the size the
    //  glyph was rasterised at. Fractional because the two differ by the
    //  supersample factor.
    float w, h;
    float bearingX, bearingY;
    //  Whole logical pixels, and deliberately still an int: this is what the
    //  interface measures text with.
    int   advance;
    //  Kept so a run can ask the font where this glyph attaches. A mark has to
    //  be positioned against what precedes it, and that lookup is by glyph id
    //  in whichever face actually produced it.
    int      gid;
    TtfFace *face;
    bool     isMark;
    //  The outline mask for this glyph, built on first use by outlineFor: the
    //  glyph padded by the outline radius, with the coverage eight offset
    //  passes of one opaque colour would leave. Mutable because glyphs are
    //  handed out const and the mask is a cache.
    mutable int   outlineR = 0;          // radius the mask was built for; 0 = none yet
    mutable bool  outlineOk = false;
    mutable float ou0 = 0, ov0 = 0, ou1 = 0, ov1 = 0;
};

class RanD3DXFont : public ID3DXFont {
public:
    LONG m_ref = 1;
    IDirect3DDevice9 *m_device = NULL;
    TtfFace *m_face = NULL;
    // The device's Thai font is Thai-only (21 KB, no Latin at all), so Latin
    // text needs a second face or every English label renders as .notdef.
    TtfFace *m_fallback = NULL;
    std::string m_path;
    int   m_pixelSize = 12;
    //  Whether the size given was a cell height (positive) or a character
    //  height (negative), which decides what it is measured against.
    bool  m_cellHeight = true;
    bool  m_bold = false, m_italic = false;
    D3DXFONT_DESCA m_desc;

    IDirect3DTexture9 *m_atlas = NULL;
    int m_penX = 1, m_penY = 1, m_rowH = 0;
    //  The size THIS font's atlas texture was actually created at. The global
    //  below is only the size the next atlas will be created at; a font that
    //  allocated before it grew would pack and compute UVs against a texture
    //  larger than the one it owns - writing past the end of the locked bits.
    int m_atlasW = 0, m_atlasH = 0;
    std::map<unsigned, Glyph> m_glyphs;
    HDC m_dc = NULL;

    RanD3DXFont(IDirect3DDevice9 *dev, const D3DXFONT_DESCA *desc);
    ~RanD3DXFont();

    // --- IUnknown
    HRESULT __stdcall QueryInterface(REFIID, void **ppv) { *ppv = this; AddRef(); return S_OK; }
    ULONG   __stdcall AddRef() { return (ULONG)++m_ref; }
    ULONG   __stdcall Release() {
        LONG r = --m_ref;
        if (r <= 0) { delete this; return 0; }
        return (ULONG)r;
    }

    // --- ID3DXFont
    HRESULT __stdcall GetDevice(LPDIRECT3DDEVICE9 *ppDevice) {
        if (!ppDevice) return D3DERR_INVALIDCALL;
        *ppDevice = m_device;
        if (m_device) m_device->AddRef();
        return D3D_OK;
    }
    HRESULT __stdcall GetDescA(D3DXFONT_DESCA *pDesc) {
        if (!pDesc) return D3DERR_INVALIDCALL;
        *pDesc = m_desc;
        return D3D_OK;
    }
    HRESULT __stdcall GetDescW(D3DXFONT_DESCW *pDesc) {
        if (!pDesc) return D3DERR_INVALIDCALL;
        memset(pDesc, 0, sizeof(*pDesc));
        pDesc->Height = m_desc.Height;
        pDesc->Weight = m_desc.Weight;
        pDesc->CharSet = m_desc.CharSet;
        for (int i = 0; i < 31 && m_desc.FaceName[i]; ++i)
            pDesc->FaceName[i] = (WCHAR)(unsigned char)m_desc.FaceName[i];
        return D3D_OK;
    }
    BOOL __stdcall GetTextMetricsA(TEXTMETRICA *tm) {
        if (!tm || !m_face) return FALSE;
        memset(tm, 0, sizeof(*tm));
        float scale = fontScale();
        tm->tmAscent = m_face->Ascender(scale);
        tm->tmDescent = m_face->Descender(scale);
        tm->tmHeight = tm->tmAscent + tm->tmDescent;
        return TRUE;
    }
    BOOL __stdcall GetTextMetricsW(TEXTMETRICW *tm) {
        return GetTextMetricsA((TEXTMETRICA *)tm);
    }
    HDC __stdcall GetDC();
    HRESULT __stdcall GetGlyphData(UINT, LPDIRECT3DTEXTURE9 *ppTexture, RECT *, POINT *) {
        if (ppTexture) *ppTexture = NULL;
        return D3DERR_NOTAVAILABLE;
    }
    HRESULT __stdcall PreloadCharacters(UINT First, UINT Last) {
        for (UINT c = First; c <= Last && c - First < 4096; ++c) glyph(c);
        return D3D_OK;
    }
    HRESULT __stdcall PreloadGlyphs(UINT, UINT) { return D3D_OK; }
    HRESULT __stdcall PreloadTextA(LPCSTR, INT) { return D3D_OK; }
    HRESULT __stdcall PreloadTextW(LPCWSTR pString, INT Count) {
        if (!pString) return D3D_OK;
        for (INT i = 0; (Count < 0 ? pString[i] != 0 : i < Count); ++i) glyph((unsigned)pString[i]);
        return D3D_OK;
    }
    INT __stdcall DrawTextA(LPD3DXSPRITE pSprite, LPCSTR pString, INT Count, LPRECT pRect,
                            DWORD Format, D3DCOLOR Color);
    INT __stdcall DrawTextW(LPD3DXSPRITE pSprite, LPCWSTR pString, INT Count, LPRECT pRect,
                            DWORD Format, D3DCOLOR Color);
    HRESULT __stdcall OnLostDevice() { return D3D_OK; }
    HRESULT __stdcall OnResetDevice() { return D3D_OK; }

private:
    //  The em size in pixels. D3DXFONT_DESC.Height follows LOGFONT: negative is
    //  already the em size, positive is a CELL height that Windows resolves
    //  against the named face's ascent + descent.
    int emPixels() const {
        if (!m_cellHeight) return m_pixelSize;
        const int em = RanFont_WinEmForCellHeight(m_desc.FaceName, m_pixelSize);
        if (em > 0) return em;
        if (!m_face) return m_pixelSize;
        const int span = m_face->AscenderUnits() + m_face->DescenderUnits();
        if (span <= 0) return m_pixelSize;
        return (int)((float)m_pixelSize * (float)m_face->UnitsPerEm() / (float)span + 0.5f);
    }

    float fontScale() const {
        if (!m_face) return 0.0f;
        return (float)emPixels() / (float)m_face->UnitsPerEm();
    }

    //  Where the baseline sits inside the line box, and how tall that box is.
    //
    //  These come from the face the client NAMED, not the one this device
    //  substituted for it. Every WINDOW_POS in uiextcfg.xml was measured against
    //  Tahoma - a caption box is 15 pixels tall for a size-9 font because
    //  Tahoma's tmHeight is 15 there - and the Android face standing in for it
    //  has a taller ascender. Taking the ascent from the substitute puts the
    //  baseline two pixels low in a box only a few pixels taller than the text,
    //  which is the whole caption sitting low in its button.
    void lineMetrics(int *ascentOut, int *lineOut) const {
        int a = 0, d = 0;
        if (!RanFont_WinMetrics(m_desc.FaceName, emPixels(), &a, &d)) {
            if (!m_face) { if (ascentOut) *ascentOut = 0; if (lineOut) *lineOut = 0; return; }
            const float sc = fontScale();
            a = m_face->Ascender(sc);
            d = m_face->Descender(sc);
        }
        if (ascentOut) *ascentOut = a;
        if (lineOut)   *lineOut   = a + d;
    }
    const Glyph *glyph(unsigned cp);
    //  Rasterise and cache by glyph id. Two faces are in play (the Thai face
    //  and the Latin fallback), so the key carries which one it came from.
    const Glyph *glyphFor(TtfFace *face, int gid);
    //  gid -> glyph, one vector per face. See glyphFor.
    std::vector<const Glyph *> m_glyphIdxMain, m_glyphIdxFall;
    //  Codepoints -> glyph ids, with ccmp applied. Returns the face each run
    //  of glyphs belongs to alongside the ids.
    void shapeRun(const WCHAR *s, INT count,
                  std::vector<int> &gids, std::vector<TtfFace *> &faces);
    //  Shaped runs, keyed by the string. See shapeRun.
    typedef std::basic_string<WCHAR> ShapeKey;
    struct ShapeResult { std::vector<int> gids; std::vector<TtfFace *> faces; };
    std::map<ShapeKey, ShapeResult> m_shapeCache;
    void ensureAtlas();
public:
    //  Public for RanD3DXFont_DrawOutline, which CD3DFontX calls.
    //  outlineR > 0 draws the run's outline (one quad per glyph from its mask)
    //  instead of the glyphs, and returns -1 if a mask could not be built.
    INT drawRun(const WCHAR *s, INT count, LPRECT pRect, DWORD Format, D3DCOLOR color,
                int outlineR = 0);
    const Glyph *outlineFor(const Glyph *g, int r);
};

// A sprite that only has to satisfy Begin/End around text: the font draws its own
// quads, so batching here would duplicate the device path for no gain.
class RanD3DXSprite : public ID3DXSprite {
public:
    LONG m_ref = 1;
    IDirect3DDevice9 *m_device;
    D3DXMATRIX m_transform;

    explicit RanD3DXSprite(IDirect3DDevice9 *dev) : m_device(dev) {
        D3DXMatrixIdentity(&m_transform);
    }
    HRESULT __stdcall QueryInterface(REFIID, void **ppv) { *ppv = this; AddRef(); return S_OK; }
    ULONG   __stdcall AddRef() { return (ULONG)++m_ref; }
    ULONG   __stdcall Release() { LONG r = --m_ref; if (r <= 0) { delete this; return 0; } return (ULONG)r; }
    HRESULT __stdcall GetDevice(LPDIRECT3DDEVICE9 *ppDevice) {
        if (!ppDevice) return D3DERR_INVALIDCALL;
        *ppDevice = m_device;
        if (m_device) m_device->AddRef();
        return D3D_OK;
    }
    HRESULT __stdcall GetTransform(D3DXMATRIX *p) { if (p) *p = m_transform; return D3D_OK; }
    HRESULT __stdcall SetTransform(const D3DXMATRIX *p) { if (p) m_transform = *p; return D3D_OK; }
    HRESULT __stdcall SetWorldViewRH(const D3DXMATRIX *, const D3DXMATRIX *) { return D3D_OK; }
    HRESULT __stdcall SetWorldViewLH(const D3DXMATRIX *, const D3DXMATRIX *) { return D3D_OK; }
    DWORD m_flags = 0;
    //  What Begin changed, so End can put it back. D3DX saves device state
    //  around a sprite batch unless the caller opts out.
    DWORD m_oldAlphaBlend = 0, m_oldSrcBlend = 0, m_oldDstBlend = 0;
    DWORD m_oldLighting = 0, m_oldZEnable = 0, m_oldCull = 0, m_oldAlphaTest = 0;
    DWORD m_oldColorOp = 0, m_oldColorArg1 = 0, m_oldColorArg2 = 0;
    DWORD m_oldAlphaOp = 0, m_oldAlphaArg1 = 0, m_oldAlphaArg2 = 0;
    bool  m_saved = false;

    HRESULT __stdcall Begin(DWORD Flags) {
        m_flags = Flags;
        if (!m_device) return D3D_OK;

        const bool save = (Flags & D3DXSPRITE_DONOTSAVESTATE) == 0;
        if (save) {
            m_device->GetRenderState(D3DRS_ALPHABLENDENABLE, &m_oldAlphaBlend);
            m_device->GetRenderState(D3DRS_SRCBLEND, &m_oldSrcBlend);
            m_device->GetRenderState(D3DRS_DESTBLEND, &m_oldDstBlend);
            m_device->GetRenderState(D3DRS_LIGHTING, &m_oldLighting);
            m_device->GetRenderState(D3DRS_ZENABLE, &m_oldZEnable);
            m_device->GetRenderState(D3DRS_CULLMODE, &m_oldCull);
            m_device->GetRenderState(D3DRS_ALPHATESTENABLE, &m_oldAlphaTest);
            m_device->GetTextureStageState(0, D3DTSS_COLOROP, &m_oldColorOp);
            m_device->GetTextureStageState(0, D3DTSS_COLORARG1, &m_oldColorArg1);
            m_device->GetTextureStageState(0, D3DTSS_COLORARG2, &m_oldColorArg2);
            m_device->GetTextureStageState(0, D3DTSS_ALPHAOP, &m_oldAlphaOp);
            m_device->GetTextureStageState(0, D3DTSS_ALPHAARG1, &m_oldAlphaArg1);
            m_device->GetTextureStageState(0, D3DTSS_ALPHAARG2, &m_oldAlphaArg2);
            m_saved = true;
        }

        m_device->SetRenderState(D3DRS_LIGHTING, FALSE);
        m_device->SetRenderState(D3DRS_ZENABLE, FALSE);
        m_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        m_device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        m_device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        m_device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        m_device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        m_device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
        m_device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        m_device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

        if (Flags & D3DXSPRITE_ALPHABLEND) {
            m_device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
            m_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
            m_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        }
        return D3D_OK;
    }

    HRESULT __stdcall Draw(LPDIRECT3DTEXTURE9 pTexture, const RECT *pSrcRect,
                           const D3DXVECTOR3 *pCenter, const D3DXVECTOR3 *pPosition,
                           D3DCOLOR Color) {
        if (!m_device || !pTexture) return D3DERR_INVALIDCALL;

        D3DSURFACE_DESC desc;
        if (FAILED(pTexture->GetLevelDesc(0, &desc)) || !desc.Width || !desc.Height)
            return D3DERR_INVALIDCALL;

        //  The source rect picks the cell; without one the whole texture is used.
        RECT src;
        if (pSrcRect) src = *pSrcRect;
        else { src.left = 0; src.top = 0; src.right = (LONG)desc.Width; src.bottom = (LONG)desc.Height; }
        const float w = (float)(src.right - src.left);
        const float h = (float)(src.bottom - src.top);
        if (w <= 0.0f || h <= 0.0f) return D3D_OK;

        //  pCenter is the point of the sprite that lands on pPosition, and the
        //  sprite transform applies on top. The loading screen leaves it
        //  identity, but a translation or a scale has to survive.
        float x = pPosition ? pPosition->x : 0.0f;
        float y = pPosition ? pPosition->y : 0.0f;
        if (pCenter) { x -= pCenter->x; y -= pCenter->y; }

        const D3DXMATRIX &m = m_transform;
        const float x0 = x * m._11 + y * m._21 + m._41;
        const float y0 = x * m._12 + y * m._22 + m._42;
        const float sx = (m._11 != 0.0f) ? w * m._11 : w;
        const float sy = (m._22 != 0.0f) ? h * m._22 : h;
        const float x1 = x0 + sx;
        const float y1 = y0 + sy;

        const float u0 = (float)src.left   / (float)desc.Width;
        const float v0 = (float)src.top    / (float)desc.Height;
        const float u1 = (float)src.right  / (float)desc.Width;
        const float v1 = (float)src.bottom / (float)desc.Height;

        struct V { float x, y, z, rhw; D3DCOLOR c; float u, v; };
        //  Half-pixel offset: D3D samples texel centres, and without it a
        //  screen-space quad bleeds into the neighbouring cells of an atlas.
        const V quad[4] = {
            { x0 - 0.5f, y0 - 0.5f, 0.0f, 1.0f, Color, u0, v0 },
            { x1 - 0.5f, y0 - 0.5f, 0.0f, 1.0f, Color, u1, v0 },
            { x1 - 0.5f, y1 - 0.5f, 0.0f, 1.0f, Color, u1, v1 },
            { x0 - 0.5f, y1 - 0.5f, 0.0f, 1.0f, Color, u0, v1 },
        };

        m_device->SetTexture(0, pTexture);
        m_device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
        m_device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, quad, sizeof(V));
        return D3D_OK;
    }

    HRESULT __stdcall Flush() { return D3D_OK; }

    HRESULT __stdcall End() {
        if (m_device && m_saved) {
            m_device->SetRenderState(D3DRS_ALPHABLENDENABLE, m_oldAlphaBlend);
            m_device->SetRenderState(D3DRS_SRCBLEND, m_oldSrcBlend);
            m_device->SetRenderState(D3DRS_DESTBLEND, m_oldDstBlend);
            m_device->SetRenderState(D3DRS_LIGHTING, m_oldLighting);
            m_device->SetRenderState(D3DRS_ZENABLE, m_oldZEnable);
            m_device->SetRenderState(D3DRS_CULLMODE, m_oldCull);
            m_device->SetRenderState(D3DRS_ALPHATESTENABLE, m_oldAlphaTest);
            m_device->SetTextureStageState(0, D3DTSS_COLOROP, m_oldColorOp);
            m_device->SetTextureStageState(0, D3DTSS_COLORARG1, m_oldColorArg1);
            m_device->SetTextureStageState(0, D3DTSS_COLORARG2, m_oldColorArg2);
            m_device->SetTextureStageState(0, D3DTSS_ALPHAOP, m_oldAlphaOp);
            m_device->SetTextureStageState(0, D3DTSS_ALPHAARG1, m_oldAlphaArg1);
            m_device->SetTextureStageState(0, D3DTSS_ALPHAARG2, m_oldAlphaArg2);
            m_saved = false;
        }
        return D3D_OK;
    }
    HRESULT __stdcall OnLostDevice() { return D3D_OK; }
    HRESULT __stdcall OnResetDevice() { return D3D_OK; }
};

RanD3DXFont::RanD3DXFont(IDirect3DDevice9 *dev, const D3DXFONT_DESCA *desc) : m_device(dev) {
    memset(&m_desc, 0, sizeof(m_desc));
    if (desc) m_desc = *desc;
    int px = m_desc.Height < 0 ? -m_desc.Height : m_desc.Height;
    m_cellHeight = m_desc.Height > 0;
    if (px <= 0) { px = 12; m_cellHeight = false; }
    m_pixelSize = px;
    m_bold = m_desc.Weight >= 600;
    m_italic = m_desc.Italic != 0;
    m_path = RanFont_Resolve(m_desc.FaceName, RanText_GetCodePage(), m_bold);

    std::map<std::string, TtfFace *>::iterator it = s_faces.find(m_path);
    if (it != s_faces.end()) {
        m_face = it->second;
    } else {
        TtfFace *f = new TtfFace();
        if (!f->Load(m_path.c_str())) { delete f; f = NULL; }
        s_faces[m_path] = f;
        m_face = f;
        if (f) LOGI("face %s for %dpx", m_path.c_str(), m_pixelSize);
    }
    if (!m_face) LOGE("no face for %s", m_desc.FaceName);

    std::string latin = RanFont_Resolve(m_desc.FaceName, 0, m_bold);
    if (latin != m_path) {
        std::map<std::string, TtfFace *>::iterator lt = s_faces.find(latin);
        if (lt != s_faces.end()) {
            m_fallback = lt->second;
        } else {
            TtfFace *f = new TtfFace();
            if (!f->Load(latin.c_str())) { delete f; f = NULL; }
            s_faces[latin] = f;
            m_fallback = f;
            if (f) LOGI("fallback face %s", latin.c_str());
        }
    }
}

RanD3DXFont::~RanD3DXFont() {
    if (m_atlas) m_atlas->Release();
    if (m_dc) DeleteDC(m_dc);
}

void RanD3DXFont::ensureAtlas() {
    if (m_atlas || !m_device) return;
    {
        const int ss = fontSuperSample();
        const int want = 1024 * ss;
        if (want > ATLAS_W) { ATLAS_W = want; ATLAS_H = want; }
    }
    m_atlasW = ATLAS_W;
    m_atlasH = ATLAS_H;
    // A8R8G8B8 rather than A8: the GLES backend already uploads ARGB, and the
    // atlas is written once per new glyph, so the extra bytes cost nothing.
    if (FAILED(m_device->CreateTexture(m_atlasW, m_atlasH, 1, 0, D3DFMT_A8R8G8B8,
                                       D3DPOOL_MANAGED, &m_atlas, NULL)))
        m_atlas = NULL;
    if (!m_atlas) return;
    D3DLOCKED_RECT lr;
    if (SUCCEEDED(m_atlas->LockRect(0, &lr, NULL, 0)) && lr.pBits) {
        memset(lr.pBits, 0, (size_t)m_atlasW * m_atlasH * 4);
        m_atlas->UnlockRect(0);
    }
}

//  Resolve a codepoint to a face and glyph id, preferring the primary face and
//  falling back for anything it does not cover (its Latin is empty).
void RanD3DXFont::shapeRun(const WCHAR *s, INT count,
                           std::vector<int> &gids, std::vector<TtfFace *> &faces) {
    gids.clear(); faces.clear();
    if (!m_face) return;

    //  Shaping the same string again every frame is most of what shaping costs.
    //
    //  The client re-measures and re-draws its labels from scratch each frame,
    //  and the answer only depends on the characters: cmap for each, then the
    //  face's ccmp lookups over the run. A profile put ~3% of the process in
    //  applySubstLookup and coverageIndex alone. The result is cached by the
    //  string itself; the table is dropped wholesale when it grows past a few
    //  hundred entries, which is far more than a screen of text.
    ShapeKey key(s, s + count);
    {
        std::map<ShapeKey, ShapeResult>::const_iterator hit = m_shapeCache.find(key);
        if (hit != m_shapeCache.end()) {
            gids = hit->second.gids;
            faces = hit->second.faces;
            return;
        }
    }
    gids.reserve((size_t)count); faces.reserve((size_t)count);
    for (INT i = 0; i < count; ++i) {
        const unsigned cp = (unsigned)s[i];
        TtfFace *f = m_face;
        int gid = f->GlyphIndex(cp);
        if (!gid && m_fallback) {
            const int alt = m_fallback->GlyphIndex(cp);
            if (alt) { f = m_fallback; gid = alt; }
        }
        gids.push_back(gid);
        faces.push_back(f);
    }

    //  ccmp is per-face and positional, so it runs over each maximal stretch of
    //  glyphs from the same face. Mixing the Latin fallback into a Thai run
    //  would have it substituting against glyph ids that mean something else
    //  entirely.
    size_t i = 0;
    while (i < gids.size()) {
        size_t j = i;
        while (j < gids.size() && faces[j] == faces[i]) ++j;
        std::vector<int> part(gids.begin() + (long)i, gids.begin() + (long)j);
        const size_t before = part.size();
        faces[i]->ApplyCcmp(part);
        if (part.size() == before) {
            std::copy(part.begin(), part.end(), gids.begin() + (long)i);
        } else {
            //  A multiple substitution grew the run; keep faces in step.
            TtfFace *f = faces[i];
            gids.erase(gids.begin() + (long)i, gids.begin() + (long)j);
            faces.erase(faces.begin() + (long)i, faces.begin() + (long)j);
            gids.insert(gids.begin() + (long)i, part.begin(), part.end());
            faces.insert(faces.begin() + (long)i, part.size(), f);
            j = i + part.size();
        }
        i = j;
    }

    //  Kept for the next frame. A blunt clear rather than an eviction policy:
    //  the working set is one screen of text, and the only way this grows is
    //  chat scrolling past, where nothing old is worth keeping anyway.
    if (m_shapeCache.size() > 512) m_shapeCache.clear();
    {
        ShapeResult &out = m_shapeCache[key];
        out.gids = gids;
        out.faces = faces;
    }
}

const Glyph *RanD3DXFont::glyph(unsigned cp) {
    if (!m_face) return NULL;
    TtfFace *face = m_face;
    int gid = face->GlyphIndex(cp);
    if (!gid && m_fallback) {
        const int alt = m_fallback->GlyphIndex(cp);
        if (alt) { face = m_fallback; gid = alt; }
    }
    return glyphFor(face, gid);
}

const Glyph *RanD3DXFont::glyphFor(TtfFace *face, int gid) {
    if (!face) return NULL;
    //  The key has to separate the two faces: glyph 40 means different things
    //  in the Thai face and in the Latin fallback.
    const unsigned key = ((face == m_fallback) ? 0x80000000u : 0u) | (unsigned)gid;

    //  The map is the store; this is the index into it.
    //
    //  Glyph ids are dense and small, so a vector answers in one load where the
    //  map costs a tree walk - and this is called twice for every character
    //  drawn (once to measure the run, once to place it), every frame. A
    //  profile put 2.8% of the process in here. std::map never moves a node, so
    //  a pointer into it stays good for the life of the font.
    std::vector<const Glyph *> &index = (face == m_fallback) ? m_glyphIdxFall
                                                            : m_glyphIdxMain;
    if ((size_t)gid < index.size() && index[(size_t)gid]) return index[(size_t)gid];

    std::map<unsigned, Glyph>::iterator it = m_glyphs.find(key);
    if (it != m_glyphs.end()) {
        if (gid >= 0) {
            if ((size_t)gid >= index.size()) index.resize((size_t)gid + 64, (const Glyph *)0);
            index[(size_t)gid] = &it->second;
        }
        return &it->second;
    }
    ensureAtlas();
    if (!m_atlas) return NULL;

    TtfGlyphBitmap gb;
    //  emPixels(), not m_pixelSize: a positive D3DXFONT_DESC.Height is a cell
    //  height and has to be converted before it can scale an outline.
    const float scale = (float)emPixels() / (float)face->UnitsPerEm();
    const int   ss    = fontSuperSample();
    if (!face->Rasterise(gid, scale * (float)ss, m_italic ? 0.2f : 0.0f,
                         m_bold ? ss : 0, gb))
        return NULL;

    Glyph g;
    //  The bitmap is ss times oversized; the quad it fills is not. Kept as
    //  floats so the division does not quantise the glyph box to whole logical
    //  pixels, which would jitter letter shapes against each other.
    const float inv = 1.0f / (float)ss;
    g.w = (float)gb.width  * inv; g.h = (float)gb.height * inv;
    g.bearingX = (float)gb.bearingX * inv; g.bearingY = (float)gb.bearingY * inv;
    //  Straight from the face at the logical scale, not gb.advance/ss: this is
    //  the number the interface measures every label with, and it has to stay
    //  exactly what it was before the glyphs got bigger.
    g.advance = face->Advance(gid, scale) + (m_bold ? 1 : 0);
    g.gid = gid;
    g.face = face;
    g.isMark = face->IsMark(gid);
    g.u0 = g.v0 = g.u1 = g.v1 = 0.0f;

    if (gb.width > 0 && gb.height > 0) {
        if (m_penX + gb.width + 1 > m_atlasW) {
            m_penX = 1;
            m_penY += m_rowH + 1;
            m_rowH = 0;
        }
        if (m_penY + gb.height + 1 > m_atlasH) {
            // The atlas is full. Rather than corrupt it, later glyphs draw blank
            // — and say so once, because it means the atlas needs to grow.
            static bool warned = false;
            if (!warned) { warned = true; LOGE("glyph atlas full at %dpx", m_pixelSize); }
            return NULL;
        }

        D3DLOCKED_RECT lr;
        if (SUCCEEDED(m_atlas->LockRect(0, &lr, NULL, 0)) && lr.pBits) {
            DWORD *base = (DWORD *)lr.pBits;
            for (int y = 0; y < gb.height; ++y) {
                DWORD *row = base + (size_t)(m_penY + y) * m_atlasW + m_penX;
                const unsigned char *src = &gb.coverage[(size_t)y * gb.width];
                for (int x = 0; x < gb.width; ++x) {
                    // White with the coverage as alpha; the vertex colour tints it.
                    row[x] = ((DWORD)src[x] << 24) | 0x00FFFFFFu;
                }
            }
            m_atlas->UnlockRect(0);
        }

        g.u0 = (float)m_penX / m_atlasW;
        g.v0 = (float)m_penY / m_atlasH;
        g.u1 = (float)(m_penX + gb.width) / m_atlasW;
        g.v1 = (float)(m_penY + gb.height) / m_atlasH;

        m_penX += gb.width + 1;
        if (gb.height > m_rowH) m_rowH = gb.height;
    }

    m_glyphs[key] = g;
    const Glyph *stored = &m_glyphs[key];
    if (gid >= 0) {
        if ((size_t)gid >= index.size()) index.resize((size_t)gid + 64, (const Glyph *)0);
        index[(size_t)gid] = stored;
    }
    return stored;
}

//  The outline mask for one glyph.
//
//  CD3DFontX draws an outlined string by drawing it at every offset of a
//  (2r+1)^2 square except the centre in one opaque colour, then the string on
//  top. Stacking one colour n times with ordinary alpha blending leaves
//  coverage 1 - prod(1 - a_i), so one draw of a mask holding exactly that
//  replaces the n passes: the same edge for a ninth of the geometry. The
//  offsets are logical pixels and the atlas is supersampled, so one logical
//  pixel is ss texels here.
//
//  Measured before building this: skipping the outline passes took the crowd
//  scene from 28.0 to 38.8 fps and the streamed interface vertices from 1,540 KB
//  to 207 KB a frame.
const Glyph *RanD3DXFont::outlineFor(const Glyph *g, int r) {
    if (!g || r <= 0 || r > 4) return NULL;
    if (g->outlineR == r) return g->outlineOk ? g : NULL;
    g->outlineR = r;
    g->outlineOk = false;
    if (!g->face || !m_atlas) return NULL;

    TtfGlyphBitmap gb;
    //  Exactly the rasterisation glyphFor used, so the mask lines up with it.
    const float scale = (float)emPixels() / (float)g->face->UnitsPerEm();
    const int   ss    = fontSuperSample();
    if (!g->face->Rasterise(g->gid, scale * (float)ss, m_italic ? 0.2f : 0.0f,
                            m_bold ? ss : 0, gb))
        return NULL;
    if (gb.width <= 0 || gb.height <= 0) return NULL;

    const int pad = r * ss;
    const int W = gb.width + 2 * pad, H = gb.height + 2 * pad;

    if (m_penX + W + 1 > m_atlasW) {
        m_penX = 1;
        m_penY += m_rowH + 1;
        m_rowH = 0;
    }
    if (m_penY + H + 1 > m_atlasH) return NULL;   // full: the caller falls back

    D3DLOCKED_RECT lr;
    if (FAILED(m_atlas->LockRect(0, &lr, NULL, 0)) || !lr.pBits) return NULL;
    DWORD *base = (DWORD *)lr.pBits;
    for (int y = 0; y < H; ++y) {
        DWORD *row = base + (size_t)(m_penY + y) * m_atlasW + m_penX;
        for (int x = 0; x < W; ++x) {
            float keep = 1.0f;                     // prod(1 - a_i)
            for (int oy = -r; oy <= r; ++oy) {
                for (int ox = -r; ox <= r; ++ox) {
                    if (!ox && !oy) continue;
                    const int sx = x - pad - ox * ss;
                    const int sy = y - pad - oy * ss;
                    if (sx < 0 || sy < 0 || sx >= gb.width || sy >= gb.height) continue;
                    keep *= 1.0f - (float)gb.coverage[(size_t)sy * gb.width + sx] / 255.0f;
                }
            }
            const int a = (int)((1.0f - keep) * 255.0f + 0.5f);
            row[x] = ((DWORD)(a > 255 ? 255 : a) << 24) | 0x00FFFFFFu;
        }
    }
    m_atlas->UnlockRect(0);

    g->ou0 = (float)m_penX / m_atlasW;
    g->ov0 = (float)m_penY / m_atlasH;
    g->ou1 = (float)(m_penX + W) / m_atlasW;
    g->ov1 = (float)(m_penY + H) / m_atlasH;
    m_penX += W + 1;
    if (H > m_rowH) m_rowH = H;
    g->outlineOk = true;
    return g;
}

//  CD3DFontX's outline in one pass per glyph; see outlineFor. Returns 1 if the
//  outline was drawn, 0 if the caller should draw its offset passes as before.
extern "C" int RanD3DXFont_DrawOutline(LPD3DXFONT font, const WCHAR *s, INT count,
                                       LPRECT rect, DWORD format, D3DCOLOR color, int radius) {
    if (!font || !s) return 0;
    RanD3DXFont *f = static_cast<RanD3DXFont *>(font);
    return f->drawRun(s, count, rect, format, color, radius) >= 0 ? 1 : 0;
}

HDC RanD3DXFont::GetDC() {
    // CD3DFontX keeps this handle and draws through it for its text-texture
    // path, so it must be a DC with this font already selected.
    if (!m_dc) {
        m_dc = CreateCompatibleDC(NULL);
        //  CreateFont takes a negative height as the em size, so the cell
        //  height has to be converted or the DC would measure a different font
        //  from the one being drawn.
        const int emPx = emPixels();
        HFONT f = CreateFontA(-emPx, 0, 0, 0, m_desc.Weight, m_desc.Italic,
                              0, 0, m_desc.CharSet, 0, 0, m_desc.Quality, 0,
                              m_desc.FaceName);
        if (f) SelectObject(m_dc, f);
    }
    return m_dc;
}

//  Where a mark sits relative to the pen, given what came before it.
//
//  Thai stacks: a tone mark goes over an upper vowel, which goes over the
//  consonant. The font expresses that as 'mark' (mark to base) and 'mkmk'
//  (mark to mark) anchor pairs; walking back to the nearest base for the first
//  and the immediately preceding mark for the second is what turns a pile of
//  overlapping glyphs into legible Thai.
struct MarkPlacer {
    //  The last base glyph seen, and the last mark placed on it.
    const Glyph *base = NULL;
    float basePenX = 0.0f;
    const Glyph *prevMark = NULL;
    float prevMarkX = 0.0f, prevMarkY = 0.0f;

    void reset() { base = NULL; prevMark = NULL; }

    //  Returns the pen-relative offset for `g` at pen position `penX`, and
    //  whether the glyph consumes advance width.
    void place(const Glyph *g, float penX, float scale, float *outX, float *outY) {
        *outX = penX; *outY = 0.0f;
        if (!g) return;

        if (!g->isMark) {
            base = g; basePenX = penX; prevMark = NULL;
            return;
        }
        if (!g->face) return;

        int dx = 0, dy = 0;
        //  Prefer stacking on the previous mark; fall back to the base.
        if (prevMark && prevMark->face == g->face &&
            g->face->MarkOffset(prevMark->gid, g->gid, true, &dx, &dy)) {
            *outX = prevMarkX + dx * scale;
            *outY = prevMarkY - dy * scale;      // font y is up, screen y is down
        } else if (base && base->face == g->face &&
                   g->face->MarkOffset(base->gid, g->gid, false, &dx, &dy)) {
            *outX = basePenX + dx * scale;
            *outY = -dy * scale;
        } else {
            //  No anchor for this pair: leave the glyph where its outline puts
            //  it, which is what happened before any of this existed.
            return;
        }
        prevMark = g; prevMarkX = *outX; prevMarkY = *outY;
    }
};

INT RanD3DXFont::drawRun(const WCHAR *s, INT count, LPRECT pRect, DWORD Format,
                         D3DCOLOR color, int outlineR) {
    if (!m_face || !s) return 0;
    const float scale = fontScale();
    int ascent = 0, lineH = 0;
    lineMetrics(&ascent, &lineH);

    if (count < 0) { count = 0; while (s[count]) ++count; }

    // DT_CALCRECT: measure only. This is how the client sizes every label, so a
    // wrong answer here misplaces text even when the glyphs are right.
    if (Format & DT_CALCRECT) {
        std::vector<int> gids; std::vector<TtfFace *> faces;
        shapeRun(s, count, gids, faces);
        int wsum = 0;
        for (size_t k = 0; k < gids.size(); ++k) {
            const Glyph *g = glyphFor(faces[k], gids[k]);
            //  Combining marks sit on the glyph before them and take no width.
            //  Counting them here would make every Thai label measure wider
            //  than it draws, and centred text would sit left of centre.
            if (g && !g->isMark) wsum += g->advance;
        }
        if (pRect) {
            pRect->right = pRect->left + wsum;
            pRect->bottom = pRect->top + lineH;
        }
        return lineH;
    }

    if (!m_device || !m_atlas) {
        // Touch every glyph so the atlas exists on the next call even if this
        // one cannot draw yet.
        for (INT i = 0; i < count; ++i) glyph((unsigned)s[i]);   // warm the atlas
        if (!m_atlas) return outlineR > 0 ? -1 : lineH;
    }

    std::vector<int> gids; std::vector<TtfFace *> faces;
    shapeRun(s, count, gids, faces);

    //  Every mask first: if any one cannot be built the caller draws the old
    //  eight passes instead, and nothing of this run has been emitted yet.
    if (outlineR > 0) {
        for (size_t k = 0; k < gids.size(); ++k) {
            const Glyph *g = glyphFor(faces[k], gids[k]);
            if (g && g->w > 0 && g->h > 0 && !outlineFor(g, outlineR)) return -1;
        }
    }

    int totalW = 0;
    for (size_t k = 0; k < gids.size(); ++k) {
        const Glyph *g = glyphFor(faces[k], gids[k]);
        if (g && !g->isMark) totalW += g->advance;
    }

    float x = pRect ? (float)pRect->left : 0.0f;
    float y = pRect ? (float)pRect->top : 0.0f;
    if (pRect) {
        if (Format & DT_CENTER) x += ((pRect->right - pRect->left) - totalW) * 0.5f;
        else if (Format & DT_RIGHT) x += (pRect->right - pRect->left) - totalW;
        if (Format & DT_VCENTER) y += ((pRect->bottom - pRect->top) - lineH) * 0.5f;
        else if (Format & DT_BOTTOM) y += (pRect->bottom - pRect->top) - lineH;
    }

    m_device->SetFVF(FONT_FVF);
    m_device->SetTexture(0, m_atlas);

    //  One draw for the whole run, not one per glyph.
    //
    //  Every glyph shares the atlas and the state around it, so the only thing
    //  a per-glyph draw bought was simplicity. It cost the frame: a sampling
    //  profile of the client in the world put 19% of the process under
    //  CD3DFontX::DrawText, almost all of it in the per-glyph DrawPrimitiveUP
    //  and the memmove behind it - the chat box alone is hundreds of glyphs a
    //  frame, and each one was a stream write, a state check and a draw call.
    //
    //  A triangle list rather than a fan, because a fan cannot be batched:
    //  six vertices a glyph, two triangles, one call at the end. The buffer is
    //  kept between calls so a run costs no allocation.
    static std::vector<FontVertex> s_verts;
    s_verts.clear();
    if (s_verts.capacity() < 6 * 128) s_verts.reserve(6 * 128);

    MarkPlacer placer;
    for (size_t k = 0; k < gids.size(); ++k) {
        const Glyph *g = glyphFor(faces[k], gids[k]);
        if (!g) continue;
        float ox = x, oy = 0.0f;
        placer.place(g, x, scale, &ox, &oy);
        if (g->w > 0 && g->h > 0) {
            float gx = ox + g->bearingX;
            float gy = y + oy + ascent - g->bearingY;
            float gw = g->w, gh = g->h;
            float u0 = g->u0, v0 = g->v0, u1 = g->u1, v1 = g->v1;
            if (outlineR > 0) {
                //  The mask is the glyph padded by the radius on every side, in
                //  logical pixels, so the quad grows by the same amount.
                const float r = (float)outlineR;
                gx -= r; gy -= r; gw += 2.0f * r; gh += 2.0f * r;
                u0 = g->ou0; v0 = g->ov0; u1 = g->ou1; v1 = g->ov1;
            }
            FontVertex v[4];
            const float z = 0.0f, rhw = 1.0f;
            v[0].x = gx;            v[0].y = gy;            v[0].u = u0; v[0].v = v0;
            v[1].x = gx + gw;       v[1].y = gy;            v[1].u = u1; v[1].v = v0;
            v[2].x = gx + gw;       v[2].y = gy + gh;       v[2].u = u1; v[2].v = v1;
            v[3].x = gx;            v[3].y = gy + gh;       v[3].u = u0; v[3].v = v1;
            for (int j = 0; j < 4; ++j) { v[j].z = z; v[j].rhw = rhw; v[j].color = color; }

            //  0,1,2 and 0,2,3 - the same two triangles the fan drew.
            s_verts.push_back(v[0]); s_verts.push_back(v[1]); s_verts.push_back(v[2]);
            s_verts.push_back(v[0]); s_verts.push_back(v[2]); s_verts.push_back(v[3]);
        }
        //  A combining mark carries no advance of its own; letting it move the
        //  pen is what spreads a Thai word out into loose, drifting glyphs.
        if (!g->isMark) x += g->advance;
    }

    if (!s_verts.empty()) {
        m_device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, (UINT)(s_verts.size() / 3),
                                  &s_verts[0], sizeof(FontVertex));
    }

    return lineH;
}

INT RanD3DXFont::DrawTextW(LPD3DXSPRITE, LPCWSTR pString, INT Count, LPRECT pRect,
                           DWORD Format, D3DCOLOR Color) {
    return drawRun(pString, Count, pRect, Format, Color);
}

INT RanD3DXFont::DrawTextA(LPD3DXSPRITE pSprite, LPCSTR pString, INT Count, LPRECT pRect,
                           DWORD Format, D3DCOLOR Color) {
    if (!pString) return 0;
    INT n = (Count < 0) ? (INT)strlen(pString) : Count;
    std::vector<WCHAR> w((size_t)n + 1, 0);
    int produced = MultiByteToWideChar(RanText_GetCodePage(), 0, pString, n, &w[0], n);
    if (produced <= 0) return 0;
    return drawRun(&w[0], produced, pRect, Format, Color);
}

} // namespace

// ------------------------------------------------------------------ factories
extern "C" HRESULT WINAPI D3DXCreateFontIndirectA(LPDIRECT3DDEVICE9 pDevice,
                                                  const D3DXFONT_DESCA *pDesc,
                                                  LPD3DXFONT *ppFont) {
    if (!pDevice || !ppFont) return D3DERR_INVALIDCALL;
    *ppFont = new RanD3DXFont(pDevice, pDesc);
    return D3D_OK;
}

extern "C" HRESULT WINAPI D3DXCreateFontIndirectW(LPDIRECT3DDEVICE9 pDevice,
                                                  const D3DXFONT_DESCW *pDesc,
                                                  LPD3DXFONT *ppFont) {
    D3DXFONT_DESCA a;
    memset(&a, 0, sizeof(a));
    if (pDesc) {
        a.Height = pDesc->Height;
        a.Width = pDesc->Width;
        a.Weight = pDesc->Weight;
        a.MipLevels = pDesc->MipLevels;
        a.Italic = pDesc->Italic;
        a.CharSet = pDesc->CharSet;
        a.OutputPrecision = pDesc->OutputPrecision;
        a.Quality = pDesc->Quality;
        a.PitchAndFamily = pDesc->PitchAndFamily;
        for (int i = 0; i < 31 && pDesc->FaceName[i]; ++i)
            a.FaceName[i] = (char)(pDesc->FaceName[i] & 0xFF);
    }
    return D3DXCreateFontIndirectA(pDevice, &a, ppFont);
}

extern "C" HRESULT WINAPI D3DXCreateFontA(LPDIRECT3DDEVICE9 pDevice, INT Height, UINT Width,
                                          UINT Weight, UINT MipLevels, BOOL Italic,
                                          DWORD CharSet, DWORD OutputPrecision, DWORD Quality,
                                          DWORD PitchAndFamily, LPCSTR pFaceName,
                                          LPD3DXFONT *ppFont) {
    D3DXFONT_DESCA d;
    memset(&d, 0, sizeof(d));
    d.Height = Height; d.Width = Width; d.Weight = Weight; d.MipLevels = MipLevels;
    d.Italic = Italic; d.CharSet = CharSet; d.OutputPrecision = OutputPrecision;
    d.Quality = Quality; d.PitchAndFamily = PitchAndFamily;
    if (pFaceName) { strncpy(d.FaceName, pFaceName, 31); d.FaceName[31] = 0; }
    return D3DXCreateFontIndirectA(pDevice, &d, ppFont);
}

extern "C" HRESULT WINAPI D3DXCreateSprite(LPDIRECT3DDEVICE9 pDevice, LPD3DXSPRITE *ppSprite) {
    if (!pDevice || !ppSprite) return D3DERR_INVALIDCALL;
    *ppSprite = new RanD3DXSprite(pDevice);
    return D3D_OK;
}
