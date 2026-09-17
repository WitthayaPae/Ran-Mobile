// EGL/GLES context owned by the platform layer, consumed by the D3D9 shim.
#pragma once
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

// Create the context on the CALLING thread and make it current. That thread is
// the frame-loop thread and must stay the only one touching GL.
int  RanGL_Init(void *nativeWindow);

//  Android only: the window comes and goes while the client keeps running.
//
//  Anything that covers the app - the browser the shop's top-up button opens,
//  the recents switcher, a call - destroys the native window, and the EGL
//  surface with it. These two drop and remake the surface while keeping the
//  context, so the textures and buffers already uploaded survive the trip.
//  iOS has no counterpart: its CAEAGLLayer lives as long as the view does.
void RanGL_SurfaceLost(void);
int  RanGL_SurfaceRestore(void *nativeWindow);

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
//  ran off the bottom. So when the whole-number scale leaves under kMinH rows
//  the scale becomes panel height / kMinH, fractional, and the client gets the
//  extra width instead.
static inline float RanGL_ChooseUIScale(int panelW, int panelH) {
    //  640, not 720: 720 fit everything but shrank the phone UI to 1.6375x
    //  ("now it's too small"). The tallest in-game windows are the item shop
    //  (605), rebuild (604), party (600) and inventory (598), so 640 still fits
    //  them all, at 1.842x on an iPhone 15 (1388x640).
    //
    //  A whole scale is kept whenever it leaves at least kMinHWhole rows, and
    //  only then does the fractional fit to kMinH apply. At 1.8422 an iPhone 15
    //  drew every one-texel frame in the interface art as 1 or 2 pixels
    //  depending on where it fell - an icon's frame measured 3 px on its left
    //  and 1 px on its right, with atlas neighbours bleeding in - while Android
    //  at a whole 2x is 2 px on every side. No filter can make a 1.84-pixel
    //  line even; only a whole scale does. 2x there leaves 589 rows, and the
    //  handful of windows taller than that are trimmed for mobile instead
    //  (item shop 605, rebuild 604, party 600, inventory 598). A 1080-row phone
    //  would get 540 at 2x, too short, and keeps the fractional fit.
    const int kMinW = 1100, kMinH = 640, kMinHWhole = 580;
    int s = 1;
    while (panelW / (s + 1) >= kMinW) ++s;
    if (s > 4) s = 4;
    float f = (float)s;
    if (panelH / f < (float)kMinHWhole) f = (float)panelH / (float)kMinH;
    return f < 1.0f ? 1.0f : f;
}
//  Seconds spent inside eglSwapBuffers since the last call, and reset.
double RanGL_TakeSwapSeconds(void);
int  RanGL_Height(void);
void RanGL_Present(void);

#ifdef __cplusplus
}
#endif
