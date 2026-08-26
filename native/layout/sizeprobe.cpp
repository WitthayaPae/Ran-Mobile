// Size probe for the structures the .wld / mesh loaders read with
// ReadBuffer(..., sizeof(T)). Any size that differs between the 32-bit MSVC ABI
// the data was written with and the ABI we run under desynchronises the stream,
// which surfaces as a load that silently stops early or a crash on garbage.
#include "stdafx.h"

#include "DxAniKeys.h"
#include "DxMethods.h"
#include "SAnimationInfo.h"
#include "DxLandDef.h"
#include "DxFrameMesh.h"
#include "DxSkinCharData.h"
#include "GLDefine.h"

SPositionKey        g_pos;
SRotateKey          g_rot;
SScaleKey           g_scale;
SMatrixKey          g_matkey;
SQuatPosKey         g_quatpos;
DXAFFINEPARTS       g_affine;
SANIMSTRIKE         g_strike;
SANIMCONINFO_104    g_conInfo104;
SLAND_FILEMARK      g_mark;
SLAND_FILEMARK_100  g_mark100;
SBONESCALE_100      g_boneScale;
D3DEXMATERIAL       g_exMaterial;
SNATIVEID           g_nativeId;
D3DXMATRIX          g_mat;
D3DXMATRIXA16       g_mata16;
D3DXVECTOR3         g_v3;
D3DXVECTOR2         g_v2;
D3DXQUATERNION      g_quat;
D3DMATERIAL9        g_mtrl;
D3DXATTRIBUTERANGE  g_attrRange;
