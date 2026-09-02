// The slice of GDI the client uses to build its font atlases.
//
// CD3DFont::CreateFont (Lib_Engine/DxCommon/D3DFont.cpp) does exactly this:
//
//   CreateCompatibleDC -> CreateDIBSection -> CreateFont -> SelectObject
//   -> per character: GetTextExtentPoint32, SetTextColor/SetBkMode, ExtTextOut
//   -> read the DIB back and pack it into an A4R4G4B4 texture
//
// So a working ExtTextOut is all that stands between the client and text on
// screen. Glyphs come from ttf_raster.cpp over the device's own system fonts.
//
// Deliberately not implemented: clipping rectangles, character spacing arrays,
// text alignment modes other than the default top-left. The atlas builder uses
// none of them, and a stub that pretends otherwise would be worse than absent.

#include "windows.h"
#include "ttf_raster.h"

#include <android/log.h>
#include <map>
#include <string>
#include <vector>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "RanGdi", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "RanGdi", __VA_ARGS__)

namespace {

enum ObjKind { KIND_FONT = 0x464F4E54, KIND_BITMAP = 0x424D4150, KIND_BRUSH = 0x42525348 };

struct GdiObj { int kind; };

struct GdiFont : GdiObj {
    std::string path;
    //  The face the client named, kept apart from the file that actually got
    //  loaded: the layout was measured against the former's metrics.
    std::string requested;
    int   pixelSize = 12;
    bool  bold = false, italic = false;
    TtfFace *face = NULL;

    //  tmAscent / tmDescent as Windows would report them for the named face,
    //  falling back to the substituted face when the name is not one we know.
    int ascent() const {
        int a = 0, d = 0;
        if (RanFont_WinMetrics(requested.c_str(), pixelSize, &a, &d)) return a;
        return face ? face->Ascender((float)pixelSize / (float)face->UnitsPerEm()) : 0;
    }
    int lineHeight() const {
        int a = 0, d = 0;
        if (RanFont_WinMetrics(requested.c_str(), pixelSize, &a, &d)) return a + d;
        if (!face) return 0;
        const float sc = (float)pixelSize / (float)face->UnitsPerEm();
        return face->Ascender(sc) + face->Descender(sc);
    }
};

struct GdiBrush : GdiObj {
    COLORREF colour = 0;
};

struct GdiBitmap : GdiObj {
    int width = 0, height = 0;
    DWORD *bits = NULL;
    bool owned = false;
    ~GdiBitmap() { if (owned) delete[] bits; }
};

struct GdiDC {
    GdiBitmap *target = NULL;
    GdiFont   *font = NULL;
    COLORREF   textColor = 0x00FFFFFF;
    COLORREF   bkColor = 0;
    int        bkMode = 2;                 // OPAQUE
};

// Faces are shared: the client builds several atlases from the same file and
// re-reading a 400 KB font per size is pure waste.
std::map<std::string, TtfFace *> g_faces;

TtfFace *faceFor(const std::string &path) {
    std::map<std::string, TtfFace *>::iterator it = g_faces.find(path);
    if (it != g_faces.end()) return it->second;
    TtfFace *f = new TtfFace();
    if (!f->Load(path.c_str())) { delete f; f = NULL; }
    g_faces[path] = f;
    if (f) LOGI("loaded %s (unitsPerEm=%d)", path.c_str(), f->UnitsPerEm());
    return f;
}

// The codepage the client set, used to turn its byte strings into Unicode.
int g_codePage = 0;

// Windows-874 (Thai): 0x00-0x7F is ASCII, 0xA1-0xFB maps linearly onto the Thai
// block at U+0E01. The handful of holes map to nothing and are drawn blank.
unsigned cp874(unsigned char b) {
    if (b < 0x80) return b;
    if (b >= 0xA1 && b <= 0xFB) return 0x0E00u + (b - 0xA0);
    switch (b) {
        case 0x85: return 0x2026;   // ellipsis
        case 0x91: return 0x2018;
        case 0x92: return 0x2019;
        case 0x93: return 0x201C;
        case 0x94: return 0x201D;
        case 0x95: return 0x2022;
        case 0x96: return 0x2013;
        case 0x97: return 0x2014;
        default:   return 0;
    }
}

// Is this run UTF-8 rather than the legacy codepage? Both turn up: the loose GUI
// XML is UTF-8, while the game-text tables inside Gui.rcc are CP874. Nothing in
// the string says which, so it has to be inferred - and inferring it from
// well-formedness alone is not enough.
//
// CP874 Thai occupies 0xA1..0xFB, which overlaps the UTF-8 continuation range
// 0x80..0xBF, so short Thai words are frequently well-formed UTF-8 by accident:
// "ลบ" (delete) is C5 BA, which is also the UTF-8 encoding of U+017A,
// and it rendered on the character-select button as a lone "z-acute". Longer
// strings almost always trip over an invalid byte and fall back correctly, which
// is why this only ever showed up on the shortest labels.
//
// What separates the two is not validity but plausibility: a real UTF-8 Thai
// string decodes into the Thai block, whereas CP874 misread as UTF-8 lands in
// whatever block the arithmetic happens to reach - Latin Extended-A, Gujarati,
// Malayalam. So the decode must also produce only characters this client could
// actually be storing.
static bool plausibleForCodePage(unsigned cp, int codePage) {
    if (codePage != 874) return true;      // no better test for the others
    if (cp >= 0x0E00 && cp <= 0x0E7F) return true;             // Thai
    switch (cp) {                          // CP874's own non-Thai high characters
        case 0x20AC: case 0x2026: case 0x2018: case 0x2019:
        case 0x201C: case 0x201D: case 0x2022: case 0x2013: case 0x2014:
            return true;
        default:
            return false;
    }
}

bool looksUtf8(const char *s, int n, int codePage) {
    bool sawMulti = false;
    for (int i = 0; i < n; ) {
        unsigned char b = (unsigned char)s[i];
        if (b < 0x80) { ++i; continue; }
        int extra;
        unsigned cp;
        //  C0 and C1 can only ever start an overlong encoding, which is not
        //  valid UTF-8. Rejecting them here removes a whole family of two-byte
        //  Thai pairs that would otherwise be misread.
        if (b == 0xC0 || b == 0xC1) return false;
        if ((b & 0xE0) == 0xC0)      { extra = 1; cp = b & 0x1Fu; }
        else if ((b & 0xF0) == 0xE0) { extra = 2; cp = b & 0x0Fu; }
        else if ((b & 0xF8) == 0xF0) { extra = 3; cp = b & 0x07u; }
        else return false;
        if (i + extra > n - 1) return false;
        for (int k = 1; k <= extra; ++k) {
            unsigned char c = (unsigned char)s[i + k];
            if ((c & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (c & 0x3Fu);
        }
        //  Overlong, surrogate or out of range: not UTF-8.
        if ((extra == 2 && cp < 0x800) || (extra == 3 && cp < 0x10000)) return false;
        if (cp >= 0xD800 && cp <= 0xDFFF) return false;
        if (cp > 0x10FFFF) return false;
        if (!plausibleForCodePage(cp, codePage)) return false;
        sawMulti = true;
        i += extra + 1;
    }
    return sawMulti;
}

// Decodes one UTF-8 sequence, advancing i.
unsigned utf8Next(const char *s, int n, int &i) {
    unsigned char b = (unsigned char)s[i];
    if (b < 0x80) { ++i; return b; }
    int extra;
    unsigned cp;
    if ((b & 0xE0) == 0xC0)      { extra = 1; cp = b & 0x1F; }
    else if ((b & 0xF0) == 0xE0) { extra = 2; cp = b & 0x0F; }
    else if ((b & 0xF8) == 0xF0) { extra = 3; cp = b & 0x07; }
    else { ++i; return 0xFFFD; }
    if (i + extra >= n + 1) { ++i; return 0xFFFD; }
    for (int k = 1; k <= extra; ++k) cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
    i += extra + 1;
    return cp;
}

// Decodes the next character of a client string. Returns the Unicode code point
// and advances `i` by the number of bytes consumed.
unsigned nextChar(const char *s, int len, int &i) {
    unsigned char b = (unsigned char)s[i];
    if (b >= 0x80 && looksUtf8(s, len, g_codePage)) return utf8Next(s, len, i);
    switch (g_codePage) {
        case 874:
            ++i;
            return cp874(b);
        case 932: case 936: case 949: case 950:
            // Double-byte scripts need a real conversion table, which is a
            // separate job; the lead byte is consumed so the string still
            // advances instead of looping.
            if (b >= 0x81 && b <= 0xFE && i + 1 < len) { i += 2; return 0xFFFD; }
            ++i;
            return b;
        default:
            ++i;
            return b;                       // Latin-1 / CP1252 lower half
    }
}

GdiDC *asDC(HDC h) { return (GdiDC *)h; }

} // namespace

// The engine sets its codepage through CHARSET::SetCodePage; this is called from
// there so text conversion and DBCS detection agree with it.
extern "C" void RanText_SetCodePage(int cp) {
    g_codePage = cp;
    LOGI("codepage %d", cp);
}

extern "C" int RanText_GetCodePage(void) { return g_codePage; }

// Shared with MultiByteToWideChar so the engine's own conversions (D3DFontX
// converts to UTF-16 before drawing) agree with what this file rasterises.
extern "C" unsigned RanText_ByteToUnicode(int cp, unsigned char b);

// Converts a whole run, choosing UTF-8 or the legacy codepage. Returns the
// number of UTF-16 units written.
extern "C" int RanText_ToWide(int cp, const char *src, int srcLen,
                              unsigned short *dst, int dstLen) {
    if (!src || !dst || dstLen <= 0) return 0;
    if (cp <= 1) cp = g_codePage;
    int out = 0;
    if (looksUtf8(src, srcLen, cp)) {
        for (int i = 0; i < srcLen && out < dstLen; )
            dst[out++] = (unsigned short)utf8Next(src, srcLen, i);
        return out;
    }
    for (int i = 0; i < srcLen && out < dstLen; ++i)
        dst[out++] = (unsigned short)RanText_ByteToUnicode(cp, (unsigned char)src[i]);
    return out;
}

extern "C" unsigned RanText_ByteToUnicode(int cp, unsigned char b) {
    //  CP_ACP (0) and CP_OEMCP (1) mean "the system codepage". On the Thai Windows
    //  install this client is built for that IS 874, and the engine passes 0 in
    //  several places - taking it literally as Latin-1 is what turned every Thai
    //  string into .notdef boxes.
    if (cp <= 1) cp = g_codePage;
    if (cp == 874) return cp874(b);
    return b;                    // Latin-1 / CP1252 lower half
}

// ------------------------------------------------------------------ objects
HDC RanGdi_CreateCompatibleDC(HDC) {
    return (HDC) new GdiDC();
}

BOOL RanGdi_DeleteDC(HDC hdc) {
    delete asDC(hdc);
    return TRUE;
}

HBITMAP RanGdi_CreateDIBSection(HDC, const BITMAPINFO *pbmi, UINT,
                                           void **ppvBits, HANDLE, DWORD) {
    if (!pbmi) return NULL;
    int w = (int)pbmi->bmiHeader.biWidth;
    int h = (int)pbmi->bmiHeader.biHeight;
    // A negative height is a top-down DIB, which is what the font builder asks
    // for; the sign only describes row order, and rows are stored top-down here.
    if (h < 0) h = -h;
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) return NULL;

    GdiBitmap *bmp = new GdiBitmap();
    bmp->kind = KIND_BITMAP;
    bmp->width = w;
    bmp->height = h;
    bmp->bits = new DWORD[(size_t)w * h];
    bmp->owned = true;
    memset(bmp->bits, 0, (size_t)w * h * 4);
    if (ppvBits) *ppvBits = bmp->bits;
    return (HBITMAP)bmp;
}

HFONT RanGdi_CreateFontA(int nHeight, int, int, int, int weight, DWORD italic,
                                    DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD,
                                    LPCSTR faceName) {
    GdiFont *f = new GdiFont();
    f->kind = KIND_FONT;
    // GDI: negative height is the em size in pixels, positive is the cell
    // height. The client always passes a negative one (MulDiv of a point size).
    int px = nHeight < 0 ? -nHeight : nHeight;
    if (px <= 0) px = 12;
    //  A positive height is a cell height, so it has to be converted to an em
    //  size against the NAMED face - the one whose metrics the caller had in
    //  mind - before any glyph is rasterised at it.
    if (nHeight > 0) {
        const int em = RanFont_WinEmForCellHeight(faceName, px);
        if (em > 0) px = em;
    }
    f->pixelSize = px;
    f->requested = faceName ? faceName : "";
    f->bold = weight >= 600;
    f->italic = italic != 0;
    f->path = RanFont_Resolve(faceName, g_codePage, f->bold);
    f->face = faceFor(f->path);
    if (!f->face) { LOGE("no usable font for %s", faceName ? faceName : "(null)"); }
    return (HFONT)f;
}

HGDIOBJ RanGdi_SelectObject(HDC hdc, HGDIOBJ obj) {
    GdiDC *dc = asDC(hdc);
    if (!dc || !obj) return NULL;
    GdiObj *o = (GdiObj *)obj;
    if (o->kind == KIND_FONT)   { HGDIOBJ prev = (HGDIOBJ)dc->font;   dc->font = (GdiFont *)o;   return prev; }
    if (o->kind == KIND_BITMAP) { HGDIOBJ prev = (HGDIOBJ)dc->target; dc->target = (GdiBitmap *)o; return prev; }
    return NULL;
}

BOOL RanGdi_DeleteObject(HGDIOBJ obj) {
    if (!obj) return TRUE;
    GdiObj *o = (GdiObj *)obj;
    // The face itself is cached and shared, so only the wrapper goes.
    if (o->kind == KIND_FONT)   { delete (GdiFont *)o;   return TRUE; }
    if (o->kind == KIND_BITMAP) { delete (GdiBitmap *)o; return TRUE; }
    if (o->kind == KIND_BRUSH)  { delete (GdiBrush *)o;  return TRUE; }
    return TRUE;
}

COLORREF RanGdi_SetTextColor(HDC hdc, COLORREF c) {
    GdiDC *dc = asDC(hdc);
    if (!dc) return 0;
    COLORREF prev = dc->textColor;
    dc->textColor = c;
    return prev;
}
COLORREF RanGdi_SetBkColor(HDC hdc, COLORREF c) {
    GdiDC *dc = asDC(hdc);
    if (!dc) return 0;
    COLORREF prev = dc->bkColor;
    dc->bkColor = c;
    return prev;
}
int RanGdi_SetBkMode(HDC hdc, int mode) {
    GdiDC *dc = asDC(hdc);
    if (!dc) return 0;
    int prev = dc->bkMode;
    dc->bkMode = mode;
    return prev;
}

// ------------------------------------------------------------------- text
BOOL RanGdi_GetTextExtentPoint32A(HDC hdc, LPCSTR str, int len, LPSIZE size) {
    if (!size) return FALSE;
    size->cx = 0; size->cy = 0;
    GdiDC *dc = asDC(hdc);
    if (!dc || !dc->font || !dc->font->face || !str) return FALSE;

    TtfFace *face = dc->font->face;
    float scale = (float)dc->font->pixelSize / (float)face->UnitsPerEm();
    if (len < 0) len = (int)strlen(str);

    int cx = 0;
    for (int i = 0; i < len; ) {
        unsigned cp = nextChar(str, len, i);
        int gid = face->GlyphIndex(cp);
        //  A combining mark takes no width of its own.
        if (face->IsMark(gid)) continue;
        cx += face->Advance(gid, scale) + (dc->font->bold ? 1 : 0);
    }
    size->cx = cx;
    size->cy = dc->font->lineHeight();
    return TRUE;
}

BOOL RanGdi_ExtTextOutA(HDC hdc, int x, int y, UINT options, const RECT *rc,
                                   LPCSTR str, UINT count, const INT *) {
    GdiDC *dc = asDC(hdc);
    if (!dc || !dc->target || !dc->font || !dc->font->face || !str) return FALSE;

    GdiBitmap *bmp = dc->target;
    TtfFace *face = dc->font->face;
    const float scale = (float)dc->font->pixelSize / (float)face->UnitsPerEm();
    const int ascent = dc->font->ascent();
    const int boldPx = dc->font->bold ? 1 : 0;
    const float shear = dc->font->italic ? 0.2f : 0.0f;

    const int r = (int)((dc->textColor) & 0xFF);
    const int g = (int)((dc->textColor >> 8) & 0xFF);
    const int b = (int)((dc->textColor >> 16) & 0xFF);

    // OPAQUE fills the text box with the background colour first. The atlas
    // builder relies on this to clear the cell it is about to draw into.
    if (dc->bkMode == 2 /*OPAQUE*/) {
        SIZE sz;
        if (RanGdi_GetTextExtentPoint32A(hdc, str, (int)count, &sz)) {
            int bx0 = x, by0 = y, bx1 = x + sz.cx, by1 = y + sz.cy;
            if (rc && (options & 2 /*ETO_OPAQUE*/)) {
                bx0 = rc->left; by0 = rc->top; bx1 = rc->right; by1 = rc->bottom;
            }
            DWORD bk = (DWORD)(((dc->bkColor & 0xFF) << 16) |
                               (((dc->bkColor >> 8) & 0xFF) << 8) |
                               ((dc->bkColor >> 16) & 0xFF));
            for (int py = by0; py < by1; ++py) {
                if (py < 0 || py >= bmp->height) continue;
                for (int px = bx0; px < bx1; ++px) {
                    if (px < 0 || px >= bmp->width) continue;
                    bmp->bits[(size_t)py * bmp->width + px] = bk;
                }
            }
        }
    }

    int pen = x;
    const int len = (int)count;
    for (int i = 0; i < len; ) {
        unsigned cp = nextChar(str, len, i);
        int gid = face->GlyphIndex(cp);
        TtfGlyphBitmap gb;
        if (!face->Rasterise(gid, scale, shear, boldPx, gb)) { continue; }

        for (int gy = 0; gy < gb.height; ++gy) {
            int py = y + ascent - gb.bearingY + gy;
            if (py < 0 || py >= bmp->height) continue;
            const unsigned char *srow = &gb.coverage[(size_t)gy * gb.width];
            DWORD *drow = &bmp->bits[(size_t)py * bmp->width];
            for (int gx = 0; gx < gb.width; ++gx) {
                int px = pen + gb.bearingX + gx;
                if (px < 0 || px >= bmp->width) continue;
                unsigned a = srow[gx];
                if (!a) continue;
                DWORD d = drow[px];
                int dr = (int)((d >> 16) & 0xFF), dg = (int)((d >> 8) & 0xFF), db = (int)(d & 0xFF);
                // Source-over in the DIB, so the shadow passes the client draws
                // underneath the glyph survive where the glyph is transparent.
                int nr = (r * (int)a + dr * (255 - (int)a)) / 255;
                int ng = (g * (int)a + dg * (255 - (int)a)) / 255;
                int nb = (b * (int)a + db * (255 - (int)a)) / 255;
                drow[px] = (DWORD)((nr << 16) | (ng << 8) | nb);
            }
        }
        //  A combining mark takes no advance; GetTextExtentPoint32A skips it
        //  too, and the two have to agree or measured and drawn widths differ.
        if (!face->IsMark(gid)) pen += gb.advance;
    }
    return TRUE;
}

//  UTF-16 (widened into 32-bit wchar_t) to UTF-8.
//
//  MultiByteToWideChar in this port writes UTF-16 code units and widens them on
//  the way out, so a wchar_t here is one UTF-16 unit, not a code point. Surrogate
//  pairs therefore have to be joined back up before encoding.
static void wideToUtf8(LPCWSTR src, int count, std::string &out) {
    out.clear();
    if (!src) return;
    if (count < 0) { count = 0; while (src[count]) ++count; }
    for (int i = 0; i < count; ++i) {
        unsigned cp = (unsigned)src[i] & 0xFFFF;
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < count) {
            const unsigned lo = (unsigned)src[i + 1] & 0xFFFF;
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                ++i;
            }
        }
        if (cp < 0x80) out.push_back((char)cp);
        else if (cp < 0x800) {
            out.push_back((char)(0xC0 | (cp >> 6)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back((char)(0xE0 | (cp >> 12)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else {
            out.push_back((char)(0xF0 | (cp >> 18)));
            out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
    }
}

//  The wide draw and measure, which the text-texture cache is written against.
//
//  Both were stubs that returned success and did nothing, which is why enabling
//  CTextUtil produced blank strings: it builds every cached string with
//  ExtTextOutW over a FillRect'd cell, and measures it with
//  GetTextExtentPoint32W. Nothing reported an error, so the textures simply came
//  out empty.
BOOL RanGdi_ExtTextOutW(HDC hdc, int x, int y, UINT options, const RECT *rc,
                        LPCWSTR str, UINT count, const INT *dx) {
    std::string utf8;
    wideToUtf8(str, (int)count, utf8);
    return RanGdi_ExtTextOutA(hdc, x, y, options, rc, utf8.c_str(), (UINT)utf8.size(), dx);
}

//  Deliberately reports nothing, and that is not laziness.
//
//  CD3DFontX::ConvWideAndTextExtent measures with this and falls back to
//  m_pd3dxFont->DrawTextW when it comes back zero:
//
//      GetTextExtentPoint32W( m_hd3dxDC, ... );
//      if ( Size.cx == 0 || Size.cy == 0 ) ConvWideAndTextExtent98( ... );
//
//  The fallback is the call that actually draws the glyphs, and every layout in
//  this port is measured against it. Answering here instead put GDI numbers into
//  use, and they disagree with what is drawn - gdi=150 against d3dx=116 on the
//  same string - which is half a string of drift on every centred label.
//
//  The A version stays real: the atlas builder in D3DFont.cpp needs it, and that
//  path measures and draws with the same DC.
BOOL RanGdi_GetTextExtentPoint32W(HDC, LPCWSTR, int, LPSIZE) {
    return TRUE;
}

HBRUSH RanGdi_CreateSolidBrush(COLORREF c) {
    GdiBrush *b = new GdiBrush();
    b->kind = KIND_BRUSH;
    b->colour = c;
    return (HBRUSH)b;
}

//  Flat fill of a rectangle, clipped to the bitmap.
//
//  The cache uses this to clear the cell before drawing the string into it;
//  without it every cached string was drawn over whatever the last one left.
int RanGdi_FillRect(HDC hdc, const RECT *rc, HBRUSH brush) {
    GdiDC *dc = asDC(hdc);
    if (!dc || !dc->target || !rc) return 0;

    COLORREF c = 0;
    if (brush) {
        GdiObj *o = (GdiObj *)brush;
        if (o->kind == KIND_BRUSH) c = ((GdiBrush *)o)->colour;
    }
    //  COLORREF is 0x00BBGGRR; the bitmap holds 0x00RRGGBB.
    const DWORD px = (DWORD)((((c) & 0xFF) << 16) | (((c) >> 8 & 0xFF) << 8) | ((c) >> 16 & 0xFF));

    GdiBitmap *bmp = dc->target;
    for (int py = rc->top; py < rc->bottom; ++py) {
        if (py < 0 || py >= bmp->height) continue;
        DWORD *row = &bmp->bits[(size_t)py * bmp->width];
        for (int pxx = rc->left; pxx < rc->right; ++pxx) {
            if (pxx < 0 || pxx >= bmp->width) continue;
            row[pxx] = px;
        }
    }
    return 1;
}

BOOL RanGdi_TextOutA(HDC hdc, int x, int y, LPCSTR str, int count) {
    return RanGdi_ExtTextOutA(hdc, x, y, 0, NULL, str, (UINT)count, NULL);
}

// GDI reports the device resolution here; the client turns a point size into
// pixels with MulDiv(height, LOGPIXELSY, 72). Reporting Windows' default 96 keeps
// text the same physical size it is on PC.
int RanGdi_GetDeviceCaps(HDC, int index) {
    switch (index) {
        case 88: case 90: return 96;     // LOGPIXELSX / LOGPIXELSY
        case 8:  return 1024;            // HORZRES
        case 10: return 768;             // VERTRES
        case 12: return 32;              // BITSPIXEL
        case 14: return 1;               // PLANES
        default: return 0;
    }
}
