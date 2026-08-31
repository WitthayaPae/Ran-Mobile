//  The on-screen touch controls.
//
//  Drawn after the client's frame and given first refusal on every touch. A
//  control that owns a touch claims it and the client never sees that finger;
//  everything else falls through, so tapping the world and the game's own
//  windows keeps working.
//
//  Nothing here knows anything about the game. The stick publishes a direction
//  and the buttons publish edges; a small guarded hook on the game side reads
//  them and calls the client's own movement and attack paths. That keeps SOURCE
//  byte-identical to the PC build and keeps this file portable.
#include "touch_ui.h"

#include <GLES3/gl3.h>
#include <android/log.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

extern "C" void RanGLR_InvalidateStateCache(void);
extern "C" void RanInput_PointerWheel(int dz);

//  Whether one of the game's own controls covers a point.
//
//  Weakly linked: this file is also built into targets that have no client to
//  ask, and a pad with no windows over it behaves exactly as it did before.
extern "C" int RanUI_PointInControl(int x, int y) __attribute__((weak));

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "RanTouch", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "RanTouch", __VA_ARGS__)

namespace {

// ---------------------------------------------------------------- geometry
struct Vec2 { float x, y; };

inline float len(float x, float y) { return sqrtf(x * x + y * y); }

// ------------------------------------------------------------------- state
bool  g_inited = false;
bool  g_active = false;
//  The game stage calls SetActive(1) every frame it is running. Counting
//  frames since the last call means the controls disappear on their own when
//  the world unloads, without every exit path having to remember to hide them.
int   g_activeAge = 999;
int   g_width = 0, g_height = 0;

//  Laid out against the short edge so the controls keep their physical size
//  whatever the panel is.
float g_unit = 100.0f;

//  --- movement stick -----------------------------------------------------
struct Stick {
    Vec2  centre    = { 0, 0 };     // where the ring sits
    Vec2  origin    = { 0, 0 };     // where the finger first went down
    Vec2  knob      = { 0, 0 };
    float radius    = 0.0f;
    int   pointer   = -1;           // the finger holding it, -1 for none
    bool  held      = false;
    Vec2  dir       = { 0, 0 };     // normalised, y positive = down the screen
    float magnitude = 0.0f;         // 0..1
} g_stick;

//  --- the attack button --------------------------------------------------
//
//  Just the one. The skill buttons around it are the client's own quick-skill
//  tray, moved into a quarter arc here - drawing a second set would mean
//  reimplementing skill icons, cooldown sweeps and drag-to-assign, and leave
//  the player with two of everything.
struct Button {
    Vec2  centre = { 0, 0 };
    float radius = 0.0f;
    int   pointer = -1;
    bool  down = false;
    bool  pressedEdge = false;      // set on press, cleared when consumed
    bool  toggled = false;          // lit: the client says this mode is on
    int   slot = 0;                 // one of the kSlot* codes, or 0..9
};

//  Codes come from the header: the client hook switches on the same values.
const int kSlotAttack   = RANTOUCH_SLOT_ATTACK;
const int kSlotPagePrev = RANTOUCH_SLOT_PAGE_PREV;
const int kSlotPageNext = RANTOUCH_SLOT_PAGE_NEXT;
const int kSlotAuto     = RANTOUCH_SLOT_AUTO;
const int kSlotPK       = RANTOUCH_SLOT_PK;
const int kSlotPickup   = RANTOUCH_SLOT_PICKUP;
const int kSlotCamLock  = RANTOUCH_SLOT_CAMLOCK;

//  Attack, two page arrows, the auto-target and PK toggles, and pick-up.
const int kButtonCount = 7;
Button g_buttons[kButtonCount];

//  Rims over the client's skill slots, in surface pixels. Filled in by the
//  client each time it lays the arc out.
struct SkillCircle { float x, y, r; bool filled; float cool; };
SkillCircle g_skillCircles[RANTOUCH_MAX_SKILL_CIRCLES];
int         g_skillCircleCount = 0;

//  --- pinch --------------------------------------------------------------
struct Pinch {
    int   a = -1, b = -1;
    float startDist = 0.0f;
    float lastDist = 0.0f;
} g_pinch;

//  Live touches we are tracking, so a pinch can be recognised from fingers that
//  no control claimed.
const int kMaxPointers = 10;
struct Touch { int id = -1; float x = 0, y = 0; bool claimed = false; } g_touch[kMaxPointers];

Touch *findTouch(int id) {
    for (int i = 0; i < kMaxPointers; ++i) if (g_touch[i].id == id) return &g_touch[i];
    return NULL;
}
Touch *addTouch(int id, float x, float y) {
    for (int i = 0; i < kMaxPointers; ++i) {
        if (g_touch[i].id < 0) { g_touch[i].id = id; g_touch[i].x = x; g_touch[i].y = y;
                                 g_touch[i].claimed = false; return &g_touch[i]; }
    }
    return NULL;
}
void removeTouch(int id) {
    for (int i = 0; i < kMaxPointers; ++i) if (g_touch[i].id == id) g_touch[i].id = -1;
}
int unclaimedCount() {
    int n = 0;
    for (int i = 0; i < kMaxPointers; ++i) if (g_touch[i].id >= 0 && !g_touch[i].claimed) ++n;
    return n;
}

// ------------------------------------------------------------------- GL
GLuint g_prog = 0, g_vbo = 0, g_vao = 0;

//  Interleaved x,y,r,g,b,a. Sized for the largest shape we emit, which is a
//  96-segment feathered disc: (1 + 97 + 97) vertices.
const int   kFloatsPerVert = 6;
float g_verts[1600 * kFloatsPerVert];

//  One draw for the whole overlay.
//
//  Every shape used to end in its own glBufferSubData + glDrawArrays. There are
//  a few hundred shapes in a frame - each control is a halo, a graded face, an
//  inner ring, a bevel, a gloss, a catchlight and a glyph - and the frame report
//  measured that as `touch-hud` 7.7-8.8 ms, about a quarter of the whole frame,
//  for geometry that fits comfortably in a single draw.
//
//  It did not show up in either of the obvious places. The shim's `submit`
//  timer only covers draws issued through the renderer, and /sdcard/ran/nulldraw
//  only suppresses those - the overlay has its own program, VAO and buffer, so
//  both reported the frame as cheap while it was not.
//
//  Shapes are now converted to triangles as they are built and accumulated
//  here. The real draw happens at emit(), which runs when the blend mode
//  changes, when the program changes, and once at the end of the frame.
const int kBatchVerts  = 160000;
const int kBatchFloats = kBatchVerts * kFloatsPerVert;
float    g_batch[kBatchFloats];
int      g_bn = 0;          //  floats queued
unsigned g_batchDraws = 0;  //  real draws issued this frame
unsigned g_vertsThisFrame = 0;

//  The static half of the overlay, built once and kept.
//
//  Measured on LDPlayer: with the overlay drawn the frame was 46.5 ms and with
//  /sdcard/ran/nohud it was 28.4 - the controls cost 18 ms a frame, and were
//  generating 94,086 vertices to do it. Almost none of that geometry changes:
//  the buttons do not move, and their faces, bevels, glosses and glyphs are
//  identical from one frame to the next. It was being rebuilt sixty times a
//  second because there was nowhere to keep it.
//
//  So it is built into its own buffer and redrawn from there, and only rebuilt
//  when something it depends on actually changes - a button going down, a
//  toggle lighting, the skill arc being rearranged, the window resizing. The
//  parts that genuinely move each frame (the stick, the recharge wipes) are
//  still built live, and they are small.
GLuint   g_vboCache = 0;
GLuint   g_vaoCache = 0;
struct Seg { int first, count; bool add; };
Seg      g_segs[32];
int      g_segCount = 0;
bool     g_capturing = false;
bool     g_additive  = false;
int      g_capFirst  = 0;           //  first vertex of the segment being built
unsigned long long g_sig = 0;       //  what the cached geometry was built from
int      g_cacheVerts = 0;
unsigned g_rebuilds = 0;            //  captures this second, for the report
GLint uViewport = -1;

//  One colour, passed around as four floats, so a helper can take "a colour"
//  rather than four parameters that can be given in the wrong order.
struct Col { float r, g, b, a; };
inline Col rgba(float r, float g, float b, float a) { Col c = { r, g, b, a }; return c; }
inline Col alpha(Col c, float k) { c.a *= k; return c; }
inline Col mixc(Col x, Col y, float t) {
    return rgba(x.r + (y.r - x.r) * t, x.g + (y.g - x.g) * t,
                x.b + (y.b - x.b) * t, x.a + (y.a - x.a) * t);
}

const char *kVS =
    "#version 300 es\n"
    "layout(location=0) in vec2 aPos;\n"
    "layout(location=1) in vec4 aCol;\n"
    "uniform vec2 uViewport;\n"
    "out vec4 vCol;\n"
    "void main() {\n"
    "    vCol = aCol;\n"
    //  Surface pixels, y down, to clip space.
    "    vec2 p = vec2(aPos.x / uViewport.x, 1.0 - aPos.y / uViewport.y);\n"
    "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

const char *kFS =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec4 vCol;\n"
    "out vec4 oColor;\n"
    "void main() { oColor = vCol; }\n";

GLuint compile(GLenum type, const char *src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        LOGE("touch shader: %s", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

bool buildProgram() {
    //  A rebuilt program means a rebuilt context, and the cached geometry died
    //  with it. Without this the signature would still match the state the old
    //  buffer was built from, and the overlay would replay from an empty one.
    g_cacheVerts = 0;
    g_sig = 0;
    g_segCount = 0;

    GLuint vs = compile(GL_VERTEX_SHADER, kVS);
    GLuint fs = compile(GL_FRAGMENT_SHADER, kFS);
    if (!vs || !fs) return false;
    g_prog = glCreateProgram();
    glAttachShader(g_prog, vs);
    glAttachShader(g_prog, fs);
    glLinkProgram(g_prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(g_prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(g_prog, sizeof(log), NULL, log);
        LOGE("touch link: %s", log);
        return false;
    }
    uViewport = glGetUniformLocation(g_prog, "uViewport");
    glGenBuffers(1, &g_vbo);

    //  A VAO of our own, and this is not optional.
    //
    //  glVertexAttribPointer records into whichever VAO is bound. Without one of
    //  these the overlay was writing attribute 0 of whatever the client had
    //  bound, pointing that VAO at this buffer. The renderer caches which VAOs
    //  it has already described and rebinds them without describing them again,
    //  so it then drew geometry out of here - long white streaks across the
    //  scene, with the driver allocating GPU memory inside every draw call
    //  trying to service it. That surfaced as an ANR: a stalled render loop
    //  stops input being consumed, and the system kills the app for it.
    glGenVertexArrays(1, &g_vao);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    //  Sized once and refilled with glBufferSubData rather than orphaned per
    //  shape: forty glBufferData calls a frame is forty allocations, and this
    //  driver charges real time for them.
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(g_batch), NULL, GL_STREAM_DRAW);
    const GLsizei stride = kFloatsPerVert * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (const void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (const void *)(2 * sizeof(float)));
    glBindVertexArray(0);

    //  The cache needs its own pair. A VAO records which buffer each attribute
    //  reads from, so the cached geometry cannot be drawn through the VAO that
    //  points at the streaming buffer.
    glGenBuffers(1, &g_vboCache);
    glGenVertexArrays(1, &g_vaoCache);
    glBindVertexArray(g_vaoCache);
    glBindBuffer(GL_ARRAY_BUFFER, g_vboCache);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (const void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (const void *)(2 * sizeof(float)));
    glBindVertexArray(0);

    return true;
}

// ------------------------------------------------------------- primitives

int g_n = 0;                                   //  floats written this shape

inline void vtx(float x, float y, Col c) {
    if (g_n + kFloatsPerVert > (int)(sizeof(g_verts) / sizeof(g_verts[0]))) return;
    g_verts[g_n++] = x; g_verts[g_n++] = y;
    g_verts[g_n++] = c.r; g_verts[g_n++] = c.g; g_verts[g_n++] = c.b; g_verts[g_n++] = c.a;
}
inline void begin() { g_n = 0; }

//  Draw everything queued so far. Must be called before anything changes state
//  the queued geometry depends on - the blend mode or the program - because a
//  deferred draw would then be issued under the new state rather than its own.
void emit() {
    if (g_bn <= 0) return;

    //  Capturing: close a segment and keep accumulating. The batch is uploaded
    //  once, at the end of the capture, and the segments say where the blend
    //  mode changes inside it - which is the one piece of state the geometry
    //  cannot carry itself.
    if (g_capturing) {
        const int end = g_bn / kFloatsPerVert;
        if (end > g_capFirst && g_segCount < (int)(sizeof(g_segs) / sizeof(g_segs[0]))) {
            g_segs[g_segCount].first = g_capFirst;
            g_segs[g_segCount].count = end - g_capFirst;
            g_segs[g_segCount].add   = g_additive;
            ++g_segCount;
        }
        g_capFirst = end;
        return;
    }

    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(g_bn * sizeof(float)), g_batch);
    glDrawArrays(GL_TRIANGLES, 0, g_bn / kFloatsPerVert);
    g_vertsThisFrame += (unsigned)(g_bn / kFloatsPerVert);
    g_bn = 0;
    ++g_batchDraws;
}

inline void putVert(const float *v) {
    for (int i = 0; i < kFloatsPerVert; ++i) g_batch[g_bn++] = v[i];
}
inline void putTri(int a, int b, int c) {
    putVert(g_verts + a * kFloatsPerVert);
    putVert(g_verts + b * kFloatsPerVert);
    putVert(g_verts + c * kFloatsPerVert);
}

//  Ends a shape: expand it to a triangle list and queue it. Fans and strips
//  cost nothing to expand - it is index arithmetic over a handful of vertices -
//  and once every shape is GL_TRIANGLES there is no primitive mode left to
//  break the batch on.
inline void flush(GLenum mode) {
    if (g_n <= 0) return;
    const int nv   = g_n / kFloatsPerVert;
    const int tris = (mode == GL_TRIANGLES) ? nv / 3 : (nv >= 3 ? nv - 2 : 0);
    const int need = tris * 3 * kFloatsPerVert;

    //  Out of room: draw what is queued and carry on into an empty batch. The
    //  result is identical, it just costs one more draw.
    if (g_bn + need > kBatchFloats) emit();

    if (need > 0 && g_bn + need > kBatchFloats) {
        //  Only reachable while capturing, where emit() cannot make room.
        //  Dropping a shape would be an invisible corruption, so say it.
        static bool s_warned = false;
        if (!s_warned) { LOGE("touch-hud: batch full at %d verts, geometry dropped", g_bn / kFloatsPerVert); s_warned = true; }
    }
    if (need > 0 && g_bn + need <= kBatchFloats) {
        if (mode == GL_TRIANGLE_FAN) {
            for (int i = 1; i <= tris; ++i) putTri(0, i, i + 1);
        } else if (mode == GL_TRIANGLE_STRIP) {
            //  Every other triangle is wound the other way in a strip, and the
            //  overlay draws with culling off - but keep the winding correct
            //  anyway, so this geometry stays valid if that ever changes.
            for (int i = 0; i < tris; ++i) {
                if (i & 1) putTri(i + 1, i, i + 2);
                else       putTri(i, i + 1, i + 2);
            }
        } else {
            for (int i = 0; i < tris; ++i) putTri(i * 3, i * 3 + 1, i * 3 + 2);
        }
    }
    g_n = 0;
}

//  Segment count follows the radius. A fixed 40-gon is smooth on a page arrow
//  and visibly faceted on the attack button, which is five times the size.
inline int segs(float r) { return r > 80.0f ? 96 : (r > 40.0f ? 64 : 40); }

//  A disc whose centre and edge colours differ, in ONE draw.
//
//  This is what drawTurned used to approximate with up to thirty stacked fans,
//  and the steps between those fans were visible on anything large. It also
//  carries a feather: a ring of fully transparent vertices just outside the
//  rim, which is the only antialiasing available here and is what removes the
//  polygon corners from every round control.
void discGrad(float cx, float cy, float r, Col centre, Col edge) {
    const int n = segs(r);
    const float fe = 1.25f;                    //  feather width in pixels
    begin();
    vtx(cx, cy, centre);
    for (int i = 0; i <= n; ++i) {
        const float t = (float)i / (float)n * 6.2831853f;
        vtx(cx + cosf(t) * r, cy + sinf(t) * r, edge);
    }
    flush(GL_TRIANGLE_FAN);

    Col clear = edge; clear.a = 0.0f;
    begin();
    for (int i = 0; i <= n; ++i) {
        const float t = (float)i / (float)n * 6.2831853f;
        const float c = cosf(t), si = sinf(t);
        vtx(cx + c * r, cy + si * r, edge);
        vtx(cx + c * (r + fe), cy + si * (r + fe), clear);
    }
    flush(GL_TRIANGLE_STRIP);
}

void drawFan(float cx, float cy, float r, float r_, float g_, float b_, float a_) {
    discGrad(cx, cy, r, rgba(r_, g_, b_, a_), rgba(r_, g_, b_, a_));
}

//  A ring, feathered on both edges.
void drawRing(float cx, float cy, float rInner, float rOuter,
              float r_, float g_, float b_, float a_) {
    const int n = segs(rOuter);
    const Col c = rgba(r_, g_, b_, a_);
    Col clear = c; clear.a = 0.0f;
    const float fe = 1.0f;
    begin();
    for (int i = 0; i <= n; ++i) {
        const float t = (float)i / (float)n * 6.2831853f;
        const float co = cosf(t), si = sinf(t);
        vtx(cx + co * (rInner - fe), cy + si * (rInner - fe), clear);
        vtx(cx + co * rInner,        cy + si * rInner,        c);
    }
    flush(GL_TRIANGLE_STRIP);
    begin();
    for (int i = 0; i <= n; ++i) {
        const float t = (float)i / (float)n * 6.2831853f;
        const float co = cosf(t), si = sinf(t);
        vtx(cx + co * rInner, cy + si * rInner, c);
        vtx(cx + co * rOuter, cy + si * rOuter, c);
    }
    flush(GL_TRIANGLE_STRIP);
    begin();
    for (int i = 0; i <= n; ++i) {
        const float t = (float)i / (float)n * 6.2831853f;
        const float co = cosf(t), si = sinf(t);
        vtx(cx + co * rOuter,        cy + si * rOuter,        c);
        vtx(cx + co * (rOuter + fe), cy + si * (rOuter + fe), clear);
    }
    flush(GL_TRIANGLE_STRIP);
}

//  The bottom `frac` of a disc, flat - the client's recharge wipe.
void drawDiscBottom(float cx, float cy, float r, float frac,
                    float r_, float g_, float b_, float a_) {
    if (frac <= 0.0f || r <= 0.0f) return;
    const Col c = rgba(r_, g_, b_, a_);
    const int rows = 24;
    const float y0 = cy + r - 2.0f * r * frac;
    begin();
    for (int i = 0; i <= rows; ++i) {
        const float y = y0 + (cy + r - y0) * (float)i / (float)rows;
        const float dy = y - cy;
        float half = r * r - dy * dy; half = half > 0.0f ? sqrtf(half) : 0.0f;
        vtx(cx - half, y, c);
        vtx(cx + half, y, c);
    }
    flush(GL_TRIANGLE_STRIP);
}

void drawArc(float cx, float cy, float rInner, float rOuter,
             float a0, float a1, float r_, float g_, float b_, float a_) {
    const int n = 24;
    const Col c = rgba(r_, g_, b_, a_);
    begin();
    for (int i = 0; i <= n; ++i) {
        const float t = a0 + (a1 - a0) * (float)i / (float)n;
        const float co = cosf(t), si = sinf(t);
        vtx(cx + co * rOuter, cy + si * rOuter, c);
        vtx(cx + co * rInner, cy + si * rInner, c);
    }
    flush(GL_TRIANGLE_STRIP);
}

//  An arc whose alpha ramps up from nothing and back down again.
//
//  The bevel is a lit arc and a shadowed arc, and with a flat alpha each one
//  stops dead where it meets the other - two hard notches on every control,
//  visible on the stick as a step at two o'clock. Fading the ends is only
//  possible now the colour is per vertex, and it is what makes the rim look
//  turned rather than painted in two halves.
void drawArcFade(float cx, float cy, float rInner, float rOuter,
                 float a0, float a1, Col c) {
    const int n = 28;
    begin();
    for (int i = 0; i <= n; ++i) {
        const float u = (float)i / (float)n;
        const float t = a0 + (a1 - a0) * u;
        //  sin gives 0 at both ends and 1 in the middle.
        Col k = c; k.a = c.a * sinf(u * 3.14159265f);
        const float co = cosf(t), si = sinf(t);
        vtx(cx + co * rOuter, cy + si * rOuter, k);
        vtx(cx + co * rInner, cy + si * rInner, k);
    }
    flush(GL_TRIANGLE_STRIP);
}

void drawQuad4(float x0, float y0, float x1, float y1,
               float x2, float y2, float x3, float y3,
               float r_, float g_, float b_, float a_) {
    const Col c = rgba(r_, g_, b_, a_);
    begin();
    vtx(x0, y0, c); vtx(x1, y1, c); vtx(x2, y2, c); vtx(x3, y3, c);
    flush(GL_TRIANGLE_FAN);
}

void drawRect(float x, float y, float w, float h,
              float r_, float g_, float b_, float a_) {
    drawQuad4(x, y, x + w, y, x + w, y + h, x, y + h, r_, g_, b_, a_);
}

//  A convex polygon, which is what every painted glyph is built from.
void drawPoly(const float *xy, int count, Col c) {
    if (count < 3) return;
    begin();
    for (int i = 0; i < count; ++i) vtx(xy[i * 2], xy[i * 2 + 1], c);
    flush(GL_TRIANGLE_FAN);
}

//  A round-capped line, as one strip plus two fans. Used by the loot chevron
//  and the grip wraps.
void drawCapsule(float x0, float y0, float x1, float y1, float hw, Col c) {
    float dx = x1 - x0, dy = y1 - y0;
    const float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.0001f) { discGrad(x0, y0, hw, c, c); return; }
    dx /= len; dy /= len;
    const float nx = -dy * hw, ny = dx * hw;
    drawQuad4(x0 + nx, y0 + ny, x1 + nx, y1 + ny,
              x1 - nx, y1 - ny, x0 - nx, y0 - ny, c.r, c.g, c.b, c.a);
    discGrad(x0, y0, hw, c, c);
    discGrad(x1, y1, hw, c, c);
}

void drawRamp(float x, float y, float w, float h,
              float r0, float g0, float b0, float r1, float g1, float b1, float a_) {
    const Col top = rgba(r0, g0, b0, a_), bot = rgba(r1, g1, b1, a_);
    begin();
    vtx(x, y, top); vtx(x + w, y, top); vtx(x, y + h, bot); vtx(x + w, y + h, bot);
    flush(GL_TRIANGLE_STRIP);
}

void drawChamfer(float x, float y, float w, float h, float cut,
                 float r_, float g_, float b_, float a_) {
    const Col c = rgba(r_, g_, b_, a_);
    const float pts[8][2] = {
        { x + cut,     y           }, { x + w - cut, y           },
        { x + w,       y + cut     }, { x + w,       y + h - cut },
        { x + w - cut, y + h       }, { x + cut,     y + h       },
        { x,           y + h - cut }, { x,           y + cut     },
    };
    begin();
    for (int i = 0; i < 8; ++i) vtx(pts[i][0], pts[i][1], c);
    flush(GL_TRIANGLE_FAN);
}

//  Seven-segment digits. Only 1..4 are ever shown, but the whole set is here
//  so the page count is not baked into the shape of the code.
void drawDigit(float x, float y, float w, float h, int d,
               float r_, float g_, float b_, float a_) {
    const float t = w * 0.22f;                 // stroke
    const float midY = y + h * 0.5f - t * 0.5f;
    //                      a      b      c      d      e      f      g
    static const bool on[10][7] = {
        {1,1,1,1,1,1,0},{0,1,1,0,0,0,0},{1,1,0,1,1,0,1},{1,1,1,1,0,0,1},
        {0,1,1,0,0,1,1},{1,0,1,1,0,1,1},{1,0,1,1,1,1,1},{1,1,1,0,0,0,0},
        {1,1,1,1,1,1,1},{1,1,1,1,0,1,1},
    };
    if (d < 0 || d > 9) return;
    const bool *S = on[d];
    if (S[0]) drawRect(x + t,     y,              w - t * 2.0f, t, r_, g_, b_, a_);
    if (S[1]) drawRect(x + w - t, y + t,          t, h * 0.5f - t * 1.5f, r_, g_, b_, a_);
    if (S[2]) drawRect(x + w - t, midY + t,       t, h * 0.5f - t * 1.5f, r_, g_, b_, a_);
    if (S[3]) drawRect(x + t,     y + h - t,      w - t * 2.0f, t, r_, g_, b_, a_);
    if (S[4]) drawRect(x,         midY + t,       t, h * 0.5f - t * 1.5f, r_, g_, b_, a_);
    if (S[5]) drawRect(x,         y + t,          t, h * 0.5f - t * 1.5f, r_, g_, b_, a_);
    if (S[6]) drawRect(x + t,     midY,           w - t * 2.0f, t, r_, g_, b_, a_);
}

void drawTri(float cx, float cy, float r, float dir,
             float r_, float g_, float b_, float a_) {
    const Col c = rgba(r_, g_, b_, a_);
    begin();
    vtx(cx,            cy + r * dir,       c);
    vtx(cx - r * 0.9f, cy - r * dir * 0.7f, c);
    vtx(cx + r * 0.9f, cy - r * dir * 0.7f, c);
    flush(GL_TRIANGLES);
}

//  A soft dark halo under a control, and a soft bloom over one. Both are a
//  single feathered fan now the colour is per vertex, where before they were
//  not possible at all.
void drawHalo(float cx, float cy, float r, Col c, float spread) {
    const int n = segs(r);
    Col clear = c; clear.a = 0.0f;
    begin();
    vtx(cx, cy, c);
    for (int i = 0; i <= n; ++i) {
        const float t = (float)i / (float)n * 6.2831853f;
        vtx(cx + cosf(t) * r * spread, cy + sinf(t) * r * spread, clear);
    }
    flush(GL_TRIANGLE_FAN);
}

// ------------------------------------------------------------------ layout
void layout() {
    const float shortEdge = (float)(g_width < g_height ? g_width : g_height);
    g_unit = shortEdge * 0.14f;             // the layout module

    //  Keep everything off the bottom edge of the screen.
    //
    //  On a phone or tablet the last strip of the display belongs to the system:
    //  the gesture handle sits there even in immersive mode, and the few dozen
    //  pixels above it are a system gesture inset, so touches there are taken
    //  for back/home before the app ever sees them. Controls that reach into it
    //  simply do not respond.
    const float bottomSafe = (float)g_height * 0.04f;

    //  The stick is drawn smaller than the area it governs. The ring is only a
    //  hint of where the thumb rests - it re-centres under the finger anyway -
    //  so a big one just covers the world without steering any better.
    g_stick.radius = g_unit * 0.72f;
    g_stick.centre.x = g_unit * 1.35f;
    g_stick.centre.y = (float)g_height - g_unit * 1.35f - bottomSafe;
    g_stick.origin = g_stick.centre;
    g_stick.knob = g_stick.centre;

    //  The attack button mirrors the stick: same height, same inset from its
    //  own edge, so both thumbs rest level with each other. The skill arc the
    //  client lays out around it is derived from exactly these numbers.
    //
    //  Pulled in from the edge by a further third of a module. The arrows and
    //  the mode toggles stack OUTBOARD of the attack button, so measuring the
    //  inset from the screen edge to the attack button alone put the button
    //  right of the middle of the cluster it belongs to - it read as shoved
    //  into the corner. This centres it against the whole group.
    //  1.45, and the ceiling is 1.48 - set by the skill arc, not by taste.
    //
    //  The client hangs the skill arc off this button. Measured off a running
    //  build rather than assumed: a slot is 41 logical px, so the outer arc
    //  radius is 232.6 and its rim another 26.7 - the arc reaches 259 px left
    //  of the attack centre. The chat panel is centred at the bottom with its
    //  right edge at x=862, so anything past 1.48 modules puts the outermost
    //  skill slot over the chat.
    //
    //  This is most of the room there is. Moving the button further in means
    //  narrowing the chat panel or tightening the arc, and the arc is already
    //  near its minimum: neighbours need 1.6 slot widths between centres and
    //  the inner radius only just provides it.
    const float attackX = (float)g_width  - g_unit * 1.45f;
    const float attackY = (float)g_height - g_unit * 1.35f - bottomSafe;

    g_buttons[0].centre.x = attackX;
    g_buttons[0].centre.y = attackY;
    g_buttons[0].radius   = g_unit * 0.52f;
    g_buttons[0].slot     = kSlotAttack;

    //  The page arrows and the mode toggles form a column against the right
    //  edge, anchored to the SCREEN rather than to the attack button.
    //
    //  They used to be placed relative to attackX, which meant that pulling
    //  the attack button inboard dragged the whole column in with it - and
    //  the page plate is wider than the arrows are (0.46 of a module against
    //  0.34), so the plate is what reached back and overlapped the attack
    //  ring by 5 device pixels. Anchoring here lets the action buttons move
    //  without the column following them.
    const float arrowR = g_unit * 0.17f;
    const float arrowX = (float)g_width - g_unit * 0.42f;

    g_buttons[1].centre.x = arrowX;
    g_buttons[1].centre.y = attackY - g_unit * 0.40f;
    g_buttons[1].radius   = arrowR;
    g_buttons[1].slot     = kSlotPagePrev;

    g_buttons[2].centre.x = arrowX;
    g_buttons[2].centre.y = attackY + g_unit * 0.40f;
    g_buttons[2].radius   = arrowR;
    g_buttons[2].slot     = kSlotPageNext;

    //  The two mode toggles go above the arrows, up the same edge. They are set
    //  once and then left alone, so being the furthest from the resting thumb
    //  matters least.
    const float modeR = g_unit * 0.22f;

    g_buttons[3].centre.x = arrowX;
    g_buttons[3].centre.y = attackY - g_unit * 1.15f;
    g_buttons[3].radius   = modeR;
    g_buttons[3].slot     = kSlotAuto;

    g_buttons[4].centre.x = arrowX;
    g_buttons[4].centre.y = attackY - g_unit * 1.85f;
    g_buttons[4].radius   = modeR;
    g_buttons[4].slot     = kSlotPK;

    g_buttons[6].centre.x = arrowX;
    g_buttons[6].centre.y = attackY - g_unit * 2.55f;
    g_buttons[6].radius   = modeR;
    g_buttons[6].slot     = kSlotCamLock;

    //  Pick-up sits directly under the attack button, where the thumb already
    //  is - looting is something you do between fights, in the same rhythm.
    g_buttons[5].centre.x = attackX;
    g_buttons[5].centre.y = attackY + g_unit * 0.52f + g_unit * 0.30f;
    g_buttons[5].radius   = modeR;
    g_buttons[5].slot     = kSlotPickup;
}

bool hit(const Vec2 &c, float r, float x, float y) {
    return len(x - c.x, y - c.y) <= r;
}

}   // namespace

// ------------------------------------------------------------------- API
void RanTouch_Init(int w, int h) {
    if (g_inited) return;
    g_width = w; g_height = h;
    for (int i = 0; i < kMaxPointers; ++i) g_touch[i].id = -1;
    if (!buildProgram()) { LOGE("touch UI disabled: no shader"); return; }
    layout();
    g_inited = true;
    LOGI("touch UI ready (%dx%d, unit %.0f)", w, h, g_unit);
}

void RanTouch_Shutdown(void) {
    if (!g_inited) return;
    if (g_vao) glDeleteVertexArrays(1, &g_vao);
    if (g_vbo) glDeleteBuffers(1, &g_vbo);
    if (g_prog) glDeleteProgram(g_prog);
    g_vao = 0; g_vbo = 0; g_prog = 0;
    g_inited = false;
}

void RanTouch_Resize(int w, int h) {
    g_width = w; g_height = h;
    if (g_inited) layout();
}

void RanTouch_SetActive(int active) {
    const bool on = active != 0;
    if (on) g_activeAge = 0;
    if (on == g_active) return;
    g_active = on;
    //  Drop anything held, or a finger that was down when the world unloaded
    //  would leave the character walking forever.
    g_stick.pointer = -1; g_stick.held = false;
    g_stick.dir.x = g_stick.dir.y = 0.0f; g_stick.magnitude = 0.0f;
    for (int i = 0; i < kButtonCount; ++i) { g_buttons[i].pointer = -1; g_buttons[i].down = false; }
    g_pinch.a = g_pinch.b = -1;
}

int RanTouch_IsActive(void) { return g_active ? 1 : 0; }

int RanTouch_PointerDown(int id, float x, float y) {
    if (!g_inited || !g_active) return 0;

    //  A window on top gets the press, not the pad.
    //
    //  The pad is drawn over the world and used to claim any press landing on
    //  one of its buttons whatever else was on screen. The client's windows are
    //  movable and several open into the lower right - the options window does -
    //  so their buttons sit under the pad and every press on them was eaten:
    //  the options could be ticked but never applied, and the window could not
    //  be dragged clear because the drag was eaten too.
    if (RanUI_PointInControl && RanUI_PointInControl((int)x, (int)y)) return 0;

    Touch *t = addTouch(id, x, y);

    //  The stick takes a touch near its home and recentres itself there, rather
    //  than making the thumb hunt for a fixed ring.
    //
    //  "Near" used to mean the whole lower-left quadrant - 45% of the width by
    //  55% of the height - which swallowed touches that had nothing to do with
    //  it, well into the middle of the screen. A pad around the ring is enough
    //  to catch a thumb landing loosely and leaves the rest of the screen alone.
    const float stickReach = g_stick.radius * 1.9f;
    if (g_stick.pointer < 0 &&
        len(x - g_stick.centre.x, y - g_stick.centre.y) < stickReach) {
        g_stick.pointer = id;
        g_stick.held = true;
        g_stick.origin.x = x; g_stick.origin.y = y;
        g_stick.knob = g_stick.origin;
        g_stick.dir.x = g_stick.dir.y = 0.0f;
        g_stick.magnitude = 0.0f;
        if (t) t->claimed = true;
        return 1;
    }

    for (int i = 0; i < kButtonCount; ++i) {
        Button &b = g_buttons[i];
        if (b.pointer < 0 && hit(b.centre, b.radius, x, y)) {
            b.pointer = id;
            b.down = true;
            b.pressedEdge = true;
            if (t) t->claimed = true;
            return 1;
        }
    }

    //  Not ours. Two unclaimed fingers mean a pinch, which we do watch - but we
    //  still let them through, so a two-finger tap on the world behaves.
    if (unclaimedCount() == 2) {
        int ids[2], k = 0;
        for (int i = 0; i < kMaxPointers && k < 2; ++i)
            if (g_touch[i].id >= 0 && !g_touch[i].claimed) ids[k++] = i;
        if (k == 2) {
            g_pinch.a = g_touch[ids[0]].id;
            g_pinch.b = g_touch[ids[1]].id;
            g_pinch.startDist = len(g_touch[ids[0]].x - g_touch[ids[1]].x,
                                    g_touch[ids[0]].y - g_touch[ids[1]].y);
            g_pinch.lastDist = g_pinch.startDist;
        }
    }
    return 0;
}

int RanTouch_PointerMove(int id, float x, float y) {
    if (!g_inited || !g_active) return 0;
    Touch *t = findTouch(id);
    if (t) { t->x = x; t->y = y; }

    if (g_stick.pointer == id) {
        float dx = x - g_stick.origin.x;
        float dy = y - g_stick.origin.y;
        const float d = len(dx, dy);
        const float r = g_stick.radius;
        if (d > 0.0001f) {
            g_stick.magnitude = d > r ? 1.0f : d / r;
            g_stick.dir.x = dx / d;
            g_stick.dir.y = dy / d;
        } else {
            g_stick.magnitude = 0.0f;
            g_stick.dir.x = g_stick.dir.y = 0.0f;
        }
        //  The knob stops at the ring even as the finger keeps going, so the
        //  direction stays readable at the edge.
        const float clamp = d > r ? r / d : 1.0f;
        g_stick.knob.x = g_stick.origin.x + dx * clamp;
        g_stick.knob.y = g_stick.origin.y + dy * clamp;
        return 1;
    }

    for (int i = 0; i < kButtonCount; ++i) {
        if (g_buttons[i].pointer == id) {
            //  Sliding off cancels the hold, the way a button should.
            g_buttons[i].down = hit(g_buttons[i].centre, g_buttons[i].radius * 1.4f, x, y);
            return 1;
        }
    }

    //  Pinch: feed the change in separation to the wheel.
    if (g_pinch.a >= 0 && (id == g_pinch.a || id == g_pinch.b)) {
        Touch *ta = findTouch(g_pinch.a), *tb = findTouch(g_pinch.b);
        if (ta && tb) {
            const float d = len(ta->x - tb->x, ta->y - tb->y);
            const float delta = d - g_pinch.lastDist;
            //  One wheel notch per this many pixels of spread. Chosen so a
            //  comfortable pinch crosses the client's zoom range in one gesture
            //  rather than needing several.
            const float kPixelsPerNotch = 12.0f;
            if (fabsf(delta) >= kPixelsPerNotch) {
                const int notches = (int)(delta / kPixelsPerNotch);
                //  Spreading the fingers zooms in, which is the wheel going up.
                RanInput_PointerWheel(notches * 120);
                g_pinch.lastDist += (float)notches * kPixelsPerNotch;
            }
        }
        return 0;
    }
    return 0;
}

int RanTouch_PointerUp(int id, float x, float y) {
    if (!g_inited) { removeTouch(id); return 0; }
    int claimed = 0;

    if (g_stick.pointer == id) {
        g_stick.pointer = -1;
        g_stick.held = false;
        g_stick.magnitude = 0.0f;
        g_stick.dir.x = g_stick.dir.y = 0.0f;
        g_stick.knob = g_stick.centre;
        g_stick.origin = g_stick.centre;
        claimed = 1;
    }
    for (int i = 0; i < kButtonCount; ++i) {
        if (g_buttons[i].pointer == id) {
            g_buttons[i].pointer = -1;
            g_buttons[i].down = false;
            claimed = 1;
        }
    }
    if (id == g_pinch.a || id == g_pinch.b) g_pinch.a = g_pinch.b = -1;

    removeTouch(id);
    return claimed;
}

void RanTouch_Frame(float) { /* state is edge-driven; nothing to age yet */ }

namespace {
void ageActivity() {
    if (g_activeAge < 1000) ++g_activeAge;
    //  A few frames of grace so a hitch does not blink the controls out.
    if (g_active && g_activeAge > 4) RanTouch_SetActive(0);
}
}

//  The skill icons, cropped to their button.
//
//  A square icon inscribed in a round face always leaves a ring of dead space -
//  that is geometry, not a bug. The way out is to stop inscribing it: draw the
//  icon as a disc whose texture coordinates run radially, which samples the
//  middle of the square and throws the corners away. The icon then fills the
//  button edge to edge.
//
//  The overlay draws it rather than the client, because the client's own quad is
//  square and there is no clipping in this pipeline. The tray hides its icon
//  control and hands the texture over instead.
namespace {

extern "C" int RanGLR_TextureSize(unsigned glTex, int *w, int *h);

struct SkillIcon { unsigned tex; float x, y, r; float u0, v0, u1, v1; float texW, texH; };
SkillIcon g_icons[16];
int       g_iconCount = 0;

GLuint g_texProg = 0;
GLint  uTexViewport = -1, uTexAlpha = -1, uTexTexSize = -1, uTexSharpen = -1;
GLuint g_texVbo = 0, g_texVao = 0;

const char *kTexVS =
    "#version 300 es\n"
    "layout(location=0) in vec2 aPos;\n"
    "layout(location=1) in vec2 aUV;\n"
    "uniform vec2 uViewport;\n"
    "out vec2 vUV;\n"
    "void main(){ vUV = aUV;\n"
    "  gl_Position = vec4((aPos.x/uViewport.x)*2.0-1.0, 1.0-(aPos.y/uViewport.y)*2.0, 0.0, 1.0); }\n";

const char *kTexFS =
    "#version 300 es\n"
    "precision mediump float;\n"
    "uniform sampler2D uTex;\n"
    "uniform float uAlpha;\n"
    "uniform vec2  uTexSize;\n"
    "uniform float uSharpen;\n"
    "in vec2 vUV;\n"
    "out vec4 oColor;\n"
    "void main(){\n"
    //  A skill icon is about 33 texels across and fills a button roughly three
    //  times that on the panel, so nearly every pixel of it is a bilinear blend
    //  between two texels - which is what makes the icons look smeared.
    //
    //  Squeezing the interpolation into about one output pixel keeps the ramp
    //  only where a texel edge genuinely falls between output pixels. Nearest
    //  sampling would be crisp too, but it would give the icons hard stair-steps.
    "  vec2 uv = vUV;\n"
    "  if (uTexSize.x > 1.0 && uSharpen > 1.0) {\n"
    "    vec2 t = vUV * uTexSize;\n"
    "    vec2 i = floor(t) + 0.5;\n"
    "    uv = (i + clamp((t - i) * uSharpen, -0.5, 0.5)) / uTexSize;\n"
    "  }\n"
    "  vec4 c = texture(uTex, uv);\n"
    "  oColor = vec4(c.rgb, c.a * uAlpha);\n"
    "}\n";

GLuint compileTex(GLenum type, const char *src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) { glDeleteShader(sh); return 0; }
    return sh;
}

void ensureTexProg() {
    if (g_texProg) return;
    const GLuint vs = compileTex(GL_VERTEX_SHADER, kTexVS);
    const GLuint fs = compileTex(GL_FRAGMENT_SHADER, kTexFS);
    if (!vs || !fs) return;
    g_texProg = glCreateProgram();
    glAttachShader(g_texProg, vs); glAttachShader(g_texProg, fs);
    glLinkProgram(g_texProg);
    glDeleteShader(vs); glDeleteShader(fs);
    uTexViewport = glGetUniformLocation(g_texProg, "uViewport");
    uTexAlpha    = glGetUniformLocation(g_texProg, "uAlpha");
    uTexTexSize  = glGetUniformLocation(g_texProg, "uTexSize");
    uTexSharpen  = glGetUniformLocation(g_texProg, "uSharpen");

    glGenVertexArrays(1, &g_texVao);
    glBindVertexArray(g_texVao);
    glGenBuffers(1, &g_texVbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_texVbo);
    //  Centre plus a ring of segments, four floats each.
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((42 + 2) * 4 * sizeof(float)), NULL, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (const void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (const void *)(2 * sizeof(float)));
    glBindVertexArray(0);
}

//  A disc of the icon: UVs walk the same circle as the positions, so the sample
//  taken at the rim is the middle of the square's edge and the corners never
//  appear.
void drawIconDisc(const SkillIcon &ic) {
    if (!ic.tex) return;
    //  How many output pixels one texel covers: the disc is 2r across and shows
    //  (u1-u0) of the sheet. Feeding the real ratio rather than a fixed number
    //  keeps the filter correct whatever size the icons are drawn at.
    float sharpen = 1.0f;
    if (ic.texW > 1.0f) {
        const float texels = (ic.u1 - ic.u0) * ic.texW;
        if (texels > 0.5f) sharpen = (2.0f * ic.r) / texels;
        if (sharpen < 1.0f) sharpen = 1.0f;
        if (sharpen > 8.0f) sharpen = 8.0f;
    }
    glUniform2f(uTexTexSize, ic.texW, ic.texH);
    glUniform1f(uTexSharpen, sharpen);
    const int seg = 40;
    float v[(42) * 4];
    int n = 0;
    const float uc = (ic.u0 + ic.u1) * 0.5f, vc = (ic.v0 + ic.v1) * 0.5f;
    //  Sample inside the icon's own border.
    //
    //  Skill icons carry a thin frame baked into the texture. Running the UVs
    //  all the way to the square's edge picks that frame up at the top, bottom
    //  and sides of the disc, which showed as four little bars across the
    //  button. Pulling the sample in leaves the frame outside the crop.
    const float kInset = 0.84f;
    const float uh = (ic.u1 - ic.u0) * 0.5f * kInset, vh = (ic.v1 - ic.v0) * 0.5f * kInset;
    v[n++] = ic.x; v[n++] = ic.y; v[n++] = uc; v[n++] = vc;
    for (int i = 0; i <= seg; ++i) {
        const float t = (float)i / (float)seg * 6.2831853f;
        const float c = cosf(t), si = sinf(t);
        v[n++] = ic.x + c * ic.r; v[n++] = ic.y + si * ic.r;
        v[n++] = uc + c * uh;     v[n++] = vc + si * vh;
    }
    glBindVertexArray(g_texVao);
    glBindBuffer(GL_ARRAY_BUFFER, g_texVbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(n * sizeof(float)), v);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ic.tex);
    glDrawArrays(GL_TRIANGLE_FAN, 0, n / 4);
}

void drawIcons(float w, float h) {
    if (g_iconCount <= 0) return;
    ensureTexProg();
    if (!g_texProg) return;
    glUseProgram(g_texProg);
    glUniform2f(uTexViewport, w, h);
    glUniform1f(uTexAlpha, 1.0f);
    glUniform1i(glGetUniformLocation(g_texProg, "uTex"), 0);
    for (int i = 0; i < g_iconCount; ++i) drawIconDisc(g_icons[i]);
    glBindVertexArray(0);
}

} // namespace

//  Handed over by the tray each time it arranges the arc.
extern "C" void RanTouch_SetSkillIcons(int count, const unsigned *tex,
                                       const float *cx, const float *cy, const float *r,
                                       const float *u0, const float *v0,
                                       const float *u1, const float *v1) {
    if (count < 0) count = 0;
    if (count > 16) count = 16;
    g_iconCount = count;
    for (int i = 0; i < count; ++i) {
        g_icons[i].tex = tex ? tex[i] : 0;
        g_icons[i].x = cx ? cx[i] : 0.0f;
        g_icons[i].y = cy ? cy[i] : 0.0f;
        g_icons[i].r = r  ? r[i]  : 0.0f;
        g_icons[i].u0 = u0 ? u0[i] : 0.0f;
        g_icons[i].v0 = v0 ? v0[i] : 0.0f;
        g_icons[i].u1 = u1 ? u1[i] : 1.0f;
        g_icons[i].v1 = v1 ? v1[i] : 1.0f;
        //  Asked once per hand-over, not per frame.
        int tw = 0, th = 0;
        if (g_icons[i].tex && RanGLR_TextureSize(g_icons[i].tex, &tw, &th)) {
            g_icons[i].texW = (float) tw;
            g_icons[i].texH = (float) th;
        } else {
            g_icons[i].texW = g_icons[i].texH = 0.0f;
        }
    }
}

// ---------------------------------------------------------------- drawing
//
//  The controls are drawn to sit inside the game's own interface rather than on
//  top of it. RAN's windows and buttons are a dark, slightly blue charcoal panel
//  behind a thin bright rim, with a darker line outside the rim separating it
//  from whatever is behind. Every control below is built from exactly that, at
//  whatever radius it happens to be, so the stick, the attack button, the mode
//  toggles, the page arrows and the skill slots read as one set - and as the
//  same set as the MENU button and the window frames beside them.
namespace {

//  Gunmetal.
//
//  The overlay used to be warm cream with a terracotta accent, which was the
//  one pale warm thing on a screen full of the client's own dark steel windows
//  - it read as bolted on. It is now the same gunmetal the client's frames are,
//  lit from the top left, with colour reserved for state: amber when an action
//  is available, cyan when a system is on, crimson for PK. A muted accent on a
//  dark ground is just another grey, so those three are luminous rather than
//  dusty.
const Col kFace   = { 0.290f, 0.337f, 0.376f, 1.0f };   // face centre
const Col kFaceE  = { 0.047f, 0.063f, 0.078f, 1.0f };   // face edge
const Col kBevHi  = { 0.729f, 0.780f, 0.820f, 1.0f };   // lit rim, top left
const Col kBevLo  = { 0.129f, 0.157f, 0.180f, 1.0f };   // shadowed rim
const Col kSteel  = { 0.502f, 0.557f, 0.596f, 1.0f };
const Col kInk    = { 0.949f, 0.969f, 0.984f, 1.0f };
const Col kDark   = { 0.012f, 0.020f, 0.027f, 1.0f };

const Col kAmber  = { 1.000f, 0.714f, 0.153f, 1.0f };
const Col kAmberH = { 1.000f, 0.925f, 0.690f, 1.0f };
const Col kCyan   = { 0.208f, 0.839f, 0.941f, 1.0f };
const Col kCyanH  = { 0.769f, 0.965f, 1.000f, 1.0f };
const Col kCrim   = { 1.000f, 0.243f, 0.345f, 1.0f };

//  The painted glyphs' own colours. These are art, not state - a crimson sword
//  is not a sword - so state lives in the chrome around them and the art only
//  ever dims.
const Col kEdge   = { 0.949f, 0.969f, 0.980f, 1.0f };   // the lit edge of a blade
const Col kBlade  = { 0.725f, 0.780f, 0.824f, 1.0f };
const Col kBladeD = { 0.424f, 0.482f, 0.533f, 1.0f };
const Col kBladeX = { 0.231f, 0.275f, 0.314f, 1.0f };
const Col kGold   = { 0.953f, 0.761f, 0.290f, 1.0f };
const Col kGoldH  = { 1.000f, 0.890f, 0.604f, 1.0f };
const Col kGoldD  = { 0.541f, 0.384f, 0.086f, 1.0f };
const Col kWood   = { 0.663f, 0.447f, 0.235f, 1.0f };
const Col kWoodL  = { 0.784f, 0.565f, 0.337f, 1.0f };
const Col kWoodD  = { 0.431f, 0.275f, 0.133f, 1.0f };
const Col kIron   = { 0.349f, 0.388f, 0.431f, 1.0f };
const Col kIronL  = { 0.541f, 0.588f, 0.635f, 1.0f };
const Col kIronD  = { 0.200f, 0.231f, 0.267f, 1.0f };

const float kRimIn = 0.88f;

//  A bevel: the rim lit from the top left and shadowed at the bottom right.
//  Two arcs, and it is most of what separates a button from a flat circle.
void bevel(float x, float y, float ri, float ro, float a) {
    //  A steel base all the way round, then the light and the shadow faded in
    //  over the top of it. Without the base the two arcs meet at a hard notch.
    drawRing(x, y, ri, ro, kSteel.r, kSteel.g, kSteel.b, 0.55f * a);
    drawArcFade(x, y, ri, ro, 3.1416f * 0.66f, 3.1416f * 1.84f, alpha(kBevHi, 0.95f * a));
    drawArcFade(x, y, ri, ro, 3.1416f * 1.72f, 3.1416f * 2.78f, alpha(kBevLo, 0.95f * a));
}

//  A catchlight along the top outer edge. Two pixels of it, and the control
//  stops looking printed on.
void rimLight(float x, float y, float r, float a) {
    drawArc(x, y, r * 0.985f, r * 1.045f, 3.1416f * 1.04f, 3.1416f * 1.96f, 1.0f, 1.0f, 1.0f, 0.30f * a);
}

//  The gloss: a soft white bloom up and left inside the face. Drawn as a
//  feathered fan offset towards the light, which is the single thing that
//  separates a glass button from a grey circle.
void gloss(float x, float y, float r, float a) {
    drawHalo(x - r * 0.26f, y - r * 0.44f, r * 0.42f, rgba(1.0f, 1.0f, 1.0f, 0.30f * a), 2.0f);
}

//  An additive bloom. The blend func changes for the duration and is put back,
//  because everything after this expects straight alpha.
void bloom(float x, float y, float r, Col c, float strength) {
    if (strength <= 0.0f) return;
    emit();                                     //  queued work is straight alpha
    g_additive = true;
    if (!g_capturing) glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    drawHalo(x, y, r * 0.55f, rgba(c.r, c.g, c.b, strength), 3.2f);
    emit();                                     //  ...and this much is additive
    g_additive = false;
    if (!g_capturing) glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

//  The whole chrome stack: halo, face, inner shadow, bevel, gloss, catchlight.
void chromeDisc(float x, float y, float r, float a, Col centre, Col edge) {
    drawHalo(x, y + r * 0.12f, r * 0.86f, rgba(0.0f, 0.0f, 0.0f, 0.55f), 1.62f);
    discGrad(x, y, r * kRimIn, alpha(centre, a), alpha(edge, a));
    drawRing(x, y, r * 0.76f, r * kRimIn, kDark.r, kDark.g, kDark.b, 0.34f * a);
    bevel(x, y, r * kRimIn, r, a);
    gloss(x, y, r * kRimIn, a);
    rimLight(x, y, r, a);
}

//  A segmented ring - the swing timer around the attack button.
void segRing(float x, float y, float ri, float ro, int n, float frac,
             Col on, Col off, float ao, float af) {
    const float step = 6.2831853f / (float)n, gap = step * 0.20f;
    for (int i = 0; i < n; ++i) {
        const float a0 = -1.5707963f + (float)i * step + gap * 0.5f;
        const float a1 = a0 + step - gap;
        const bool lit = ((float)(i + 1) / (float)n) <= frac + 1e-6f;
        const Col c = lit ? on : off;
        drawArc(x, y, ri, ro, a0, a1, c.r, c.g, c.b, lit ? ao : af);
    }
}

// ------------------------------------------------------- painted glyphs
//
//  Not tinted line drawings. A sword is steel with a lit edge and gold
//  furniture; a chest is wood with iron straps. All three were measured off
//  their references on game-icons.net rather than drawn from memory, which
//  is what fixed them: the sword is -45 degrees with a length-to-width of
//  2.46, and the chest's lid is a flat-topped trapezoid at 36% of the height
//  rather than a dome.

//  Rotate-and-scale a unit-space polygon into place, then fill it.
void artPoly(float ox, float oy, float r, float si, float co,
             const float *uv, int count, Col c, float a) {
    float xy[32];
    if (count > 16) count = 16;
    for (int i = 0; i < count; ++i) {
        const float u = uv[i * 2], v = uv[i * 2 + 1];
        xy[i * 2]     = ox + r * (u * co - v * si);
        xy[i * 2 + 1] = oy + r * (u * si + v * co);
    }
    drawPoly(xy, count, alpha(c, a));
}
void artDisc(float ox, float oy, float r, float si, float co,
             float u, float v, float rr, Col c, float a) {
    const float x = ox + r * (u * co - v * si), y = oy + r * (u * si + v * co);
    const Col k = alpha(c, a);
    discGrad(x, y, r * rr, k, k);
}

//  One sword, blade up before rotation.
void artSword(float ox, float oy, float r, bool detail, float a, float tilt) {
    const float si = sinf(tilt), co = cosf(tilt);
    static const float blade[] = { 0,-1.06f, 0.060f,-0.80f, 0.068f,-0.10f, 0.062f,0.13f,
                                  -0.062f,0.13f, -0.068f,-0.10f, -0.060f,-0.80f };
    static const float lit[]   = { 0,-1.06f, -0.060f,-0.80f, -0.068f,-0.10f, -0.062f,0.13f,
                                  -0.026f,0.13f, -0.028f,-0.76f };
    static const float shade[] = { 0.026f,-0.74f, 0.060f,-0.80f, 0.068f,-0.10f, 0.062f,0.13f,
                                   0.026f,0.13f };
    static const float full[]  = { -0.017f,-0.72f, 0.017f,-0.72f, 0.017f,0.03f, -0.017f,0.03f };
    static const float guard[] = { -0.39f,0.135f, 0.39f,0.135f, 0.39f,0.235f, -0.39f,0.235f };
    static const float guardL[]= { -0.39f,0.135f, -0.16f,0.135f, -0.16f,0.235f, -0.39f,0.235f };
    static const float capL[]  = { -0.44f,0.07f, -0.34f,0.07f, -0.34f,0.30f, -0.44f,0.30f };
    static const float capR[]  = {  0.34f,0.07f,  0.44f,0.07f,  0.44f,0.30f,  0.34f,0.30f };
    static const float grip[]  = { -0.038f,0.28f, 0.038f,0.28f, 0.033f,0.70f, -0.033f,0.70f };
    static const float gripL[] = { -0.038f,0.28f, -0.008f,0.28f, -0.009f,0.70f, -0.033f,0.70f };
    static const float pomA[]  = { -0.105f,0.695f, 0.105f,0.695f, 0.105f,0.775f, -0.105f,0.775f };
    static const float pomB[]  = { -0.042f,0.655f, 0.042f,0.655f, 0.042f,0.865f, -0.042f,0.865f };

    artPoly(ox, oy, r, si, co, blade, 7, kBlade,  a);
    artPoly(ox, oy, r, si, co, lit,   6, kEdge,   a);
    artPoly(ox, oy, r, si, co, shade, 5, kBladeD, a);
    if (detail) artPoly(ox, oy, r, si, co, full, 4, kBladeX, a);
    artPoly(ox, oy, r, si, co, guard,  4, kGold,  a);
    artPoly(ox, oy, r, si, co, guardL, 4, kGoldH, a);
    artPoly(ox, oy, r, si, co, capL,   4, kGold,  a);
    artPoly(ox, oy, r, si, co, capR,   4, kGoldD, a);
    artDisc(ox, oy, r, si, co, 0.0f,   0.185f, 0.105f, kGold, a);
    artDisc(ox, oy, r, si, co, 0.0f,   0.185f, 0.055f, rgba(0.10f,0.07f,0.02f,1.0f), a);
    artDisc(ox, oy, r, si, co, -0.025f,0.165f, 0.030f, kGoldH, a);
    artPoly(ox, oy, r, si, co, grip,  4, kWoodD, a);
    artPoly(ox, oy, r, si, co, gripL, 4, kWood,  a);
    artPoly(ox, oy, r, si, co, pomA,  4, kGold,  a);
    artPoly(ox, oy, r, si, co, pomB,  4, kGold,  a);
    artDisc(ox, oy, r, si, co, -0.022f, 0.715f, 0.030f, kGoldH, a);
}

//  The crossed pair, for PK. Splayed wider than 45 degrees on purpose: at 45
//  the two blades lie on top of each other and the whole thing is an X.
void artCrossed(float ox, float oy, float r, bool detail, float a) {
    for (int k = 0; k < 2; ++k) artSword(ox, oy, r, detail, a, (k ? -0.62f : 0.62f));
}

//  The chest.
void artChest(float ox, float oy, float r, float a, bool chev) {
    const float si = 0.0f, co = 1.0f;
    if (chev) {
        const Col g = alpha(kGold, a), gh = alpha(kGoldH, a);
        drawCapsule(ox - r*0.40f, oy - r*1.42f, ox, oy - r*1.00f, r*0.115f, g);
        drawCapsule(ox + r*0.40f, oy - r*1.42f, ox, oy - r*1.00f, r*0.115f, g);
        drawCapsule(ox - r*0.40f, oy - r*1.42f, ox - r*0.12f, oy - r*1.13f, r*0.055f, gh);
    }
    const float LT = -0.74f, LB = -0.21f, SB = -0.16f, BB = 0.56f, FB = 0.74f;
    const float lid[]  = { -0.88f,LT,  0.88f,LT,  0.95f,LB, -0.95f,LB };
    const float lidL[] = { -0.88f,LT, -0.34f,LT, -0.36f,LB, -0.95f,LB };
    const float lidD[] = {  0.44f,LT,  0.88f,LT,  0.95f,LB,  0.47f,LB };
    const float bod[]  = { -0.95f,SB,  0.95f,SB,  0.95f,BB, -0.95f,BB };
    const float bodL[] = { -0.95f,SB, -0.40f,SB, -0.40f,BB, -0.95f,BB };
    const float bodD[] = {  0.48f,SB,  0.95f,SB,  0.95f,BB,  0.48f,BB };
    const float seam[] = { -0.99f,LB,  0.99f,LB,  0.99f,SB, -0.99f,SB };
    const float feet[] = { -0.95f,BB,  0.95f,BB,  1.02f,FB, -1.02f,FB };
    const float feeL[] = { -0.95f,BB, -0.52f,BB, -0.56f,FB, -1.02f,FB };
    const float lock[] = { -0.17f,-0.34f, 0.17f,-0.34f, 0.17f,0.30f, -0.17f,0.30f };
    const float locL[] = { -0.17f,-0.34f,-0.06f,-0.34f,-0.06f,0.30f, -0.17f,0.30f };
    const float locD[] = {  0.10f,-0.34f, 0.17f,-0.34f, 0.17f,0.30f,  0.10f,0.30f };
    const float slot[] = { -0.032f,-0.09f, 0.032f,-0.09f, 0.022f,0.15f, -0.022f,0.15f };

    artPoly(ox, oy, r, si, co, lid,  4, kWood,  a);
    artPoly(ox, oy, r, si, co, lidL, 4, kWoodL, a);
    artPoly(ox, oy, r, si, co, lidD, 4, kWoodD, a);
    artPoly(ox, oy, r, si, co, bod,  4, kWood,  a);
    artPoly(ox, oy, r, si, co, bodL, 4, kWoodL, a);
    artPoly(ox, oy, r, si, co, bodD, 4, kWoodD, a);
    artPoly(ox, oy, r, si, co, seam, 4, kIronD, a);
    //  Four iron straps, at the reference's own gap positions.
    static const float kStrap[4] = { -0.78f, -0.52f, 0.52f, 0.78f };
    for (int i = 0; i < 4; ++i) {
        const float u = kStrap[i];
        const float bar[] = { u-0.055f,LT, u+0.055f,LT, u+0.055f,BB, u-0.055f,BB };
        const float hi[]  = { u-0.055f,LT, u-0.016f,LT, u-0.016f,BB, u-0.055f,BB };
        artPoly(ox, oy, r, si, co, bar, 4, kIron,  a);
        artPoly(ox, oy, r, si, co, hi,  4, kIronL, a);
    }
    artPoly(ox, oy, r, si, co, feet, 4, kWoodD, a);
    artPoly(ox, oy, r, si, co, feeL, 4, kWood,  a);
    artPoly(ox, oy, r, si, co, lock, 4, kGold,  a);
    artPoly(ox, oy, r, si, co, locL, 4, kGoldH, a);
    artPoly(ox, oy, r, si, co, locD, 4, kGoldD, a);
    artDisc(ox, oy, r, si, co, 0.0f, -0.09f, 0.068f, rgba(0.165f,0.106f,0.020f,1.0f), a);
    artPoly(ox, oy, r, si, co, slot, 4, rgba(0.165f,0.106f,0.020f,1.0f), a);
}

//  The marks that are still marks rather than pictures: the reticle for
//  auto-target and the eye for camera lock.
void glyphMark(const Button &b, float a) {
    const float R = b.radius;
    const Col c = alpha(kInk, a);
    if (b.slot == kSlotPagePrev)
        drawTri(b.centre.x, b.centre.y, R * 0.46f, -1.0f, c.r, c.g, c.b, c.a);
    else if (b.slot == kSlotPageNext)
        drawTri(b.centre.x, b.centre.y, R * 0.46f, +1.0f, c.r, c.g, c.b, c.a);
    else if (b.slot == kSlotAuto) {
        drawRing(b.centre.x, b.centre.y, R * 0.26f, R * 0.34f, c.r, c.g, c.b, c.a);
        const float t = R * 0.09f;
        drawRect(b.centre.x - t*0.5f, b.centre.y - R*0.60f, t, R*0.22f, c.r, c.g, c.b, c.a);
        drawRect(b.centre.x - t*0.5f, b.centre.y + R*0.38f, t, R*0.22f, c.r, c.g, c.b, c.a);
        drawRect(b.centre.x - R*0.60f, b.centre.y - t*0.5f, R*0.22f, t, c.r, c.g, c.b, c.a);
        drawRect(b.centre.x + R*0.38f, b.centre.y - t*0.5f, R*0.22f, t, c.r, c.g, c.b, c.a);
    }
    else if (b.slot == kSlotCamLock) {
        drawRing(b.centre.x, b.centre.y, R * 0.13f, R * 0.21f, c.r, c.g, c.b, c.a);
        drawArc(b.centre.x, b.centre.y + R * 0.30f, R * 0.40f, R * 0.50f,
                3.5343f, 5.8905f, c.r, c.g, c.b, c.a);
        drawArc(b.centre.x, b.centre.y - R * 0.30f, R * 0.40f, R * 0.50f,
                0.3927f, 2.7489f, c.r, c.g, c.b, c.a);
    }
}

} // namespace

//  Which skill page the tray is showing, 1..4. Set from the client, which owns
//  the tab index; 0 means "not known yet" and the plate stays off.
int g_skillPage = 0;

//  The page readout, in the client's own idiom.
//
//  RAN builds every readout the same way: a light bevelled chamfered frame
//  around a sunken near-black well, with the figure sitting in the well. The
//  MENU button and the chat tabs are both that construction, so matching it is
//  what makes this look like part of the game rather than part of the overlay.
//
//  It deliberately does not follow the style of the round controls around it.
//  It is a readout, not a button, and the game already has a house style for
//  readouts.
void drawPageLabel(float cx, float cy, float w, float h) {
    if (g_skillPage < 1) return;

    const float cut = w * 0.055f;
    const float x = cx - w * 0.5f, y = cy - h * 0.5f;

    //  Gunmetal, like everything else on the pad.
    //
    //  This was the client's own cream readout, which was the right call while
    //  the overlay was cream too - beside dark steel buttons it was the one
    //  bright rectangle on the screen.
    drawChamfer(x - 2.0f, y - 2.0f, w + 4.0f, h + 4.0f, cut,
                kDark.r, kDark.g, kDark.b, 0.75f);
    drawChamfer(x, y, w, h, cut, kSteel.r, kSteel.g, kSteel.b, 0.85f);
    drawRamp(x + cut, y + 1.0f, w - cut * 2.0f, h - 2.0f,
             kFace.r, kFace.g, kFace.b, kFaceE.r, kFaceE.g, kFaceE.b, 1.0f);

    //  The sunken well.
    const float wx = x + w * 0.10f, wy = y + h * 0.11f;
    const float ww = w * 0.80f,     wh = h * 0.78f;
    drawChamfer(wx - 1.5f, wy - 1.5f, ww + 3.0f, wh + 3.0f, cut * 0.7f,
                kDark.r, kDark.g, kDark.b, 1.0f);
    drawRamp(wx, wy, ww, wh, 0.055f, 0.070f, 0.082f, 0.020f, 0.027f, 0.033f, 1.0f);

    //  The page, alone.
    //
    //  The "/ 4" went: the total never changes, so it was a constant taking up
    //  a third of the plate to say nothing, and it left the figure itself small.
    //  Which page you are on is the whole content, so it gets the whole well.
    //
    //  Centred on its ink, not on its cell. A seven-segment 1 is only its two
    //  right-hand bars, so centring the cell leaves the figure sitting well
    //  right of the middle of the plate.
    const float dh = wh * 0.68f, dw = dh * 0.62f;
    //  The 1 lives in the cell's right-hand bars, so the cell moves LEFT by half
    //  its width less half a stroke to bring that ink onto the centre line.
    const float bias = (g_skillPage == 1) ? -(dw * 0.5f - dw * 0.11f) : 0.0f;
    //  Amber, because the page you are on is a state, and amber is what state
    //  is drawn in everywhere else on the pad.
    drawDigit(cx - dw * 0.5f + bias, wy + (wh - dh) * 0.5f, dw, dh, g_skillPage,
              kAmber.r, kAmber.g, kAmber.b, 1.0f);
}

//  Everything the cached geometry is built from, in one number.
//
//  Deliberately does NOT include the stick or the recharge wipes: those change
//  every frame while the player is moving or a skill is cooling, and they are
//  drawn live instead. Including them would rebuild the whole overlay on almost
//  every frame, which is the situation this exists to avoid.
static unsigned long long staticSignature() {
    unsigned long long h = 1469598103934665603ULL;   //  FNV-1a
    const unsigned char *p; int n;
    #define MIX(v) do { const unsigned char *q = (const unsigned char *)&(v);                         for (int k = 0; k < (int)sizeof(v); ++k) { h ^= q[k]; h *= 1099511628211ULL; } } while (0)
    MIX(g_width); MIX(g_height); MIX(g_unit);
    MIX(g_skillCircleCount); MIX(g_iconCount);
    for (int i = 0; i < g_skillCircleCount && i < RANTOUCH_MAX_SKILL_CIRCLES; ++i) {
        const SkillCircle &c = g_skillCircles[i];
        MIX(c.x); MIX(c.y); MIX(c.r); MIX(c.filled);   //  c.cool is drawn live
    }
    for (int i = 0; i < kButtonCount; ++i) {
        const Button &b = g_buttons[i];
        MIX(b.centre.x); MIX(b.centre.y); MIX(b.radius);
        MIX(b.down); MIX(b.toggled); MIX(b.slot);
    }
    #undef MIX
    (void)p; (void)n;
    return h;
}

void RanTouch_Render(void) {
    ageActivity();
    if (!g_inited || !g_active || !g_prog) return;

    //  Diagnostic: draw no overlay at all. Re-read once a second so it can be
    //  switched while the game runs.
    //
    //  What this separates is the cost of building this geometry from the cost
    //  of filling it. The section timer cannot tell those apart, and the fix is
    //  different for each: caching the geometry, or drawing less of it.
    {
        static double s_check = 0.0;
        static bool   s_off = false;
        struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
        const double now = (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
        if (now - s_check >= 1.0) {
            s_check = now;
            s_off = (access("/sdcard/ran/nohud", F_OK) == 0);
        }
        if (s_off) return;
    }

    //  Save nothing, restore nothing: tell the renderer afterwards that its
    //  cache is stale and let it re-establish what it needs.
    glUseProgram(g_prog);
    //  Everything below happens inside our own VAO, so nothing here can disturb
    //  the attribute layout of the client's.
    glBindVertexArray(g_vao);
    //  ...but GL_ARRAY_BUFFER is NOT part of VAO state. A VAO remembers which
    //  buffer each attribute reads from; the binding point itself is global.
    //  Binding only the VAO left the glBufferSubData calls below writing into
    //  whichever buffer the client had bound last - corrupting its geometry,
    //  while these draws read whatever was stale in ours. Missing controls and a
    //  render thread pinned at 99% were the same bug.
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUniform2f(uViewport, (float)g_width, (float)g_height);

    //  Stick: the ring sits where the finger went down while held, so the
    //  control follows the thumb instead of the thumb hunting for the control.
    const Vec2 base = g_stick.held ? g_stick.origin : g_stick.centre;
    {
        const float a = g_stick.held ? 1.0f : 0.82f;
        const float R = g_stick.radius;

        //  The well is a hint, not a hole. It used to be a near-opaque black
        //  disc that covered the world under the thumb; at 30% the ring still
        //  reads and the ground shows through.
        drawHalo(base.x, base.y + R * 0.12f, R * 0.86f, rgba(0.0f, 0.0f, 0.0f, 0.42f), 1.55f);
        drawFan(base.x, base.y, R * 0.92f, kFaceE.r, kFaceE.g, kFaceE.b, 0.30f * a);

        //  Eight ticks, which is what gives the ring a sense of direction even
        //  before the thumb moves.
        for (int i = 0; i < 8; ++i) {
            const float t = -1.5707963f + (float)i * 6.2831853f / 8.0f;
            const float c = cosf(t), si = sinf(t);
            drawCapsule(base.x + c * R * 0.62f, base.y + si * R * 0.62f,
                        base.x + c * R * 0.74f, base.y + si * R * 0.74f,
                        R * 0.028f, alpha(kSteel, 0.55f * a));
        }
        drawRing(base.x, base.y, R * 0.90f, R * 0.93f, kDark.r, kDark.g, kDark.b, 0.40f * a);
        bevel(base.x, base.y, R * 0.92f, R, a);
        rimLight(base.x, base.y, R, a);

        //  A heading wedge on the rim, which the old stick gave no sign of at
        //  all, and the whole rim goes amber at full deflection - which is how
        //  you see you are running without having to look for it.
        const float mag = g_stick.magnitude;
        if (g_stick.held && mag > 0.02f) {
            const float ang = atan2f(g_stick.dir.y, g_stick.dir.x);
            if (mag > 0.94f) {
                drawRing(base.x, base.y, R * 0.92f, R, kAmber.r, kAmber.g, kAmber.b, 0.95f);
                bloom(base.x, base.y, R, kAmber, 0.24f);
            } else {
                drawArc(base.x, base.y, R * 0.92f, R, ang - 0.26f, ang + 0.26f,
                        kAmber.r, kAmber.g, kAmber.b, 0.40f + 0.55f * mag);
            }
        }

        //  The knob: the solid part, the thing the thumb is actually holding.
        const float kr = R * 0.42f;
        if (g_stick.held) bloom(g_stick.knob.x, g_stick.knob.y, kr, kAmber, 0.20f);
        chromeDisc(g_stick.knob.x, g_stick.knob.y, kr, 1.0f,
                   g_stick.held ? rgba(0.659f, 0.486f, 0.227f, 1.0f) : kFace,
                   g_stick.held ? rgba(0.204f, 0.102f, 0.016f, 1.0f) : kFaceE);
        if (g_stick.held)
            drawRing(g_stick.knob.x, g_stick.knob.y, kr * kRimIn, kr,
                     kAmberH.r, kAmberH.g, kAmberH.b, 0.90f);
    }

    //  The stick moves with the thumb, so it is built live - and it has to be
    //  drawn before the cached half, not queued behind it.
    emit();

    //  ---- the static half: built only when it changes ---------------------
    const unsigned long long sig = staticSignature();
    if (sig != g_sig || g_cacheVerts == 0) {
        g_capturing = true;
        g_bn = 0; g_capFirst = 0; g_segCount = 0; g_additive = false;

    //  The skill rims go down first, so a button that happens to overlap one is
    //  drawn over it rather than under.
    //
    //  Opaque, and wide enough to reach the corners of the square icon beneath:
    //  a 33-wide icon has a half-width of 16.5 but a half-diagonal of 23.3, so a
    //  rim running from the half-width out past the diagonal hides the corners
    //  and leaves a round window. That is what turns a square slot round.
    for (int i = 0; i < g_skillCircleCount; ++i) {
        const SkillCircle &c = g_skillCircles[i];
        //  A ring around the slot, not a mask over it.
        //
        //  Masking worked only while the overlay drew last: it covered the square
        //  frame's corners and left a round window. Drawing under the interface
        //  means the slot is painted afterwards, so a mask would simply be
        //  painted over. This sits entirely outside the square instead - clear of
        //  the half-diagonal at 1.41 - which reads as a round button and does not
        //  depend on draw order at all.
        //  A full face, not a floating rim.
        //
        //  The slot keeps the client's square frame art - clearing its texture
        //  draws an untextured white quad rather than nothing, which is why that
        //  was abandoned. But the overlay is drawn before the interface, so a
        //  machined disc laid down here ends up behind the slot: the icon sits
        //  on our button and the dark square sinks into the dark face instead of
        //  standing against the world.
        //
        //  1.62 of the slot half-width clears the square's half-diagonal (1.41),
        //  so no corner pokes out of the rim.
        //  Just past the square half-diagonal (1.41), so the icon fills the
        //  circle instead of floating in it with a ring of dead space.
        const float fr = c.r * 1.30f;

        //  An empty slot keeps its frame, dimmed.
        //
        //  An earlier pass reduced these to nothing at all, on the grounds that
        //  eight of the ten are usually empty and the arc was cluttered. The
        //  clutter was real; the cure was worse. Stripped bare they read as
        //  holes where buttons should be. At about a third strength the arc
        //  still reads as a row of slots.
        if (!c.filled) {
            discGrad(c.x, c.y, fr * kRimIn, alpha(kFace, 0.34f), alpha(kFaceE, 0.34f));
            bevel(c.x, c.y, fr * kRimIn, fr, 0.34f);
            for (int k = 0; k < 6; ++k) {
                const float t = -1.5707963f + (float)k * 6.2831853f / 6.0f;
                const float co = cosf(t), si = sinf(t);
                drawCapsule(c.x + co * fr * 0.48f, c.y + si * fr * 0.48f,
                            c.x + co * fr * 0.62f, c.y + si * fr * 0.62f,
                            fr * 0.035f, alpha(kSteel, 0.30f));
            }
            continue;
        }

        chromeDisc(c.x, c.y, fr, 1.0f, kFace, kFaceE);
    }

    for (int i = 0; i < kButtonCount; ++i) {
        const Button &b = g_buttons[i];
        //  A press shrinks the button and brightens its rim. It used to change
        //  alpha only, which is invisible against a moving scene.
        const float press = b.down ? 0.94f : 1.0f;
        const float R = b.radius * press;

        if (b.slot == kSlotAttack) {
            //  The hero control. A hot face rather than a neutral one: the
            //  thing the thumb lives on has to look charged.
            const bool dead = !b.toggled && false;      //  reserved: no-target dimming
            const float a = 1.0f;
            bloom(b.centre.x, b.centre.y, R, kAmber, b.down ? 0.42f : 0.20f);
            //  Twelve segments: the swing timer. Full until the client feeds a
            //  fraction in, so it reads as ready rather than as broken.
            segRing(b.centre.x, b.centre.y, R * 1.00f, R * 1.09f, 12, 1.0f,
                    kAmber, kSteel, 0.62f, 0.22f);
            chromeDisc(b.centre.x, b.centre.y, R * 0.94f, a,
                       rgba(0.196f, 0.137f, 0.067f, 1.0f),
                       rgba(0.024f, 0.018f, 0.012f, 1.0f));
            //  The heat is a bloom under the glyph, not a bright fill: a pale
            //  face and a pale blade cancel each other out.
            bloom(b.centre.x, b.centre.y, R * 0.40f, kAmber, 0.10f);
            artSword(b.centre.x, b.centre.y, R * 0.62f, true, dead ? 0.45f : 1.0f, -0.785f);
            drawRing(b.centre.x, b.centre.y, R * 0.80f, R * 0.88f,
                     kAmberH.r, kAmberH.g, kAmberH.b, 0.16f + (b.down ? 0.55f : 0.0f));
            continue;
        }

        //  A mode that is on fills with its own colour and takes a bloom, so
        //  auto-target and PK are readable at the edge of vision instead of
        //  being two near-identical dark discs.
        const bool pk  = (b.slot == kSlotPK);
        const bool loot = (b.slot == kSlotPickup);
        const Col state = pk ? kCrim : kCyan;

        if (b.toggled) {
            bloom(b.centre.x, b.centre.y, R, state, 0.26f);
            chromeDisc(b.centre.x, b.centre.y, R, 1.0f,
                       mixc(state, rgba(0,0,0,1), 0.48f),
                       mixc(state, rgba(0,0,0,1), 0.86f));
            drawRing(b.centre.x, b.centre.y, R * kRimIn, R,
                     state.r, state.g, state.b, 0.95f);
        } else {
            chromeDisc(b.centre.x, b.centre.y, R, b.down ? 1.0f : 0.94f, kFace, kFaceE);
        }

        if (pk)        artCrossed(b.centre.x, b.centre.y, R * 0.60f, false, b.toggled ? 1.0f : 0.80f);
        else if (loot) artChest(b.centre.x, b.centre.y + R * 0.30f, R * 0.46f,
                                b.toggled ? 1.0f : 0.82f, true);
        else           glyphMark(b, b.toggled ? 1.0f : 0.88f);
    }

        emit();                             //  closes the last segment
        g_capturing = false;
        g_cacheVerts = g_bn / kFloatsPerVert;
        glBindBuffer(GL_ARRAY_BUFFER, g_vboCache);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(g_bn * sizeof(float)),
                     g_batch, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
        g_bn = 0;
        g_sig = sig;
        ++g_rebuilds;
    }

    //  ...and drawn from the buffer it was built into. The segment list carries
    //  the blend mode, because that is the only state the vertices cannot.
    if (g_cacheVerts > 0) {
        glBindVertexArray(g_vaoCache);
        for (int i = 0; i < g_segCount; ++i) {
            glBlendFunc(GL_SRC_ALPHA, g_segs[i].add ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
            glDrawArrays(GL_TRIANGLES, g_segs[i].first, g_segs[i].count);
            ++g_batchDraws;
            g_vertsThisFrame += (unsigned)g_segs[i].count;
        }
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBindVertexArray(g_vao);
        glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    }

    //  The icons go on top of their faces.
    emit();                                     //  before the program changes
    drawIcons((float)g_width, (float)g_height);
    //  Back to flat colour for the recharge wipe, which goes over the icon.
    glUseProgram(g_prog);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glUniform2f(uViewport, (float)g_width, (float)g_height);
    for (int i = 0; i < g_skillCircleCount; ++i) {
        const SkillCircle &c = g_skillCircles[i];
        if (!c.filled || c.cool <= 0.0f) continue;
        //  ARGB(150,0,0,0), the colour the client tints its own recharge bar.
        drawDiscBottom(c.x, c.y, c.r * 1.30f * kRimIn, c.cool,
                       0.0f, 0.0f, 0.0f, 150.0f / 255.0f);
    }
    //  Back to the flat-colour program for anything after this.
    glUseProgram(g_prog);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);

    //  The page readout sits in the gap between the two page arrows, so nothing
    //  else has to move to make room for it.
    {
        const Button &bUp = g_buttons[1], &bDn = g_buttons[2];
        const float gap = (bDn.centre.y - bUp.centre.y) - (bUp.radius + bDn.radius);
        if (gap > 8.0f)
            drawPageLabel((bUp.centre.x + bDn.centre.x) * 0.5f,
                          (bUp.centre.y + bDn.centre.y) * 0.5f,
                          g_unit * 0.46f, gap - 6.0f);
    }

    emit();

    //  What the batching actually bought, once a second.
    {
        static double s_last = 0.0;
        static unsigned s_frames = 0, s_draws = 0; static double s_verts = 0.0;
        struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
        const double now = (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
        ++s_frames; s_draws += g_batchDraws; s_verts += (double)g_vertsThisFrame;
        if (s_last == 0.0) s_last = now;
        else if (now - s_last >= 1.0) {
            LOGI("touch-hud: %.1f draws/frame, %.0f verts/frame, %u rebuilds/s (cache %d verts)",
                 (double)s_draws / (double)s_frames, s_verts / (double)s_frames,
                 g_rebuilds, g_cacheVerts);
            g_rebuilds = 0;
            s_last = now; s_frames = 0; s_draws = 0; s_verts = 0.0;
        }
        g_batchDraws = 0; g_vertsThisFrame = 0;
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);

    //  Leave the pipeline in the state the renderer's cache is about to claim.
    //
    //  RanGLR_InvalidateStateCache memsets that cache to zero, which asserts
    //  "depth off, blend off, cull off, no depth writes". Simply invalidating
    //  after enabling blending here left the cache saying blending was off while
    //  it was actually on - so the next setBlend(false) compared 0 with 0, took
    //  the shortcut, and never issued the glDisable. Opaque geometry kept
    //  blending, which is what showed up as the world overlapping itself.
    //
    //  Invalidation only says "I do not know what the GL state is". It cannot
    //  discover it - so the GL state has to be put where the cache expects.
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    //  The renderer caches the bound VAO, the program and the blend state, and
    //  all three have just changed behind its back.
    RanGLR_InvalidateStateCache();
}

// ------------------------------------------------- what the game side reads
extern "C" int RanTouch_GetStick(float *outX, float *outY, float *outMag) {
    if (!g_inited || !g_active || !g_stick.held || g_stick.magnitude <= 0.12f) return 0;
    if (outX)   *outX = g_stick.dir.x;
    if (outY)   *outY = g_stick.dir.y;
    if (outMag) *outMag = g_stick.magnitude;
    return 1;
}

extern "C" void RanTouch_SetToggle(int slot, int on) {
    for (int i = 0; i < kButtonCount; ++i)
        if (g_buttons[i].slot == slot) g_buttons[i].toggled = (on != 0);
}

extern "C" void RanTouch_SetSkillCircles(int count, const float *cx,
                                         const float *cy, const float *r,
                                         const int *filled, const float *cool) {
    if (count < 0) count = 0;
    if (count > RANTOUCH_MAX_SKILL_CIRCLES) count = RANTOUCH_MAX_SKILL_CIRCLES;
    const float w = (g_width  > 0) ? (float)g_width  : 1.0f;
    const float h = (g_height > 0) ? (float)g_height : 1.0f;
    for (int i = 0; i < count; ++i) {
        g_skillCircles[i].x = cx[i] * w;
        g_skillCircles[i].y = cy[i] * h;
        g_skillCircles[i].r = r[i]  * h;
        //  No flags at all means every slot is filled, which is how this
        //  behaved before the flag existed.
        g_skillCircles[i].filled = filled ? (filled[i] != 0) : true;
        g_skillCircles[i].cool = cool ? cool[i] : 0.0f;
    }
    g_skillCircleCount = count;
}

extern "C" void RanTouch_GetAttackCircle(float *cx, float *cy, float *r) {
    //  Fractions of the surface. g_width/g_height are only zero before Init, and
    //  a caller that early would otherwise divide by it.
    const float w = (g_width  > 0) ? (float)g_width  : 1.0f;
    const float h = (g_height > 0) ? (float)g_height : 1.0f;
    if (cx) *cx = g_buttons[0].centre.x / w;
    if (cy) *cy = g_buttons[0].centre.y / h;
    if (r)  *r  = g_buttons[0].radius   / h;
}

//  The tray owns the tab index; the overlay only draws it.
extern "C" void RanTouch_SetSkillPage(int page) {
    g_skillPage = (page >= 1 && page <= 9) ? page : 0;
}

extern "C" int RanTouch_IsPinching(void) {
    return (g_pinch.a >= 0 && g_pinch.b >= 0) ? 1 : 0;
}

extern "C" int RanTouch_ConsumeButton(int *outSlot) {
    if (!g_inited || !g_active) return 0;
    for (int i = 0; i < kButtonCount; ++i) {
        if (g_buttons[i].pressedEdge) {
            g_buttons[i].pressedEdge = false;
            if (outSlot) *outSlot = g_buttons[i].slot;
            return 1;
        }
    }
    return 0;
}
