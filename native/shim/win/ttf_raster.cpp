// TrueType outline reader + scanline rasteriser. See ttf_raster.h for scope.

#include "ttf_raster.h"
#include "../platform/ran_plat.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>   // strcasecmp, for matching the face name the client asked for
#include <math.h>
#include <dirent.h>

#define LOGI(...) RanPlat_Log(RANLOG_INFO,  "RanFont", __VA_ARGS__)
#define LOGE(...) RanPlat_Log(RANLOG_ERROR, "RanFont", __VA_ARGS__)

namespace {

inline unsigned rdU16(const unsigned char *p) { return ((unsigned)p[0] << 8) | p[1]; }
inline int      rdS16(const unsigned char *p) { return (int)(short)rdU16(p); }
inline unsigned rdU32(const unsigned char *p) {
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3];
}

// Glyph flags
enum { ON_CURVE = 1, X_SHORT = 2, Y_SHORT = 4, REPEAT_FLAG = 8, X_SAME = 16, Y_SAME = 32 };
// Composite flags
enum {
    ARG_1_AND_2_ARE_WORDS = 0x0001, ARGS_ARE_XY_VALUES = 0x0002,
    WE_HAVE_A_SCALE = 0x0008, MORE_COMPONENTS = 0x0020,
    X_AND_Y_SCALE = 0x0040, TWO_BY_TWO = 0x0080
};

struct Edge { float x0, y0, x1, y1; int dir; };

} // namespace

// ------------------------------------------------------------------ loading
unsigned TtfFace::tableOffset(const char *tag) const {
    std::map<std::string, unsigned>::const_iterator it = m_tables.find(std::string(tag));
    return (it == m_tables.end()) ? 0 : it->second;
}

bool TtfFace::Load(const char *path) {
    m_ok = false;
    FILE *f = fopen(path, "rb");
    if (!f) { LOGE("cannot open %s", path); return false; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 12) { fclose(f); return false; }
    m_data.resize((size_t)n);
    size_t got = fread(&m_data[0], 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) return false;

    const unsigned char *d = &m_data[0];
    unsigned tag = rdU32(d);
    // 0x00010000 = TrueType outlines, 'true' = the Apple variant. 'OTTO' is CFF,
    // which has no glyf table and is refused rather than half-parsed.
    if (tag != 0x00010000u && tag != 0x74727565u) {
        LOGE("%s: unsupported sfnt 0x%08X", path, tag);
        return false;
    }
    int numTables = (int)rdU16(d + 4);
    for (int i = 0; i < numTables; ++i) {
        const unsigned char *rec = d + 12 + 16 * i;
        if ((size_t)(rec - d) + 16 > m_data.size()) break;
        std::string t((const char *)rec, 4);
        m_tables[t] = rdU32(rec + 8);
    }

    unsigned head = tableOffset("head");
    unsigned hhea = tableOffset("hhea");
    unsigned maxp = tableOffset("maxp");
    m_glyf = tableOffset("glyf");
    m_loca = tableOffset("loca");
    m_hmtx = tableOffset("hmtx");
    m_cmap = tableOffset("cmap");
    if (!head || !hhea || !maxp || !m_glyf || !m_loca || !m_cmap) {
        LOGE("%s: missing a required table", path);
        return false;
    }

    m_unitsPerEm = (int)rdU16(d + head + 18);
    if (m_unitsPerEm <= 0) m_unitsPerEm = 1000;
    m_indexToLocFormat = rdS16(d + head + 50);
    m_ascender = rdS16(d + hhea + 4);
    m_descender = rdS16(d + hhea + 6);
    m_numHMetrics = (int)rdU16(d + hhea + 34);
    m_numGlyphs = (int)rdU16(d + maxp + 4);

    //  Optional layout tables. Thai needs the mark-attachment lookups in GPOS;
    //  see ttf_layout.cpp. A face without them still renders, just without
    //  marks being repositioned.
    m_gdef = tableOffset("GDEF");
    m_gpos = tableOffset("GPOS");
    m_gsub = tableOffset("GSUB");
    parseLayout();

    m_ok = true;
    return true;
}

int TtfFace::Ascender(float scale) const  { return (int)ceilf(m_ascender * scale); }
int TtfFace::Descender(float scale) const { return (int)ceilf(-m_descender * scale); }

// -------------------------------------------------------------------- cmap
int TtfFace::GlyphIndex(unsigned cp) const {
    if (!m_ok) return 0;
    const unsigned char *d = &m_data[0];
    const unsigned char *cm = d + m_cmap;
    int n = (int)rdU16(cm + 2);

    unsigned best = 0;
    int bestScore = -1;
    for (int i = 0; i < n; ++i) {
        const unsigned char *rec = cm + 4 + 8 * i;
        int plat = (int)rdU16(rec), enc = (int)rdU16(rec + 2);
        unsigned off = rdU32(rec + 4);
        int score = -1;
        if (plat == 3 && enc == 10) score = 4;        // Windows UCS-4
        else if (plat == 3 && enc == 1) score = 3;    // Windows BMP
        else if (plat == 0) score = 2;                // Unicode
        else if (plat == 3 && enc == 0) score = 1;    // Windows symbol
        if (score > bestScore) { bestScore = score; best = off; }
    }
    if (bestScore < 0) return 0;

    const unsigned char *sub = cm + best;
    int format = (int)rdU16(sub);
    if (format == 4) {
        int segX2 = (int)rdU16(sub + 6);
        const unsigned char *endCodes = sub + 14;
        const unsigned char *startCodes = endCodes + segX2 + 2;
        const unsigned char *idDeltas = startCodes + segX2;
        const unsigned char *idRangeOffs = idDeltas + segX2;
        if (cp > 0xFFFF) return 0;
        for (int s = 0; s < segX2 / 2; ++s) {
            unsigned end = rdU16(endCodes + s * 2);
            if (cp > end) continue;
            unsigned start = rdU16(startCodes + s * 2);
            if (cp < start) return 0;
            int delta = rdS16(idDeltas + s * 2);
            unsigned ro = rdU16(idRangeOffs + s * 2);
            if (ro == 0) return (int)((cp + delta) & 0xFFFF);
            const unsigned char *p = idRangeOffs + s * 2 + ro + (cp - start) * 2;
            if ((size_t)(p - d) + 1 >= m_data.size()) return 0;
            unsigned g = rdU16(p);
            return g ? (int)((g + delta) & 0xFFFF) : 0;
        }
        return 0;
    }
    if (format == 12) {
        unsigned groups = rdU32(sub + 12);
        for (unsigned g = 0; g < groups; ++g) {
            const unsigned char *rec = sub + 16 + 12 * g;
            unsigned s = rdU32(rec), e = rdU32(rec + 4), gi = rdU32(rec + 8);
            if (cp < s) return 0;
            if (cp <= e) return (int)(gi + (cp - s));
        }
        return 0;
    }
    if (format == 6) {
        unsigned first = rdU16(sub + 6), count = rdU16(sub + 8);
        if (cp < first || cp >= first + count) return 0;
        return (int)rdU16(sub + 10 + (cp - first) * 2);
    }
    if (format == 0) {
        if (cp > 255) return 0;
        return sub[6 + cp];
    }
    return 0;
}

int TtfFace::Advance(int gid, float scale) const {
    if (!m_ok || !m_hmtx || gid < 0) return 0;
    const unsigned char *d = &m_data[0];
    int idx = (gid < m_numHMetrics) ? gid : (m_numHMetrics - 1);
    if (idx < 0) return 0;
    unsigned adv = rdU16(d + m_hmtx + idx * 4);
    return (int)(adv * scale + 0.5f);
}

// ---------------------------------------------------------------- outlines
bool TtfFace::collectContours(int gid, float scale, float shear, float dx, float dy,
                              std::vector<std::vector<Point> > &contours, int depth) const {
    if (!m_ok || gid < 0 || gid >= m_numGlyphs || depth > 4) return false;
    const unsigned char *d = &m_data[0];

    unsigned start, end;
    if (m_indexToLocFormat == 0) {
        start = (unsigned)rdU16(d + m_loca + gid * 2) * 2;
        end   = (unsigned)rdU16(d + m_loca + gid * 2 + 2) * 2;
    } else {
        start = rdU32(d + m_loca + gid * 4);
        end   = rdU32(d + m_loca + gid * 4 + 4);
    }
    if (end <= start) return true;                   // empty glyph (e.g. space)

    const unsigned char *g = d + m_glyf + start;
    int numContours = rdS16(g);

    if (numContours >= 0) {
        const unsigned char *p = g + 10;
        std::vector<int> endPts((size_t)numContours);
        for (int i = 0; i < numContours; ++i) { endPts[(size_t)i] = (int)rdU16(p); p += 2; }
        int numPts = numContours ? endPts[(size_t)numContours - 1] + 1 : 0;
        unsigned instLen = rdU16(p); p += 2 + instLen;

        std::vector<unsigned char> flags((size_t)numPts);
        for (int i = 0; i < numPts; ) {
            unsigned char fl = *p++;
            flags[(size_t)i++] = fl;
            if (fl & REPEAT_FLAG) {
                int r = *p++;
                while (r-- > 0 && i < numPts) flags[(size_t)i++] = fl;
            }
        }
        std::vector<int> xs((size_t)numPts), ys((size_t)numPts);
        int v = 0;
        for (int i = 0; i < numPts; ++i) {
            unsigned char fl = flags[(size_t)i];
            if (fl & X_SHORT) { int dxv = *p++; v += (fl & X_SAME) ? dxv : -dxv; }
            else if (!(fl & X_SAME)) { v += rdS16(p); p += 2; }
            xs[(size_t)i] = v;
        }
        v = 0;
        for (int i = 0; i < numPts; ++i) {
            unsigned char fl = flags[(size_t)i];
            if (fl & Y_SHORT) { int dyv = *p++; v += (fl & Y_SAME) ? dyv : -dyv; }
            else if (!(fl & Y_SAME)) { v += rdS16(p); p += 2; }
            ys[(size_t)i] = v;
        }

        int first = 0;
        for (int c = 0; c < numContours; ++c) {
            int last = endPts[(size_t)c];
            std::vector<Point> pts;
            for (int i = first; i <= last && i < numPts; ++i) {
                Point pt;
                float fy = ys[(size_t)i] * scale + dy;
                pt.x = xs[(size_t)i] * scale + dx + shear * fy;
                pt.y = fy;
                pt.onCurve = (flags[(size_t)i] & ON_CURVE) != 0;
                pts.push_back(pt);
            }
            if (!pts.empty()) contours.push_back(pts);
            first = last + 1;
        }
        return true;
    }

    // Composite glyph.
    const unsigned char *p = g + 10;
    for (;;) {
        unsigned flags2 = rdU16(p); p += 2;
        int glyphIdx = (int)rdU16(p); p += 2;
        int a1, a2;
        if (flags2 & ARG_1_AND_2_ARE_WORDS) { a1 = rdS16(p); p += 2; a2 = rdS16(p); p += 2; }
        else { a1 = (signed char)p[0]; a2 = (signed char)p[1]; p += 2; }
        // Only offsets are honoured; component scaling is rare in UI fonts and
        // ignoring it is visibly better than mis-applying it.
        if (flags2 & WE_HAVE_A_SCALE) p += 2;
        else if (flags2 & X_AND_Y_SCALE) p += 4;
        else if (flags2 & TWO_BY_TWO) p += 8;

        float ox = 0, oy = 0;
        if (flags2 & ARGS_ARE_XY_VALUES) { ox = a1 * scale; oy = a2 * scale; }
        collectContours(glyphIdx, scale, shear, dx + ox, dy + oy, contours, depth + 1);

        if (!(flags2 & MORE_COMPONENTS)) break;
    }
    return true;
}

// -------------------------------------------------------------- rasteriser
bool TtfFace::Rasterise(int gid, float scale, float shear, int boldPx,
                        TtfGlyphBitmap &out) const {
    out = TtfGlyphBitmap();
    out.advance = Advance(gid, scale) + boldPx;
    if (!m_ok) return false;

    std::vector<std::vector<Point> > contours;
    if (!collectContours(gid, scale, shear, 0.0f, 0.0f, contours, 0)) return false;
    if (contours.empty()) return true;               // blank but valid (space)

    // Flatten quadratic curves, with the TrueType rule that two consecutive
    // off-curve points imply an on-curve point halfway between them.
    std::vector<std::vector<Point> > flat;
    for (size_t c = 0; c < contours.size(); ++c) {
        const std::vector<Point> &src = contours[c];
        if (src.size() < 2) continue;
        std::vector<Point> poly;

        // Find a starting on-curve point (synthesising one if the contour has none).
        size_t startIdx = src.size();
        for (size_t i = 0; i < src.size(); ++i) if (src[i].onCurve) { startIdx = i; break; }
        Point startPt;
        if (startIdx == src.size()) {
            startPt.x = (src[0].x + src[src.size() - 1].x) * 0.5f;
            startPt.y = (src[0].y + src[src.size() - 1].y) * 0.5f;
            startPt.onCurve = true;
            startIdx = 0;
        } else {
            startPt = src[startIdx];
        }
        poly.push_back(startPt);

        Point cur = startPt;
        bool haveCtrl = false;
        Point ctrl;
        const size_t n = src.size();
        for (size_t k = 1; k <= n; ++k) {
            const Point &pt = src[(startIdx + k) % n];
            if (pt.onCurve) {
                if (haveCtrl) {
                    const int STEPS = 8;
                    for (int s = 1; s <= STEPS; ++s) {
                        float t = (float)s / STEPS, u = 1.0f - t;
                        Point q;
                        q.x = u * u * cur.x + 2 * u * t * ctrl.x + t * t * pt.x;
                        q.y = u * u * cur.y + 2 * u * t * ctrl.y + t * t * pt.y;
                        q.onCurve = true;
                        poly.push_back(q);
                    }
                    haveCtrl = false;
                } else {
                    poly.push_back(pt);
                }
                cur = pt;
            } else {
                if (haveCtrl) {
                    Point mid;
                    mid.x = (ctrl.x + pt.x) * 0.5f;
                    mid.y = (ctrl.y + pt.y) * 0.5f;
                    mid.onCurve = true;
                    const int STEPS = 8;
                    for (int s = 1; s <= STEPS; ++s) {
                        float t = (float)s / STEPS, u = 1.0f - t;
                        Point q;
                        q.x = u * u * cur.x + 2 * u * t * ctrl.x + t * t * mid.x;
                        q.y = u * u * cur.y + 2 * u * t * ctrl.y + t * t * mid.y;
                        q.onCurve = true;
                        poly.push_back(q);
                    }
                    cur = mid;
                }
                ctrl = pt;
                haveCtrl = true;
            }
        }
        if (poly.size() >= 3) flat.push_back(poly);
    }
    if (flat.empty()) return true;

    // Bounds, in font-y-up pixel space.
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    for (size_t c = 0; c < flat.size(); ++c)
        for (size_t i = 0; i < flat[c].size(); ++i) {
            const Point &p = flat[c][i];
            if (p.x < minX) minX = p.x;
            if (p.x > maxX) maxX = p.x;
            if (p.y < minY) minY = p.y;
            if (p.y > maxY) maxY = p.y;
        }
    int x0 = (int)floorf(minX), x1 = (int)ceilf(maxX) + boldPx;
    int y0 = (int)floorf(minY), y1 = (int)ceilf(maxY);
    int w = x1 - x0, h = y1 - y0;
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return true;

    out.width = w;
    out.height = h;
    out.bearingX = x0;
    out.bearingY = y1;                 // top edge above the baseline
    out.coverage.assign((size_t)w * h, 0);

    // Edge list in raster space: x right, y DOWN from the glyph top.
    std::vector<Edge> edges;
    for (size_t c = 0; c < flat.size(); ++c) {
        const std::vector<Point> &poly = flat[c];
        for (size_t i = 0; i < poly.size(); ++i) {
            const Point &a = poly[i];
            const Point &b = poly[(i + 1) % poly.size()];
            float ax = a.x - x0, ay = (float)y1 - a.y;
            float bx = b.x - x0, by = (float)y1 - b.y;
            if (ay == by) continue;
            Edge e;
            e.dir = (ay < by) ? 1 : -1;
            if (ay < by) { e.x0 = ax; e.y0 = ay; e.x1 = bx; e.y1 = by; }
            else         { e.x0 = bx; e.y0 = by; e.x1 = ax; e.y1 = ay; }
            edges.push_back(e);
        }
    }
    if (edges.empty()) return true;

    const int SUB = 4;                       // vertical subsamples per pixel row
    const float subWeight = 1.0f / SUB;
    std::vector<float> row((size_t)w);
    std::vector<std::pair<float, int> > xs;

    for (int py = 0; py < h; ++py) {
        std::fill(row.begin(), row.end(), 0.0f);
        for (int s = 0; s < SUB; ++s) {
            float sy = py + (s + 0.5f) * subWeight;
            xs.clear();
            for (size_t i = 0; i < edges.size(); ++i) {
                const Edge &e = edges[i];
                if (sy < e.y0 || sy >= e.y1) continue;
                float t = (sy - e.y0) / (e.y1 - e.y0);
                xs.push_back(std::make_pair(e.x0 + t * (e.x1 - e.x0), e.dir));
            }
            if (xs.size() < 2) continue;
            std::sort(xs.begin(), xs.end());

            int winding = 0;
            for (size_t i = 0; i + 1 < xs.size(); ++i) {
                winding += xs[i].second;
                if (winding == 0) continue;              // nonzero fill rule
                float sx = xs[i].first, ex = xs[i + 1].first;
                if (ex <= 0.0f || sx >= (float)w) continue;
                if (sx < 0.0f) sx = 0.0f;
                if (ex > (float)w) ex = (float)w;
                int ix0 = (int)floorf(sx), ix1 = (int)ceilf(ex) - 1;
                for (int px = ix0; px <= ix1 && px < w; ++px) {
                    if (px < 0) continue;
                    float l = sx > (float)px ? sx : (float)px;
                    float r = ex < (float)(px + 1) ? ex : (float)(px + 1);
                    if (r > l) row[(size_t)px] += (r - l) * subWeight;
                }
            }
        }
        unsigned char *dst = &out.coverage[(size_t)py * w];
        for (int px = 0; px < w; ++px) {
            float v = row[(size_t)px];
            if (v <= 0.0f) continue;
            if (v > 1.0f) v = 1.0f;
            dst[px] = (unsigned char)(v * 255.0f + 0.5f);
        }
    }

    // Synthetic bold: smear horizontally by boldPx, which is what GDI does for a
    // face that has no real bold variant.
    for (int b = 0; b < boldPx; ++b) {
        for (int py = 0; py < h; ++py) {
            unsigned char *r = &out.coverage[(size_t)py * w];
            for (int px = w - 1; px > 0; --px)
                if (r[px - 1] > r[px]) r[px] = r[px - 1];
        }
    }
    return true;
}

// ------------------------------------------------------------ font picking
//  What Windows reports for the handful of faces this client names.
//
//  Measured with GetTextMetricsA on Windows 11 and cross-checked against each
//  file's OS/2 usWinAscent / usWinDescent, which is where GDI takes tmAscent and
//  tmDescent from for a TrueType face. Tahoma is the one that matters: it is
//  _DEFAULT_FONT, so every WINDOW_POS in uiextcfg.xml was measured against its
//  line box.
struct WinFaceMetrics {
    const char *name;
    int unitsPerEm;
    int winAscent;
    int winDescent;
};

static const WinFaceMetrics kWinFaces[] = {
    { "Tahoma",               2048, 2049, 423 },
    { "Verdana",              2048, 2059, 430 },
    { "Arial",                2048, 1854, 434 },
    { "Microsoft Sans Serif", 2048, 1888, 430 },
};

static const WinFaceMetrics *findWinFace(const char *faceName) {
    if (!faceName || !*faceName) return NULL;
    for (size_t i = 0; i < sizeof(kWinFaces) / sizeof(kWinFaces[0]); ++i)
        if (strcasecmp(faceName, kWinFaces[i].name) == 0) return &kWinFaces[i];
    return NULL;
}

//  GDI rounds each metric to nearest, independently: Tahoma at em 12 gives
//  12.006 -> 12 and 2.478 -> 2, for a tmHeight of 14. Rounding the sum instead,
//  or ceiling either half, moves the baseline by a pixel and the whole UI with
//  it.
static int roundMetric(int units, int emPx, int unitsPerEm) {
    return (int)((double)units * (double)emPx / (double)unitsPerEm + 0.5);
}

bool RanFont_WinMetrics(const char *faceName, int emPx, int *ascent, int *descent) {
    const WinFaceMetrics *m = findWinFace(faceName);
    if (!m || emPx <= 0) return false;
    if (ascent)  *ascent  = roundMetric(m->winAscent,  emPx, m->unitsPerEm);
    if (descent) *descent = roundMetric(m->winDescent, emPx, m->unitsPerEm);
    return true;
}

int RanFont_WinEmForCellHeight(const char *faceName, int cellPx) {
    const WinFaceMetrics *m = findWinFace(faceName);
    if (!m || cellPx <= 0) return 0;
    const int span = m->winAscent + m->winDescent;
    if (span <= 0) return 0;
    return (int)((double)cellPx * (double)m->unitsPerEm / (double)span + 0.5);
}

std::string RanFont_Resolve(const char *faceName, int codePage, bool bold) {
    static std::vector<std::string> files;
    if (files.empty()) {
        DIR *d = opendir("/system/fonts");
        if (d) {
            struct dirent *e;
            while ((e = readdir(d)) != NULL) {
                std::string n = e->d_name;
                if (n.size() > 4 && (n.rfind(".ttf") == n.size() - 4 ||
                                     n.rfind(".otf") == n.size() - 4))
                    files.push_back(n);
            }
            closedir(d);
        }
        LOGI("/system/fonts: %d files", (int)files.size());
    }

    // The face name the client asks for ("Tahoma", a Thai face, ...) does not
    // exist on Android, so the language is what actually decides the file.
    const char *wantScript = NULL;
    switch (codePage) {
        case 874:  wantScript = "Thai";     break;
        case 949:  wantScript = "CJK";      break;
        case 932:  wantScript = "CJK";      break;
        case 936:  wantScript = "CJK";      break;
        case 950:  wantScript = "CJK";      break;
        default:   wantScript = NULL;       break;
    }

    std::string best;
    for (size_t pass = 0; pass < 2 && best.empty(); ++pass) {
        for (size_t i = 0; i < files.size(); ++i) {
            const std::string &n = files[i];
            if (wantScript && n.find(wantScript) == std::string::npos) continue;
            if (!wantScript && n.find("Roboto-") != 0 && n.find("DroidSans.") != 0) continue;
            if (n.find("UI-") != std::string::npos) continue;      // prefer the text cut
            if (n.find("Serif") != std::string::npos) continue;
            bool isBold = n.find("Bold") != std::string::npos;
            if (pass == 0 && isBold != bold) continue;
            best = std::string("/system/fonts/") + n;
            break;
        }
    }
    if (best.empty()) best = "/system/fonts/Roboto-Regular.ttf";
    (void)faceName;
    return best;
}
