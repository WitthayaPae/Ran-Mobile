//  D3DXLoadMeshHierarchyFromX* and ID3DXSkinInfo.
//
//  Every character, weapon and attachment in the game is a skinned `.x` file
//  loaded through this path: DxSkinMesh9::OnCreateSkin hands D3DX its own
//  ID3DXALLOCATEHIERARCHY, keeps the frames it gets back as its skeleton-bound
//  hierarchy, and asks the skin info to turn the mesh into a blended one it can
//  draw with fixed-function vertex blending:
//
//      pSkinInfo->ConvertToBlendedMesh( ... &NumInfl, &NumAttributeGroups,
//                                       &pBoneCombinationBuf, &MeshData.pMesh );
//      ...
//      SetTransform( D3DTS_WORLDMATRIX(i), &boneMatrix[i] );
//      SetRenderState( D3DRS_VERTEXBLEND, NumBlend );
//      pMesh->DrawSubset( iAttrib );
//
//  So the blended mesh produced here has to satisfy three things at once: each
//  attribute group uses at most `NumInfl` bones, every vertex carries its
//  weights in the slot order that group's bone combination declares, and the
//  group's AttribId still names the original material.

#include "windows.h"
#include <d3d9.h>
#include <d3dx9.h>

#include "xfile_parse.h"
#include "xmesh_build.h"

#include <android/log.h>
#include <math.h>
#include <string.h>
#include <map>
#include <string>
#include <algorithm>
#include <vector>
#include <set>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "RanXH", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "RanXH", __VA_ARGS__)

namespace {

//  The most bones a single attribute group may reference. Four is not a
//  tuning choice: D3DRS_VERTEXBLEND is an enum whose largest non-indexed value,
//  D3DVBF_3WEIGHTS, means four matrices. A group of five would make the engine
//  set a value D3D9 does not define, and the draw would silently stop blending.
const DWORD kMaxPalette = 4;

//  One-shot load reporting (see g_logLoads below, declared early so the skin
//  conversion can report too).
extern int g_logLoads;

struct Influence { DWORD vertex; float weight; };

struct Bone {
    std::string            name;
    D3DXMATRIX             offset;
    std::vector<Influence> influences;
};

// ------------------------------------------------------------------ buffer
class XBuffer : public ID3DXBuffer {
public:
    LONG m_ref;
    std::vector<BYTE> m_bytes;
    explicit XBuffer(size_t n) : m_ref(1), m_bytes(n, 0) {}
    HRESULT __stdcall QueryInterface(REFIID, void **ppv) { *ppv = this; AddRef(); return S_OK; }
    ULONG   __stdcall AddRef() { return (ULONG)++m_ref; }
    ULONG   __stdcall Release() { LONG r = --m_ref; if (r <= 0) { delete this; return 0; } return (ULONG)r; }
    LPVOID  __stdcall GetBufferPointer() { return m_bytes.empty() ? NULL : &m_bytes[0]; }
    DWORD   __stdcall GetBufferSize() { return (DWORD)m_bytes.size(); }
};

// --------------------------------------------------------------- skin info
class RanSkinInfo : public ID3DXSkinInfo {
public:
    LONG              m_ref;
    DWORD             m_numVertices;
    DWORD             m_fvf;
    float             m_minInfluence;
    std::vector<Bone> m_bones;

    RanSkinInfo(DWORD numVertices, DWORD fvf, DWORD numBones)
        : m_ref(1), m_numVertices(numVertices), m_fvf(fvf), m_minInfluence(0.0f) {
        m_bones.resize(numBones);
        for (size_t i = 0; i < m_bones.size(); ++i) D3DXMatrixIdentity(&m_bones[i].offset);
    }

    HRESULT __stdcall QueryInterface(REFIID, void **ppv) { *ppv = this; AddRef(); return S_OK; }
    ULONG   __stdcall AddRef() { return (ULONG)++m_ref; }
    ULONG   __stdcall Release() { LONG r = --m_ref; if (r <= 0) { delete this; return 0; } return (ULONG)r; }

    // ---- influences
    HRESULT __stdcall SetBoneInfluence(DWORD bone, DWORD num, const DWORD *vertices, const FLOAT *weights) {
        if (bone >= m_bones.size()) return D3DERR_INVALIDCALL;
        std::vector<Influence> &v = m_bones[bone].influences;
        v.clear();
        v.reserve(num);
        for (DWORD i = 0; i < num; ++i) {
            Influence in;
            in.vertex = vertices ? vertices[i] : 0;
            in.weight = weights ? weights[i] : 0.0f;
            v.push_back(in);
        }
        return D3D_OK;
    }
    HRESULT __stdcall SetBoneVertexInfluence(DWORD bone, DWORD infl, float weight) {
        if (bone >= m_bones.size() || infl >= m_bones[bone].influences.size()) return D3DERR_INVALIDCALL;
        m_bones[bone].influences[infl].weight = weight;
        return D3D_OK;
    }
    DWORD __stdcall GetNumBoneInfluences(DWORD bone) {
        return bone < m_bones.size() ? (DWORD)m_bones[bone].influences.size() : 0;
    }
    HRESULT __stdcall GetBoneInfluence(DWORD bone, DWORD *vertices, FLOAT *weights) {
        if (bone >= m_bones.size()) return D3DERR_INVALIDCALL;
        const std::vector<Influence> &v = m_bones[bone].influences;
        for (size_t i = 0; i < v.size(); ++i) {
            if (vertices) vertices[i] = v[i].vertex;
            if (weights)  weights[i]  = v[i].weight;
        }
        return D3D_OK;
    }
    HRESULT __stdcall GetBoneVertexInfluence(DWORD bone, DWORD infl, float *pWeight, DWORD *pVertex) {
        if (bone >= m_bones.size() || infl >= m_bones[bone].influences.size()) return D3DERR_INVALIDCALL;
        if (pWeight) *pWeight = m_bones[bone].influences[infl].weight;
        if (pVertex) *pVertex = m_bones[bone].influences[infl].vertex;
        return D3D_OK;
    }
    HRESULT __stdcall FindBoneVertexInfluenceIndex(DWORD bone, DWORD vertex, DWORD *pIndex) {
        if (bone >= m_bones.size()) return D3DERR_INVALIDCALL;
        const std::vector<Influence> &v = m_bones[bone].influences;
        for (size_t i = 0; i < v.size(); ++i)
            if (v[i].vertex == vertex) { if (pIndex) *pIndex = (DWORD)i; return D3D_OK; }
        return D3DERR_INVALIDCALL;
    }
    HRESULT __stdcall GetMaxVertexInfluences(DWORD *pMax) {
        if (!pMax) return D3DERR_INVALIDCALL;
        std::vector<DWORD> perVertex(m_numVertices, 0);
        for (size_t b = 0; b < m_bones.size(); ++b)
            for (size_t i = 0; i < m_bones[b].influences.size(); ++i) {
                DWORD v = m_bones[b].influences[i].vertex;
                if (v < m_numVertices) ++perVertex[v];
            }
        DWORD best = 0;
        for (DWORD v = 0; v < m_numVertices; ++v) if (perVertex[v] > best) best = perVertex[v];
        *pMax = best;
        return D3D_OK;
    }
    DWORD __stdcall GetNumBones() { return (DWORD)m_bones.size(); }
    HRESULT __stdcall GetMaxFaceInfluences(LPDIRECT3DINDEXBUFFER9, DWORD, DWORD *pMax) {
        // Without the index buffer contents the palette bound is the honest answer.
        if (pMax) *pMax = kMaxPalette;
        return D3D_OK;
    }

    HRESULT __stdcall SetMinBoneInfluence(FLOAT f) { m_minInfluence = f; return D3D_OK; }
    FLOAT   __stdcall GetMinBoneInfluence() { return m_minInfluence; }

    HRESULT __stdcall SetBoneName(DWORD bone, LPCSTR name) {
        if (bone >= m_bones.size()) return D3DERR_INVALIDCALL;
        m_bones[bone].name = name ? name : "";
        return D3D_OK;
    }
    LPCSTR __stdcall GetBoneName(DWORD bone) {
        return bone < m_bones.size() ? m_bones[bone].name.c_str() : NULL;
    }
    HRESULT __stdcall SetBoneOffsetMatrix(DWORD bone, const D3DXMATRIX *pM) {
        if (bone >= m_bones.size() || !pM) return D3DERR_INVALIDCALL;
        m_bones[bone].offset = *pM;
        return D3D_OK;
    }
    LPD3DXMATRIX __stdcall GetBoneOffsetMatrix(DWORD bone) {
        return bone < m_bones.size() ? &m_bones[bone].offset : NULL;
    }

    HRESULT __stdcall Clone(LPD3DXSKININFO *ppSkinInfo) {
        if (!ppSkinInfo) return D3DERR_INVALIDCALL;
        RanSkinInfo *s = new RanSkinInfo(m_numVertices, m_fvf, (DWORD)m_bones.size());
        s->m_bones = m_bones;
        s->m_minInfluence = m_minInfluence;
        *ppSkinInfo = s;
        return D3D_OK;
    }
    HRESULT __stdcall Remap(DWORD, DWORD *) { return D3D_OK; }

    HRESULT __stdcall SetFVF(DWORD fvf) { m_fvf = fvf; return D3D_OK; }
    HRESULT __stdcall SetDeclaration(const D3DVERTEXELEMENT9 *) { return D3D_OK; }
    DWORD   __stdcall GetFVF() { return m_fvf; }
    HRESULT __stdcall GetDeclaration(D3DVERTEXELEMENT9 Declaration[MAX_FVF_DECL_SIZE]) {
        return D3DXDeclaratorFromFVF(m_fvf, Declaration);
    }

    HRESULT __stdcall UpdateSkinnedMesh(const D3DXMATRIX *pBoneTransforms,
                                        const D3DXMATRIX *pBoneInvTransposeTransforms,
                                        LPCVOID pVerticesSrc, PVOID pVerticesDst);

    HRESULT __stdcall ConvertToBlendedMesh(LPD3DXMESH pMesh, DWORD Options, const DWORD *pAdjacencyIn,
                                           DWORD *pAdjacencyOut, DWORD *pFaceRemap,
                                           LPD3DXBUFFER *ppVertexRemap, DWORD *pMaxFaceInfl,
                                           DWORD *pNumBoneCombinations,
                                           LPD3DXBUFFER *ppBoneCombinationTable, LPD3DXMESH *ppMesh);

    HRESULT __stdcall ConvertToIndexedBlendedMesh(LPD3DXMESH pMesh, DWORD Options, DWORD paletteSize,
                                                  const DWORD *pAdjacencyIn, DWORD *pAdjacencyOut,
                                                  DWORD *pFaceRemap, LPD3DXBUFFER *ppVertexRemap,
                                                  DWORD *pMaxVertexInfl, DWORD *pNumBoneCombinations,
                                                  LPD3DXBUFFER *ppBoneCombinationTable,
                                                  LPD3DXMESH *ppMesh) {
        // Nothing in the client asks for the indexed form; the blended one is
        // what DxSkinMesh9_NORMAL draws.
        (void)paletteSize; (void)pMaxVertexInfl;
        return ConvertToBlendedMesh(pMesh, Options, pAdjacencyIn, pAdjacencyOut, pFaceRemap,
                                    ppVertexRemap, pMaxVertexInfl, pNumBoneCombinations,
                                    ppBoneCombinationTable, ppMesh);
    }

    // Per-vertex influences, strongest first, at most kMaxPalette of them.
    void perVertexInfluences(std::vector<std::vector<std::pair<DWORD, float> > > &out) const {
        out.assign(m_numVertices, std::vector<std::pair<DWORD, float> >());
        for (size_t b = 0; b < m_bones.size(); ++b) {
            for (size_t i = 0; i < m_bones[b].influences.size(); ++i) {
                const Influence &in = m_bones[b].influences[i];
                if (in.vertex >= m_numVertices || in.weight <= 0.0f) continue;
                out[in.vertex].push_back(std::make_pair((DWORD)b, in.weight));
            }
        }
        for (DWORD v = 0; v < m_numVertices; ++v) {
            std::vector<std::pair<DWORD, float> > &w = out[v];
            // Insertion sort by weight, descending — these lists are 1-3 long.
            for (size_t i = 1; i < w.size(); ++i)
                for (size_t j = i; j > 0 && w[j].second > w[j - 1].second; --j)
                    std::swap(w[j], w[j - 1]);
            if (w.size() > kMaxPalette) w.resize(kMaxPalette);
            float sum = 0.0f;
            for (size_t i = 0; i < w.size(); ++i) sum += w[i].second;
            if (sum > 0.0f) for (size_t i = 0; i < w.size(); ++i) w[i].second /= sum;
        }
    }
};

//  Software skinning, for the paths that ask for transformed vertices rather
//  than a blended mesh (collision and shadow code do).
HRESULT RanSkinInfo::UpdateSkinnedMesh(const D3DXMATRIX *pBoneTransforms, const D3DXMATRIX *,
                                       LPCVOID pVerticesSrc, PVOID pVerticesDst) {
    if (!pBoneTransforms || !pVerticesSrc || !pVerticesDst) return D3DERR_INVALIDCALL;
    const DWORD stride = D3DXGetFVFVertexSize(m_fvf);
    if (!stride) return D3DERR_INVALIDCALL;

    memcpy(pVerticesDst, pVerticesSrc, (size_t)stride * m_numVertices);

    std::vector<std::vector<std::pair<DWORD, float> > > infl;
    perVertexInfluences(infl);

    const bool hasNormal = (m_fvf & D3DFVF_NORMAL) != 0;
    for (DWORD v = 0; v < m_numVertices; ++v) {
        if (infl[v].empty()) continue;
        const float *src = (const float *)((const BYTE *)pVerticesSrc + (size_t)v * stride);
        float *dst = (float *)((BYTE *)pVerticesDst + (size_t)v * stride);

        D3DXVECTOR3 pos(0, 0, 0), nrm(0, 0, 0);
        for (size_t k = 0; k < infl[v].size(); ++k) {
            const DWORD bone = infl[v][k].first;
            const float w = infl[v][k].second;
            D3DXMATRIX m;
            D3DXMatrixMultiply(&m, &m_bones[bone].offset, &pBoneTransforms[bone]);

            D3DXVECTOR3 p;
            D3DXVec3TransformCoord(&p, (const D3DXVECTOR3 *)src, &m);
            pos += p * w;

            if (hasNormal) {
                D3DXVECTOR3 n;
                D3DXVec3TransformNormal(&n, (const D3DXVECTOR3 *)(src + 3), &m);
                nrm += n * w;
            }
        }
        dst[0] = pos.x; dst[1] = pos.y; dst[2] = pos.z;
        if (hasNormal) {
            D3DXVec3Normalize(&nrm, &nrm);
            dst[3] = nrm.x; dst[4] = nrm.y; dst[5] = nrm.z;
        }
    }
    return D3D_OK;
}

//  Turn a plain mesh plus this skin into a mesh the fixed-function vertex
//  blender can draw: faces grouped so that no group needs more than NumInfl
//  bone matrices, vertices duplicated per group so a vertex's weights line up
//  with that group's palette slots.
HRESULT RanSkinInfo::ConvertToBlendedMesh(LPD3DXMESH pMesh, DWORD Options, const DWORD *,
                                          DWORD *pAdjacencyOut, DWORD *pFaceRemap,
                                          LPD3DXBUFFER *ppVertexRemap, DWORD *pMaxFaceInfl,
                                          DWORD *pNumBoneCombinations,
                                          LPD3DXBUFFER *ppBoneCombinationTable, LPD3DXMESH *ppMesh) {
    if (!pMesh || !ppMesh) return D3DERR_INVALIDCALL;
    *ppMesh = NULL;

    const DWORD srcFVF    = pMesh->GetFVF();
    const DWORD srcStride = pMesh->GetNumBytesPerVertex();
    const DWORD numFaces  = pMesh->GetNumFaces();
    const DWORD numVerts  = pMesh->GetNumVertices();
    if (!numFaces || !numVerts) return D3DERR_INVALIDCALL;

    BYTE  *srcVerts = NULL;
    WORD  *srcIdx   = NULL;
    DWORD *srcAttr  = NULL;
    if (FAILED(pMesh->LockVertexBuffer(0, (LPVOID *)&srcVerts)) || !srcVerts) return D3DERR_INVALIDCALL;
    if (FAILED(pMesh->LockIndexBuffer(0, (LPVOID *)&srcIdx)) || !srcIdx) {
        pMesh->UnlockVertexBuffer();
        return D3DERR_INVALIDCALL;
    }
    pMesh->LockAttributeBuffer(0, &srcAttr);

    std::vector<std::vector<std::pair<DWORD, float> > > infl;
    perVertexInfluences(infl);

    //  Faces whose bone set had to be pruned to fit the palette, reported
    //  with the blend summary: a non-zero count is a visible skinning change.
    static unsigned g_prunedFaces = 0;
    const unsigned prunedBefore = g_prunedFaces;

    // ---- group faces so each group's bone set fits the palette
    struct Group {
        DWORD attrib;
        std::vector<DWORD> bones;            // palette slot -> bone index
        std::vector<DWORD> faces;
    };
    std::vector<Group> groups;

    for (DWORD f = 0; f < numFaces; ++f) {
        const DWORD attrib = srcAttr ? srcAttr[f] : 0;

        // Bones this face needs.
        std::vector<DWORD> need;
        for (int c = 0; c < 3; ++c) {
            const DWORD v = srcIdx[(size_t)f * 3 + c];
            if (v >= numVerts) continue;
            for (size_t k = 0; k < infl[v].size(); ++k) {
                const DWORD b = infl[v][k].first;
                bool have = false;
                for (size_t i = 0; i < need.size(); ++i) if (need[i] == b) { have = true; break; }
                if (!have) need.push_back(b);
            }
        }

        //  Best fit, not first fit: put the face in the group that has to grow
        //  the least. Every group is one draw call at run time, and a character
        //  is drawn from ~20 of them, so packing quality is frame rate.
        int target = -1;
        size_t bestNew = kMaxPalette + 1;
        std::vector<DWORD> bestMerged;
        for (size_t g = 0; g < groups.size(); ++g) {
            if (groups[g].attrib != attrib) continue;
            std::vector<DWORD> merged = groups[g].bones;
            size_t added = 0;
            for (size_t i = 0; i < need.size(); ++i) {
                bool have = false;
                for (size_t j = 0; j < merged.size(); ++j) if (merged[j] == need[i]) { have = true; break; }
                if (!have) { merged.push_back(need[i]); ++added; }
            }
            if (merged.size() > kMaxPalette) continue;
            if (added < bestNew) {
                bestNew = added;
                bestMerged.swap(merged);
                target = (int)g;
                if (added == 0) break;          // cannot do better than free
            }
        }
        if (target >= 0) groups[target].bones.swap(bestMerged);
        if (target < 0) {
            //  A face whose three vertices between them name more bones than
            //  the palette holds cannot be drawn in one call. Truncating the
            //  list drops whichever bones happen to sit at the end and the
            //  emit loop below renormalises the survivors, which drags those
            //  vertices toward the wrong joint - that is what a stretched
            //  shoulder or arm looks like. Drop the WEAKEST influences
            //  instead: sum each bone's weight over the face and keep the
            //  strongest kMaxPalette, exactly what D3DX does.
            if (need.size() > kMaxPalette) {
                std::vector<std::pair<float, DWORD> > strength;
                strength.reserve(need.size());
                for (size_t i = 0; i < need.size(); ++i) {
                    float w = 0.0f;
                    for (int c = 0; c < 3; ++c) {
                        const DWORD v = srcIdx[(size_t)f * 3 + c];
                        if (v >= numVerts) continue;
                        for (size_t k = 0; k < infl[v].size(); ++k)
                            if (infl[v][k].first == need[i]) w += infl[v][k].second;
                    }
                    strength.push_back(std::make_pair(w, need[i]));
                }
                std::sort(strength.begin(), strength.end());
                std::reverse(strength.begin(), strength.end());
                need.clear();
                for (size_t i = 0; i < kMaxPalette; ++i) need.push_back(strength[i].second);
                ++g_prunedFaces;
            }
            Group g;
            g.attrib = attrib;
            g.bones = need;
            groups.push_back(g);
            target = (int)groups.size() - 1;
        }
        groups[target].faces.push_back(f);
    }

    //  NumInfl is the palette size the caller will feed to D3DRS_VERTEXBLEND,
    //  so it must cover the largest group, and the FVF carries NumInfl-1
    //  weights (the last one is implied).
    DWORD numInfl = 1;
    for (size_t g = 0; g < groups.size(); ++g)
        if (groups[g].bones.size() > numInfl) numInfl = (DWORD)groups[g].bones.size();
    if (numInfl > kMaxPalette) numInfl = kMaxPalette;

    DWORD blendFVF = D3DFVF_XYZ;
    switch (numInfl) {
        case 1:  blendFVF = D3DFVF_XYZ;   break;
        case 2:  blendFVF = D3DFVF_XYZB1; break;
        case 3:  blendFVF = D3DFVF_XYZB2; break;
        default: blendFVF = D3DFVF_XYZB3; break;
    }
    if (srcFVF & D3DFVF_NORMAL) blendFVF |= D3DFVF_NORMAL;
    if (srcFVF & D3DFVF_DIFFUSE) blendFVF |= D3DFVF_DIFFUSE;
    if (srcFVF & D3DFVF_TEX1)   blendFVF |= D3DFVF_TEX1;

    // Offsets inside the SOURCE vertex, walked in FVF order.
    DWORD sNormal = 0xFFFFFFFF, sDiffuse = 0xFFFFFFFF, sUV = 0xFFFFFFFF, off = 12;
    if (srcFVF & D3DFVF_NORMAL)  { sNormal = off;  off += 12; }
    if (srcFVF & D3DFVF_PSIZE)   { off += 4; }
    if (srcFVF & D3DFVF_DIFFUSE) { sDiffuse = off; off += 4; }
    if (srcFVF & D3DFVF_SPECULAR){ off += 4; }
    if (srcFVF & D3DFVF_TEX1)    { sUV = off; }

    // ---- build the blended vertex/index data
    const DWORD weightCount = numInfl - 1;
    const DWORD dstStride   = D3DXGetFVFVertexSize(blendFVF);

    std::vector<BYTE>  outVerts;
    std::vector<WORD>  outIdx;
    std::vector<DWORD> outAttr;
    std::vector<DWORD> vertexRemap;              // new vertex -> old vertex
    outIdx.reserve((size_t)numFaces * 3);
    outAttr.reserve(numFaces);

    std::vector<D3DXBONECOMBINATION> combos(groups.size());
    std::vector<DWORD> comboBones((size_t)groups.size() * numInfl, UINT_MAX);

    for (size_t g = 0; g < groups.size(); ++g) {
        const Group &grp = groups[g];
        const DWORD vertexStart = (DWORD)vertexRemap.size();
        const DWORD faceStart   = (DWORD)outAttr.size();

        // slot lookup for this group
        std::map<DWORD, DWORD> slotOf;
        for (size_t s = 0; s < grp.bones.size(); ++s) {
            slotOf[grp.bones[s]] = (DWORD)s;
            comboBones[g * numInfl + s] = grp.bones[s];
        }

        std::map<DWORD, DWORD> emitted;          // old vertex -> new vertex
        for (size_t fi = 0; fi < grp.faces.size(); ++fi) {
            const DWORD f = grp.faces[fi];
            for (int c = 0; c < 3; ++c) {
                const DWORD v = srcIdx[(size_t)f * 3 + c];
                std::map<DWORD, DWORD>::iterator it = emitted.find(v);
                DWORD nv;
                if (it != emitted.end()) {
                    nv = it->second;
                } else {
                    nv = (DWORD)vertexRemap.size();
                    emitted[v] = nv;
                    vertexRemap.push_back(v);

                    const BYTE *sv = srcVerts + (size_t)v * srcStride;
                    const size_t base = outVerts.size();
                    outVerts.resize(base + dstStride, 0);
                    BYTE *dv = &outVerts[base];

                    memcpy(dv, sv, 12);                       // position
                    DWORD doff = 12;

                    //  Weights in palette-slot order. Anything this group does
                    //  not carry stays zero, and slot numInfl-1 is implied by
                    //  the fixed-function pipeline as 1 - sum(weights).
                    std::vector<float> w(numInfl, 0.0f);
                    if (v < numVerts) {
                        for (size_t k = 0; k < infl[v].size(); ++k) {
                            std::map<DWORD, DWORD>::iterator s = slotOf.find(infl[v][k].first);
                            if (s != slotOf.end()) w[s->second] += infl[v][k].second;
                        }
                    }
                    float sum = 0.0f;
                    for (DWORD i = 0; i < numInfl; ++i) sum += w[i];
                    if (sum <= 0.0f) w[0] = 1.0f;
                    else if (sum != 1.0f) for (DWORD i = 0; i < numInfl; ++i) w[i] /= sum;

                    for (DWORD i = 0; i < weightCount; ++i) {
                        memcpy(dv + doff, &w[i], 4);
                        doff += 4;
                    }

                    if (blendFVF & D3DFVF_NORMAL) {
                        if (sNormal != 0xFFFFFFFF) memcpy(dv + doff, sv + sNormal, 12);
                        doff += 12;
                    }
                    if (blendFVF & D3DFVF_DIFFUSE) {
                        if (sDiffuse != 0xFFFFFFFF) memcpy(dv + doff, sv + sDiffuse, 4);
                        else { DWORD white = 0xFFFFFFFF; memcpy(dv + doff, &white, 4); }
                        doff += 4;
                    }
                    if (blendFVF & D3DFVF_TEX1) {
                        if (sUV != 0xFFFFFFFF) memcpy(dv + doff, sv + sUV, 8);
                        doff += 8;
                    }
                }
                outIdx.push_back((WORD)nv);
            }
            //  The attribute id of a blended mesh subset is the GROUP index:
            //  DrawMeshContainer draws with DrawSubset(iAttrib) and looks the
            //  material up through the bone combination's AttribId.
            outAttr.push_back((DWORD)g);
            if (pFaceRemap) pFaceRemap[outAttr.size() - 1] = f;
        }

        combos[g].AttribId    = grp.attrib;
        combos[g].FaceStart   = faceStart;
        combos[g].FaceCount   = (DWORD)(outAttr.size() - faceStart);
        combos[g].VertexStart = vertexStart;
        combos[g].VertexCount = (DWORD)(vertexRemap.size() - vertexStart);
        combos[g].BoneId      = NULL;            // filled in once the buffer exists
    }

    pMesh->UnlockAttributeBuffer();
    pMesh->UnlockIndexBuffer();
    pMesh->UnlockVertexBuffer();

    if (vertexRemap.size() > 65535) {
        LOGE("blended mesh needs %u vertices, more than a 16-bit index can reach",
             (unsigned)vertexRemap.size());
        return D3DERR_INVALIDCALL;
    }

    // ---- hand back a real mesh
    LPDIRECT3DDEVICE9 device = NULL;
    pMesh->GetDevice(&device);

    LPD3DXMESH outMesh = NULL;
    HRESULT hr = D3DXCreateMeshFVF((DWORD)outAttr.size(), (DWORD)vertexRemap.size(),
                                   Options, blendFVF, device, &outMesh);
    if (device) device->Release();
    if (FAILED(hr) || !outMesh) return FAILED(hr) ? hr : E_FAIL;

    void *dst = NULL;
    if (SUCCEEDED(outMesh->LockVertexBuffer(0, &dst)) && dst && !outVerts.empty())
        memcpy(dst, &outVerts[0], outVerts.size());
    outMesh->UnlockVertexBuffer();

    dst = NULL;
    if (SUCCEEDED(outMesh->LockIndexBuffer(0, &dst)) && dst && !outIdx.empty())
        memcpy(dst, &outIdx[0], outIdx.size() * sizeof(WORD));
    outMesh->UnlockIndexBuffer();

    DWORD *attrDst = NULL;
    if (SUCCEEDED(outMesh->LockAttributeBuffer(0, &attrDst)) && attrDst && !outAttr.empty())
        memcpy(attrDst, &outAttr[0], outAttr.size() * sizeof(DWORD));
    outMesh->UnlockAttributeBuffer();

    if (pAdjacencyOut)
        for (DWORD i = 0; i < (DWORD)outAttr.size() * 3; ++i) pAdjacencyOut[i] = 0xFFFFFFFF;

    if (ppVertexRemap) {
        XBuffer *vr = new XBuffer(vertexRemap.size() * sizeof(DWORD));
        if (!vertexRemap.empty())
            memcpy(vr->GetBufferPointer(), &vertexRemap[0], vertexRemap.size() * sizeof(DWORD));
        *ppVertexRemap = vr;
    }

    if (ppBoneCombinationTable) {
        //  D3DX lays the table out as the entries followed by their bone id
        //  arrays, with each entry's BoneId pointing into that tail.
        const size_t entries = combos.size();
        const size_t bytes = entries * sizeof(D3DXBONECOMBINATION)
                           + entries * numInfl * sizeof(DWORD);
        XBuffer *buf = new XBuffer(bytes);
        D3DXBONECOMBINATION *table = (D3DXBONECOMBINATION *)buf->GetBufferPointer();
        DWORD *ids = (DWORD *)(table + entries);
        for (size_t g = 0; g < entries; ++g) {
            table[g] = combos[g];
            table[g].BoneId = ids + g * numInfl;
            for (DWORD s = 0; s < numInfl; ++s) table[g].BoneId[s] = comboBones[g * numInfl + s];
        }
        *ppBoneCombinationTable = buf;
    }

    //  ---- self-check: does the blended vertex mean what the source meant?
    {
        DWORD badVertices = 0, worstBone = 0;
        float worstError = 0.0f;
        std::string worstName;

        for (size_t g = 0; g < groups.size(); ++g) {
            const Group &grp = groups[g];
            for (DWORD nv = combos[g].VertexStart;
                 nv < combos[g].VertexStart + combos[g].VertexCount; ++nv) {
                const DWORD src = vertexRemap[nv];
                if (src >= numVerts) continue;

                //  What the GPU will use: the stored weights, and the implied
                //  one in the slot the engine will name as the last bone.
                std::vector<float> got(numInfl, 0.0f);
                const BYTE *dv = &outVerts[(size_t)nv * dstStride] + 12;
                float used = 0.0f;
                for (DWORD i = 0; i < weightCount; ++i) {
                    float f = 0.0f;
                    memcpy(&f, dv + i * 4, 4);
                    got[i] = f;
                    used += f;
                }
                const DWORD lastSlot = (DWORD)grp.bones.size() - 1;
                if (lastSlot < numInfl) got[lastSlot] += 1.0f - used;

                //  What the source said.
                for (size_t k = 0; k < infl[src].size(); ++k) {
                    const DWORD bone = infl[src][k].first;
                    const float want = infl[src][k].second;
                    float have = 0.0f;
                    for (size_t s = 0; s < grp.bones.size(); ++s)
                        if (grp.bones[s] == bone) { have = got[s]; break; }
                    const float err = fabsf(have - want);
                    if (err > 0.01f) {
                        ++badVertices;
                        if (err > worstError) {
                            worstError = err;
                            worstBone = bone;
                            worstName = m_bones[bone].name;
                        }
                        break;
                    }
                }
            }
        }

        if (badVertices) {
            LOGE("blend self-check: %u vertices differ from their source weights; "
                 "worst is bone %u [%s] off by %.2f",
                 badVertices, worstBone, worstName.c_str(), worstError);
        }
    }

    if (g_logLoads > 0) {
        DWORD unweighted = 0, maxInfl = 0;
        for (DWORD v = 0; v < numVerts; ++v) {
            if (infl[v].empty()) ++unweighted;
            if (infl[v].size() > maxInfl) maxInfl = (DWORD)infl[v].size();
        }
        LOGI("blended: verts %u->%u faces %u groups %u numInfl %u maxInfl %u unweighted %u pruned %u",
             numVerts, (unsigned)vertexRemap.size(), numFaces, (unsigned)combos.size(),
             numInfl, maxInfl, unweighted, g_prunedFaces - prunedBefore);
    }

    if (pNumBoneCombinations) *pNumBoneCombinations = (DWORD)combos.size();
    if (pMaxFaceInfl) *pMaxFaceInfl = numInfl;

    *ppMesh = outMesh;
    return D3D_OK;
}

// ------------------------------------------------------------ .x hierarchy
const XNode *childOfType(const XNode *n, const char *typeName) {
    for (size_t i = 0; i < n->children.size(); ++i)
        if (n->children[i]->typeName == typeName) return n->children[i];
    return NULL;
}

//  A `.x` string member is stored as a pointer to the parser's own copy.
const char *stringMember(const std::vector<BYTE> &data, size_t offset) {
    if (data.size() < offset + sizeof(const char *)) return NULL;
    const char *p = NULL;
    memcpy(&p, &data[offset], sizeof(p));
    return p;
}

//  SkinWeights: bone name, then count, vertex indices, weights, offset matrix.
bool readSkinWeights(const XNode *n, std::string &boneName,
                     std::vector<DWORD> &vertices, std::vector<float> &weights,
                     D3DXMATRIX &offset) {
    const char *name = stringMember(n->data, 0);
    if (!name) return false;
    boneName = name;

    size_t p = sizeof(const char *);
    if (n->data.size() < p + 4) return false;
    DWORD count = 0;
    memcpy(&count, &n->data[p], 4);
    p += 4;

    const size_t need = (size_t)count * 4 + (size_t)count * 4 + 16 * 4;
    if (n->data.size() < p + need) return false;

    vertices.resize(count);
    if (count) { memcpy(&vertices[0], &n->data[p], (size_t)count * 4); }
    p += (size_t)count * 4;

    weights.resize(count);
    if (count) { memcpy(&weights[0], &n->data[p], (size_t)count * 4); }
    p += (size_t)count * 4;

    memcpy(&offset, &n->data[p], 16 * 4);
    return true;
}

//  One-shot load reporting: the first few files loaded print what came out, so
//  the counts can be checked against an independent read of the same file.
int g_logLoads = 6;

//  Frames counted per file, for the same check.
int g_frameCount = 0;

struct Loader {
    LPD3DXALLOCATEHIERARCHY alloc;
    LPDIRECT3DDEVICE9       device;
    DWORD                   options;
    DWORD                   meshes;

    Loader() : alloc(NULL), device(NULL), options(0), meshes(0) {}

    //  Build the skin info for a Mesh node, or NULL when the mesh is rigid.
    LPD3DXSKININFO skinFor(const XNode *mesh, DWORD numVertices, DWORD fvf) {
        std::vector<const XNode *> weightNodes;
        for (size_t i = 0; i < mesh->children.size(); ++i)
            if (mesh->children[i]->typeName == "SkinWeights")
                weightNodes.push_back(mesh->children[i]);
        if (weightNodes.empty()) return NULL;

        RanSkinInfo *skin = new RanSkinInfo(numVertices, fvf, (DWORD)weightNodes.size());
        DWORD bone = 0;
        for (size_t i = 0; i < weightNodes.size(); ++i) {
            std::string name;
            std::vector<DWORD> verts;
            std::vector<float> w;
            D3DXMATRIX offset;
            if (!readSkinWeights(weightNodes[i], name, verts, w, offset)) continue;
            skin->SetBoneName(bone, name.c_str());
            skin->SetBoneOffsetMatrix(bone, &offset);
            skin->SetBoneInfluence(bone, (DWORD)verts.size(),
                                   verts.empty() ? NULL : &verts[0],
                                   w.empty() ? NULL : &w[0]);
            ++bone;
        }
        return skin;
    }

    //  One frame, its mesh (if any), then siblings and children — the shape the
    //  engine walks with pFrameFirstChild / pFrameSibling.
    LPD3DXFRAME buildFrame(const XNode *node) {
        ++g_frameCount;
        LPD3DXFRAME frame = NULL;
        if (FAILED(alloc->CreateFrame(node->name.empty() ? NULL : node->name.c_str(), &frame)) || !frame)
            return NULL;

        const XNode *xform = childOfType(node, "FrameTransformMatrix");
        if (xform && xform->data.size() >= 16 * 4)
            memcpy(&frame->TransformationMatrix, &xform->data[0], 16 * 4);
        else
            D3DXMatrixIdentity(&frame->TransformationMatrix);

        LPD3DXMESHCONTAINER *tail = &frame->pMeshContainer;
        LPD3DXFRAME *childTail = &frame->pFrameFirstChild;

        for (size_t i = 0; i < node->children.size(); ++i) {
            const XNode *c = node->children[i];

            if (c->typeName == "Mesh") {
                LPD3DXMESH mesh = NULL;
                LPD3DXBUFFER adjacency = NULL, materials = NULL, effects = NULL;
                DWORD numMaterials = 0;
                if (FAILED(RanMesh_FromXNode(c, options, device, &adjacency, &materials,
                                             &effects, &numMaterials, &mesh)) || !mesh) {
                    if (adjacency) adjacency->Release();
                    if (materials) materials->Release();
                    if (effects) effects->Release();
                    continue;
                }

                LPD3DXSKININFO skin = skinFor(c, mesh->GetNumVertices(), mesh->GetFVF());

                D3DXMESHDATA data;
                memset(&data, 0, sizeof(data));
                data.Type = D3DXMESHTYPE_MESH;
                data.pMesh = mesh;

                //  A material with no texture draws flat white. The mesh under a
                //  frame is unnamed, so this has to be reported from here, where
                //  the frame name is: it is the only handle anyone has on the piece.
                if (numMaterials && materials) {
                    const D3DXMATERIAL *mm = (const D3DXMATERIAL *)materials->GetBufferPointer();
                    for (DWORD mi = 0; mi < numMaterials; ++mi) {
                        if (mm[mi].pTextureFilename && mm[mi].pTextureFilename[0]) continue;
                        static std::set<std::string> s_said;
                        char szIdx[16];
                        snprintf(szIdx, sizeof(szIdx), ":%u", (unsigned)mi);
                        const std::string key = (c->name.empty() ? node->name : c->name) + szIdx;
                        if (s_said.size() < 64 && s_said.insert(key).second)
                            LOGE("mesh \"%s\" material %u has no TextureFilename in the .x",
                                 (c->name.empty() ? node->name : c->name).c_str(), (unsigned)mi);
                    }
                }

                LPD3DXMESHCONTAINER container = NULL;
                HRESULT hr = alloc->CreateMeshContainer(
                    c->name.empty() ? NULL : c->name.c_str(), &data,
                    numMaterials ? (const D3DXMATERIAL *)materials->GetBufferPointer() : NULL,
                    NULL, numMaterials,
                    adjacency ? (const DWORD *)adjacency->GetBufferPointer() : NULL,
                    skin, &container);

                if (g_logLoads) {
                    DWORD bones = skin ? skin->GetNumBones() : 0;
                    LOGI("  mesh frame=%s name=%s verts=%u faces=%u bones=%u fvf=%08X",
                         node->name.empty() ? "(none)" : node->name.c_str(),
                         c->name.empty() ? "(none)" : c->name.c_str(),
                         mesh->GetNumVertices(), mesh->GetNumFaces(), (unsigned)bones,
                         (unsigned)mesh->GetFVF());
                }

                if (SUCCEEDED(hr) && container) {
                    container->pNextMeshContainer = NULL;
                    *tail = container;
                    tail = &container->pNextMeshContainer;
                    ++meshes;
                }

                if (skin) skin->Release();
                mesh->Release();
                if (adjacency) adjacency->Release();
                if (materials) materials->Release();
                if (effects) effects->Release();
                continue;
            }

            if (c->typeName == "Frame") {
                LPD3DXFRAME child = buildFrame(c);
                if (child) {
                    child->pFrameSibling = NULL;
                    *childTail = child;
                    childTail = &child->pFrameSibling;
                }
            }
        }

        return frame;
    }
};

HRESULT loadHierarchy(const void *bytes, size_t size, DWORD options, LPDIRECT3DDEVICE9 device,
                      LPD3DXALLOCATEHIERARCHY alloc, LPD3DXFRAME *ppFrameRoot) {
    if (!bytes || !size || !alloc || !ppFrameRoot) return D3DERR_INVALIDCALL;
    *ppFrameRoot = NULL;

    XFile *file = XFile_Parse(bytes, size);
    if (!file) return D3DXERR_INVALIDDATA;

    g_frameCount = 0;
    Loader loader;
    loader.alloc = alloc;
    loader.device = device;
    loader.options = options;

    //  A file may hold several root frames; the engine wants one root, so an
    //  unnamed frame owns them as children when there is more than one.
    std::vector<const XNode *> roots;
    for (size_t i = 0; i < file->roots.size(); ++i) {
        const XNode *n = file->roots[i];
        if (n->typeName == "Frame" || n->typeName == "Mesh") roots.push_back(n);
    }
    if (roots.empty()) { delete file; return D3DXERR_INVALIDDATA; }

    LPD3DXFRAME root = NULL;
    if (roots.size() == 1 && roots[0]->typeName == "Frame") {
        root = loader.buildFrame(roots[0]);
    } else {
        if (FAILED(alloc->CreateFrame(NULL, &root)) || !root) { delete file; return E_FAIL; }
        D3DXMatrixIdentity(&root->TransformationMatrix);
        root->pMeshContainer = NULL;
        root->pFrameFirstChild = NULL;
        root->pFrameSibling = NULL;

        LPD3DXFRAME *tail = &root->pFrameFirstChild;
        for (size_t i = 0; i < roots.size(); ++i) {
            //  A bare Mesh at file scope is wrapped so it still hangs off a frame.
            const XNode *n = roots[i];
            LPD3DXFRAME child = NULL;
            if (n->typeName == "Frame") {
                child = loader.buildFrame(n);
            } else {
                XNode wrapper;
                wrapper.typeName = "Frame";
                wrapper.children.push_back(const_cast<XNode *>(n));
                child = loader.buildFrame(&wrapper);
                wrapper.children.clear();          // the file still owns the mesh
            }
            if (child) { child->pFrameSibling = NULL; *tail = child; tail = &child->pFrameSibling; }
        }
    }

    if (g_logLoads > 0) {
        --g_logLoads;
        LOGI("loaded hierarchy: %d frames, %u mesh containers", g_frameCount, loader.meshes);
    }

    delete file;
    if (!root) return E_FAIL;

    *ppFrameRoot = root;
    return D3D_OK;
}

void destroyFrame(LPD3DXFRAME frame, LPD3DXALLOCATEHIERARCHY alloc) {
    if (!frame) return;
    if (frame->pFrameSibling)    destroyFrame(frame->pFrameSibling, alloc);
    if (frame->pFrameFirstChild) destroyFrame(frame->pFrameFirstChild, alloc);
    LPD3DXMESHCONTAINER mc = frame->pMeshContainer;
    while (mc) {
        LPD3DXMESHCONTAINER next = mc->pNextMeshContainer;
        alloc->DestroyMeshContainer(mc);
        mc = next;
    }
    alloc->DestroyFrame(frame);
}

void frameBounds(const D3DXFRAME *frame, const D3DXMATRIX &parent,
                 D3DXVECTOR3 &vMin, D3DXVECTOR3 &vMax, bool &any) {
    if (!frame) return;

    D3DXMATRIX world;
    D3DXMatrixMultiply(&world, &frame->TransformationMatrix, &parent);

    for (const D3DXMESHCONTAINER *mc = frame->pMeshContainer; mc; mc = mc->pNextMeshContainer) {
        LPD3DXMESH mesh = mc->MeshData.pMesh;
        if (!mesh) continue;
        BYTE *verts = NULL;
        if (FAILED(mesh->LockVertexBuffer(0, (LPVOID *)&verts)) || !verts) continue;
        const DWORD stride = mesh->GetNumBytesPerVertex();
        const DWORD count = mesh->GetNumVertices();
        for (DWORD i = 0; i < count; ++i) {
            D3DXVECTOR3 p;
            D3DXVec3TransformCoord(&p, (const D3DXVECTOR3 *)(verts + (size_t)i * stride), &world);
            if (!any) { vMin = vMax = p; any = true; continue; }
            if (p.x < vMin.x) vMin.x = p.x;
            if (p.y < vMin.y) vMin.y = p.y;
            if (p.z < vMin.z) vMin.z = p.z;
            if (p.x > vMax.x) vMax.x = p.x;
            if (p.y > vMax.y) vMax.y = p.y;
            if (p.z > vMax.z) vMax.z = p.z;
        }
        mesh->UnlockVertexBuffer();
    }

    frameBounds(frame->pFrameFirstChild, world, vMin, vMax, any);
    frameBounds(frame->pFrameSibling, parent, vMin, vMax, any);
}

} // namespace

// ---------------------------------------------------------------- entry points
extern "C" HRESULT WINAPI D3DXLoadMeshHierarchyFromXInMemory(
        LPCVOID Memory, DWORD SizeOfMemory, DWORD Options, LPDIRECT3DDEVICE9 pD3DDevice,
        LPD3DXALLOCATEHIERARCHY pAlloc, LPD3DXLOADUSERDATA,
        LPD3DXFRAME *ppFrameHierarchy, LPD3DXANIMATIONCONTROLLER *ppAnimController) {
    if (ppAnimController) *ppAnimController = NULL;   // the engine animates through its own skeleton
    return loadHierarchy(Memory, SizeOfMemory, Options, pD3DDevice, pAlloc, ppFrameHierarchy);
}

extern "C" HRESULT WINAPI D3DXLoadMeshHierarchyFromXA(
        LPCSTR pFilename, DWORD Options, LPDIRECT3DDEVICE9 pD3DDevice,
        LPD3DXALLOCATEHIERARCHY pAlloc, LPD3DXLOADUSERDATA pUserDataLoader,
        LPD3DXFRAME *ppFrameHierarchy, LPD3DXANIMATIONCONTROLLER *ppAnimController) {
    if (!pFilename) return D3DERR_INVALIDCALL;

    FILE *f = fopen(pFilename, "rb");
    if (!f) {
        LOGE("cannot open %s", pFilename);
        return D3DXERR_INVALIDDATA;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); return D3DXERR_INVALIDDATA; }

    std::vector<BYTE> bytes((size_t)size);
    const size_t got = fread(&bytes[0], 1, (size_t)size, f);
    fclose(f);
    if (got != (size_t)size) return D3DXERR_INVALIDDATA;

    return D3DXLoadMeshHierarchyFromXInMemory(&bytes[0], (DWORD)bytes.size(), Options, pD3DDevice,
                                              pAlloc, pUserDataLoader, ppFrameHierarchy,
                                              ppAnimController);
}

extern "C" HRESULT WINAPI D3DXFrameDestroy(LPD3DXFRAME pFrameRoot, LPD3DXALLOCATEHIERARCHY pAlloc) {
    if (!pFrameRoot || !pAlloc) return D3D_OK;
    destroyFrame(pFrameRoot, pAlloc);
    return D3D_OK;
}

extern "C" HRESULT WINAPI D3DXFrameCalculateBoundingSphere(const D3DXFRAME *pFrameRoot,
                                                           D3DXVECTOR3 *pCenter, FLOAT *pRadius) {
    D3DXVECTOR3 vMin(0, 0, 0), vMax(0, 0, 0);
    bool any = false;
    D3DXMATRIX identity;
    D3DXMatrixIdentity(&identity);
    frameBounds(pFrameRoot, identity, vMin, vMax, any);

    D3DXVECTOR3 center = (vMin + vMax) * 0.5f;
    if (pCenter) *pCenter = center;
    if (pRadius) {
        D3DXVECTOR3 half = vMax - center;
        *pRadius = any ? D3DXVec3Length(&half) : 0.0f;
    }
    return D3D_OK;
}

extern "C" HRESULT WINAPI D3DXCreateSkinInfoFVF(DWORD NumVertices, DWORD FVF, DWORD NumBones,
                                                LPD3DXSKININFO *ppSkinInfo) {
    if (!ppSkinInfo) return D3DERR_INVALIDCALL;
    *ppSkinInfo = new RanSkinInfo(NumVertices, FVF, NumBones);
    return D3D_OK;
}
