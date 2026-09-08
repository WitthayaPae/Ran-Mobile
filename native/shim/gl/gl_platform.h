//  What "GL" means on this platform.
//
//  The renderer is written once and compiled for both, but the two platforms
//  disagree about three small things and nothing else:
//
//    * the header path — <GLES3/gl3.h> on Android, <OpenGLES/ES3/gl.h> on iOS;
//    * ES 3.1 — Android may have it, iOS never does. gl31.h simply is not there,
//      so the ES 3.1 entry points have to be declared where the file uses them
//      (it already resolves them at runtime, because plenty of Android drivers
//      lack them too);
//    * how an extension is looked up — eglGetProcAddress on Android; on iOS
//      there is no such call, and no extension this renderer wants exists
//      anyway, so the answer is always NULL and every fallback path is taken.
//
//  Nothing above this header changes. RanGL_ProcAddress is deliberately the
//  same shape as eglGetProcAddress so the call sites read the same.
#pragma once

#if defined(__APPLE__)
  //  #include, not #import: this header is reached from plain C++ translation
  //  units (gl_render.cpp among them), where #import is a clang extension that
  //  warns. The OpenGLES headers are ordinary C headers with their own guards.
  #include <OpenGLES/ES3/gl.h>
  #include <OpenGLES/ES3/glext.h>
#else
  #include <GLES3/gl3.h>
  #include <GLES3/gl31.h>
  #include <EGL/egl.h>
#endif

//  The renderer declares the ES 3.1 and EXT entry points itself, because it
//  resolves them at runtime on every platform. That means it needs the calling
//  convention macro whether or not the platform's headers went as far as 3.1.
#ifndef GL_APIENTRY
#define GL_APIENTRY
#endif

#ifdef __cplusplus
extern "C" {
#endif

//  A GL extension entry point, or NULL when the platform does not have it.
static inline void *RanGL_ProcAddress ( const char *name )
{
#if defined(__APPLE__)
    //  iOS links what it has and has nothing this renderer asks for by name:
    //  no glBufferStorageEXT, no ES 3.1 vertex-attrib-format, no disjoint
    //  timer query. Returning NULL is not a stub - it is the truth, and the
    //  renderer's existing "driver lacks it" paths are what run.
    (void)name;
    return 0;
#else
    return (void *)eglGetProcAddress ( name );
#endif
}

#ifdef __cplusplus
}
#endif
