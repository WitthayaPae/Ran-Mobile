// DDS / TGA / BMP / PNG decoding for the D3DX texture entry points.
//
// Design notes:
//
//  * DXT data is kept COMPRESSED. The engine inspects the created texture's
//    format (TextureManager.cpp: D3DFMT_DXT1 -> hard alpha, DXT3/5 -> soft) to
//    decide how a texture is drawn, so expanding to ARGB here would quietly
//    change how the game looks. The GL layer decides whether the GPU takes the
//    blocks directly or they get decoded there.
//  * Mip chains in the file are preserved; the engine asks for D3DX_DEFAULT
//    levels and the loaders pass on whatever the file actually has.
//  * PNG is decoded with the zlib the client already links (Lib_ZLib), so no
//    new dependency enters the build.

#include "image_decode.h"

#include <string.h>
#include <stdlib.h>
#include <zlib.h>

namespace {

inline UINT rd32(const BYTE *p) {
    return (UINT)p[0] | ((UINT)p[1] << 8) | ((UINT)p[2] << 16) | ((UINT)p[3] << 24);
}
inline UINT rd32be(const BYTE *p) {
    return ((UINT)p[0] << 24) | ((UINT)p[1] << 16) | ((UINT)p[2] << 8) | (UINT)p[3];
}
inline UINT rd16(const BYTE *p) { return (UINT)p[0] | ((UINT)p[1] << 8); }

int bppOf(D3DFORMAT f) {
    switch (f) {
        case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: case D3DFMT_A8B8G8R8: return 4;
        case D3DFMT_R8G8B8:                                               return 3;
        case D3DFMT_R5G6B5: case D3DFMT_A1R5G5B5:
        case D3DFMT_X1R5G5B5: case D3DFMT_A4R4G4B4:
        case D3DFMT_A8L8:                                                 return 2;
        case D3DFMT_A8: case D3DFMT_L8:                                   return 1;
        default:                                                          return 0;
    }
}
bool compressedFmt(D3DFORMAT f) {
    return f == D3DFMT_DXT1 || f == D3DFMT_DXT2 || f == D3DFMT_DXT3
        || f == D3DFMT_DXT4 || f == D3DFMT_DXT5;
}
//  A dimension no real texture has, and no GLES driver here would accept.
//
//  These decoders parse files a player can replace: the client reads its
//  textures out of the data directory, which on Android is world-writable
//  storage. A header claiming 2^30 pixels is not content - and unchecked it
//  reaches arithmetic written for real numbers. 16384 is the largest
//  GL_MAX_TEXTURE_SIZE any device in this port reports.
const UINT kMaxDim = 16384;
bool saneDims(UINT w, UINT h) { return w && h && w <= kMaxDim && h <= kMaxDim; }

size_t levelBytes(UINT w, UINT h, D3DFORMAT f) {
    if (compressedFmt(f)) {
        UINT bw = (w + 3) / 4, bh = (h + 3) / 4;
        return (size_t)bw * bh * ((f == D3DFMT_DXT1) ? 8 : 16);
    }
    return (size_t)w * h * bppOf(f);
}

// ------------------------------------------------------------------- DDS
enum { DDPF_ALPHAPIXELS = 0x1, DDPF_FOURCC = 0x4, DDPF_RGB = 0x40, DDPF_LUMINANCE = 0x20000 };

D3DFORMAT ddsFormat(const BYTE *pf) {
    UINT flags  = rd32(pf + 4);
    UINT fourcc = rd32(pf + 8);
    if (flags & DDPF_FOURCC) {
        switch (fourcc) {
            case 0x31545844u: return D3DFMT_DXT1;   // 'DXT1'
            case 0x32545844u: return D3DFMT_DXT2;
            case 0x33545844u: return D3DFMT_DXT3;
            case 0x34545844u: return D3DFMT_DXT4;
            case 0x35545844u: return D3DFMT_DXT5;
            default:          return D3DFMT_UNKNOWN;
        }
    }
    UINT bits = rd32(pf + 12);
    UINT rm = rd32(pf + 16), gm = rd32(pf + 20), am = rd32(pf + 28);
    if (flags & DDPF_RGB) {
        if (bits == 32) {
            if (am == 0xFF000000u && rm == 0x00FF0000u) return D3DFMT_A8R8G8B8;
            if (rm == 0x00FF0000u)                      return D3DFMT_X8R8G8B8;
            if (am == 0xFF000000u && rm == 0x000000FFu) return D3DFMT_A8B8G8R8;
            return D3DFMT_A8R8G8B8;
        }
        if (bits == 24) return D3DFMT_R8G8B8;
        if (bits == 16) {
            if (am == 0x8000u) return D3DFMT_A1R5G5B5;
            if (am == 0xF000u) return D3DFMT_A4R4G4B4;
            if (gm == 0x07E0u) return D3DFMT_R5G6B5;
            return D3DFMT_A1R5G5B5;
        }
    }
    if (flags & DDPF_LUMINANCE) return (bits == 16) ? D3DFMT_A8L8 : D3DFMT_L8;
    if (bits == 8) return D3DFMT_A8;
    return D3DFMT_UNKNOWN;
}

bool decodeDDS(const BYTE *p, size_t size, RanImage &out) {
    if (size < 128 || memcmp(p, "DDS ", 4) != 0) return false;
    const BYTE *h = p + 4;                       // DDS_HEADER
    if (rd32(h + 0) != 124) return false;
    UINT height = rd32(h + 8), width = rd32(h + 12);
    UINT mips = rd32(h + 24);
    D3DFORMAT fmt = ddsFormat(h + 72);
    if (fmt == D3DFMT_UNKNOWN || !saneDims(width, height)) return false;
    if (mips == 0) mips = 1;

    const BYTE *data = p + 128;
    size_t left = size - 128;

    out.width = width; out.height = height; out.format = fmt;
    out.fileFormat = 4;                          // D3DXIFF_DDS
    out.levels.clear();

    UINT w = width, hh = height;
    for (UINT i = 0; i < mips; ++i) {
        size_t n = levelBytes(w ? w : 1, hh ? hh : 1, fmt);
        if (n == 0 || n > left) break;           // truncated file: keep what is there
        out.levels.push_back(std::vector<BYTE>(data, data + n));
        data += n; left -= n;
        w  = w  > 1 ? w  / 2 : 1;
        hh = hh > 1 ? hh / 2 : 1;
    }
    if (out.levels.empty()) return false;
    out.mipLevels = (UINT)out.levels.size();
    return true;
}

//  A cube DDS is the same header with DDSCAPS2_CUBEMAP set, followed by the six
//  faces one after another - each a complete mip chain, in the order D3D names
//  them. Nothing else about the format changes.
bool decodeDDSCube(const BYTE *p, size_t size, RanImage outFaces[6]) {
    if (size < 128 || memcmp(p, "DDS ", 4) != 0) return false;
    const BYTE *h = p + 4;
    if (rd32(h + 0) != 124) return false;

    const UINT caps2 = rd32(h + 108);
    const UINT DDSCAPS2_CUBEMAP = 0x00000200u;
    const UINT ALL_FACES        = 0x0000FC00u;   // the six face bits
    if (!(caps2 & DDSCAPS2_CUBEMAP)) return false;
    if ((caps2 & ALL_FACES) != ALL_FACES) return false;   // partial cubes are not used

    UINT height = rd32(h + 8), width = rd32(h + 12);
    UINT mips = rd32(h + 24);
    D3DFORMAT fmt = ddsFormat(h + 72);
    if (fmt == D3DFMT_UNKNOWN || !saneDims(width, height)) return false;
    if (mips == 0) mips = 1;

    const BYTE *data = p + 128;
    size_t left = size - 128;

    for (int face = 0; face < 6; ++face) {
        RanImage &out = outFaces[face];
        out.width = width; out.height = height; out.format = fmt;
        out.fileFormat = 4;                      // D3DXIFF_DDS
        out.levels.clear();

        UINT w = width, hh = height;
        for (UINT i = 0; i < mips; ++i) {
            size_t n = levelBytes(w ? w : 1, hh ? hh : 1, fmt);
            if (n == 0 || n > left) break;
            out.levels.push_back(std::vector<BYTE>(data, data + n));
            data += n; left -= n;
            w  = w  > 1 ? w  / 2 : 1;
            hh = hh > 1 ? hh / 2 : 1;
        }
        if (out.levels.empty()) return false;
        out.mipLevels = (UINT)out.levels.size();
    }
    return true;
}

// ------------------------------------------------------------------- TGA
// Written as A8R8G8B8, i.e. B,G,R,A in memory, matching a D3D locked surface.
bool decodeTGA(const BYTE *p, size_t size, RanImage &out) {
    if (size < 18) return false;
    BYTE idLen = p[0];
    BYTE cmapType = p[1];
    BYTE imgType = p[2];
    UINT width = rd16(p + 12), height = rd16(p + 14);
    BYTE depth = p[16];
    BYTE desc = p[17];
    if (cmapType != 0) return false;
    if (imgType != 2 && imgType != 3 && imgType != 10 && imgType != 11) return false;
    if (!saneDims(width, height)) return false;
    if (depth != 8 && depth != 24 && depth != 32) return false;

    const BYTE *src = p + 18 + idLen;
    const BYTE *end = p + size;
    const UINT srcBpp = depth / 8;
    const bool rle = (imgType == 10 || imgType == 11);

    std::vector<BYTE> pix((size_t)width * height * 4);
    size_t written = 0;
    const size_t total = (size_t)width * height;

    while (written < total) {
        if (rle) {
            if (src >= end) break;
            BYTE hdr = *src++;
            UINT count = (UINT)(hdr & 0x7F) + 1;
            if (hdr & 0x80) {                                   // run
                if (src + srcBpp > end) break;
                BYTE b = src[0];
                BYTE g = srcBpp > 1 ? src[1] : src[0];
                BYTE r = srcBpp > 2 ? src[2] : src[0];
                BYTE a = (srcBpp == 4) ? src[3] : 255;
                src += srcBpp;
                for (UINT i = 0; i < count && written < total; ++i, ++written) {
                    BYTE *d = &pix[written * 4];
                    d[0] = b; d[1] = g; d[2] = r; d[3] = a;
                }
            } else {                                            // literal run
                for (UINT i = 0; i < count && written < total; ++i, ++written) {
                    if (src + srcBpp > end) break;
                    BYTE *d = &pix[written * 4];
                    d[0] = src[0];
                    d[1] = srcBpp > 1 ? src[1] : src[0];
                    d[2] = srcBpp > 2 ? src[2] : src[0];
                    d[3] = (srcBpp == 4) ? src[3] : 255;
                    src += srcBpp;
                }
            }
        } else {
            if (src + srcBpp > end) break;
            BYTE *d = &pix[written * 4];
            d[0] = src[0];
            d[1] = srcBpp > 1 ? src[1] : src[0];
            d[2] = srcBpp > 2 ? src[2] : src[0];
            d[3] = (srcBpp == 4) ? src[3] : 255;
            src += srcBpp;
            ++written;
        }
    }
    if (written == 0) return false;

    // Descriptor bit 5 means the first row is the TOP one. TGA's default is
    // bottom-up, which has to be flipped to match a D3D surface.
    if ((desc & 0x20) == 0) {
        std::vector<BYTE> row((size_t)width * 4);
        for (UINT y = 0; y < height / 2; ++y) {
            BYTE *a = &pix[(size_t)y * width * 4];
            BYTE *b = &pix[(size_t)(height - 1 - y) * width * 4];
            memcpy(&row[0], a, row.size());
            memcpy(a, b, row.size());
            memcpy(b, &row[0], row.size());
        }
    }

    out.width = width; out.height = height; out.format = D3DFMT_A8R8G8B8;
    out.mipLevels = 1; out.fileFormat = 2;       // D3DXIFF_TGA
    out.levels.clear();
    out.levels.push_back(pix);
    return true;
}

// ------------------------------------------------------------------- BMP
bool decodeBMP(const BYTE *p, size_t size, RanImage &out) {
    if (size < 54 || p[0] != 'B' || p[1] != 'M') return false;
    UINT dataOff = rd32(p + 10);
    if (rd32(p + 14) < 40) return false;
    int  swidth  = (int)rd32(p + 18);
    int  sheight = (int)rd32(p + 22);
    UINT bits = rd16(p + 28);
    if (rd32(p + 30) != 0) return false;         // compressed BMP: not used here
    if (bits != 24 && bits != 32) return false;
    if (swidth <= 0 || sheight == 0) return false;

    const bool topDown = sheight < 0;
    UINT h = (UINT)(topDown ? -sheight : sheight);
    UINT w = (UINT)swidth;
    if (!saneDims(w, h)) return false;
    UINT srcBpp = bits / 8;
    //  size_t, not UINT. w is a 32-bit field straight out of the file, so
    //  w * srcBpp in UINT arithmetic wraps: a width of 2^30 at 4 bytes gives a
    //  stride of 0, the "does the pixel data fit" test below then compares
    //  against nothing, and the row loop reads wherever it likes. The
    //  dimension guard above already makes it impossible; this makes it
    //  impossible twice, because the guard is one line someone could relax.
    size_t stride = (((size_t)w * srcBpp + 3) / 4) * 4;
    if ((size_t)dataOff + stride * (size_t)h > size) return false;

    std::vector<BYTE> pix((size_t)w * h * 4);
    for (UINT y = 0; y < h; ++y) {
        const BYTE *srow = p + dataOff + (size_t)(topDown ? y : (h - 1 - y)) * stride;
        BYTE *drow = &pix[(size_t)y * w * 4];
        for (UINT x = 0; x < w; ++x) {
            drow[x * 4 + 0] = srow[x * srcBpp + 0];
            drow[x * 4 + 1] = srow[x * srcBpp + 1];
            drow[x * 4 + 2] = srow[x * srcBpp + 2];
            drow[x * 4 + 3] = (srcBpp == 4) ? srow[x * srcBpp + 3] : 255;
        }
    }
    out.width = w; out.height = h; out.format = D3DFMT_A8R8G8B8;
    out.mipLevels = 1; out.fileFormat = 1;       // D3DXIFF_BMP
    out.levels.clear();
    out.levels.push_back(pix);
    return true;
}

// ------------------------------------------------------------------- PNG
BYTE paethPredict(int a, int b, int c) {
    int pp = a + b - c;
    int pa = abs(pp - a), pb = abs(pp - b), pc = abs(pp - c);
    if (pa <= pb && pa <= pc) return (BYTE)a;
    if (pb <= pc) return (BYTE)b;
    return (BYTE)c;
}

bool decodePNG(const BYTE *p, size_t size, RanImage &out) {
    static const BYTE sig[8] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };
    if (size < 57 || memcmp(p, sig, 8) != 0) return false;

    UINT width = 0, height = 0;
    BYTE depth = 0, colorType = 0, interlace = 0;
    std::vector<BYTE> idat, palette, trns;

    size_t off = 8;
    while (off + 8 <= size) {
        UINT len = rd32be(p + off);
        const BYTE *type = p + off + 4;
        const BYTE *body = p + off + 8;
        if (off + 12 + (size_t)len > size) break;
        if (!memcmp(type, "IHDR", 4) && len >= 13) {
            width = rd32be(body); height = rd32be(body + 4);
            depth = body[8]; colorType = body[9]; interlace = body[12];
        } else if (!memcmp(type, "PLTE", 4)) {
            palette.assign(body, body + len);
        } else if (!memcmp(type, "tRNS", 4)) {
            trns.assign(body, body + len);
        } else if (!memcmp(type, "IDAT", 4)) {
            idat.insert(idat.end(), body, body + len);
        } else if (!memcmp(type, "IEND", 4)) {
            break;
        }
        off += 12 + (size_t)len;
    }
    // Interlaced and 16-bit-per-channel PNGs do not appear in the client data;
    // refusing them is better than producing a scrambled image.
    if (!saneDims(width, height) || depth != 8 || interlace != 0 || idat.empty()) return false;

    int channels;
    switch (colorType) {
        case 0: channels = 1; break;             // grey
        case 2: channels = 3; break;             // RGB
        case 3: channels = 1; break;             // palette index
        case 4: channels = 2; break;             // grey + alpha
        case 6: channels = 4; break;             // RGBA
        default: return false;
    }
    if (colorType == 3 && palette.empty()) return false;

    const size_t stride = (size_t)width * channels;
    std::vector<BYTE> raw((stride + 1) * height);
    uLongf destLen = (uLongf)raw.size();
    if (uncompress(&raw[0], &destLen, &idat[0], (uLong)idat.size()) != Z_OK) return false;

    std::vector<BYTE> lines(stride * height);
    for (UINT y = 0; y < height; ++y) {
        BYTE filter = raw[(size_t)y * (stride + 1)];
        const BYTE *src = &raw[(size_t)y * (stride + 1) + 1];
        BYTE *dst = &lines[(size_t)y * stride];
        const BYTE *prev = y ? &lines[(size_t)(y - 1) * stride] : NULL;
        for (size_t x = 0; x < stride; ++x) {
            int a = (x >= (size_t)channels) ? dst[x - channels] : 0;
            int b = prev ? prev[x] : 0;
            int c = (prev && x >= (size_t)channels) ? prev[x - channels] : 0;
            int v = src[x];
            switch (filter) {
                case 0: break;
                case 1: v += a; break;
                case 2: v += b; break;
                case 3: v += (a + b) / 2; break;
                case 4: v += paethPredict(a, b, c); break;
                default: return false;
            }
            dst[x] = (BYTE)v;
        }
    }

    std::vector<BYTE> pix((size_t)width * height * 4);
    for (UINT y = 0; y < height; ++y) {
        for (UINT x = 0; x < width; ++x) {
            const BYTE *s = &lines[(size_t)y * stride + (size_t)x * channels];
            BYTE *d = &pix[((size_t)y * width + x) * 4];
            BYTE r = 0, g = 0, b = 0, a = 255;
            switch (colorType) {
                case 0: r = g = b = s[0]; break;
                case 4: r = g = b = s[0]; a = s[1]; break;
                case 2: r = s[0]; g = s[1]; b = s[2]; break;
                case 6: r = s[0]; g = s[1]; b = s[2]; a = s[3]; break;
                case 3: {
                    UINT idx = s[0];
                    if ((size_t)idx * 3 + 2 < palette.size()) {
                        r = palette[idx * 3 + 0];
                        g = palette[idx * 3 + 1];
                        b = palette[idx * 3 + 2];
                    }
                    if ((size_t)idx < trns.size()) a = trns[idx];
                    break;
                }
                default: return false;
            }
            d[0] = b; d[1] = g; d[2] = r; d[3] = a;   // D3D byte order
        }
    }

    out.width = width; out.height = height; out.format = D3DFMT_A8R8G8B8;
    out.mipLevels = 1; out.fileFormat = 3;       // D3DXIFF_PNG
    out.levels.clear();
    out.levels.push_back(pix);
    return true;
}

} // namespace

bool RanImage_DecodeCube(const void *data, size_t size, RanImage outFaces[6]) {
    if (!data || size < 128) return false;
    return decodeDDSCube((const BYTE *)data, size, outFaces);
}

bool RanImage_Decode(const void *data, size_t size, RanImage &out) {
    if (!data || size < 16) return false;
    const BYTE *p = (const BYTE *)data;
    if (decodeDDS(p, size, out)) return true;
    if (decodePNG(p, size, out)) return true;
    if (decodeBMP(p, size, out)) return true;
    // TGA last: it has no magic number, so it must not shadow the others.
    if (decodeTGA(p, size, out)) return true;
    // Anything else the platform knows — JPEG map textures come through here.
    if (RanImage_DecodePlatform(p, size, out)) return true;
    return false;
}
