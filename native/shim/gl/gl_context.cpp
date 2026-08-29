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
#include "../platform/touch_ui.h"

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <pthread.h>
#include <android/native_window.h>

//  What the chosen config actually gave us. Depth bias is expressed in units
//  of the smallest resolvable depth step, so this decides its scale.
static int g_depthBits = 24;
//  Which thread currently has the context bound. EGL allows exactly one, and
//  the loading screen borrows it, so this moves.
static pthread_t g_ctxThread;
static bool      g_ctxHeld = false;

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "RanGL", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "RanGL", __VA_ARGS__)

namespace {

EGLDisplay g_display = EGL_NO_DISPLAY;
EGLSurface g_surface = EGL_NO_SURFACE;
EGLContext g_context = EGL_NO_CONTEXT;
int g_width = 0, g_height = 0;
bool g_ready = false;
//  The real panel, and how much smaller the frame is drawn than the panel.
//  Touch events arrive in panel pixels; everything else works in frame pixels.
int  g_panelWidth = 0, g_panelHeight = 0;
int  g_renderScale = 1;
//  Panel pixels per drawn pixel. 1 is the full panel; 2 halves the buffer and
//  lets the display stretch it. Separate from g_renderScale, which is only ever
//  about how large the GUI is laid out.
int  g_bufferDiv = 1;
bool g_swapPreserved = false;

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

    //  Panel pixels per logical pixel, chosen so the client lays out at >= 1100
    //  across - the width a PC GUI needs to stay tappable.
    g_renderScale = 1;
    while (g_panelWidth / (g_renderScale + 1) >= 1100) ++g_renderScale;
    if (g_renderScale > 4) g_renderScale = 4;

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
        FILE *f = fopen("/sdcard/ran/renderscale", "rb");
        if (f) {
            char buf[16] = { 0 };
            if (fread(buf, 1, sizeof(buf) - 1, f) > 0) {
                const int v = atoi(buf);
                if (v >= 1 && v <= g_renderScale) g_bufferDiv = v;
            }
            fclose(f);
        }
    }

    const int bufferW = g_panelWidth  / g_bufferDiv;
    const int bufferH = g_panelHeight / g_bufferDiv;
    ANativeWindow_setBuffersGeometry(win, bufferW, bufferH, nativeVisual);
    LOGI("panel %dx%d, drawing %dx%d, laid out %dx%d (UI scale %d, renderscale %d)",
         g_panelWidth, g_panelHeight, bufferW, bufferH,
         g_panelWidth / g_renderScale, g_panelHeight / g_renderScale,
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
    g_swapPreserved = eglSurfaceAttrib(g_display, g_surface, EGL_SWAP_BEHAVIOR,
                                       EGL_BUFFER_PRESERVED) == EGL_TRUE;
    if (g_swapPreserved) {
        EGLint behaviour = 0;
        eglQuerySurface(g_display, g_surface, EGL_SWAP_BEHAVIOR, &behaviour);
        g_swapPreserved = (behaviour == EGL_BUFFER_PRESERVED);
    }
    LOGI("swap behaviour: %s", g_swapPreserved ? "preserved" : "destroyed (frames are fully cleared)");

    eglSwapInterval(g_display, 1);

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
    return 1;
}
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
extern "C" int RanGL_UIScale(void) {
    const int d = (g_renderScale > 0 ? g_renderScale : 1) / (g_bufferDiv > 0 ? g_bufferDiv : 1);
    return d > 0 ? d : 1;
}
extern "C" int RanGL_LogicalWidth(void)  { return g_width  / RanGL_UIScale(); }
extern "C" int RanGL_LogicalHeight(void) { return g_height / RanGL_UIScale(); }

//  Panel pixels per frame pixel, for turning a touch into a client coordinate.
extern "C" int RanGL_InputScale(void)    { return g_renderScale > 0 ? g_renderScale : 1; }

//  Time inside the swap, which is where a GPU that cannot keep up shows up:
//  the client calls Present from inside its own Render, so without this the
//  swap hides in the render figure.
static double g_swapSeconds = 0.0;

extern "C" double RanGL_TakeSwapSeconds(void) {
    const double v = g_swapSeconds;
    g_swapSeconds = 0.0;
    return v;
}

extern "C" void RanGL_Present(void) {
    if (!g_ready) return;
    //  The touch controls go on last, over the finished frame.
    //  The touch controls used to be drawn here, at the end of the frame, which
    //  put them on top of everything including the game's own windows - so an
    //  open inventory appeared underneath the joystick. They are drawn from
    //  DxGameStage now, just before the interface, so every window covers them.
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
