// ID3DXMesh and the .x mesh loaders.
//
// The client loads meshes three ways — D3DXLoadMeshFromXof (a Mesh node handed
// over by its own .x walk), D3DXLoadMeshFromXInMemory and D3DXLoadMeshFromX —
// and then either draws subsets directly or copies the vertex/index data out.
// So the mesh has to be a real container, not a handle: it owns a vertex buffer,
// an index buffer and an attribute buffer, and its Lock methods hand out the
// actual bytes.
//
// A Mesh node in the file gives positions and faces; MeshNormals,
// MeshTextureCoords and MeshMaterialList arrive as child objects. Those decide
// the FVF, which is why it is computed rather than requested.

#include "windows.h"
#include "../platform/ran_plat.h"
#include <d3d9.h>
#include <d3dx9.h>

#include "xfile_parse.h"
#include "xmesh_build.h"

#include <string.h>
#include <vector>
#include <set>
#define LOGW(...) RanPlat_Log(RANLOG_WARN, "RanXMesh", __VA_ARGS__)

#define LOGI(...) RanPlat_Log(RANLOG_INFO,  "RanXMesh", __VA_ARGS__)
#define LOGE(...) RanPlat_Log(RANLOG_ERROR, "RanXMesh", __VA_ARGS__)

// The parsed node behind an ID3DXFileData, so LoadMeshFromXof can reach it.
XNode *RanXFile_NodeOf(ID3DXFileData *data);
XFile *RanXFile_Parse(const void *bytes, size_t size);

namespace {

struct Vec3 { float x, y, z; };
struct Vec2 { float u, v; };

// ------------------------------------------------------------------- buffer
class RanBuffer : public ID3DXBuffer {
public:
    LONG m_ref;
    std::vector<BYTE> m_data;

    explicit RanBuffer(size_t n) : m_ref(1), m_data(n) {}

    HRESULT __stdcall QueryInterface(REFIID, void **ppv) { *ppv = this; AddRef(); return S_OK; }
    ULONG   __stdcall AddRef() { return (ULONG)++m_ref; }
    ULONG   __stdcall Release() { LONG r = --m_ref; if (r <= 0) { delete this; return 0; } return (ULONG)r; }
    LPVOID  __stdcall GetBufferPointer() { return m_data.empty() ? NULL : &m_data[0]; }
    DWORD   __stdcall GetBufferSize() { return (DWORD)m_data.size(); }
};

// --------------------------------------------------------------------- mesh
class RanMesh : public ID3DXMesh {
public:
    LONG m_ref;
    IDirect3DDevice9 *m_device;
    DWORD m_fvf, m_options;
    DWORD m_numVerts, m_numFaces, m_stride;
    std::vector<BYTE>  m_vertices;
    std::vector<WORD>  m_indices;
    std::vector<DWORD> m_attributes;              // one per face
    std::vector<D3DXATTRIBUTERANGE> m_attribTable;

    RanMesh(IDirect3DDevice9 *dev, DWORD numFaces, DWORD numVerts, DWORD options, DWORD fvf)
        : m_ref(1), m_device(dev), m_fvf(fvf), m_options(options),
          m_numVerts(numVerts), m_numFaces(numFaces) {
        m_stride = D3DXGetFVFVertexSize(fvf);
        m_vertices.assign((size_t)m_numVerts * m_stride, 0);
        m_indices.assign((size_t)m_numFaces * 3, 0);
        m_attributes.assign(m_numFaces, 0);
        if (m_device) m_device->AddRef();
    }
    ~RanMesh() { if (m_device) m_device->Release(); }

    HRESULT __stdcall QueryInterface(REFIID, void **ppv) { *ppv = this; AddRef(); return S_OK; }
    ULONG   __stdcall AddRef() { return (ULONG)++m_ref; }
    ULONG   __stdcall Release() { LONG r = --m_ref; if (r <= 0) { delete this; return 0; } return (ULONG)r; }

    // --- ID3DXBaseMesh
    HRESULT __stdcall DrawSubset(DWORD AttribId) {
        if (!m_device || m_indices.empty()) return D3D_OK;
        // Faces carrying this attribute are contiguous after Optimize; without
        // it they are not, so the run is found rather than assumed.
        DWORD first = 0, count = 0;
        bool found = false;
        for (DWORD f = 0; f < m_numFaces; ++f) {
            if (m_attributes[f] == AttribId) {
                if (!found) { first = f; found = true; }
                ++count;
            } else if (found) {
                break;
            }
        }
        if (!found || !count) return D3D_OK;

        m_device->SetFVF(m_fvf);
        return m_device->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, m_numVerts, count,
                                                &m_indices[(size_t)first * 3], D3DFMT_INDEX16,
                                                &m_vertices[0], m_stride);
    }
    DWORD __stdcall GetNumFaces() { return m_numFaces; }
    DWORD __stdcall GetNumVertices() { return m_numVerts; }
    DWORD __stdcall GetFVF() { return m_fvf; }
    HRESULT __stdcall GetDeclaration(D3DVERTEXELEMENT9 Declaration[MAX_FVF_DECL_SIZE]) {
        if (!Declaration) return D3DERR_INVALIDCALL;
        return D3DXDeclaratorFromFVF(m_fvf, Declaration);
    }
    DWORD __stdcall GetNumBytesPerVertex() { return m_stride; }
    DWORD __stdcall GetOptions() { return m_options; }
    HRESULT __stdcall GetDevice(LPDIRECT3DDEVICE9 *ppDevice) {
        if (!ppDevice) return D3DERR_INVALIDCALL;
        *ppDevice = m_device;
        if (m_device) m_device->AddRef();
        return D3D_OK;
    }
    HRESULT __stdcall CloneMeshFVF(DWORD Options, DWORD FVF, LPDIRECT3DDEVICE9 pDevice,
                                   LPD3DXMESH *ppCloneMesh) {
        if (!ppCloneMesh) return D3DERR_INVALIDCALL;
        RanMesh *m = new RanMesh(pDevice ? pDevice : m_device, m_numFaces, m_numVerts, Options, FVF);
        // Same FVF: a straight copy. Different: keep position and let the rest
        // default, which is what every caller here needs (they re-fill it).
        if (FVF == m_fvf) {
            m->m_vertices = m_vertices;
        } else {
            for (DWORD i = 0; i < m_numVerts; ++i)
                memcpy(&m->m_vertices[(size_t)i * m->m_stride],
                       &m_vertices[(size_t)i * m_stride],
                       m->m_stride < m_stride ? m->m_stride : m_stride);
        }
        m->m_indices = m_indices;
        m->m_attributes = m_attributes;
        m->m_attribTable = m_attribTable;
        *ppCloneMesh = m;
        return D3D_OK;
    }
    HRESULT __stdcall CloneMesh(DWORD Options, const D3DVERTEXELEMENT9 *, LPDIRECT3DDEVICE9 pDevice,
                                LPD3DXMESH *ppCloneMesh) {
        return CloneMeshFVF(Options, m_fvf, pDevice, ppCloneMesh);
    }
    HRESULT __stdcall GetVertexBuffer(LPDIRECT3DVERTEXBUFFER9 *ppVB) {
        if (ppVB) *ppVB = NULL;
        return D3DERR_INVALIDCALL;                 // the data lives here, not in a VB
    }
    HRESULT __stdcall GetIndexBuffer(LPDIRECT3DINDEXBUFFER9 *ppIB) {
        if (ppIB) *ppIB = NULL;
        return D3DERR_INVALIDCALL;
    }
    HRESULT __stdcall LockVertexBuffer(DWORD, LPVOID *ppData) {
        if (!ppData) return D3DERR_INVALIDCALL;
        *ppData = m_vertices.empty() ? NULL : &m_vertices[0];
        return D3D_OK;
    }
    HRESULT __stdcall UnlockVertexBuffer() { return D3D_OK; }
    HRESULT __stdcall LockIndexBuffer(DWORD, LPVOID *ppData) {
        if (!ppData) return D3DERR_INVALIDCALL;
        *ppData = m_indices.empty() ? NULL : &m_indices[0];
        return D3D_OK;
    }
    HRESULT __stdcall UnlockIndexBuffer() { return D3D_OK; }
    HRESULT __stdcall GetAttributeTable(D3DXATTRIBUTERANGE *pAttribTable, DWORD *pAttribTableSize) {
        if (!pAttribTableSize) return D3DERR_INVALIDCALL;
        if (m_attribTable.empty()) rebuildAttributeTable();
        if (!pAttribTable) { *pAttribTableSize = (DWORD)m_attribTable.size(); return D3D_OK; }
        DWORD n = *pAttribTableSize < m_attribTable.size() ? *pAttribTableSize
                                                          : (DWORD)m_attribTable.size();
        for (DWORD i = 0; i < n; ++i) pAttribTable[i] = m_attribTable[i];
        *pAttribTableSize = n;
        return D3D_OK;
    }
    HRESULT __stdcall ConvertPointRepsToAdjacency(const DWORD *, DWORD *) { return E_NOTIMPL; }
    HRESULT __stdcall ConvertAdjacencyToPointReps(const DWORD *, DWORD *) { return E_NOTIMPL; }
    HRESULT __stdcall GenerateAdjacency(FLOAT, DWORD *pAdjacency) {
        // Neighbour information is only used for optimisation passes that are
        // no-ops here; "no neighbour" is the honest answer.
        if (pAdjacency) for (DWORD i = 0; i < m_numFaces * 3; ++i) pAdjacency[i] = 0xFFFFFFFF;
        return D3D_OK;
    }
    HRESULT __stdcall UpdateSemantics(D3DVERTEXELEMENT9[MAX_FVF_DECL_SIZE]) { return D3D_OK; }

    // --- ID3DXMesh
    HRESULT __stdcall LockAttributeBuffer(DWORD, DWORD **ppData) {
        if (!ppData) return D3DERR_INVALIDCALL;
        *ppData = m_attributes.empty() ? NULL : &m_attributes[0];
        return D3D_OK;
    }
    HRESULT __stdcall UnlockAttributeBuffer() { m_attribTable.clear(); return D3D_OK; }
    HRESULT __stdcall Optimize(DWORD Options, const DWORD *, DWORD *, DWORD *, LPD3DXBUFFER *,
                               LPD3DXMESH *ppOptMesh) {
        if (!ppOptMesh) return D3DERR_INVALIDCALL;
        return CloneMeshFVF(Options, m_fvf, m_device, ppOptMesh);
    }
    HRESULT __stdcall OptimizeInplace(DWORD, const DWORD *, DWORD *, DWORD *, LPD3DXBUFFER *) {
        sortFacesByAttribute();
        return D3D_OK;
    }
    HRESULT __stdcall SetAttributeTable(const D3DXATTRIBUTERANGE *pAttribTable, DWORD cAttribTableSize) {
        m_attribTable.assign(pAttribTable, pAttribTable + cAttribTableSize);
        return D3D_OK;
    }

private:
    void sortFacesByAttribute() {
        // Stable counting sort: subsets become contiguous, which is what
        // DrawSubset and the attribute table both want.
        std::vector<WORD>  idx;
        std::vector<DWORD> att;
        idx.reserve(m_indices.size());
        att.reserve(m_attributes.size());
        DWORD maxAttr = 0;
        for (DWORD f = 0; f < m_numFaces; ++f) if (m_attributes[f] > maxAttr) maxAttr = m_attributes[f];
        for (DWORD a = 0; a <= maxAttr; ++a) {
            for (DWORD f = 0; f < m_numFaces; ++f) {
                if (m_attributes[f] != a) continue;
                idx.push_back(m_indices[(size_t)f * 3 + 0]);
                idx.push_back(m_indices[(size_t)f * 3 + 1]);
                idx.push_back(m_indices[(size_t)f * 3 + 2]);
                att.push_back(a);
            }
        }
        m_indices.swap(idx);
        m_attributes.swap(att);
        m_attribTable.clear();
    }

    void rebuildAttributeTable() {
        m_attribTable.clear();
        for (DWORD f = 0; f < m_numFaces; ) {
            DWORD a = m_attributes[f];
            DWORD start = f;
            while (f < m_numFaces && m_attributes[f] == a) ++f;
            D3DXATTRIBUTERANGE r;
            r.AttribId = a;
            r.FaceStart = start;
            r.FaceCount = f - start;
            r.VertexStart = 0;
            r.VertexCount = m_numVerts;
            m_attribTable.push_back(r);
        }
    }
};

// -------------------------------------------------------- .x Mesh -> RanMesh
const XNode *findChild(const XNode *node, const char *typeName) {
    for (size_t i = 0; i < node->children.size(); ++i) {
        const XNode *c = XNode_Deref(node->children[i]);
        if (c->typeName == typeName) return c;
    }
    return NULL;
}

// Members are packed in stream order, so reading one is a cursor walk.
struct Cursor {
    const BYTE *p;
    size_t left;
    Cursor(const std::vector<BYTE> &v) : p(v.empty() ? NULL : &v[0]), left(v.size()) {}
    bool u32(unsigned &v) {
        if (left < 4) return false;
        memcpy(&v, p, 4); p += 4; left -= 4;
        return true;
    }
    bool f32(float &v) {
        if (left < 4) return false;
        memcpy(&v, p, 4); p += 4; left -= 4;
        return true;
    }
    bool vec3(Vec3 &v) { return f32(v.x) && f32(v.y) && f32(v.z); }
    bool vec2(Vec2 &v) { return f32(v.u) && f32(v.v); }
};

HRESULT meshFromNode(const XNode *mesh, DWORD options, LPDIRECT3DDEVICE9 device,
                     LPD3DXBUFFER *ppAdjacency, LPD3DXBUFFER *ppMaterials,
                     LPD3DXBUFFER *ppEffectInstances, DWORD *pNumMaterials,
                     LPD3DXMESH *ppMesh) {
    if (!mesh || !ppMesh) return D3DERR_INVALIDCALL;
    *ppMesh = NULL;

    Cursor c(mesh->data);
    unsigned numVerts = 0;
    if (!c.u32(numVerts) || numVerts == 0) return D3DXERR_INVALIDDATA;

    std::vector<Vec3> positions(numVerts);
    for (unsigned i = 0; i < numVerts; ++i)
        if (!c.vec3(positions[i])) return D3DXERR_INVALIDDATA;

    unsigned numFacesIn = 0;
    if (!c.u32(numFacesIn)) return D3DXERR_INVALIDDATA;

    // A face may have more than three corners; triangulate as a fan.
    std::vector<unsigned> tri;
    std::vector<unsigned> trisPerFace(numFacesIn, 0);
    std::vector<std::vector<unsigned> > faceCorners(numFacesIn);
    tri.reserve((size_t)numFacesIn * 3);
    for (unsigned f = 0; f < numFacesIn; ++f) {
        unsigned n = 0;
        if (!c.u32(n) || n < 3 || n > 32) return D3DXERR_INVALIDDATA;
        std::vector<unsigned> corner(n);
        for (unsigned k = 0; k < n; ++k)
            if (!c.u32(corner[k])) return D3DXERR_INVALIDDATA;
        for (unsigned k = 2; k < n; ++k) {
            tri.push_back(corner[0]);
            tri.push_back(corner[k - 1]);
            tri.push_back(corner[k]);
        }
        trisPerFace[f] = n - 2;
        faceCorners[f].swap(corner);
    }
    const DWORD numFaces = (DWORD)(tri.size() / 3);
    if (!numFaces) return D3DXERR_INVALIDDATA;

    // Optional channels decide the FVF.
    const XNode *normalsNode = findChild(mesh, "MeshNormals");
    const XNode *texNode     = findChild(mesh, "MeshTextureCoords");
    const XNode *matNode     = findChild(mesh, "MeshMaterialList");

    std::vector<Vec3> normals;
    if (normalsNode) {
        Cursor nc(normalsNode->data);
        unsigned n = 0;
        if (nc.u32(n) && n) {
            std::vector<Vec3> src(n);
            bool ok = true;
            for (unsigned i = 0; i < n; ++i)
                if (!nc.vec3(src[i])) { ok = false; break; }

            if (ok && n == numVerts) {
                normals.swap(src);
            } else if (ok) {
                //  Normals are indexed by their own face list (a corner-normal
                //  mesh), so each vertex takes the average of the corners that
                //  reference it. Matching the vertex count is the exception,
                //  not the rule: a 176-vertex piece ships 528 normals.
                unsigned nFaces = 0;
                if (nc.u32(nFaces)) {
                    std::vector<Vec3> acc(numVerts);
                    std::vector<unsigned> hits(numVerts, 0);
                    memset(&acc[0], 0, sizeof(Vec3) * numVerts);
                    bool walked = true;
                    for (unsigned f = 0; f < nFaces && f < numFacesIn && walked; ++f) {
                        unsigned cnt = 0;
                        if (!nc.u32(cnt) || cnt < 3 || cnt > 32) { walked = false; break; }
                        for (unsigned k = 0; k < cnt; ++k) {
                            unsigned ni = 0;
                            if (!nc.u32(ni)) { walked = false; break; }
                            if (ni >= src.size()) continue;
                            //  The normal face list runs parallel to the mesh
                            //  face list, so corner k here is corner k there.
                            if (k >= faceCorners[f].size()) continue;
                            const unsigned vi = faceCorners[f][k];
                            if (vi >= numVerts) continue;
                            acc[vi].x += src[ni].x; acc[vi].y += src[ni].y; acc[vi].z += src[ni].z;
                            ++hits[vi];
                        }
                    }
                    if (walked) {
                        normals.resize(numVerts);
                        for (unsigned i = 0; i < numVerts; ++i) {
                            if (!hits[i]) { normals[i].x = 0; normals[i].y = 1; normals[i].z = 0; continue; }
                            float x = acc[i].x, y = acc[i].y, z = acc[i].z;
                            float len = sqrtf(x * x + y * y + z * z);
                            if (len < 1e-6f) { normals[i].x = 0; normals[i].y = 1; normals[i].z = 0; }
                            else { normals[i].x = x / len; normals[i].y = y / len; normals[i].z = z / len; }
                        }
                    }
                }
            }
        }
    }
    std::vector<Vec2> uvs;
    if (texNode) {
        Cursor tc(texNode->data);
        unsigned n = 0;
        if (tc.u32(n) && n) {
            uvs.resize(n);
            for (unsigned i = 0; i < n; ++i)
                if (!tc.vec2(uvs[i])) { uvs.clear(); break; }
        }
    }

    //  Some exports carry no MeshNormals/MeshTextureCoords at all: the extra
    //  per-vertex channels live in a DeclData block instead, a D3DVERTEXELEMENT9
    //  array followed by one packed record per vertex. Without reading it the
    //  mesh has no UVs, so every pixel samples texel 0 and the piece draws as a
    //  flat colour that looks exactly like a missing texture.
    if (normals.size() != numVerts || uvs.size() != numVerts) {
        const XNode *declNode = findChild(mesh, "DeclData");
        if (declNode) {
            Cursor dc(declNode->data);
            unsigned nElem = 0;
            if (dc.u32(nElem) && nElem && nElem <= 32) {
                struct Elem { unsigned type, usage; unsigned dwords, offset; };
                std::vector<Elem> elems(nElem);
                unsigned strideDW = 0;
                bool ok = true;
                for (unsigned i = 0; i < nElem && ok; ++i) {
                    unsigned type = 0, method = 0, usage = 0, usageIndex = 0;
                    if (!dc.u32(type) || !dc.u32(method) || !dc.u32(usage) || !dc.u32(usageIndex)) { ok = false; break; }
                    //  D3DDECLTYPE sizes, in DWORDs. Anything outside this set
                    //  would desync the record walk, so give up rather than
                    //  guess a stride.
                    static const unsigned kSize[] = { 1, 2, 3, 4, 1, 1, 1, 2, 1, 2, 2, 4, 2, 4, 2, 4, 2 };
                    if (type >= sizeof(kSize) / sizeof(kSize[0])) { ok = false; break; }
                    elems[i].type = type;
                    elems[i].usage = usage;
                    elems[i].dwords = kSize[type];
                    elems[i].offset = strideDW;
                    strideDW += kSize[type];
                }
                unsigned nDW = 0;
                if (ok && strideDW && dc.u32(nDW) && nDW / strideDW >= numVerts) {
                    if (dc.left >= (size_t)nDW * 4) {
                        const BYTE *raw = dc.p;
                        const size_t recBytes = (size_t)strideDW * 4;
                        for (unsigned e = 0; e < nElem; ++e) {
                            const Elem &el = elems[e];
                            const size_t off = (size_t)el.offset * 4;
                            if (el.usage == 3 && el.type == 2 && normals.size() != numVerts) {
                                normals.resize(numVerts);
                                for (unsigned i = 0; i < numVerts; ++i)
                                    memcpy(&normals[i], raw + (size_t)i * recBytes + off, 12);
                            } else if (el.usage == 5 && el.type == 1 && uvs.size() != numVerts) {
                                uvs.resize(numVerts);
                                for (unsigned i = 0; i < numVerts; ++i)
                                    memcpy(&uvs[i], raw + (size_t)i * recBytes + off, 8);
                            }
                        }
                    }
                }
            }
        }
    }

    DWORD fvf = D3DFVF_XYZ;
    if (normals.size() == numVerts) fvf |= D3DFVF_NORMAL;
    if (uvs.size() == numVerts)     fvf |= D3DFVF_TEX1;

    RanMesh *out = new RanMesh(device, numFaces, numVerts, options, fvf);

    const DWORD stride = out->m_stride;
    for (unsigned i = 0; i < numVerts; ++i) {
        BYTE *v = &out->m_vertices[(size_t)i * stride];
        memcpy(v, &positions[i], 12);
        DWORD off = 12;
        if (fvf & D3DFVF_NORMAL) { memcpy(v + off, &normals[i], 12); off += 12; }
        if (fvf & D3DFVF_TEX1)   { memcpy(v + off, &uvs[i], 8); off += 8; }
    }
    for (size_t i = 0; i < tri.size(); ++i) out->m_indices[i] = (WORD)tri[i];

    // Materials: the list gives one index per ORIGINAL face, so a triangulated
    // face inherits its parent's index.
    DWORD numMaterials = 0;
    if (matNode) {
        Cursor mc(matNode->data);
        unsigned nMat = 0, nFaceIdx = 0;
        if (mc.u32(nMat) && mc.u32(nFaceIdx)) {
            numMaterials = nMat;
            std::vector<unsigned> perFace(nFaceIdx);
            for (unsigned i = 0; i < nFaceIdx; ++i)
                if (!mc.u32(perFace[i])) { perFace.clear(); break; }

            if (!perFace.empty()) {
                DWORD t = 0;
                for (unsigned f = 0; f < numFacesIn && t < numFaces; ++f) {
                    unsigned attr = (nFaceIdx == 1) ? perFace[0]
                                  : (f < perFace.size() ? perFace[f] : 0);
                    for (unsigned k = 0; k < trisPerFace[f] && t < numFaces; ++k)
                        out->m_attributes[t++] = attr;
                }
                for (; t < numFaces; ++t) out->m_attributes[t] = 0;
            }
        }

        if (ppMaterials) {
            //  Collect the materials before allocating, because the texture
            //  names have to live INSIDE the buffer we hand back.
            //
            //  They used to be pointers into the XFile's string storage, and
            //  loadFromBytes deletes the XFile as soon as this returns - so
            //  every pTextureFilename was dangling before the caller ever read
            //  it. Whether that mattered came down to the allocator: read the
            //  freed block soon enough and the old bytes were still there and
            //  the texture loaded, read it once the block had been reused and
            //  strlen found a 0 and the name came back empty. DxSimMesh then
            //  copied that empty string, failed to load any texture, and drew
            //  the subset untextured - the blank white money and item drops.
            //
            //  D3DX puts the strings after the D3DXMATERIAL array in the same
            //  buffer, which is why callers may hold the pointers for as long
            //  as they hold the buffer. Do the same.
            std::vector<const XNode *> matNodes;
            std::vector<std::string> matNames;
            for (size_t i = 0; i < matNode->children.size() && matNodes.size() < numMaterials; ++i) {
                const XNode *m = XNode_Deref(matNode->children[i]);
                if (m->typeName != "Material") continue;
                matNodes.push_back(m);
                std::string name;
                const XNode *tex = findChild(m, "TextureFilename");
                if (tex && tex->data.size() >= sizeof(char *)) {
                    const char *p = NULL;
                    memcpy(&p, &tex->data[0], sizeof(p));
                    if (p) name = p;
                }
                matNames.push_back(name);
            }

            const DWORD slots = numMaterials ? numMaterials : 1;
            size_t strBytes = 0;
            for (size_t i = 0; i < matNames.size(); ++i)
                if (!matNames[i].empty()) strBytes += matNames[i].size() + 1;

            RanBuffer *buf = new RanBuffer(sizeof(D3DXMATERIAL) * slots + strBytes);
            D3DXMATERIAL *mats = (D3DXMATERIAL *)buf->GetBufferPointer();
            memset(mats, 0, buf->GetBufferSize());
            char *strp = (char *)buf->GetBufferPointer() + sizeof(D3DXMATERIAL) * slots;

            for (size_t i = 0; i < matNodes.size(); ++i) {
                const XNode *m = matNodes[i];
                Cursor mcur(m->data);
                D3DXMATERIAL &dst = mats[i];
                float r, g, b, a, power, sr, sg, sb, er, eg, eb;
                if (mcur.f32(r) && mcur.f32(g) && mcur.f32(b) && mcur.f32(a) &&
                    mcur.f32(power) && mcur.f32(sr) && mcur.f32(sg) && mcur.f32(sb) &&
                    mcur.f32(er) && mcur.f32(eg) && mcur.f32(eb)) {
                    dst.MatD3D.Diffuse.r = r; dst.MatD3D.Diffuse.g = g;
                    dst.MatD3D.Diffuse.b = b; dst.MatD3D.Diffuse.a = a;
                    dst.MatD3D.Ambient = dst.MatD3D.Diffuse;
                    dst.MatD3D.Specular.r = sr; dst.MatD3D.Specular.g = sg; dst.MatD3D.Specular.b = sb;
                    dst.MatD3D.Power = power;
                    dst.MatD3D.Emissive.r = er; dst.MatD3D.Emissive.g = eg; dst.MatD3D.Emissive.b = eb;
                }
                if (!matNames[i].empty()) {
                    memcpy(strp, matNames[i].c_str(), matNames[i].size() + 1);
                    dst.pTextureFilename = (LPSTR)strp;
                    strp += matNames[i].size() + 1;
                }
            }
            if (!numMaterials) numMaterials = 1;
            *ppMaterials = buf;
        }
    } else if (ppMaterials) {
        *ppMaterials = new RanBuffer(0);
    }

    if (pNumMaterials) *pNumMaterials = numMaterials;
    if (ppAdjacency) {
        RanBuffer *adj = new RanBuffer(sizeof(DWORD) * numFaces * 3);
        DWORD *p = (DWORD *)adj->GetBufferPointer();
        for (DWORD i = 0; i < numFaces * 3; ++i) p[i] = 0xFFFFFFFF;
        *ppAdjacency = adj;
    }
    if (ppEffectInstances) *ppEffectInstances = new RanBuffer(0);

    *ppMesh = out;
    return D3D_OK;
}

// The first Mesh anywhere under a node — .x files wrap meshes in frames.
const XNode *findMesh(const XNode *node) {
    if (!node) return NULL;
    if (node->typeName == "Mesh") return node;
    for (size_t i = 0; i < node->children.size(); ++i) {
        const XNode *m = findMesh(node->children[i]);
        if (m) return m;
    }
    return NULL;
}

HRESULT loadFromBytes(const void *bytes, size_t size, DWORD options, LPDIRECT3DDEVICE9 device,
                      LPD3DXBUFFER *ppAdjacency, LPD3DXBUFFER *ppMaterials,
                      LPD3DXBUFFER *ppEffectInstances, DWORD *pNumMaterials, LPD3DXMESH *ppMesh) {
    XFile *file = XFile_Parse(bytes, size);
    if (!file) return D3DXERR_INVALIDDATA;

    const XNode *mesh = NULL;
    for (size_t i = 0; i < file->roots.size() && !mesh; ++i) mesh = findMesh(file->roots[i]);
    if (!mesh) { delete file; return D3DXERR_INVALIDDATA; }

    HRESULT hr = meshFromNode(mesh, options, device, ppAdjacency, ppMaterials,
                              ppEffectInstances, pNumMaterials, ppMesh);
    delete file;                                   // the mesh copied what it needs
    return hr;
}

} // namespace

// --------------------------------------------------------------- entry points
extern "C" HRESULT WINAPI D3DXLoadMeshFromXof(LPD3DXFILEDATA pxofMesh, DWORD Options,
                                              LPDIRECT3DDEVICE9 pD3DDevice,
                                              LPD3DXBUFFER *ppAdjacency,
                                              LPD3DXBUFFER *ppMaterials,
                                              LPD3DXBUFFER *ppEffectInstances,
                                              DWORD *pNumMaterials, LPD3DXMESH *ppMesh) {
    XNode *node = RanXFile_NodeOf(pxofMesh);
    if (!node) return D3DERR_INVALIDCALL;
    const XNode *mesh = findMesh(node);
    if (!mesh) return D3DXERR_INVALIDDATA;
    return meshFromNode(mesh, Options, pD3DDevice, ppAdjacency, ppMaterials,
                        ppEffectInstances, pNumMaterials, ppMesh);
}

extern "C" HRESULT WINAPI D3DXLoadMeshFromXInMemory(LPCVOID Memory, DWORD SizeOfMemory,
                                                    DWORD Options, LPDIRECT3DDEVICE9 pD3DDevice,
                                                    LPD3DXBUFFER *ppAdjacency,
                                                    LPD3DXBUFFER *ppMaterials,
                                                    LPD3DXBUFFER *ppEffectInstances,
                                                    DWORD *pNumMaterials, LPD3DXMESH *ppMesh) {
    return loadFromBytes(Memory, SizeOfMemory, Options, pD3DDevice, ppAdjacency, ppMaterials,
                         ppEffectInstances, pNumMaterials, ppMesh);
}

extern "C" HRESULT WINAPI D3DXLoadMeshFromXA(LPCSTR pFilename, DWORD Options,
                                             LPDIRECT3DDEVICE9 pD3DDevice,
                                             LPD3DXBUFFER *ppAdjacency, LPD3DXBUFFER *ppMaterials,
                                             LPD3DXBUFFER *ppEffectInstances,
                                             DWORD *pNumMaterials, LPD3DXMESH *ppMesh) {
    if (!pFilename) return D3DERR_INVALIDCALL;
    FILE *f = fopen(pFilename, "rb");
    if (!f) return D3DXERR_INVALIDDATA;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return D3DXERR_INVALIDDATA; }
    std::vector<BYTE> bytes((size_t)n);
    size_t got = fread(&bytes[0], 1, (size_t)n, f);
    fclose(f);
    if (!got) return D3DXERR_INVALIDDATA;
    return loadFromBytes(&bytes[0], got, Options, pD3DDevice, ppAdjacency, ppMaterials,
                         ppEffectInstances, pNumMaterials, ppMesh);
}

extern "C" HRESULT WINAPI D3DXLoadMeshFromXW(LPCWSTR pFilename, DWORD Options,
                                             LPDIRECT3DDEVICE9 pD3DDevice,
                                             LPD3DXBUFFER *ppAdjacency, LPD3DXBUFFER *ppMaterials,
                                             LPD3DXBUFFER *ppEffectInstances,
                                             DWORD *pNumMaterials, LPD3DXMESH *ppMesh) {
    std::string s;
    for (const WCHAR *w = pFilename; w && *w; ++w) s.push_back((char)(*w & 0xFF));
    return D3DXLoadMeshFromXA(s.c_str(), Options, pD3DDevice, ppAdjacency, ppMaterials,
                              ppEffectInstances, pNumMaterials, ppMesh);
}

extern "C" HRESULT WINAPI D3DXCreateMeshFVF(DWORD NumFaces, DWORD NumVertices, DWORD Options,
                                            DWORD FVF, LPDIRECT3DDEVICE9 pD3DDevice,
                                            LPD3DXMESH *ppMesh) {
    if (!ppMesh) return D3DERR_INVALIDCALL;
    *ppMesh = new RanMesh(pD3DDevice, NumFaces, NumVertices, Options, FVF);
    return D3D_OK;
}

extern "C" HRESULT WINAPI D3DXCreateBuffer(DWORD NumBytes, LPD3DXBUFFER *ppBuffer) {
    if (!ppBuffer) return D3DERR_INVALIDCALL;
    *ppBuffer = new RanBuffer(NumBytes);
    return D3D_OK;
}

//  Shared with the hierarchy loader, which needs the same Mesh -> ID3DXMesh
//  conversion but drives it per frame.
HRESULT RanMesh_FromXNode(const XNode *mesh, DWORD options, LPDIRECT3DDEVICE9 device,
                          LPD3DXBUFFER *ppAdjacency, LPD3DXBUFFER *ppMaterials,
                          LPD3DXBUFFER *ppEffectInstances, DWORD *pNumMaterials,
                          LPD3DXMESH *ppMesh) {
    return meshFromNode(mesh, options, device, ppAdjacency, ppMaterials,
                        ppEffectInstances, pNumMaterials, ppMesh);
}
