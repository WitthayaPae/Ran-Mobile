// EGL/GLES context owned by the platform layer, consumed by the D3D9 shim.
#pragma once

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
int  RanGL_UIScale(void);
//  Panel pixels per frame pixel: touch events arrive in panel pixels.
int  RanGL_InputScale(void);
//  Seconds spent inside eglSwapBuffers since the last call, and reset.
double RanGL_TakeSwapSeconds(void);
int  RanGL_Height(void);
void RanGL_Present(void);

#ifdef __cplusplus
}
#endif
