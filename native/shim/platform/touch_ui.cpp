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

extern "C" void RanGLR_InvalidateStateCache(void);
extern "C" void RanInput_PointerWheel(int dz);

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

//  Scratch for one shape at a time; the buffer is sized to hold it.
float g_verts[256 * 2];
GLint  uViewport = -1, uColor = -1;

const char *kVS =
    "#version 300 es\n"
    "layout(location=0) in vec2 aPos;\n"
    "uniform vec2 uViewport;\n"
    "void main() {\n"
    //  Surface pixels, y down, to clip space.
    "    vec2 p = vec2(aPos.x / uViewport.x, 1.0 - aPos.y / uViewport.y);\n"
    "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

const char *kFS =
    "#version 300 es\n"
    "precision mediump float;\n"
    "uniform vec4 uColor;\n"
    "out vec4 oColor;\n"
    "void main() { oColor = uColor; }\n";

GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        LOGE("touch shader: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bool buildProgram() {
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
    uColor    = glGetUniformLocation(g_prog, "uColor");
    glGenBuffers(1, &g_vbo);

    //  A VAO of our own, and this is not optional.
    //
    //  glVertexAttribPointer records into whichever VAO is bound. Without one of
    //  these the overlay was writing attribute 0 of whatever the client had
    //  bound, pointing that VAO at this two-float buffer. The renderer caches
    //  which VAOs it has already described and rebinds them without describing
    //  them again, so it then drew geometry out of here - long white streaks
    //  across the scene, with the driver allocating GPU memory inside every draw
    //  call trying to service it. That surfaced as an ANR: a stalled render loop
    //  stops input being consumed, and the system kills the app for it.
    //
    //  It was harmless only while the overlay drew last, after all client
    //  drawing. Moving it under the interface is what armed it.
    glGenVertexArrays(1, &g_vao);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    //  Sized once and refilled with glBufferSubData rather than orphaned per
    //  shape: forty glBufferData calls a frame is forty allocations, and this
    //  driver charges real time for them.
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(g_verts), NULL, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (const void *)0);
    glBindVertexArray(0);

    return true;
}

//  A filled circle as a fan, and a ring as a triangle strip. Both are rebuilt
//  per draw: a handful of controls a frame is nothing next to the scene.

void drawFan(float cx, float cy, float r, float r_, float g_, float b_, float a_) {
    //  Segment count follows the radius. A fixed 40-gon is smooth on a page
    //  arrow and visibly faceted on the attack button, which is five times the
    //  size - the flat sides are what read as "pixelated".
    const int seg = r > 80.0f ? 96 : (r > 40.0f ? 64 : 40);
    int n = 0;
    g_verts[n++] = cx; g_verts[n++] = cy;
    for (int i = 0; i <= seg; ++i) {
        const float t = (float)i / (float)seg * 6.2831853f;
        g_verts[n++] = cx + cosf(t) * r;
        g_verts[n++] = cy + sinf(t) * r;
    }
    glUniform4f(uColor, r_, g_, b_, a_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(n * sizeof(float)), g_verts);
    glDrawArrays(GL_TRIANGLE_FAN, 0, n / 2);
}

//  The bottom `frac` of a disc, filled flat.
//
//  This is the client's recharge bar shaped to a round button: the engine draws
//  a vertical progress bar over the square icon, dark, covering the fraction of
//  the delay still to run. Rows of a triangle strip, each as wide as the circle
//  is at that height, give the same wipe with a curved edge instead of a square
//  one - and being rows rather than a rotating sweep, it reads the same way as
//  the original at a glance.
void drawDiscBottom(float cx, float cy, float r, float frac,
                    float r_, float g_, float b_, float a_) {
    if (frac <= 0.0f || r <= 0.0f) return;
    if (frac > 1.0f) frac = 1.0f;
    const float top = cy + r - 2.0f * r * frac;   // the wipe's upper edge
    const int rows = 24;
    int n = 0;
    for (int i = 0; i <= rows; ++i) {
        const float y = top + (cy + r - top) * ((float)i / (float)rows);
        const float dy = y - cy;
        float half = r * r - dy * dy;
        half = half > 0.0f ? sqrtf(half) : 0.0f;
        g_verts[n++] = cx - half; g_verts[n++] = y;
        g_verts[n++] = cx + half; g_verts[n++] = y;
    }
    glUniform4f(uColor, r_, g_, b_, a_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(n * sizeof(float)), g_verts);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, n / 2);
}

void drawRing(float cx, float cy, float rInner, float rOuter,
              float r_, float g_, float b_, float a_) {
    const int seg = rOuter > 80.0f ? 96 : (rOuter > 40.0f ? 64 : 40);
    int n = 0;
    for (int i = 0; i <= seg; ++i) {
        const float t = (float)i / (float)seg * 6.2831853f;
        const float c = cosf(t), s = sinf(t);
        g_verts[n++] = cx + c * rOuter; g_verts[n++] = cy + s * rOuter;
        g_verts[n++] = cx + c * rInner; g_verts[n++] = cy + s * rInner;
    }
    glUniform4f(uColor, r_, g_, b_, a_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(n * sizeof(float)), g_verts);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, n / 2);
}

//  A solid triangle for the page arrows. `dir` is -1 for up, +1 for down.
//  An arbitrary quad, wound as a fan. The slash in "/ 4" is a rotated bar and
//  the segment digits are all rectangles, so everything below is built on this.
void drawQuad4(float x0, float y0, float x1, float y1,
               float x2, float y2, float x3, float y3,
               float r_, float g_, float b_, float a_) {
    int n = 0;
    g_verts[n++] = x0; g_verts[n++] = y0;
    g_verts[n++] = x1; g_verts[n++] = y1;
    g_verts[n++] = x2; g_verts[n++] = y2;
    g_verts[n++] = x3; g_verts[n++] = y3;
    glUniform4f(uColor, r_, g_, b_, a_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(n * sizeof(float)), g_verts);
    glDrawArrays(GL_TRIANGLE_FAN, 0, n / 2);
}

void drawRect(float x, float y, float w, float h,
              float r_, float g_, float b_, float a_) {
    drawQuad4(x, y, x + w, y, x + w, y + h, x, y + h, r_, g_, b_, a_);
}

//  A vertical band gradient, since a draw carries one flat colour.
//
//  RAN's readouts are a light-to-grey vertical ramp; at this size a handful of
//  bands is indistinguishable from a smooth one, and it costs no shader work.
void drawRamp(float x, float y, float w, float h,
              float r0, float g0, float b0, float r1, float g1, float b1, float a_) {
    const int kBands = 7;
    for (int i = 0; i < kBands; ++i) {
        const float t = (float)i / (float)(kBands - 1);
        drawRect(x, y + h * (float)i / kBands, w, h / kBands + 1.0f,
                 r0 + (r1 - r0) * t, g0 + (g1 - g0) * t, b0 + (b1 - b0) * t, a_);
    }
}

//  The client's plate outline: a rectangle with its corners cut.
void drawChamfer(float x, float y, float w, float h, float cut,
                 float r_, float g_, float b_, float a_) {
    int n = 0;
    const float pts[8][2] = {
        { x + cut,     y         }, { x + w - cut, y         },
        { x + w,       y + cut   }, { x + w,       y + h - cut },
        { x + w - cut, y + h     }, { x + cut,     y + h     },
        { x,           y + h - cut }, { x,         y + cut   },
    };
    for (int i = 0; i < 8; ++i) { g_verts[n++] = pts[i][0]; g_verts[n++] = pts[i][1]; }
    glUniform4f(uColor, r_, g_, b_, a_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(n * sizeof(float)), g_verts);
    glDrawArrays(GL_TRIANGLE_FAN, 0, n / 2);
}

//  Seven-segment digits. Only 1..4 are ever shown, but the whole set is here
//  so the page count is not baked into the shape of the code.
void drawDigit(float x, float y, float w, float h, int d,
               float r_, float g_, float b_, float a_) {
    const float t = w * 0.22f;                 // stroke
    const float midY = y + h * 0.5f - t * 0.5f;
    //                      a      b      c      d      e      f      g
    static const int kSeg[10][7] = {
        {1,1,1,1,1,1,0}, {0,1,1,0,0,0,0}, {1,1,0,1,1,0,1}, {1,1,1,1,0,0,1},
        {0,1,1,0,0,1,1}, {1,0,1,1,0,1,1}, {1,0,1,1,1,1,1}, {1,1,1,0,0,0,0},
        {1,1,1,1,1,1,1}, {1,1,1,1,0,1,1},
    };
    if (d < 0 || d > 9) return;
    const int *S = kSeg[d];
    if (S[0]) drawRect(x + t,         y,               w - 2*t, t, r_, g_, b_, a_);
    //	The verticals meet in the middle rather than stopping short of it.
    //
    //	Leaving the classic seven-segment gap there made a 1 - which is only its
    //	two right-hand bars - read as a colon.
    if (S[1]) drawRect(x + w - t,     y + t,           t, h*0.5f - t*0.5f, r_, g_, b_, a_);
    if (S[2]) drawRect(x + w - t,     midY,            t, h*0.5f - t*0.5f, r_, g_, b_, a_);
    if (S[3]) drawRect(x + t,         y + h - t,       w - 2*t, t, r_, g_, b_, a_);
    if (S[4]) drawRect(x,             midY,            t, h*0.5f - t*0.5f, r_, g_, b_, a_);
    if (S[5]) drawRect(x,             y + t,           t, h*0.5f - t*0.5f, r_, g_, b_, a_);
    if (S[6]) drawRect(x + t,         midY,            w - 2*t, t, r_, g_, b_, a_);
}

//  A slice of a ring. The sheen along the top of a machined face is an arc,
//  not a full ring - a ring reads as a second rim.
void drawArc(float cx, float cy, float rInner, float rOuter,
             float a0, float a1,
             float r_, float g_, float b_, float a_) {
    const int seg = 24;
    int n = 0;
    for (int i = 0; i <= seg; ++i) {
        const float t = a0 + (a1 - a0) * (float)i / (float)seg;
        const float c = cosf(t), si = sinf(t);
        g_verts[n++] = cx + c * rOuter; g_verts[n++] = cy + si * rOuter;
        g_verts[n++] = cx + c * rInner; g_verts[n++] = cy + si * rInner;
    }
    glUniform4f(uColor, r_, g_, b_, a_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(n * sizeof(float)), g_verts);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, n / 2);
}

//  A turned metal face: concentric bands, bright near the middle and falling
//  away to the edge.
//
//  The overlay carries one flat colour per draw, so a gradient has to be built
//  out of steps. On a disc the natural direction is radial - a handful of rings
//  is smooth at these sizes, and it gives the face the domed look that separates
//  a machined button from a flat sticker.
void drawTurned(float cx, float cy, float r,
                float rc, float gc, float bc,      // centre
                float re, float ge, float be,      // edge
                float a_) {
    //  A domed face, stepped finely enough that the steps do not show.
    //
    //  The first version used a fixed nine bands regardless of size. On the
    //  attack button that is a twelve pixel step and you see every ring, which
    //  is what made these look wrong. Tying the count to the radius keeps a step
    //  near a pixel whatever the control, so it reads as a gradient again.
    const int bands = (int)(r * 0.35f) < 10 ? 10 : ((int)(r * 0.35f) > 30 ? 30 : (int)(r * 0.35f));
    for (int i = bands; i >= 1; --i) {
        const float t = (float)i / (float)bands;
        const float k = 1.0f - t;
        drawFan(cx, cy, r * t,
                re + (rc - re) * k, ge + (gc - ge) * k, be + (bc - be) * k, a_);
    }
}

void drawTri(float cx, float cy, float r, float dir,
             float r_, float g_, float b_, float a_) {
    int n = 0;
    g_verts[n++] = cx;            g_verts[n++] = cy + r * dir;
    g_verts[n++] = cx - r * 0.9f; g_verts[n++] = cy - r * dir * 0.7f;
    g_verts[n++] = cx + r * 0.9f; g_verts[n++] = cy - r * dir * 0.7f;
    glUniform4f(uColor, r_, g_, b_, a_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(n * sizeof(float)), g_verts);
    glDrawArrays(GL_TRIANGLES, 0, 3);
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
    const float attackX = (float)g_width  - g_unit * 1.35f;
    const float attackY = (float)g_height - g_unit * 1.35f - bottomSafe;

    g_buttons[0].centre.x = attackX;
    g_buttons[0].centre.y = attackY;
    g_buttons[0].radius   = g_unit * 0.52f;
    g_buttons[0].slot     = kSlotAttack;

    //  The page arrows sit outboard of the attack button, between it and the
    //  screen edge. Up pages back, down pages forward, and they stack so the
    //  pair reads as one control.
    const float arrowR = g_unit * 0.17f;
    const float arrowX = attackX + g_unit * 0.52f + g_unit * 0.08f + arrowR;

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

//  Warm paper rather than dark glass: a cream surface, a soft warm-grey rim, a
//  single terracotta accent reserved for the one control that matters, and dark
//  warm ink for the marks. Low contrast between neighbours, high contrast only
//  where something is asking to be pressed.
//  The dark half of the palette, not the light one. Cream surfaces are right on
//  a page but not over arbitrary game footage - on pale pavement they washed out
//  completely. The warm near-black reads against both a bright street and a dark
//  interior, and cream becomes the rim and the ink instead, which is the same
//  pairing the other way round.
const float kFillR = 0.149f, kFillG = 0.145f, kFillB = 0.141f, kFillA = 0.66f; // warm black
const float kRimR  = 0.925f, kRimG  = 0.910f, kRimB  = 0.878f;                 // cream rim
const float kEdgeR = 0.086f, kEdgeG = 0.078f, kEdgeB = 0.071f;                 // warm shadow
const float kInkR  = 0.941f, kInkG  = 0.929f, kInkB  = 0.902f;                 // cream ink

//  The accent. One hue, used for the primary action and for a mode that is on -
//  never for decoration, so its presence always means something.
const float kAccR  = 0.851f, kAccG  = 0.467f, kAccB  = 0.341f;                 // terracotta
const float kAccDR = 0.667f, kAccDG = 0.318f, kAccDB = 0.208f;                 // deep clay
const float kCoolR = 0.361f, kCoolG = 0.478f, kCoolB = 0.600f;                 // muted slate

//  The rim occupies the outer tenth of the radius; kRimIn is where the fill
//  stops so the two do not blend into each other at low alpha.
const float kRimIn = 0.90f;

//  Forged: a face that is lighter towards the middle and falls to near black at
//  the rim, with a bright arc along the top edge where the light would catch a
//  turned surface.
const float kFaceCR = 0.227f, kFaceCG = 0.208f, kFaceCB = 0.176f;   // centre
const float kFaceER = 0.063f, kFaceEG = 0.059f, kFaceEB = 0.051f;   // edge
//  No sheen: it belonged to the domed face and reads as a stray bright band
//  on a flat one. Kept at zero rather than removed so the call sites still
//  document where the light would fall if the faces ever gain depth again.
const float kSheen  = 0.14f;

//  A rim at radius r. The outer line is a warm shadow at low alpha rather than
//  a hard black edge - enough to lift the control off a bright scene without
//  drawing a line around it.
void frame(float x, float y, float r, float a) {
    drawRing(x, y, r * kRimIn, r,          kRimR,  kRimG,  kRimB,  0.90f * a);
    drawRing(x, y, r,          r * 1.045f, kEdgeR, kEdgeG, kEdgeB, 0.30f * a);
    //  There is no multisampling here, so the rim ends in a hard staircase.
    //  A pair of fading rings just outside it softens that edge for the cost of
    //  two more draws.
    drawRing(x, y, r * 1.045f, r * 1.075f, kEdgeR, kEdgeG, kEdgeB, 0.16f * a);
    drawRing(x, y, r * 1.075f, r * 1.105f, kEdgeR, kEdgeG, kEdgeB, 0.07f * a);
}

//  Same rim, drawn in the accent - for the primary control and for a lit mode.
void frameAcc(float x, float y, float r, float a, float cr, float cg, float cb) {
    drawRing(x, y, r * kRimIn, r,          cr,     cg,     cb,     0.95f * a);
    drawRing(x, y, r,          r * 1.045f, kEdgeR, kEdgeG, kEdgeB, 0.30f * a);
}

//  What goes inside a button. Kept apart from the frame so the lit and unlit
//  paths cannot drift into drawing different marks.
//  Cut into the face rather than printed on it: the mark is drawn once in
//  near-black a shade below where it belongs, then again in cream on top, so the
//  edge catches light the way an engraving would.
void glyphInk(const Button &b, float a, float dy, float r_, float g_, float b_);

void glyph(const Button &b, float a) {
    glyphInk(b, a * 0.75f, b.radius * 0.055f, 0.04f, 0.035f, 0.03f);
    glyphInk(b, a, 0.0f, kInkR, kInkG, kInkB);
}

void glyphInk(const Button &b, float a, float dy, float ir, float ig, float ib) {
    //  Screen y grows downward, so "up" is the negative direction.
    if (b.slot == kSlotPagePrev)
        drawTri(b.centre.x, b.centre.y + dy, b.radius * 0.46f, -1.0f, ir, ig, ib, a);
    else if (b.slot == kSlotPageNext)
        drawTri(b.centre.x, b.centre.y + dy, b.radius * 0.46f, +1.0f, ir, ig, ib, a);
    else if (b.slot == kSlotPickup) {
        //  An arrow into a line: down onto the ground, which is what the button
        //  does. Distinct from the page arrows, which have no line.
        //
        //  Sized off the rim rather than guessed: the head is a third of the
        //  radius and the whole mark sits inside 0.62r, so nothing crosses the
        //  frame. The first version used a ring for the ground line and a head
        //  half the button wide, which spilled over the edge.
        const float stem = b.radius * 0.09f;
        drawRect(b.centre.x - stem * 0.5f, b.centre.y + dy - b.radius * 0.46f,
                 stem, b.radius * 0.44f, ir, ig, ib, a);
        drawTri(b.centre.x, b.centre.y + dy + b.radius * 0.02f, b.radius * 0.30f,
                +1.0f, ir, ig, ib, a);
        drawRect(b.centre.x - b.radius * 0.42f, b.centre.y + dy + b.radius * 0.42f,
                 b.radius * 0.84f, stem, ir, ig, ib, a);
    }
    else if (b.slot == kSlotAuto) {
        //  A reticle: what auto-target does is pick something out.
        drawRing(b.centre.x, b.centre.y + dy, b.radius * 0.26f, b.radius * 0.34f, ir, ig, ib, a);
        const float t = b.radius * 0.09f;
        drawRect(b.centre.x - t*0.5f, b.centre.y + dy - b.radius*0.60f, t, b.radius*0.22f, ir, ig, ib, a);
        drawRect(b.centre.x - t*0.5f, b.centre.y + dy + b.radius*0.38f, t, b.radius*0.22f, ir, ig, ib, a);
        drawRect(b.centre.x - b.radius*0.60f, b.centre.y + dy - t*0.5f, b.radius*0.22f, t, ir, ig, ib, a);
        drawRect(b.centre.x + b.radius*0.38f, b.centre.y + dy - t*0.5f, b.radius*0.22f, t, ir, ig, ib, a);
    }
    else if (b.slot == kSlotPK) {
        //  Crossed blades.
        const float d = b.radius * 0.42f, t = b.radius * 0.11f;
        drawQuad4(b.centre.x - d - t, b.centre.y + dy - d + t,
                  b.centre.x - d + t, b.centre.y + dy - d - t,
                  b.centre.x + d + t, b.centre.y + dy + d - t,
                  b.centre.x + d - t, b.centre.y + dy + d + t, ir, ig, ib, a);
        drawQuad4(b.centre.x + d - t, b.centre.y + dy - d - t,
                  b.centre.x + d + t, b.centre.y + dy - d + t,
                  b.centre.x - d + t, b.centre.y + dy + d + t,
                  b.centre.x - d - t, b.centre.y + dy + d - t, ir, ig, ib, a);
    }
    else if (b.slot == kSlotCamLock) {
        //  An eye: what the camera is doing, held.
        drawRing(b.centre.x, b.centre.y + dy, b.radius * 0.13f, b.radius * 0.21f, ir, ig, ib, a);
        drawArc(b.centre.x, b.centre.y + dy + b.radius * 0.30f,
                b.radius * 0.40f, b.radius * 0.50f, 3.5343f, 5.8905f, ir, ig, ib, a);
        drawArc(b.centre.x, b.centre.y + dy - b.radius * 0.30f,
                b.radius * 0.40f, b.radius * 0.50f, 0.3927f, 2.7489f, ir, ig, ib, a);
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

    //  Dark outline, then the light-to-grey face on top of it.
    drawChamfer(x - 2.0f, y - 2.0f, w + 4.0f, h + 4.0f, cut, 0.09f, 0.085f, 0.075f, 0.95f);
    drawChamfer(x, y, w, h, cut, 0.72f, 0.71f, 0.68f, 1.0f);
    drawRamp(x + cut, y + 1.0f, w - cut * 2.0f, h - 2.0f,
             0.91f, 0.90f, 0.88f, 0.55f, 0.54f, 0.51f, 1.0f);

    //  The sunken well.
    const float wx = x + w * 0.10f, wy = y + h * 0.11f;
    const float ww = w * 0.80f,     wh = h * 0.78f;
    drawChamfer(wx - 1.5f, wy - 1.5f, ww + 3.0f, wh + 3.0f, cut * 0.7f, 0.03f, 0.028f, 0.024f, 1.0f);
    drawRamp(wx, wy, ww, wh, 0.08f, 0.072f, 0.060f, 0.20f, 0.19f, 0.165f, 1.0f);

    //  The page, large, and the count under it in a quieter grey.
    //
    //  Centred on its ink, not on its cell. A seven-segment 1 is only its two
    //  right-hand bars, so centring the cell leaves the figure sitting well
    //  right of the middle of the plate.
    const float dh = wh * 0.52f, dw = dh * 0.62f;
    //  The 1 lives in the cell's right-hand bars, so the cell moves LEFT by half
    //  its width less half a stroke to bring that ink onto the centre line.
    const float bias = (g_skillPage == 1) ? -(dw * 0.5f - dw * 0.11f) : 0.0f;
    drawDigit(cx - dw * 0.5f + bias, wy + wh * 0.08f, dw, dh, g_skillPage,
              0.95f, 0.93f, 0.88f, 1.0f);

    //  " / 4 " - a leaning bar and a small four.
    const float sh = wh * 0.24f, sw = sh * 0.58f;
    const float sy = wy + wh * 0.70f;
    const float lean = sw * 0.30f, bar = sw * 0.18f;
    const float slashX = cx - sw * 0.80f;
    drawQuad4(slashX + lean,       sy,
              slashX + lean + bar, sy,
              slashX - lean + bar, sy + sh,
              slashX - lean,       sy + sh,
              0.66f, 0.64f, 0.60f, 1.0f);
    drawDigit(cx + sw * 0.10f, sy, sw, sh, 4, 0.66f, 0.64f, 0.60f, 1.0f);
}

void RanTouch_Render(void) {
    ageActivity();
    if (!g_inited || !g_active || !g_prog) return;

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
        const float a = g_stick.held ? 1.0f : 0.68f;

        //  The well is barely there when idle - a hint of where the thumb goes,
        //  not a control competing with the game behind it.
        drawTurned(base.x, base.y, g_stick.radius * kRimIn,
                   kFaceCR, kFaceCG, kFaceCB, kFaceER, kFaceEG, kFaceEB, 0.78f * a);
        frame(base.x, base.y, g_stick.radius, a * 0.90f);

        //  The knob is the solid part: it is what the thumb is holding, so it
        //  is the one that reads at full strength, and it takes the accent
        //  while it is actually being moved.
        if (g_stick.held) {
            drawTurned(g_stick.knob.x, g_stick.knob.y, g_stick.radius * 0.40f,
                       0.94f, 0.60f, 0.45f, kAccDR, kAccDG, kAccDB, 0.96f);
            frameAcc(g_stick.knob.x, g_stick.knob.y, g_stick.radius * 0.42f, 1.0f,
                     kAccDR, kAccDG, kAccDB);
        } else {
            drawTurned(g_stick.knob.x, g_stick.knob.y, g_stick.radius * 0.40f,
                       0.86f, 0.84f, 0.80f, 0.42f, 0.40f, 0.37f, 0.94f);
            frame(g_stick.knob.x, g_stick.knob.y, g_stick.radius * 0.42f, 0.95f);
        }
    }

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

        //  An empty slot is glass.
        //
        //  A solid dark face on a slot with nothing in it is a button offering
        //  something that is not there, and ten of them crowd the screen. Empty
        //  slots keep only their rim, so the game shows through and the arc
        //  reads as the few skills you actually have. They stay droppable - the
        //  client's control is still underneath, this only changes the paint.
        //
        //  Not nothing at all: the key number beside each slot is a separate
        //  control that goes on drawing either way, and with the disc gone those
        //  digits float loose over the world.
        if (!c.filled) {
            drawFan(c.x, c.y, fr * kRimIn, kFaceER, kFaceEG, kFaceEB, 0.16f);
            drawRing(c.x, c.y, fr * kRimIn, fr, kRimR, kRimG, kRimB, 0.38f);
            drawRing(c.x, c.y, fr, fr * 1.055f, kEdgeR, kEdgeG, kEdgeB, 0.20f);
            continue;
        }

        drawTurned(c.x, c.y, fr, kFaceCR, kFaceCG, kFaceCB,
                   kFaceER, kFaceEG, kFaceEB, 0.95f);
        drawRing(c.x, c.y, fr * kRimIn, fr, kRimR, kRimG, kRimB, 0.95f);
        drawRing(c.x, c.y, fr, fr * 1.055f, kEdgeR, kEdgeG, kEdgeB, 0.55f);
    }

    for (int i = 0; i < kButtonCount; ++i) {
        const Button &b = g_buttons[i];
        const float a = b.down ? 1.0f : 0.85f;

        //  A press shrinks the button and brightens its rim.
        //
        //  It used to change alpha only, which is invisible against a moving
        //  scene - so the attack button gave no sign it had been hit. Scaling is
        //  the cue a physical button gives, and it survives any backdrop.
        const float press = b.down ? 0.92f : 1.0f;

        //  Attack is the primary action, and the only control that wears the
        //  accent by default. Everything else is quiet cream until it has
        //  something to say.
        if (b.slot == kSlotAttack) {
            const float ar = b.radius * press;
            //  Pressed, the face darkens as well as shrinking - the accent going
            //  duller is what a struck button looks like.
            if (b.down)
                drawTurned(b.centre.x, b.centre.y, ar * kRimIn,
                           kAccDR, kAccDG, kAccDB, kAccDR * 0.8f, kAccDG * 0.8f, kAccDB * 0.8f, 1.0f);
            else
                drawTurned(b.centre.x, b.centre.y, ar * kRimIn,
                           0.95f, 0.61f, 0.46f, kAccDR, kAccDG, kAccDB, 0.94f);
            //  Cream, like every other rim.
            //
            //  It was drawn in deep clay, which is within a few percent of the
            //  face colour - so the button lost its edge entirely and read as a
            //  blob. The rim is what makes these look like controls.
            frame(b.centre.x, b.centre.y, ar, 1.0f);
            continue;
        }

        //  A mode that is on fills with the accent and takes an accent rim; off,
        //  it is the same cream as its neighbours. PK gets the deep clay, the
        //  one colour that reads as "careful", and camera lock the muted slate,
        //  because it is a view setting rather than a combat one.
        if (b.toggled) {
            const bool pk  = (b.slot == kSlotPK);
            const bool cam = (b.slot == kSlotCamLock);
            const float cr = pk ? kAccDR : (cam ? kCoolR : kAccR);
            const float cg = pk ? kAccDG : (cam ? kCoolG : kAccG);
            const float cb = pk ? kAccDB : (cam ? kCoolB : kAccB);
            drawTurned(b.centre.x, b.centre.y, b.radius * press * kRimIn,
                       cr + 0.10f, cg + 0.10f, cb + 0.10f,
                       cr * 0.80f, cg * 0.80f, cb * 0.80f, 0.94f);
            frameAcc(b.centre.x, b.centre.y, b.radius * press, 1.0f,
                     cr * 0.70f, cg * 0.70f, cb * 0.70f);
            glyph(b, 1.0f);
            continue;
        }

        drawTurned(b.centre.x, b.centre.y, b.radius * press * kRimIn,
                   kFaceCR, kFaceCG, kFaceCB, kFaceER, kFaceEG, kFaceEB,
                   b.down ? 1.0f : 0.92f);
        frame(b.centre.x, b.centre.y, b.radius * press, a);
        glyph(b, a);
    }

    //  The icons go on top of their faces.
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
