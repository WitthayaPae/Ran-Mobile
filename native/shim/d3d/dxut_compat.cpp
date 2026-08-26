#include "dxut_compat.h"
static IDirect3D9       *g_pD3D    = NULL;
static IDirect3DDevice9 *g_pDevice = NULL;
static D3DSURFACE_DESC   g_backDesc = {};
IDirect3DDevice9 *DXUTGetD3DDevice() { return g_pDevice; }
IDirect3D9       *DXUTGetD3DObject() { return g_pD3D; }
void DXUTSetD3D(IDirect3D9 *pD3D, IDirect3DDevice9 *pDev) { g_pD3D = pD3D; g_pDevice = pDev; }
const D3DSURFACE_DESC *DXUTGetBackBufferSurfaceDesc() { return &g_backDesc; }
extern "C" const GUID IID_IDirect3DBaseTexture9_shim =
    { 0x580ca87e, 0x1d3c, 0x4d54, { 0x99, 0x1d, 0xb7, 0xd3, 0xe3, 0xc2, 0x98, 0xce } };

D3DXMATRIX D3DUtil_GetCubeMapViewMatrix(DWORD dwFace) {
    D3DXVECTOR3 vEye(0, 0, 0), vLook, vUp;
    switch (dwFace) {
        case D3DCUBEMAP_FACE_POSITIVE_X: vLook = D3DXVECTOR3( 1, 0, 0); vUp = D3DXVECTOR3(0, 1, 0); break;
        case D3DCUBEMAP_FACE_NEGATIVE_X: vLook = D3DXVECTOR3(-1, 0, 0); vUp = D3DXVECTOR3(0, 1, 0); break;
        case D3DCUBEMAP_FACE_POSITIVE_Y: vLook = D3DXVECTOR3( 0, 1, 0); vUp = D3DXVECTOR3(0, 0,-1); break;
        case D3DCUBEMAP_FACE_NEGATIVE_Y: vLook = D3DXVECTOR3( 0,-1, 0); vUp = D3DXVECTOR3(0, 0, 1); break;
        case D3DCUBEMAP_FACE_POSITIVE_Z: vLook = D3DXVECTOR3( 0, 0, 1); vUp = D3DXVECTOR3(0, 1, 0); break;
        default:                         vLook = D3DXVECTOR3( 0, 0,-1); vUp = D3DXVECTOR3(0, 1, 0); break;
    }
    D3DXMATRIX mView;
    D3DXMatrixLookAtLH(&mView, &vEye, &vLook, &vUp);
    return mView;
}
