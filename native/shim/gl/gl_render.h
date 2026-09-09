// D3D9 fixed-function -> GLES3 translation, consumed by the D3D9 shim device.
#pragma once
#include "windows.h"

#ifdef __cplusplus
extern "C" {
#endif

int  RanGLR_Init(void);
void RanGLR_ClearRect(int x, int y, int w, int h);
void RanGLR_ClearRectOff(void);
void RanGLR_Clear(DWORD flags, D3DCOLOR color, float z, DWORD stencil);
void RanGLR_SetViewport(int x, int y, int w, int h);

// Fixed-function lighting and fog, as the device currently has them set.
//
// `lights` is 8 entries of { type, diffuse rgb, ambient rgb, position+range,
// direction, attenuation }, flattened; `enabled` is a bit per light. World
// matrix and camera position are needed because lighting is evaluated in world
// space, which is where the engine's lights live.
struct RanGlLight {
    int   type;                 // D3DLIGHT_POINT 1, SPOT 2, DIRECTIONAL 3
    float diffuse[3];
    float ambient[3];
    float position[3], range;
    float direction[3];
    float atten[3];             // constant, linear, quadratic
};

// Fixed-function specular, which D3D adds after texturing rather than
// modulating into it. `lightSpecular` is lightCount * 3 floats.
// The display gamma ramp, as GDI's SetDeviceGammaRamp delivers it: 3 * 256
// WORDs (red, green, blue). NULL disables correction.
// Tells the renderer its cached GL state is stale, after something else has
// issued GL against the same context.
void RanGLR_InvalidateStateCache(void);

void RanGLR_SetGammaRamp(const unsigned short *ramp);

void RanGLR_SetSpecular(int enabled, const float *matSpecular, float power,
                        const float *lightSpecular, int lightCount);

void RanGLR_SetLighting(int enabled, const float *worldMatrix, const float *cameraPos,
                        const float *globalAmbient, const float *matDiffuse,
                        const float *matAmbient, const float *matEmissive,
                        const RanGlLight *lights, int lightCount);

// Fog: mode is D3DFOG_NONE/EXP/EXP2/LINEAR, colour is RGB.
void RanGLR_SetFog(int enabled, int mode, const float *color,
                   float start, float end, float density);

// Stage 0's colour/alpha combiner. D3D's fixed function decides per stage
// whether the texture, the vertex colour, or a product of them reaches the
// frame buffer; a shader that always multiplies gets black wherever the engine
// selected the texture alone and the vertex colour happens to be black.
// Stage 1: mode 1 modulates a cube map addressed by the camera-space normal
// into the stage 0 result, which is what the character specular passes ask for.
void RanGLR_SetStage1(int mode, unsigned glCubeTex, unsigned gl2DTex,
                      const float *viewMatrix);

void RanGLR_SetTextureStage(DWORD colorOp, DWORD colorArg1, DWORD colorArg2,
                            DWORD alphaOp, DWORD alphaArg1, DWORD alphaArg2,
                            DWORD texFactor);

// Applies the device's D3D render-state array to GL. Called immediately before
// each draw so state blocks (which replay many states at once) stay correct.
void RanGLR_ApplyState(const DWORD *renderState);

// One draw.
//
// `indexCount` and `vertexCount` are separate on purpose: an indexed draw
// references vertexCount vertices through indexCount indices, and the two are
// unrelated. Using one number for both uploads the wrong slice of the vertex
// buffer, which draws a mesh made of vertices that belong to other meshes.
// `indices` may be NULL for a non-indexed draw; `vertexCount` may be 0 to let
// the primitive type and count imply it.
void RanGLR_Draw(DWORD primType, UINT primCount, const void *verts, UINT stride,
                 DWORD fvf, unsigned glTexture, const float *mvp,
                 const void *indices, UINT indexBits, UINT indexCount,
                 UINT vertexCount);

// `dataSize` is required for the compressed formats, where the byte count
// cannot be derived from width*height alone.
// Draw census for one interval, then reset. Answers "is anything reaching the
// screen, and of what kind" without a per-draw log flood.
void RanGLR_LogStats(void);
double RanGLR_TakeDrawSeconds(void);
//  The same measurement without the reset, so a second reader does not steal
//  it from the first. Seconds since the process started.
double RanGLR_DrawSecondsTotal(void);
unsigned long RanGLR_TakeDrawCount(void);
//  Re-read the diagnostic switches under /sdcard/ran (nulldraw, notex, ...).
void RanGLR_RefreshDiagnostics(void);
//  GL calls issued since the last read, by kind, and reset.
void RanGLR_TakeCallCounts(unsigned long *uniform, unsigned long *texture,
                           unsigned long *attrib, unsigned long *state,
                           unsigned long *draw, unsigned long *buffer);
//  Client vertex/index buffer traffic since the last call, and reset: a buffer
//  re-uploaded whole every frame does not show up as draw submission.
void RanGLR_TakeBufferStats(unsigned long *count, unsigned long *bytes, double *seconds);
//  1 when the GPU accepted the shipped compressed textures, 0 when they had to
//  be decoded on the CPU (four times the memory and texture bandwidth).
int  RanGLR_TexturesCompressed(void);
//  Write only the range the client locked into a buffer GL already sized.
void RanGLR_OrphanBuffer(unsigned buffer, int isIndex, unsigned size);
void RanGLR_ReportBufferKinds(unsigned frames);
void RanGLR_FrameEnd(void);
int  RanGLR_FrameDrawCount(void);
void RanGLR_GpuSectionBegin(const char *name);
void RanGLR_GpuSectionEnd(void);
void RanGLR_ReportGpuSections(unsigned frames);
int  RanGLR_StreamVertices(const void *data, unsigned size,
                           unsigned *outBuffer, unsigned *outOffset);
void RanGLR_UpdateBufferRangeUnsync(unsigned buffer, int isIndex, unsigned offset,
                                    const void *data, unsigned size);
void RanGLR_UpdateBufferRange(unsigned buffer, int isIndex, unsigned offset,
                              const void *data, unsigned size);
void RanGLR_LogTextureStats(void);
void RanGLR_SetRenderTargetTexture(unsigned glTex, int w, int h);

// Copies one render-target texture into another (D3D StretchRect).
int  RanGLR_BlitTexture(unsigned srcTex, int sx0, int sy0, int sx1, int sy1,
                        unsigned dstTex, int dx0, int dy0, int dx1, int dy1,
                        int linear);
void RanGLR_ForgetRenderTarget(unsigned glTex);
unsigned RanGLR_CreateEmptyTexture(void);

// True only on the thread holding the EGL context; GL from any other thread
// silently does nothing.
int  RanGLR_OnRenderThread(void);
void RanGLR_SetVertexBlend(int weightCount, const float *worldMatrices16x4,
                           const float *viewProj16);
void RanGLR_DiagArm(int n);
int  RanGLR_DiagArmed(void);
void RanGLR_DiagVerts(const void *cpuVerts);
void RanGLR_DiagIndices(const void *cpuIndices, UINT indexBits);
void RanGLR_DiagTag(const char *what);
void RanGLR_DiagArmUI(int n);

unsigned RanGLR_CreateBuffer(void);
void RanGLR_UpdateBuffer(unsigned buffer, int isIndex, const void *data, unsigned size);
void RanGLR_DeleteBuffer(unsigned buffer);
void RanGLR_DrawVBO(DWORD primType, UINT primCount, unsigned glVB, UINT vbByteOffset,
                    UINT stride, DWORD fvf, unsigned glTexture, const float *mvp,
                    unsigned glIB, UINT ibByteOffset, UINT indexBits,
                    UINT indexCount, UINT vertexCount);
void RanGLR_SetMaterialAlpha(float a);
void RanGLR_SetSampler(DWORD minFilter, DWORD magFilter, DWORD mipFilter,
                       DWORD addressU, DWORD addressV, DWORD maxAnisotropy);
void RanGLR_ApplySampler(unsigned tex);
unsigned RanGLR_UploadTextureLevel(unsigned existing, int level, int width, int height,
                                   int d3dFormat, const void *bits, unsigned dataSize);
void RanGLR_FinishTexture(unsigned tex, int levels, int d3dFormat);

// Uploads one face of a cube map. face is 0..5 in D3D order (+X, -X, +Y, -Y,
// +Z, -Z); pass the same texture name back for each face and level.
unsigned RanGLR_UploadCubeFaceLevel(unsigned existing, int face, int level,
                                    int width, int height, int d3dFormat,
                                    const void *bits, unsigned dataSize);
void RanGLR_FinishCubeTexture(unsigned tex, int levels);
//  Replace one rectangle of a texture GL already holds, with no mip rebuild:
//  the dynamic-texture path (the font atlas gains a glyph, not an atlas).
void RanGLR_UpdateTextureRect(unsigned tex, int x, int y, int w, int h,
                              int d3dFormat, const void *bits, unsigned pitchBytes);
// (the single-level upload is gone; RanGLR_UploadTextureLevel takes its place)
void     RanGLR_DeleteTexture(unsigned tex);

#ifdef __cplusplus
}
#endif
