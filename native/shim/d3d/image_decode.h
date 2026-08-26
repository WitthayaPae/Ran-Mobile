// Image decoding for the D3DX texture loaders.
//
// The client ships DDS (mostly DXT1/3/5), with TGA/BMP/PNG appearing in a few
// places. D3DX did this on Windows; here it is done explicitly so the shim can
// hand the GL backend the exact surface format the engine believes it has —
// TextureManager keys its alpha handling off D3DFMT_DXT1 vs DXT3/5, so a loader
// that silently expanded everything to ARGB would change engine behaviour.
#pragma once
#include "windows.h"
#include <d3d9.h>
#include <vector>

struct RanImage {
    UINT width = 0, height = 0;
    UINT mipLevels = 0;
    D3DFORMAT format = D3DFMT_UNKNOWN;
    UINT fileFormat = 0;                       // D3DXIFF_*
    std::vector<std::vector<BYTE> > levels;    // level 0 first
};

// Decodes from memory. Returns false if the bytes are not a format we read.
bool RanImage_Decode(const void *data, size_t size, RanImage &out);

// A cube map: six faces in D3D order (+X, -X, +Y, -Y, +Z, -Z), each with its
// own mip chain. Returns false unless the bytes really are a cube DDS.
bool RanImage_DecodeCube(const void *data, size_t size, RanImage outFaces[6]);

// Whatever the platform decoder understands (JPEG, WebP, ...), tried only after
// the readers above decline. Defined in image_decode_android.cpp.
bool RanImage_DecodeAndroid(const void *data, size_t size, RanImage &out);
