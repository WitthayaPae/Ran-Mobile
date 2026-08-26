// Minimal stand-in for DXUT: the engine calls only the device accessors.
#pragma once
#include "windows.h"
#include <d3d9.h>
IDirect3DDevice9 *DXUTGetD3DDevice();
IDirect3D9       *DXUTGetD3DObject();
void              DXUTSetD3D(IDirect3D9 *pD3D, IDirect3DDevice9 *pDev);
const D3DSURFACE_DESC *DXUTGetBackBufferSurfaceDesc();
#ifdef __cplusplus
inline HRESULT DXUTTrace(const char *, DWORD, HRESULT hr, const wchar_t *, bool) { return hr; }
#endif
#define V(x)        { hr = (x); }
#define V_RETURN(x) { hr = (x); if (FAILED(hr)) return hr; }
#define SAFE_DELETE(p)       { if(p) { delete (p);       (p)=NULL; } }
#define SAFE_DELETE_ARRAY(p) { if(p) { delete[] (p);     (p)=NULL; } }
#define SAFE_RELEASE(p)      { if(p) { (p)->Release();   (p)=NULL; } }

// D3DUtil helpers lifted from DXUTmisc (the only ones the engine calls).
#ifdef __cplusplus
#include <d3dx9math.h>
inline void D3DUtil_InitMaterial(D3DMATERIAL9 &mtrl, float r, float g, float b, float a) {
    memset(&mtrl, 0, sizeof(mtrl));
    mtrl.Diffuse.r = mtrl.Ambient.r = r;
    mtrl.Diffuse.g = mtrl.Ambient.g = g;
    mtrl.Diffuse.b = mtrl.Ambient.b = b;
    mtrl.Diffuse.a = mtrl.Ambient.a = a;
}
inline void D3DUtil_InitLight(D3DLIGHT9 &light, D3DLIGHTTYPE ltType,
                              float x, float y, float z) {
    memset(&light, 0, sizeof(light));
    light.Type = ltType;
    light.Diffuse.r = light.Diffuse.g = light.Diffuse.b = 1.0f;
    D3DXVECTOR3 vDir(x, y, z);
    D3DXVec3Normalize((D3DXVECTOR3 *)&light.Direction, &vDir);
    light.Position.x = x; light.Position.y = y; light.Position.z = z;
    light.Range = 1000.0f;
}
#endif
// The SDK declares these IIDs; the .lib that defines them is Windows-only.
#ifdef __cplusplus
extern "C" const GUID IID_IDirect3DBaseTexture9_shim;
#define IID_IDirect3DBaseTexture9 IID_IDirect3DBaseTexture9_shim
#endif
#ifdef __cplusplus
D3DXMATRIX D3DUtil_GetCubeMapViewMatrix(DWORD dwFace);
#endif
#define DDERR_INVALIDPARAMS ((HRESULT)0x80070057L)
#ifdef __cplusplus
inline HWND DXUTGetHWNDDeviceFullScreen() { return NULL; }
inline HWND DXUTGetHWNDDeviceWindowed() { return NULL; }
#endif
