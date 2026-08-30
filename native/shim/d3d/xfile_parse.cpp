// DirectX .x container reader. See xfile_parse.h for what this produces.
//
// Header is 16 bytes: "xof " + major/minor ("0303") + format ("bin ", "txt ",
// "bzip") + float size ("0032" or "0064").
//
// The binary body is a stream of 16-bit tokens; the text body is the same
// grammar in ASCII. Template definitions are parsed only far enough to skip
// them: the templates that matter are the standard D3DRM set, and their layout
// is fixed, so member data is packed in stream order and consumers cast it.

#include "xfile_parse.h"

#include <zlib.h>
#include <android/log.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <set>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "RanXFile", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "RanXFile", __VA_ARGS__)

namespace {

void warnOnce(const char *what) {
    static std::set<std::string> seen;
    if (seen.insert(std::string(what)).second) LOGE("%s", what);
}

// ------------------------------------------------------------------- MSZip
//
// Not a zlib container: a u32 total size, then blocks of
//   u16 uncompressed size, u16 compressed size, 'CK', raw deflate data
// where each block after the first back-references up to 32K of the PREVIOUS
// block's output. Rather than the dictionary API, the history is prepended as
// an uncompressed *stored* deflate block and dropped from the output — the
// back-references then resolve against real stream data.
bool inflateWithHistory(const BYTE *chunk, size_t chunkSize,
                        const std::vector<BYTE> &history, std::vector<BYTE> &out) {
    std::vector<BYTE> stream;
    if (!history.empty()) {
        size_t len = history.size();          // <= 32768, one stored block
        stream.push_back(0x00);               // BFINAL=0, BTYPE=00 (stored)
        stream.push_back((BYTE)(len & 0xFF));
        stream.push_back((BYTE)((len >> 8) & 0xFF));
        stream.push_back((BYTE)(~len & 0xFF));
        stream.push_back((BYTE)((~len >> 8) & 0xFF));
        stream.insert(stream.end(), history.begin(), history.end());
    }
    stream.insert(stream.end(), chunk, chunk + chunkSize);

    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) return false;
    zs.next_in = &stream[0];
    zs.avail_in = (uInt)stream.size();

    std::vector<BYTE> buf(history.size() + 65536);
    size_t produced = 0;
    for (;;) {
        if (produced == buf.size()) buf.resize(buf.size() * 2);
        zs.next_out = &buf[produced];
        zs.avail_out = (uInt)(buf.size() - produced);
        int r = inflate(&zs, Z_NO_FLUSH);
        produced = buf.size() - zs.avail_out;
        if (r == Z_STREAM_END) break;
        if (r != Z_OK) { inflateEnd(&zs); return false; }
        if (zs.avail_in == 0 && zs.avail_out != 0) break;
    }
    inflateEnd(&zs);
    if (produced < history.size()) return false;
    out.assign(buf.begin() + history.size(), buf.begin() + produced);
    return true;
}

bool msZipDecompress(const BYTE *p, size_t size, std::vector<BYTE> &out) {
    if (size < 4) return false;
    size_t pos = 4;                                   // u32 total size
    std::vector<BYTE> history;
    while (pos + 6 <= size) {
        unsigned uncomp = (unsigned)p[pos] | ((unsigned)p[pos + 1] << 8);
        unsigned comp   = (unsigned)p[pos + 2] | ((unsigned)p[pos + 3] << 8);
        if (p[pos + 4] != 'C' || p[pos + 5] != 'K') { warnOnce("MSZip: block is not CK"); return false; }
        pos += 6;
        if (comp < 2 || pos + (comp - 2) > size) { warnOnce("MSZip: block runs past end"); return false; }

        std::vector<BYTE> block;
        if (!inflateWithHistory(p + pos, comp - 2, history, block)) {
            warnOnce("MSZip: inflate failed");
            return false;
        }
        pos += comp - 2;

        out.insert(out.end(), block.begin(), block.end());
        history = block;
        if (history.size() > 32768) history.erase(history.begin(), history.end() - 32768);
        if (block.size() < uncomp) break;              // last, short block
    }
    return !out.empty();
}

// ------------------------------------------------- standard template GUIDs
struct TemplateGuid { const char *name; GUID guid; };

#define XG(n, a, b, c, d0, d1, d2, d3, d4, d5, d6, d7) \
    { n, { a, b, c, { d0, d1, d2, d3, d4, d5, d6, d7 } } }

const TemplateGuid kTemplates[] = {
    XG("Header",                0x3d82ab43, 0x62da, 0x11cf, 0xab,0x39,0x00,0x20,0xaf,0x71,0xe4,0x33),
    XG("Vector",                0x3d82ab5e, 0x62da, 0x11cf, 0xab,0x39,0x00,0x20,0xaf,0x71,0xe4,0x33),
    XG("Coords2d",              0xf6f23f44, 0x7686, 0x11cf, 0x8f,0x52,0x00,0x40,0x33,0x35,0x94,0xa3),
    XG("Matrix4x4",             0xf6f23f45, 0x7686, 0x11cf, 0x8f,0x52,0x00,0x40,0x33,0x35,0x94,0xa3),
    XG("ColorRGBA",             0x35ff44e0, 0x6c7c, 0x11cf, 0x8f,0x52,0x00,0x40,0x33,0x35,0x94,0xa3),
    XG("ColorRGB",              0xd3e16e81, 0x7835, 0x11cf, 0x8f,0x52,0x00,0x40,0x33,0x35,0x94,0xa3),
    XG("Material",              0x3d82ab4d, 0x62da, 0x11cf, 0xab,0x39,0x00,0x20,0xaf,0x71,0xe4,0x33),
    XG("TextureFilename",       0xa42790e1, 0x7810, 0x11cf, 0x8f,0x52,0x00,0x40,0x33,0x35,0x94,0xa3),
    XG("MeshFace",              0x3d82ab5f, 0x62da, 0x11cf, 0xab,0x39,0x00,0x20,0xaf,0x71,0xe4,0x33),
    XG("Mesh",                  0x3d82ab44, 0x62da, 0x11cf, 0xab,0x39,0x00,0x20,0xaf,0x71,0xe4,0x33),
    XG("MeshNormals",           0xf6f23f43, 0x7686, 0x11cf, 0x8f,0x52,0x00,0x40,0x33,0x35,0x94,0xa3),
    XG("MeshTextureCoords",     0xf6f23f40, 0x7686, 0x11cf, 0x8f,0x52,0x00,0x40,0x33,0x35,0x94,0xa3),
    XG("MeshMaterialList",      0xf6f23f42, 0x7686, 0x11cf, 0x8f,0x52,0x00,0x40,0x33,0x35,0x94,0xa3),
    XG("MeshVertexColors",      0x1630b821, 0x7842, 0x11cf, 0x8f,0x52,0x00,0x40,0x33,0x35,0x94,0xa3),
    XG("Frame",                 0x3d82ab46, 0x62da, 0x11cf, 0xab,0x39,0x00,0x20,0xaf,0x71,0xe4,0x33),
    XG("FrameTransformMatrix",  0xf6f23f41, 0x7686, 0x11cf, 0x8f,0x52,0x00,0x40,0x33,0x35,0x94,0xa3),
    XG("Animation",             0x3d82ab4f, 0x62da, 0x11cf, 0xab,0x39,0x00,0x20,0xaf,0x71,0xe4,0x33),
    XG("AnimationSet",          0x3d82ab50, 0x62da, 0x11cf, 0xab,0x39,0x00,0x20,0xaf,0x71,0xe4,0x33),
    XG("AnimationKey",          0x10dd46a8, 0x775b, 0x11cf, 0x8f,0x52,0x00,0x40,0x33,0x35,0x94,0xa3),
    XG("AnimationOptions",      0xe2bf56c0, 0x840f, 0x11cf, 0x8f,0x52,0x00,0x40,0x33,0x35,0x94,0xa3),
    XG("XSkinMeshHeader",       0x3cf169ce, 0xff7c, 0x44ab, 0x93,0xc0,0xf7,0x8f,0x62,0xd1,0x72,0xe2),
    XG("SkinWeights",           0x6f0d123b, 0xbad2, 0x4167, 0xa0,0xd0,0x80,0x22,0x4f,0x25,0xfa,0xbb),
    XG("VertexDuplicationIndices", 0xb8d65549, 0xd7c9, 0x4995, 0x89,0xcf,0x53,0xa9,0xa8,0xb0,0x31,0xe3),
};
#undef XG

const size_t kTemplateCount = sizeof(kTemplates) / sizeof(kTemplates[0]);

// ------------------------------------------------------------ binary tokens
enum {
    TOKEN_NAME = 1, TOKEN_STRING = 2, TOKEN_INTEGER = 3, TOKEN_GUID = 5,
    TOKEN_INTEGER_LIST = 6, TOKEN_FLOAT_LIST = 7,
    TOKEN_OBRACE = 10, TOKEN_CBRACE = 11, TOKEN_OPAREN = 12, TOKEN_CPAREN = 13,
    TOKEN_OBRACKET = 14, TOKEN_CBRACKET = 15, TOKEN_OANGLE = 16, TOKEN_CANGLE = 17,
    TOKEN_DOT = 18, TOKEN_COMMA = 19, TOKEN_SEMICOLON = 20,
    TOKEN_TEMPLATE = 31,
    TOKEN_WORD = 40, TOKEN_DWORD = 41, TOKEN_FLOAT = 42, TOKEN_DOUBLE = 43,
    TOKEN_CHAR = 44, TOKEN_UCHAR = 45, TOKEN_SWORD = 46, TOKEN_SDWORD = 47,
    TOKEN_VOID = 48, TOKEN_LPSTR = 49, TOKEN_UNICODE = 50, TOKEN_CSTRING = 51,
    TOKEN_ARRAY = 52
};

class BinaryReader {
public:
    BinaryReader(const BYTE *p, size_t n, bool doubleFloats)
        : m_p(p), m_n(n), m_at(0), m_double(doubleFloats) {}

    bool eof() const { return m_at >= m_n; }
    size_t tell() const { return m_at; }

    bool u16(unsigned &v) {
        if (m_at + 2 > m_n) return false;
        v = (unsigned)m_p[m_at] | ((unsigned)m_p[m_at + 1] << 8);
        m_at += 2;
        return true;
    }
    bool u32(unsigned &v) {
        if (m_at + 4 > m_n) return false;
        v = (unsigned)m_p[m_at] | ((unsigned)m_p[m_at + 1] << 8) |
            ((unsigned)m_p[m_at + 2] << 16) | ((unsigned)m_p[m_at + 3] << 24);
        m_at += 4;
        return true;
    }
    bool f32(float &v) {
        if (m_double) {
            if (m_at + 8 > m_n) return false;
            double d;
            memcpy(&d, m_p + m_at, 8);
            m_at += 8;
            v = (float)d;
            return true;
        }
        if (m_at + 4 > m_n) return false;
        memcpy(&v, m_p + m_at, 4);
        m_at += 4;
        return true;
    }
    bool bytes(void *dst, size_t n) {
        if (m_at + n > m_n) return false;
        memcpy(dst, m_p + m_at, n);
        m_at += n;
        return true;
    }
    bool skip(size_t n) {
        if (m_at + n > m_n) return false;
        m_at += n;
        return true;
    }
    // A counted string: u32 length then the characters, no terminator.
    bool countedString(std::string &s) {
        unsigned len;
        if (!u32(len)) return false;
        if (m_at + len > m_n) return false;
        s.assign((const char *)(m_p + m_at), len);
        m_at += len;
        return true;
    }

private:
    const BYTE *m_p;
    size_t m_n, m_at;
    bool m_double;
};

void appendU32(std::vector<BYTE> &v, unsigned x) {
    v.push_back((BYTE)(x & 0xFF));
    v.push_back((BYTE)((x >> 8) & 0xFF));
    v.push_back((BYTE)((x >> 16) & 0xFF));
    v.push_back((BYTE)((x >> 24) & 0xFF));
}
void appendFloat(std::vector<BYTE> &v, float f) {
    BYTE b[4];
    memcpy(b, &f, 4);
    v.insert(v.end(), b, b + 4);
}
void appendPtr(std::vector<BYTE> &v, const void *p) {
    BYTE b[sizeof(void *)];
    memcpy(b, &p, sizeof(p));
    v.insert(v.end(), b, b + sizeof(b));
}

struct BinaryParser {
    BinaryReader r;
    XFile *file;
    bool ok;

    BinaryParser(const BYTE *p, size_t n, bool dbl, XFile *f)
        : r(p, n, dbl), file(f), ok(true) {}

    // Template definitions carry no data we need; skip to the matching brace.
    bool skipTemplate() {
        int depth = 0;
        for (;;) {
            unsigned tok;
            if (!r.u16(tok)) return false;
            switch (tok) {
                case TOKEN_OBRACE: ++depth; break;
                case TOKEN_CBRACE: if (--depth <= 0) return true; break;
                case TOKEN_NAME: { std::string s; if (!r.countedString(s)) return false; } break;
                case TOKEN_STRING: {
                    std::string s;
                    unsigned term;
                    if (!r.countedString(s) || !r.u16(term)) return false;
                } break;
                case TOKEN_INTEGER: { unsigned v; if (!r.u32(v)) return false; } break;
                case TOKEN_GUID: if (!r.skip(16)) return false; break;
                case TOKEN_INTEGER_LIST: {
                    unsigned n;
                    if (!r.u32(n) || !r.skip((size_t)n * 4)) return false;
                } break;
                case TOKEN_FLOAT_LIST: {
                    unsigned n;
                    if (!r.u32(n)) return false;
                    for (unsigned i = 0; i < n; ++i) { float f; if (!r.f32(f)) return false; }
                } break;
                default: break;                     // punctuation and type tokens
            }
        }
    }

    // One data object: NAME [name] OBRACE ...members and children... CBRACE
    XNode *parseObject(const std::string &typeName) {
        XNode *node = new XNode();
        node->typeName = typeName;
        XFile_GuidForTemplate(typeName.c_str(), &node->type);

        unsigned tok;
        if (!r.u16(tok)) { delete node; return NULL; }
        if (tok == TOKEN_NAME) {
            if (!r.countedString(node->name)) { delete node; return NULL; }
            if (!r.u16(tok)) { delete node; return NULL; }
        }
        if (tok == TOKEN_GUID) {                    // optional explicit id
            if (!r.skip(16) || !r.u16(tok)) { delete node; return NULL; }
        }
        if (tok != TOKEN_OBRACE) { delete node; return NULL; }

        for (;;) {
            if (!r.u16(tok)) { delete node; return NULL; }
            if (tok == TOKEN_CBRACE) break;

            switch (tok) {
                case TOKEN_INTEGER: {
                    unsigned v;
                    if (!r.u32(v)) { delete node; return NULL; }
                    appendU32(node->data, v);
                } break;
                case TOKEN_INTEGER_LIST: {
                    unsigned n;
                    if (!r.u32(n)) { delete node; return NULL; }
                    for (unsigned i = 0; i < n; ++i) {
                        unsigned v;
                        if (!r.u32(v)) { delete node; return NULL; }
                        appendU32(node->data, v);
                    }
                } break;
                case TOKEN_FLOAT_LIST: {
                    unsigned n;
                    if (!r.u32(n)) { delete node; return NULL; }
                    for (unsigned i = 0; i < n; ++i) {
                        float f;
                        if (!r.f32(f)) { delete node; return NULL; }
                        appendFloat(node->data, f);
                    }
                } break;
                case TOKEN_STRING: {
                    std::string s;
                    unsigned term;
                    if (!r.countedString(s) || !r.u16(term)) { delete node; return NULL; }
                    // D3DX presents a string member as a pointer into stable
                    // storage, which is what TextureFilename consumers expect.
                    std::string *stored = new std::string(s);
                    file->strings.push_back(stored);
                    node->stringOffsets.push_back(node->data.size());
                    appendPtr(node->data, stored->c_str());
                } break;
                case TOKEN_NAME: {
                    // A nested object, or a reference to a named one.
                    std::string childType;
                    if (!r.countedString(childType)) { delete node; return NULL; }
                    XNode *child = parseObject(childType);
                    if (!child) { delete node; return NULL; }
                    child->parent = node;
                    node->children.push_back(child);
                } break;
                case TOKEN_OBRACE: {
                    // `{ Name }` — a reference to an object defined elsewhere.
                    unsigned t2;
                    std::string refName;
                    if (!r.u16(t2)) { delete node; return NULL; }
                    if (t2 == TOKEN_NAME) {
                        if (!r.countedString(refName)) { delete node; return NULL; }
                        if (!r.u16(t2)) { delete node; return NULL; }
                    }
                    if (t2 != TOKEN_CBRACE) { delete node; return NULL; }
                    XNode *ref = new XNode();
                    ref->typeName = "__reference";
                    ref->name = refName;
                    ref->parent = node;
                    node->children.push_back(ref);
                } break;
                case TOKEN_SEMICOLON: case TOKEN_COMMA:
                    break;
                default:
                    // Unexpected token inside data: stop rather than desync.
                    warnOnce("binary .x: unexpected token in data object");
                    delete node;
                    return NULL;
            }
        }
        return node;
    }

    void parse() {
        for (;;) {
            unsigned tok;
            if (!r.u16(tok)) return;                 // clean end of stream
            if (tok == TOKEN_TEMPLATE) {
                if (!skipTemplate()) { ok = false; return; }
                continue;
            }
            if (tok == TOKEN_NAME) {
                std::string typeName;
                if (!r.countedString(typeName)) { ok = false; return; }
                XNode *node = parseObject(typeName);
                if (!node) { ok = false; return; }
                file->roots.push_back(node);
                continue;
            }
            // Anything else at top level: skip it.
        }
    }
};

// --------------------------------------------------------------- text form
struct TextParser {
    const char *p, *end;
    XFile *file;

    TextParser(const char *s, size_t n, XFile *f) : p(s), end(s + n), file(f) {}

    void skipSpace() {
        for (;;) {
            while (p < end && (unsigned char)*p <= ' ') ++p;
            if (p + 1 < end && p[0] == '/' && p[1] == '/') {
                while (p < end && *p != '\n') ++p;
                continue;
            }
            if (p < end && *p == '#') {              // '#' comments are legal too
                while (p < end && *p != '\n') ++p;
                continue;
            }
            break;
        }
    }
    bool word(std::string &out) {
        skipSpace();
        const char *s = p;
        while (p < end && (isalnum((unsigned char)*p) || *p == '_')) ++p;
        if (p == s) return false;
        out.assign(s, p - s);
        return true;
    }
    bool literal(char c) {
        skipSpace();
        if (p < end && *p == c) { ++p; return true; }
        return false;
    }
    char peek() { skipSpace(); return p < end ? *p : '\0'; }

    void skipBraces() {                              // for template definitions
        int depth = 0;
        while (p < end) {
            if (*p == '{') ++depth;
            else if (*p == '}') { --depth; ++p; if (depth <= 0) return; continue; }
            ++p;
        }
    }

    XNode *parseObject(const std::string &typeName) {
        XNode *node = new XNode();
        node->typeName = typeName;
        XFile_GuidForTemplate(typeName.c_str(), &node->type);

        std::string maybeName;
        if (peek() != '{') {
            if (!word(maybeName)) { delete node; return NULL; }
            node->name = maybeName;
        }
        if (!literal('{')) { delete node; return NULL; }

        for (;;) {
            char c = peek();
            if (c == '\0') { delete node; return NULL; }
            if (c == '}') { ++p; break; }
            if (c == ';' || c == ',') { ++p; continue; }

            if (c == '"') {                          // string member
                ++p;
                const char *s = p;
                while (p < end && *p != '"') ++p;
                std::string *stored = new std::string(s, p - s);
                if (p < end) ++p;
                file->strings.push_back(stored);
                appendPtr(node->data, stored->c_str());
                continue;
            }
            if (c == '-' || (c >= '0' && c <= '9')) {  // number member
                char *stop = NULL;
                double v = strtod(p, &stop);
                bool isFloat = false;
                for (const char *q = p; q < stop; ++q)
                    if (*q == '.' || *q == 'e' || *q == 'E') { isFloat = true; break; }
                p = stop;
                if (isFloat) appendFloat(node->data, (float)v);
                else appendU32(node->data, (unsigned)(long long)v);
                continue;
            }
            if (c == '{') {                          // reference
                ++p;
                std::string refName;
                word(refName);
                literal('}');
                XNode *ref = new XNode();
                ref->typeName = "__reference";
                ref->name = refName;
                ref->parent = node;
                node->children.push_back(ref);
                continue;
            }

            std::string childType;
            if (!word(childType)) { delete node; return NULL; }
            XNode *child = parseObject(childType);
            if (!child) { delete node; return NULL; }
            child->parent = node;
            node->children.push_back(child);
        }
        return node;
    }

    bool parse() {
        for (;;) {
            skipSpace();
            if (p >= end) return true;
            std::string w;
            if (!word(w)) return true;
            if (w == "template") {
                std::string tname;
                word(tname);
                skipBraces();
                continue;
            }
            XNode *node = parseObject(w);
            if (!node) return !file->roots.empty();
            file->roots.push_back(node);
        }
    }
};

} // namespace

bool XFile_GuidForTemplate(const char *name, GUID *out) {
    if (!name || !out) return false;
    for (size_t i = 0; i < kTemplateCount; ++i) {
        if (strcmp(kTemplates[i].name, name) == 0) { *out = kTemplates[i].guid; return true; }
    }
    memset(out, 0, sizeof(*out));
    return false;
}

XFile *XFile_Parse(const void *data, size_t size) {
    if (!data || size < 16) return NULL;
    const BYTE *p = (const BYTE *)data;
    if (memcmp(p, "xof ", 4) != 0) { warnOnce(".x: bad magic"); return NULL; }

    char format[5] = {0};
    memcpy(format, p + 8, 4);
    char floatBits[5] = {0};
    memcpy(floatBits, p + 12, 4);
    const bool doubleFloats = (strncmp(floatBits, "0064", 4) == 0);

    XFile *file = new XFile();

    if (strncmp(format, "txt", 3) == 0) {
        TextParser tp((const char *)(p + 16), size - 16, file);
        if (!tp.parse() && file->roots.empty()) { delete file; warnOnce(".x: text parse failed"); return NULL; }
        return file;
    }

    std::vector<BYTE> unpacked;
    const BYTE *body = p + 16;
    size_t bodySize = size - 16;

    if (strncmp(format, "bzip", 4) == 0) {
        if (!msZipDecompress(body, bodySize, unpacked)) {
            delete file;
            return NULL;
        }
        body = unpacked.empty() ? NULL : &unpacked[0];
        bodySize = unpacked.size();
    } else if (strncmp(format, "bin", 3) != 0) {
        delete file;
        warnOnce(".x: unknown format field");
        return NULL;
    }

    if (!body) { delete file; return NULL; }

    BinaryParser bp(body, bodySize, doubleFloats, file);
    bp.parse();
    if (file->roots.empty()) {
        delete file;
        warnOnce(".x: no data objects parsed");
        return NULL;
    }
    return file;
}
