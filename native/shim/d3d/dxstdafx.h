// Replacement for Lib_Engine/DxCommon9/dxstdafx.h.
// The game only uses DXUT for the device accessors, so the sample framework is dropped.
#pragma once
#include "windows.h"
#include <ddraw.h>
#include <d3d9types.h>
#include <d3dx9math.h>
#include <d3dx9mesh.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <dxerr9.h>
#include <rmxfguid.h>
#include <dxfile.h>
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <dinput.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <dsound.h>
#define STRSAFE_NO_DEPRECATE
#include <strsafe.h>
#include "dxut_compat.h"

#define LPDIRECT3DDEVICEQ       LPDIRECT3DDEVICE9
#define LPDIRECT3DTEXTUREQ      LPDIRECT3DTEXTURE9
#define LPDIRECT3DSURFACEQ      LPDIRECT3DSURFACE9
#define LPDIRECT3DVERTEXBUFFERQ LPDIRECT3DVERTEXBUFFER9
#define LPDIRECT3DINDEXBUFFERQ  LPDIRECT3DINDEXBUFFER9
#define LPDIRECT3DCUBETEXTUREQ  LPDIRECT3DCUBETEXTURE9
#define D3DVIEWPORTQ            D3DVIEWPORT9
#define D3DMATERIALQ            D3DMATERIAL9
#define D3DLIGHTQ               D3DLIGHT9
#define LPDIRECT3DQ             LPDIRECT3D9
#define D3DCAPSQ                D3DCAPS9

#include "xrmxftmpl.h"
#include "dxutil.h"
#include "DSUtil.h"
#include "DebugSet.h"
#include "profile.h"

#include <string>
#include <iostream>
#include <fstream>
#define TSTRING     std::string
#define TCERR       std::cerr
#define TOFSTREAM   std::ofstream
#define TSTREAM     std::fstream
#define TFSTREAM    std::fstream
