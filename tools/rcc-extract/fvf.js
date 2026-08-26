'use strict';
//
// D3D9 fixed-function FVF decoding.
//
// The octree writer does not record a vertex struct — it records the FVF DWORD
// and writes `vertexCount * D3DXGetFVFVertexSize(fvf)` raw bytes. So the stride
// must be derived from the FVF, not hardcoded. Two show up in shipped terrain:
//
//   0x112  D3DFVF_XYZ|NORMAL|TEX1              VERTEX              32 bytes
//   0x152  D3DFVF_XYZ|NORMAL|DIFFUSE|TEX1      VERTEXNORCOLORTEX   36 bytes
//
// but the file is free to contain others, and `OctreeLoadOLD` uses the FVF
// where a version number would otherwise be, so this has to be general.
//
// Reference: d3d9types.h, and DxVertexFVF.h for the structs the engine uses.
//

// Position masks (D3DFVF_POSITION_MASK = 0x400E)
const XYZ = 0x002;
const XYZRHW = 0x004;
const XYZB1 = 0x006;
const XYZB2 = 0x008;
const XYZB3 = 0x00a;
const XYZB4 = 0x00c;
const XYZB5 = 0x00e;
const XYZW = 0x4002;

const NORMAL = 0x010;
const PSIZE = 0x020;
const DIFFUSE = 0x040;
const SPECULAR = 0x080;

const TEXCOUNT_MASK = 0xf00;
const TEXCOUNT_SHIFT = 8;

// Per-texture-stage coordinate sizes live in bits 16+, two bits per stage.
// 00 = 2 floats, 01 = 3, 10 = 4, 11 = 1.
const TEXCOORD_SIZE_FLOATS = [2, 3, 4, 1];

function positionFloats(fvf) {
  switch (fvf & 0x400e) {
    case XYZ: return 3;
    case XYZRHW: return 4;
    case XYZB1: return 4;
    case XYZB2: return 5;
    case XYZB3: return 6;
    case XYZB4: return 7;
    case XYZB5: return 8;
    case XYZW: return 4;
    default: return 0;
  }
}

const texCount = (fvf) => (fvf & TEXCOUNT_MASK) >>> TEXCOUNT_SHIFT;

function texCoordFloats(fvf, stage) {
  const bits = (fvf >>> (16 + stage * 2)) & 3;
  return TEXCOORD_SIZE_FLOATS[bits];
}

/** Byte size of one vertex for this FVF — the D3DX function of the same name. */
function vertexSize(fvf) {
  let bytes = positionFloats(fvf) * 4;
  if (fvf & NORMAL) bytes += 12;
  if (fvf & PSIZE) bytes += 4;
  if (fvf & DIFFUSE) bytes += 4;
  if (fvf & SPECULAR) bytes += 4;
  for (let t = 0; t < texCount(fvf); t++) bytes += texCoordFloats(fvf, t) * 4;
  return bytes;
}

/**
 * Byte offsets of the components we care about, or null when absent.
 * Order is fixed by D3D: position, blend weights, normal, psize, diffuse,
 * specular, then texture coordinates.
 */
function layout(fvf) {
  let off = 0;
  const out = { size: 0, position: null, normal: null, diffuse: null,
                specular: null, texcoords: [] };

  const pos = positionFloats(fvf);
  if (pos) { out.position = off; off += pos * 4; }
  if (fvf & NORMAL) { out.normal = off; off += 12; }
  if (fvf & PSIZE) off += 4;
  if (fvf & DIFFUSE) { out.diffuse = off; off += 4; }
  if (fvf & SPECULAR) { out.specular = off; off += 4; }
  for (let t = 0; t < texCount(fvf); t++) {
    const n = texCoordFloats(fvf, t);
    out.texcoords.push({ offset: off, floats: n });
    off += n * 4;
  }
  out.size = off;
  return out;
}

// The two the terrain writer actually emits (NsOCTree.cpp:82-94).
const FVF_VERTEX = 0x112;              // pos + normal + uv        -> 32
const FVF_VERTEX_NOR_COLOR_TEX = 0x152; // pos + normal + rgba + uv -> 36

module.exports = {
  vertexSize, layout, texCount, positionFloats,
  FVF_VERTEX, FVF_VERTEX_NOR_COLOR_TEX,
  XYZ, NORMAL, DIFFUSE, SPECULAR, PSIZE, TEXCOUNT_MASK, TEXCOUNT_SHIFT,
};
