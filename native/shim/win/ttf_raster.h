// Minimal TrueType outline reader + scanline rasteriser.
//
// The client draws all of its text through GDI: it builds a DIB, selects an
// HFONT, calls ExtTextOut for every glyph and copies the result into a D3D
// texture atlas (Lib_Engine/DxCommon/D3DFont.cpp). Reproducing that on Android
// needs a glyph rasteriser, and the NDK ships none — so this is one, kept small
// and dependency-free rather than pulling in a font library.
//
// Scope is deliberately what the atlas builder needs: single glyphs, no shaping,
// no hinting, no kerning. Anti-aliasing is 4x vertical supersampling with
// analytic horizontal coverage, which is what makes small text readable.
#pragma once

#include <string>
#include <vector>
#include <map>

struct TtfGlyphBitmap {
    int width = 0, height = 0;
    int bearingX = 0;      // left edge relative to the pen position
    int bearingY = 0;      // TOP edge relative to the baseline (positive = above)
    int advance = 0;
    std::vector<unsigned char> coverage;   // width*height, 0..255
};

class TtfFace {
public:
    // Loads a .ttf/.otf-with-glyf file. Returns false for CFF-only fonts, which
    // this reader does not decode.
    bool Load(const char *path);
    bool Valid() const { return m_ok; }

    int  UnitsPerEm() const { return m_unitsPerEm; }
    // Unscaled, in font units. A cell height is expressed against these, not
    // against the em box.
    int  AscenderUnits() const  { return m_ascender; }
    int  DescenderUnits() const { return -m_descender; }   // positive
    int  Ascender(float scale) const;
    int  Descender(float scale) const;      // positive value

    int  GlyphIndex(unsigned cp) const;
    int  Advance(int gid, float scale) const;

    // Rasterises one glyph. `shear` skews x by shear*y for synthetic italic;
    // `boldPx` dilates horizontally for synthetic bold.
    bool Rasterise(int gid, float scale, float shear, int boldPx, TtfGlyphBitmap &out) const;

    // True when this face carries GPOS mark-attachment lookups. Thai needs
    // them: a tone mark over a tall consonant (ป ฝ ฟ ฬ) has to clear the
    // ascender, and a mark stacked on an upper vowel has to sit above it.
    bool HasMarkPositioning() const { return m_markLookup >= 0 || m_mkmkLookup >= 0; }

    // Is this glyph a combining mark (GDEF class 3)? A mark carries no advance
    // of its own and is placed relative to what precedes it.
    bool IsMark(int gid) const;

    // Where the mark at `markGid` should sit relative to the origin of
    // `baseGid`, in font units. Returns false when the pair has no anchor,
    // leaving the caller to draw the mark at its default outline position.
    // `baseIsMark` selects mkmk (mark over mark) instead of mark (mark over
    // base).
    bool MarkOffset(int baseGid, int markGid, bool baseIsMark,
                    int *dxUnits, int *dyUnits) const;

    // Applies the font's 'ccmp' feature to a run of glyph ids, in place.
    //
    // Thai needs it. A tone mark has two forms: a low one that sits directly on
    // a consonant and a high one that stacks above a vowel. The cmap always
    // gives the high form, and ccmp is what swaps in the low form when there is
    // no vowel underneath - and what moves the mark into the glyph range GPOS
    // knows how to attach at all. Skip it and every tone mark floats.
    void ApplyCcmp(std::vector<int> &glyphs) const;

private:
    struct Point { float x, y; bool onCurve; };

    bool collectContours(int gid, float scale, float shear, float dx, float dy,
                         std::vector<std::vector<Point> > &contours, int depth) const;

    unsigned tableOffset(const char *tag) const;

    // --- OpenType layout (GDEF/GPOS), enough for mark attachment ------------
    void parseLayout();
    int  coverageIndex(unsigned covOff, int gid) const;
    int  classOf(unsigned classDefOff, int gid) const;
    bool anchorAt(unsigned anchorOff, int *x, int *y) const;
    bool markAnchor(unsigned markArrayOff, int markCovIndex,
                    int *cls, int *x, int *y) const;
    bool applyMarkLookup(unsigned lookupOff, int baseGid, int markGid,
                         int *dx, int *dy) const;

    // GSUB. Each returns the change in run length at pos, or -1 for no match.
    int applySubstLookup(unsigned lookupOff, std::vector<int> &g, size_t pos) const;
    int applySingleSubst(unsigned sub, std::vector<int> &g, size_t pos) const;
    int applyMultipleSubst(unsigned sub, std::vector<int> &g, size_t pos) const;
    int applyChainContext(unsigned sub, std::vector<int> &g, size_t pos) const;

    std::vector<unsigned char> m_data;
    bool m_ok = false;
    unsigned m_glyf = 0, m_loca = 0, m_hmtx = 0, m_cmap = 0;
    int m_unitsPerEm = 1000, m_indexToLocFormat = 0, m_numGlyphs = 0, m_numHMetrics = 0;
    int m_ascender = 0, m_descender = 0;
    unsigned m_gdef = 0, m_gpos = 0;
    unsigned m_glyphClassDef = 0;          // GDEF, for "is this a mark"
    int m_markLookup = -1, m_mkmkLookup = -1;   // offsets into GPOS, -1 if absent
    unsigned m_gsub = 0, m_gsubLookupList = 0;
    std::vector<unsigned> m_ccmpLookups;        // GSUB lookup offsets, in order
    std::map<std::string, unsigned> m_tables;
};

// Picks a font file for a requested face name and language, from the fonts the
// device actually has. Returns an absolute path, never empty.
std::string RanFont_Resolve(const char *faceName, int codePage, bool bold);
// The metrics Windows would report for the face the client NAMED, which is not
// the face this device ended up rasterising. Every rect in the UI config was
// authored against Tahoma's line box, so a substituted face with a taller
// ascender silently drops every caption below where the layout expects it.
// Returns false when the named face is not in the table, leaving the caller to
// fall back to the real face's own metrics.
//
//   emPx     the em size in pixels, that is -LOGFONT.lfHeight
//   ascent   tmAscent, descent tmDescent, rounded the way GDI rounds them
bool RanFont_WinMetrics(const char *faceName, int emPx, int *ascent, int *descent);

// Turns a positive LOGFONT.lfHeight (a cell height) into the em size Windows
// would pick for the named face, so the glyphs come out the size that was asked
// for. Returns 0 when the face is not in the table.
int RanFont_WinEmForCellHeight(const char *faceName, int cellPx);
