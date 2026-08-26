//  OpenType mark attachment, enough of it for Thai.
//
//  Thai does not come out right by drawing glyphs at their default outline
//  positions. A tone mark over a tall consonant (ป ฝ ฟ ฬ) has to clear the
//  ascender, and a tone mark over an upper vowel has to sit above it rather than
//  through it. The font carries that knowledge in GPOS: the 'mark' feature
//  attaches a mark to a base glyph, 'mkmk' attaches a mark to another mark.
//
//  On the PC this is Uniscribe's job, inside ID3DXFont — which is exactly why
//  D3DFontX.cpp goes out of its way to load d3dx9_35.dll, "which shapes Thai
//  correctly via Uniscribe". Nothing does it here, so it is done by hand.
//
//  Only what Thai needs is implemented: lookup types 4 (MarkBase) and 6
//  (MarkMark), coverage formats 1 and 2, class definitions 1 and 2, and anchor
//  formats 1-3. The device-table hinting in anchor formats 2 and 3 is ignored;
//  it only moves things sub-pixel.
#include "ttf_raster.h"

#include <string.h>

namespace {
inline unsigned rdU16(const unsigned char *p) { return ((unsigned)p[0] << 8) | p[1]; }
inline int      rdS16(const unsigned char *p) { return (int)(short)rdU16(p); }
}

void TtfFace::parseLayout() {
    if (m_data.empty()) return;
    const unsigned char *d = &m_data[0];
    const size_t n = m_data.size();

    //  GDEF is what tells us a glyph is a mark. Without it every glyph looks
    //  like a base and nothing ever attaches.
    if (m_gdef && m_gdef + 6 <= n) {
        const unsigned off = rdU16(d + m_gdef + 4);        // GlyphClassDef
        if (off) m_glyphClassDef = m_gdef + off;
    }

    //  GSUB 'ccmp'. Its lookups have to be applied in list order, so they are
    //  collected rather than reduced to one.
    if (m_gsub && m_gsub + 10 <= n) {
        const unsigned featureList = m_gsub + rdU16(d + m_gsub + 6);
        const unsigned lookupList  = m_gsub + rdU16(d + m_gsub + 8);
        if (featureList + 2 <= n && lookupList + 2 <= n) {
            m_gsubLookupList = lookupList;
            const int featureCount = (int)rdU16(d + featureList);
            const int lookupCount  = (int)rdU16(d + lookupList);
            for (int i = 0; i < featureCount; ++i) {
                const unsigned rec = featureList + 2 + (unsigned)i * 6;
                if (rec + 6 > n) break;
                if (memcmp(d + rec, "ccmp", 4) != 0) continue;
                const unsigned feat = featureList + rdU16(d + rec + 4);
                if (feat + 4 > n) continue;
                const int nLookups = (int)rdU16(d + feat + 2);
                for (int k = 0; k < nLookups; ++k) {
                    const unsigned io = feat + 4 + (unsigned)k * 2;
                    if (io + 2 > n) break;
                    const int li = (int)rdU16(d + io);
                    if (li < 0 || li >= lookupCount) continue;
                    const unsigned lo = lookupList + 2 + (unsigned)li * 2;
                    if (lo + 2 > n) continue;
                    m_ccmpLookups.push_back(lookupList + rdU16(d + lo));
                }
            }
        }
    }

    if (!m_gpos || m_gpos + 10 > n) return;
    const unsigned featureList = m_gpos + rdU16(d + m_gpos + 6);
    const unsigned lookupList  = m_gpos + rdU16(d + m_gpos + 8);
    if (featureList + 2 > n || lookupList + 2 > n) return;

    //  Read the feature list directly instead of walking ScriptList first.
    //  Thai fonts apply mark/mkmk under every script they cover, and a face
    //  whose features we could only reach through a language system we failed
    //  to match would silently position nothing at all.
    const int featureCount = (int)rdU16(d + featureList);
    const int lookupCount  = (int)rdU16(d + lookupList);
    for (int i = 0; i < featureCount; ++i) {
        const unsigned rec = featureList + 2 + (unsigned)i * 6;
        if (rec + 6 > n) break;
        const bool isMark = (memcmp(d + rec, "mark", 4) == 0);
        const bool isMkmk = (memcmp(d + rec, "mkmk", 4) == 0);
        if (!isMark && !isMkmk) continue;

        const unsigned feat = featureList + rdU16(d + rec + 4);
        if (feat + 4 > n) continue;
        const int nLookups = (int)rdU16(d + feat + 2);
        for (int k = 0; k < nLookups; ++k) {
            const unsigned idxOff = feat + 4 + (unsigned)k * 2;
            if (idxOff + 2 > n) break;
            const int li = (int)rdU16(d + idxOff);
            if (li < 0 || li >= lookupCount) continue;
            const unsigned lOffOff = lookupList + 2 + (unsigned)li * 2;
            if (lOffOff + 2 > n) continue;
            const unsigned lookup = lookupList + rdU16(d + lOffOff);
            if (lookup + 6 > n) continue;
            const int type = (int)rdU16(d + lookup);
            if (isMark && type == 4 && m_markLookup < 0) m_markLookup = (int)lookup;
            if (isMkmk && type == 6 && m_mkmkLookup < 0) m_mkmkLookup = (int)lookup;
        }
    }
}

//  Index of `gid` within a coverage table, or -1 when it is not covered.
int TtfFace::coverageIndex(unsigned covOff, int gid) const {
    if (m_data.empty()) return -1;
    const unsigned char *d = &m_data[0];
    const size_t n = m_data.size();
    if (!covOff || covOff + 4 > n) return -1;

    const int format = (int)rdU16(d + covOff);
    const int count  = (int)rdU16(d + covOff + 2);
    if (format == 1) {
        //  A sorted glyph list, so a binary search.
        int lo = 0, hi = count - 1;
        while (lo <= hi) {
            const int mid = (lo + hi) / 2;
            const unsigned e = covOff + 4 + (unsigned)mid * 2;
            if (e + 2 > n) return -1;
            const int g = (int)rdU16(d + e);
            if (g == gid) return mid;
            if (g < gid) lo = mid + 1; else hi = mid - 1;
        }
        return -1;
    }
    if (format == 2) {
        //  Ranges, each carrying the coverage index of its first glyph.
        for (int i = 0; i < count; ++i) {
            const unsigned r = covOff + 4 + (unsigned)i * 6;
            if (r + 6 > n) break;
            const int first = (int)rdU16(d + r), last = (int)rdU16(d + r + 2);
            if (gid >= first && gid <= last) return (int)rdU16(d + r + 4) + (gid - first);
        }
    }
    return -1;
}

int TtfFace::classOf(unsigned off, int gid) const {
    if (m_data.empty()) return 0;
    const unsigned char *d = &m_data[0];
    const size_t n = m_data.size();
    if (!off || off + 4 > n) return 0;

    const int format = (int)rdU16(d + off);
    if (format == 1) {
        const int start = (int)rdU16(d + off + 2);
        const int count = (int)rdU16(d + off + 4);
        if (gid < start || gid >= start + count) return 0;
        const unsigned e = off + 6 + (unsigned)(gid - start) * 2;
        if (e + 2 > n) return 0;
        return (int)rdU16(d + e);
    }
    if (format == 2) {
        const int count = (int)rdU16(d + off + 2);
        for (int i = 0; i < count; ++i) {
            const unsigned r = off + 4 + (unsigned)i * 6;
            if (r + 6 > n) break;
            if (gid >= (int)rdU16(d + r) && gid <= (int)rdU16(d + r + 2))
                return (int)rdU16(d + r + 4);
        }
    }
    return 0;
}

bool TtfFace::IsMark(int gid) const {
    //  GDEF glyph class 3 is "mark". With no GDEF the face cannot tell us, and
    //  calling everything a base is the answer that changes nothing.
    return m_glyphClassDef ? classOf(m_glyphClassDef, gid) == 3 : false;
}

bool TtfFace::anchorAt(unsigned off, int *x, int *y) const {
    if (m_data.empty()) return false;
    const unsigned char *d = &m_data[0];
    if (!off || off + 6 > m_data.size()) return false;
    //  Formats 1, 2 and 3 share the same leading x/y; the rest is hinting.
    if (x) *x = rdS16(d + off + 2);
    if (y) *y = rdS16(d + off + 4);
    return true;
}

bool TtfFace::markAnchor(unsigned markArrayOff, int covIndex,
                         int *cls, int *x, int *y) const {
    if (m_data.empty()) return false;
    const unsigned char *d = &m_data[0];
    const size_t n = m_data.size();
    if (!markArrayOff || markArrayOff + 2 > n) return false;

    const int count = (int)rdU16(d + markArrayOff);
    if (covIndex < 0 || covIndex >= count) return false;
    const unsigned rec = markArrayOff + 2 + (unsigned)covIndex * 4;
    if (rec + 4 > n) return false;
    if (cls) *cls = (int)rdU16(d + rec);
    const unsigned anchor = rdU16(d + rec + 2);
    if (!anchor) return false;
    return anchorAt(markArrayOff + anchor, x, y);
}

bool TtfFace::applyMarkLookup(unsigned lookup, int baseGid, int markGid,
                              int *dxOut, int *dyOut) const {
    if (m_data.empty()) return false;
    const unsigned char *d = &m_data[0];
    const size_t n = m_data.size();
    if (lookup + 6 > n) return false;

    const int subCount = (int)rdU16(d + lookup + 4);
    for (int sIdx = 0; sIdx < subCount; ++sIdx) {
        const unsigned sOffOff = lookup + 6 + (unsigned)sIdx * 2;
        if (sOffOff + 2 > n) break;
        const unsigned sub = lookup + rdU16(d + sOffOff);
        if (sub + 12 > n) continue;
        if (rdU16(d + sub) != 1) continue;          // only subtable format 1 exists

        const unsigned markCov   = sub + rdU16(d + sub + 2);
        const unsigned baseCov   = sub + rdU16(d + sub + 4);
        const int      classCount = (int)rdU16(d + sub + 6);
        const unsigned markArray = sub + rdU16(d + sub + 8);
        const unsigned baseArray = sub + rdU16(d + sub + 10);
        if (classCount <= 0) continue;

        const int mi = coverageIndex(markCov, markGid);
        const int bi = coverageIndex(baseCov, baseGid);
        if (mi < 0 || bi < 0) continue;

        int cls = 0, mx = 0, my = 0;
        if (!markAnchor(markArray, mi, &cls, &mx, &my)) continue;
        if (cls < 0 || cls >= classCount) continue;

        //  BaseArray (type 4) and Mark2Array (type 6) have the same shape: a
        //  count, then one anchor offset per class for each covered glyph.
        if (baseArray + 2 > n) continue;
        const int baseCount = (int)rdU16(d + baseArray);
        if (bi >= baseCount) continue;
        const unsigned row  = baseArray + 2 + (unsigned)bi * (unsigned)classCount * 2;
        const unsigned cell = row + (unsigned)cls * 2;
        if (cell + 2 > n) continue;
        const unsigned anchor = rdU16(d + cell);
        if (!anchor) continue;

        int bx = 0, by = 0;
        if (!anchorAt(baseArray + anchor, &bx, &by)) continue;

        //  The mark is placed so that its own anchor lands on the base's.
        if (dxOut) *dxOut = bx - mx;
        if (dyOut) *dyOut = by - my;
        return true;
    }
    return false;
}

bool TtfFace::MarkOffset(int baseGid, int markGid, bool baseIsMark,
                         int *dx, int *dy) const {
    if (!m_ok) return false;
    const int lookup = baseIsMark ? m_mkmkLookup : m_markLookup;
    if (lookup < 0) return false;
    return applyMarkLookup((unsigned)lookup, baseGid, markGid, dx, dy);
}


// ------------------------------------------------------------------ GSUB
//
//  Only what 'ccmp' uses: single substitution (type 1), multiple substitution
//  (type 2) and chained context (type 6 format 3). The chained form is the one
//  that matters — it is how the font says "this tone mark is preceded by a
//  vowel, so use the high variant" — and it drives the other two through nested
//  lookup records.

int TtfFace::applySingleSubst(unsigned sub, std::vector<int> &g, size_t pos) const {
    const unsigned char *d = &m_data[0];
    const size_t n = m_data.size();
    if (sub + 6 > n) return -1;
    const int fmt = (int)rdU16(d + sub);
    const int ci = coverageIndex(sub + rdU16(d + sub + 2), g[pos]);
    if (ci < 0) return -1;
    if (fmt == 1) {
        g[pos] = (g[pos] + rdS16(d + sub + 4)) & 0xFFFF;
        return 0;
    }
    if (fmt == 2) {
        const int count = (int)rdU16(d + sub + 4);
        if (ci >= count) return -1;
        const unsigned e = sub + 6 + (unsigned)ci * 2;
        if (e + 2 > n) return -1;
        g[pos] = (int)rdU16(d + e);
        return 0;
    }
    return -1;
}

int TtfFace::applyMultipleSubst(unsigned sub, std::vector<int> &g, size_t pos) const {
    const unsigned char *d = &m_data[0];
    const size_t n = m_data.size();
    if (sub + 6 > n) return -1;
    if (rdU16(d + sub) != 1) return -1;
    const int ci = coverageIndex(sub + rdU16(d + sub + 2), g[pos]);
    if (ci < 0) return -1;
    const int count = (int)rdU16(d + sub + 4);
    if (ci >= count) return -1;
    const unsigned so = sub + 6 + (unsigned)ci * 2;
    if (so + 2 > n) return -1;
    const unsigned seq = sub + rdU16(d + so);
    if (seq + 2 > n) return -1;
    const int glyphCount = (int)rdU16(d + seq);
    if (glyphCount <= 0) return -1;

    std::vector<int> out;
    out.reserve((size_t)glyphCount);
    for (int i = 0; i < glyphCount; ++i) {
        const unsigned e = seq + 2 + (unsigned)i * 2;
        if (e + 2 > n) return -1;
        out.push_back((int)rdU16(d + e));
    }
    g[pos] = out[0];
    if (glyphCount > 1)
        g.insert(g.begin() + (long)pos + 1, out.begin() + 1, out.end());
    return glyphCount - 1;
}

int TtfFace::applyChainContext(unsigned sub, std::vector<int> &g, size_t pos) const {
    const unsigned char *d = &m_data[0];
    const size_t n = m_data.size();
    if (sub + 4 > n) return -1;
    if (rdU16(d + sub) != 3) return -1;             // only format 3 is used here

    unsigned p = sub + 2;
    const int backCount = (int)rdU16(d + p); p += 2;
    //  Backtrack coverages are listed nearest-first, walking away from pos.
    for (int i = 0; i < backCount; ++i, p += 2) {
        if (p + 2 > n) return -1;
        if (pos < (size_t)(i + 1)) return -1;
        if (coverageIndex(sub + rdU16(d + p), g[pos - 1 - (size_t)i]) < 0) return -1;
    }
    if (p + 2 > n) return -1;
    const int inputCount = (int)rdU16(d + p); p += 2;
    for (int i = 0; i < inputCount; ++i, p += 2) {
        if (p + 2 > n) return -1;
        if (pos + (size_t)i >= g.size()) return -1;
        if (coverageIndex(sub + rdU16(d + p), g[pos + (size_t)i]) < 0) return -1;
    }
    if (p + 2 > n) return -1;
    const int aheadCount = (int)rdU16(d + p); p += 2;
    for (int i = 0; i < aheadCount; ++i, p += 2) {
        if (p + 2 > n) return -1;
        const size_t at = pos + (size_t)inputCount + (size_t)i;
        if (at >= g.size()) return -1;
        if (coverageIndex(sub + rdU16(d + p), g[at]) < 0) return -1;
    }

    if (p + 2 > n) return -1;
    const int recCount = (int)rdU16(d + p); p += 2;
    int grew = 0;
    for (int i = 0; i < recCount; ++i, p += 4) {
        if (p + 4 > n) break;
        const int seqIdx = (int)rdU16(d + p);
        const int lookupIdx = (int)rdU16(d + p + 2);
        if (!m_gsubLookupList || m_gsubLookupList + 2 > n) break;
        const int lookupCount = (int)rdU16(d + m_gsubLookupList);
        if (lookupIdx < 0 || lookupIdx >= lookupCount) continue;
        const unsigned lo = m_gsubLookupList + 2 + (unsigned)lookupIdx * 2;
        if (lo + 2 > n) continue;
        const size_t at = pos + (size_t)seqIdx + (size_t)grew;
        if (at >= g.size()) continue;
        const int r = applySubstLookup(m_gsubLookupList + rdU16(d + lo), g, at);
        if (r > 0) grew += r;
    }
    return grew;
}

int TtfFace::applySubstLookup(unsigned lookup, std::vector<int> &g, size_t pos) const {
    const unsigned char *d = &m_data[0];
    const size_t n = m_data.size();
    if (lookup + 6 > n || pos >= g.size()) return -1;
    const int type = (int)rdU16(d + lookup);
    const int subCount = (int)rdU16(d + lookup + 4);
    for (int i = 0; i < subCount; ++i) {
        const unsigned so = lookup + 6 + (unsigned)i * 2;
        if (so + 2 > n) break;
        const unsigned sub = lookup + rdU16(d + so);
        int r = -1;
        if (type == 1)      r = applySingleSubst(sub, g, pos);
        else if (type == 2) r = applyMultipleSubst(sub, g, pos);
        else if (type == 6) r = applyChainContext(sub, g, pos);
        if (r >= 0) return r;
    }
    return -1;
}

void TtfFace::ApplyCcmp(std::vector<int> &glyphs) const {
    if (!m_ok || m_ccmpLookups.empty() || glyphs.empty()) return;
    //  Lookups apply in list order, each sweeping the whole run left to right.
    //  That order is what makes the Thai case work: one lookup rewrites every
    //  tone mark to its low form, a later one puts back the high form where a
    //  vowel precedes it.
    for (size_t li = 0; li < m_ccmpLookups.size(); ++li) {
        for (size_t i = 0; i < glyphs.size(); ) {
            const int r = applySubstLookup(m_ccmpLookups[li], glyphs, i);
            i += (r > 0) ? (size_t)r + 1 : 1;
        }
    }
}
