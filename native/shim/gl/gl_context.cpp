//  Android only. iOS has no EGL: gl_context_ios.mm answers the same RanGL_*
//  API on EAGL, and the recursive source glob compiles whichever one the
//  platform defines.
#ifdef __ANDROID__
// EGL / OpenGL ES 3 context for the D3D9 shim.
//
// Owns the connection between the Android surface and the renderer. The GL
// context is created on, and stays current on, the thread that runs the frame
// loop (android_main's thread) — everything in the D3D9 shim assumes that and
// never touches GL from anywhere else.
//
// Kept separate from d3d9_impl.cpp so the device object has no EGL knowledge:
// the iOS port later swaps this file for an EAGL/Metal-via-ANGLE equivalent
// without touching the D3D translation.

#include "gl_context.h"
#include "../platform/ran_plat.h"
#include "../platform/touch_ui.h"

#include <EGL/egl.h>
#include <unistd.h>
#include <GLES3/gl3.h>
#include <pthread.h>
#include <android/native_window.h>

//  What the chosen config actually gave us. Depth bias is expressed in units
//  of the smallest resolvable depth step, so this decides its scale.
static int g_depthBits = 24;
//  Which thread currently has the context bound. EGL allows exactly one, and
//  the loading screen borrows it, so this moves.
static pthread_t g_ctxThread;
static bool      g_ctxHeld = false;

#define LOGI(...) RanPlat_Log(RANLOG_INFO,  "RanGL", __VA_ARGS__)
#define LOGE(...) RanPlat_Log(RANLOG_ERROR, "RanGL", __VA_ARGS__)

namespace {

EGLDisplay g_display = EGL_NO_DISPLAY;
EGLSurface g_surface = EGL_NO_SURFACE;
EGLContext g_context = EGL_NO_CONTEXT;
int g_width = 0, g_height = 0;
bool g_ready = false;
//  The real panel, and how much smaller the frame is drawn than the panel.
//  Touch events arrive in panel pixels; everything else works in frame pixels.
int  g_panelWidth = 0, g_panelHeight = 0;
float g_renderScale = 1.0f;
//  Panel pixels per drawn pixel. 1 is the full panel; 2 halves the buffer and
//  lets the display stretch it. Separate from g_renderScale, which is only ever
//  about how large the GUI is laid out.
int  g_bufferDiv = 1;
bool g_swapPreserved = false;
//  Counts frames, for anything that must happen once a frame and no more.
unsigned g_frameIndex = 0;
//  Set from /sdcard/ran/preserveswap: keep the old behaviour everywhere.
bool g_forcePreserved = false;
//  The thread that created the context - the one that draws the game.
pthread_t g_mainThread;
//  Ask EGL to keep or drop the colour buffer across the swap.
//
//  Changing this costs one EGL call and only when it actually changes, so it
//  can follow whoever is drawing: the loading thread wants its incremental
//  frame kept, the game thread wants the tile buffer discarded.
void setSwapPreserved(bool on) {
    if (g_display == EGL_NO_DISPLAY || g_surface == EGL_NO_SURFACE) return;
    if (g_forcePreserved) on = true;
    //  /sdcard/ran/nopreserveswap: never preserve, even for the loading screen.
    //  For telling a preservation problem apart from something else.
    if (RanPlat_DiagExists("nopreserveswap")) on = false;
    if (on == g_swapPreserved) return;
    if (eglSurfaceAttrib(g_display, g_surface, EGL_SWAP_BEHAVIOR,
                         on ? EGL_BUFFER_PRESERVED : EGL_BUFFER_DESTROYED) != EGL_TRUE)
        return;
    EGLint behaviour = 0;
    eglQuerySurface(g_display, g_surface, EGL_SWAP_BEHAVIOR, &behaviour);
    g_swapPreserved = (behaviour == EGL_BUFFER_PRESERVED);
    LOGI("swap now %s (frame %u)", g_swapPreserved ? "preserved" : "destroyed", g_frameIndex);
}

const char *eglErrStr(EGLint e) {
    switch (e) {
        case EGL_SUCCESS: return "SUCCESS";
        case EGL_NOT_INITIALIZED: return "NOT_INITIALIZED";
        case EGL_BAD_ACCESS: return "BAD_ACCESS";
        case EGL_BAD_ALLOC: return "BAD_ALLOC";
        case EGL_BAD_ATTRIBUTE: return "BAD_ATTRIBUTE";
        case EGL_BAD_CONFIG: return "BAD_CONFIG";
        case EGL_BAD_CONTEXT: return "BAD_CONTEXT";
        case EGL_BAD_DISPLAY: return "BAD_DISPLAY";
        case EGL_BAD_MATCH: return "BAD_MATCH";
        case EGL_BAD_NATIVE_WINDOW: return "BAD_NATIVE_WINDOW";
        case EGL_BAD_SURFACE: return "BAD_SURFACE";
        case EGL_CONTEXT_LOST: return "CONTEXT_LOST";
        default: return "?";
    }
}

} // namespace

extern "C" int RanGL_Init(void *nativeWindow) {
    if (g_ready) return 1;
    ANativeWindow *win = (ANativeWindow *)nativeWindow;
    if (!win) { LOGE("no native window"); return 0; }

    g_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_display == EGL_NO_DISPLAY) { LOGE("eglGetDisplay failed"); return 0; }

    EGLint major = 0, minor = 0;
    if (!eglInitialize(g_display, &major, &minor)) {
        LOGE("eglInitialize failed: %s", eglErrStr(eglGetError()));
        return 0;
    }
    LOGI("EGL %d.%d", major, minor);

    // 24-bit depth + 8-bit stencil: the engine's shadow and water passes use
    // stencil, and asking for it up front avoids a surface recreate later.
    const EGLint configAttribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      8,
        EGL_DEPTH_SIZE,      24,
        EGL_STENCIL_SIZE,    8,
        //  The client leaves parts of the frame untouched between swaps, so the
        //  surface has to keep them — see RanGL_SwapPreserved.
        //  Keep asking for a preserved-capable config even though preservation
        //  is left off: measured, dropping the bit made the driver pick a worse
        //  config and cost 4 fps (swap 9.9 -> 13.0 ms). Requesting the
        //  capability is not the same as using it.
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT | EGL_SWAP_BEHAVIOR_PRESERVED_BIT,
        EGL_NONE
    };
    EGLConfig config;
    EGLint numConfigs = 0;
    if (!eglChooseConfig(g_display, configAttribs, &config, 1, &numConfigs) || numConfigs < 1) {
        // Fall back to 16-bit depth, no stencil — old/soft GLES drivers.
        const EGLint fallback[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
            EGL_DEPTH_SIZE, 16,
            EGL_NONE
        };
        if (!eglChooseConfig(g_display, fallback, &config, 1, &numConfigs) || numConfigs < 1) {
            LOGE("no usable EGL config: %s", eglErrStr(eglGetError()));
            return 0;
        }
        g_depthBits = 16;
        LOGI("using fallback EGL config (16-bit depth, no stencil)");
    }

    // The native window buffer format must match the config or eglCreateWindowSurface
    // fails on some drivers.
    EGLint nativeVisual = 0;
    eglGetConfigAttrib(g_display, config, EGL_NATIVE_VISUAL_ID, &nativeVisual);

    //  Render at the logical size and let the display scale it up. A PC-sized
    //  GUI on a 2560x1440 panel is unusable with a finger, so the client is run
    //  at roughly 1280 across either way; drawing it at panel resolution only
    //  costs fragments, it adds no detail.
    //  Two different questions that used to share one answer.
    //
    //  How large the GUI should be laid out, and how many pixels to draw it
    //  with. A PC-sized GUI on a 2560x1600 panel is unusable with a finger, so
    //  running the client at roughly 1280 across is right. But that was done by
    //  shrinking the frame buffer to 1280x800 and letting the display stretch
    //  it, which throws away half the panel in each direction: everything on
    //  screen is a 2x nearest-neighbour blow-up. That is the pixellation.
    //
    //  The reasoning was that the client's art is authored at 1024x768 and gains
    //  nothing from being drawn larger. True of the art, false of everything
    //  else - the 3D scene, the text and the overlay's buttons are geometry, and
    //  resolve as finely as the buffer allows.
    //
    //  So: buffer at the full panel, client coordinates stay logical, and
    //  RanGL_UIScale carries the ratio between them. The viewport and scissor
    //  paths already multiply by it, which is what it was built for, and touch
    //  already divides by InputScale, which is the same number.
    g_panelWidth  = ANativeWindow_getWidth(win);
    g_panelHeight = ANativeWindow_getHeight(win);

    //  Panel pixels per logical pixel - see RanGL_ChooseUIScale.
    g_renderScale = RanGL_ChooseUIScale(g_panelWidth, g_panelHeight);

    //  How large to lay the GUI out, and how many pixels to draw it with, are
    //  two questions. They used to share one answer.
    //
    //  A PC-sized GUI on a 2560x1600 panel is unusable with a finger, so running
    //  the client at roughly 1280 across is right. But that was done by shrinking
    //  the frame buffer to 1280x800 and letting the display stretch it, which
    //  throws away half the panel in each direction - everything on screen is a
    //  2x nearest-neighbour blow-up, and that is the blur.
    //
    //  The old reasoning was that the client's art is authored at 1024x768 and
    //  gains nothing from a larger buffer. True of the art, false of everything
    //  else: the 3D scene, the text and the overlay's buttons are geometry and
    //  resolve as finely as the buffer allows.
    //
    //  So: buffer at the full panel, client coordinates stay logical, and
    //  RanGL_UIScale carries the ratio. The viewport and scissor paths already
    //  multiply by it - that is what it was built for - and touch already
    //  divides by InputScale, the same number.
    //  Sharpness is a judgement call, so it is a setting rather than my opinion.
    //
    //  Put a number in /sdcard/ran/renderscale: 1 draws at the full panel (sharp
    //  geometry and text, but the art is authored at 1024x768 and is magnified
    //  further, which some people read as soft); 2 draws at half and lets the
    //  display stretch it, which is what the build did before. The GUI is laid
    //  out at the same size either way, so only sharpness changes.
    g_bufferDiv = 1;
    {
        //  Read once at startup, so a miss here costs one log line, not one a
        //  second - but keep it consistent with the other switches.
        FILE *f = (RanPlat_DiagExists("renderscale"))
                      ? RanPlat_DiagOpen("renderscale") : NULL;
        if (f) {
            char buf[16] = { 0 };
            if (fread(buf, 1, sizeof(buf) - 1, f) > 0) {
                const int v = atoi(buf);
                //  Only a whole divisor of a whole scale: a fractional UI scale
                //  divided again would lay the client out larger than the panel.
                if (v >= 1 && (float)v <= g_renderScale &&
                    g_renderScale == (float)(int)g_renderScale &&
                    (int)g_renderScale % v == 0) g_bufferDiv = v;
            }
            fclose(f);
        }
    }

    const int bufferW = g_panelWidth  / g_bufferDiv;
    const int bufferH = g_panelHeight / g_bufferDiv;
    ANativeWindow_setBuffersGeometry(win, bufferW, bufferH, nativeVisual);
    LOGI("panel %dx%d, drawing %dx%d, laid out %dx%d (UI scale %.4f, buffer divisor %d)",
         g_panelWidth, g_panelHeight, bufferW, bufferH,
         (int)lroundf(g_panelWidth / g_renderScale), (int)lroundf(g_panelHeight / g_renderScale),
         g_renderScale / g_bufferDiv, g_bufferDiv);

    g_surface = eglCreateWindowSurface(g_display, config, win, NULL);
    if (g_surface == EGL_NO_SURFACE) {
        LOGE("eglCreateWindowSurface failed: %s", eglErrStr(eglGetError()));
        return 0;
    }

    const EGLint ctxAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    g_context = eglCreateContext(g_display, config, EGL_NO_CONTEXT, ctxAttribs);
    if (g_context == EGL_NO_CONTEXT) {
        LOGE("eglCreateContext failed: %s", eglErrStr(eglGetError()));
        return 0;
    }

    if (!eglMakeCurrent(g_display, g_surface, g_surface, g_context)) {
        LOGE("eglMakeCurrent failed: %s", eglErrStr(eglGetError()));
        return 0;
    }
    g_ctxThread = pthread_self();
    g_mainThread = pthread_self();
    g_ctxHeld = true;

    eglQuerySurface(g_display, g_surface, EGL_WIDTH, &g_width);
    eglQuerySurface(g_display, g_surface, EGL_HEIGHT, &g_height);

    LOGI("GL_VENDOR   %s", glGetString(GL_VENDOR));
    LOGI("GL_RENDERER %s", glGetString(GL_RENDERER));
    LOGI("GL_VERSION  %s", glGetString(GL_VERSION));
    LOGI("surface %dx%d", g_width, g_height);

    // The client presents every frame; without this the loop would spin as fast
    // as the CPU allows and burn the battery for nothing.
    //  Ask the surface to keep its contents across swaps. If the driver says no
    //  the renderer clears the whole frame instead (see RanGL_SwapPreserved).
    //  Preserved swap is expensive on a tiled GPU: it stops the driver
    //  discarding the tile buffer at the swap and makes it restore the whole
    //  frame instead, which is a full-screen copy every frame at panel size.
    //
    //  The client clears the frame at the top of every scene anyway, and the
    //  renderer already handles a destroyed surface by clearing everything
    //  rather than honouring a partial clear rectangle - so nothing depends on
    //  it. /sdcard/ran/preserveswap asks for it back, to compare.
    //  Preserved by default again.
    //
    //  Turning it off is worth about 4 fps - the driver can then discard the
    //  tile buffer at the swap instead of restoring it - but the loading screen
    //  draws incrementally and leaves most of the frame untouched between
    //  swaps, so without preservation it flickers and never settles. That is
    //  what the surface attribute is for and the frame rate does not buy it.
    //
    //  Preserved only while the loading screen needs it - see setSwapPreserved.
    //
    //  Preservation is not free on a tiled GPU: every frame begins by loading
    //  the whole colour buffer back into tile memory, which at 2560x1600 is
    //  16 MB read before a single triangle. The game frame does not need it,
    //  because it redraws every pixel; the loading screen does, because it
    //  draws a progress bar and nothing else between swaps.
    //
    //  /sdcard/ran/preserveswap forces it on everywhere, to compare.
    g_forcePreserved = (RanPlat_DiagExists("preserveswap"));
    setSwapPreserved(g_forcePreserved);
    //  What the driver actually gave us. eglChooseConfig treats EGL_SAMPLES as a
    //  minimum, so a multisampled config satisfies a request for none - and on a
    //  4 megapixel panel that would quietly multiply the fill cost.
    {
        EGLint samples = 0, sampleBufs = 0, depth = 0, stencil = 0;
        eglGetConfigAttrib(g_display, config, EGL_SAMPLES, &samples);
        eglGetConfigAttrib(g_display, config, EGL_SAMPLE_BUFFERS, &sampleBufs);
        eglGetConfigAttrib(g_display, config, EGL_DEPTH_SIZE, &depth);
        eglGetConfigAttrib(g_display, config, EGL_STENCIL_SIZE, &stencil);
        LOGI("config: %d samples (%d buffers), depth %d, stencil %d",
             samples, sampleBufs, depth, stencil);
    }

    LOGI("swap behaviour: %s", g_swapPreserved
             ? "preserved" : "destroyed while playing, preserved while loading");

    //  /sdcard/ran/novsync releases the frame rate from the display, to find
    //  out whether the GPU could go faster or is simply the limit.
    eglSwapInterval(g_display, (RanPlat_DiagExists("novsync")) ? 0 : 1);

    g_ready = true;
    return 1;
}

extern "C" void RanGL_Shutdown(void) {
    if (g_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(g_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (g_context != EGL_NO_CONTEXT) eglDestroyContext(g_display, g_context);
        if (g_surface != EGL_NO_SURFACE) eglDestroySurface(g_display, g_surface);
        eglTerminate(g_display);
    }
    g_display = EGL_NO_DISPLAY;
    g_surface = EGL_NO_SURFACE;
    g_context = EGL_NO_CONTEXT;
    g_ready = false;
}

extern "C" int  RanGL_SwapPreserved(void) { return g_swapPreserved ? 1 : 0; }

extern "C" int  RanGL_Ready(void)  { return g_ready ? 1 : 0; }
extern "C" int  RanGL_DepthBits(void) { return g_depthBits; }

//  True only on the thread that currently holds the context.
extern "C" int RanGL_HasContext(void) {
    return (g_ctxHeld && pthread_equal(pthread_self(), g_ctxThread)) ? 1 : 0;
}

//  Hand the context to whichever thread is about to draw. Releasing from the
//  holder and binding on the taker is the only order EGL allows.
extern "C" void RanGL_ReleaseContext(void) {
    if (g_display == EGL_NO_DISPLAY || !g_ctxHeld) return;
    if (!pthread_equal(pthread_self(), g_ctxThread)) return;
    eglMakeCurrent(g_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    g_ctxHeld = false;
}

extern "C" int RanGL_AcquireContext(void) {
    if (g_display == EGL_NO_DISPLAY || g_context == EGL_NO_CONTEXT) return 0;
    if (g_ctxHeld && pthread_equal(pthread_self(), g_ctxThread)) return 1;
    if (g_ctxHeld) return 0;                    // someone else still has it
    if (!eglMakeCurrent(g_display, g_surface, g_surface, g_context)) {
        LOGE("eglMakeCurrent (acquire) failed: %s", eglErrStr(eglGetError()));
        return 0;
    }
    g_ctxThread = pthread_self();
    g_ctxHeld = true;

    //  The loading screen draws a little of the frame at a time and swaps, so
    //  it needs what it drew last time to still be there. The game thread does
    //  not, and paying for the restore every frame is 16 MB of tile traffic.
    setSwapPreserved(!pthread_equal(pthread_self(), g_mainThread));
    return 1;
}
//  Which framebuffer object IS the screen. On EGL that is 0; on iOS the screen
//  is a framebuffer the app creates around the CAEAGLLayer's renderbuffer, so
//  the two platforms disagree and nothing may bind a literal 0 to go back to
//  the display.
extern "C" unsigned RanGL_DefaultFramebuffer(void) { return 0; }

extern "C" int  RanGL_Width(void)  { return g_width; }
extern "C" int  RanGL_Height(void) { return g_height; }

//  The frame is already the size the client should think it is — the display
//  does the upscale — so these are all the same number now. RanGL_UIScale stays
//  1 and exists so the viewport/clear paths need no special case.
//  g_width/g_height are the frame: panel pixels. The client thinks in logical
//  ones, UIScale times larger, so a PC-sized GUI stays tappable. Anything that
//  touches real pixels (viewport, scissor) multiplies by UIScale; anything that
//  lays out uses the Logical pair.
//  The frame is g_width/g_height. The client lays out at panel/g_renderScale
//  whatever the buffer size is, so the ratio between the two - which is what
//  the viewport and scissor paths multiply by - is what UIScale reports.
extern "C" float RanGL_UIScale(void) {
    const float d = (g_renderScale > 0 ? g_renderScale : 1.0f) / (float)(g_bufferDiv > 0 ? g_bufferDiv : 1);
    return d >= 1.0f ? d : 1.0f;
}
extern "C" int RanGL_LogicalWidth(void)  { return (int)lroundf(g_width  / RanGL_UIScale()); }
extern "C" int RanGL_LogicalHeight(void) { return (int)lroundf(g_height / RanGL_UIScale()); }

//  Panel pixels per logical pixel, for turning a touch into a client coordinate.
extern "C" float RanGL_InputScale(void)  { return g_renderScale > 0 ? g_renderScale : 1.0f; }

//  Time inside the swap, which is where a GPU that cannot keep up shows up:
//  the client calls Present from inside its own Render, so without this the
//  swap hides in the render figure.
static double g_swapSeconds = 0.0;

extern "C" double RanGL_TakeSwapSeconds(void) {
    const double v = g_swapSeconds;
    g_swapSeconds = 0.0;
    return v;
}

//  Defined by the renderer: the frame is over, so the next one starts on a
//  buffer whose contents are not ours.
extern "C" void RanGLR_FrameEnd(void);
extern "C" int RanGLR_FrameDrawCount(void);

extern "C" unsigned RanGL_FrameIndex(void) { return g_frameIndex; }

extern "C" void RanGL_Present(void) {
    if (!g_ready) return;
    ++g_frameIndex;
    //  Temporary: with /sdcard/ran/presentlog present, say who presented each
    //  frame and how much it drew, for a bounded burst.
    {
        static int s_left = 0;
        static bool s_armed = false;
        const bool on = (RanPlat_DiagExists("presentlog"));
        if (on != s_armed) { s_armed = on; if (on) s_left = 150; }
        if (s_left > 0) {
            --s_left;
            LOGI("PRESENT %u tid=%d draws=%d preserved=%d",
                 g_frameIndex, (int)gettid(), RanGLR_FrameDrawCount(),
                 g_swapPreserved ? 1 : 0);
        }
    }
    RanGLR_FrameEnd();
    //  The touch controls go on last, over the finished frame.
    //  The touch controls used to be drawn here, at the end of the frame, which
    //  put them on top of everything including the game's own windows - so an
    //  open inventory appeared underneath the joystick. They are drawn from
    //  DxGameStage now, just before the interface, so every window covers them.
    //  Nothing needs the depth or stencil buffer after the frame is finished, so
    //  say so: a tiled GPU otherwise writes both back out to memory at the end
    //  of every render pass. At 2560x1600 that is 16 MB of pure waste a frame.
    {
        const GLenum unwanted[] = { GL_DEPTH, GL_STENCIL };
        glInvalidateFramebuffer(GL_FRAMEBUFFER, 2, unwanted);
    }

    struct timespec ts0;
    clock_gettime(CLOCK_MONOTONIC, &ts0);
    if (!eglSwapBuffers(g_display, g_surface)) {
        EGLint e = eglGetError();
        // A lost surface is normal on rotate/background; the app layer recreates it.
        LOGE("eglSwapBuffers failed: %s", eglErrStr(e));
    }
    struct timespec ts1;
    clock_gettime(CLOCK_MONOTONIC, &ts1);
    g_swapSeconds += (double)(ts1.tv_sec - ts0.tv_sec) +
                     (double)(ts1.tv_nsec - ts0.tv_nsec) * 1e-9;
}

#endif  //  __ANDROID__
