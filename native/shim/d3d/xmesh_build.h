//  Building an ID3DXMesh out of a parsed .x `Mesh` node, shared between the
//  single-mesh loader (D3DXLoadMeshFromX*) and the hierarchy loader.
#pragma once

#include "windows.h"
#include <d3d9.h>
#include <d3dx9.h>

struct XNode;

//  Same contract as D3DXLoadMeshFromX*, but starting from an already parsed
//  node. Any out-parameter may be NULL.
HRESULT RanMesh_FromXNode(const XNode *mesh, DWORD options, LPDIRECT3DDEVICE9 device,
                          LPD3DXBUFFER *ppAdjacency, LPD3DXBUFFER *ppMaterials,
                          LPD3DXBUFFER *ppEffectInstances, DWORD *pNumMaterials,
                          LPD3DXMESH *ppMesh);
