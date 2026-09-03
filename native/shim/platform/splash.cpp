//  The screen shown while the client boots.
//
//  There is no way to draw this from inside the game: m_pd3dDevice is still NULL
//  right through DxGlobalStage::OneTimeSceneInit, and the lobby stage is entered
//  by assigning m_emThisStage directly rather than through ChangeStage, so
//  StartThreadLOAD never runs at startup - confirmed by RanLoad logging nothing
//  at all across a whole boot. The window was therefore plain black for the
//  entire load, which reads as a hang.
//
//  An Android window background was tried first and does not work here: with a
//  NativeActivity the surface is created and painted black immediately, and a
//  capture 0.7s after launch on the tablet was black with the theme correctly
//  linked and the drawable packaged. So it is drawn here instead, on the GL
//  surface the game itself will use - the swap chain holds it until the client's
//  own first frame replaces it, which is exactly as long as it is wanted.
//
//  It is the launcher's patch screen continuing, not a second screen: the same
//  lobby art filling the panel and the same RAN mark over it, in the same place.
//  The player has already been watching that page for the whole patch check, and
//  the client then boots for several seconds behind a surface it has not drawn
//  to yet.
//
//  Everything that used to make this look like its own page is gone - the
//  ld_top and ld_under bands, the HINT badge, the corner spinner. Half of them
//  the launcher could not show anyway, because its screen is painted while the
//  data root holding them is still downloading, and the difference read as two
//  loading pages for one wait.

#include "../gl/gl_platform.h"
#include "ran_plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOGI(...) RanPlat_Log(RANLOG_INFO,  "RanSplash", __VA_ARGS__)
#define LOGE(...) RanPlat_Log(RANLOG_ERROR, "RanSplash", __VA_ARGS__)

extern "C" int  RanGL_Width(void);
extern "C" int  RanGL_Height(void);
extern "C" void RanGL_Present(void);
extern "C" void RanSplash_Step(void);

namespace {

unsigned rd32(const unsigned char *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24);
}

//  DXT1, DXT5 and uncompressed 32-bit, which is everything the loading screen
//  uses. Always decoded to RGBA8 on the CPU: this device advertises S3TC and
//  does not honour it.
unsigned char *decodeDds(const unsigned char *b, long sz, unsigned *outW, unsigned *outH) {
    if (sz < 128 || memcmp(b, "DDS ", 4)) return NULL;
    const unsigned H = rd32(b + 12), W = rd32(b + 16);
    if (!W || !H || W > 8192 || H > 8192) return NULL;

    const unsigned pfFlags = rd32(b + 80);
    const bool fourCC = (pfFlags & 0x4) != 0;
    const bool dxt1 = fourCC && !memcmp(b + 84, "DXT1", 4);
    const bool dxt5 = fourCC && !memcmp(b + 84, "DXT5", 4);

    unsigned char *out = (unsigned char *)calloc((size_t)W * H, 4);
    if (!out) return NULL;

    if (dxt1 || dxt5) {
        const unsigned blockBytes = dxt5 ? 16 : 8;
        const unsigned bw = (W + 3) / 4, bh = (H + 3) / 4;
        if ((long)(128 + (size_t)bw * bh * blockBytes) > sz) { free(out); return NULL; }
        const unsigned char *blocks = b + 128;

        for (unsigned by = 0; by < bh; ++by)
            for (unsigned bx = 0; bx < bw; ++bx) {
                const unsigned char *blk = blocks + ((size_t)by * bw + bx) * blockBytes;

                unsigned char alpha[16];
                if (dxt5) {
                    const int a0 = blk[0], a1 = blk[1];
                    int a[8];
                    a[0] = a0; a[1] = a1;
                    if (a0 > a1) for (int i = 0; i < 6; ++i) a[2 + i] = ((6 - i) * a0 + (1 + i) * a1) / 7;
                    else {
                        for (int i = 0; i < 4; ++i) a[2 + i] = ((4 - i) * a0 + (1 + i) * a1) / 5;
                        a[6] = 0; a[7] = 255;
                    }
                    unsigned long long bits = 0;
                    for (int i = 0; i < 6; ++i) bits |= (unsigned long long)blk[2 + i] << (8 * i);
                    for (int i = 0; i < 16; ++i) alpha[i] = (unsigned char)a[(bits >> (3 * i)) & 7];
                    blk += 8;
                } else {
                    memset(alpha, 255, sizeof(alpha));
                }

                const unsigned c0 = blk[0] | (blk[1] << 8), c1 = blk[2] | (blk[3] << 8);
                int r[4], g[4], bl[4];
                r[0] = ((c0 >> 11) & 31) * 255 / 31; g[0] = ((c0 >> 5) & 63) * 255 / 63; bl[0] = (c0 & 31) * 255 / 31;
                r[1] = ((c1 >> 11) & 31) * 255 / 31; g[1] = ((c1 >> 5) & 63) * 255 / 63; bl[1] = (c1 & 31) * 255 / 31;
                //  DXT5 always uses the four-colour form; DXT1 only when c0 > c1.
                if (dxt5 || c0 > c1) {
                    r[2] = (2 * r[0] + r[1]) / 3; g[2] = (2 * g[0] + g[1]) / 3; bl[2] = (2 * bl[0] + bl[1]) / 3;
                    r[3] = (r[0] + 2 * r[1]) / 3; g[3] = (g[0] + 2 * g[1]) / 3; bl[3] = (bl[0] + 2 * bl[1]) / 3;
                } else {
                    r[2] = (r[0] + r[1]) / 2; g[2] = (g[0] + g[1]) / 2; bl[2] = (bl[0] + bl[1]) / 2;
                    r[3] = g[3] = bl[3] = 0;
                }

                const unsigned bits = blk[4] | (blk[5] << 8) | (blk[6] << 16) | ((unsigned)blk[7] << 24);
                for (int py = 0; py < 4; ++py)
                    for (int px = 0; px < 4; ++px) {
                        const unsigned x = bx * 4 + px, y = by * 4 + py;
                        if (x >= W || y >= H) continue;
                        const int k = py * 4 + px;
                        const int i = (bits >> (2 * k)) & 3;
                        unsigned char *o = out + ((size_t)y * W + x) * 4;
                        o[0] = (unsigned char)r[i]; o[1] = (unsigned char)g[i]; o[2] = (unsigned char)bl[i];
                        //  DXT1's fourth entry is transparent in the 3-colour form.
                        o[3] = (!dxt5 && c0 <= c1 && i == 3) ? 0 : alpha[k];
                    }
            }
        *outW = W; *outH = H;
        return out;
    }

    //  Uncompressed. Only 32-bit is used here; the masks say where the channels are.
    const unsigned bits = rd32(b + 88);
    if (bits != 32) { free(out); return NULL; }
    const unsigned rM = rd32(b + 92), gM = rd32(b + 96), bM = rd32(b + 100), aM = rd32(b + 104);
    if ((long)(128 + (size_t)W * H * 4) > sz) { free(out); return NULL; }
    const unsigned char *src = b + 128;

    //  Shift for each mask; a zero mask means the channel is absent.
    unsigned rs = 0, gs = 0, bs = 0, as = 0;
    for (unsigned m = rM; m && !(m & 1); m >>= 1) ++rs;
    for (unsigned m = gM; m && !(m & 1); m >>= 1) ++gs;
    for (unsigned m = bM; m && !(m & 1); m >>= 1) ++bs;
    for (unsigned m = aM; m && !(m & 1); m >>= 1) ++as;

    for (size_t i = 0; i < (size_t)W * H; ++i) {
        const unsigned px = rd32(src + i * 4);
        unsigned char *o = out + i * 4;
        o[0] = rM ? (unsigned char)((px & rM) >> rs) : 0;
        o[1] = gM ? (unsigned char)((px & gM) >> gs) : 0;
        o[2] = bM ? (unsigned char)((px & bM) >> bs) : 0;
        o[3] = aM ? (unsigned char)((px & aM) >> as) : 255;
    }
    *outW = W; *outH = H;
    return out;
}

GLuint loadTexture(const char *root, const char *name, unsigned *w, unsigned *h) {
    char path[512];
    snprintf(path, sizeof(path), "%s/textures/gui/%s", root, name);
    FILE *f = fopen(path, "rb");
    if (!f) { LOGE("missing %s", path); return 0; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *raw = (unsigned char *)malloc(sz > 0 ? sz : 1);
    const size_t got = raw ? fread(raw, 1, (size_t)sz, f) : 0;
    fclose(f);
    if (!raw || got != (size_t)sz) { free(raw); return 0; }

    unsigned W = 0, H = 0;
    unsigned char *rgba = decodeDds(raw, sz, &W, &H);
    free(raw);
    if (!rgba) { LOGE("cannot decode %s", name); return 0; }

    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)W, (GLsizei)H, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    free(rgba);
    if (w) *w = W;
    if (h) *h = H;
    return t;
}

const char *kVert =
    "#version 300 es\n"
    "layout(location=0) in vec2 aPos;\n"
    "uniform vec4 uRect;\n"      // x, y, w, h in pixels
    "uniform vec4 uUV;\n"        // u0, v0, du, dv - the sub-rect to sample
    "uniform vec2 uViewport;\n"
    "out vec2 vUV;\n"
    "void main(){\n"
    "  vUV = uUV.xy + aPos * uUV.zw;\n"
    "  vec2 p = uRect.xy + aPos * uRect.zw;\n"
    "  gl_Position = vec4((p.x/uViewport.x)*2.0-1.0, 1.0-(p.y/uViewport.y)*2.0, 0.0, 1.0);\n"
    "}\n";

const char *kFrag =
    "#version 300 es\n"
    "precision mediump float;\n"
    "uniform sampler2D uTex;\n"
    "in vec2 vUV;\n"
    "out vec4 oColor;\n"
    "void main(){ oColor = texture(uTex, vUV); }\n";

GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512]; glGetShaderInfoLog(s, sizeof(log), NULL, log); LOGE("shader: %s", log); glDeleteShader(s); return 0; }
    return s;
}

GLint g_uRect = -1, g_uUV = -1;

//  uUV is the sub-rectangle of the texture to sample, as (u0,v0,du,dv). The
//  spinner is a strip of four 105x105 frames in one 512x128 image, which is how
//  NLOADINGTHREAD animates it.
void quad(GLuint tex, float x, float y, float w, float h,
          float u0 = 0.0f, float v0 = 0.0f, float du = 1.0f, float dv = 1.0f) {
    if (!tex) return;
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform4f(g_uRect, x, y, w, h);
    glUniform4f(g_uUV, u0, v0, du, dv);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

//  Kept between calls so the boot screen can be redrawn a frame at a time while
//  the client loads, instead of being painted once and left static.
struct State {
    GLuint art, mark, cover;
    float  markU0, markV0, markDU, markDV;
    GLuint prog, vao, vbo;
    int    frame;
    bool   live;
} g_s = { 0,0,0, 0,0,0,0, 0,0,0, 0, false };

//  The launcher's own page, rasterised.
//
//  The patch screen has to stay on screen unchanged while the client boots -
//  art, dark band, status line and progress bar, exactly as the player was
//  already looking at. Redrawing that here would mean reimplementing the band
//  and its text in GL before the client's font system exists, and it would
//  drift from the launcher's layout the first time either changed.
//
//  So the launcher draws its own view hierarchy into a bitmap just before it
//  starts the game and leaves it in cache/, and this paints that. Same picture
//  by construction. cache/ because the patch manifest does not list it, so a
//  patch never fights over the file.
GLuint loadCover(const char *root) {
    char path[512];
    snprintf(path, sizeof(path), "%s/cache/bootcover.bin", root);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    unsigned hdr[3] = { 0, 0, 0 };
    if (fread(hdr, 4, 3, f) != 3 || hdr[0] != 0x434e4152u /* "RANC" */ ||
        !hdr[1] || !hdr[2] || hdr[1] > 4096 || hdr[2] > 4096) { fclose(f); return 0; }

    const size_t bytes = (size_t)hdr[1] * hdr[2] * 4;
    unsigned char *px = (unsigned char *)malloc(bytes);
    const size_t got = px ? fread(px, 1, bytes, f) : 0;
    fclose(f);
    if (!px || got != bytes) { free(px); return 0; }

    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)hdr[1], (GLsizei)hdr[2], 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    free(px);
    LOGI("boot cover %ux%u from the launcher", hdr[1], hdr[2]);
    return t;
}

} // namespace

//  Load everything and paint the first frame. Safe before anything else has
//  touched GL.
extern "C" void RanSplash_Begin(const char *dataRoot) {
    if (!dataRoot || g_s.live) return;

    unsigned artW = 0, artH = 0;
    g_s.cover = loadCover(dataRoot);
    if (!g_s.cover) {
        //  No handover picture - the game was started without the launcher, or
        //  the write failed. Fall back to composing the same thing here.
        g_s.art = loadTexture(dataRoot, "loading_002.dds", &artW, &artH);
        if (!g_s.art) return;                   // no art, no boot screen
    }

    //  The same file LOGIN_MARK now names in the ui config, whole rather than
    //  as a sub-rectangle of a sheet: one copy of the logo, so the launcher's
    //  PNG, the login screen and this cannot drift apart.
    unsigned sheetW = 0, sheetH = 0;
    if (!g_s.cover) g_s.mark = loadTexture(dataRoot, "ranlegacy_mark.dds", &sheetW, &sheetH);
    if (g_s.mark && sheetW && sheetH) {
        g_s.markU0 = 0.0f; g_s.markV0 = 0.0f;
        g_s.markDU = 1.0f; g_s.markDV = 1.0f;
    } else {
        g_s.mark = 0;
    }

    const GLuint vs = compile(GL_VERTEX_SHADER, kVert);
    const GLuint fs = compile(GL_FRAGMENT_SHADER, kFrag);
    if (!vs || !fs) return;
    g_s.prog = glCreateProgram();
    glAttachShader(g_s.prog, vs); glAttachShader(g_s.prog, fs);
    glLinkProgram(g_s.prog);
    glDeleteShader(vs); glDeleteShader(fs);

    static const float unit[12] = { 0,0, 1,0, 1,1, 0,0, 1,1, 0,1 };
    glGenVertexArrays(1, &g_s.vao);
    glBindVertexArray(g_s.vao);
    glGenBuffers(1, &g_s.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_s.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(unit), unit, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (const void *)0);

    g_s.frame = 0;
    g_s.live  = true;
    LOGI("boot screen up (art %ux%u)", artW, artH);
    RanSplash_Step();
}

//  Repaint between the client boot steps, which
//  is the same thing LOADINGSTEP::SETSTEP does for the in-game loading screen -
//  so the animation tracks real progress rather than a timer.
extern "C" void RanSplash_Step(void) {
    if (!g_s.live) return;

    const float W = (float)RanGL_Width(), H = (float)RanGL_Height();
    const float sx = W / 1024.0f, sy = H / 768.0f;

    glBindVertexArray(g_s.vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_s.vbo);
    glViewport(0, 0, (GLsizei)W, (GLsizei)H);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(g_s.prog);
    g_uRect = glGetUniformLocation(g_s.prog, "uRect");
    g_uUV   = glGetUniformLocation(g_s.prog, "uUV");
    glUniform2f(glGetUniformLocation(g_s.prog, "uViewport"), W, H);
    glUniform1i(glGetUniformLocation(g_s.prog, "uTex"), 0);
    glActiveTexture(GL_TEXTURE0);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    //  The same 1024x768 virtual layout NLOADINGTHREAD uses, scaled to the panel.
    //
    //  This is the launcher's patch screen continuing, not a second screen.
    //
    //  The player has already been looking at this art for the whole patch
    //  check; the client then boots for several seconds behind a surface it has
    //  not drawn to yet. Painting anything different here - the HINT badge, the
    //  corner spinner, the top and bottom bands - turned that handover into a
    //  visible jump between two loading pages for one wait. So it is the same
    //  composition the launcher uses: the art filling the panel, and the RAN
    //  mark in the same place. Removing it altogether was the other option, and
    //  is worse: the surface underneath is black.
    //
    //  Cover rather than fit, matching the launcher's CENTER_CROP: the art is
    //  2:1 and no panel is, and bars down the sides would not line up with what
    //  the launcher just showed.
    if (g_s.cover) {
        //  Already the whole window, laid out by the launcher. 1:1.
        quad(g_s.cover, 0.0f, 0.0f, W, H);
    } else {
        const float aspect = W / H;
        float w = W, h = H;
        if (aspect > 2.0f) h = W / 2.0f;        //  wider than the art: fill width
        else               w = H * 2.0f;        //  taller: fill height
        quad(g_s.art, (W - w) * 0.5f, (H - h) * 0.5f, w, h);
    }

    //  Same size and position as the launcher's: 230dp wide, 30dp from the top,
    //  centred. dp is 160ths of an inch; this panel reports its own density, so
    //  the closest thing available here is a fraction of the width.
    if (g_s.mark) {
        //  Square now, and the same fraction of the width the launcher uses.
        const float mw = W * 0.137f, mh = mw;
        quad(g_s.mark, (W - mw) * 0.5f, H * 0.035f, mw, mh,
             g_s.markU0, g_s.markV0, g_s.markDU, g_s.markDV);
    }

    RanGL_Present();
    ++g_s.frame;
}

//  Let go of everything. The last painted frame stays on screen until the
//  client presents its own.
extern "C" void RanSplash_End(void) {
    if (!g_s.live) return;
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    glDisable(GL_BLEND);
    glDeleteBuffers(1, &g_s.vbo);
    glDeleteVertexArrays(1, &g_s.vao);
    glDeleteProgram(g_s.prog);
    const GLuint texes[3] = { g_s.art, g_s.mark, g_s.cover };
    glDeleteTextures(3, texes);
    g_s.art = g_s.mark = g_s.cover = 0;
    g_s.live = false;
    LOGI("boot screen done after %d frames", g_s.frame);
}
