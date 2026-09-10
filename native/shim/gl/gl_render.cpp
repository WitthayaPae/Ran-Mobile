// D3D9 fixed-function -> OpenGL ES 3.
//
// The engine has no shaders for its main paths: it sets render states, texture
// stage states and a transform stack, then draws. GLES3 has no fixed function at
// all, so this file is the translation layer:
//
//   * one "uber" shader pair covering the combinations the client actually uses
//   * D3D render state (blend, depth, cull, alpha test) -> GL state
//   * FVF vertex layouts -> vertex attribute pointers
//   * D3DPT_TRIANGLEFAN etc. -> GL primitive modes
//
// Two coordinate details decide whether anything appears in the right place:
//
//   1. D3DFVF_XYZRHW vertices are ALREADY in screen pixels — no matrices apply.
//      They map to clip space directly, and because D3D's Y axis points down
//      while GL's points up, Y is flipped here.
//   2. D3D's depth range is [0,1] and GL ES's is [-1,1]; glDepthRangef(0,1)
//      plus the projection convention keeps comparisons matching.
//
// Textures are uploaded lazily: the engine locks a texture, writes pixels and
// unlocks, so upload happens on first use after a change rather than per-frame.

#include "windows.h"
#include "../platform/ran_plat.h"
#include <d3d9.h>

#include "gl_platform.h"
#include <string.h>
#include <map>
#include <set>
#include <pthread.h>
#include <unistd.h>
#include <vector>

#include "gl_context.h"
#include "gl_render.h"

#define LOGI(...) RanPlat_Log(RANLOG_INFO,  "RanGL", __VA_ARGS__)
#define LOGE(...) RanPlat_Log(RANLOG_ERROR, "RanGL", __VA_ARGS__)

namespace {

// ---------------------------------------------------------------- shaders
// Vertex positions arrive either pre-transformed (UI, XYZRHW) or in world space
// needing world*view*proj. One shader handles both via uPreTransformed.
const char *kVS =
    "#version 300 es\n"
    "layout(location=0) in vec4 aPos;\n"
    "layout(location=1) in vec4 aColor;\n"
    "layout(location=2) in vec2 aUV;\n"
    "layout(location=3) in vec3 aNormal;\n"
    "layout(location=4) in vec3 aBlend;\n"
    //  The second texture coordinate set. Only a few things use it - the moon
    //  picks its phase out of a 2x2 atlas with it - but nothing could before,
    //  because only the first set was ever read off the vertex.
    "layout(location=5) in vec2 aUV2;\n"
    //  Which palette slot each of the vertex's four influences uses. Only read
    //  when the mesh carries them (D3DFVF_LASTBETA_UBYTE4); positional meshes
    //  leave it at zero and take the branch below instead.
    "layout(location=6) in vec4 aBoneIdx;\n"
    "uniform mat4 uMVP;\n"
    "uniform mat4 uWorld;\n"
    "uniform vec2 uViewport;\n"
    "#ifndef uPreTransformed\n"
    "uniform int  uPreTransformed;\n"
    "#endif\n"
    "uniform mat4 uWorldM[16];\n"
    "uniform mat4 uViewProj;\n"
    "#ifndef uVertexBlend\n"
    "uniform int  uVertexBlend;\n"
    "#endif\n"
    "#ifndef uIndexedBlend\n"
    "uniform int  uIndexedBlend;\n"
    "#endif\n"
    "uniform vec3 uCameraPos;\n"
    //  Lighting moved here from the fragment stage.
    //
    //  D3D fixed function lights per vertex and interpolates the result -
    //  Gouraud - so per-pixel lighting was both a departure from the PC client
    //  and the most expensive thing in the frame: eight lights, a normalize, a
    //  pow and a branch for every one of four million pixels.
    "#ifndef uLighting\n"
    "uniform highp int   uLighting;\n"
    "#endif\n"
    "uniform int   uLightCount;\n"
    "uniform int   uLightType[8];\n"
    "uniform vec3  uLightDiffuse[8];\n"
    "uniform vec3  uLightAmbient[8];\n"
    "uniform vec4  uLightPos[8];\n"
    "uniform vec3  uLightDir[8];\n"
    "uniform vec3  uLightAtten[8];\n"
    "uniform vec3  uLightSpecular[8];\n"
    "uniform vec3  uGlobalAmbient;\n"
    "uniform vec3  uMatDiffuse;\n"
    "uniform vec3  uMatAmbient;\n"
    "uniform vec3  uMatEmissive;\n"
    "#ifndef uSpecularOn\n"
    "uniform highp int   uSpecularOn;\n"
    "#endif\n"
    "uniform float uMatPower;\n"
    "uniform int   uHasVertexColor;\n"
    "uniform highp vec3  uCameraPosF;\n"
    //  D3DRS_FOGVERTEXMODE: the client asks for vertex fog and the device
    //  reports it, so the factor belongs here - one distance and one exp per
    //  vertex instead of per pixel.
    "#ifndef uFogMode\n"
    "uniform highp int   uFogMode;\n"
    "#endif\n"
    "uniform float uFogStart;\n"
    "uniform float uFogEnd;\n"
    "uniform float uFogDensity;\n"
    "out float vFog;\n"
    "out vec3 vLit;\n"
    "out vec3 vSpec;\n"
    "out vec4 vColor;\n"
    "out vec2 vUV;\n"
    "out vec2 vUV2;\n"
    "out vec3 vWorldPos;\n"
    "out vec3 vNormal;\n"
    "void main() {\n"
    "    if (uPreTransformed == 1) {\n"
    "        // screen pixels -> clip space, with D3D's downward Y flipped\n"
    "        float x = (aPos.x / uViewport.x) * 2.0 - 1.0;\n"
    "        float y = 1.0 - (aPos.y / uViewport.y) * 2.0;\n"
    "        // D3D clip z is [0,w], GL clip z is [-w,w]: a pre-transformed\n"
    "        // z of 0 means the near plane, and passed through unchanged it\n"
    "        // lands at window depth 0.5 - the middle of whatever the last\n"
    "        // scene left in the depth buffer.\n"
    "        gl_Position = vec4(x, y, aPos.z * 2.0 - 1.0, 1.0);\n"
    "        vWorldPos = vec3(0.0);\n"
    "        vNormal = vec3(0.0, 1.0, 0.0);\n"
    "    } else if (uIndexedBlend == 1) {\n"
    "        //  Indexed blending: three weights and four palette slots, the\n"
    "        //  fourth weight implied as 1 - the others. The slot numbers let a\n"
    "        //  group reference the whole sixteen-matrix palette while any one\n"
    "        //  vertex still blends four, which is what keeps a character to a\n"
    "        //  few draws instead of one per four bones.\n"
    "        float w3 = 1.0 - (aBlend.x + aBlend.y + aBlend.z);\n"
    "        ivec4 bi = ivec4(aBoneIdx + 0.5);\n"
    "        vec4 p4 = vec4(aPos.xyz, 1.0);\n"
    "        vec4 n4 = vec4(aNormal, 0.0);\n"
    "        vec3 pos = aBlend.x * (uWorldM[bi.x] * p4).xyz\n"
    "                 + aBlend.y * (uWorldM[bi.y] * p4).xyz\n"
    "                 + aBlend.z * (uWorldM[bi.z] * p4).xyz\n"
    "                 +      w3  * (uWorldM[bi.w] * p4).xyz;\n"
    "        vec3 nrm = aBlend.x * (uWorldM[bi.x] * n4).xyz\n"
    "                 + aBlend.y * (uWorldM[bi.y] * n4).xyz\n"
    "                 + aBlend.z * (uWorldM[bi.z] * n4).xyz\n"
    "                 +      w3  * (uWorldM[bi.w] * n4).xyz;\n"
    "        gl_Position = uViewProj * vec4(pos, 1.0);\n"
    "        vWorldPos = pos;\n"
    "        vNormal = mat3(uWorld) * nrm;\n"
    "    } else if (uVertexBlend > 0) {\n"
    "        //  Positional blending, for meshes that do not carry slots: the\n"
    "        //  vertex has uVertexBlend weights and the matrix after them takes\n"
    "        //  what is left, against D3DTS_WORLDMATRIX(0..3).\n"
    "        float w[4];\n"
    "        w[0] = aBlend.x; w[1] = aBlend.y; w[2] = aBlend.z; w[3] = 0.0;\n"
    "        float used = 0.0;\n"
    "        for (int i = 0; i < 3; ++i) {\n"
    "            if (i >= uVertexBlend) w[i] = 0.0;\n"
    "            else used += w[i];\n"
    "        }\n"
    "        w[uVertexBlend] = 1.0 - used;\n"
    "        vec3 pos = vec3(0.0);\n"
    "        vec3 nrm = vec3(0.0);\n"
    "        for (int i = 0; i < 4; ++i) {\n"
    "            if (i > uVertexBlend) break;\n"
    "            pos += w[i] * (uWorldM[i] * vec4(aPos.xyz, 1.0)).xyz;\n"
    "            nrm += w[i] * (uWorldM[i] * vec4(aNormal, 0.0)).xyz;\n"
    "        }\n"
    "        gl_Position = uViewProj * vec4(pos, 1.0);\n"
    "        vWorldPos = pos;\n"
    "        vNormal = normalize(nrm);\n"
    "    } else {\n"
    "        gl_Position = uMVP * vec4(aPos.xyz, 1.0);\n"
    "        //  D3D stores a row-vector matrix row by row and GL reads those\n"
    "        //  same bytes as a column-vector one, so v * M_d3d IS M_gl * v:\n"
    "        //  written the GL way, exactly like uMVP above.\n"
    "        vWorldPos = (uWorld * vec4(aPos.xyz, 1.0)).xyz;\n"
    "        vNormal   = normalize((uWorld * vec4(aNormal, 0.0)).xyz);\n"
    "    }\n"
    "    vColor = aColor.bgra;\n"   // D3DCOLOR is B,G,R,A in memory
    "    vUV = aUV;\n"
    "    vUV2 = aUV2;\n"
    "\n"
    "    vFog = 1.0;\n"
    "    if (uFogMode > 0) {\n"
    "        float fd = distance(vWorldPos, uCameraPosF);\n"
    "        if (uFogMode == 3) vFog = (uFogEnd - fd) / max(uFogEnd - uFogStart, 0.0001);\n"
    "        else if (uFogMode == 1) vFog = exp(-uFogDensity * fd);\n"
    "        else vFog = exp(-uFogDensity * uFogDensity * fd * fd);\n"
    "        vFog = clamp(vFog, 0.0, 1.0);\n"
    "    }\n"
    "    vLit = vec3(1.0);\n"
    "    vSpec = vec3(0.0);\n"
    "    if (uLighting == 1) {\n"
    "        vec3 n = normalize(vNormal);\n"
    "        vec3 lit = uMatEmissive + uMatAmbient * uGlobalAmbient;\n"
    "        vec3 spec = vec3(0.0);\n"
    "        vec3 V = normalize(uCameraPosF - vWorldPos);\n"
    "        for (int i = 0; i < 8; ++i) {\n"
    "            if (i >= uLightCount) break;\n"
    "            vec3 L;\n"
    "            float atten = 1.0;\n"
    "            if (uLightType[i] == 3) {\n"          // directional
    "                L = -normalize(uLightDir[i]);\n"
    "            } else {\n"                            // point / spot
    "                vec3 d = uLightPos[i].xyz - vWorldPos;\n"
    "                float dist = length(d);\n"
    "                if (dist > uLightPos[i].w) continue;\n"
    "                L = d / max(dist, 0.0001);\n"
    "                atten = 1.0 / max(uLightAtten[i].x + uLightAtten[i].y * dist +\n"
    "                                  uLightAtten[i].z * dist * dist, 0.0001);\n"
    "                atten = clamp(atten, 0.0, 1.0);\n"
    "            }\n"
    "            float ndotl = max(dot(n, L), 0.0);\n"
    //  D3D takes the diffuse material from the vertex colour when the vertex has
    //  one (D3DMCS_COLOR1, the default) and from the material only when it does
    //  not. The vertex colour is already multiplied in by the texture stage, so
    //  applying the material as well would count it twice.
    "            vec3 md = (uHasVertexColor == 1) ? vec3(1.0) : uMatDiffuse;\n"
    "            lit += atten * (uLightDiffuse[i] * md * ndotl + uLightAmbient[i]);\n"
    "            if (uSpecularOn == 1 && ndotl > 0.0) {\n"
    "                vec3 H = normalize(L + V);\n"                  // Blinn half-vector
    "                float sp = pow(max(dot(n, H), 0.0), max(uMatPower, 1.0));\n"
    "                spec += atten * uLightSpecular[i] * sp;\n"
    "            }\n"
    "        }\n"
    "        vLit = clamp(lit, 0.0, 1.0);\n"
    "        vSpec = spec;\n"
    "    }\n"
    "}\n";

const char *kFS =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec4 vColor;\n"
    "in vec2 vUV;\n"
    "in vec2 vUV2;\n"
    "in vec3 vWorldPos;\n"
    "in vec3 vNormal;\n"
    //  Gouraud: the lighting was worked out per vertex, as D3D's fixed function
    //  does, and only interpolated here.
    "in vec3 vLit;\n"
    "in vec3 vSpec;\n"
    "#ifndef uLighting\n"
    "uniform highp int   uLighting;\n"
    "#endif\n"
    "#ifndef uSpecularOn\n"
    "uniform highp int   uSpecularOn;\n"
    "#endif\n"
    "uniform vec3        uMatSpecular;\n"
    "uniform float       uMatAlpha;\n"
    "uniform highp vec3  uCameraPosF;\n"
    "#ifndef uFogMode\n"
    "uniform highp int   uFogMode;\n"
    "#endif\n"
    "uniform vec3  uFogColor;\n"
    "#ifndef uGammaOn\n"
    "uniform int   uGammaOn;\n"
    "#endif\n"
    "uniform sampler2D uGammaLut;\n"
    "in float vFog;\n"
    "uniform sampler2D uTex;\n"
    "#ifndef uUseTexture\n"
    "uniform int   uUseTexture;\n"
    "#endif\n"
    //  Declared in this stage too: the same uniform is shared across the
    //  program, and the fragment stage needs it to tell interface draws from
    //  world ones.
    //  highp explicitly: the vertex stage defaults an int to highp and the
    //  fragment stage to mediump, and a uniform shared by both stages has to
    //  agree or the program will not link.
    "#ifndef uPreTransformed\n"
    "uniform highp int uPreTransformed;\n"
    "#endif\n"
    "uniform vec2  uTexSize;\n"     // texels of the bound texture, 0 if unknown
    "uniform float uUiSharpen;\n"   // magnification the interface is drawn at
    "#ifndef uAlphaTest\n"
    "uniform int   uAlphaTest;\n"
    "#endif\n"
    //  Measurement only, from /sdcard/ran/plainfs: skip everything after the
    //  texture fetch. If the frame does not get faster, the fragment shader is
    //  not what the GPU is spending its time on and the fill is elsewhere.
    "uniform highp int uPlain;\n"
    "uniform float uAlphaRef;\n"
    "out vec4 oColor;\n"
    "uniform int uColorOp;\n"     // D3DTOP_*, stage 0
    "uniform int uColorArg1;\n"   // D3DTA_*
    "uniform int uColorArg2;\n"
    "uniform int uAlphaOp;\n"
    "uniform int uAlphaArg1;\n"
    "uniform int uAlphaArg2;\n"
    "uniform vec4 uTexFactor;\n"
    "uniform samplerCube uTexCube;\n"
    //  Stage 1 as a plain 2D texture. The character effects put a gloss map
    //  here - hair, armour trim - and modulate it over the stage 0 result.
    "uniform sampler2D uTexStage1;\n"
    "#ifndef uStage1\n"
    "uniform int  uStage1;\n"
    "#endif\n"
    "uniform mat4 uView;\n"
    "\n"
    "vec4 argValue(int arg, vec4 tex, vec4 diffuse) {\n"
    "    int sel = arg & 7;\n"          // low bits pick the source
    "    vec4 v = diffuse;\n"                      // 0 DIFFUSE, and CURRENT at stage 0
    "    if      (sel == 2) v = tex;\n"            // D3DTA_TEXTURE
    "    else if (sel == 3) v = uTexFactor;\n"     // D3DTA_TFACTOR
    "    if ((arg & 16) != 0) v = vec4(1.0) - v;\n"        // D3DTA_COMPLEMENT
    "    if ((arg & 32) != 0) v = vec4(v.a);\n"            // D3DTA_ALPHAREPLICATE
    "    return v;\n"
    "}\n"
    "\n"
    "vec2 sharpUV(vec2 uv) {\n"
    "    //  Bilinear across a whole texel is what makes magnified interface art\n"
    "    //  look mushy: every pixel between two texel centres is a blend. The\n"
    "    //  icons and panels are authored for a 1024x768 screen and are drawn\n"
    "    //  around twice that here, so almost every pixel is such a blend.\n"
    "    //\n"
    "    //  Squeezing the interpolation into roughly one output pixel instead\n"
    "    //  keeps the smooth ramp where a texel boundary genuinely falls between\n"
    "    //  output pixels, and makes everything else flat. Nearest sampling would\n"
    "    //  also be crisp, but it would put the jagged stair-steps back.\n"
    "    vec2 t = uv * uTexSize;\n"
    "    vec2 i = floor(t) + 0.5;\n"
    "    vec2 f = clamp((t - i) * uUiSharpen, -0.5, 0.5);\n"
    "    return (i + f) / uTexSize;\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    //  Interface only. World geometry is as often minified as magnified,\n"
    "    //  and this is a magnification filter.\n"
    "    vec2 uvS = (uPreTransformed == 1 && uUseTexture == 1 &&\n"
    "                uTexSize.x > 1.0 && uUiSharpen > 1.0) ? sharpUV(vUV) : vUV;\n"
    "    vec4 tex = (uUseTexture == 1) ? texture(uTex, uvS) : vec4(1.0);\n"
    "    if (uPlain == 1) { oColor = tex * vColor; return; }\n"
    "    //  With lighting on, the pipeline's diffuse alpha is the material's;\n"
    "    //  the vertex colour only carries it for unlit geometry.\n"
    "    vec4 diffuse = vec4(vColor.rgb, uLighting == 1 ? uMatAlpha : vColor.a);\n"
    "    vec4 a1 = argValue(uColorArg1, tex, diffuse);\n"
    "    vec4 a2 = argValue(uColorArg2, tex, diffuse);\n"
    "    vec3 rgb;\n"
    "    if      (uColorOp == 2)  rgb = a1.rgb;\n"                    // SELECTARG1
    "    else if (uColorOp == 3)  rgb = a2.rgb;\n"                    // SELECTARG2
    "    else if (uColorOp == 5)  rgb = a1.rgb * a2.rgb * 2.0;\n"     // MODULATE2X
    "    else if (uColorOp == 6)  rgb = a1.rgb * a2.rgb * 4.0;\n"     // MODULATE4X
    "    else if (uColorOp == 7)  rgb = a1.rgb + a2.rgb;\n"           // ADD
    "    else if (uColorOp == 1)  rgb = vec3(1.0);\n"                 // DISABLE
    "    else                     rgb = a1.rgb * a2.rgb;\n"           // MODULATE
    "\n"
    "    //  Stage 1, as the character specular passes configure it: a cube map\n"
    "    //  modulated into the stage 0 result, addressed by the camera-space\n"
    "    //  normal (D3DTSS_TCI_CAMERASPACENORMAL with D3DTTFF_COUNT3).\n"
    "    if (uStage1 == 1) {\n"
    "        vec3 cn = normalize(mat3(uView) * normalize(vNormal));\n"
    "        rgb *= texture(uTexCube, cn).rgb;\n"
    "    } else if (uStage1 == 3) {\n"
    "        //  D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR: the cube is addressed\n"
    "        //  by the view vector reflected about the normal, both in camera\n"
    "        //  space. This is the environment reflection on shiny pieces.\n"
    "        vec3 vn = normalize(mat3(uView) * normalize(vNormal));\n"
    "        vec3 vp = (uView * vec4(vWorldPos, 1.0)).xyz;\n"
    "        rgb *= texture(uTexCube, reflect(normalize(vp), vn)).rgb;\n"
    "    } else if (uStage1 == 5) {\n"
    "        //  MODULATE2X(TEXTURE, CURRENT) with a 2D texture on stage 1 and\n"
    "        //  coordinate set 0. This is the shine on hair and on the coloured\n"
    "        //  parts of a character: without it they are flat paint.\n"
    "        rgb *= 2.0 * texture(uTexStage1, vUV).rgb;\n"
    "    } else if (uStage1 == 2) {\n"
    "        //  MODULATE(TFACTOR, CURRENT): a flat tint over the stage 0 result,\n"
    "        //  with no texture on the stage at all. This is how the ambient\n"
    "        //  character effect colours a piece.\n"
    "        rgb *= uTexFactor.rgb;\n"
    "    }\n"
    "\n"
    "    vec4 b1 = argValue(uAlphaArg1, tex, diffuse);\n"
    "    vec4 b2 = argValue(uAlphaArg2, tex, diffuse);\n"
    "    float alpha;\n"
    "    if      (uAlphaOp == 2)  alpha = b1.a;\n"
    "    else if (uAlphaOp == 3)  alpha = b2.a;\n"
    "    else if (uAlphaOp == 5)  alpha = b1.a * b2.a * 2.0;\n"
    "    else if (uAlphaOp == 1)  alpha = diffuse.a;\n"   // DISABLE: pipeline alpha
    "    else                     alpha = b1.a * b2.a;\n"
    "\n"
    "    //  Stage 1 taking its alpha from the same texture, addressed by the\n"
    "    //  second coordinate set. The moon is drawn this way: the sheet holds\n"
    "    //  four phases in a 2x2 grid, stage 0 samples it for colour with set 0\n"
    "    //  and stage 1 picks the current phase's quadrant with set 1 and uses\n"
    "    //  only its alpha as the mask. Reading set 0 for both meant the mask\n"
    "    //  was the whole sheet, and all four moons showed at once.\n"
    "    if (uStage1 == 4) alpha = texture(uTex, vUV2).a * diffuse.a;\n"
    "\n"
    "    vec4 c = vec4(rgb, alpha);\n"
    "\n"
    "    if (uLighting == 1) {\n"
    "        c.rgb *= vLit;\n"
    "        if (uSpecularOn == 1) c.rgb += uMatSpecular * vSpec;\n"
    "    }\n"
    "\n"
    "    if (uAlphaTest == 1 && c.a < uAlphaRef) discard;\n"
    "\n"
    "    if (uFogMode > 0) c.rgb = mix(uFogColor, c.rgb, vFog);\n"
    "    //  The display gamma ramp the client asked for. Applied here because\n"
    "    //  everything the client draws passes through this shader, which makes\n"
    "    //  it equivalent to programming the display LUT.\n"
    "    if (uGammaOn == 1) {\n"
    "        c.r = texture(uGammaLut, vec2(c.r, 0.5)).r;\n"
    "        c.g = texture(uGammaLut, vec2(c.g, 0.5)).g;\n"
    "        c.b = texture(uGammaLut, vec2(c.b, 0.5)).b;\n"
    "    }\n"
    "    oColor = c;\n"
    "}\n";

//  GL calls issued per frame, by kind. Counting them is exact where skipping
//  them is not: skipping a uniform upload only moves the work, because the
//  cache that suppressed the next one no longer matches.
unsigned long g_callsUniform = 0, g_callsTexture = 0, g_callsAttrib = 0,
              g_callsState = 0, g_callsDraw = 0, g_callsBuffer = 0;

//  Last value sent for each uniform, so a redundant upload becomes a compare.
//  One shader program, so the location is a stable key — and a small dense one,
//  which makes a flat array the right structure: this is checked tens of
//  thousands of times a frame and a map lookup showed up in the frame time.
struct UniformSlot {
    int   count;
    //  Largest entry is the packed light block: eight lights x 17 floats.
    float values[8 * 17];
    UniformSlot() : count(0) {}
};
std::vector<UniformSlot> g_uniformCache;

bool uniformChanged(GLint loc, const float *values, int count) {
    if (loc < 0 || count > (int)(sizeof(((UniformSlot *)0)->values) / sizeof(float))) return loc >= 0;
    if ((int)g_uniformCache.size() <= loc) g_uniformCache.resize(loc + 1);
    UniformSlot &slot = g_uniformCache[loc];
    if (slot.count == count && memcmp(slot.values, values, sizeof(float) * count) == 0)
        return false;
    slot.count = count;
    memcpy(slot.values, values, sizeof(float) * count);
    return true;
}

void setUniform1i(GLint loc, GLint v) {
    const float f = (float)v;
    if (uniformChanged(loc, &f, 1)) { glUniform1i(loc, v); ++g_callsUniform; }
}

void setUniform2f(GLint loc, GLfloat x, GLfloat y) {
    const float v[2] = { x, y };
    if (uniformChanged(loc, v, 2)) { glUniform2f(loc, x, y); ++g_callsUniform; }
}

void setUniform1f(GLint loc, GLfloat v) {
    if (uniformChanged(loc, &v, 1)) { glUniform1f(loc, v); ++g_callsUniform; }
}

//  Polygon offset, cached: it changes rarely and every GL call is measurable.
float g_poFactor = 0.0f, g_poUnits = 0.0f;
bool  g_poOn = false;

void setPolygonOffset(float factor, float units) {
    const bool want = (factor != 0.0f || units != 0.0f);
    if (want != g_poOn) {
        g_poOn = want;
        if (want) glEnable(GL_POLYGON_OFFSET_FILL);
        else      glDisable(GL_POLYGON_OFFSET_FILL);
        ++g_callsState;
    }
    if (want && (factor != g_poFactor || units != g_poUnits)) {
        g_poFactor = factor; g_poUnits = units;
        glPolygonOffset(factor, units);
        ++g_callsState;
    }
}

void setUniformVec4(GLint loc, const float *v) {
    if (uniformChanged(loc, v, 4)) { glUniform4fv(loc, 1, v); ++g_callsUniform; }
}

void setUniform3fv(GLint loc, const float *v) {
    if (uniformChanged(loc, v, 3)) { glUniform3fv(loc, 1, v); ++g_callsUniform; }
}

void setUniformMatrix(GLint loc, const float *m, int count) {
    if (uniformChanged(loc, m, 16 * count)) { glUniformMatrix4fv(loc, count, GL_FALSE, m); ++g_callsUniform; }
}

//  The program is recreated only on init; drop the cache with it.
void resetUniformCache() { g_uniformCache.clear(); }

GLuint g_prog = 0;
GLint  uWorld = -1, uCameraPos = -1, uCameraPosF = -1,
       uLighting = -1, uLightCount = -1, uGlobalAmbient = -1,
       uMatDiffuse = -1, uHasVertexColor = -1, uMatAmbient = -1, uMatEmissive = -1,
       uSpecularOn = -1, uMatSpecular = -1, uMatPower = -1, uLightSpecular = -1,
       uLightType = -1, uLightDiffuse = -1, uLightAmbient = -1,
       uLightPos = -1, uLightDir = -1, uLightAtten = -1,
       uFogMode = -1, uFogColor = -1, uFogStart = -1, uFogEnd = -1, uFogDensity = -1;
extern "C" void RanDiag_Backtrace(char *out, size_t cap);
GLint  uMatAlpha = -1;
//  D3DRS_TEXTUREFACTOR as four floats. White is the D3D default, and is what a
//  stage selecting TFACTOR gets until the engine sets one.
float  g_texFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
//  Stage 1: 0 off, 1 cube map addressed by the camera-space normal. That is the
//  only configuration the engine asks for; anything else is reported, not guessed.
//  Every texture's base-level size, so a draw can tell the shader how many
//  texels it is magnifying.
std::map<unsigned, std::pair<int, int> > g_texDims;

bool   g_noUiSharp = false;
bool   g_plainFS = false;
int    g_fsProbe = 0;
//  Whether characters are drawn into the water reflection. Off by default on
//  this port; see RanGLR_ReflectChars below.
bool   g_reflectChars = false;
//  How many characters may cast a shadow in one frame, and how many slots are
//  left in the frame being built. See RanGLR_TakeShadowSlot.
int    g_shadowBudget = 6;
int    g_shadowLeft = 0;
int    g_stage1Mode = 0;
unsigned g_stage1Cube = 0;
float  g_viewMatrix[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
GLint  uFlipY = -1, uWorldM = -1, uViewProj = -1, uVertexBlend = -1, uIndexedBlend = -1;
GLint  uTexStage1 = -1;
//  The 2D texture bound to stage 1, on its own unit so the cube map can keep
//  unit 1 and stage 0 can keep unit 0.
unsigned g_stage1Tex2D = 0;
//  Set from the FVF of the mesh being drawn: it carries palette slots or it
//  does not, and the two blends are not interchangeable.
int    g_indexedBlend = 0;
GLint  uMVP = -1, uViewport = -1, uPreTransformed = -1, uTex = -1,
       uUseTexture = -1, uAlphaTest = -1, uAlphaRef = -1,
       uColorOp = -1, uColorArg1 = -1, uColorArg2 = -1,
       uAlphaOp = -1, uAlphaArg1 = -1, uAlphaArg2 = -1, uTexFactor = -1,
       uTexCube = -1, uStage1 = -1, uView = -1,
       uTexSize = -1, uUiSharpen = -1,
       uGammaOn = -1, uGammaLut = -1, uPlain = -1;
GLuint g_vbo = 0, g_ibo = 0, g_vao = 0;
// Stage-0 combiner, mirroring the device's texture stage state.
DWORD g_colorOp = 4 /*MODULATE*/, g_colorArg1 = 2 /*TEXTURE*/, g_colorArg2 = 0 /*DIFFUSE*/;
DWORD g_alphaOp = 4, g_alphaArg1 = 2, g_alphaArg2 = 0;

// Fixed-function lighting and fog, as last set by the device.
int   g_lightingOn = 0, g_lightCount = 0;
//  Fixed-function vertex blending: how many weights the vertices carry (0 = off)
//  and the world matrices the palette slots point at.
int   g_vertexBlend = 0;
float g_worldM[16 * 16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1,
                       1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1,
                       1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1,
                       1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
float g_viewProj[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
float g_world[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
float g_cameraPos[3] = { 0, 0, 0 };
int   g_specularOn = 0;
//  The ramp the client last set, as a 256x1 RGB texture on its own unit.
unsigned g_gammaLut = 0;
int      g_gammaOn = 0;
unsigned char g_gammaBytes[256 * 3] = { 0 };
bool     g_gammaDirty = false;
float g_matSpecular[3] = { 0, 0, 0 }, g_matPower = 1.0f;
float g_lightSpecular[8 * 3] = { 0 };
float g_globalAmbient[3] = { 0, 0, 0 };
float g_matDiffuse[3] = { 1, 1, 1 }, g_matAmbient[3] = { 1, 1, 1 }, g_matEmissive[3] = { 0, 0, 0 };
float g_matAlpha = 1.0f;
int   g_lightType[8] = { 0 };
float g_lightDiffuse[24] = { 0 }, g_lightAmbient[24] = { 0 };
float g_lightPos[32] = { 0 }, g_lightDir[24] = { 0 }, g_lightAtten[24] = { 0 };

int   g_fogMode = 0;
float g_fogColor[3] = { 0, 0, 0 }, g_fogStart = 0.0f, g_fogEnd = 1.0f, g_fogDensity = 0.0f;

DWORD g_dsBlend=0,g_dsSrc=0,g_dsDst=0,g_dsZ=0,g_dsZW=0,g_dsCull=0,g_dsATest=0,g_dsARef=0;
int g_diagDraws = 0, g_diagUI = 0, g_diagUpload = 0;

//  Stop the frame after N draws, so a screenshot at successive N says which
//  draw put a given thing on the screen. The number is read out of the file
//  rather than the file merely existing, so it can be bisected without a
//  relaunch - which matters when getting back into the world costs a login.
//  -1 leaves every draw alone.
int g_drawLimit = -1;
int g_frameDraw = 0;
//  Whether this frame has cleared the colour buffer yet.
//
//  Without preservation the buffer a frame starts on holds whatever was in it
//  two swaps ago - and the surface has more than two, so that can be a frame
//  from another screen entirely. If the client draws without clearing first,
//  that stale image shows through: entering the world alternated between the
//  world and the loading screen still sitting in the other buffer, which is the
//  flicker. So a frame that has not cleared gets one before its first draw.
bool g_frameClearedColor = false;
//  One frame's worth of "what was draw number N", from /sdcard/ran/drawlog.
//
//  The draw-limit sweep says which range of draws costs the frame; this says
//  what those draws are. Written for one frame only, because it is one line per
//  draw and there are hundreds.
int g_drawLog = 0;

//  How much of the frame is drawn into an off-screen target rather than
//  straight at the panel, and how big the largest such target is.
//
//  It matters for sharpness: a render target is created at whatever size the
//  client asks for, which is its own logical size. Now that the frame itself is
//  the full panel, anything that goes through a target is drawn at half and
//  magnified back up - so if the scene renders into one, drawing the frame at
//  panel resolution buys nothing for it.
unsigned long g_rtDraws = 0;
int g_rtBiggestW = 0, g_rtBiggestH = 0;
bool g_drawDump = false;      // /sdcard/ran/drawdump, one burst per touch
const void *g_diagVerts = NULL;   // CPU copy of a buffer-sourced draw, dump only
const void *g_diagIndices = NULL; // and its indices, so a subset walk is possible
UINT g_diagIndexBits = 16;
char g_diagTag[512] = "?";         // what the engine says it is drawing

//  Render targets. The client draws the character portrait, the cube map and
//  several effect passes into textures; without a real off-screen target those
//  passes -- and the full-screen black Clear that opens them -- land on the
//  visible frame and wipe the scene that was just drawn.
struct RanRT { GLuint fbo = 0, depth = 0; int w = 0, h = 0; };
std::map<GLuint, RanRT> g_rts;
bool g_rtActive = false;
int  g_rtW = 0, g_rtH = 0;
//  Which framebuffer the renderer is drawing into, so a blit can put it back.
GLuint g_rtFbo = 0;

int curWidth()  { return g_rtActive ? g_rtW : RanGL_LogicalWidth(); }
int curHeight() { return g_rtActive ? g_rtH : RanGL_LogicalHeight(); }
//  Bumped whenever the sampler state changes, so a draw can tell in one
//  compare whether the bound texture still has the right parameters. Defined
//  here because the draw path sits above the sampler code.
DWORD g_samplerGeneration = 1;
unsigned long g_texUploads = 0, g_texBytes = 0;
GLenum g_texLastError = 0;
GLenum g_frontFace = GL_CW;   // winding the current D3D cull mode leaves visible
bool   g_cullWanted = false;  // D3DRS_CULLMODE, applied per draw

//  Everything the draw path binds or toggles, as last actually sent to GL.
//  Cleared on init; nothing else in the shim talks to GL behind its back except
//  the texture and render-target paths, which reset the pieces they touch.
struct GlState {
    GLuint program;
    GLuint vao;
    GLuint arrayBuffer;
    GLuint elementBuffer;
    GLuint texture2D;

    int    blendEnabled;
    GLenum blendSrc, blendDst;
    int    depthTest;
    GLenum depthFunc;
    int    depthMask;
    int    cullEnabled;
    GLenum frontFace;

    //  The vertex layout a draw asked for: same FVF, stride and base means the
    //  attribute pointers are already right.
    DWORD  fvf;
    UINT   stride;
    GLsizei vertexBase;
    GLuint vertexBuffer;

    void reset() { memset(this, 0, sizeof(*this)); depthFunc = 0; fvf = 0xFFFFFFFF; }
};
GlState g_gl;

void useProgram(GLuint p)  { if (g_gl.program != p) { glUseProgram(p); g_gl.program = p; } }

//  Unit 0's binding is cached so a run of draws sharing a texture costs one
//  glBindTexture. Every bind of unit 0 has to go through here: an upload path
//  that binds behind the cache's back leaves the cache naming one texture
//  while the sampler holds another, and the next draw both samples the wrong
//  image and writes its sampler state onto that one.
void bindTex2D(GLuint t) {
    if (g_gl.texture2D == t) return;
    glBindTexture(GL_TEXTURE_2D, t);
    g_gl.texture2D = t;
}
//  Deleting a bound texture unbinds it in GL, so the cache has to forget it or
//  it would skip the bind that puts a real texture back.
void forgetTex2D(GLuint t) { if (g_gl.texture2D == t) g_gl.texture2D = 0; }
//  The element array binding lives inside the vertex array object: switching
//  VAOs changes which index buffer is bound without any glBindBuffer of ours,
//  so a cached "already bound" shortcut can leave a draw with no index buffer
//  at all - which the emulator's GL encoder turns into a client-pointer draw
//  and a null dereference. Forget the cached value whenever the VAO changes.
void bindVAO(GLuint v)     { if (g_gl.vao != v) { glBindVertexArray(v); g_gl.vao = v;
                                                  g_gl.elementBuffer = 0xFFFFFFFFu; } }
void bindArray(GLuint b)   { if (g_gl.arrayBuffer != b) { glBindBuffer(GL_ARRAY_BUFFER, b); g_gl.arrayBuffer = b; ++g_callsAttrib; } }
void bindElements(GLuint b){ if (g_gl.elementBuffer != b) { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b); g_gl.elementBuffer = b; ++g_callsAttrib; } }

void setBlend(bool on, GLenum src, GLenum dst) {
    if (g_gl.blendEnabled != (int)on) {
        if (on) glEnable(GL_BLEND); else glDisable(GL_BLEND);
        g_gl.blendEnabled = on;
    }
    if (on && (g_gl.blendSrc != src || g_gl.blendDst != dst)) {
        glBlendFunc(src, dst);
        g_gl.blendSrc = src; g_gl.blendDst = dst;
    }
}

void setDepth(bool test, GLenum func, bool write) {
    if (g_gl.depthTest != (int)test) {
        if (test) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        g_gl.depthTest = test;
    }
    if (test && g_gl.depthFunc != func) { glDepthFunc(func); g_gl.depthFunc = func; }
    if (g_gl.depthMask != (int)write) { glDepthMask(write ? GL_TRUE : GL_FALSE); g_gl.depthMask = write; }
}

void setCull(bool on, GLenum front) {
    if (g_gl.cullEnabled != (int)on) {
        if (on) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
        g_gl.cullEnabled = on;
        if (on) glCullFace(GL_BACK);
    }
    if (on && g_gl.frontFace != front) { glFrontFace(front); g_gl.frontFace = front; }
}

unsigned long g_drawCalls = 0, g_uiDraws = 0, g_texturedDraws = 0, g_vertsDrawn = 0;
//  Texture traffic: full chain uploads against partial rectangle updates,
//  the difference between a glyph costing an atlas and costing a scanline.
unsigned long g_texUpdates = 0, g_texUpdateBytes = 0, g_texFullUploads = 0;
//  Separate attribute format (ES 3.1). The format of a vertex - which
//  attribute sits at which offset - changes only when the FVF changes, while
//  the buffer and the offset within it change on nearly every draw. Describing
//  them separately turns ten glVertexAttribPointer calls per draw into one
//  glBindVertexBuffer, which is most of what draw submission costs.
typedef void (GL_APIENTRY *PFN_VAFORMAT)(GLuint, GLint, GLenum, GLboolean, GLuint);
typedef void (GL_APIENTRY *PFN_VABINDING)(GLuint, GLuint);
typedef void (GL_APIENTRY *PFN_BINDVB)(GLuint, GLuint, GLintptr, GLsizei);

//  GL_EXT_buffer_storage: immutable storage that can stay mapped while the GPU
//  reads it.
#ifndef GL_MAP_PERSISTENT_BIT_EXT
#define GL_MAP_PERSISTENT_BIT_EXT 0x0040
#define GL_MAP_COHERENT_BIT_EXT   0x0080
#endif
typedef void (GL_APIENTRY *PFN_BUFSTORAGE)(GLenum, GLsizeiptr, const void *, GLbitfield);
PFN_BUFSTORAGE p_glBufferStorageEXT = NULL;
bool g_havePersistentMap = false;

PFN_VAFORMAT  p_glVertexAttribFormat  = NULL;
PFN_VABINDING p_glVertexAttribBinding = NULL;
PFN_BINDVB    p_glBindVertexBuffer    = NULL;
bool g_haveAttribFormat = false;

//  Diagnostic: with /sdcard/ran/nulldraw present, every state change still
//  happens but the draw itself is dropped. What is left of the frame is the
//  CPU work the client and the shim do regardless of the GPU - the part that
//  costs the same on a fast desktop emulator and on a tablet.
bool g_nullDraw = false;
//  Finer diagnostics, same mechanism: each file removes one class of GL call
//  from the draw path so its share of the frame can be measured directly.
bool g_skipUniform = false;   // /sdcard/ran/nouniform
bool g_skipTex     = false;   // /sdcard/ran/notex
bool g_skipAttr    = false;   // /sdcard/ran/noattr
bool g_skipStream  = false;   // /sdcard/ran/nostream
bool g_skipBlend   = false;   // /sdcard/ran/noblend
bool g_cpuSkin     = false;   // /sdcard/ran/cpuskin
bool g_noAttribFmt = false;   // /sdcard/ran/noattribformat

GLuint g_whiteTex = 0;          // stands in when no texture is bound
bool   g_inited = false;
//  Only one thread owns the EGL context; GL from any other does nothing.
pthread_t g_renderThread;
bool   g_renderThreadKnown = false;

GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        glGetShaderInfoLog(s, sizeof(log) - 1, NULL, log);
        LOGE("shader compile failed: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

// D3D blend factor -> GL. Only the factors the client actually sets are mapped;
// anything else falls back to ONE/ZERO rather than guessing.
GLenum blendFactor(DWORD d3d) {
    switch (d3d) {
        case D3DBLEND_ZERO:            return GL_ZERO;
        case D3DBLEND_ONE:             return GL_ONE;
        case D3DBLEND_SRCCOLOR:        return GL_SRC_COLOR;
        case D3DBLEND_INVSRCCOLOR:     return GL_ONE_MINUS_SRC_COLOR;
        case D3DBLEND_SRCALPHA:        return GL_SRC_ALPHA;
        case D3DBLEND_INVSRCALPHA:     return GL_ONE_MINUS_SRC_ALPHA;
        case D3DBLEND_DESTALPHA:       return GL_DST_ALPHA;
        case D3DBLEND_INVDESTALPHA:    return GL_ONE_MINUS_DST_ALPHA;
        case D3DBLEND_DESTCOLOR:       return GL_DST_COLOR;
        case D3DBLEND_INVDESTCOLOR:    return GL_ONE_MINUS_DST_COLOR;
        case D3DBLEND_SRCALPHASAT:     return GL_SRC_ALPHA_SATURATE;
        default:                       return GL_ONE;
    }
}

GLenum cmpFunc(DWORD d3d) {
    switch (d3d) {
        case D3DCMP_NEVER:        return GL_NEVER;
        case D3DCMP_LESS:         return GL_LESS;
        case D3DCMP_EQUAL:        return GL_EQUAL;
        case D3DCMP_LESSEQUAL:    return GL_LEQUAL;
        case D3DCMP_GREATER:      return GL_GREATER;
        case D3DCMP_NOTEQUAL:     return GL_NOTEQUAL;
        case D3DCMP_GREATEREQUAL: return GL_GEQUAL;
        default:                  return GL_ALWAYS;
    }
}

} // namespace

// ------------------------------------------------------------------- setup
//  The diagnostic switches are re-read while the game runs, so a measurement
//  can be taken without restarting and logging in again - which matters when
//  the server drops a session on every reconnect.
extern "C" void RanD3D_ProbeTextures(void);

extern "C" void RanGLR_RefreshDiagnostics(void) {
    struct { const char *name; bool *flag; const char *what; } diag[] = {
        { "nulldraw",  &g_nullDraw,    "every GL call a draw makes" },
        { "nouniform", &g_skipUniform, "uniform uploads" },
        { "notex",     &g_skipTex,     "texture binds and sampler state" },
        { "noattr",    &g_skipAttr,    "vertex attribute setup" },
        { "nostream",  &g_skipStream,  "streaming buffer writes" },
        { "noblend",   &g_skipBlend,   "vertex blending (each group rides its first bone)" },
        { "cpuskin",   &g_cpuSkin,     "GPU skinning (the blend is done on the CPU instead)" },
        { "noattribformat", &g_noAttribFmt, "ES 3.1 separate attribute format" },
        { "nouisharp", &g_noUiSharp, "the sharper magnification filter on interface art" },
        { "plainfs",   &g_plainFS,   "everything the fragment shader does after the texture fetch" },
        { "reflectchars", &g_reflectChars, "NOT skipping character reflections (they are skipped by default)" },
    };

    //  A one-shot readback of every loaded texture. Same re-arm as the draw
    //  dump: delete the file and touch it again.
    {
        static bool s_probe = false;
        const bool on = (RanPlat_DiagExists("texprobe"));
        if (on != s_probe) {
            s_probe = on;
            if (on) RanD3D_ProbeTextures();
        }
    }

    {
        int probe = 0;
        FILE *f = (RanPlat_DiagExists("fsprobe"))
                      ? RanPlat_DiagOpen("fsprobe") : NULL;
        if (f) {
            char buf[16] = { 0 };
            if (fread(buf, 1, sizeof(buf) - 1, f) > 0) probe = atoi(buf);
            fclose(f);
        }
        if (probe != g_fsProbe) {
            g_fsProbe = probe;
            LOGI("diagnostic: fragment probe %d", g_fsProbe);
        }
    }

    {
        //  Delete the file and touch it again to take another frame.
        static bool s_logArmed = false;
        const bool on = (RanPlat_DiagExists("drawlog"));
        if (on != s_logArmed) {
            s_logArmed = on;
            //  Two, because the clear that starts the logged frame takes one:
            //  arming lands mid-frame, so the first clear only opens the frame
            //  that gets logged and the second closes it.
            if (on) g_drawLog = 2;
        }
    }

    {
        int limit = -1;
        //  access() first: the file resolver logs every failed open, and this is
        //  polled once a second whether the file is there or not.
        FILE *f = (RanPlat_DiagExists("drawlimit"))
                      ? RanPlat_DiagOpen("drawlimit") : NULL;
        if (f) {
            char buf[32] = { 0 };
            if (fread(buf, 1, sizeof(buf) - 1, f) > 0) limit = atoi(buf);
            fclose(f);
        }
        if (limit != g_drawLimit) {
            g_drawLimit = limit;
            LOGI("diagnostic: draw limit %d", g_drawLimit);
        }
    }

    //  Not a skip: a one-shot burst of per-draw detail, re-armed by deleting
    //  the file and touching it again.
    {
        //  access(), not fopen(): the resolver logs every failed open, and these
        //  are probed once a second whether the file is there or not.
        const bool on = (RanPlat_DiagExists("drawdump"));
        if (on != g_drawDump) {
            g_drawDump = on;
            if (on) {
                g_diagDraws = 600;
                //  The interface is drawn pre-transformed and goes down a separate
                //  path, so a dump that only covers world draws cannot explain a
                //  black panel.
                g_diagUI = 600;
                LOGI("diagnostic: dumping the next 600 world draws and 600 interface draws");
            }
        }
    }
    for (size_t i = 0; i < sizeof(diag) / sizeof(diag[0]); ++i) {
        const bool on = RanPlat_DiagExists(diag[i].name) != 0;
        if (on != *diag[i].flag) {
            *diag[i].flag = on;
            LOGI("diagnostic: %s %s", on ? "skipping" : "restored", diag[i].what);
        }
    }
}

//  Read every uniform location out of one program.
//
//  Pulled out of the setup so a shader variant can be given the same treatment;
//  the globals always describe whichever program is bound.
void fetchUniformLocations(GLuint prog) {
    uMVP            = glGetUniformLocation(prog, "uMVP");
    uViewport       = glGetUniformLocation(prog, "uViewport");
    uPreTransformed = glGetUniformLocation(prog, "uPreTransformed");
    uFlipY          = glGetUniformLocation(prog, "uFlipY");
    uMatAlpha       = glGetUniformLocation(prog, "uMatAlpha");
    uWorldM         = glGetUniformLocation(prog, "uWorldM");
    uViewProj       = glGetUniformLocation(prog, "uViewProj");
    uVertexBlend    = glGetUniformLocation(prog, "uVertexBlend");
    uIndexedBlend   = glGetUniformLocation(prog, "uIndexedBlend");
    uTex            = glGetUniformLocation(prog, "uTex");
    uUseTexture     = glGetUniformLocation(prog, "uUseTexture");
    uAlphaTest      = glGetUniformLocation(prog, "uAlphaTest");
    uAlphaRef       = glGetUniformLocation(prog, "uAlphaRef");
    uColorOp        = glGetUniformLocation(prog, "uColorOp");
    uColorArg1      = glGetUniformLocation(prog, "uColorArg1");
    uColorArg2      = glGetUniformLocation(prog, "uColorArg2");
    uAlphaOp        = glGetUniformLocation(prog, "uAlphaOp");
    uAlphaArg1      = glGetUniformLocation(prog, "uAlphaArg1");
    uAlphaArg2      = glGetUniformLocation(prog, "uAlphaArg2");
    uTexFactor      = glGetUniformLocation(prog, "uTexFactor");
    uTexCube        = glGetUniformLocation(prog, "uTexCube");
    uTexStage1      = glGetUniformLocation(prog, "uTexStage1");
    uStage1         = glGetUniformLocation(prog, "uStage1");
    uTexSize        = glGetUniformLocation(prog, "uTexSize");
    uUiSharpen      = glGetUniformLocation(prog, "uUiSharpen");
    uGammaOn        = glGetUniformLocation(prog, "uGammaOn");
    uPlain          = glGetUniformLocation(prog, "uPlain");
    uGammaLut       = glGetUniformLocation(prog, "uGammaLut");
    uSpecularOn     = glGetUniformLocation(prog, "uSpecularOn");
    uMatSpecular    = glGetUniformLocation(prog, "uMatSpecular");
    uMatPower       = glGetUniformLocation(prog, "uMatPower");
    uLightSpecular  = glGetUniformLocation(prog, "uLightSpecular");
    uView           = glGetUniformLocation(prog, "uView");
    //  Stage 0 stays on unit 0; the cube map lives on unit 1 for its whole life.
    glUseProgram(prog);
    glUniform1i(uTexCube, 1);
    if (uTexStage1 >= 0) glUniform1i(uTexStage1, 2);

    uWorld          = glGetUniformLocation(prog, "uWorld");
    uCameraPos      = glGetUniformLocation(prog, "uCameraPos");
    uCameraPosF     = glGetUniformLocation(prog, "uCameraPosF");
    uLighting       = glGetUniformLocation(prog, "uLighting");
    uLightCount     = glGetUniformLocation(prog, "uLightCount");
    uGlobalAmbient  = glGetUniformLocation(prog, "uGlobalAmbient");
    uMatDiffuse     = glGetUniformLocation(prog, "uMatDiffuse");
    uHasVertexColor = glGetUniformLocation(prog, "uHasVertexColor");
    uMatAmbient     = glGetUniformLocation(prog, "uMatAmbient");
    uMatEmissive    = glGetUniformLocation(prog, "uMatEmissive");
    uLightType      = glGetUniformLocation(prog, "uLightType");
    uLightDiffuse   = glGetUniformLocation(prog, "uLightDiffuse");
    uLightAmbient   = glGetUniformLocation(prog, "uLightAmbient");
    uLightPos       = glGetUniformLocation(prog, "uLightPos");
    uLightDir       = glGetUniformLocation(prog, "uLightDir");
    uLightAtten     = glGetUniformLocation(prog, "uLightAtten");
    uFogMode        = glGetUniformLocation(prog, "uFogMode");
    uFogColor       = glGetUniformLocation(prog, "uFogColor");
    uFogStart       = glGetUniformLocation(prog, "uFogStart");
    uFogEnd         = glGetUniformLocation(prog, "uFogEnd");
    uFogDensity     = glGetUniformLocation(prog, "uFogDensity");
}
//  A shader for the state, instead of a shader for every state.
//
//  One "uber" program carrying every path the fixed-function pipeline can ask
//  for is long, and length costs more than the branches themselves: fewer waves
//  fit on the GPU at once, so there is less work to hide memory latency behind.
//  Measured on the Tab S9 - a fragment shader that returns straight after the
//  texture fetch takes the swap from 13.4 ms to 8.1 ms, while pinning any one
//  feature to its cheap path changes nothing. That is the shape of an occupancy
//  problem, not an arithmetic one.
//
//  So each combination of the state that changes the shader's shape gets its
//  own program, with those uniforms replaced by constants. There are a few
//  dozen in practice; they are built the first time they are used and kept.
namespace {

//  Every uniform location the renderer holds, so a variant can be swapped in
//  and out without the rest of the file knowing there is more than one program.
GLint *const kLocationVars[] = {
    &uMVP, &uViewport, &uPreTransformed, &uFlipY, &uMatAlpha, &uWorldM, &uViewProj,
    &uVertexBlend, &uIndexedBlend, &uTex, &uUseTexture, &uAlphaTest, &uAlphaRef,
    &uColorOp, &uColorArg1, &uColorArg2, &uAlphaOp, &uAlphaArg1, &uAlphaArg2,
    &uTexFactor, &uTexCube, &uStage1, &uTexSize, &uUiSharpen, &uGammaOn, &uPlain,
    &uGammaLut, &uSpecularOn, &uMatSpecular, &uMatPower, &uLightSpecular, &uView,
    &uWorld, &uCameraPos, &uCameraPosF, &uLighting, &uLightCount, &uGlobalAmbient,
    &uMatDiffuse, &uHasVertexColor, &uMatAmbient, &uMatEmissive, &uLightType,
    &uLightDiffuse, &uLightAmbient, &uLightPos, &uLightDir, &uLightAtten,
    &uFogMode, &uFogColor, &uFogStart, &uFogEnd, &uFogDensity,
};
const size_t kLocationCount = sizeof(kLocationVars) / sizeof(kLocationVars[0]);

struct Variant {
    GLuint prog;
    GLint  locs[kLocationCount];
    //  Its own uniform value cache: a location means nothing in another program,
    //  and without this every switch would re-upload everything.
    std::vector<UniformSlot> cache;
    Variant() : prog(0) { for (size_t i = 0; i < kLocationCount; ++i) locs[i] = -1; }
};

std::map<unsigned, Variant> g_variants;
unsigned g_variantKey = 0xFFFFFFFFu;

//  What the key says, and what it becomes in the preamble.
unsigned variantKey(int preTransformed, int lighting, int specular, int fogMode,
                    int stage1, int alphaTest, int gammaOn, int useTexture,
                    int indexedBlend, int vertexBlend) {
    return (unsigned)((preTransformed ? 1 : 0)
                    | (lighting  ? 2 : 0)
                    | (specular  ? 4 : 0)
                    | ((fogMode & 3) << 3)
                    | ((stage1  & 7) << 5)
                    | (alphaTest ? 0x100 : 0)
                    | (gammaOn   ? 0x200 : 0)
                    | (useTexture ? 0x400 : 0)
                    | (indexedBlend ? 0x800 : 0)
                    | ((vertexBlend & 7) << 12));
}

std::string variantPreamble(unsigned key) {
    char buf[512];
    snprintf(buf, sizeof(buf),
             "#define uPreTransformed %d\n"
             "#define uLighting %d\n"
             "#define uSpecularOn %d\n"
             "#define uFogMode %d\n"
             "#define uStage1 %d\n"
             "#define uAlphaTest %d\n"
             "#define uGammaOn %d\n"
             "#define uUseTexture %d\n"
             "#define uIndexedBlend %d\n"
             "#define uVertexBlend %d\n",
             (key & 1) ? 1 : 0,
             (key & 2) ? 1 : 0,
             (key & 4) ? 1 : 0,
             (int)((key >> 3) & 3),
             (int)((key >> 5) & 7),
             (key & 0x100) ? 1 : 0,
             (key & 0x200) ? 1 : 0,
             (key & 0x400) ? 1 : 0,
             (key & 0x800) ? 1 : 0,
             (int)((key >> 12) & 7));
    return std::string(buf);
}

//  The version line has to stay first, so the defines go after it.
std::string withPreamble(const char *src, const std::string &defines) {
    std::string out(src);
    const size_t nl = out.find(0x0a);
    if (nl == std::string::npos) return out;
    return out.substr(0, nl + 1) + defines + out.substr(nl + 1);
}

}

//  Defined below, once the location names are in scope.
void fetchUniformLocations(GLuint prog);

namespace {

bool buildVariant(unsigned key, Variant &v) {
    const std::string defines = variantPreamble(key);
    const std::string vsSrc = withPreamble(kVS, defines);
    const std::string fsSrc = withPreamble(kFS, defines);

    GLuint vs = compile(GL_VERTEX_SHADER, vsSrc.c_str());
    GLuint fs = compile(GL_FRAGMENT_SHADER, fsSrc.c_str());
    if (!vs || !fs) return false;

    v.prog = glCreateProgram();
    glAttachShader(v.prog, vs);
    glAttachShader(v.prog, fs);
    glLinkProgram(v.prog);
    GLint ok = 0;
    glGetProgramiv(v.prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        glGetProgramInfoLog(v.prog, sizeof(log) - 1, NULL, log);
        LOGE("variant %04x link failed: %s", key, log);
        glDeleteProgram(v.prog);
        v.prog = 0;
        return false;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);

    fetchUniformLocations(v.prog);
    for (size_t i = 0; i < kLocationCount; ++i) v.locs[i] = *kLocationVars[i];

    //  The cube map lives on unit 1 for the life of the program.
    useProgram(v.prog);
    if (uTexCube >= 0) glUniform1i(uTexCube, 1);
    if (uTexStage1 >= 0) glUniform1i(uTexStage1, 2);
    return true;
}

void useVariant(unsigned key) {
    //  Re-assert the program even when the variant has not changed. The touch
    //  HUD draws with a program of its own and then invalidates the state cache,
    //  which zeroes g_gl.program but leaves g_variantKey naming the engine's
    //  last variant - so this used to return with the HUD's program still bound
    //  and every following draw took the wrong shader until some other variant
    //  happened to be asked for. useProgram is itself cached, so this is free
    //  whenever nothing moved.
    if (key == g_variantKey) {
        std::map<unsigned, Variant>::iterator cur = g_variants.find(key);
        if (cur != g_variants.end()) useProgram(cur->second.prog);
        return;
    }

    //  Park the current program's cache before the locations change under it.
    std::map<unsigned, Variant>::iterator prev = g_variants.find(g_variantKey);
    if (prev != g_variants.end()) prev->second.cache.swap(g_uniformCache);
    g_uniformCache.clear();

    std::map<unsigned, Variant>::iterator it = g_variants.find(key);
    if (it == g_variants.end()) {
        Variant v;
        if (!buildVariant(key, v)) {
            //  Fall back to whatever is bound rather than drawing nothing.
            g_variantKey = 0xFFFFFFFFu;
            return;
        }
        it = g_variants.insert(std::make_pair(key, Variant())).first;
        it->second.prog = v.prog;
        for (size_t i = 0; i < kLocationCount; ++i) it->second.locs[i] = v.locs[i];
        LOGI("shader variant %04x built (%u in all)", key, (unsigned)g_variants.size());
    }

    for (size_t i = 0; i < kLocationCount; ++i) *kLocationVars[i] = it->second.locs[i];
    it->second.cache.swap(g_uniformCache);
    useProgram(it->second.prog);
    g_variantKey = key;
}

}

extern "C" int RanGLR_Init(void) {
    if (g_inited) return 1;
    if (!RanGL_Ready()) return 0;

    GLuint vs = compile(GL_VERTEX_SHADER, kVS);
    GLuint fs = compile(GL_FRAGMENT_SHADER, kFS);
    if (!vs || !fs) return 0;

    g_prog = glCreateProgram();
    glAttachShader(g_prog, vs);
    glAttachShader(g_prog, fs);
    glLinkProgram(g_prog);
    GLint ok = 0;
    glGetProgramiv(g_prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        glGetProgramInfoLog(g_prog, sizeof(log) - 1, NULL, log);
        LOGE("program link failed: %s", log);
        return 0;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);

    //  Unit 0 for the whole run: the renderer binds one texture at a time, so
    //  selecting the unit per draw was a GL call that never changed anything.
    glActiveTexture(GL_TEXTURE0);

    RanGLR_RefreshDiagnostics();

    {
        const char *ext = (const char *)glGetString(GL_EXTENSIONS);
        if (ext && strstr(ext, "GL_EXT_buffer_storage"))
            p_glBufferStorageEXT = (PFN_BUFSTORAGE)RanGL_ProcAddress("glBufferStorageEXT");
        g_havePersistentMap = p_glBufferStorageEXT != NULL;
        LOGI("persistent buffer mapping: %s",
             g_havePersistentMap ? "yes (streaming writes are a memcpy)" : "no");
    }

    //  ES 3.1 separate attribute format, if this driver has it.
    p_glVertexAttribFormat  = (PFN_VAFORMAT)RanGL_ProcAddress("glVertexAttribFormat");
    p_glVertexAttribBinding = (PFN_VABINDING)RanGL_ProcAddress("glVertexAttribBinding");
    p_glBindVertexBuffer    = (PFN_BINDVB)RanGL_ProcAddress("glBindVertexBuffer");
    g_haveAttribFormat = p_glVertexAttribFormat && p_glVertexAttribBinding && p_glBindVertexBuffer;
    LOGI("separate attribute format: %s", g_haveAttribFormat ? "yes" : "no (ES 3.0 path)");

    fetchUniformLocations(g_prog);

    glGenVertexArrays(1, &g_vao);
    glGenBuffers(1, &g_vbo);
    glGenBuffers(1, &g_ibo);

    // A 1x1 white texture keeps the shader branch-free when nothing is bound.
    const GLubyte white[4] = { 255, 255, 255, 255 };
    glGenTextures(1, &g_whiteTex);
    glBindTexture(GL_TEXTURE_2D, g_whiteTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glDepthRangef(0.0f, 1.0f);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    resetUniformCache();
    g_gl.reset();
    g_inited = true;
    g_renderThread = pthread_self();
    g_renderThreadKnown = true;
    {
        const char *ext = (const char *)glGetString(GL_EXTENSIONS);
        LOGI("buffer_storage: %s   map_buffer_range: %s",
             (ext && strstr(ext, "GL_EXT_buffer_storage")) ? "yes" : "no",
             (ext && strstr(ext, "GL_EXT_map_buffer_range")) ? "yes" : "core");
    }
    LOGI("GLES renderer ready");
    return 1;
}

//  glTex == 0 selects the back buffer. Everything else renders into that
//  texture through a cached FBO sized to the surface.
//  Copy one render-target texture into another, which is what D3D StretchRect
//  does for the engine's off-screen chain. Both sides already have a
//  framebuffer object from having been render targets, so this is a blit.
extern "C" int RanGLR_BlitTexture(unsigned srcTex, int sx0, int sy0, int sx1, int sy1,
                                  unsigned dstTex, int dx0, int dy0, int dx1, int dy1,
                                  int linear) {
    if (!g_inited || !srcTex) return 0;

    std::map<GLuint, RanRT>::iterator s = g_rts.find(srcTex);
    if (s == g_rts.end() || !s->second.fbo) return 0;

    GLuint dstFbo = RanGL_DefaultFramebuffer();   // no texture means the screen
    if (dstTex) {
        std::map<GLuint, RanRT>::iterator d = g_rts.find(dstTex);
        if (d == g_rts.end() || !d->second.fbo) return 0;
        dstFbo = d->second.fbo;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, s->second.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstFbo);
    glBlitFramebuffer(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1,
                      GL_COLOR_BUFFER_BIT, linear ? GL_LINEAR : GL_NEAREST);

    //  Leave the binding where the renderer expects it.
    glBindFramebuffer(GL_FRAMEBUFFER, g_rtActive ? g_rtFbo : RanGL_DefaultFramebuffer());
    ++g_callsState;
    return 1;
}

extern "C" void RanGLR_SetRenderTargetTexture(unsigned glTex, int w, int h) {
    if (!g_inited) return;

    if (!glTex || w <= 0 || h <= 0) {
        if (g_rtActive) {
            glBindFramebuffer(GL_FRAMEBUFFER, RanGL_DefaultFramebuffer());
            g_rtActive = false;
        }
        glViewport(0, 0, RanGL_Width(), RanGL_Height());
        return;
    }

    RanRT &rt = g_rts[glTex];
    g_rtFbo = rt.fbo;
    if (!rt.fbo || rt.w != w || rt.h != h) {
        if (!rt.fbo) glGenFramebuffers(1, &rt.fbo);
        if (!rt.depth) glGenRenderbuffers(1, &rt.depth);
        rt.w = w; rt.h = h;

        bindTex2D(glTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glBindRenderbuffer(GL_RENDERBUFFER, rt.depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, w, h);

        glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, glTex, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rt.depth);

        //  Freshly allocated storage is undefined, and undefined is not black.
        //
        //  glTexImage2D with a NULL pointer reserves the memory and leaves
        //  whatever was in it. That is fine for a pass that clears before it
        //  draws - the post-process chain does - but not for one that expects
        //  the target to start empty, or for a target that is composited before
        //  anything has rendered into it. Composited additively, undefined
        //  memory is a full-screen wash of an arbitrary colour.
        //
        //  D3D leaves a new render target's contents undefined too, so this is
        //  not emulating a documented behaviour; it is choosing the one value
        //  that makes an unwritten target harmless under every blend the engine
        //  uses. It costs one clear per target, once.
        {
            GLenum stTmp = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (stTmp == GL_FRAMEBUFFER_COMPLETE) {
                GLboolean scis = glIsEnabled(GL_SCISSOR_TEST);
                if (scis) glDisable(GL_SCISSOR_TEST);
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glDepthMask(GL_TRUE);
                glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
                glClearDepthf(1.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                if (scis) glEnable(GL_SCISSOR_TEST);
            }
        }

        GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (st != GL_FRAMEBUFFER_COMPLETE) {
            LOGE("render target %ux%u incomplete: 0x%04X", w, h, st);
            glBindFramebuffer(GL_FRAMEBUFFER, RanGL_DefaultFramebuffer());
            g_rtActive = false;
            glViewport(0, 0, RanGL_Width(), RanGL_Height());
            return;
        }
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo);
    }

    g_rtActive = true;
    g_rtFbo = rt.fbo;
    g_rtW = w; g_rtH = h;
    glViewport(0, 0, w, h);
}

//  A texture that has been drawn into must never be re-uploaded from its CPU
//  bits, and its FBO owns nothing once the texture goes away.
//  A bare texture name for a surface that has never been uploaded; storage is
//  allocated when it is first attached to a render target.
//  True only on the thread that owns the EGL context.
extern "C" int RanGLR_OnRenderThread(void) {
    //  Whoever holds the context can issue GL. That is normally the main thread
    //  and, while the loading screen is up, the loading thread.
    return RanGL_HasContext();
}

extern "C" unsigned RanGLR_CreateEmptyTexture(void) {
    GLuint t = 0;
    glGenTextures(1, &t);
    return t;
}

extern "C" void RanGLR_ForgetRenderTarget(unsigned glTex) {
    std::map<GLuint, RanRT>::iterator it = g_rts.find(glTex);
    if (it == g_rts.end()) return;
    if (it->second.fbo)   glDeleteFramebuffers(1, &it->second.fbo);
    if (it->second.depth) glDeleteRenderbuffers(1, &it->second.depth);
    g_rts.erase(it);
}

//  A D3D clear rectangle is in client pixels with Y down; GL scissors from the
//  bottom, and the client's pixels are logical ones when the frame is upscaled.
extern "C" void RanGLR_ClearRect(int x, int y, int w, int h) {
    if (!g_inited) return;
    if (w <= 0 || h <= 0) { glDisable(GL_SCISSOR_TEST); return; }

    //  Without a preserved surface the untouched region of the frame holds
    //  whatever the rotating buffer had two frames ago, so honouring the
    //  rectangle would flicker. Clearing everything is the safe reading of
    //  "the client expects last frame to still be there".
    if (!g_rtActive && !RanGL_SwapPreserved()) { glDisable(GL_SCISSOR_TEST); return; }

    const int scale = g_rtActive ? 1 : RanGL_UIScale();
    const int surfaceH = g_rtActive ? g_rtH : RanGL_Height();
    glEnable(GL_SCISSOR_TEST);
    glScissor(x * scale, surfaceH - (y + h) * scale, w * scale, h * scale);
}

extern "C" void RanGLR_ClearRectOff(void) {
    if (g_inited) glDisable(GL_SCISSOR_TEST);
}

//  The frame is over: the next one starts on a buffer of unknown content.
extern "C" int RanGLR_FrameDrawCount(void) { return g_frameDraw; }

extern "C" void RanGLR_FrameEnd(void) { g_frameClearedColor = false; }

extern "C" void RanGLR_Clear(DWORD flags, D3DCOLOR color, float z, DWORD stencil) {
    if (!g_inited) return;
    //  The client clears the frame once at the top of the scene, which is the
    //  only frame boundary visible from this layer.
    if (!g_rtActive && (flags & D3DCLEAR_TARGET)) {
        if (g_drawLog > 0) --g_drawLog;
        g_frameDraw = 0;
        g_frameClearedColor = true;
    }
    GLbitfield mask = 0;
    if (flags & D3DCLEAR_TARGET) {
        // D3DCOLOR is ARGB, packed 0xAARRGGBB.
        float a = ((color >> 24) & 0xFF) / 255.0f;
        float r = ((color >> 16) & 0xFF) / 255.0f;
        float g = ((color >> 8) & 0xFF) / 255.0f;
        float b = (color & 0xFF) / 255.0f;
        glClearColor(r, g, b, a);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        mask |= GL_COLOR_BUFFER_BIT;
    }
    if (flags & D3DCLEAR_ZBUFFER) {
        glClearDepthf(z);
        glDepthMask(GL_TRUE);          // a masked depth buffer cannot be cleared
        mask |= GL_DEPTH_BUFFER_BIT;
    }
    if (flags & D3DCLEAR_STENCIL) {
        glClearStencil((GLint)stencil);
        mask |= GL_STENCIL_BUFFER_BIT;
    }
    if (mask) glClear(mask);
}

extern "C" void RanGLR_SetViewport(int x, int y, int w, int h) {
    if (!g_inited) return;
    //  The client works in logical pixels (see RanGL_UIScale) and the frame is
    //  stretched to the panel, so a viewport it sets scales with it — except on
    //  a render target, which is already sized in real pixels.
    const int scale = g_rtActive ? 1 : RanGL_UIScale();
    const int surfaceH = g_rtActive ? g_rtH : RanGL_Height();
    // D3D viewport Y is measured from the top, GL's from the bottom.
    glViewport(x * scale, surfaceH - (y + h) * scale, w * scale, h * scale);
}

// Apply the D3D render-state block to GL immediately before a draw. Doing it
// per-draw rather than per-state-set keeps this correct with state blocks, which
// replay dozens of states at once.
extern "C" void RanGLR_SetMaterialAlpha(float a) { g_matAlpha = a; }

//  ramp is 3 * 256 WORDs: red, then green, then blue, each 0..65535 as GDI
//  expects. Passing NULL turns the ramp off.
//  Anything that issues GL behind the renderer's back - the touch overlay does,
//  once a frame - has to say so, or the cached state describes a context that no
//  longer exists and the next draw silently skips the binds it still needs.
extern "C" void RanGLR_InvalidateStateCache(void) {
    g_gl.reset();
    //  g_gl.reset() forgets which program is bound, so the variant cache has to
    //  forget too: they are two halves of one piece of state, and leaving this
    //  one set is what let a draw skip its glUseProgram entirely.
    g_variantKey = 0xFFFFFFFFu;
    glActiveTexture(GL_TEXTURE0);
}

extern "C" void RanGLR_SetGammaRamp(const unsigned short *ramp) {
    if (!ramp) { g_gammaOn = 0; return; }
    bool identity = true;
    for (int i = 0; i < 256; ++i) {
        const unsigned char r = (unsigned char)(ramp[i] >> 8);
        const unsigned char g = (unsigned char)(ramp[256 + i] >> 8);
        const unsigned char b = (unsigned char)(ramp[512 + i] >> 8);
        g_gammaBytes[i * 3 + 0] = r;
        g_gammaBytes[i * 3 + 1] = g;
        g_gammaBytes[i * 3 + 2] = b;
        //  A ramp that maps every level to itself is what the client sets to
        //  turn correction off; skipping it avoids three texture reads a pixel.
        if (r != i || g != i || b != i) identity = false;
    }
    g_gammaOn = identity ? 0 : 1;
    g_gammaDirty = true;
}

extern "C" void RanGLR_SetSpecular(int enabled, const float *matSpecular, float power,
                                   const float *lightSpecular, int lightCount) {
    g_specularOn = enabled ? 1 : 0;
    if (matSpecular) memcpy(g_matSpecular, matSpecular, sizeof(g_matSpecular));
    g_matPower = power;
    if (lightCount > 8) lightCount = 8;
    if (lightCount < 0) lightCount = 0;
    memset(g_lightSpecular, 0, sizeof(g_lightSpecular));
    if (lightSpecular && lightCount)
        memcpy(g_lightSpecular, lightSpecular, sizeof(float) * 3 * (size_t)lightCount);
}

extern "C" void RanGLR_SetLighting(int enabled, const float *worldMatrix, const float *cameraPos,
                                   const float *globalAmbient, const float *matDiffuse,
                                   const float *matAmbient, const float *matEmissive,
                                   const RanGlLight *lights, int lightCount) {
    g_lightingOn = enabled ? 1 : 0;
    if (worldMatrix) memcpy(g_world, worldMatrix, sizeof(g_world));
    if (cameraPos) memcpy(g_cameraPos, cameraPos, sizeof(g_cameraPos));
    if (globalAmbient) memcpy(g_globalAmbient, globalAmbient, sizeof(g_globalAmbient));
    if (matDiffuse) memcpy(g_matDiffuse, matDiffuse, sizeof(g_matDiffuse));
    if (matAmbient) memcpy(g_matAmbient, matAmbient, sizeof(g_matAmbient));
    if (matEmissive) memcpy(g_matEmissive, matEmissive, sizeof(g_matEmissive));

    if (lightCount > 8) lightCount = 8;
    if (lightCount < 0) lightCount = 0;
    g_lightCount = lightCount;
    for (int i = 0; i < lightCount && lights; ++i) {
        g_lightType[i] = lights[i].type;
        for (int k = 0; k < 3; ++k) {
            g_lightDiffuse[i * 3 + k] = lights[i].diffuse[k];
            g_lightAmbient[i * 3 + k] = lights[i].ambient[k];
            g_lightPos[i * 4 + k]     = lights[i].position[k];
            g_lightDir[i * 3 + k]     = lights[i].direction[k];
            g_lightAtten[i * 3 + k]   = lights[i].atten[k];
        }
        g_lightPos[i * 4 + 3] = lights[i].range;
    }
}

extern "C" void RanGLR_SetFog(int enabled, int mode, const float *color,
                              float start, float end, float density) {
    g_fogMode = enabled ? mode : 0;
    if (color) memcpy(g_fogColor, color, sizeof(g_fogColor));
    g_fogStart = start;
    g_fogEnd = end;
    g_fogDensity = density;
}

//  D3DRS_VERTEXBLEND plus the world matrix palette, as the device last set them.
extern "C" void RanGLR_SetVertexBlend(int weightCount, const float *worldMatrices16x4,
                                      const float *viewProj16) {
    g_vertexBlend = weightCount < 0 ? 0 : (weightCount > 3 ? 3 : weightCount);
    if (worldMatrices16x4) memcpy(g_worldM, worldMatrices16x4, sizeof(g_worldM));
    if (viewProj16) memcpy(g_viewProj, viewProj16, sizeof(g_viewProj));
}

//  Stage 1, bound once per draw. mode 1 means "cube map by camera-space normal";
//  0 turns the stage off. The cube texture stays on texture unit 1.
extern "C" void RanGLR_SetStage1(int mode, unsigned glCubeTex, unsigned gl2DTex,
                                 const float *viewMatrix) {
    //  1 and 3 are the two cube-map addressings and need a cube bound; 2 is a
    //  flat tint and 4 takes alpha from the stage 0 texture at the second
    //  coordinate set, neither of which needs one.
    //
    //  Mode 3 used to fall through to 0 here, so the reflection addressing the
    //  caller asks for never reached the shader even though the shader has a
    //  branch for it.
    if ((mode == 1 || mode == 3) && glCubeTex)  g_stage1Mode = mode;
    else if (mode == 5 && gl2DTex)              g_stage1Mode = mode;
    else if (mode == 2 || mode == 4)            g_stage1Mode = mode;
    else                                        g_stage1Mode = 0;

    if (gl2DTex && gl2DTex != g_stage1Tex2D) {
        g_stage1Tex2D = gl2DTex;
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, gl2DTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glActiveTexture(GL_TEXTURE0);
    }

    if (viewMatrix) memcpy(g_viewMatrix, viewMatrix, sizeof(g_viewMatrix));
    if (glCubeTex && glCubeTex != g_stage1Cube) {
        g_stage1Cube = glCubeTex;
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_CUBE_MAP, glCubeTex);
        glActiveTexture(GL_TEXTURE0);
        ++g_callsTexture;
    }
}

extern "C" void RanGLR_SetTextureStage(DWORD colorOp, DWORD colorArg1, DWORD colorArg2,
                                       DWORD alphaOp, DWORD alphaArg1, DWORD alphaArg2,
                                       DWORD texFactor) {
    g_colorOp = colorOp; g_colorArg1 = colorArg1; g_colorArg2 = colorArg2;
    g_alphaOp = alphaOp; g_alphaArg1 = alphaArg1; g_alphaArg2 = alphaArg2;
    //  D3DCOLOR is ARGB, and the shader wants it as four floats.
    g_texFactor[0] = (float)((texFactor >> 16) & 0xFF) / 255.0f;
    g_texFactor[1] = (float)((texFactor >> 8) & 0xFF) / 255.0f;
    g_texFactor[2] = (float)(texFactor & 0xFF) / 255.0f;
    g_texFactor[3] = (float)((texFactor >> 24) & 0xFF) / 255.0f;
}

//  Everything the shader needs for this draw, sent to the program that is
//  actually bound.
//
//  This used to run inside RanGLR_ApplyState, which is called before the draw
//  chooses its shader variant - so the uniforms went to the previous program.
//  The state is all in globals already, so it simply moved.
void applyProgramUniforms() {
    if (g_fsProbe & 1) {
        //  MODULATE(TEXTURE, DIFFUSE) for colour and alpha both: the cheapest
        //  path through argValue and the two op ladders.
        setUniform1i(uColorOp, 4); setUniform1i(uColorArg1, 2); setUniform1i(uColorArg2, 0);
        setUniform1i(uAlphaOp, 4); setUniform1i(uAlphaArg1, 2); setUniform1i(uAlphaArg2, 0);
    } else {
        setUniform1i(uColorOp,   (GLint)g_colorOp);
        setUniform1i(uColorArg1, (GLint)g_colorArg1);
        setUniform1i(uColorArg2, (GLint)g_colorArg2);
        setUniform1i(uAlphaOp,   (GLint)g_alphaOp);
        setUniform1i(uAlphaArg1, (GLint)g_alphaArg1);
        setUniform1i(uAlphaArg2, (GLint)g_alphaArg2);
    }
    setUniformVec4(uTexFactor, g_texFactor);
    setUniform1i(uStage1, (g_fsProbe & 2) ? 0 : g_stage1Mode);
    if (g_stage1Mode) setUniformMatrix(uView, g_viewMatrix, 1);

    setUniform1i(uVertexBlend, g_vertexBlend);
    setUniformMatrix(uWorldM, g_worldM, 16);
    setUniformMatrix(uViewProj, g_viewProj, 1);

    setUniform1i(uLighting, g_lightingOn);
    setUniform1i(uLightCount, g_lightCount);
    setUniformMatrix(uWorld, g_world, 1);
    setUniform3fv(uCameraPos, g_cameraPos);
    setUniform3fv(uCameraPosF, g_cameraPos);
    setUniform1i(uSpecularOn, g_specularOn);

    //  Unit 3: unit 0 is the stage-0 texture, 1 the stage-1 texture and 2 the
    //  cube, so the ramp goes above them and stays bound.
    if (g_gammaDirty && g_gammaOn) {
        g_gammaDirty = false;
        if (!g_gammaLut) glGenTextures(1, &g_gammaLut);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, g_gammaLut);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 256, 1, 0, GL_RGB,
                     GL_UNSIGNED_BYTE, g_gammaBytes);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glActiveTexture(GL_TEXTURE0);
    }
    setUniform1i(uPlain, g_plainFS ? 1 : 0);
    if (g_fsProbe & 4) setUniform1i(uGammaOn, 0);
    setUniform1i(uGammaOn, (g_gammaOn && g_gammaLut) ? 1 : 0);
    if (g_gammaOn && g_gammaLut) {
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, g_gammaLut);
        glActiveTexture(GL_TEXTURE0);
        if (uGammaLut >= 0) glUniform1i(uGammaLut, 3);
    }
    if (g_specularOn) {
        setUniform3fv(uMatSpecular, g_matSpecular);
        setUniform1f(uMatPower, g_matPower);
        if (uLightSpecular >= 0) glUniform3fv(uLightSpecular, 8, g_lightSpecular);
    }
    setUniform3fv(uGlobalAmbient, g_globalAmbient);
    setUniform3fv(uMatDiffuse, g_matDiffuse);
    setUniform3fv(uMatAmbient, g_matAmbient);
    setUniform3fv(uMatEmissive, g_matEmissive);
    setUniform1f(uMatAlpha, g_matAlpha);
    if (g_lightCount > 0) {
        //  Compared as one block: the light set changes far less often than it
        //  is sent, and six array uploads a draw is most of the uniform traffic.
        //  Fixed storage, because this runs on every draw and a heap allocation
        //  there is a cost of its own — eight lights is the D3D maximum.
        float lights[8 * 17];
        int n = 0;
        for (int i = 0; i < g_lightCount; ++i) lights[n++] = (float)g_lightType[i];
        memcpy(lights + n, g_lightDiffuse, sizeof(float) * g_lightCount * 3); n += g_lightCount * 3;
        memcpy(lights + n, g_lightAmbient, sizeof(float) * g_lightCount * 3); n += g_lightCount * 3;
        memcpy(lights + n, g_lightPos,     sizeof(float) * g_lightCount * 4); n += g_lightCount * 4;
        memcpy(lights + n, g_lightDir,     sizeof(float) * g_lightCount * 3); n += g_lightCount * 3;
        memcpy(lights + n, g_lightAtten,   sizeof(float) * g_lightCount * 3); n += g_lightCount * 3;
        if (uniformChanged(uLightType, lights, n)) {
            glUniform1iv(uLightType, g_lightCount, g_lightType);
            glUniform3fv(uLightDiffuse, g_lightCount, g_lightDiffuse);
            glUniform3fv(uLightAmbient, g_lightCount, g_lightAmbient);
            glUniform4fv(uLightPos, g_lightCount, g_lightPos);
            glUniform3fv(uLightDir, g_lightCount, g_lightDir);
            glUniform3fv(uLightAtten, g_lightCount, g_lightAtten);
        }
    }

    setUniform1i(uFogMode, g_fogMode);
    setUniform3fv(uFogColor, g_fogColor);
    setUniform1f(uFogStart, g_fogStart);
    setUniform1f(uFogEnd, g_fogEnd);
    setUniform1f(uFogDensity, g_fogDensity);

    setUniform1i(uAlphaTest, ((g_fsProbe & 8) == 0 && g_dsATest) ? 1 : 0);
    setUniform1f(uAlphaRef, (float)g_dsARef / 255.0f);
}

extern "C" void RanGLR_ApplyState(const DWORD *rs) {
    if (!g_inited || !rs) return;
    g_dsBlend = rs[D3DRS_ALPHABLENDENABLE]; g_dsSrc = rs[D3DRS_SRCBLEND]; g_dsDst = rs[D3DRS_DESTBLEND];
    g_dsZ = rs[D3DRS_ZENABLE]; g_dsZW = rs[D3DRS_ZWRITEENABLE]; g_dsCull = rs[D3DRS_CULLMODE];
    g_dsATest = rs[D3DRS_ALPHATESTENABLE]; g_dsARef = rs[D3DRS_ALPHAREF] & 0xFF;
    // The alpha-test uniforms below go to the CURRENT program, so it has to be
    // bound here and not left to the draw call that follows - through the
    // cache, since this runs once per draw and the program never changes.

    setBlend(rs[D3DRS_ALPHABLENDENABLE] != 0,
             blendFactor(rs[D3DRS_SRCBLEND]), blendFactor(rs[D3DRS_DESTBLEND]));

    //  D3DRS_DEPTHBIAS is a float packed into the state DWORD, added straight to
    //  the depth value; the engine uses it to lift decals, trims and effect
    //  layers off the surface they share. Ignoring it left those layers fighting
    //  the surface, which reads as two textures overlapping each other.
    //  glPolygonOffset works in units of the smallest resolvable depth step, so
    //  the bias is scaled by the depth buffer resolution.
    {
        float bias = 0.0f, slope = 0.0f;
        memcpy(&bias,  &rs[D3DRS_DEPTHBIAS], sizeof(float));
        memcpy(&slope, &rs[D3DRS_SLOPESCALEDEPTHBIAS], sizeof(float));
        //  The context asks for a 24-bit depth buffer and falls back to 16;
        //  scaling by the wrong one would offset by 256x.
        setPolygonOffset(slope, bias * (float)(1 << RanGL_DepthBits()));
    }

    setDepth(rs[D3DRS_ZENABLE] != 0,
             cmpFunc(rs[D3DRS_ZFUNC] ? rs[D3DRS_ZFUNC] : D3DCMP_LESSEQUAL),
             rs[D3DRS_ZWRITEENABLE] != 0);

    // D3D names the winding that is CULLED; GL names the winding that is FRONT.
    // The face the UI path wants is recorded here and inverted per draw for the
    // world path, which does not flip Y (see RanGLR_Draw).
    switch (rs[D3DRS_CULLMODE]) {
        case D3DCULL_NONE: g_cullWanted = false; break;
        //  Measured, and it surprised me: the world's ground declares
        //  D3DCULL_CCW and the character-select ground declares D3DCULL_CW, yet
        //  both are only visible with GL's front face set to CW. Their geometry
        //  is wound the same way and one of the two maps disagrees with its own
        //  declared mode — the PC build never notices because terrain draws
        //  unlit, where a back face is indistinguishable. So a cull mode of
        //  either winding culls the same side here; D3DCULL_NONE still means
        //  none, which is the distinction that actually carries meaning.
        case D3DCULL_CW:
        case D3DCULL_CCW:  g_cullWanted = true; g_frontFace = GL_CW; break;
        default:           g_cullWanted = false; break;
    }

}

// FVF layout -> attribute pointers. Only the components the shader consumes are
// wired; the rest (normals, extra UV sets) are skipped by stride.
//  One draw path. `glVB`/`glIB` name buffers the client owns; when they are 0
//  the vertex and index data are streamed from `verts`/`indices` instead.
//  Streaming geometry: one store per target, written front to back and
//  respecified only when it wraps.
struct RingBuffer {
    GLuint  buffer;
    GLenum  target;
    GLsizei capacity;
    GLsizei cursor;
    //  Non-NULL once the buffer is mapped for the rest of the run.
    unsigned char *mapped;
    //  Marks the point the GPU has to have reached before the cursor may pass
    //  this way again.
    GLsync lapFence;

    RingBuffer(GLenum t) : buffer(0), target(t), capacity(0), cursor(0),
                           mapped(NULL), lapFence(0) {}

    //  Immutable storage, mapped once. Only ever called for a fresh name.
    bool createPersistent(GLsizei bytes) {
        const GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT_EXT | GL_MAP_COHERENT_BIT_EXT;
        p_glBufferStorageEXT(target, bytes, NULL, flags);
        void *p = glMapBufferRange(target, 0, bytes, flags);
        if (!p) return false;
        mapped = (unsigned char *)p;
        capacity = bytes;
        cursor = 0;
        return true;
    }

    //  About to overwrite where the GPU may still be reading: wait for the
    //  fence left the last time around. At sixteen megabytes against a few
    //  hundred kilobytes a frame, this is tens of frames apart and the wait is
    //  already satisfied.
    void waitForLap() {
        if (!lapFence) return;
        glClientWaitSync(lapFence, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ull);
        glDeleteSync(lapFence);
        lapFence = 0;
    }

    void markLap() {
        if (lapFence) glDeleteSync(lapFence);
        lapFence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    }

    //  Returns the byte offset the data was written at.
    GLintptr write(const void *data, GLsizei size) {
        if (!buffer) glGenBuffers(1, &buffer);
        if (target == GL_ARRAY_BUFFER) bindArray(buffer); else bindElements(buffer);

        //  The fast path: the buffer is already mapped, so the write is a
        //  memcpy and costs the driver nothing at all.
        if (g_havePersistentMap) {
            if (!mapped) {
                GLsizei want = size * 4;
                if (want < (16 << 20)) want = 16 << 20;
                if (!createPersistent(want)) {
                    //  Storage is immutable once created, so a failed map means
                    //  this name is spent: take a fresh one and fall back.
                    glDeleteBuffers(1, &buffer);
                    glGenBuffers(1, &buffer);
                    if (target == GL_ARRAY_BUFFER) { g_gl.arrayBuffer = 0; bindArray(buffer); }
                    else                           { g_gl.elementBuffer = 0; bindElements(buffer); }
                    g_havePersistentMap = false;
                    LOGE("persistent mapping failed; streaming through glBufferSubData");
                }
            }
            if (mapped) {
                if (size > capacity) return 0;             // never seen: a draw larger than the ring
                if (cursor + size > capacity) {
                    //  Wrapping means reusing memory the GPU may still be
                    //  reading, so the fence has to cover this lap's draws -
                    //  and those were submitted DURING the lap. Fencing at the
                    //  start of a lap and waiting on it at the end guarantees
                    //  nothing about them: it only proves the work from before
                    //  the lap finished. That is the race behind the exploded,
                    //  flickering characters on the tablet, which the emulator
                    //  never showed because it serialises the GPU anyway. It
                    //  needs real streaming volume to bite, which is why
                    //  character select was clean and the world was not.
                    //
                    //  Fence here, after the lap's last draw, then wait.
                    markLap();
                    waitForLap();
                    cursor = 0;
                }
                memcpy(mapped + cursor, data, (size_t)size);
                const GLintptr offset = cursor;
                cursor += size;
                cursor = (cursor + 15) & ~15;
                return offset;
            }
        }

        //  Grow to fit the largest single draw seen so far, with room to
        //  spare. Generous, because every wrap costs an orphan and a fresh
        //  allocation: a frame streams a few hundred kilobytes, so eight
        //  megabytes is many frames of headroom.
        if (size > capacity || capacity == 0) {
            capacity = size * 4;
            if (capacity < (8 << 20)) capacity = 8 << 20;
            glBufferData(target, capacity, NULL, GL_STREAM_DRAW);
            ++g_callsBuffer;
            cursor = 0;
        } else if (cursor + size > capacity) {
            //  Wrapped: tell the driver the old contents are dead, so it hands
            //  back fresh memory instead of waiting for the draws that read
            //  the old contents to finish.
            glBufferData(target, capacity, NULL, GL_STREAM_DRAW);
            ++g_callsBuffer;
            cursor = 0;
        }

        const GLintptr offset = cursor;
        //  Without persistent mapping, one glBufferSubData is the cheapest way
        //  to get the bytes across: mapping a range costs a map and an unmap
        //  for the same copy, and on a driver that emulates GL - where a call
        //  is expensive and a stall is not - that doubles the price of every
        //  streamed draw.
        glBufferSubData(target, offset, size, data);
        ++g_callsBuffer;
        cursor += size;
        //  Keep slices aligned; unaligned attribute reads are slow or illegal.
        cursor = (cursor + 15) & ~15;
        return offset;
    }
};

//  One vertex array object per layout a client buffer is drawn with. The key
//  is everything glVertexAttribPointer would have been told: which buffers,
//  which FVF, which stride, and where the vertices start.
struct VaoKey {
    unsigned vb, ib;
    DWORD    fvf;
    UINT     stride;
    GLsizei  base;

    bool operator<(const VaoKey &o) const {
        if (vb != o.vb) return vb < o.vb;
        if (ib != o.ib) return ib < o.ib;
        if (fvf != o.fvf) return fvf < o.fvf;
        if (stride != o.stride) return stride < o.stride;
        return base < o.base;
    }
};
std::map<VaoKey, GLuint> g_vaoCache;
unsigned long g_vaoCreated = 0, g_vaoHits = 0;

//  A buffer that goes away takes every layout described against it with it -
//  names are recycled, and a stale VAO would point at whatever took the name.
void forgetVaosForBuffer(unsigned buffer) {
    for (std::map<VaoKey, GLuint>::iterator it = g_vaoCache.begin(); it != g_vaoCache.end(); ) {
        if (it->first.vb == buffer || it->first.ib == buffer) {
            GLuint v = it->second;
            glDeleteVertexArrays(1, &v);
            g_vaoCache.erase(it++);
        } else {
            ++it;
        }
    }
}

RingBuffer g_streamVerts(GL_ARRAY_BUFFER);
RingBuffer g_streamIndices(GL_ELEMENT_ARRAY_BUFFER);

//  How much of a frame is spent submitting draws, as opposed to the engine
//  deciding what to draw. Measured because "it is slow" is not a diagnosis —
//  but two clock reads per draw are themselves a cost, so it is off unless
//  RAN_TIME_DRAWS is defined.
double g_drawSeconds = 0.0;
//  The same seconds, never reset. Two places report draw time - the per-frame
//  FRAME line and the 300-frame frame-budget census - and both used to call
//  RanGLR_TakeDrawSeconds, which resets. Whichever ran first got the time and
//  the other printed 0.0 ms, which is why the budget line has always claimed
//  draw submission costs nothing while the FRAME line beside it said 2 us x
//  287 draws. A reader that does not reset lets both be right.
double g_drawSecondsTotal = 0.0;

static double nowSeconds() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

//  Draws since the last read, so a frame report can say what a draw costs -
//  the only figure that stays comparable when the scene gets busier.
unsigned long g_drawsSinceReport = 0;

extern "C" void RanGLR_TakeCallCounts(unsigned long *uniform, unsigned long *texture,
                                      unsigned long *attrib, unsigned long *state,
                                      unsigned long *draw, unsigned long *buffer) {
    if (uniform) *uniform = g_callsUniform;
    if (texture) *texture = g_callsTexture;
    if (attrib)  *attrib  = g_callsAttrib;
    if (state)   *state   = g_callsState;
    if (draw)    *draw    = g_callsDraw;
    if (buffer)  *buffer  = g_callsBuffer;
    g_callsUniform = g_callsTexture = g_callsAttrib = g_callsState = g_callsDraw = g_callsBuffer = 0;
}

extern "C" unsigned long RanGLR_TakeDrawCount(void) {
    const unsigned long v = g_drawsSinceReport;
    g_drawsSinceReport = 0;
    return v;
}

extern "C" double RanGLR_TakeDrawSeconds(void) {
    const double v = g_drawSeconds;
    g_drawSeconds = 0.0;
    return v;
}

//  Monotonic, for a second reader that must not disturb the first.
extern "C" double RanGLR_DrawSecondsTotal(void) { return g_drawSecondsTotal; }

static void drawInternal(DWORD primType, UINT primCount, const void *verts,
                         UINT stride, DWORD fvf, unsigned glTexture,
                         const float *mvp, const void *indices, UINT indexBits,
                         UINT indexCount, UINT vertexCount,
                         unsigned glVB, UINT vbByteOffset,
                         unsigned glIB, UINT ibByteOffset) {
    if (!g_inited) return;
    if (!glVB && !verts) return;
#ifdef RAN_TIME_DRAWS
    const double drawStart = nowSeconds();
#endif

    // How many elements the primitive type implies, used for both the
    // non-indexed vertex count and as a fallback index count.
    UINT implied;
    GLenum mode;
    switch (primType) {
        case D3DPT_POINTLIST:     mode = GL_POINTS;         implied = primCount;     break;
        case D3DPT_LINELIST:      mode = GL_LINES;          implied = primCount * 2; break;
        case D3DPT_LINESTRIP:     mode = GL_LINE_STRIP;     implied = primCount + 1; break;
        case D3DPT_TRIANGLELIST:  mode = GL_TRIANGLES;      implied = primCount * 3; break;
        case D3DPT_TRIANGLESTRIP: mode = GL_TRIANGLE_STRIP; implied = primCount + 2; break;
        case D3DPT_TRIANGLEFAN:   mode = GL_TRIANGLE_FAN;   implied = primCount + 2; break;
        default: return;
    }

    const UINT icount = indexCount ? indexCount : implied;
    // For an indexed draw the caller knows how many vertices the indices span;
    // without that number the safe fallback is the highest index + 1.
    UINT vcount = vertexCount;
    if (!vcount) {
        if (indices && indexBits && !glIB) {
            UINT maxIndex = 0;
            if (indexBits == 16) {
                const unsigned short *p = (const unsigned short *)indices;
                for (UINT i = 0; i < icount; ++i) if (p[i] > maxIndex) maxIndex = p[i];
            } else {
                const unsigned *p = (const unsigned *)indices;
                for (UINT i = 0; i < icount; ++i) if (p[i] > maxIndex) maxIndex = p[i];
            }
            vcount = maxIndex + 1;
        } else {
            vcount = implied;
        }
    }

    ++g_drawCalls;
    ++g_drawsSinceReport;
    g_vertsDrawn += icount;

    //  Diagnostic: drop the draw and every GL call it would make, leaving only
    //  the client-side cost of deciding to draw.
    if (g_nullDraw) {
#ifdef RAN_TIME_DRAWS
        { const double dt = nowSeconds() - drawStart;
          g_drawSeconds += dt; g_drawSecondsTotal += dt; }
#endif
        return;
    }
    if ((fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW) ++g_uiDraws;
    if (glTexture) ++g_texturedDraws;

    //  Counted before the cut, so the numbering does not shift as the limit
    //  moves; a draw past the limit simply is not issued.
    //  Nothing has cleared this frame and the buffer is not preserved, so what
    //  is under this draw is undefined. Make it defined.
    if (!g_rtActive && !g_frameClearedColor && !RanGL_SwapPreserved()) {
        g_frameClearedColor = true;
        //  A clear obeys the scissor and the depth mask, and both may be set
        //  from the last draw of the previous frame.
        glDisable(GL_SCISSOR_TEST);
        const int maskWas = g_gl.depthMask;
        if (!maskWas) glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        if (!maskWas) glDepthMask(GL_FALSE);
    }

    ++g_frameDraw;
    if (g_drawLog > 0) {
        //  Everything that decides what a draw costs to fill: how many pixels it
        //  can touch, and whether it can be rejected early.
        LOGI("DRAW %d prims %u fvf %04lx tex %u blend %d ztest %d zwrite %d",
             g_frameDraw, (unsigned)primCount, (unsigned long)fvf, glTexture,
             (int)g_gl.blendEnabled, (int)g_gl.depthTest, (int)g_gl.depthMask);
    }
    if (g_drawLimit >= 0 && g_frameDraw > g_drawLimit) return;
    if (g_rtActive) {
        ++g_rtDraws;
        if (g_rtW * g_rtH > g_rtBiggestW * g_rtBiggestH) { g_rtBiggestW = g_rtW; g_rtBiggestH = g_rtH; }
    }

    if (g_drawLimit > 0 && g_frameDraw == g_drawLimit) {
        static int s_saidFor = -1;
        if (s_saidFor != g_drawLimit) {
            s_saidFor = g_drawLimit;
            const void *vp = verts ? verts : g_diagVerts;
            float bx0 = 1e30f, by0 = 1e30f, bx1 = -1e30f, by1 = -1e30f;
            for (UINT q = 0; vp && q < vcount && q < 4096; ++q) {
                const float *sp = (const float *)((const BYTE *)vp + (size_t)q * stride);
                if (sp[0] < bx0) bx0 = sp[0];
                if (sp[0] > bx1) bx1 = sp[0];
                if (sp[1] < by0) by0 = sp[1];
                if (sp[1] > by1) by1 = sp[1];
            }
            LOGI("draw #%d: fvf=%08lX stride=%u tex=%u prim=%lu vc=%u "
                 "box=(%.0f,%.0f)-(%.0f,%.0f) rhw=%d blend=%lu(%lu,%lu) "
                 "cop=%lu,%lu,%lu aop=%lu,%lu,%lu tfactor=%.2f,%.2f,%.2f,%.2f",
                 g_frameDraw, (unsigned long)fvf, stride, glTexture,
                 (unsigned long)primCount, vcount, bx0, by0, bx1, by1,
                 (fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW ? 1 : 0,
                 (unsigned long)g_dsBlend, (unsigned long)g_dsSrc, (unsigned long)g_dsDst,
                 (unsigned long)g_colorOp, (unsigned long)g_colorArg1, (unsigned long)g_colorArg2,
                 (unsigned long)g_alphaOp, (unsigned long)g_alphaArg1, (unsigned long)g_alphaArg2,
                 g_texFactor[0], g_texFactor[1], g_texFactor[2], g_texFactor[3]);
        }
    }

    //  Per-draw dump of a world (non pre-transformed) draw: what the pixel
    //  pipeline was asked to do, and where vertex 0 actually lands in NDC.
    if (g_diagUI > 0 && verts && (fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW) {
        --g_diagUI;
        const float *v = (const float *)verts;
        //  Only vertex 0 is guaranteed to exist here: a 2-vertex line draw made
        //  reading a third vertex a read past the end of the caller's buffer.
        int dOff2 = 16;
        if (fvf & D3DFVF_NORMAL) dOff2 += 12;
        if (fvf & D3DFVF_PSIZE)  dOff2 += 4;
        unsigned diff2 = (fvf & D3DFVF_DIFFUSE)
            ? *(const unsigned *)((const char *)verts + dOff2) : 0xFFFFFFFFu;
        //  Where this batch actually lands. One draw covers many quads, so the
        //  first vertex says almost nothing; a box can be matched against a
        //  rectangle on screen.
        float ux0 = 1e30f, uy0 = 1e30f, ux1 = -1e30f, uy1 = -1e30f;
        {
            const void *uv = verts ? verts : g_diagVerts;
            for (UINT q = 0; uv && q < vcount; ++q) {
                const float *sp = (const float *)((const BYTE *)uv + (size_t)q * stride);
                if (sp[0] < ux0) ux0 = sp[0];
                if (sp[0] > ux1) ux1 = sp[0];
                if (sp[1] < uy0) uy0 = sp[1];
                if (sp[1] > uy1) uy1 = sp[1];
            }
        }
        RanPlat_Log(RANLOG_INFO, "RanUI",
            "tex=%u prim=%lu vc=%u box=(%.0f,%.0f)-(%.0f,%.0f) diff=%08X blend=%lu(%lu,%lu) "
            "cop=%lu,%lu,%lu aop=%lu,%lu,%lu",
            glTexture, (unsigned long)primCount, vertexCount, ux0, uy0, ux1, uy1, diff2,
            (unsigned long)g_dsBlend, (unsigned long)g_dsSrc, (unsigned long)g_dsDst,
            (unsigned long)g_colorOp, (unsigned long)g_colorArg1, (unsigned long)g_colorArg2,
            (unsigned long)g_alphaOp, (unsigned long)g_alphaArg1, (unsigned long)g_alphaArg2);
    }

    const void *dumpVerts = verts ? verts : g_diagVerts;
    if (g_diagDraws > 0 && dumpVerts && (fvf & D3DFVF_POSITION_MASK) != D3DFVF_XYZRHW) {
        --g_diagDraws;
        const float *v = (const float *)dumpVerts;
        float c[4] = { 0, 0, 0, 0 };
        if (mvp) for (int r = 0; r < 4; ++r)
            c[r] = v[0] * mvp[r] + v[1] * mvp[4 + r] + v[2] * mvp[8 + r] + mvp[12 + r];
        const float iw = c[3] != 0.0f ? 1.0f / c[3] : 0.0f;
        //  Colour offset, same FVF walk as the attribute setup below.
        int dOff = 0;
        switch (fvf & D3DFVF_POSITION_MASK) {
            case D3DFVF_XYZRHW: dOff = 16; break;
            case D3DFVF_XYZB1:  dOff = 16; break;
            case D3DFVF_XYZB2:  dOff = 20; break;
            case D3DFVF_XYZB3:  dOff = 24; break;
            case D3DFVF_XYZB4:  dOff = 28; break;
            case D3DFVF_XYZB5:  dOff = 32; break;
            default:            dOff = 12; break;
        }
        if (fvf & D3DFVF_NORMAL) dOff += 12;
        if (fvf & D3DFVF_PSIZE)  dOff += 4;
        unsigned diffuse = (fvf & D3DFVF_DIFFUSE)
            ? *(const unsigned *)((const char *)dumpVerts + dOff) : 0xFFFFFFFFu;
        RanPlat_Log(RANLOG_INFO, "RanPal",
            "vblend=%d M0=(%.1f,%.1f,%.1f) M1=(%.1f,%.1f,%.1f) M2=(%.1f,%.1f,%.1f) M3=(%.1f,%.1f,%.1f)",
            g_vertexBlend,
            g_worldM[12], g_worldM[13], g_worldM[14],
            g_worldM[28], g_worldM[29], g_worldM[30],
            g_worldM[44], g_worldM[45], g_worldM[46],
            g_worldM[60], g_worldM[61], g_worldM[62]);
        //  Where this draw actually lands, blended exactly as the shader
        //  will blend it. A piece whose box is an order of magnitude larger
        //  than a limb is the one drawing the burst.
        float bmin[3] = { 1e30f, 1e30f, 1e30f }, bmax[3] = { -1e30f, -1e30f, -1e30f };
        //  The same box before any matrix touches it. A group whose bind-space
        //  extent is already the whole body was grouped wrong; one that is
        //  small here and large above was blended wrong.
        float lmin[3] = { 1e30f, 1e30f, 1e30f }, lmax[3] = { -1e30f, -1e30f, -1e30f };
        float wsMin = 1e30f, wsMax = -1e30f;
        float slotSum[4][3] = { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } };
        unsigned slotN[4] = { 0, 0, 0, 0 };
        {
            int wOff = -1, wCount = 0;
            switch (fvf & D3DFVF_POSITION_MASK) {
                case D3DFVF_XYZB1: wOff = 12; wCount = 1; break;
                case D3DFVF_XYZB2: wOff = 12; wCount = 2; break;
                case D3DFVF_XYZB3: wOff = 12; wCount = 3; break;
                case D3DFVF_XYZB4: wOff = 12; wCount = 4; break;
                case D3DFVF_XYZB5: wOff = 12; wCount = 4; break;
                default: break;
            }
            //  Walk this draw's own indices when they are available, so the
            //  numbers describe this bone group and not the whole mesh.
            const void *di = indices ? indices : g_diagIndices;
            const UINT dbits = indices ? indexBits : g_diagIndexBits;
            const UINT steps = (di && icount) ? icount : vcount;
            for (UINT s = 0; s < steps; ++s) {
                UINT i = s;
                if (di && icount) i = (dbits == 16) ? ((const WORD *)di)[s] : ((const DWORD *)di)[s];
                if (i >= vcount) continue;
                const float *sp = (const float *)((const BYTE *)dumpVerts + (size_t)i * stride);
                float o[3];
                if (wOff >= 0 && g_vertexBlend > 0) {
                    const float *w = (const float *)((const BYTE *)sp + wOff);
                    float weight[4] = { 0, 0, 0, 0 }, used = 0.0f;
                    for (int k = 0; k < wCount && k < g_vertexBlend && k < 4; ++k) { weight[k] = w[k]; used += w[k]; }
                    if (g_vertexBlend < 4) weight[g_vertexBlend] = 1.0f - used;
                    o[0] = o[1] = o[2] = 0.0f;
                    for (int k = 0; k <= g_vertexBlend && k < 4; ++k) {
                        const float *m = g_worldM + k * 16;
                        o[0] += weight[k] * (sp[0]*m[0] + sp[1]*m[4] + sp[2]*m[8]  + m[12]);
                        o[1] += weight[k] * (sp[0]*m[1] + sp[1]*m[5] + sp[2]*m[9]  + m[13]);
                        o[2] += weight[k] * (sp[0]*m[2] + sp[1]*m[6] + sp[2]*m[10] + m[14]);
                    }
                } else { o[0] = sp[0]; o[1] = sp[1]; o[2] = sp[2]; }
                for (int k = 0; k < 3; ++k) {
                    if (sp[k] < lmin[k]) lmin[k] = sp[k];
                    if (sp[k] > lmax[k]) lmax[k] = sp[k];
                }
                //  Which slot this vertex rides, and where it ended up.
                if (wOff >= 0 && g_vertexBlend > 0) {
                    const float *w = (const float *)((const BYTE *)sp + wOff);
                    float used = 0.0f;
                    int best = g_vertexBlend;
                    float bestW = 0.0f;
                    for (int k = 0; k < wCount && k < g_vertexBlend && k < 4; ++k) {
                        used += w[k];
                        if (w[k] > bestW) { bestW = w[k]; best = k; }
                    }
                    if (1.0f - used > bestW) best = g_vertexBlend;
                    if (best >= 0 && best < 4) {
                        for (int k = 0; k < 3; ++k) slotSum[best][k] += o[k];
                        ++slotN[best];
                    }
                }
                if (wOff >= 0) {
                    const float *w = (const float *)((const BYTE *)sp + wOff);
                    float s = 0.0f;
                    for (int k = 0; k < wCount && k < 4; ++k) s += w[k];
                    if (s < wsMin) wsMin = s;
                    if (s > wsMax) wsMax = s;
                }

                //  Blended positions are world space and go through the view
                //  projection; a rigid draw's own MVP already carries its world
                //  matrix. Either way what comes out is clip space.
                const float *xf = (wOff >= 0 && g_vertexBlend > 0) ? g_viewProj : mvp;
                (void)xf;
                if (!xf) continue;
                float c[4];
                for (int r = 0; r < 4; ++r)
                    c[r] = o[0]*xf[r] + o[1]*xf[4+r] + o[2]*xf[8+r] + xf[12+r];
                if (c[3] <= 0.0001f) continue;          // behind the eye
                const float inv = 1.0f / c[3];
                const float ndc[3] = { c[0]*inv, c[1]*inv, c[2]*inv };
                for (int k = 0; k < 3; ++k) {
                    if (ndc[k] < bmin[k]) bmin[k] = ndc[k];
                    if (ndc[k] > bmax[k]) bmax[k] = ndc[k];
                }
            }
        }
        if (bmin[0] <= bmax[0]) {
            RanPlat_Log(RANLOG_INFO, "RanBox",
                "%s tex=%u vc=%u vblend=%d ndc=(%.2f %.2f)-(%.2f %.2f) "
                "s0=%u(%.1f,%.1f,%.1f) s1=%u(%.1f,%.1f,%.1f) "
                "s2=%u(%.1f,%.1f,%.1f) s3=%u(%.1f,%.1f,%.1f)",
                g_diagTag, glTexture, vcount, g_vertexBlend,
                bmin[0], bmin[1], bmax[0], bmax[1],
                slotN[0], slotN[0] ? slotSum[0][0]/slotN[0] : 0.f, slotN[0] ? slotSum[0][1]/slotN[0] : 0.f, slotN[0] ? slotSum[0][2]/slotN[0] : 0.f,
                slotN[1], slotN[1] ? slotSum[1][0]/slotN[1] : 0.f, slotN[1] ? slotSum[1][1]/slotN[1] : 0.f, slotN[1] ? slotSum[1][2]/slotN[1] : 0.f,
                slotN[2], slotN[2] ? slotSum[2][0]/slotN[2] : 0.f, slotN[2] ? slotSum[2][1]/slotN[2] : 0.f, slotN[2] ? slotSum[2][2]/slotN[2] : 0.f,
                slotN[3], slotN[3] ? slotSum[3][0]/slotN[3] : 0.f, slotN[3] ? slotSum[3][1]/slotN[3] : 0.f, slotN[3] ? slotSum[3][2]/slotN[3] : 0.f);
        }

        //  An untextured draw whose stage still samples the texture is the
        //  white-surface bug; the only useful thing to say about it is where
        //  it came from.
        if (!glTexture && (g_colorArg1 == 2 || g_colorArg2 == 2)) {
            char szTrace[512];
            RanDiag_Backtrace(szTrace, sizeof(szTrace));
            RanPlat_Log(RANLOG_WARN, "RanDraw",
                "untextured but sampling: fvf=%08X prim=%lu cop=%lu,%lu,%lu at%s",
                (unsigned)fvf, (unsigned long)primCount, (unsigned long)g_colorOp,
                (unsigned long)g_colorArg1, (unsigned long)g_colorArg2, szTrace);
        }

        RanPlat_Log(RANLOG_INFO, "RanDraw",
            "fvf=%08X stride=%u tex=%u prim=%lu idx=%u v0=(%.1f,%.1f,%.1f) ndc=(%.2f,%.2f,%.2f w=%.2f) "
            "diff=%08X vblend=%d blend=%lu(%lu,%lu) z=%lu zw=%lu cull=%lu atest=%lu/%lu light=%d/%d cop=%lu,%lu,%lu",
            (unsigned)fvf, stride, glTexture, (unsigned long)primCount, icount,
            v[0], v[1], v[2], c[0] * iw, c[1] * iw, c[2] * iw, c[3], diffuse, g_vertexBlend,
            (unsigned long)g_dsBlend, (unsigned long)g_dsSrc, (unsigned long)g_dsDst,
            (unsigned long)g_dsZ, (unsigned long)g_dsZW, (unsigned long)g_dsCull,
            (unsigned long)g_dsATest, (unsigned long)g_dsARef, g_lightingOn, g_lightCount,
            (unsigned long)g_colorOp, (unsigned long)g_colorArg1, (unsigned long)g_colorArg2);
    }

    //  The program is bound when the draw picks its shader variant, further
    //  down. Binding the unspecialised one here undid that for every draw that
    //  reused the previous variant, which is most of them.

    GLsizei streamVertexOffset = 0;
    //  A draw from the client's own buffers reuses the same layout every frame,
    //  so it gets a VAO of its own and one bind instead of ten calls. Whether
    //  that VAO still has to be described is answered below.
    bool needLayout = true;
    //  Measured on LDPlayer: binding a per-layout VAO costs more there than the
    //  ten attribute calls it saves, and the frame got slower with it. The
    //  cache is left in place but off, because on a real driver the trade goes
    //  the other way and this is where to turn it back on.
    const bool kUseVaoCache = false;
    if (glVB && kUseVaoCache) {
        VaoKey key;
        key.vb = glVB; key.ib = glIB; key.fvf = fvf; key.stride = stride;
        key.base = (GLsizei)vbByteOffset;
        std::map<VaoKey, GLuint>::iterator it = g_vaoCache.find(key);
        if (it != g_vaoCache.end()) {
            bindVAO(it->second);
            needLayout = false;
            ++g_vaoHits;
        } else {
            //  A map this large means something is generating layouts rather
            //  than reusing them; start again rather than grow without bound.
            if (g_vaoCache.size() > 2048) {
                for (std::map<VaoKey, GLuint>::iterator d = g_vaoCache.begin(); d != g_vaoCache.end(); ++d) {
                    GLuint v = d->second;
                    glDeleteVertexArrays(1, &v);
                }
                g_vaoCache.clear();
            }
            GLuint vao = 0;
            glGenVertexArrays(1, &vao);
            bindVAO(vao);
            g_vaoCache[key] = vao;
            ++g_vaoCreated;
        }
        bindArray(glVB);
        //  The comparison below describes the streaming VAO only; a draw that
        //  went through a cached one leaves it unknown.
        g_gl.fvf = 0xFFFFFFFFu;
        g_gl.vertexBuffer = 0xFFFFFFFFu;
    } else if (glVB) {
        bindVAO(g_vao);
        bindArray(glVB);
    } else {
        bindVAO(g_vao);
        if (!g_skipStream)
            streamVertexOffset = (GLsizei)g_streamVerts.write(verts, (GLsizei)stride * vcount);
    }

    const bool preTransformed = (fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW;

    //  Diagnostic: blend on the CPU from the same palette the shader would use,
    //  then draw the result as plain world-space geometry.
    static std::vector<BYTE> s_cpuSkinned;
    bool cpuSkinnedThisDraw = false;
    if (g_cpuSkin && verts && !preTransformed && g_vertexBlend > 0) {
        //  Where the weights sit inside this vertex, walked the same way as below.
        GLsizei wOff = -1;
        int wCount = 0;
        switch (fvf & D3DFVF_POSITION_MASK) {
            case D3DFVF_XYZB1: wOff = 12; wCount = 1; break;
            case D3DFVF_XYZB2: wOff = 12; wCount = 2; break;
            case D3DFVF_XYZB3: wOff = 12; wCount = 3; break;
            case D3DFVF_XYZB4: wOff = 12; wCount = 4; break;
            case D3DFVF_XYZB5: wOff = 12; wCount = 4; break;
            default: break;
        }
        if (wOff >= 0) {
            s_cpuSkinned.assign((const BYTE *)verts, (const BYTE *)verts + (size_t)stride * vcount);
            for (UINT i = 0; i < vcount; ++i) {
                float *pos = (float *)&s_cpuSkinned[(size_t)i * stride];
                const float *w = (const float *)(&s_cpuSkinned[(size_t)i * stride] + wOff);

                float weight[5] = { 0, 0, 0, 0, 0 };
                float used = 0.0f;
                for (int k = 0; k < wCount && k < g_vertexBlend; ++k) { weight[k] = w[k]; used += w[k]; }
                weight[g_vertexBlend] = 1.0f - used;

                const float px = pos[0], py = pos[1], pz = pos[2];
                float ox = 0.0f, oy = 0.0f, oz = 0.0f;
                for (int k = 0; k <= g_vertexBlend && k < 5; ++k) {
                    const float *m = g_worldM + k * 16;
                    //  Row-vector convention, the same as the shader's uWorldM * v.
                    ox += weight[k] * (px * m[0] + py * m[4] + pz * m[8]  + m[12]);
                    oy += weight[k] * (px * m[1] + py * m[5] + pz * m[9]  + m[13]);
                    oz += weight[k] * (px * m[2] + py * m[6] + pz * m[10] + m[14]);
                }
                pos[0] = ox; pos[1] = oy; pos[2] = oz;
            }
            verts = &s_cpuSkinned[0];
            cpuSkinnedThisDraw = true;
        }
    }
    //  With a client buffer the vertices start part-way in; with the streaming
    //  one they start wherever the ring handed out.
    const GLsizei vbBase = glVB ? (GLsizei)vbByteOffset : streamVertexOffset;

    // Walk the FVF in its fixed declaration order to find each component's offset.
    GLsizei off = 0;
    GLsizei posOff = 0, colorOff = -1, uvOff = -1, normalOff = -1;
    GLsizei blendOff = -1, blendCount = 0;
    GLsizei boneIdxOff = -1;
    switch (fvf & D3DFVF_POSITION_MASK) {
        case D3DFVF_XYZRHW: off = 16; break;
        case D3DFVF_XYZ:    off = 12; break;
        case D3DFVF_XYZB1:  off = 16; blendOff = 12; blendCount = 1; break;
        case D3DFVF_XYZB2:  off = 20; blendOff = 12; blendCount = 2; break;
        case D3DFVF_XYZB3:  off = 24; blendOff = 12; blendCount = 3; break;
        case D3DFVF_XYZB4:  off = 28; blendOff = 12; blendCount = 3;
                            //  With LASTBETA_UBYTE4 the fourth beta is four bytes
                            //  of palette slots rather than a weight.
                            if (fvf & D3DFVF_LASTBETA_UBYTE4) boneIdxOff = 24;
                            break;
        case D3DFVF_XYZB5:  off = 32; blendOff = 12; blendCount = 3; break;
        default:            off = 12; break;
    }
    if (fvf & D3DFVF_NORMAL)   { normalOff = off; off += 12; }
    if (fvf & D3DFVF_PSIZE)    off += 4;
    if (fvf & D3DFVF_DIFFUSE)  { colorOff = off; off += 4; }
    if (fvf & D3DFVF_SPECULAR) off += 4;
    const UINT texSets = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    if (texSets) uvOff = off;
    //  The second set, when there is one. Both are plain float2 in every vertex
    //  this engine declares (D3DFVF_TEXCOORDSIZE2 is the default), so the second
    //  starts one float2 after the first.
    int uv2Off = (texSets >= 2) ? (int)(off + 8) : -1;

    //  Re-specifying the attribute pointers is ~10 GL calls, and consecutive
    //  draws nearly always share a layout: same FVF, same stride, same buffer,
    //  same base. Only redo them when one of those changed.
    const GLuint layoutBuffer = glVB ? glVB : g_gl.arrayBuffer;
    //  With a VAO the answer came from the cache above; the streaming path
    //  still compares, because consecutive UI draws share a layout often
    //  enough to be worth the check.
    const bool layoutChanged = (glVB && kUseVaoCache) ? needLayout
                                    : (g_gl.fvf != fvf || g_gl.stride != stride ||
                                       g_gl.vertexBase != vbBase || g_gl.vertexBuffer != layoutBuffer);
    if (g_skipAttr) {
        // nothing: the layout stays whatever the last draw left behind
    } else if (g_haveAttribFormat && !g_noAttribFmt) {
        //  Format first, and only when the shape of a vertex actually changed.
        if (g_gl.fvf != fvf) {
            g_gl.fvf = fvf;
            g_callsAttrib += 10;               // format + binding + enables

            p_glVertexAttribFormat(0, preTransformed ? 4 : 3, GL_FLOAT, GL_FALSE, (GLuint)posOff);
            p_glVertexAttribBinding(0, 0);
            glEnableVertexAttribArray(0);

            if (blendOff >= 0) {
                p_glVertexAttribFormat(4, blendCount, GL_FLOAT, GL_FALSE, (GLuint)blendOff);
                p_glVertexAttribBinding(4, 0);
                glEnableVertexAttribArray(4);
            } else {
                glDisableVertexAttribArray(4);
                glVertexAttrib3f(4, 0.0f, 0.0f, 0.0f);
            }

            //  Palette slots, unnormalised: they are indices, not colours, so
            //  the shader wants 0..15 and not 0..1.
            if (boneIdxOff >= 0) {
                p_glVertexAttribFormat(6, 4, GL_UNSIGNED_BYTE, GL_FALSE, (GLuint)boneIdxOff);
                p_glVertexAttribBinding(6, 0);
                glEnableVertexAttribArray(6);
            } else {
                glDisableVertexAttribArray(6);
                glVertexAttrib4f(6, 0.0f, 0.0f, 0.0f, 0.0f);
            }

            if (colorOff >= 0) {
                //  D3DCOLOR is BGRA bytes; the shader undoes the swizzle.
                p_glVertexAttribFormat(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, (GLuint)colorOff);
                p_glVertexAttribBinding(1, 0);
                glEnableVertexAttribArray(1);
            } else {
                glDisableVertexAttribArray(1);
                glVertexAttrib4f(1, 1.0f, 1.0f, 1.0f, 1.0f);
            }

            if (normalOff >= 0) {
                p_glVertexAttribFormat(3, 3, GL_FLOAT, GL_FALSE, (GLuint)normalOff);
                p_glVertexAttribBinding(3, 0);
                glEnableVertexAttribArray(3);
            } else {
                glDisableVertexAttribArray(3);
                glVertexAttrib3f(3, 0.0f, 1.0f, 0.0f);
            }

            if (uvOff >= 0) {
                p_glVertexAttribFormat(2, 2, GL_FLOAT, GL_FALSE, (GLuint)uvOff);
                p_glVertexAttribBinding(2, 0);
                glEnableVertexAttribArray(2);
            } else {
                glDisableVertexAttribArray(2);
                glVertexAttrib2f(2, 0.0f, 0.0f);
            }

            if (uv2Off >= 0) {
                p_glVertexAttribFormat(5, 2, GL_FLOAT, GL_FALSE, (GLuint)uv2Off);
                p_glVertexAttribBinding(5, 0);
                glEnableVertexAttribArray(5);
            } else {
                glDisableVertexAttribArray(5);
                glVertexAttrib2f(5, 0.0f, 0.0f);
            }
        }

        //  Then where to read them from: one call, however much moved.
        const GLuint sourceBuffer = glVB ? glVB : g_gl.arrayBuffer;
        if (g_gl.vertexBuffer != sourceBuffer || g_gl.vertexBase != vbBase ||
            g_gl.stride != stride) {
            g_gl.vertexBuffer = sourceBuffer;
            g_gl.vertexBase = vbBase;
            g_gl.stride = stride;
            p_glBindVertexBuffer(0, sourceBuffer, (GLintptr)vbBase, (GLsizei)stride);
            ++g_callsAttrib;
        }
    } else if (layoutChanged) {
    g_gl.fvf = fvf; g_gl.stride = stride;
    g_gl.vertexBase = vbBase; g_gl.vertexBuffer = layoutBuffer;

    g_callsAttrib += 10;
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, preTransformed ? 4 : 3, GL_FLOAT, GL_FALSE, stride,
                          (const void *)(intptr_t)(posOff + vbBase));

    if (blendOff >= 0) {
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, blendCount, GL_FLOAT, GL_FALSE, stride,
                              (const void *)(intptr_t)(blendOff + vbBase));
    } else {
        glDisableVertexAttribArray(4);
        glVertexAttrib4f(4, 0.0f, 0.0f, 0.0f, 0.0f);
    }

    if (boneIdxOff >= 0) {
        glEnableVertexAttribArray(6);
        glVertexAttribPointer(6, 4, GL_UNSIGNED_BYTE, GL_FALSE, stride,
                              (const void *)(intptr_t)(boneIdxOff + vbBase));
    } else {
        glDisableVertexAttribArray(6);
        glVertexAttrib4f(6, 0.0f, 0.0f, 0.0f, 0.0f);
    }

    if (colorOff >= 0) {
        glEnableVertexAttribArray(1);
        // D3DCOLOR is BGRA bytes in memory; GL_BGRA is not in ES, so the shader
        // receives it as normalised RGBA and the swizzle is undone below.
        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                              (const void *)(intptr_t)(colorOff + vbBase));
    } else {
        glDisableVertexAttribArray(1);
        glVertexAttrib4f(1, 1.0f, 1.0f, 1.0f, 1.0f);
    }

    if (normalOff >= 0) {
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, stride,
                              (const void *)(intptr_t)(normalOff + vbBase));
    } else {
        glDisableVertexAttribArray(3);
        glVertexAttrib3f(3, 0.0f, 1.0f, 0.0f);
    }

    if (uvOff >= 0) {
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                              (const void *)(intptr_t)(uvOff + vbBase));
    } else {
        glDisableVertexAttribArray(2);
        glVertexAttrib2f(2, 0.0f, 0.0f);
    }

    if (uv2Off >= 0) {
        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, stride,
                              (const void *)(intptr_t)(uv2Off + vbBase));
    } else {
        glDisableVertexAttribArray(5);
        glVertexAttrib2f(5, 0.0f, 0.0f);
    }

    // Both paths end up with the same apparent winding: the UI flip and the
    // world path's lack of one cancel against GL's bottom-left window origin.
    // Measured, not derived - inverting the world path culled the entire scene
    // while leaving the two-sided foliage visible, which is what made it look
    // like a missing ground mesh rather than a culling bug.
    }

    //  Pre-transformed geometry is 2D: quads the client lays out in client
    //  pixels, whose winding is whatever the control happened to emit. The
    //  in-game HUD and the outer GUI disagree about it, and D3D's cull mode is
    //  meaningless for them, so they are never culled. Only world geometry is.
    if (preTransformed) {
        setCull(false, g_gl.frontFace);
    } else {
        //  A render target's rows run bottom-up, so that pass mirrors once and
        //  wants the opposite face.
        //  A render target's rows run bottom-up, so that pass mirrors once
        //  and wants the opposite face.
        setCull(g_cullWanted,
                g_rtActive ? (g_frontFace == GL_CCW ? GL_CW : GL_CCW) : g_frontFace);
    }

    //  Blending applies only to vertices that actually carry weights: the
    //  client leaves D3DRS_VERTEXBLEND set after drawing a character.
    if (!g_skipUniform) {
    const bool indexedBlend = (fvf & D3DFVF_LASTBETA_UBYTE4) != 0 &&
                              boneIdxOff >= 0 && !g_skipBlend && !cpuSkinnedThisDraw;
    const int blendCountNow = (!indexedBlend && blendOff >= 0 && !g_skipBlend && !cpuSkinnedThisDraw)
                                  ? g_vertexBlend : 0;
    const int lightingNow   = preTransformed ? 0 : g_lightingOn;

    //  The shader this draw wants, which decides where every uniform below
    //  goes. Everything in the key is a constant inside the program, so the
    //  driver compiles away the paths this draw does not use.
    useVariant(variantKey(preTransformed ? 1 : 0, lightingNow, g_specularOn,
                          g_fogMode, (g_fsProbe & 2) ? 0 : g_stage1Mode,
                          ((g_fsProbe & 8) == 0 && g_dsATest) ? 1 : 0,
                          (g_gammaOn && g_gammaLut) ? 1 : 0,
                          glTexture ? 1 : 0, indexedBlend ? 1 : 0, blendCountNow));
    applyProgramUniforms();

    setUniform1i(uIndexedBlend, indexedBlend ? 1 : 0);
    setUniform1i(uVertexBlend, blendCountNow);
    //  Whether this vertex format carries its own colour decides where the
    //  diffuse material comes from, so it is per draw, not per state block.
    setUniform1i(uHasVertexColor, colorOff >= 0 ? 1 : 0);
    setUniform1i(uPreTransformed, preTransformed ? 1 : 0);

    //  Pre-transformed vertices are never lit.
    //
    //  D3D9 skips the lighting pipeline entirely for D3DFVF_XYZRHW and uses the
    //  vertex diffuse as it is - which is why the interface never disables
    //  D3DRS_LIGHTING for itself: on the real device it does not have to. Here
    //  the world lighting state was applied to those draws as well, so the whole
    //  GUI dimmed and brightened with the in-game time of day, and went dark at
    //  night along with the scene behind it.
    //
    //  Set per draw, after the state block, because it depends on the vertex
    //  format rather than on any render state.
    setUniform1i(uLighting, preTransformed ? 0 : g_lightingOn);
    {
        const float viewport[2] = { (float)curWidth(), (float)curHeight() };
        if (uniformChanged(uViewport, viewport, 2)) glUniform2f(uViewport, viewport[0], viewport[1]);
    }
    setUniform1f(uFlipY, g_rtActive ? -1.0f : 1.0f);
    //  The shader reads uMVP only for world geometry that is not skinned:
    //  pre-transformed vertices use the viewport, and blended ones use
    //  uViewProj with the bone palette. Uploading it for those was a matrix a
    //  draw that nothing read.
    const bool usesMVP = !preTransformed && (cpuSkinnedThisDraw || !(blendOff >= 0 && g_vertexBlend > 0));
    //  CPU-skinned vertices are already in world space, so the view-projection
    //  is the whole transform.
    if (cpuSkinnedThisDraw) setUniformMatrix(uMVP, g_viewProj, 1);
    else if (mvp && usesMVP) setUniformMatrix(uMVP, mvp, 1);
    }

    //  Unit 0 is the only one this renderer uses and it is selected at init,
    //  so there is no glActiveTexture here. Rebinding the same texture is pure
    //  overhead, and ApplySampler is itself a no-op for a texture that already
    //  carries the current sampler state.
    const GLuint wanted = glTexture ? glTexture : g_whiteTex;
    if (!g_skipTex) {
        if (wanted != g_gl.texture2D) ++g_callsTexture;
        bindTex2D(wanted);
    }
    if (glTexture && !g_skipTex) RanGLR_ApplySampler(glTexture);
    if (!g_skipUniform) {
    setUniform1i(uTex, 0);
    setUniform1i(uUseTexture, glTexture ? 1 : 0);
    //  How many texels the interface is magnifying, for the sharper filter.
    //  Memoised on the texture name: consecutive draws share one far more often
    //  than not, so the map is barely touched.
    {
        static unsigned s_lastTex = 0xFFFFFFFFu;
        static float    s_lastW = 0.0f, s_lastH = 0.0f;
        if (glTexture != s_lastTex) {
            s_lastTex = glTexture;
            s_lastW = s_lastH = 0.0f;
            std::map<unsigned, std::pair<int, int> >::const_iterator d = g_texDims.find(glTexture);
            if (d != g_texDims.end()) {
                s_lastW = (float)d->second.first;
                s_lastH = (float)d->second.second;
            }
        }
        setUniform2f(uTexSize, s_lastW, s_lastH);
        setUniform1f(uUiSharpen, g_noUiSharp ? 1.0f : (float)RanGL_UIScale());
    }
    }

    if (glIB && indexBits) {
        bindElements(glIB);   // part of the bound VAO's state, and cached with it
        glDrawElements(mode, icount, indexBits == 16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                       (const void *)(intptr_t)ibByteOffset);
        ++g_callsDraw;
    } else if (indices && indexBits) {
        const GLsizei isize = (GLsizei)(indexBits / 8) * icount;
        const GLintptr ioffset = g_streamIndices.write(indices, isize);
        glDrawElements(mode, icount, indexBits == 16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                       (const void *)(intptr_t)ioffset);
        ++g_callsDraw;
    } else {
        glDrawArrays(mode, 0, vcount);
        ++g_callsDraw;
    }

#ifdef RAN_TIME_DRAWS
    { const double dt = nowSeconds() - drawStart;
      g_drawSeconds += dt; g_drawSecondsTotal += dt; }
#endif
}

extern "C" void RanGLR_Draw(DWORD primType, UINT primCount, const void *verts,
                            UINT stride, DWORD fvf, unsigned glTexture,
                            const float *mvp, const void *indices, UINT indexBits,
                            UINT indexCount, UINT vertexCount) {
    drawInternal(primType, primCount, verts, stride, fvf, glTexture, mvp,
                 indices, indexBits, indexCount, vertexCount, 0, 0, 0, 0);
}

//  Draw straight from buffers the client filled once.
extern "C" void RanGLR_DrawVBO(DWORD primType, UINT primCount, unsigned glVB, UINT vbByteOffset,
                               UINT stride, DWORD fvf, unsigned glTexture, const float *mvp,
                               unsigned glIB, UINT ibByteOffset, UINT indexBits,
                               UINT indexCount, UINT vertexCount) {
    drawInternal(primType, primCount, NULL, stride, fvf, glTexture, mvp,
                 NULL, indexBits, indexCount, vertexCount,
                 glVB, vbByteOffset, glIB, ibByteOffset);
}

//  Buffer objects for the client's vertex and index buffers.
extern "C" unsigned RanGLR_CreateBuffer(void) {
    if (!g_inited) return 0;
    GLuint b = 0;
    glGenBuffers(1, &b);
    return b;
}

unsigned long g_bufUploads = 0, g_bufUploadBytes = 0;
double        g_bufUploadSeconds = 0.0;

//  Which kind of write the buffer time is going into.
//
//  "whole" and "sub" are driver calls against a buffer the client owns; "map"
//  is a memcpy into the streaming ring. Splitting them is what showed that the
//  cost was never the bytes - 295 KB a frame took 10 ms - but the call.
struct BufKind { const char *name; unsigned long calls; double seconds; };
BufKind g_bufKinds[4] = { {"orphan",0,0.0}, {"map",0,0.0}, {"sub",0,0.0}, {"whole",0,0.0} };
void noteBufKind(int k, double dt) { ++g_bufKinds[k].calls; g_bufKinds[k].seconds += dt; }

//  How long the GPU spent on a section, rather than how long the CPU took to
//  submit it. On this tiled GPU the answer turned out to be "almost nothing in
//  any single section" - the fragment work all happens at the flush - which is
//  itself the finding: a fill-bound frame cannot be attributed this way, and
//  the A/B switches below are what measure it.
namespace {

typedef void (*PFN_GENQUERIES)(GLsizei, GLuint *);
typedef void (*PFN_DELETEQUERIES)(GLsizei, const GLuint *);
typedef void (*PFN_BEGINQUERY)(GLenum, GLuint);
typedef void (*PFN_ENDQUERY)(GLenum);
typedef void (*PFN_GETQUERYOBJECTUI64V)(GLuint, GLenum, GLuint64 *);
typedef void (*PFN_GETQUERYOBJECTUIV)(GLuint, GLenum, GLuint *);

PFN_GENQUERIES          p_glGenQueriesEXT = NULL;
PFN_DELETEQUERIES       p_glDeleteQueriesEXT = NULL;
PFN_BEGINQUERY          p_glBeginQueryEXT = NULL;
PFN_ENDQUERY            p_glEndQueryEXT = NULL;
PFN_GETQUERYOBJECTUI64V p_glGetQueryObjectui64vEXT = NULL;
PFN_GETQUERYOBJECTUIV   p_glGetQueryObjectuivEXT = NULL;

#define RAN_GL_TIME_ELAPSED_EXT           0x88BF
#define RAN_GL_QUERY_RESULT_EXT           0x8866
#define RAN_GL_QUERY_RESULT_AVAILABLE_EXT 0x8867

bool g_gpuTimerChecked = false, g_gpuTimerOn = false;

struct GpuSection { const char *name; GLuint query; double seconds; };
GpuSection g_gpuSections[16];
unsigned   g_gpuSectionCount = 0;
int        g_gpuActive = -1;

bool gpuTimerReady() {
    if (!g_gpuTimerChecked) {
        g_gpuTimerChecked = true;
        const char *ext = (const char *)glGetString(GL_EXTENSIONS);
        if (ext && strstr(ext, "GL_EXT_disjoint_timer_query")) {
            p_glGenQueriesEXT = (PFN_GENQUERIES)RanGL_ProcAddress("glGenQueriesEXT");
            p_glDeleteQueriesEXT = (PFN_DELETEQUERIES)RanGL_ProcAddress("glDeleteQueriesEXT");
            p_glBeginQueryEXT = (PFN_BEGINQUERY)RanGL_ProcAddress("glBeginQueryEXT");
            p_glEndQueryEXT = (PFN_ENDQUERY)RanGL_ProcAddress("glEndQueryEXT");
            p_glGetQueryObjectui64vEXT = (PFN_GETQUERYOBJECTUI64V)RanGL_ProcAddress("glGetQueryObjectui64vEXT");
            p_glGetQueryObjectuivEXT = (PFN_GETQUERYOBJECTUIV)RanGL_ProcAddress("glGetQueryObjectuivEXT");
            g_gpuTimerOn = p_glGenQueriesEXT && p_glBeginQueryEXT && p_glEndQueryEXT &&
                           p_glGetQueryObjectui64vEXT && p_glGetQueryObjectuivEXT;
        }
        LOGI("GPU timer queries: %s", g_gpuTimerOn ? "yes" : "no");
    }
    return g_gpuTimerOn;
}

int gpuSectionIndex(const char *name) {
    for (unsigned i = 0; i < g_gpuSectionCount; ++i)
        if (g_gpuSections[i].name == name) return (int)i;
    if (g_gpuSectionCount >= 16) return -1;
    GpuSection &sec = g_gpuSections[g_gpuSectionCount];
    sec.name = name; sec.query = 0; sec.seconds = 0.0;
    return (int)g_gpuSectionCount++;
}

//  Takes whatever results are ready; never waits, because waiting would stall
//  the thing being measured.
void collectGpuSections() {
    for (unsigned i = 0; i < g_gpuSectionCount; ++i) {
        if (!g_gpuSections[i].query) continue;
        GLuint ready = 0;
        p_glGetQueryObjectuivEXT(g_gpuSections[i].query, RAN_GL_QUERY_RESULT_AVAILABLE_EXT, &ready);
        if (!ready) continue;
        GLuint64 ns = 0;
        p_glGetQueryObjectui64vEXT(g_gpuSections[i].query, RAN_GL_QUERY_RESULT_EXT, &ns);
        g_gpuSections[i].seconds += (double)ns * 1e-9;
        p_glDeleteQueriesEXT(1, &g_gpuSections[i].query);
        g_gpuSections[i].query = 0;
    }
}

}

//  One section at a time: the passes worth measuring do not nest.
extern "C" void RanGLR_GpuSectionBegin(const char *name) {
    if (!g_inited || !gpuTimerReady() || g_gpuActive >= 0) return;
    const int i = gpuSectionIndex(name);
    if (i < 0 || g_gpuSections[i].query) return;      // last one not collected yet
    GLuint q = 0;
    p_glGenQueriesEXT(1, &q);
    if (!q) return;
    p_glBeginQueryEXT(RAN_GL_TIME_ELAPSED_EXT, q);
    g_gpuSections[i].query = q;
    g_gpuActive = i;
}

extern "C" void RanGLR_GpuSectionEnd(void) {
    if (g_gpuActive < 0) return;
    p_glEndQueryEXT(RAN_GL_TIME_ELAPSED_EXT);
    g_gpuActive = -1;
}

extern "C" void RanGLR_ReportGpuSections(unsigned frames) {
    if (!g_gpuTimerOn || !frames || !g_gpuSectionCount) return;
    collectGpuSections();
    char line[512] = "FRAME gpu:";
    for (unsigned i = 0; i < g_gpuSectionCount; ++i) {
        char one[64];
        snprintf(one, sizeof(one), " %s %.1fms", g_gpuSections[i].name,
                 g_gpuSections[i].seconds * 1000.0 / frames);
        strncat(line, one, sizeof(line) - strlen(line) - 1);
        g_gpuSections[i].seconds = 0.0;
    }
    LOGI("%s", line);
}

extern "C" void RanGLR_ReportBufferKinds(unsigned frames) {
    if (!frames) return;
    char line[256] = "FRAME buffer calls:";
    for (int k = 0; k < 4; ++k) {
        char one[64];
        snprintf(one, sizeof(one), " %s %.1f/f %.2fms", g_bufKinds[k].name,
                 (double)g_bufKinds[k].calls / frames, g_bufKinds[k].seconds * 1000.0 / frames);
        strncat(line, one, sizeof(line) - strlen(line) - 1);
        g_bufKinds[k].calls = 0; g_bufKinds[k].seconds = 0.0;
    }
    LOGI("%s", line);
}

extern "C" void RanGLR_TakeBufferStats(unsigned long *count, unsigned long *bytes, double *seconds) {
    if (count) *count = g_bufUploads;
    if (bytes) *bytes = g_bufUploadBytes;
    if (seconds) *seconds = g_bufUploadSeconds;
    g_bufUploads = g_bufUploadBytes = 0;
    g_bufUploadSeconds = 0.0;
}

//  Put a slice of vertices in the streaming ring and say where it landed.
//
//  The ring is persistently mapped, so this is a memcpy: no driver call, no
//  synchronisation, no allocation. Writing the same bytes into a buffer the
//  client owns costs about 120 us a call on this driver, whether through
//  glBufferSubData or an unsynchronised glMapBufferRange - measured at ninety
//  calls and 11 ms a frame, a third of the frame.
//
//  Returns 0 if the ring cannot take it, in which case the caller writes the
//  client's own buffer as before.
extern "C" int RanGLR_StreamVertices(const void *data, unsigned size,
                                     unsigned *outBuffer, unsigned *outOffset) {
    if (!g_inited || !data || !size) return 0;
    const double t0 = nowSeconds();
    const GLintptr off = g_streamVerts.write(data, (GLsizei)size);
    if (!g_streamVerts.buffer) return 0;
    if (outBuffer) *outBuffer = g_streamVerts.buffer;
    if (outOffset) *outOffset = (unsigned)off;
    ++g_bufUploads;
    g_bufUploadBytes += size;
    { const double dt = nowSeconds() - t0; g_bufUploadSeconds += dt; noteBufKind(1, dt); }
    return 1;
}

//  Throw a buffer's contents away and take fresh storage for it.
//
//  This is D3DLOCK_DISCARD: the client is about to rewrite the buffer and does
//  not care what was in it. Respecifying with no data lets the driver hand back
//  new memory at once and retire the old allocation behind the frames still
//  reading it, so the write that follows never waits.
extern "C" void RanGLR_OrphanBuffer(unsigned buffer, int isIndex, unsigned size) {
    if (!g_inited || !buffer || !size) return;
    const double t0 = nowSeconds();
    const GLenum target = isIndex ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
    if (isIndex) bindElements(buffer); else bindArray(buffer);
    glBufferData(target, (GLsizeiptr)size, NULL, GL_STREAM_DRAW);
    ++g_callsBuffer;
    { const double dt = nowSeconds() - t0; g_bufUploadSeconds += dt; noteBufKind(0, dt); }
    if (isIndex) g_gl.elementBuffer = 0xFFFFFFFFu;
}

//  Write a range the client has promised the GPU is not reading.
//
//  This is D3DLOCK_NOOVERWRITE. It is the fallback for when the streaming ring
//  cannot take the slice; the ring is faster still, because this call costs the
//  driver about 120 us whether it synchronises or not.
extern "C" void RanGLR_UpdateBufferRangeUnsync(unsigned buffer, int isIndex, unsigned offset,
                                               const void *data, unsigned size) {
    if (!g_inited || !buffer || !data || !size) return;
    const double t0 = nowSeconds();
    const GLenum target = isIndex ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
    if (isIndex) bindElements(buffer); else bindArray(buffer);
    void *dst = glMapBufferRange(target, (GLintptr)offset, (GLsizeiptr)size,
                                 GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT |
                                 GL_MAP_INVALIDATE_RANGE_BIT);
    if (dst) {
        memcpy(dst, data, size);
        glUnmapBuffer(target);
    } else {
        //  A driver that will not map falls back to the blocking write rather
        //  than to nothing being drawn.
        glBufferSubData(target, (GLintptr)offset, (GLsizeiptr)size, data);
    }
    ++g_callsBuffer;
    ++g_bufUploads;
    g_bufUploadBytes += size;
    { const double dt = nowSeconds() - t0; g_bufUploadSeconds += dt; noteBufKind(2, dt); }
    if (isIndex) g_gl.elementBuffer = 0xFFFFFFFFu;
}

extern "C" void RanGLR_UpdateBuffer(unsigned buffer, int isIndex, const void *data, unsigned size) {
    if (!g_inited || !buffer || !data || !size) return;
    const double t0 = nowSeconds();
    const GLenum target = isIndex ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
    //  Through the cache, so the next draw still knows what is bound.
    if (isIndex) bindElements(buffer); else bindArray(buffer);
    glBufferData(target, (GLsizeiptr)size, data, GL_STATIC_DRAW);
    ++g_callsBuffer;
    ++g_bufUploads;
    g_bufUploadBytes += size;
    { const double dt = nowSeconds() - t0; g_bufUploadSeconds += dt; noteBufKind(3, dt); }
    //  Ran outside the draw path: make the next draw bind for real.
    if (isIndex) g_gl.elementBuffer = 0xFFFFFFFFu;
}

//  Write part of a buffer GL already holds. The whole point is not to
//  re-specify storage: glBufferData would throw the old contents away and
//  make the driver find new memory, every frame, for buffers where the client
//  rewrote a few hundred bytes.
extern "C" void RanGLR_UpdateBufferRange(unsigned buffer, int isIndex, unsigned offset,
                                         const void *data, unsigned size) {
    if (!g_inited || !buffer || !data || !size) return;
    const double t0 = nowSeconds();
    const GLenum target = isIndex ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
    //  Through the cache, so the next draw still knows what is bound.
    if (isIndex) bindElements(buffer); else bindArray(buffer);
    glBufferSubData(target, (GLintptr)offset, (GLsizeiptr)size, data);
    ++g_callsBuffer;
    ++g_bufUploads;
    g_bufUploadBytes += size;
    { const double dt = nowSeconds() - t0; g_bufUploadSeconds += dt; noteBufKind(2, dt); }
    //  Ran outside the draw path: make the next draw bind for real.
    if (isIndex) g_gl.elementBuffer = 0xFFFFFFFFu;
}

extern "C" void RanGLR_DeleteBuffer(unsigned buffer) {
    if (!buffer) return;
    forgetVaosForBuffer(buffer);
    GLuint b = buffer;
    glDeleteBuffers(1, &b);
    //  GL unbinds a deleted buffer, and glGenBuffers hands the same name out
    //  again. Leaving it in the bind cache meant the next upload to the reused
    //  name was skipped as "already bound" and went to buffer 0 instead, so the
    //  draw that followed read an element buffer with no storage.
    if (g_gl.arrayBuffer == b)   g_gl.arrayBuffer = 0;
    if (g_gl.elementBuffer == b) g_gl.elementBuffer = 0;
    //  The attribute layout is described against a buffer name. Once names can
    //  be recycled, a later draw that happens to match the cached name, stride
    //  and FVF would skip re-specifying its pointers and read from whatever is
    //  bound - client memory, if nothing is. Force the next draw to describe
    //  itself again.
    g_gl.vertexBuffer = 0xFFFFFFFFu;
    g_gl.fvf = 0xFFFFFFFFu;
    g_gl.stride = 0xFFFFFFFFu;
    g_gl.vertexBase = -1;
}

// ------------------------------------------------------------ DXT support
//
// Almost every texture the client ships is DXT1/3/5. Two paths:
//
//   * the GPU understands S3TC (most Adreno/Mali parts expose
//     GL_EXT_texture_compression_s3tc) — the blocks are uploaded untouched,
//     which is both faster and a quarter of the memory;
//   * it does not — the blocks are decoded to RGBA here.
//
// The decoder is not a fallback nobody exercises: emulators frequently lack the
// extension, so it is the path the desktop test device takes.

namespace {

bool g_s3tcChecked = false;
bool g_haveS3TC = false;   // cleared if the driver rejects a compressed upload

bool haveS3TC() {
    if (!g_s3tcChecked) {
        g_s3tcChecked = true;
        const char *ext = (const char *)glGetString(GL_EXTENSIONS);
        //  An escape hatch, checked once: with /sdcard/ran/nos3tc present at
        //  launch every DXT texture is decoded here instead of handed to the
        //  driver. It exists to tell "the file is wrong" apart from "the driver
        //  mishandles this format", which no amount of reading the file can.
        if (RanPlat_DiagExists("nos3tc")) {
            g_haveS3TC = false;
            LOGI("S3TC (DXT) textures: decoded on CPU (nos3tc)");
            return g_haveS3TC;
        }
        g_haveS3TC = ext && (strstr(ext, "GL_EXT_texture_compression_s3tc") != NULL ||
                             strstr(ext, "GL_NV_texture_compression_s3tc") != NULL ||
                             strstr(ext, "GL_ANGLE_texture_compression_dxt5") != NULL);
        LOGI("S3TC (DXT) textures: %s", g_haveS3TC ? "native" : "decoded on CPU");
    }
    return g_haveS3TC;
}

#ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT  0x83F0
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif

// D3DFMT_DXT1..DXT5 are 827611204..827611208 ('DXT1'..'DXT5' as FOURCC).
enum {
    FMT_DXT1 = 0x31545844, FMT_DXT2 = 0x32545844, FMT_DXT3 = 0x33545844,
    FMT_DXT4 = 0x34545844, FMT_DXT5 = 0x35545844
};

bool isDXT(int f) {
    return f == FMT_DXT1 || f == FMT_DXT2 || f == FMT_DXT3 || f == FMT_DXT4 || f == FMT_DXT5;
}

GLenum s3tcInternal(int f) {
    switch (f) {
        case FMT_DXT1: return GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
        case FMT_DXT2: case FMT_DXT3: return GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
        default:       return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    }
}

void rgb565(unsigned short c, GLubyte *out) {
    // Bit replication, not a shift: 31 -> 255, so white stays white.
    unsigned r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
    out[0] = (GLubyte)((r << 3) | (r >> 2));
    out[1] = (GLubyte)((g << 2) | (g >> 4));
    out[2] = (GLubyte)((b << 3) | (b >> 2));
}

// Decodes one 4x4 colour block into `dst` (RGBA rows of `pitch` pixels).
void decodeColorBlock(const GLubyte *b, GLubyte *dst, int pitch, int bw, int bh,
                      bool dxt1Alpha) {
    unsigned short c0 = (unsigned short)(b[0] | (b[1] << 8));
    unsigned short c1 = (unsigned short)(b[2] | (b[3] << 8));
    GLubyte col[4][4];
    rgb565(c0, col[0]); col[0][3] = 255;
    rgb565(c1, col[1]); col[1][3] = 255;

    if (c0 > c1 || !dxt1Alpha) {
        for (int i = 0; i < 3; ++i) {
            col[2][i] = (GLubyte)((2 * col[0][i] + col[1][i]) / 3);
            col[3][i] = (GLubyte)((col[0][i] + 2 * col[1][i]) / 3);
        }
        col[2][3] = col[3][3] = 255;
    } else {
        for (int i = 0; i < 3; ++i)
            col[2][i] = (GLubyte)((col[0][i] + col[1][i]) / 2);
        col[2][3] = 255;
        col[3][0] = col[3][1] = col[3][2] = 0;
        col[3][3] = 0;                       // the transparent code of DXT1
    }

    unsigned bits = (unsigned)b[4] | ((unsigned)b[5] << 8) |
                    ((unsigned)b[6] << 16) | ((unsigned)b[7] << 24);
    for (int y = 0; y < bh; ++y) {
        for (int x = 0; x < bw; ++x) {
            unsigned code = (bits >> (2 * (y * 4 + x))) & 3;
            GLubyte *d = dst + ((size_t)y * pitch + x) * 4;
            d[0] = col[code][0]; d[1] = col[code][1];
            d[2] = col[code][2]; d[3] = col[code][3];
        }
    }
}

// Expands DXT1/2/3/4/5 to RGBA8. Returns false if the data is short.
bool decodeDXT(int fmt, int width, int height, const GLubyte *src, size_t size,
               std::vector<GLubyte> &out) {
    const int bw = (width + 3) / 4, bh = (height + 3) / 4;
    const bool hasAlphaBlock = (fmt != FMT_DXT1);
    const size_t blockBytes = hasAlphaBlock ? 16 : 8;
    if (size < (size_t)bw * bh * blockBytes) return false;

    out.assign((size_t)width * height * 4, 0);

    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            const GLubyte *blk = src + ((size_t)by * bw + bx) * blockBytes;
            const int px = bx * 4, py = by * 4;
            const int cw = (px + 4 <= width) ? 4 : width - px;
            const int ch = (py + 4 <= height) ? 4 : height - py;
            GLubyte *dst = &out[((size_t)py * width + px) * 4];

            const GLubyte *colorPart = hasAlphaBlock ? blk + 8 : blk;
            decodeColorBlock(colorPart, dst, width, cw, ch, !hasAlphaBlock);

            if (fmt == FMT_DXT2 || fmt == FMT_DXT3) {
                // 4 bits of alpha per texel, 16 texels, low nibble first.
                for (int y = 0; y < ch; ++y) {
                    unsigned row = (unsigned)blk[y * 2] | ((unsigned)blk[y * 2 + 1] << 8);
                    for (int x = 0; x < cw; ++x) {
                        unsigned a = (row >> (4 * x)) & 0xF;
                        dst[((size_t)y * width + x) * 4 + 3] = (GLubyte)((a << 4) | a);
                    }
                }
            } else if (fmt == FMT_DXT4 || fmt == FMT_DXT5) {
                GLubyte a0 = blk[0], a1 = blk[1];
                GLubyte a[8];
                a[0] = a0; a[1] = a1;
                if (a0 > a1) {
                    for (int i = 0; i < 6; ++i)
                        a[2 + i] = (GLubyte)(((6 - i) * a0 + (1 + i) * a1) / 7);
                } else {
                    for (int i = 0; i < 4; ++i)
                        a[2 + i] = (GLubyte)(((4 - i) * a0 + (1 + i) * a1) / 5);
                    a[6] = 0; a[7] = 255;
                }
                unsigned long long bits = 0;
                for (int i = 0; i < 6; ++i) bits |= (unsigned long long)blk[2 + i] << (8 * i);
                for (int y = 0; y < ch; ++y) {
                    for (int x = 0; x < cw; ++x) {
                        unsigned code = (unsigned)((bits >> (3 * (y * 4 + x))) & 7);
                        dst[((size_t)y * width + x) * 4 + 3] = a[code];
                    }
                }
            }
        }
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------- textures
// D3D surfaces are ARGB/BGRA byte order; GLES3 has no BGRA upload, so the
// conversion happens here once per upload rather than per pixel per frame.
//  Sampler state as the device last set it for stage 0, applied to whichever
//  texture a draw binds.
DWORD g_sampMin = 2, g_sampMag = 2, g_sampMip = 0, g_sampAddrU = 1, g_sampAddrV = 1;
DWORD g_sampAniso = 1;

GLenum wrapMode(DWORD d3d) {
    switch (d3d) {
        case 2:  return GL_MIRRORED_REPEAT;      // D3DTADDRESS_MIRROR
        case 3:  return GL_CLAMP_TO_EDGE;        // D3DTADDRESS_CLAMP
        case 4:  return GL_CLAMP_TO_EDGE;        // BORDER — no border colour in ES
        default: return GL_REPEAT;               // D3DTADDRESS_WRAP
    }
}

//  D3DTEXF_NONE/POINT/LINEAR/ANISOTROPIC crossed with the mip filter, which is
//  what decides between the four GL minification modes.
GLenum minFilterFor(DWORD minF, DWORD mipF, bool hasMips) {
    const bool linear = (minF != 1);             // anything but D3DTEXF_POINT
    if (!hasMips || mipF == 0) return linear ? GL_LINEAR : GL_NEAREST;
    if (mipF == 1) return linear ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST;
    return linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST;
}

extern "C" void RanGLR_SetSampler(DWORD minFilter, DWORD magFilter, DWORD mipFilter,
                                  DWORD addressU, DWORD addressV, DWORD maxAnisotropy) {
    const DWORD aniso = maxAnisotropy ? maxAnisotropy : 1;
    if (minFilter == g_sampMin && magFilter == g_sampMag && mipFilter == g_sampMip &&
        addressU == g_sampAddrU && addressV == g_sampAddrV && aniso == g_sampAniso)
        return;
    g_sampMin = minFilter; g_sampMag = magFilter; g_sampMip = mipFilter;
    g_sampAddrU = addressU; g_sampAddrV = addressV;
    g_sampAniso = aniso;
    ++g_samplerGeneration;
}

//  Anisotropic filtering is an extension in ES; ask once.
float maxAnisotropySupported() {
    static bool checked = false;
    static float best = 1.0f;
    if (!checked) {
        checked = true;
        const char *ext = (const char *)glGetString(GL_EXTENSIONS);
        if (ext && strstr(ext, "GL_EXT_texture_filter_anisotropic"))
            glGetFloatv(0x84FF /*GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT*/, &best);
    }
    return best;
}

//  Levels actually uploaded for a texture, so the sampler knows whether mip
//  filtering is even possible.
std::map<GLuint, int> g_texLevels;

//  Which sampler generation each texture was last given. GL keeps these
//  parameters in the texture object, so a texture that already has the current
//  state needs none of the six calls below - and with a couple of hundred draws
//  a frame, nearly all of them binding a different texture, those calls were
//  the single biggest part of submission.
//  What each texture actually carries, so only a genuine difference costs a
//  call. Zero means "never set", which no valid GL enum is.
struct TexSampler {
    GLint minFilter, magFilter, wrapS, wrapT;
    float aniso;
    TexSampler() : minFilter(0), magFilter(0), wrapS(0), wrapT(0), aniso(0.0f) {}
};
std::map<GLuint, TexSampler> g_texSampler;

extern "C" void RanGLR_ForgetSamplerState(unsigned tex) { g_texSampler.erase((GLuint)tex); }

extern "C" void RanGLR_ApplySampler(unsigned tex) {
    if (!g_inited || !tex) return;

    std::map<GLuint, int>::iterator it = g_texLevels.find(tex);
    const bool hasMips = it != g_texLevels.end() && it->second > 1;

    TexSampler want;
    want.minFilter = (GLint)minFilterFor(g_sampMin, g_sampMip, hasMips);
    want.magFilter = g_sampMag == 1 ? GL_NEAREST : GL_LINEAR;
    want.wrapS = (GLint)wrapMode(g_sampAddrU);
    want.wrapT = (GLint)wrapMode(g_sampAddrV);

    const float maxAniso = maxAnisotropySupported();
    if (maxAniso > 1.0f) {
        want.aniso = (float)g_sampAniso;
        if (g_sampMin != 3) want.aniso = 1.0f;           // only D3DTEXF_ANISOTROPIC asks for it
        if (want.aniso > maxAniso) want.aniso = maxAniso;
    }

    TexSampler &have = g_texSampler[tex];
    if (have.minFilter != want.minFilter) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, want.minFilter);
        have.minFilter = want.minFilter; ++g_callsTexture;
    }
    if (have.magFilter != want.magFilter) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, want.magFilter);
        have.magFilter = want.magFilter; ++g_callsTexture;
    }
    if (have.wrapS != want.wrapS) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, want.wrapS);
        have.wrapS = want.wrapS; ++g_callsTexture;
    }
    if (have.wrapT != want.wrapT) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, want.wrapT);
        have.wrapT = want.wrapT; ++g_callsTexture;
    }
    if (maxAniso > 1.0f && have.aniso != want.aniso) {
        glTexParameterf(GL_TEXTURE_2D, 0x84FE /*GL_TEXTURE_MAX_ANISOTROPY_EXT*/, want.aniso);
        have.aniso = want.aniso; ++g_callsTexture;
    }
}

extern "C" unsigned RanGLR_UploadTextureLevel(unsigned existing, int level, int width, int height,
                                              int d3dFormat, const void *bits, unsigned dataSize) {
    if (!g_inited || !bits || width <= 0 || height <= 0) return existing;
    GLuint tex = existing;
    if (!tex) {
        glGenTextures(1, &tex);
        bindTex2D(tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    } else {
        bindTex2D(tex);
    }

    if (level == 0) g_texDims[tex] = std::make_pair(width, height);

    // DXT first: it is what nearly every shipped texture is.
    if (isDXT(d3dFormat)) {
        if (haveS3TC()) {
            //  Clear any older error so the check below is about this upload.
            while (glGetError() != GL_NO_ERROR) {}
            glCompressedTexImage2D(GL_TEXTURE_2D, level, s3tcInternal(d3dFormat), width, height, 0,
                                   (GLsizei)dataSize, bits);
            const GLenum err = glGetError();
            if (err != GL_NO_ERROR) {
                //  The driver advertised S3TC and then refused it. Stop
                //  believing it and decode on the CPU from here on.
                LOGE("compressed upload rejected (0x%04X) for a %dx%d DXT texture — "
                     "decoding DXT on the CPU from now on", err, width, height);
                g_haveS3TC = false;
            }
        }
        if (!haveS3TC()) {
            std::vector<GLubyte> rgba;
            if (!decodeDXT(d3dFormat, width, height, (const GLubyte *)bits, dataSize, rgba))
                return tex;
            glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, width, height, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, &rgba[0]);
        }
        return tex;
    }

    const int n = width * height;
    switch (d3dFormat) {
        case D3DFMT_A8R8G8B8:
        case D3DFMT_X8R8G8B8: {
            GLubyte *rgba = new GLubyte[n * 4];
            const GLubyte *src = (const GLubyte *)bits;
            const bool opaque = (d3dFormat == D3DFMT_X8R8G8B8);
            for (int i = 0; i < n; ++i) {
                rgba[i * 4 + 0] = src[i * 4 + 2];   // B -> R
                rgba[i * 4 + 1] = src[i * 4 + 1];   // G
                rgba[i * 4 + 2] = src[i * 4 + 0];   // R -> B
                rgba[i * 4 + 3] = opaque ? 255 : src[i * 4 + 3];
            }
            glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, width, height, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, rgba);
            delete[] rgba;
            break;
        }
        //  Already in RGBA byte order, so it goes straight up. X8B8G8R8 carries
        //  no alpha and is forced opaque.
        case D3DFMT_A8B8G8R8:
        case D3DFMT_X8B8G8R8: {
            if (d3dFormat == D3DFMT_A8B8G8R8) {
                glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, width, height, 0, GL_RGBA,
                             GL_UNSIGNED_BYTE, bits);
            } else {
                GLubyte *rgba = new GLubyte[n * 4];
                memcpy(rgba, bits, (size_t)n * 4);
                for (int i = 0; i < n; ++i) rgba[i * 4 + 3] = 255;
                glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, width, height, 0, GL_RGBA,
                             GL_UNSIGNED_BYTE, rgba);
                delete[] rgba;
            }
            break;
        }
        //  24-bit uncompressed, stored B,G,R per pixel like the 32-bit form
        //  without the alpha byte. The item icon atlases ship in this format,
        //  and without a case here they fell through to "upload nothing" - the
        //  texture object existed, had no image, and GLES samples an incomplete
        //  texture as opaque black. That is every black item icon.
        case D3DFMT_R8G8B8: {
            GLubyte *rgba = new GLubyte[n * 4];
            const GLubyte *src = (const GLubyte *)bits;
            for (int i = 0; i < n; ++i) {
                rgba[i * 4 + 0] = src[i * 3 + 2];   // B -> R
                rgba[i * 4 + 1] = src[i * 3 + 1];   // G
                rgba[i * 4 + 2] = src[i * 3 + 0];   // R -> B
                rgba[i * 4 + 3] = 255;
            }
            glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, width, height, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, rgba);
            delete[] rgba;
            break;
        }
        case D3DFMT_R5G6B5:
            glTexImage2D(GL_TEXTURE_2D, level, GL_RGB, width, height, 0, GL_RGB,
                         GL_UNSIGNED_SHORT_5_6_5, bits);
            break;
        case D3DFMT_A4R4G4B4: {
            // ARGB4444 -> RGBA4444: rotate one nibble.
            GLushort *dst = new GLushort[n];
            const GLushort *src = (const GLushort *)bits;
            for (int i = 0; i < n; ++i) dst[i] = (GLushort)((src[i] << 4) | (src[i] >> 12));
            glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, width, height, 0, GL_RGBA,
                         GL_UNSIGNED_SHORT_4_4_4_4, dst);
            delete[] dst;
            break;
        }
        case D3DFMT_A8:
            glTexImage2D(GL_TEXTURE_2D, level, GL_R8, width, height, 0, GL_RED,
                         GL_UNSIGNED_BYTE, bits);
            break;
        case D3DFMT_A1R5G5B5:
        case D3DFMT_X1R5G5B5: {
            //  ARGB1555 -> RGBA5551: the colour rotates left one bit and the
            //  alpha bit moves from the top to the bottom. X1 carries no alpha,
            //  so force it opaque rather than inheriting the unused bit.
            const bool hasAlpha = (d3dFormat == D3DFMT_A1R5G5B5);
            GLushort *dst = new GLushort[n];
            const GLushort *src = (const GLushort *)bits;
            for (int i = 0; i < n; ++i) {
                const GLushort v = src[i];
                const GLushort a = hasAlpha ? (GLushort)((v >> 15) & 1) : (GLushort)1;
                dst[i] = (GLushort)(((v & 0x7FFF) << 1) | a);
            }
            glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, width, height, 0, GL_RGBA,
                         GL_UNSIGNED_SHORT_5_5_5_1, dst);
            delete[] dst;
            break;
        }
        default: {
            //  Leaving the texture empty is not neutral: GLES samples an
            //  incomplete texture as opaque black, which looks like content
            //  rather than like a missing format. Say which format it was.
            static std::set<int> s_said;
            if (s_said.size() < 16 && s_said.insert(d3dFormat).second)
                LOGE("no upload path for D3D format %d (0x%08X) — %dx%d texture "
                     "will sample as black", d3dFormat, (unsigned)d3dFormat, width, height);
            return tex;
        }
    }
    return tex;
}

//  Replace one rectangle of an existing texture, converting the same way the
//  full upload does. No mip regeneration: this runs many times a frame for the
//  font atlas, and rebuilding a chain each time is what made a glyph cost more
//  than the frame it appeared in.
extern "C" void RanGLR_UpdateTextureRect(unsigned tex, int x, int y, int w, int h,
                                         int d3dFormat, const void *bits, unsigned pitchBytes) {
    if (!g_inited || !tex || !bits || w <= 0 || h <= 0) return;
    bindTex2D(tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const int n = w * h;
    switch (d3dFormat) {
        case D3DFMT_A8R8G8B8:
        case D3DFMT_X8R8G8B8: {
            const bool opaque = (d3dFormat == D3DFMT_X8R8G8B8);
            GLubyte *rgba = new GLubyte[(size_t)n * 4];
            for (int row = 0; row < h; ++row) {
                const GLubyte *src = (const GLubyte *)bits + (size_t)row * pitchBytes;
                GLubyte *dst = rgba + (size_t)row * w * 4;
                for (int i = 0; i < w; ++i) {
                    dst[i * 4 + 0] = src[i * 4 + 2];
                    dst[i * 4 + 1] = src[i * 4 + 1];
                    dst[i * 4 + 2] = src[i * 4 + 0];
                    dst[i * 4 + 3] = opaque ? 255 : src[i * 4 + 3];
                }
            }
            glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            delete[] rgba;
            break;
        }
        case D3DFMT_R5G6B5: {
            GLushort *dst = new GLushort[n];
            for (int row = 0; row < h; ++row)
                memcpy(dst + (size_t)row * w, (const GLubyte *)bits + (size_t)row * pitchBytes,
                       (size_t)w * 2);
            glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, dst);
            delete[] dst;
            break;
        }
        case D3DFMT_A4R4G4B4: {
            GLushort *dst = new GLushort[n];
            for (int row = 0; row < h; ++row) {
                const GLushort *src = (const GLushort *)((const GLubyte *)bits + (size_t)row * pitchBytes);
                for (int i = 0; i < w; ++i)
                    dst[(size_t)row * w + i] = (GLushort)((src[i] << 4) | (src[i] >> 12));
            }
            glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, dst);
            delete[] dst;
            break;
        }
        case D3DFMT_A8: {
            GLubyte *dst = new GLubyte[n];
            for (int row = 0; row < h; ++row)
                memcpy(dst + (size_t)row * w, (const GLubyte *)bits + (size_t)row * pitchBytes, (size_t)w);
            glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RED, GL_UNSIGNED_BYTE, dst);
            delete[] dst;
            break;
        }
        default:
            break;                                  // leave it to the full path
    }
    ++g_texUpdates;
    g_texUpdateBytes += (unsigned long)n * 4;
}

//  Whether the GPU took the shipped DXT blocks or we had to decode them: a
//  device that refuses S3TC holds every texture uncompressed, which is four
//  times the memory and bandwidth and is worth knowing from a screenshot.
extern "C" int RanGLR_TexturesCompressed(void) { return haveS3TC() ? 1 : 0; }

extern "C" void RanGLR_DiagArm(int n) { g_diagDraws = n; }
extern "C" int  RanGLR_DiagArmed(void) { return g_diagDraws > 0 ? 1 : 0; }
extern "C" void RanGLR_DiagVerts(const void *p) { g_diagVerts = p; }
extern "C" void RanGLR_DiagIndices(const void *p, UINT bits) {
    g_diagIndices = p;
    g_diagIndexBits = bits;
}
extern "C" void RanGLR_DiagTag(const char *s) {
    if (!s) { g_diagTag[0] = '?'; g_diagTag[1] = 0; return; }
    size_t i = 0;
    for (; s[i] && i < sizeof(g_diagTag) - 1; ++i) g_diagTag[i] = s[i];
    g_diagTag[i] = 0;
}
extern "C" void RanGLR_DiagArmUI(int n) { g_diagUI = n; g_diagUpload = n; }

//  Called once the whole chain is in: a texture that shipped a single level
//  still gets mips, because the map is drawn far enough that the difference is
//  the shimmering the PC client does not have.
//  A cube face upload. The conversion is the same as the 2D path; only the
//  target differs, so the compressed and uncompressed cases are shared by
//  routing through a helper that takes the GL target.
extern "C" unsigned RanGLR_UploadCubeFaceLevel(unsigned existing, int face, int level,
                                               int width, int height, int d3dFormat,
                                               const void *bits, unsigned dataSize) {
    if (!g_inited || !bits || width <= 0 || height <= 0) return existing;
    if (face < 0 || face > 5) return existing;

    GLuint tex = existing;
    if (!tex) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
        glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
    }

    const GLenum target = (GLenum)(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face);

    if (isDXT(d3dFormat)) {
        if (haveS3TC()) {
            while (glGetError() != GL_NO_ERROR) {}
            glCompressedTexImage2D(target, level, s3tcInternal(d3dFormat), width, height, 0,
                                   (GLsizei)dataSize, bits);
            if (glGetError() == GL_NO_ERROR) return tex;
            g_haveS3TC = false;                  // driver lied; fall through and decode
        }
        std::vector<GLubyte> rgba;
        if (!decodeDXT(d3dFormat, width, height, (const GLubyte *)bits, dataSize, rgba)) return tex;
        glTexImage2D(target, level, GL_RGBA, width, height, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, &rgba[0]);
        return tex;
    }

    const int n = width * height;
    if (d3dFormat == D3DFMT_A8R8G8B8 || d3dFormat == D3DFMT_X8R8G8B8) {
        GLubyte *conv = new GLubyte[n * 4];
        const GLubyte *src = (const GLubyte *)bits;
        const bool opaque = (d3dFormat == D3DFMT_X8R8G8B8);
        for (int i = 0; i < n; ++i) {
            conv[i * 4 + 0] = src[i * 4 + 2];
            conv[i * 4 + 1] = src[i * 4 + 1];
            conv[i * 4 + 2] = src[i * 4 + 0];
            conv[i * 4 + 3] = opaque ? 255 : src[i * 4 + 3];
        }
        glTexImage2D(target, level, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, conv);
        delete[] conv;
    } else if (d3dFormat == D3DFMT_R8G8B8) {
        GLubyte *conv = new GLubyte[n * 4];
        const GLubyte *src = (const GLubyte *)bits;
        for (int i = 0; i < n; ++i) {
            conv[i * 4 + 0] = src[i * 3 + 2];
            conv[i * 4 + 1] = src[i * 3 + 1];
            conv[i * 4 + 2] = src[i * 3 + 0];
            conv[i * 4 + 3] = 255;
        }
        glTexImage2D(target, level, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, conv);
        delete[] conv;
    }
    return tex;
}

extern "C" void RanGLR_FinishCubeTexture(unsigned tex, int levels) {
    if (!g_inited || !tex) return;
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex);
    if (levels > 1) {
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, levels - 1);
    }
}

extern "C" void RanGLR_FinishTexture(unsigned tex, int levels, int d3dFormat) {
    if (!g_inited || !tex) return;
    ++g_texFullUploads;
    bindTex2D(tex);
    if (levels <= 1 && !isDXT(d3dFormat)) {
        while (glGetError() != GL_NO_ERROR) {}
        glGenerateMipmap(GL_TEXTURE_2D);
        {
            const GLenum e = glGetError();
            if (e != GL_NO_ERROR) {
                static int said = 0;
                if (said < 5) { ++said;
                    LOGE("glGenerateMipmap FAILED 0x%04X tex=%u d3dfmt=%d", e, tex, d3dFormat); }
            }
        }
        levels = 2;                                   // "has mips" is all the sampler needs
    }
    if (levels > 1) glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);
    g_texLevels[tex] = levels;
    //  Whether the texture has mips decides its min filter, so the sampler
    //  state it was given before this is no longer the right one.
    g_texSampler.erase(tex);
}

extern "C" void RanGLR_LogStats(void) {
    //  The frame profiler only runs in the game stage, so the flag files were
    //  unreadable at character select - which is exactly where the character
    //  needs looking at. The stats report runs everywhere, so refresh here too.
    RanGLR_RefreshDiagnostics();
    LOGI("draws=%lu (ui=%lu textured=%lu) verts=%lu texfull=%lu texrect=%lu (%lu KB) vao=%lu new/%lu hit (%u live) glErr=0x%04X",
         g_drawCalls, g_uiDraws, g_texturedDraws, g_vertsDrawn,
         g_texFullUploads, g_texUpdates, g_texUpdateBytes / 1024,
         g_vaoCreated, g_vaoHits, (unsigned)g_vaoCache.size(), glGetError());
    LOGI("into render targets: %lu draws, largest %dx%d (frame is %dx%d)",
         g_rtDraws, g_rtBiggestW, g_rtBiggestH, RanGL_Width(), RanGL_Height());
    g_rtDraws = 0; g_rtBiggestW = g_rtBiggestH = 0;
    g_drawCalls = g_uiDraws = g_texturedDraws = g_vertsDrawn = 0;
    g_texUpdates = g_texUpdateBytes = g_texFullUploads = 0;
    g_vaoCreated = g_vaoHits = 0;
}

//  Upload accounting: a scene that goes black after a lot of content is loaded
//  is usually the GPU refusing new textures, and glGetError is the only place
//  that says so.
extern "C" void RanGLR_LogTextureStats(void) {
    RanPlat_Log(RANLOG_INFO, "RanTex", "uploads=%lu bytes=%lu lastErr=0x%04X",
                        g_texUploads, g_texBytes, g_texLastError);
}

extern "C" void RanGLR_DeleteTexture(unsigned tex) {
    g_texSampler.erase((GLuint)tex);
    g_texLevels.erase((GLuint)tex);
    g_texDims.erase((GLuint)tex);
    forgetTex2D((GLuint)tex);
    if (tex) { GLuint t = tex; glDeleteTextures(1, &t); }
}

//  --- texture readback probe -------------------------------------------
//
//  Names every texture as it is uploaded, and on request reads a few texels of
//  each one back off the GPU. The file on disk and the pixels the sampler sees
//  are different things, and a bug in between - a wrong internal format, a
//  short upload, a driver that lies about a compressed format - shows up here
//  and nowhere else.
namespace {
struct NotedTex { unsigned gl; std::string name; };
std::vector<NotedTex> g_notedTex;
}

extern "C" void RanD3D_NoteTexture(unsigned glTex, const char *name) {
    if (!glTex || !name) return;
    for (size_t i = 0; i < g_notedTex.size(); ++i)
        if (g_notedTex[i].gl == glTex) { g_notedTex[i].name = name; return; }
    NotedTex t; t.gl = glTex; t.name = name;
    g_notedTex.push_back(t);
}

//  Read one pixel back out of whatever is currently being drawn into, and say
//  what it is. The loading screen renders from its own thread and outside the
//  frame path the other probes hang off, so it needs a probe it can call
//  itself, once per draw, to find which draw blackens the screen.
//  What GL is actually configured to do, read from GL itself rather than from
//  the shim's own record of what it meant to set. The two disagreeing is
//  exactly the class of bug this is for.
extern "C" void RanGL_ProbeState(const char *tag) {
    GLint v[4] = { 0, 0, 0, 0 };
    GLboolean m[4] = { 0, 0, 0, 0 };
    LOGI("gl state [%s]:", tag ? tag : "?");
    LOGI("  BLEND=%d src=%d dst=%d  DEPTH_TEST=%d depthMask=%d",
         (int)glIsEnabled(GL_BLEND),
         (glGetIntegerv(GL_BLEND_SRC_RGB, v), v[0]),
         (glGetIntegerv(GL_BLEND_DST_RGB, v), v[0]),
         (int)glIsEnabled(GL_DEPTH_TEST),
         (glGetBooleanv(GL_DEPTH_WRITEMASK, m), (int)m[0]));
    glGetBooleanv(GL_COLOR_WRITEMASK, m);
    LOGI("  colorMask=%d%d%d%d  DITHER=%d  SCISSOR=%d  CULL=%d  STENCIL=%d",
         (int)m[0], (int)m[1], (int)m[2], (int)m[3],
         (int)glIsEnabled(GL_DITHER), (int)glIsEnabled(GL_SCISSOR_TEST),
         (int)glIsEnabled(GL_CULL_FACE), (int)glIsEnabled(GL_STENCIL_TEST));
    LOGI("  SAMPLE_ALPHA_TO_COVERAGE=%d SAMPLE_COVERAGE=%d samples=%d",
         (int)glIsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE),
         (int)glIsEnabled(GL_SAMPLE_COVERAGE),
         (glGetIntegerv(GL_SAMPLES, v), v[0]));
    glGetIntegerv(GL_SCISSOR_BOX, v);
    LOGI("  scissorBox=(%d,%d %dx%d)", v[0], v[1], v[2], v[3]);
    glGetIntegerv(GL_VIEWPORT, v);
    LOGI("  viewport=(%d,%d %dx%d)", v[0], v[1], v[2], v[3]);
    LOGI("  uniforms: lighting=%d gammaOn=%d alphaTest=%lu ref=%lu stage1=%d fog=%d",
         g_lightingOn, g_gammaOn, (unsigned long)g_dsATest,
         (unsigned long)g_dsARef, g_stage1Mode, g_fogMode);
    LOGI("  colorOp=%lu arg1=%lu arg2=%lu  alphaOp=%lu arg1=%lu arg2=%lu",
         (unsigned long)g_colorOp, (unsigned long)g_colorArg1, (unsigned long)g_colorArg2,
         (unsigned long)g_alphaOp, (unsigned long)g_alphaArg1, (unsigned long)g_alphaArg2);
}

extern "C" int RanGLR_TextureSize(unsigned glTex, int *w, int *h);

//  Read a whole uploaded texture back and write it out as raw RGBA, so what
//  the GPU actually holds can be compared byte for byte against the file it
//  came from. Four sampled texels cannot tell a decode bug from a draw bug.
extern "C" void RanD3D_DumpTexture(const char *want) {
    if (!want) return;
    for (size_t i = 0; i < g_notedTex.size(); ++i) {
        if (g_notedTex[i].name.find(want) == std::string::npos) continue;

        const unsigned tex = g_notedTex[i].gl;
        int w = 0, h = 0;
        RanGLR_TextureSize(tex, &w, &h);
        if (w <= 0 || h <= 0) { LOGI("dump %s: no size", g_notedTex[i].name.c_str()); continue; }

        GLuint fbo = 0; GLint prevFbo = 0;
        glGenFramebuffers(1, &fbo);
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
            std::vector<unsigned char> px((size_t)w * h * 4, 0);
            glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, &px[0]);
            std::string out = g_notedTex[i].name + ".raw";
            FILE *f = RanPlat_DiagOpenWrite(out.c_str());
            if (f) {
                fwrite(&px[0], 1, px.size(), f);
                fclose(f);
                LOGI("dump %s: %dx%d RGBA written", out.c_str(), w, h);
            } else {
                LOGI("dump %s: could not open the output", out.c_str());
            }
        } else {
            LOGI("dump %s: not readable", g_notedTex[i].name.c_str());
        }
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
        glDeleteFramebuffers(1, &fbo);
    }
}

extern "C" void RanGL_ProbePixel(int x, int y, const char *tag) {
    unsigned char px[4] = { 0, 0, 0, 0 };
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    LOGI("pixel probe %-12s (%d,%d) = %02X%02X%02X%02X  glErr=0x%04X",
         tag ? tag : "?", x, y, px[0], px[1], px[2], px[3],
         (unsigned)glGetError());
}

extern "C" void RanD3D_ProbeTextures(void) {
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    LOGI("texture probe: %u textures", (unsigned)g_notedTex.size());
    for (size_t i = 0; i < g_notedTex.size(); ++i) {
        const NotedTex &t = g_notedTex[i];
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, t.gl, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            LOGI("  tex %u %s: not readable", t.gl, t.name.c_str());
            continue;
        }
        //  Four texels rather than one: a uniform colour and a decode that
        //  happens to be right in one corner look the same from a single sample.
        unsigned char px[4][4];
        const int at[4][2] = { { 0, 0 }, { 1, 1 }, { 3, 5 }, { 7, 3 } };
        for (int k = 0; k < 4; ++k) {
            px[k][0] = px[k][1] = px[k][2] = px[k][3] = 0;
            glReadPixels(at[k][0], at[k][1], 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px[k]);
        }
        LOGI("  tex %u %s: %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X",
             t.gl, t.name.c_str(),
             px[0][0], px[0][1], px[0][2], px[0][3],
             px[1][0], px[1][1], px[1][2], px[1][3],
             px[2][0], px[2][1], px[2][2], px[2][3],
             px[3][0], px[3][1], px[3][2], px[3][3]);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
    glDeleteFramebuffers(1, &fbo);
}

//  The base-level size of a texture the renderer has uploaded.
//
//  The touch overlay draws the skill icons itself, with its own shader, so it
//  cannot ask D3D how big they are - but it needs the texel count to know how
//  far it is magnifying them.
extern "C" int RanGLR_TextureSize(unsigned glTex, int *w, int *h) {
    std::map<unsigned, std::pair<int, int> >::const_iterator d = g_texDims.find(glTex);
    if (d == g_texDims.end()) return 0;
    if (w) *w = d->second.first;
    if (h) *h = d->second.second;
    return 1;
}

//  Should characters be drawn into the water reflection?
//
//  No, by default, and it is the single biggest saving available on this port.
//  The reflection is a second pass over every character into a 512x512 target:
//  measured at 41% of all skinned draws in a busy scene (100 of 244 a frame),
//  which is exactly the cost that grows with the number of players and mobs on
//  screen.
//
//  It is also not correct here. The pass leans on SetClipPlane to cut the
//  reflection at the water surface, and this shim does not implement clip
//  planes, so what it draws is not clipped to the water anyway.
//
//  /sdcard/ran/reflectchars turns them back on, live, for comparison.
extern "C" int RanGLR_ReflectChars(void) { return g_reflectChars ? 1 : 0; }

//  --- character shadow budget ---------------------------------------------
//
//  Every character, mob, pet and summon is rendered a second time into the
//  512x512 shadow buffer, through the one chokepoint
//  DxShadowMap::RenderShadowCharMob. That is the cost that grows with the
//  number of things on screen, and in a crowd it was measured at well over a
//  hundred extra draws a frame.
//
//  Rather than turn shadows off, only the first few casters of each frame get
//  one. The client renders the player before the crowd, so the player keeps a
//  shadow and the crowd loses theirs, which is the right way round.
//
//  The number is read from /sdcard/ran/shadowcount, so it can be tuned against
//  a real scene without a rebuild. 0 disables character shadows entirely.
extern "C" void RanGLR_ResetShadowBudget(void) {
    static int s_polled = 0;
    if ((s_polled++ % 120) == 0) {
        //  Same as above: do not make the resolver log a miss every time.
        FILE *f = (RanPlat_DiagExists("shadowcount"))
                      ? RanPlat_DiagOpen("shadowcount") : NULL;
        if (f) {
            char buf[16] = { 0 };
            if (fread(buf, 1, sizeof(buf) - 1, f) > 0) {
                const int v = atoi(buf);
                if (v >= 0 && v <= 256) g_shadowBudget = v;
            }
            fclose(f);
        } else {
            g_shadowBudget = 6;
        }
    }
    g_shadowLeft = g_shadowBudget;
}

extern "C" int RanGLR_TakeShadowSlot(void) {
    if (g_shadowLeft <= 0) return 0;
    --g_shadowLeft;
    return 1;
}
