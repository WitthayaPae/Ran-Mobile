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
#include "../platform/ran_plat.h"

#include "../gl/gl_platform.h"
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

//  The same question with the world's name plates left out.
//
//  A plate is a control like any other, and in a crowd they cover most of the
//  middle of the screen. Asking the plain question there answers "a control is
//  here" almost everywhere, which is not what this guard is for - it exists so
//  an open WINDOW keeps its press.
extern "C" int RanUI_PointInDragControl(int x, int y) __attribute__((weak));

#define LOGI(...) RanPlat_Log(RANLOG_INFO,  "RanTouch", __VA_ARGS__)
#define LOGE(...) RanPlat_Log(RANLOG_ERROR, "RanTouch", __VA_ARGS__)

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

//  --- player HUD layout ----------------------------------------------------
//
//  Every control the player can rearrange, as an adjustment on top of the
//  layout below: an offset in layout modules (so it keeps its meaning on any
//  panel), a size and an opacity. Zero offset, size 1 and opacity 1 is the
//  layout as designed, which is what reset puts back. The skill arc is its own
//  group: it hangs off the attack button, so moving the attack button moves it,
//  and its own offset moves it relative to the button.
enum { kGrpStick, kGrpAttack, kGrpSkill, kGrpPage, kGrpAuto, kGrpPK,
       kGrpPickup, kGrpCamera, kGrpMenu, kGrpPotion, kGrpCorner, kGrpCount };
//  Defaults live on the members, not in a list beside the array.
//
//  The list had eight rows for an array that had grown to eleven, so the last
//  three groups - menu, potion row, corner icons - started at scale 0 and
//  opacity 0. A zero scale is a button with no radius: the menu button drew
//  nothing and could not be hit, and the potion bezels were drawn at zero
//  alpha. It went unseen because nothing applied those two fields to those
//  groups until the size fix did. Written this way the array cannot rot again
//  when a group is added.
struct HudAdj { float dx = 0.0f, dy = 0.0f, scale = 1.0f, alpha = 1.0f; };
HudAdj g_adj[kGrpCount];
//  Multiplies the alpha of every vertex written, so a control drawn under it
//  comes out at the player's chosen opacity without each shape knowing.
float g_drawAlpha = 1.0f;
//  The attack button's radius before the player's size is applied: the skill
//  arc is sized from this and the arc's own size, not from the button's.
float g_attackBaseR = 0.0f;
//  Where the attack button sits before the player moved it: the skill arc hangs
//  off this, not off the button, so moving the button leaves the slots alone.
float g_attackBaseX = 0.0f, g_attackBaseY = 0.0f;
//  Each skill slot's own offset, in layout modules. The slots are the client's
//  controls - it places them on the arc and then applies these, so a player can
//  put one skill where their thumb wants it without moving the rest.
//  A slot's own place AND its own size.
//
//  The group's scale sizes a whole row at once, which is right for a pair of
//  page arrows and wrong for six skill slots: the one a thumb reaches for
//  wants to be bigger than the five beside it. Size follows the same rule the
//  offset already did - the editor gives it to whatever is selected, and a
//  selected SLOT is a slot, not its group.
struct SlotAdj { float dx = 0.0f, dy = 0.0f, scale = 1.0f; };
SlotAdj g_slotAdj[RANTOUCH_MAX_SKILL_CIRCLES];
//  The potion slots move one at a time too, for the same reason the skill
//  slots do: six buttons in a fixed row is a layout, not an arrangement.
const int kPotMax = 8;
SlotAdj g_potAdj[kPotMax];

//  The quest box and the small party frame, which stay out of the menu grid
//  and so are placed by the client in the top right corner. It hands over the
//  box they occupy each frame so the editor can outline and grab them; the
//  offset goes back the same way the potion row's does.
//  The corner icons are TWO things, not one.
//
//  The quest box and the small party frame sit side by side and the player
//  reaches for them separately, so each carries its own offset and its own
//  size - like a skill slot, not like the page arrows.
const int kCornerMax = 4;
struct CornerBox { float x, y, r; bool has; };
CornerBox g_cornerBox[kCornerMax] = { };
int       g_cornerCount = 0;
SlotAdj   g_cornerAdj[kCornerMax];
SlotAdj   g_cornerBefore[kCornerMax];

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
const int kSlotF1       = RANTOUCH_SLOT_F1;
const int kSlotF2       = RANTOUCH_SLOT_F2;
const int kSlotF3       = RANTOUCH_SLOT_F3;
const int kSlotF4       = RANTOUCH_SLOT_F4;
const int kSlotAuto     = RANTOUCH_SLOT_AUTO;
const int kSlotPK       = RANTOUCH_SLOT_PK;
const int kSlotPickup   = RANTOUCH_SLOT_PICKUP;
const int kSlotCamLock  = RANTOUCH_SLOT_CAMLOCK;
const int kSlotVehicle  = RANTOUCH_SLOT_VEHICLE;
const int kSlotMenu     = RANTOUCH_SLOT_MENU;
const int kSlotChat     = RANTOUCH_SLOT_CHAT;

//  Attack, four skill-page buttons, the auto-target and PK toggles, pick-up,
//  camera lock, the ride button, and the menu.
//
//  F3 and F4 are appended rather than slotted in beside F1 and F2, because the
//  indices of the buttons below them are written out in half a dozen places -
//  the group table, the outline table, the layout. Their ORDER on screen comes
//  from layout(), not from their position here.
const int kButtonCount = 12;
const int kBtnF1 = 1, kBtnF2 = 2, kBtnF3 = 9, kBtnF4 = 10;
//  The chat button, placed by the client like the ride button below it.
const int kBtnChat = 11;

//  The ride button is placed by the client, not by layout(): it lives beside
//  the chat window, which the player drags. Hidden until the client says where.
bool  g_vehShow = false;
float g_vehFracX = 0.0f, g_vehFracY = 0.0f;

//  The chat fold button: 0 off, 1 collapse, 2 the icon that brings it back.
//  Placed by the client too - it rides the chat window's top right corner,
//  and the chat is dragged and resized.
int   g_chatMode = 0;
float g_chatFracX = 0.0f, g_chatFracY = 0.0f, g_chatFracR = 0.0f;
Button g_buttons[kButtonCount];

//  Rims over the client's skill slots, in surface pixels. Filled in by the
//  client each time it lays the arc out.
//  press: seconds since this circle was last pressed, negative when idle.
//
//  These circles are the client's quick-skill slots, not the overlay's
//  buttons, so the overlay does not claim their presses - it only notices
//  them. Without this a skill button was the one control on the pad that
//  did not answer a thumb: the attack button shrinks and brightens, and
//  the slots did nothing at all.
struct SkillCircle { float x, y, r; bool filled; float cool; float press; };
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

//  The joystick while nobody holds it, cached the same way.
//
//  It was built live every frame - ~12,000 vertices, ~290 KB through
//  glBufferSubData - although at rest it depends only on where it sits and how
//  big it is. Measured with ~90 players in view: /sdcard/ran/nohud was worth
//  ~2.3 fps, and the live stick is most of what the static cache leaves.
//  Verified on LDPlayer 2026-09-13, ~95 players in view: 36.9 -> 39.6 fps over
//  six interleaved rounds, touch-hud 2.6-4.1 ms -> 0.5-2.2 ms, stick unchanged
//  by eye. On by default; "nostickcache" builds it live again. A held stick is
//  always built live.
GLuint   g_vboStick = 0;
GLuint   g_vaoStick = 0;
Seg      g_stickSegs[32];
int      g_stickSegCount = 0;
int      g_stickVerts = 0;
unsigned long long g_stickSig = 0;
bool     g_stickCacheOn = true;

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
    //  highp like every overlay shader: mediump is a true 16-bit float on
    //  Apple GPUs, and the same program must look the same on both platforms.
    "precision highp float;\n"
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
    g_stickVerts = 0;
    g_stickSig = 0;
    g_stickSegCount = 0;

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

    //  And one more pair for the resting joystick.
    glGenBuffers(1, &g_vboStick);
    glGenVertexArrays(1, &g_vaoStick);
    glBindVertexArray(g_vaoStick);
    glBindBuffer(GL_ARRAY_BUFFER, g_vboStick);
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
    g_verts[g_n++] = c.r; g_verts[g_n++] = c.g; g_verts[g_n++] = c.b; g_verts[g_n++] = c.a * g_drawAlpha;
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

    //  A one is a single unbroken bar, not two seven-segment bars with the
    //  middle segment's gap between them.
    //
    //  On a real seven-segment display that gap is just how a 1 looks, and at
    //  the size the skill page readout is drawn - a couple of dozen pixels, in
    //  amber, in a dark box - a short bar above a shorter bar reads as an
    //  exclamation mark. It was reported as an alert, which is exactly what it
    //  looked like.
    if (d == 1) {
        drawRect(x + w - t, y, t, h, r_, g_, b_, a_);
        return;
    }

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
void placePageRow();

//  Where the chat button goes.
//
//  Open it is the chat's corner control and the client owns the spot. FOLDED
//  there is no chat to hang it on, so it parks against the ride button - which
//  is the overlay's own, and only the overlay knows where that ended up once
//  the player has moved it. Straight to its right, one gap away, so the two
//  read as a pair on the same line.
void placeChatButton() {
    g_buttons[kBtnChat].slot = kSlotChat;

    if (g_chatMode == 2) {
        const Button &veh = g_buttons[7];
        const float R = g_buttons[kBtnChat].radius;
        if (veh.radius > 0.0f) {
            g_buttons[kBtnChat].centre.x = veh.centre.x + (veh.radius + R + R * 0.30f);
            g_buttons[kBtnChat].centre.y = veh.centre.y;
            return;
        }
    }

    g_buttons[kBtnChat].centre.x = g_chatFracX * (float) g_width;
    g_buttons[kBtnChat].centre.y = g_chatFracY * (float) g_height;
}

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
    //  The page row is placed by placePageRow(), at the end of this function
    //  and again whenever the client moves the skill arc: it hangs off the
    //  first skill slot, which the client owns.
    const float arrowX = (float)g_width - g_unit * 0.42f;

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

    //  The menu, at the top of the same column. It was a plate in the corner
    //  borrowed from the interface art, which looked like what it was - a piece
    //  of another UI sitting on this one. As a round button here it is the same
    //  object as everything else the thumb works, and the HUD editor can move
    //  it like the rest.
    g_buttons[8].centre.x = arrowX;
    g_buttons[8].centre.y = attackY - g_unit * 3.40f;
    g_buttons[8].radius   = modeR;
    g_buttons[8].slot     = kSlotMenu;

    //  Pick-up sits directly under the attack button, where the thumb already
    //  is - looting is something you do between fights, in the same rhythm.
    g_buttons[5].centre.x = attackX;
    g_buttons[5].centre.y = attackY + g_unit * 0.52f + g_unit * 0.30f;
    g_buttons[5].radius   = modeR;
    g_buttons[5].slot     = kSlotPickup;

    //  Same radius as the mode toggles, so it is the same button; only its
    //  centre comes from somewhere else.
    g_buttons[7].radius = modeR;
    g_buttons[7].slot   = kSlotVehicle;
    g_buttons[7].centre.x = g_vehFracX * (float) g_width;
    g_buttons[7].centre.y = g_vehFracY * (float) g_height;

    //  The chat fold button. Open, the client places it on the chat's top right
    //  corner and sizes it, because it is that window's own frame control.
    g_buttons[kBtnChat].radius = ( g_chatMode == 2 || g_chatFracR <= 0.0f )
                               ? modeR
                               : g_chatFracR * (float) g_height;
    placeChatButton();

    //  The player's arrangement, on top of the designed positions.
    const float W = (float)g_width, H = (float)g_height;
    struct Clamp { static void to(Vec2 &c, float r, float W, float H) {
        if (c.x < r) c.x = r; if (c.x > W - r) c.x = W - r;
        if (c.y < r) c.y = r; if (c.y > H - r) c.y = H - r;
    } };

    g_stick.radius   *= g_adj[kGrpStick].scale;
    g_stick.centre.x += g_adj[kGrpStick].dx * g_unit;
    g_stick.centre.y += g_adj[kGrpStick].dy * g_unit;
    Clamp::to(g_stick.centre, g_stick.radius, W, H);
    g_stick.origin = g_stick.centre;
    g_stick.knob   = g_stick.centre;

    g_attackBaseR = g_buttons[0].radius;
    g_attackBaseX = g_buttons[0].centre.x;
    g_attackBaseY = g_buttons[0].centre.y;
    const int single[6][2] = { { 0, kGrpAttack }, { 3, kGrpAuto }, { 4, kGrpPK },
                               { 5, kGrpPickup }, { 6, kGrpCamera }, { 8, kGrpMenu } };
    //  Six entries, six passes.
    //
    //  It ran to five, so the last row - the MENU button - was never given its
    //  offset or its size. In the editor it outlined and selected like the
    //  others and then ignored everything the player did to it.
    for (int k = 0; k < 6; ++k) {
        Button &b = g_buttons[single[k][0]];
        const HudAdj &a = g_adj[single[k][1]];
        b.radius   *= a.scale;
        b.centre.x += a.dx * g_unit;
        b.centre.y += a.dy * g_unit;
        Clamp::to(b.centre, b.radius, W, H);
    }

    placePageRow();
}

//  F1..F4, in a row under the first skill slot.
//
//  Under slot 1 and not up the right edge, because that is where the thing
//  they change is: the row of skills is what a page turn replaces, so the
//  control for it belongs against that row rather than beside the attack
//  button. The arc is the client's, handed over every frame, so this runs
//  again whenever it moves - it cannot live in layout() alone.
//
//  A row and not a column: four buttons reading left to right are read as four
//  places. Stacked, they read as an order to step through.
void placePageRow() {
    const float W = (float)g_width, H = (float)g_height;
    const HudAdj &a = g_adj[kGrpPage];

    const float r    = g_unit * 0.19f * a.scale;
    const float step = r * 2.15f;

    //  Where the row sits before the player moves it: centred under slot 1.
    //  Without an arc - out of the world, or the tray not laid out yet - it
    //  falls back to the bottom right, where it used to be.
    float cx, cy;
    if (g_skillCircleCount > 0) {
        const SkillCircle &c = g_skillCircles[0];
        cx = c.x;
        cy = c.y + c.r * 1.32f + r * 1.15f;
    } else {
        cx = W - g_unit * 0.42f - step * 1.5f;
        cy = H - g_unit * 0.40f;
    }

    cx += a.dx * g_unit;
    cy += a.dy * g_unit;
    //  The row moves as one, so it is clamped as one.
    if (cx < step * 1.5f + r)     cx = step * 1.5f + r;
    if (cx > W - step * 1.5f - r) cx = W - step * 1.5f - r;
    if (cy < r)                   cy = r;
    if (cy > H - r)               cy = H - r;

    const int col[4]  = { kBtnF1, kBtnF2, kBtnF3, kBtnF4 };
    const int slot[4] = { kSlotF1, kSlotF2, kSlotF3, kSlotF4 };
    for (int k = 0; k < 4; ++k) {
        Button &b  = g_buttons[col[k]];
        b.slot     = slot[k];
        b.radius   = r;
        b.centre.x = cx + ((float)k - 1.5f) * step;
        b.centre.y = cy;
    }
}

//  Which group a button belongs to, for its opacity (-1: not player-arranged).
int groupOfButton(int i) {
    switch (i) {
        case 0: return kGrpAttack;
        case kBtnF1: case kBtnF2: case kBtnF3: case kBtnF4: return kGrpPage;
        case 3: return kGrpAuto;
        case 4: return kGrpPK;
        case 5: return kGrpPickup;
        case 6: return kGrpCamera;
        case 8: return kGrpMenu;
        default: return -1;
    }
}

bool hit(const Vec2 &c, float r, float x, float y) {
    return len(x - c.x, y - c.y) <= r;
}

//  --- HUD editor -----------------------------------------------------------
//  The menu WINDOW is not one of the things the editor arranges.
//
//  It used to be: the editor held it open and dragged it for the client, since
//  in edit mode no touch reaches the client and the window could not be
//  dragged by its own title bar. But it is not a control that stays on screen
//  - it opens, a choice is made, it shuts - so arranging it means arranging
//  something that is never there while playing. The menu BUTTON is the thing
//  that sits in the way, and that is a group of its own.
//
bool   g_edit = false;
bool   g_editBar = false;           //  dragging the editor's own toolbar
int    g_editSel = -1;              //  selected group, -1 none
int    g_editSlot = -1;             //  which skill slot, when the group is the arc
int    g_editPtr = -1;              //  the finger dragging it
float  g_editLastX = 0, g_editLastY = 0;
HudAdj g_editBefore[kGrpCount];     //  for cancel
int    g_hudSavedGen = 0;           //  bumped on save; the client polls it
SlotAdj g_slotBefore[RANTOUCH_MAX_SKILL_CIRCLES];
SlotAdj g_potBefore[kPotMax];

//  The toolbar across the top: cancel, reset all, size -, size +, opacity -,
//  opacity +, save. Positions in surface pixels, recomputed from the unit.
enum { kToolCancel, kToolReset, kToolSizeDn, kToolSizeUp, kToolAlphaDn, kToolAlphaUp,
       kToolSave, kToolCount };
//  The toolbar can be moved out of the way.
//
//  It sits across the top, which is where the status bars, the corner icons
//  and now the potion row live - so the one thing the player cannot arrange
//  was covering the things they came to arrange. Dragging the PLATE (anywhere
//  on the bar that is not one of its buttons) moves it; the buttons keep
//  working as taps. In pixels, and cleared with everything else by reset.
float g_toolDX = 0.0f, g_toolDY = 0.0f;

void toolCircle(int t, Vec2 &c, float &r) {
    r = g_unit * 0.26f;
    const float step = g_unit * 0.66f;
    //  cancel, reset | size - [value] + | opacity - [value] + | save
    static const float slot[kToolCount] = { 0.0f, 1.0f, 2.6f, 4.4f, 5.6f, 7.4f, 9.0f };
    const float total = 9.0f * step;
    c.x = (float)g_width * 0.5f - total * 0.5f + slot[t] * step + g_toolDX;
    c.y = g_unit * 0.50f + g_toolDY;
}

//  The plate behind the buttons, which is also its handle.
void toolBar(float *x, float *y, float *w, float *h) {
    Vec2 c0, c1; float r;
    toolCircle(kToolCancel, c0, r);
    toolCircle(kToolSave, c1, r);
    if (x) *x = c0.x - r * 1.5f;
    if (y) *y = c0.y - r * 1.45f;
    if (w) *w = (c1.x - c0.x) + r * 3.0f;
    if (h) *h = r * 2.9f;
}

//  Where a group is, for selecting and outlining it.
//  The potion slots.
//
//  The client's own tray, moved and re-dressed the way the skill tray already
//  is: it owns the slots, their items and what a tap does, and hands over
//  where each one sits so the overlay can draw it round to match. Plain arrays
//  rather than the SkillIcon struct because the editor needs the centres here,
//  well before that struct is declared.
unsigned g_potTex[kPotMax] = { 0 };
float g_potX[kPotMax] = { 0 }, g_potY[kPotMax] = { 0 }, g_potRad[kPotMax] = { 0 };
float g_potU0[kPotMax] = { 0 }, g_potV0[kPotMax] = { 0 };
float g_potU1[kPotMax] = { 0 }, g_potV1[kPotMax] = { 0 };
float g_potTW[kPotMax] = { 0 }, g_potTH[kPotMax] = { 0 };
//  The client's own fade for a slot whose item the bag no longer holds.
float g_potDim[kPotMax] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
int   g_potCount = 0;

bool groupCircle(int g, Vec2 &c, float &r) {
    switch (g) {
        case kGrpStick:  c = g_stick.centre; r = g_stick.radius * 1.25f; return true;
        case kGrpAttack: c = g_buttons[0].centre; r = g_buttons[0].radius * 1.15f; return true;
        case kGrpAuto:   c = g_buttons[3].centre; r = g_buttons[3].radius * 1.35f; return true;
        case kGrpPK:     c = g_buttons[4].centre; r = g_buttons[4].radius * 1.35f; return true;
        case kGrpPickup: c = g_buttons[5].centre; r = g_buttons[5].radius * 1.35f; return true;
        case kGrpCamera: c = g_buttons[6].centre; r = g_buttons[6].radius * 1.35f; return true;
        case kGrpMenu:   c = g_buttons[8].centre; r = g_buttons[8].radius * 1.35f; return true;
        case kGrpPage: {
            c.x = (g_buttons[kBtnF1].centre.x + g_buttons[kBtnF4].centre.x) * 0.5f;
            c.y = g_buttons[kBtnF1].centre.y;
            r = (g_buttons[kBtnF4].centre.x - g_buttons[kBtnF1].centre.x) * 0.5f
              + g_buttons[kBtnF1].radius * 1.3f;
            return true;
        }
        case kGrpPotion: {
            if (g_potCount <= 0) return false;
            float sx = 0, sy = 0;
            for (int i = 0; i < g_potCount; ++i) { sx += g_potX[i]; sy += g_potY[i]; }
            c.x = sx / (float)g_potCount; c.y = sy / (float)g_potCount;
            r = 0.0f;
            for (int i = 0; i < g_potCount; ++i) {
                const float d = hypotf(g_potX[i] - c.x, g_potY[i] - c.y) + g_potRad[i] * 1.45f;
                if (d > r) r = d;
            }
            return true;
        }
        case kGrpCorner: {
            //  Outlined one by one in the editor; this is only their extent.
            if (g_cornerCount <= 0) return false;
            float sx = 0, sy = 0; int n = 0;
            for (int i = 0; i < g_cornerCount; ++i) {
                if (!g_cornerBox[i].has) continue;
                sx += g_cornerBox[i].x; sy += g_cornerBox[i].y; ++n;
            }
            if (n <= 0) return false;
            c.x = sx / (float)n; c.y = sy / (float)n;
            r = 0.0f;
            for (int i = 0; i < g_cornerCount; ++i) {
                if (!g_cornerBox[i].has) continue;
                const float d = len(g_cornerBox[i].x - c.x, g_cornerBox[i].y - c.y)
                              + g_cornerBox[i].r;
                if (d > r) r = d;
            }
            return true;
        }
        case kGrpSkill: {
            //  Outlined per slot in the editor; this is only its extent.
            if (g_skillCircleCount <= 0) return false;
            float sx = 0, sy = 0;
            for (int i = 0; i < g_skillCircleCount; ++i) { sx += g_skillCircles[i].x; sy += g_skillCircles[i].y; }
            c.x = sx / (float)g_skillCircleCount; c.y = sy / (float)g_skillCircleCount;
            r = 0.0f;
            for (int i = 0; i < g_skillCircleCount; ++i) {
                const float d = len(g_skillCircles[i].x - c.x, g_skillCircles[i].y - c.y) + g_skillCircles[i].r * 1.3f;
                if (d > r) r = d;
            }
            return true;
        }
    }
    return false;
}

//  The group under a finger. Small buttons first, so one sitting on top of a
//  bigger group can still be picked; the skill slots before the stick.
int groupAt(float x, float y) {
    static const int order[] = { -2, -3, -4, kGrpPickup, kGrpPage, kGrpAuto, kGrpPK,
                                 kGrpCamera, kGrpMenu, kGrpAttack, kGrpStick };
    g_editSlot = -1;
    for (size_t k = 0; k < sizeof(order) / sizeof(order[0]); ++k) {
        const int g = order[k];
        if (g == -2) {
            //  A slot, not the arc: each one moves on its own.
            for (int i = 0; i < g_skillCircleCount && i < RANTOUCH_MAX_SKILL_CIRCLES; ++i)
                if (len(x - g_skillCircles[i].x, y - g_skillCircles[i].y) <= g_skillCircles[i].r * 1.3f) {
                    g_editSlot = i;
                    return kGrpSkill;
                }
            continue;
        }
        if (g == -3) {
            //  And a potion slot on its own, for the same reason.
            for (int i = 0; i < g_potCount && i < kPotMax; ++i)
                if (len(x - g_potX[i], y - g_potY[i]) <= g_potRad[i] * 1.45f) {
                    g_editSlot = i;
                    return kGrpPotion;
                }
            continue;
        }
        if (g == -4) {
            //  And each corner icon: the quest box and the party frame are
            //  reached for separately, so they are picked separately.
            for (int i = 0; i < g_cornerCount && i < kCornerMax; ++i)
                if (g_cornerBox[i].has &&
                    len(x - g_cornerBox[i].x, y - g_cornerBox[i].y) <= g_cornerBox[i].r) {
                    g_editSlot = i;
                    return kGrpCorner;
                }
            continue;
        }
        Vec2 c; float r;
        if (groupCircle(g, c, r) && hit(c, r, x, y)) return g;
    }
    return -1;
}

void editDefaultsAll() {
    for (int i = 0; i < kGrpCount; ++i) { g_adj[i].dx = g_adj[i].dy = 0; g_adj[i].scale = 1; g_adj[i].alpha = 1; }
    for (int i = 0; i < RANTOUCH_MAX_SKILL_CIRCLES; ++i) {
        g_slotAdj[i].dx = g_slotAdj[i].dy = 0.0f; g_slotAdj[i].scale = 1.0f;
    }
    for (int i = 0; i < kPotMax; ++i) {
        g_potAdj[i].dx = g_potAdj[i].dy = 0.0f; g_potAdj[i].scale = 1.0f;
    }
    //  The corner icons too.
    //
    //  They were added to the editor and not to this, so reset put everything
    //  else back and left the quest box and the party frame wherever they had
    //  been dragged - which reads as "reset does not work".
    for (int i = 0; i < kCornerMax; ++i) {
        g_cornerAdj[i].dx = g_cornerAdj[i].dy = 0.0f; g_cornerAdj[i].scale = 1.0f;
    }
    g_toolDX = g_toolDY = 0.0f;
}

void editEnd() {
    g_edit = false;
    g_editBar = false;
    g_editSel = -1;
    g_editSlot = -1;
    g_editPtr = -1;
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

    //  Editing the HUD: every touch is the editor's. Nothing reaches the game,
    //  so dragging a skill slot moves it instead of casting it.
    if (g_edit) {
        for (int t = 0; t < kToolCount; ++t) {
            Vec2 c; float r;
            toolCircle(t, c, r);
            if (!hit(c, r * 1.2f, x, y)) continue;
            HudAdj *a = (g_editSel >= 0) ? &g_adj[g_editSel] : NULL;

            //  Size goes to whatever is SELECTED, and a slot is a thing on its
            //  own - the same rule dragging has always used. With one scale a
            //  group, growing the skill slot under the thumb grew all six.
            float *pScale = a ? &a->scale : NULL;
            if (g_editSlot >= 0) {
                if (g_editSel == kGrpSkill && g_editSlot < RANTOUCH_MAX_SKILL_CIRCLES)
                    pScale = &g_slotAdj[g_editSlot].scale;
                else if (g_editSel == kGrpPotion && g_editSlot < kPotMax)
                    pScale = &g_potAdj[g_editSlot].scale;
                else if (g_editSel == kGrpCorner && g_editSlot < kCornerMax)
                    pScale = &g_cornerAdj[g_editSlot].scale;
            }
            switch (t) {
                case kToolCancel:
                    memcpy(g_adj, g_editBefore, sizeof(g_adj));
                    memcpy(g_slotAdj, g_slotBefore, sizeof(g_slotAdj));
                    memcpy(g_potAdj,  g_potBefore,  sizeof(g_potAdj));
                    memcpy(g_cornerAdj, g_cornerBefore, sizeof(g_cornerAdj));
                    editEnd();
                    break;
                case kToolReset:  editDefaultsAll(); break;
                case kToolSizeDn: if (pScale) { *pScale -= 0.1f; if (*pScale < 0.6f) *pScale = 0.6f; } break;
                case kToolSizeUp: if (pScale) { *pScale += 0.1f; if (*pScale > 1.6f) *pScale = 1.6f; } break;
                case kToolAlphaDn: if (a) { a->alpha -= 0.1f; if (a->alpha < 0.2f) a->alpha = 0.2f; } break;
                case kToolAlphaUp: if (a) { a->alpha += 0.1f; if (a->alpha > 1.0f) a->alpha = 1.0f; } break;
                case kToolSave:
                    editEnd();
                    ++g_hudSavedGen;
                    break;
            }
            layout();
            return 1;
        }
        //  The plate itself: not a button, so it is the bar's handle.
        {
            float bx, by, bw, bh;
            toolBar(&bx, &by, &bw, &bh);
            if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
                g_editBar = true;
                g_editPtr = id; g_editLastX = x; g_editLastY = y;
                return 1;
            }
        }

        const int g = groupAt(x, y);
        g_editSel = g;
        g_editBar = false;
        if (g >= 0) { g_editPtr = id; g_editLastX = x; g_editLastY = y; }
        return 1;
    }

    //  A window on top gets the press, not the pad.
    //
    //  The pad is drawn over the world and used to claim any press landing on
    //  one of its buttons whatever else was on screen. The client's windows are
    //  movable and several open into the lower right - the options window does -
    //  so their buttons sit under the pad and every press on them was eaten:
    //  the options could be ticked but never applied, and the window could not
    //  be dragged clear because the drag was eaten too.
    //  Name plates do not count. Returning here also skips addTouch, so a
    //  finger that landed on a plate was never recorded at all - and a pinch
    //  needs two recorded fingers. In town, where there is a plate under
    //  almost every pixel, that meant no second finger, no pinch, and no zoom:
    //  the crowd bug that killed camera rotation, in its other half.
    //  Except the chat fold button, which sits ON the chat's own frame.
    //
    //  That is the whole point of it - it is the window's corner control - so
    //  the rule below would hand every press on it to the window underneath and
    //  the chat could never be folded. It is the one button placed over a
    //  window on purpose, so it is the one that answers before the rule.
    if (g_chatMode != 0) {
        Button &bc = g_buttons[kBtnChat];
        const bool onIt = (g_chatMode == 1)
            ? (fabsf(x - bc.centre.x) <= bc.radius * RANTOUCH_CHATBAR_ASPECT &&
               fabsf(y - bc.centre.y) <= bc.radius)
            : hit(bc.centre, bc.radius, x, y);
        if (bc.pointer < 0 && onIt) {
            bc.pointer = id;
            bc.down = true;
            bc.pressedEdge = true;
            Touch *tc = addTouch(id, x, y);
            if (tc) tc->claimed = true;
            return 1;
        }
    }

    {
        const int inControl = RanUI_PointInDragControl
                                ? RanUI_PointInDragControl((int)x, (int)y)
                                : (RanUI_PointInControl ? RanUI_PointInControl((int)x, (int)y) : 0);
        if (inControl) return 0;
    }

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
        if (b.slot == kSlotVehicle && !g_vehShow) continue;
        //  Already answered above, by its own shape, before the window rule.
        if (b.slot == kSlotChat) continue;
        if (b.pointer < 0 && hit(b.centre, b.radius, x, y)) {
            b.pointer = id;
            b.down = true;
            b.pressedEdge = true;
            if (t) t->claimed = true;
            return 1;
        }
    }

    //  Not one of ours, but a skill circle might be under it. Noticed, never
    //  claimed: the press still goes through to the client, which is what
    //  actually runs the skill.
    for (int i = 0; i < g_skillCircleCount; ++i) {
        SkillCircle &c = g_skillCircles[i];
        if (!c.filled) continue;
        if (len(x - c.x, y - c.y) <= c.r) { c.press = 0.0f; break; }
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
    if (g_edit) {
        if (id == g_editPtr && g_editBar) {
            g_toolDX += x - g_editLastX;
            g_toolDY += y - g_editLastY;
            //  Kept where it can be grabbed again.
            float bx, by, bw, bh;
            toolBar(&bx, &by, &bw, &bh);
            if (bx < 0.0f)                       g_toolDX -= bx;
            if (by < 0.0f)                       g_toolDY -= by;
            if (bx + bw > (float)g_width)        g_toolDX -= bx + bw - (float)g_width;
            if (by + bh > (float)g_height)       g_toolDY -= by + bh - (float)g_height;
            g_editLastX = x; g_editLastY = y;
            return 1;
        }
        if (id == g_editPtr && g_editSel >= 0) {
            const float mdx = (x - g_editLastX) / g_unit;
            const float mdy = (y - g_editLastY) / g_unit;
            if (g_editSel == kGrpSkill && g_editSlot >= 0) {
                g_slotAdj[g_editSlot].dx += mdx;
                g_slotAdj[g_editSlot].dy += mdy;
            } else if (g_editSel == kGrpPotion && g_editSlot >= 0) {
                g_potAdj[g_editSlot].dx += mdx;
                g_potAdj[g_editSlot].dy += mdy;
            } else if (g_editSel == kGrpCorner && g_editSlot >= 0) {
                //  One corner icon, not the pair: they are picked separately,
                //  so they have to move separately too - dragging the quest box
                //  was taking the party frame with it.
                g_cornerAdj[g_editSlot].dx += mdx;
                g_cornerAdj[g_editSlot].dy += mdy;
            } else {
                g_adj[g_editSel].dx += mdx;
                g_adj[g_editSel].dy += mdy;
            }
            g_editLastX = x; g_editLastY = y;
            layout();
        }
        return 1;
    }
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
    if (g_edit) {
        if (id == g_editPtr) g_editPtr = -1;
        removeTouch(id);
        return 1;
    }
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

//  How long a skill circle stays lit after a press. Short enough to read as a
//  button answering, long enough to see at 60 fps.
const float kSkillPressTime = 0.20f;

void RanTouch_Frame(float dt) {
    for (int i = 0; i < g_skillCircleCount; ++i) {
        SkillCircle &c = g_skillCircles[i];
        if (c.press < 0.0f) continue;
        c.press += dt;
        if (c.press >= kSkillPressTime) c.press = -1.0f;
    }
}

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
GLint  uTexSampler = -1;

//  A RING of buffers, not one.
//
//  Every painted control - each button cell, each slot bezel, each icon - wrote
//  its handful of vertices into one buffer and drew from it immediately: about
//  45 write-then-draw pairs into the same object every frame. On Apple that
//  write lands on a buffer the GPU is still reading, and the driver stalls
//  until it is free; the stall was measured at ~450us here once before, and it
//  is charged to the draw that follows.
//
//  Measured on an iPhone 15, standing in a field with nine mobs and no other
//  players: 31 fps with the overlay drawn, 60 fps with `nohud` set - the whole
//  frame budget twice over, 12-14 ms, in a HUD that issues five draws.
//
//  Round-robining through 128 buffers means a buffer is not written again for
//  about three frames, by which time the GPU has long finished with it. The
//  attribute pointers are part of VAO state and name the buffer they were set
//  against, so each buffer carries its own VAO.
const int kTexRing = 128;
GLuint g_texVbo[kTexRing] = { 0 }, g_texVao[kTexRing] = { 0 };
int    g_texAt = 0;

//  The next free slot, bound and ready to be written.
int texSlot() {
    const int i = g_texAt;
    g_texAt = (g_texAt + 1) % kTexRing;
    glBindVertexArray(g_texVao[i]);
    glBindBuffer(GL_ARRAY_BUFFER, g_texVbo[i]);
    return i;
}

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
    //  highp, not mediump. The sharpen below works in texel units
    //  (vUV * uTexSize, up to 2048 and more on an icon sheet), and on Apple
    //  GPUs mediump is a real 16-bit float: above 1024 it cannot hold a
    //  fraction of a texel, and above 2048 it steps in 2s. floor(t) and t - i
    //  collapsed, and the iPhone's skill icons came out blocky and uneven.
    //  Adreno and the emulator run mediump at full precision, which is why
    //  Android never showed it.
    "precision highp float;\n"
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

    //  Looked up once. It was asked for by name on every painted control, and
    //  glGetUniformLocation is a driver-side string lookup.
    uTexSampler  = glGetUniformLocation(g_texProg, "uTex");

    glGenVertexArrays(kTexRing, g_texVao);
    glGenBuffers(kTexRing, g_texVbo);
    for (int i = 0; i < kTexRing; ++i) {
        glBindVertexArray(g_texVao[i]);
        glBindBuffer(GL_ARRAY_BUFFER, g_texVbo[i]);
        //  Centre plus a ring of segments, four floats each - the largest
        //  write any of the three painted paths makes.
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((42 + 2) * 4 * sizeof(float)), NULL, GL_STREAM_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (const void *)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (const void *)(2 * sizeof(float)));
    }
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
    texSlot();
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(n * sizeof(float)), v);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ic.tex);
    //  Say what we need sampled, every time.
    //
    //  This is the client's own texture and it carries whatever sampler state
    //  the client last set on it. An item icon has ONE level; the moment
    //  something asks for a mipmapped min filter the texture is incomplete,
    //  and an incomplete texture samples as nothing at all - a black disc,
    //  with no GL error. That is the potion slot going black during a skill or
    //  while being hit and coming back after: combat is when the client is
    //  drawing effects and changing filters. The sheet path sets these for the
    //  same reason; this one never did, and a desktop GL driver - which is
    //  what the emulator runs - is lenient enough to hide it.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glDrawArrays(GL_TRIANGLE_FAN, 0, n / 4);
}

//  How much a pressed slot shrinks. 0.94 is the attack button's figure, so the
//  whole pad answers a thumb with the same movement.
float iconPressScale(const SkillIcon &ic) {
    for (int i = 0; i < g_skillCircleCount; ++i) {
        const SkillCircle &c = g_skillCircles[i];
        if (c.press < 0.0f) continue;
        //  Matched by position: icons and circles are set from the same slot,
        //  but by two calls, so nothing guarantees the indices line up.
        if (len(ic.x - c.x, ic.y - c.y) <= ic.r * 0.5f) {
            //  Down hard, then ease back - a button that is pushed in and
            //  released, rather than one that fades.
            const float t = c.press / kSkillPressTime;
            const float d = (t < 0.35f) ? 1.0f : (1.0f - (t - 0.35f) / 0.65f);
            return 1.0f - 0.06f * d;
        }
    }
    return 1.0f;
}

//  The painted sheet for the controls themselves.
//
//  The buttons used to be drawn from shapes - a chrome disc and a vector mark -
//  which read as machined rather than painted beside the client's own art. The
//  client hands over one texture and the overlay samples a cell out of it, the
//  same way it already does for the skill pictures. If the sheet never arrives
//  the old shapes still draw, so a missing file costs the look and nothing else.
unsigned g_hudTex = 0;
float    g_hudTexW = 0.0f, g_hudTexH = 0.0f;
//  Five across, 22 controls in a 25-cell square sheet. It was four across and
//  exactly full, which is why adding the F-key buttons moved it: the order here
//  IS the order in tools/icon-art/hud-pack.js and the two cannot drift apart.
const int kHudCols = 5;

enum {
    kCellAtk = 0, kCellAtkRing, kCellSkillFrame, kCellAuto,     kCellAutoOn,
    kCellPK,      kCellPKOn,    kCellCamLock,    kCellCamLockOn,kCellPickup,
    kCellVehicle, kCellMenu,    kCellF1,         kCellF2,       kCellF3,
    kCellF4,      kCellF1On,    kCellF2On,       kCellF3On,     kCellF4On,
    kCellStickBase, kCellStickKnob,
    //  The chat pair: the round icon that brings the chat back, and the plate
    //  on its frame that folds it away. The plate is the one cell that is not
    //  round art - see the draw below.
    kCellChat,      kCellChatClose,
};

bool hudSheet() { return g_hudTex != 0 && g_hudTexW > 1.0f; }

//  One cell of the sheet, as a square centred on a point.
void drawHudCell(int cell, float cx, float cy, float half, float alpha) {
    if (!hudSheet() || cell < 0) return;
    ensureTexProg();
    if (!g_texProg) return;

    const float cw = 1.0f / (float)kHudCols;
    const float ch = cw;                       //  the sheet is square, 5 x 5
    const float u0 = (float)(cell % kHudCols) * cw;
    const float v0 = (float)(cell / kHudCols) * ch;

    glUseProgram(g_texProg);
    //  Say what the blend is rather than inheriting it. The pad sets its own
    //  blend for the stick's segments and for the halo fans, and a cell drawn
    //  while one of those is in force comes out invisible with no GL error.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glUniform2f(uTexViewport, (float)g_width, (float)g_height);
    glUniform1f(uTexAlpha, alpha);
    glUniform1i(uTexSampler, 0);
    glUniform2f(uTexTexSize, g_hudTexW, g_hudTexH);
    //  One output pixel per texel is the honest ratio here: the cell is square
    //  and drawn square, unlike the skill discs which crop a rectangle.
    glUniform1f(uTexSharpen, 1.0f);

    const float v[] = {
        cx - half, cy - half, u0,      v0,
        cx + half, cy - half, u0 + cw, v0,
        cx + half, cy + half, u0 + cw, v0 + ch,
        cx - half, cy - half, u0,      v0,
        cx + half, cy + half, u0 + cw, v0 + ch,
        cx - half, cy + half, u0,      v0 + ch,
    };
    texSlot();
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)sizeof(v), v);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_hudTex);
    //  The sheet ships with one level and no mipmaps, and a texture whose min
    //  filter asks for mipmaps it does not have is incomplete - it samples as
    //  nothing at all. Every control drawn from it was invisible, with no GL
    //  error to say why, until these were set.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    //  Back to the flat shader the rest of the overlay draws with.
    glUseProgram(g_prog);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
}

//  Which cell a button uses, and which when it is switched on.
int hudCellFor(int slot, bool on) {
    switch (slot) {
        case kSlotAuto:    return on ? kCellAutoOn    : kCellAuto;
        case kSlotPK:      return on ? kCellPKOn      : kCellPK;
        case kSlotCamLock: return on ? kCellCamLockOn : kCellCamLock;
        case kSlotPickup:  return kCellPickup;
        case kSlotVehicle: return kCellVehicle;
        case kSlotMenu:    return kCellMenu;
        case kSlotChat:    return ( g_chatMode == 1 ) ? kCellChatClose : kCellChat;
        case kSlotF1:      return on ? kCellF1On : kCellF1;
        case kSlotF2:      return on ? kCellF2On : kCellF2;
        case kSlotF3:      return on ? kCellF3On : kCellF3;
        case kSlotF4:      return on ? kCellF4On : kCellF4;
    }
    return -1;
}

//  Where each bezel's window is, as a fraction of the cell's half-width.
//
//  Measured off the art by walking out from the centre of the cell until the
//  alpha comes up - all four ways, because neither frame is centred on its own
//  canvas and neither hole is square:
//
//      skillframe.png   L 0.619  R 0.614  T 0.589  B 0.534
//      stick_base.png   L 0.641  R 0.638  T 0.641  B 0.576
//
//  So each has a half-size (the mean of the opposite pair) and an upward
//  offset (half their difference). Sizing a picture to the SMALLEST of the
//  four - which is what one number did - left it short of the frame on the
//  other three sides: the gap between the frame and the icon on both rows.
const float kFrameWinX  = 0.616f;   //  skillframe.png
const float kFrameWinY  = 0.561f;
const float kFrameWinUp = 0.027f;
const float kSeatWin    = 0.620f;   //  stick_base.png, a circle
const float kSeatUp     = 0.032f;

//  Both bezels are drawn at this much of the slot's half-width.
const float kBezelSize  = 1.62f;

//  The icon as a square, which is what it is.
//
//  drawIconDisc crops it to a circle, which is what the round bezel needed:
//  a square picture in a round face leaves a ring of dead space, and there is
//  no clipping in this pipeline to crop with. A square frame wants the whole
//  picture instead.
void drawIconQuad(const SkillIcon &ic, float hw, float hh) {
    if (!ic.tex) return;
    float sharpen = 1.0f;
    if (ic.texW > 1.0f) {
        const float texels = (ic.u1 - ic.u0) * ic.texW;
        if (texels > 0.5f) sharpen = (2.0f * hw) / texels;
        if (sharpen < 1.0f) sharpen = 1.0f;
        if (sharpen > 8.0f) sharpen = 8.0f;
    }
    glUniform2f(uTexTexSize, ic.texW, ic.texH);
    glUniform1f(uTexSharpen, sharpen);

    //  Inside the icon's own baked border, the same crop the disc used and for
    //  the same reason: the client's icons carry a thin frame in the texture,
    //  and ours is the one that should show. At 0.90 a grey line from that
    //  border still showed down the right and bottom edges of the square.
    const float kInset = 0.84f;
    const float uc = (ic.u0 + ic.u1) * 0.5f, vc = (ic.v0 + ic.v1) * 0.5f;
    const float uh = (ic.u1 - ic.u0) * 0.5f * kInset, vh = (ic.v1 - ic.v0) * 0.5f * kInset;
    const float x0 = ic.x - hw, x1 = ic.x + hw;
    const float y0 = ic.y - hh, y1 = ic.y + hh;
    const float v[] = {
        x0, y0, uc - uh, vc - vh,
        x1, y0, uc + uh, vc - vh,
        x1, y1, uc + uh, vc + vh,
        x0, y0, uc - uh, vc - vh,
        x1, y1, uc + uh, vc + vh,
        x0, y1, uc - uh, vc + vh,
    };
    texSlot();
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)sizeof(v), v);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, ic.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void drawIcons(float w, float h) {
    if (g_iconCount <= 0) return;
    ensureTexProg();
    if (!g_texProg) return;
    glUseProgram(g_texProg);
    glUniform2f(uTexViewport, w, h);
    glUniform1f(uTexAlpha, g_adj[kGrpSkill].alpha);
    glUniform1i(uTexSampler, 0);
    for (int i = 0; i < g_iconCount; ++i) {
        SkillIcon ic = g_icons[i];
        //  Fill the seat's hole, not the icon's authored size.
        //
        //  The client hands the picture over at about four fifths of the slot,
        //  which left a dark ring of bezel showing all round it. The slot the
        //  icon belongs to is found the same way iconPressScale finds it - by
        //  position, since the circles and the icons arrive in two calls and
        //  nothing promises the indices line up.
        if (hudSheet()) {
            for (int k = 0; k < g_skillCircleCount; ++k) {
                const SkillCircle &c = g_skillCircles[k];
                if (len(ic.x - c.x, ic.y - c.y) > ic.r * 0.5f) continue;
                ic.r  = c.r * kBezelSize * kSeatWin;
                ic.y -= c.r * kBezelSize * kSeatUp;
                break;
            }
        }
        ic.r *= iconPressScale(ic);
        drawIconDisc(ic);
    }
    glBindVertexArray(0);
}

} // namespace

//  Handed over by the tray each time it arranges the arc.
//  Where the potion tray has put its slots, handed over each time it arranges.
extern "C" void RanTouch_SetPotionIcons(int count, const unsigned *tex,
                                        const float *cx, const float *cy, const float *r,
                                        const float *u0, const float *v0,
                                        const float *u1, const float *v1,
                                        const float *dim) {
    if (count < 0) count = 0;
    if (count > 8) count = 8;
    g_potCount = count;
    for (int i = 0; i < count; ++i) {
        g_potTex[i] = tex ? tex[i] : 0;
        g_potX[i]   = cx ? cx[i] : 0.0f;
        g_potY[i]   = cy ? cy[i] : 0.0f;
        g_potRad[i] = r  ? r[i]  : 0.0f;
        g_potU0[i]  = u0 ? u0[i] : 0.0f;
        g_potV0[i]  = v0 ? v0[i] : 0.0f;
        g_potU1[i]  = u1 ? u1[i] : 1.0f;
        g_potV1[i]  = v1 ? v1[i] : 1.0f;
        g_potDim[i] = dim ? dim[i] : 1.0f;
        //  The renderer's own record, not the driver's.
        //
        //  glGetTexLevelParameteriv does not exist on iOS's GLES2 headers, and
        //  binding a texture here to ask about it goes behind the bind cache's
        //  back - the next draw would sample whatever this left bound. The
        //  skill icons ask the same way.
        int tw = 0, th = 0;
        if (g_potTex[i] && RanGLR_TextureSize(g_potTex[i], &tw, &th)) {
            g_potTW[i] = (float) tw;
            g_potTH[i] = (float) th;
        } else {
            g_potTW[i] = g_potTH[i] = 0.0f;
        }
    }
}

//  Where the client has put the corner icons - the quest box and the small
//  party frame - so the editor can outline and grab them. In pixels.
extern "C" void RanTouch_SetCornerBox(int i, float cx, float cy, float r) {
    if (i < 0 || i >= kCornerMax) return;
    g_cornerBox[i].x = cx; g_cornerBox[i].y = cy; g_cornerBox[i].r = r;
    g_cornerBox[i].has = (r > 0.0f);
    if (i + 1 > g_cornerCount) g_cornerCount = i + 1;
}

//  And what the player did to icon i, in pixels, for the client to apply.
//
//  The group's own offset moves the pair; the icon's moves it alone. Both are
//  added, so dragging the pair and then nudging one of them does what it looks
//  like it does.
extern "C" void RanTouch_GetCornerAdjust(int i, float *dx, float *dy, float *scale) {
    const float gx = g_adj[kGrpCorner].dx * g_unit;
    const float gy = g_adj[kGrpCorner].dy * g_unit;
    const float gs = g_adj[kGrpCorner].scale;
    if (i < 0 || i >= kCornerMax) {
        if (dx) *dx = gx; if (dy) *dy = gy; if (scale) *scale = gs;
        return;
    }
    if (dx)    *dx    = gx + g_cornerAdj[i].dx * g_unit;
    if (dy)    *dy    = gy + g_cornerAdj[i].dy * g_unit;
    if (scale) *scale = gs * g_cornerAdj[i].scale;
}

//  How big the player has asked the skill slots to be.
//
//  The arc's own radius already carries this (RanTouch_GetSkillArc), which
//  spreads the slots further apart - but the slots themselves are the client's
//  controls and kept their authored size, so "bigger" moved them and left them
//  the same size. The tray scales its slots by this.
extern "C" float RanTouch_GetSkillScale(void) { return g_adj[kGrpSkill].scale; }

//  Where potion slot i has been dragged, in pixels, on top of the row.
extern "C" void RanTouch_GetPotionSlotOffset(int i, float *dx, float *dy) {
    if (dx) *dx = 0.0f;
    if (dy) *dy = 0.0f;
    if (i < 0 || i >= kPotMax) return;
    if (dx) *dx = g_potAdj[i].dx * g_unit;
    if (dy) *dy = g_potAdj[i].dy * g_unit;
}

//  The box the skill slots occupy, in pixels. The potion row is laid out
//  against it - above the slots, centred on them - so the two read as one
//  block of round buttons under the thumb instead of two rows that happen to
//  be near each other. Returns 0 while there are no slots to measure.
extern "C" int RanTouch_GetSkillBounds(float *cx, float *top, float *width) {
    if (g_skillCircleCount <= 0) return 0;
    float l = 1e9f, r = -1e9f, t = 1e9f;
    for (int i = 0; i < g_skillCircleCount; ++i) {
        const SkillCircle &c = g_skillCircles[i];
        if (c.x - c.r < l) l = c.x - c.r;
        if (c.x + c.r > r) r = c.x + c.r;
        if (c.y - c.r < t) t = c.y - c.r;
    }
    if (cx)    *cx    = (l + r) * 0.5f;
    if (top)   *top   = t;
    if (width) *width = r - l;
    return 1;
}

//  What the player has done to the potion row in the HUD editor, so the tray
//  can lay itself out there.
//
//  Handed over in PIXELS. The editor keeps every offset in units of g_unit -
//  that is what makes a drag feel the same on a phone and a tablet - and the
//  tray had no way to know that: it read the raw number as a fraction of the
//  screen and multiplied by the width, which threw the row several screens
//  away on the first drag. The conversion belongs on this side, where g_unit
//  lives.
extern "C" void RanTouch_GetPotionAdjust(float *dx, float *dy, float *scale) {
    if (dx)    *dx    = g_adj[kGrpPotion].dx * g_unit;
    if (dy)    *dy    = g_adj[kGrpPotion].dy * g_unit;
    if (scale) *scale = g_adj[kGrpPotion].scale;
}

//  The painted sheet for the controls. Handed over once by the client, which
//  owns the texture; 0 puts the drawn shapes back.
extern "C" void RanTouch_SetHudSheet(unsigned tex, int w, int h) {
    g_hudTex  = tex;
    g_hudTexW = (float)w;
    g_hudTexH = (float)h;
}

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
//  A motorcycle, side on, in the same steel-and-gold the other glyphs use.
//
//  Drawn rather than blitted so it is the same object as the attack ring and
//  the mode toggles: same face, same rim, same palette, and it stays sharp at
//  any button size instead of being a 30 px sprite stretched over a disc.
void artBike(float ox, float oy, float r, float a) {
    const Col dark  = alpha(kDark,   a);
    const Col rim   = alpha(kSteel,  a);
    const Col spoke = alpha(kBladeD, a);
    const Col body  = alpha(kBlade,  a);
    const Col bodyL = alpha(kEdge,   a);
    const Col gold  = alpha(kGold,   a);
    const Col goldH = alpha(kGoldH,  a);

    const float RW = -0.56f, FW = 0.60f, WY = 0.36f;
    const float TO = 0.38f, TI = 0.22f;

    //  Wheels are the light part. Dark tyres on a dark face vanish at button
    //  size - the first cut of this glyph read as a blob with two smudges.
    drawRing(ox + r*RW, oy + r*WY, r*TI, r*TO, rim.r, rim.g, rim.b, rim.a);
    drawRing(ox + r*FW, oy + r*WY, r*TI, r*TO, rim.r, rim.g, rim.b, rim.a);
    drawRing(ox + r*RW, oy + r*WY, 0.0f, r*TI, dark.r, dark.g, dark.b, dark.a * 0.9f);
    drawRing(ox + r*FW, oy + r*WY, 0.0f, r*TI, dark.r, dark.g, dark.b, dark.a * 0.9f);
    drawCapsule(ox + r*(RW-TI), oy + r*WY, ox + r*(RW+TI), oy + r*WY, r*0.032f, spoke);
    drawCapsule(ox + r*(FW-TI), oy + r*WY, ox + r*(FW+TI), oy + r*WY, r*0.032f, spoke);
    drawRing(ox + r*RW, oy + r*WY, 0.0f, r*0.070f, gold.r, gold.g, gold.b, gold.a);
    drawRing(ox + r*FW, oy + r*WY, 0.0f, r*0.070f, gold.r, gold.g, gold.b, gold.a);

    //  Swing arm and fork.
    drawCapsule(ox + r*RW, oy + r*WY, ox - r*0.04f, oy + r*0.14f, r*0.055f, body);
    drawCapsule(ox + r*FW, oy + r*WY, ox + r*0.42f, oy - r*0.18f, r*0.060f, body);

    //  Three pieces, which is what makes it a motorcycle rather than a bicycle:
    //  a tail that humps up over the back wheel, a tank in the middle, and a
    //  fairing dropping to the front.
    const float tail[] = { -0.72f,-0.06f, -0.46f,-0.26f, -0.16f,-0.20f,
                           -0.10f, 0.02f, -0.44f, 0.08f, -0.72f, 0.04f };
    const float tank[] = { -0.20f,-0.20f,  0.10f,-0.30f,  0.34f,-0.24f,
                            0.32f, 0.02f, -0.06f, 0.08f, -0.22f, 0.00f };
    const float cowl[] = {  0.30f,-0.28f,  0.52f,-0.34f,  0.62f,-0.16f,
                            0.54f, 0.06f,  0.38f, 0.02f,  0.32f,-0.12f };
    artPoly(ox, oy, r, 0.0f, 1.0f, tail, 6, body,  a);
    artPoly(ox, oy, r, 0.0f, 1.0f, tank, 6, body,  a);
    artPoly(ox, oy, r, 0.0f, 1.0f, cowl, 6, body,  a);

    //  Lit top edges only; filling any of them pale turns it back into a blob.
    const float tailL[] = { -0.72f,-0.06f, -0.46f,-0.26f, -0.16f,-0.20f,
                            -0.18f,-0.13f, -0.45f,-0.18f, -0.72f, 0.00f };
    const float tankL[] = { -0.20f,-0.20f,  0.10f,-0.30f,  0.34f,-0.24f,
                             0.33f,-0.17f,  0.10f,-0.23f, -0.19f,-0.13f };
    const float cowlL[] = {  0.30f,-0.28f,  0.52f,-0.34f,  0.62f,-0.16f,
                             0.55f,-0.14f,  0.50f,-0.26f,  0.33f,-0.21f };
    artPoly(ox, oy, r, 0.0f, 1.0f, tailL, 6, bodyL, a);
    artPoly(ox, oy, r, 0.0f, 1.0f, tankL, 6, bodyL, a);
    artPoly(ox, oy, r, 0.0f, 1.0f, cowlL, 6, bodyL, a);

    //  Bars, and the screen as the one gold flash up front.
    drawCapsule(ox + r*0.18f, oy - r*0.44f, ox + r*0.44f, oy - r*0.36f, r*0.048f, rim);
    const float scr[] = { 0.34f,-0.32f,  0.50f,-0.42f,  0.58f,-0.30f,  0.44f,-0.22f };
    artPoly(ox, oy, r, 0.0f, 1.0f, scr, 4, goldH, a * 0.9f);

    //  Exhaust along the bottom, warm against all that steel.
    drawCapsule(ox - r*0.02f, oy + r*0.14f, ox - r*0.50f, oy + r*0.20f, r*0.055f, gold);
    drawCapsule(ox - r*0.02f, oy + r*0.14f, ox - r*0.28f, oy + r*0.17f, r*0.022f, goldH);
}

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

//  1..4 for the page buttons, 0 for anything else.
int pageOfSlot(int slot) {
    switch (slot) {
        case kSlotF1: return 1;
        case kSlotF2: return 2;
        case kSlotF3: return 3;
        case kSlotF4: return 4;
    }
    return 0;
}

//  The marks that are still marks rather than pictures: the reticle for
//  auto-target and the eye for camera lock.
void glyphMark(const Button &b, float a) {
    const float R = b.radius;
    const Col c = alpha(kInk, a);
    const int nPage = pageOfSlot(b.slot);
    if (nPage > 0) {
        //  The sheet paints these as "F1".."F4"; without it, the figure alone.
        //  An arrow would be wrong now - these are four places, not two steps.
        const float dh = R * 0.90f, dw = dh * 0.62f;
        drawDigit(b.centre.x - dw * 0.5f, b.centre.y - dh * 0.5f, dw, dh, nPage,
                  c.r, c.g, c.b, c.a);
    }
    else if (b.slot == kSlotAuto) {
        drawRing(b.centre.x, b.centre.y, R * 0.26f, R * 0.34f, c.r, c.g, c.b, c.a);
        const float t = R * 0.09f;
        drawRect(b.centre.x - t*0.5f, b.centre.y - R*0.60f, t, R*0.22f, c.r, c.g, c.b, c.a);
        drawRect(b.centre.x - t*0.5f, b.centre.y + R*0.38f, t, R*0.22f, c.r, c.g, c.b, c.a);
        drawRect(b.centre.x - R*0.60f, b.centre.y - t*0.5f, R*0.22f, t, c.r, c.g, c.b, c.a);
        drawRect(b.centre.x + R*0.38f, b.centre.y - t*0.5f, R*0.22f, t, c.r, c.g, c.b, c.a);
    }
    else if (b.slot == kSlotMenu) {
        //  Four squares. The same mark the design used, and the one thing a
        //  grid of icons can be drawn as at this size and still be read.
        const float s2 = R * 0.30f, g2 = R * 0.10f;
        drawRect(b.centre.x - s2 - g2*0.5f, b.centre.y - s2 - g2*0.5f, s2, s2, c.r, c.g, c.b, c.a);
        drawRect(b.centre.x + g2*0.5f,      b.centre.y - s2 - g2*0.5f, s2, s2, c.r, c.g, c.b, c.a);
        drawRect(b.centre.x - s2 - g2*0.5f, b.centre.y + g2*0.5f,      s2, s2, c.r, c.g, c.b, c.a);
        drawRect(b.centre.x + g2*0.5f,      b.centre.y + g2*0.5f,      s2, s2, c.r, c.g, c.b, c.a);
    }
    else if (b.slot == kSlotChat) {
        //  Folded: the button IS the chat, so it wears a speech bubble rather
        //  than an arrow - what brings the chat back should look like chat.
        //  (Open, it is a plate with a bar and never reaches this.)
        {
            const float bw = R * 0.62f, bh = R * 0.44f;
            drawRect(b.centre.x - bw, b.centre.y - bh * 1.15f,
                     bw * 2.0f, bh * 1.7f, c.r, c.g, c.b, c.a);
            const float tail[6] = {
                b.centre.x - bw * 0.52f, b.centre.y + bh * 0.55f,
                b.centre.x - bw * 0.04f, b.centre.y + bh * 0.55f,
                b.centre.x - bw * 0.62f, b.centre.y + bh * 1.45f,
            };
            drawPoly(tail, 3, c);
        }
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

//  Everything the cached geometry is built from, in one number.
//
//  Deliberately does NOT include the stick or the recharge wipes: those change
//  every frame while the player is moving or a skill is cooling, and they are
//  drawn live instead. Including them would rebuild the whole overlay on almost
//  every frame, which is the situation this exists to avoid.
//  The joystick's shapes, exactly as RanTouch_Render used to build them inline.
static void drawStickShapes(const Vec2 &base) {
        const float a = g_stick.held ? 1.0f : 0.82f;
        const float R = g_stick.radius;

        //  The well is a hint, not a hole. It used to be a near-opaque black
        //  disc that covered the world under the thumb; at 30% the ring still
        //  reads and the ground shows through.
        drawHalo(base.x, base.y + R * 0.12f, R * 0.86f, rgba(0.0f, 0.0f, 0.0f, 0.42f), 1.55f);

        //  Painted seat, if the sheet is here. What stays drawn is everything
        //  that moves: the heading wedge, the amber rim at full deflection and
        //  the glow under the knob all answer the thumb, and none of that can
        //  come out of a still image.
        if (!hudSheet())
        drawFan(base.x, base.y, R * 0.92f, kFaceE.r, kFaceE.g, kFaceE.b, 0.30f * a);

        //  Eight ticks, which is what gives the ring a sense of direction even
        //  before the thumb moves.
        if (!hudSheet()) {
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
        }

        //  No amber rim and no heading wedge.
        //
        //  They were there to say "you are running" and "this way" without
        //  being looked for, but the thing they marked is already under the
        //  thumb that made it happen, and the ring is the one control on the
        //  screen that is never out of the corner of your eye.

        //  The knob: the solid part, the thing the thumb is actually holding.
        const float kr = R * 0.42f;
        if (!hudSheet())
        chromeDisc(g_stick.knob.x, g_stick.knob.y, kr, 1.0f,
                   g_stick.held ? rgba(0.659f, 0.486f, 0.227f, 1.0f) : kFace,
                   g_stick.held ? rgba(0.204f, 0.102f, 0.016f, 1.0f) : kFaceE);
        //  Nothing drawn over the painted knob: it moves, which is the only
        //  feedback it needs.
}

//  Everything the resting joystick's geometry depends on.
static unsigned long long stickSignature(const Vec2 &base) {
    unsigned long long h = 1469598103934665603ULL;   //  FNV-1a
    #define MIX(v) do { const unsigned char *q = (const unsigned char *)&(v); for (int k = 0; k < (int)sizeof(v); ++k) { h ^= q[k]; h *= 1099511628211ULL; } } while (0)
    MIX(g_width); MIX(g_height); MIX(g_unit);
    MIX(base.x); MIX(base.y);
    MIX(g_stick.radius); MIX(g_stick.knob.x); MIX(g_stick.knob.y);
    MIX(g_adj[kGrpStick].alpha);
    #undef MIX
    return h;
}

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
    for (int i = 0; i < kGrpCount; ++i) { MIX(g_adj[i].alpha); MIX(g_adj[i].scale); }
    #undef MIX
    (void)p; (void)n;
    return h;
}

//  The editor's overlay: an outline on every group the player can arrange, a
//  brighter one on the selected group, and the toolbar across the top.
void drawEditor();
void drawEditor() {
    const float u = g_unit;
    //  Outlines.
    for (int i = 0; i < g_skillCircleCount && i < RANTOUCH_MAX_SKILL_CIRCLES; ++i) {
        const SkillCircle &sc = g_skillCircles[i];
        const float rr = sc.r * 1.32f;
        if (g_editSel == kGrpSkill && g_editSlot == i) {
            //  A thin ring, no halo: the glow read as a heavy shadow over the
            //  control it was meant to point at.
            drawRing(sc.x, sc.y, rr - u * 0.016f, rr, kAmber.r, kAmber.g, kAmber.b, 0.95f);
        } else {
            drawRing(sc.x, sc.y, rr - u * 0.018f, rr, kInk.r, kInk.g, kInk.b, 0.5f);
        }
    }
    for (int i = 0; i < g_potCount && i < kPotMax; ++i) {
        const float rr = g_potRad[i] * 1.45f;
        if (g_editSel == kGrpPotion && g_editSlot == i) {
            drawRing(g_potX[i], g_potY[i], rr - u * 0.016f, rr, kAmber.r, kAmber.g, kAmber.b, 0.95f);
        } else {
            drawRing(g_potX[i], g_potY[i], rr - u * 0.018f, rr, kInk.r, kInk.g, kInk.b, 0.5f);
        }
    }
    for (int i = 0; i < g_cornerCount && i < kCornerMax; ++i) {
        if (!g_cornerBox[i].has) continue;
        const float rr = g_cornerBox[i].r;
        if (g_editSel == kGrpCorner && g_editSlot == i) {
            drawRing(g_cornerBox[i].x, g_cornerBox[i].y, rr - u * 0.016f, rr,
                     kAmber.r, kAmber.g, kAmber.b, 0.95f);
        } else {
            drawRing(g_cornerBox[i].x, g_cornerBox[i].y, rr - u * 0.018f, rr,
                     kInk.r, kInk.g, kInk.b, 0.5f);
        }
    }
    for (int g = 0; g < kGrpCount; ++g) {
        //  outlined one by one above
        if (g == kGrpSkill || g == kGrpPotion || g == kGrpCorner) continue;
        Vec2 c; float r;
        if (!groupCircle(g, c, r)) continue;
        if (g == g_editSel) {
            drawRing(c.x, c.y, r - u * 0.016f, r, kAmber.r, kAmber.g, kAmber.b, 0.95f);
        } else {
            drawRing(c.x, c.y, r - u * 0.02f, r, kInk.r, kInk.g, kInk.b, 0.55f);
        }
    }

    emit();

    //  Toolbar plate, which is also the handle it is dragged by.
    {
        float bx, by, bw, bh;
        toolBar(&bx, &by, &bw, &bh);
        drawRect(bx, by, bw, bh, 0.0f, 0.0f, 0.0f, g_editBar ? 0.75f : 0.55f);
    }
    const bool haveSel = g_editSel >= 0;
    for (int t = 0; t < kToolCount; ++t) {
        Vec2 c; float r;
        toolCircle(t, c, r);
        const bool needsSel = (t >= kToolSizeDn && t <= kToolAlphaUp);
        const float dim = (needsSel && !haveSel) ? 0.35f : 1.0f;
        chromeDisc(c.x, c.y, r, dim, kFace, kFaceE);
        const Col ink = alpha(t == kToolSave ? kCyan : (t == kToolCancel ? kCrim : kInk), dim);
        const float s = r * 0.46f, w = r * 0.09f;
        switch (t) {
            case kToolCancel:
                drawCapsule(c.x - s, c.y - s, c.x + s, c.y + s, w, ink);
                drawCapsule(c.x - s, c.y + s, c.x + s, c.y - s, w, ink);
                break;
            case kToolSave:
                drawCapsule(c.x - s, c.y, c.x - s * 0.25f, c.y + s * 0.7f, w, ink);
                drawCapsule(c.x - s * 0.25f, c.y + s * 0.7f, c.x + s, c.y - s * 0.7f, w, ink);
                break;
            case kToolReset:
                drawArc(c.x, c.y, s * 0.78f, s, 0.9f, 5.6f, ink.r, ink.g, ink.b, ink.a);
                drawTri(c.x + s * 0.55f, c.y - s * 0.72f, s * 0.42f, 1.0f, ink.r, ink.g, ink.b, ink.a);
                break;
            case kToolSizeDn: case kToolAlphaDn:
                drawCapsule(c.x - s, c.y, c.x + s, c.y, w, ink);
                break;
            case kToolSizeUp: case kToolAlphaUp:
                drawCapsule(c.x - s, c.y, c.x + s, c.y, w, ink);
                drawCapsule(c.x, c.y - s, c.x, c.y + s, w, ink);
                break;
        }
    }
    //  What the - and + change, and its current value, between each pair.
    for (int pair = 0; pair < 2; ++pair) {
        Vec2 a, b; float r;
        toolCircle(pair == 0 ? kToolSizeDn : kToolAlphaDn, a, r);
        toolCircle(pair == 0 ? kToolSizeUp : kToolAlphaUp, b, r);
        const float mx = (a.x + b.x) * 0.5f;
        const float ly = a.y - r * 0.75f;
        //  A square for size, a half-filled disc for opacity.
        if (pair == 0) drawRing(mx, ly, r * 0.16f, r * 0.26f, kSteel.r, kSteel.g, kSteel.b, 0.9f);
        else           drawDiscBottom(mx, ly, r * 0.26f, 0.5f, kSteel.r, kSteel.g, kSteel.b, 0.9f);
        if (!haveSel) continue;
        //  The number has to be the one the buttons change.
        //
        //  It read the GROUP's size while a selected slot's own size was what
        //  moved, so pressing + on a skill slot grew the slot and left the
        //  readout sitting at 100.
        float vScale = g_adj[g_editSel].scale;
        if (g_editSlot >= 0) {
            if (g_editSel == kGrpSkill && g_editSlot < RANTOUCH_MAX_SKILL_CIRCLES)
                vScale = g_slotAdj[g_editSlot].scale;
            else if (g_editSel == kGrpPotion && g_editSlot < kPotMax)
                vScale = g_potAdj[g_editSlot].scale;
            else if (g_editSel == kGrpCorner && g_editSlot < kCornerMax)
                vScale = g_cornerAdj[g_editSlot].scale;
        }
        const float v = (pair == 0) ? vScale : g_adj[g_editSel].alpha;
        int pct = (int)(v * 100.0f + 0.5f);
        const float dh = r * 0.62f, dw = dh * 0.55f, gap = dw * 0.25f;
        char digits[4]; int nd = 0;
        if (pct >= 100) digits[nd++] = (char)(pct / 100);
        digits[nd++] = (char)((pct / 10) % 10);
        digits[nd++] = (char)(pct % 10);
        float x = mx - (nd * dw + (nd - 1) * gap) * 0.5f;
        for (int i = 0; i < nd; ++i) {
            drawDigit(x, a.y - dh * 0.25f, dw, dh, digits[i], kInk.r, kInk.g, kInk.b, 1.0f);
            x += dw + gap;
        }
    }
    emit();
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
            s_off = (RanPlat_DiagExists("nohud"));
            g_stickCacheOn = !(RanPlat_DiagExists("nostickcache"));
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
    if (g_stickCacheOn && !g_stick.held) {
        //  At rest: replay the stick from its own buffer, rebuilt only when
        //  where it sits or how big it is changes. The main cache's segment
        //  list is replayed later this frame, so it is kept aside, not reused.
        emit();
        const unsigned long long ssig = stickSignature(base);
        if (ssig != g_stickSig || g_stickVerts == 0) {
            g_drawAlpha = g_adj[kGrpStick].alpha;
            Seg saved[32];
            const int savedCount = g_segCount;
            memcpy(saved, g_segs, sizeof(saved));

            g_capturing = true;
            g_bn = 0; g_capFirst = 0; g_segCount = 0; g_additive = false;
            drawStickShapes(base);
            emit();                             //  closes the last segment
            g_capturing = false;

            g_drawAlpha = 1.0f;
            g_stickVerts = g_bn / kFloatsPerVert;
            g_stickSegCount = g_segCount;
            memcpy(g_stickSegs, g_segs, sizeof(Seg) * (size_t)g_segCount);
            glBindBuffer(GL_ARRAY_BUFFER, g_vboStick);
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(g_bn * sizeof(float)),
                         g_batch, GL_STATIC_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
            g_bn = 0;
            g_stickSig = ssig;

            memcpy(g_segs, saved, sizeof(saved));
            g_segCount = savedCount;
            ++g_rebuilds;
        }
        if (g_stickVerts > 0) {
            glBindVertexArray(g_vaoStick);
            for (int i = 0; i < g_stickSegCount; ++i) {
                glBlendFunc(GL_SRC_ALPHA, g_stickSegs[i].add ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
                glDrawArrays(GL_TRIANGLES, g_stickSegs[i].first, g_stickSegs[i].count);
                ++g_batchDraws;
                g_vertsThisFrame += (unsigned)g_stickSegs[i].count;
            }
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glBindVertexArray(g_vao);
            glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
        }
    } else {
        g_drawAlpha = g_adj[kGrpStick].alpha;
        drawStickShapes(base);
        emit();
        g_drawAlpha = 1.0f;
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
    g_drawAlpha = g_adj[kGrpSkill].alpha;
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

        //  Painted bezels are drawn in the live pass below, for the same
        //  reason the buttons are: this loop fills a cache that is replayed.
        if (hudSheet()) continue;

        chromeDisc(c.x, c.y, fr, 1.0f, kFace, kFaceE);
    }

    for (int i = 0; i < kButtonCount; ++i) {
        const Button &b = g_buttons[i];
        //  Not placed by the client yet - outside the world, or no chat.
        if (b.slot == kSlotVehicle && !g_vehShow) continue;
        if (b.slot == kSlotChat) continue;     //  drawn in RanTouch_RenderChatTop
        {
            const int grp = groupOfButton(i);
            emit();
            g_drawAlpha = (grp >= 0) ? g_adj[grp].alpha : 1.0f;
        }
        //  A press shrinks the button and brightens its rim. It used to change
        //  alpha only, which is invisible against a moving scene.
        const float press = b.down ? 0.94f : 1.0f;
        const float R = b.radius * press;

        if (b.slot == kSlotAttack) {
            //  The hero control. A hot face rather than a neutral one: the
            //  thing the thumb lives on has to look charged.
            const bool dead = !b.toggled && false;      //  reserved: no-target dimming
            const float a = 1.0f;

            //  Nothing drawn around the painted button.
            //
            //  It carried an amber bloom and a twelve-segment ring - the swing
            //  timer, full until the client fed a fraction in, which it never
            //  does - over art that already has a ring of its own. Two rings,
            //  and the drawn one was the brighter. The joystick lost the same
            //  pair for the same reason.
            if (hudSheet()) continue;

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
        //  A lit page button is amber: it is saying where you ARE, which is
        //  what amber means everywhere else here. Cyan would read as a mode.
        const Col state = pk ? kCrim : (pageOfSlot(b.slot) > 0 ? kAmber : kCyan);

        //  Painted: nothing to build here.
        //
        //  This loop fills a cache that is rebuilt only when the layout
        //  changes, and then replayed every frame - so a texture drawn from
        //  inside it appears for the one frame of the rebuild and never again.
        //  That is exactly what happened. The cells are drawn in their own
        //  pass after the cache is replayed; all that is left here is the
        //  bloom under a lit toggle, which is flat colour and caches happily.
        if (hudSheet() && hudCellFor(b.slot, b.toggled) >= 0) {
            if (b.toggled) bloom(b.centre.x, b.centre.y, R, state, 0.26f);
            continue;
        }

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
        else if (b.slot == kSlotVehicle)
                       artBike(b.centre.x, b.centre.y, R * 0.68f, b.toggled ? 1.0f : 0.90f);
        else           glyphMark(b, b.toggled ? 1.0f : 0.88f);
    }

        emit();                             //  closes the last segment
        g_drawAlpha = 1.0f;
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

    //  The painted controls, drawn live.
    //
    //  Everything above this point is cached geometry replayed from a buffer;
    //  these are textures and have to be issued every frame, after that replay
    //  so they sit on top of the faces and under the skill pictures.
    emit();
    if (hudSheet()) {
        for (int i = 0; i < kButtonCount; ++i) {
            const Button &b = g_buttons[i];
            if (b.slot == kSlotVehicle && !g_vehShow) continue;
            if (b.slot == kSlotChat) continue;     //  drawn in RanTouch_RenderChatTop
            const int grp = groupOfButton(i);
            const float ga = (grp >= 0) ? g_adj[grp].alpha : 1.0f;
            const float R  = b.radius * (b.down ? 0.94f : 1.0f);
            if (b.slot == kSlotAttack) {
                drawHudCell(kCellAtkRing, b.centre.x, b.centre.y, R * 1.18f, ga);
                drawHudCell(kCellAtk,     b.centre.x, b.centre.y, R * 1.02f, ga);
                continue;
            }
            const int cell = hudCellFor(b.slot, b.toggled);
            if (cell < 0) continue;

            //  The fold plate is a wide bar painted inside a square cell: 178
            //  of the cell's 256 across, 144 down. Drawn at this half-size its
            //  height comes out 2R and its width 2R * ASPECT, which is the
            //  rectangle the press is tested against.
            if (b.slot == kSlotChat && g_chatMode == 1) {
                drawHudCell(cell, b.centre.x, b.centre.y,
                            R * (256.0f / 144.0f), ga);
                continue;
            }

            drawHudCell(cell, b.centre.x, b.centre.y, R * 1.06f, ga);
        }
        if (g_stick.radius > 0.0f) {
            //  The seat follows the thumb the way the drawn one did: where the
            //  finger landed while it is held, its resting place otherwise.
            const Vec2 sbase = g_stick.held ? g_stick.origin : g_stick.centre;
            const float sa = g_adj[kGrpStick].alpha;
            drawHudCell(kCellStickBase, sbase.x, sbase.y, g_stick.radius * 1.06f, sa);
            drawHudCell(kCellStickKnob, g_stick.knob.x, g_stick.knob.y,
                        g_stick.radius * 0.47f, sa);
        }
        glUseProgram(g_prog);
        glBindVertexArray(g_vao);
        glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
        glUniform2f(uViewport, (float)g_width, (float)g_height);
    }

    //  The skill slots get the painted bezel too - it was in the sheet from the
    //  start and only the potion row was using it.
    if (hudSheet() && g_skillCircleCount > 0) {
        const float sa = g_adj[kGrpSkill].alpha;
        for (int i = 0; i < g_skillCircleCount; ++i) {
            const SkillCircle &c = g_skillCircles[i];
            //  The stick's seat, not the square frame that came with the
            //  set: these slots are circles, and a square bezel round a round
            //  icon leaves its corners hanging in the air. The seat is the
            //  one round bezel in the sheet.
            drawHudCell(kCellStickBase, c.x, c.y, c.r * 1.62f, sa);
        }
    }

    //  The potion slots: the painted bezel, then the item inside it.
    if (hudSheet() && g_potCount > 0) {
        const float pa = g_adj[kGrpPotion].alpha;
        for (int i = 0; i < g_potCount; ++i)
            //  skillframe.png, which had been packed since the set arrived and
            //  drawn by nothing. A potion picture is square, and the frame is
            //  the square one in the sheet.
            drawHudCell(kCellSkillFrame, g_potX[i], g_potY[i], g_potRad[i] * 1.62f, pa);
        ensureTexProg();
        if (g_texProg) {
            glUseProgram(g_texProg);
            glUniform2f(uTexViewport, (float)g_width, (float)g_height);
            glUniform1i(uTexSampler, 0);
            for (int i = 0; i < g_potCount; ++i) {
                //  Per icon, because the fade is per slot: the one whose potion
                //  is in the hand goes dim while the rest stay lit.
                glUniform1f(uTexAlpha, pa * g_potDim[i]);
                //  An empty slot is a bezel and nothing else - the row keeps
                //  its full length so a potion dropped into slot 5 does not
                //  make the whole row jump.
                //
                //  And a handle that is no longer a texture draws as a black
                //  disc: the client releases an item's picture when the slot
                //  changes or the stack runs out, and anything holding the old
                //  name samples nothing. glIsTexture is the one question GL
                //  will answer about a name it did not just give us, and it
                //  costs nothing at six slots a frame.
                if (!g_potTex[i] || !glIsTexture(g_potTex[i])) continue;
                SkillIcon ic;
                ic.tex = g_potTex[i]; ic.x = g_potX[i]; ic.y = g_potY[i]; ic.r = g_potRad[i];
                ic.u0 = g_potU0[i]; ic.v0 = g_potV0[i];
                ic.u1 = g_potU1[i]; ic.v1 = g_potV1[i];
                ic.texW = g_potTW[i]; ic.texH = g_potTH[i];
                //  The frame's window, which is wider than it is tall and sits
                //  a little above the middle of the cell.
                const float F = g_potRad[i] * kBezelSize;
                ic.y -= F * kFrameWinUp;
                drawIconQuad(ic, F * kFrameWinX, F * kFrameWinY);
            }
            glBindVertexArray(0);
        }
        glUseProgram(g_prog);
        glBindVertexArray(g_vao);
        glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
        glUniform2f(uViewport, (float)g_width, (float)g_height);
    }

    //  The icons go on top of their faces.
    emit();                                     //  before the program changes
    drawIcons((float)g_width, (float)g_height);
    //  Back to flat colour for the recharge wipe, which goes over the icon.
    glUseProgram(g_prog);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glUniform2f(uViewport, (float)g_width, (float)g_height);
    g_drawAlpha = g_adj[kGrpSkill].alpha;
    for (int i = 0; i < g_skillCircleCount; ++i) {
        const SkillCircle &c = g_skillCircles[i];
        if (!c.filled || c.cool <= 0.0f) continue;
        //  ARGB(150,0,0,0), the colour the client tints its own recharge bar.
        //  Over the picture, which now fills the seat's hole - a wipe sized to
        //  anything else spills onto the bezel or stops short of the icon.
        const float ir = hudSheet() ? c.r * kBezelSize * kSeatWin : c.r * 1.30f * kRimIn;
        const float iy = hudSheet() ? c.y - c.r * kBezelSize * kSeatUp : c.y;
        drawDiscBottom(c.x, iy, ir, c.cool,
                       0.0f, 0.0f, 0.0f, 150.0f / 255.0f);
    }
    //  Back to the flat-colour program for anything after this.
    glUseProgram(g_prog);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);

    emit();
    g_drawAlpha = 1.0f;

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
    //  A circle that has never been pressed has to start idle, and one that is
    //  mid-flash has to keep its age: this is called every frame, so assigning
    //  the whole struct would restart or erase the flash on the next frame.
    static bool s_init = false;
    if (!s_init) {
        s_init = true;
        for (int i = 0; i < RANTOUCH_MAX_SKILL_CIRCLES; ++i)
            g_skillCircles[i].press = -1.0f;
    }

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

    //  The page row hangs off slot 1, which just moved. Only the row is
    //  replaced - calling layout() here would also recentre the stick, which
    //  is held by a thumb while this runs.
    placePageRow();
}

extern "C" void RanTouch_GetAttackCircle(float *cx, float *cy, float *r) {
    //  Fractions of the surface. g_width/g_height are only zero before Init, and
    //  a caller that early would otherwise divide by it.
    const float w = (g_width  > 0) ? (float)g_width  : 1.0f;
    const float h = (g_height > 0) ? (float)g_height : 1.0f;
    //  The skill arc's own offset and size ride on top of the attack button:
    //  moving the button carries the arc, and the arc can be moved against it.
    //  Sized from the button's designed radius, so resizing the button alone
    //  does not resize the arc.
    const HudAdj &a = g_adj[kGrpSkill];
    const float baseR = g_attackBaseR > 0.0f ? g_attackBaseR : g_buttons[0].radius;
    const float baseX = g_attackBaseR > 0.0f ? g_attackBaseX : g_buttons[0].centre.x;
    const float baseY = g_attackBaseR > 0.0f ? g_attackBaseY : g_buttons[0].centre.y;
    if (cx) *cx = (baseX + a.dx * g_unit) / w;
    if (cy) *cy = (baseY + a.dy * g_unit) / h;
    if (r)  *r  = baseR * a.scale / h;
}

//  The tray owns the tab index; the overlay only draws it.
extern "C" void RanTouch_SetChatButton(float cx, float cy, float r, int mode) {
    g_chatMode  = mode;
    g_chatFracX = cx;
    g_chatFracY = cy;
    g_chatFracR = r;
    if (g_inited) {
        g_buttons[kBtnChat].radius = ( mode == 2 || r <= 0.0f )
                                   ? g_buttons[3].radius       //  a mode toggle
                                   : r * (float) g_height;
        placeChatButton();
    }
}

extern "C" void RanTouch_SetVehicleButton(float cx, float cy, int show) {
    g_vehShow  = show != 0;
    g_vehFracX = cx;
    g_vehFracY = cy;
    if (g_inited) {
        g_buttons[7].centre.x = cx * (float) g_width;
        g_buttons[7].centre.y = cy * (float) g_height;
    }
}

extern "C" void RanTouch_SetSkillPage(int page) {
    g_skillPage = (page >= 1 && page <= 9) ? page : 0;
    //  The page is a state, and a state on this pad is a lit button. It used to
    //  be a separate readout plate between the two arrows; with a button per
    //  page there is nothing left for a plate to say.
    for (int i = 0; i < kButtonCount; ++i) {
        const int n = pageOfSlot(g_buttons[i].slot);
        if (n > 0) g_buttons[i].toggled = (n == g_skillPage);
    }
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

//  --- HUD arrangement API -------------------------------------------------

//  Enter or leave the editor. Entering snapshots the arrangement for cancel and
//  releases anything held, so no stick or button stays down under the editor.
extern "C" void RanTouch_SetEditMode(int on) {
    if (on && !g_edit) {
        memcpy(g_editBefore, g_adj, sizeof(g_adj));
        memcpy(g_slotBefore, g_slotAdj, sizeof(g_slotAdj));
        memcpy(g_potBefore,  g_potAdj,  sizeof(g_potAdj));
        memcpy(g_cornerBefore, g_cornerAdj, sizeof(g_cornerAdj));
        g_stick.pointer = -1; g_stick.held = false; g_stick.magnitude = 0.0f;
        g_stick.knob = g_stick.origin = g_stick.centre;
        for (int i = 0; i < kButtonCount; ++i) { g_buttons[i].pointer = -1; g_buttons[i].down = false; g_buttons[i].pressedEdge = false; }
        for (int i = 0; i < kMaxPointers; ++i) g_touch[i].id = -1;
        g_pinch.a = g_pinch.b = -1;
        g_edit = true;
        g_editSel = -1;
    } else if (!on && g_edit) {
        editEnd();
    }
}

extern "C" int RanTouch_IsEditMode(void) { return g_edit ? 1 : 0; }

//  The arrangement as kGrpCount * 4 floats: dx, dy, size, opacity per group.
//  The arrangement, as floats.
//
//  Groups first (dx, dy, size, opacity each), then every slot as a triple
//  (dx, dy, size): the skill slots, the potion slots and the corner icons.
//  Appended, never reordered - a shorter file is an older one.
extern "C" int RanTouch_GetHudLayout(float *out, int max) {
    const int n = kGrpCount * 4
                + (RANTOUCH_MAX_SKILL_CIRCLES + kPotMax + kCornerMax) * 3;
    if (!out || max < n) return n;
    for (int i = 0; i < kGrpCount; ++i) {
        out[i * 4]     = g_adj[i].dx;    out[i * 4 + 1] = g_adj[i].dy;
        out[i * 4 + 2] = g_adj[i].scale; out[i * 4 + 3] = g_adj[i].alpha;
    }
    int w = kGrpCount * 4;
    #define PUT(A, N) for (int i = 0; i < (N); ++i) {         out[w++] = (A)[i].dx; out[w++] = (A)[i].dy; out[w++] = (A)[i].scale; }
    PUT(g_slotAdj,   RANTOUCH_MAX_SKILL_CIRCLES)
    PUT(g_potAdj,    kPotMax)
    PUT(g_cornerAdj, kCornerMax)
    #undef PUT
    return n;
}

//  Applies a saved arrangement. Values out of range are the ones a damaged or
//  hand-edited file would carry, so each is clamped rather than trusted.
extern "C" void RanTouch_SetHudLayout(const float *in, int n) {
    if (!in || n < 4) return;
    //  As many groups as the file actually carries.
    //
    //  A file written before a group was added is shorter than kGrpCount * 4,
    //  and refusing it outright threw away the player's whole arrangement the
    //  first time the corner icons joined the editor. The groups are appended,
    //  never reordered, so a short file is simply an older one: read what is
    //  there and leave the rest at its default.
    const int nGrp = (n / 4 < kGrpCount) ? n / 4 : kGrpCount;
    for (int i = 0; i < nGrp; ++i) {
        float dx = in[i * 4], dy = in[i * 4 + 1], sc = in[i * 4 + 2], al = in[i * 4 + 3];
        if (!(dx > -40.0f && dx < 40.0f)) dx = 0.0f;
        if (!(dy > -40.0f && dy < 40.0f)) dy = 0.0f;
        if (!(sc >= 0.6f && sc <= 1.6f)) sc = 1.0f;
        if (!(al >= 0.2f && al <= 1.0f)) al = 1.0f;
        g_adj[i].dx = dx; g_adj[i].dy = dy; g_adj[i].scale = sc; g_adj[i].alpha = al;
    }
    if (nGrp == kGrpCount) {
        int r = kGrpCount * 4;
        #define TAKE(A, N) for (int i = 0; i < (N); ++i) {             if (r + 2 >= n) break;             float dx = in[r++], dy = in[r++], sc = in[r++];             if (!(dx > -40.0f && dx < 40.0f)) dx = 0.0f;             if (!(dy > -40.0f && dy < 40.0f)) dy = 0.0f;             if (!(sc >= 0.6f && sc <= 1.6f))  sc = 1.0f;             (A)[i].dx = dx; (A)[i].dy = dy; (A)[i].scale = sc; }
        TAKE(g_slotAdj,   RANTOUCH_MAX_SKILL_CIRCLES)
        TAKE(g_potAdj,    kPotMax)
        TAKE(g_cornerAdj, kCornerMax)
        #undef TAKE
    }
    if (g_inited) layout();
}

//  Where the client should put skill slot i, as a fraction of the surface on
//  top of the arc it computed. The slots are its controls, not the overlay's.
//  How big the player has asked skill slot i to be, on top of its group.
extern "C" float RanTouch_GetSkillSlotScale(int i) {
    if (i < 0 || i >= RANTOUCH_MAX_SKILL_CIRCLES) return 1.0f;
    return g_adj[kGrpSkill].scale * g_slotAdj[i].scale;
}

//  And potion slot i.
extern "C" float RanTouch_GetPotionSlotScale(int i) {
    if (i < 0 || i >= kPotMax) return 1.0f;
    return g_potAdj[i].scale;
}

extern "C" void RanTouch_GetSkillSlotOffset(int i, float *fx, float *fy) {
    if (fx) *fx = 0.0f;
    if (fy) *fy = 0.0f;
    if (i < 0 || i >= RANTOUCH_MAX_SKILL_CIRCLES || g_width <= 0 || g_height <= 0) return;
    if (fx) *fx = g_slotAdj[i].dx * g_unit / (float)g_width;
    if (fy) *fy = g_slotAdj[i].dy * g_unit / (float)g_height;
}

//  The editor's own layer, drawn AFTER the client's interface.
//
//  The overlay itself is drawn under the interface - that is what keeps the
//  pad behind the game's windows - so the editor's toolbar came out behind
//  them too. The client calls this once more at the end of its own render.
//  The chat button, drawn over the whole interface.
//
//  The rest of the pad is drawn UNDER the client's windows - that is what keeps
//  it out of their way - but this one belongs to the chat window and sits in its
//  top right corner, so drawn there it went behind the very window it folds. It
//  gets the same treatment the HUD editor's toolbar gets: its own little pass,
//  after CInnerInterface::Render.
extern "C" void RanTouch_RenderChatTop(void) {
    if (!g_inited || !g_active || !g_prog || g_chatMode == 0) return;

    glUseProgram(g_prog);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUniform2f(uViewport, (float)g_width, (float)g_height);
    g_drawAlpha = 1.0f;

    const Button &b = g_buttons[kBtnChat];
    const float R = b.radius * (b.down ? 0.94f : 1.0f);
    const int cell = hudSheet() ? hudCellFor(kSlotChat, false) : -1;

    if (cell >= 0) {
        //  The fold plate is a wide bar painted inside a square cell: 178 of the
        //  cell's 256 across, 144 down. Drawn at this half-size its height comes
        //  out 2R and its width 2R * ASPECT - the rectangle the press is tested
        //  against.
        if (g_chatMode == 1)
            drawHudCell(cell, b.centre.x, b.centre.y, R * (256.0f / 144.0f), 1.0f);
        else
            drawHudCell(cell, b.centre.x, b.centre.y, R * 1.06f, 1.0f);
    }
    else if (g_chatMode == 1) {
        //  No sheet: the plate, drawn. A steel disc with an arrow on it read as
        //  one more thumb button sitting on the chat, so this is a window
        //  control - a chamfered plate with a minimise bar across it.
        const float hw = R * RANTOUCH_CHATBAR_ASPECT, hh = R;
        const float x0 = b.centre.x - hw, y0 = b.centre.y - hh;
        const float cut = hh * 0.45f;
        const float a = b.down ? 1.0f : 0.92f;
        drawChamfer(x0, y0, hw * 2.0f, hh * 2.0f, cut, kFaceE.r, kFaceE.g, kFaceE.b, a);
        drawChamfer(x0 + 1.5f, y0 + 1.5f, hw * 2.0f - 3.0f, hh * 2.0f - 3.0f, cut,
                    kFace.r, kFace.g, kFace.b, a);
        const float bw = hw * 0.92f, bh = hh * 0.17f;
        drawRect(b.centre.x - bw, b.centre.y - bh, bw * 2.0f, bh * 2.0f,
                 kInk.r, kInk.g, kInk.b, a);
        emit();
    }
    else {
        chromeDisc(b.centre.x, b.centre.y, R, b.down ? 1.0f : 0.94f, kFace, kFaceE);
        glyphMark(b, 0.88f);
        emit();
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    glDisable(GL_BLEND);
    RanGLR_InvalidateStateCache();
}

extern "C" void RanTouch_RenderEditTop(void) {
    if (!g_inited || !g_edit || !g_prog) return;
    glUseProgram(g_prog);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUniform2f(uViewport, (float)g_width, (float)g_height);
    g_drawAlpha = 1.0f;
    drawEditor();
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    glDisable(GL_BLEND);
    RanGLR_InvalidateStateCache();
}

//  Changes each time the player saves in the editor, so the client knows to
//  write the arrangement to its options.
extern "C" int RanTouch_HudSavedGeneration(void) { return g_hudSavedGen; }

