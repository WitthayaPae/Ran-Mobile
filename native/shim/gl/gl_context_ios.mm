//  The EAGL half of gl_context.cpp. Same exported API, same bookkeeping, a
//  different way of getting a drawable.
//
//  Everything above this file is unchanged: the renderer, the D3D9 shim and the
//  client all talk to RanGL_* and never learn which platform answered. What
//  differs is only what a "surface" is — an EGLSurface on a native window there,
//  a renderbuffer bound to a CAEAGLLayer here.
//
//  The scale arithmetic is copied deliberately rather than shared: it is six
//  lines, and the two files are meant to be readable side by side.
//
//  NOT YET COMPILED — there is no Mac on this machine.
#ifdef __APPLE__

#import <Foundation/Foundation.h>
#import <QuartzCore/CAEAGLLayer.h>
#import <OpenGLES/EAGL.h>
#import <OpenGLES/EAGLDrawable.h>
#import <OpenGLES/ES3/gl.h>

#include "gl_context.h"
#include "../platform/ran_plat.h"

#include <pthread.h>
#include <time.h>

#define LOGI(...) RanPlat_Log(RANLOG_INFO,  "RanGL", __VA_ARGS__)
#define LOGE(...) RanPlat_Log(RANLOG_ERROR, "RanGL", __VA_ARGS__)

extern "C" void RanGLR_FrameEnd ( void );
extern "C" int  RanGLR_FrameDrawCount ( void );

namespace {

EAGLContext  *g_context = nil;
CAEAGLLayer  *g_layer   = nil;

GLuint g_fbo = 0, g_colorRB = 0, g_depthRB = 0;

int  g_width = 0, g_height = 0;
bool g_ready = false;

int  g_panelWidth = 0, g_panelHeight = 0;
float g_renderScale = 1.0f;
int  g_bufferDiv = 1;
int  g_depthBits = 24;

//  There is no equivalent of EGL_BUFFER_PRESERVED worth using: a retained
//  backing costs a full-screen copy every frame on a tiler, and the renderer
//  already clears everything up front when the surface does not preserve.
bool g_swapPreserved = false;

unsigned g_frameIndex = 0;
double   g_swapSeconds = 0.0;

//  Which thread has the context. EAGL, like EGL, allows exactly one, and the
//  loading screen borrows it.
pthread_t g_ctxThread;
bool      g_ctxHeld = false;
pthread_t g_mainThread;

double nowSeconds ( void )
{
    struct timespec ts;
    clock_gettime ( CLOCK_MONOTONIC, &ts );
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

//  Colour, depth and stencil for the current layer size. Called again whenever
//  the layer resizes, which on iOS is a rotation or a split-view change.
bool makeBuffers ( void )
{
    if (g_fbo) {
        glDeleteFramebuffers ( 1, &g_fbo );
        glDeleteRenderbuffers ( 1, &g_colorRB );
        glDeleteRenderbuffers ( 1, &g_depthRB );
        g_fbo = g_colorRB = g_depthRB = 0;
    }

    glGenFramebuffers ( 1, &g_fbo );
    glBindFramebuffer ( GL_FRAMEBUFFER, g_fbo );

    glGenRenderbuffers ( 1, &g_colorRB );
    glBindRenderbuffer ( GL_RENDERBUFFER, g_colorRB );
    if (![g_context renderbufferStorage:GL_RENDERBUFFER fromDrawable:g_layer]) {
        LOGE ( "renderbufferStorage:fromDrawable failed" );
        return false;
    }
    glFramebufferRenderbuffer ( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                GL_RENDERBUFFER, g_colorRB );

    GLint w = 0, h = 0;
    glGetRenderbufferParameteriv ( GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH,  &w );
    glGetRenderbufferParameteriv ( GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &h );
    g_width = (int)w; g_height = (int)h;

    //  24-bit depth with 8-bit stencil: the shadow and water passes use stencil,
    //  and the EGL config asks for the same pair.
    glGenRenderbuffers ( 1, &g_depthRB );
    glBindRenderbuffer ( GL_RENDERBUFFER, g_depthRB );
    glRenderbufferStorage ( GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h );
    glFramebufferRenderbuffer ( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                GL_RENDERBUFFER, g_depthRB );
    glFramebufferRenderbuffer ( GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                                GL_RENDERBUFFER, g_depthRB );
    g_depthBits = 24;

    const GLenum st = glCheckFramebufferStatus ( GL_FRAMEBUFFER );
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        LOGE ( "framebuffer incomplete: 0x%04x", (unsigned)st );
        return false;
    }
    return true;
}

} // namespace

//  nativeWindow is the CAEAGLLayer the view controller owns.
extern "C" int RanGL_Init ( void *nativeWindow )
{
    if (g_ready) return 1;
    if (!nativeWindow) { LOGE ( "no layer" ); return 0; }

    g_layer = (__bridge CAEAGLLayer *)nativeWindow;
    g_layer.opaque = YES;
    //  Nothing is retained between frames: the client redraws the world every
    //  frame, so a retained backing is bandwidth for no gain.
    g_layer.drawableProperties = @{ kEAGLDrawablePropertyRetainedBacking : @NO,
                                    kEAGLDrawablePropertyColorFormat     : kEAGLColorFormatRGBA8 };

    //  ES 3.0. iOS has no 3.1, which is why the renderer already treats the
    //  separate attribute format as optional - the same fallback that keeps it
    //  working on Android drivers without it.
    g_context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
    if (!g_context || ![EAGLContext setCurrentContext:g_context]) {
        LOGE ( "no ES3 context" );
        return 0;
    }
    g_mainThread = pthread_self ();
    g_ctxThread  = g_mainThread;
    g_ctxHeld    = true;

    const CGFloat scale = g_layer.contentsScale > 0 ? g_layer.contentsScale : 1;
    g_panelWidth  = (int)( g_layer.bounds.size.width  * scale + 0.5 );
    g_panelHeight = (int)( g_layer.bounds.size.height * scale + 0.5 );

    //  The same rule as Android - see RanGL_ChooseUIScale. On an iPhone this is
    //  the fractional case: 2556x1179 lays out at 1561x720.
    g_renderScale = RanGL_ChooseUIScale ( g_panelWidth, g_panelHeight );

    //  A number in the diagnostic file "renderscale" draws smaller and lets the
    //  display stretch it. 1 is the full panel.
    g_bufferDiv = 1;
    {
        FILE *f = RanPlat_DiagExists ( "renderscale" )
                    ? RanPlat_DiagOpen ( "renderscale" ) : NULL;
        if (f) {
            char buf[32] = { 0 };
            if (fread ( buf, 1, sizeof(buf) - 1, f ) > 0) {
                const int v = atoi ( buf );
                //  Only a whole divisor of a whole scale.
                if (v >= 1 && (float)v <= g_renderScale &&
                    g_renderScale == (float)(int)g_renderScale &&
                    (int)g_renderScale % v == 0) g_bufferDiv = v;
            }
            fclose ( f );
        }
    }

    //  contentsScale is how the drawable size is chosen on iOS: there is no
    //  "buffer size" knob, the layer decides. Dividing the scale is what
    //  renderscale means here.
    if (g_bufferDiv > 1) g_layer.contentsScale = scale / g_bufferDiv;

    if (!makeBuffers ()) return 0;

    LOGI ( "panel %dx%d, drawing %dx%d, laid out %dx%d (UI scale %.4f, renderscale %d)",
           g_panelWidth, g_panelHeight, g_width, g_height,
           (int)lroundf ( g_panelWidth / g_renderScale ), (int)lroundf ( g_panelHeight / g_renderScale ),
           g_renderScale / g_bufferDiv, g_bufferDiv );

    g_ready = true;
    return 1;
}

extern "C" void RanGL_Shutdown ( void )
{
    if (!g_ready) return;
    if (g_fbo) {
        glDeleteFramebuffers ( 1, &g_fbo );
        glDeleteRenderbuffers ( 1, &g_colorRB );
        glDeleteRenderbuffers ( 1, &g_depthRB );
        g_fbo = g_colorRB = g_depthRB = 0;
    }
    [EAGLContext setCurrentContext:nil];
    g_context = nil;
    g_layer = nil;
    g_ready = false;
    g_ctxHeld = false;
}

//  Called when the layer's size changed: rotation, or a split view. The Android
//  path never needed this because the window is fixed landscape; iOS resizes the
//  layer under us and the renderbuffer has to follow or the frame is stretched.
extern "C" int RanGL_SurfaceChanged ( void )
{
    if (!g_ready) return 0;
    const CGFloat scale = g_layer.contentsScale > 0 ? g_layer.contentsScale : 1;
    const int w = (int)( g_layer.bounds.size.width  * scale + 0.5 );
    const int h = (int)( g_layer.bounds.size.height * scale + 0.5 );
    if (w == g_width && h == g_height) return 1;
    return makeBuffers () ? 1 : 0;
}

extern "C" int  RanGL_SwapPreserved ( void ) { return g_swapPreserved ? 1 : 0; }
extern "C" int  RanGL_Ready ( void )         { return g_ready ? 1 : 0; }
extern "C" int  RanGL_DepthBits ( void )     { return g_depthBits; }
extern "C" int  RanGL_Width ( void )         { return g_width; }
extern "C" int  RanGL_Height ( void )        { return g_height; }

extern "C" int RanGL_HasContext ( void )
{
    return ( g_ctxHeld && pthread_equal ( g_ctxThread, pthread_self() ) ) ? 1 : 0;
}

//  On EAGL there is no default framebuffer: framebuffer 0 has no attachments
//  and is incomplete, so binding it makes every following draw fail with
//  GL_INVALID_FRAMEBUFFER_OPERATION and render nowhere. The screen is g_fbo.
extern "C" unsigned RanGL_DefaultFramebuffer ( void ) { return g_fbo; }

extern "C" void RanGL_ReleaseContext ( void )
{
    if (!g_ctxHeld) return;
    [EAGLContext setCurrentContext:nil];
    g_ctxHeld = false;
}

extern "C" int RanGL_AcquireContext ( void )
{
    if (!g_context) return 0;
    if (![EAGLContext setCurrentContext:g_context]) {
        LOGE ( "setCurrentContext (acquire) failed" );
        return 0;
    }
    g_ctxThread = pthread_self ();
    g_ctxHeld = true;
    return 1;
}

extern "C" float RanGL_UIScale ( void )
{
    const float d = ( g_renderScale > 0 ? g_renderScale : 1.0f ) /
                    (float)( g_bufferDiv > 0 ? g_bufferDiv : 1 );
    return d >= 1.0f ? d : 1.0f;
}
extern "C" int RanGL_LogicalWidth ( void )  { return (int)lroundf ( g_width  / RanGL_UIScale() ); }
extern "C" int RanGL_LogicalHeight ( void ) { return (int)lroundf ( g_height / RanGL_UIScale() ); }
extern "C" float RanGL_InputScale ( void )  { return g_renderScale > 0 ? g_renderScale : 1.0f; }

extern "C" double RanGL_TakeSwapSeconds ( void )
{
    const double v = g_swapSeconds;
    g_swapSeconds = 0.0;
    return v;
}

extern "C" unsigned RanGL_FrameIndex ( void ) { return g_frameIndex; }

extern "C" void RanGL_Present ( void )
{
    if (!g_ready) return;
    ++g_frameIndex;

    {
        static int  s_left = 0;
        static bool s_armed = false;
        const bool on = RanPlat_DiagExists ( "presentlog" ) != 0;
        if (on != s_armed) { s_armed = on; if (on) s_left = 150; }
        if (s_left > 0) {
            --s_left;
            LOGI ( "PRESENT %u draws=%d", g_frameIndex, RanGLR_FrameDrawCount() );
        }
    }

    RanGLR_FrameEnd ();

    //  Nothing needs depth or stencil once the frame is finished, and saying so
    //  keeps a tiler from writing both back to memory - 16 MB a frame at this
    //  resolution. The colour buffer must NOT be invalidated: it is what is
    //  about to be presented.
    {
        const GLenum unwanted[] = { GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT };
        glInvalidateFramebuffer ( GL_FRAMEBUFFER, 2, unwanted );
    }

    const double t0 = nowSeconds ();
    glBindRenderbuffer ( GL_RENDERBUFFER, g_colorRB );
    if (![g_context presentRenderbuffer:GL_RENDERBUFFER])
        LOGE ( "presentRenderbuffer failed" );
    //  Back to the frame buffer for the next frame's first draw: presenting
    //  leaves the renderbuffer bound, and every draw path assumes the FBO is.
    glBindFramebuffer ( GL_FRAMEBUFFER, g_fbo );
    g_swapSeconds += nowSeconds () - t0;
}

#endif  //  __APPLE__
