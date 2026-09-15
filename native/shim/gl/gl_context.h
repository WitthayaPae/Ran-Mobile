// EGL/GLES context owned by the platform layer, consumed by the D3D9 shim.
#pragma once
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

// Create the context on the CALLING thread and make it current. That thread is
// the frame-loop thread and must stay the only one touching GL.
int  RanGL_Init(void *nativeWindow);

//  The drawable resized under us - a rotation or a split view. iOS only: the
//  Android window is a fixed landscape surface that never moves, which is why
//  there is no counterpart in gl_context.cpp. Rebuilds the renderbuffers when
//  the size really changed, and does nothing when it did not.
int  RanGL_SurfaceChanged(void);
void RanGL_Shutdown(void);

int  RanGL_Ready(void);
//  Whether the surface keeps its contents across a swap. When it does not, a
//  frame that clears only part of itself would show stale pixels, so the
//  renderer clears everything up front.
int  RanGL_SwapPreserved(void);
int  RanGL_Width(void);

// Depth bits of the chosen config (24, or 16 on the fallback path).
int  RanGL_DepthBits(void);

// The EGL context belongs to one thread at a time. The loading screen renders
// from its own thread, so the context is handed over for the duration.
int  RanGL_HasContext(void);
unsigned RanGL_DefaultFramebuffer(void);
void RanGL_ProbePixel(int x, int y, const char *tag);
void RanGL_ReleaseContext(void);
int  RanGL_AcquireContext(void);
//  Logical size = what the client believes the screen is; the frame is stretched
//  from there to the real panel.
int  RanGL_LogicalWidth(void);
int  RanGL_LogicalHeight(void);
//  Frame pixels per logical pixel. Fractional on phones - see RanGL_ChooseUIScale.
float RanGL_UIScale(void);
//  Panel pixels per logical pixel: touch events arrive in panel pixels.
float RanGL_InputScale(void);

//  Panel pixels per logical pixel, shared by the Android and iOS contexts.
//
//  Whole-number first: the largest scale that keeps the client >= 1100 across,
//  the width a PC GUI needs to stay tappable (tablet 2560x1600 -> 2, 1280x800;
//  LDPlayer 2560x1440 -> 2, 1280x720). A phone is too wide for that to also
//  leave enough height: an iPhone 15 at 2556x1179 got 2, a 1278x589 client, and
//  the inventory (598 tall), the item shop (605) and the party window (600)
//  ran off the bottom. So when the whole-number scale leaves under 720 rows -
//  the height every window is verified at on LDPlayer - the scale becomes
//  panel height / 720, fractional, and the client gets the extra width instead.
static inline float RanGL_ChooseUIScale(int panelW, int panelH) {
    const int kMinW = 1100, kMinH = 720;
    int s = 1;
    while (panelW / (s + 1) >= kMinW) ++s;
    if (s > 4) s = 4;
    float f = (float)s;
    if (panelH / f < (float)kMinH) f = (float)panelH / (float)kMinH;
    return f < 1.0f ? 1.0f : f;
}
//  Seconds spent inside eglSwapBuffers since the last call, and reset.
double RanGL_TakeSwapSeconds(void);
int  RanGL_Height(void);
void RanGL_Present(void);

#ifdef __cplusplus
}
#endif
