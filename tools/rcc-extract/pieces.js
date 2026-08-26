'use strict';
//
// The REPLACE-PIECE system — the vegetation and prop instances the map places
// from external `.pis` files.
//
// `DxLandMan::LoadFile_VER116` (DxLandManSaveLoad.cpp:1521) reads, directly
// after the DxOctree section, a chain of DXPIECEOBJ records:
//
//   [BOOL more]                       1 while entries follow
//     [f32 fCurTime]                  animation phase
//     [BOOL hasFile]                  then [u32 len][len bytes, NUL included]
//     [BOOL hasFrame]                 then ANOTHER BOOL, then [u32 len][bytes]
//     [f32[16] matComb]               row-major D3D world matrix
//
// Each entry instances `data/piece/<file>` via DxReplaceContainer::LoadPiece.
// This chain is why log_in.wld reported "no objects": all its scenery beyond
// the terrain — 116 trees — lives here, not in the octree. The PC login
// renders them; a port that skips this chain shows a bare field.
//
// A `.pis` (DXREPLACEPIECE::Load, DxReplaceContainer.cpp:210) is a container
// of formats this pipeline already reads:
//
//   132-byte CSerialFile header
//   f32[3] vMax, f32[3] vMin
//   [BOOL hasFrame]      -> DxFrame tree        (not seen in shipped pieces)
//   [BOOL hasAnim]       -> DxAnimationMan
//   [BOOL bRendAni]
//   [u32 effectCount]    -> effect entries
//   [BOOL landEff chain]
//   [BOOL hasStaticMesh] -> DxStaticMesh INLINE (same [version][blockSize]
//                           bucket container as the .wld0 sidecar)
//
// The shipped tree pieces carry everything in the inline static mesh: FVF
// 0x152 geometry whose IN-VB DIFFUSE is the baked lighting (landType !=
// DAYNIGHT_ON, so the colour thread never rewrites it, and the land path
// draws with D3DRS_LIGHTING = FALSE).
//
const SM = require('./staticmesh');

/**
 * Walk the DXPIECEOBJ chain. `at` must be the first byte after the DxOctree
 * section (mapobj.parse's `end`).
 */
function walkReplaceChain(buf, at) {
  const out = [];
  let o = at;
  const u32 = () => { const v = buf.readUInt32LE(o); o += 4; return v; };
  const f32 = () => { const v = buf.readFloatLE(o); o += 4; return v; };

  let exist = u32();
  while (exist === 1) {
    if (out.length > 200000) throw new Error('pieces: implausible chain length');
    const time = f32();
    let file = null;
    if (u32() === 1) {
      const n = u32();
      if (n > 1024) throw new Error('pieces: implausible file-name length');
      file = buf.toString('latin1', o, o + n - 1);   // len includes the NUL
      o += n;
    }
    if (u32() === 1) {
      // The frame name sits behind a SECOND exist flag (DxLandManSaveLoad
      // .cpp:1540-1550) — the outer BOOL says "a name slot follows", the
      // inner one says whether it is non-empty.
      if (u32() === 1) {
        const n = u32();
        if (n > 1024) throw new Error('pieces: implausible frame-name length');
        o += n;
      }
    }
    const matrix = new Array(16);
    for (let i = 0; i < 16; i++) matrix[i] = buf.readFloatLE(o + i * 4);
    o += 64;
    out.push({ file, time, matrix });
    exist = u32();
  }
  return { instances: out, end: o };
}

/**
 * Parse a `.pis` file into a flat mesh list the ROBJ encoder understands:
 * one entry per geometry-bearing octree node, node-local indices.
 */
function parsePis(buf, name) {
  const c = new SM.Cursor(buf, 132);
  const vMax = c.vec3();
  const vMin = c.vec3();

  if (c.u32()) throw new Error(`${name}: DxFrame pieces not supported yet`);
  if (c.u32()) throw new Error(`${name}: animated pieces not supported yet`);
  c.u32();                                   // bRendAni
  const fx = c.u32();
  if (fx) throw new Error(`${name}: piece effects not supported yet`);
  if (c.u32()) throw new Error(`${name}: piece land-effects not supported yet`);
  if (!c.u32()) return { vMax, vMin, meshes: [] };

  const version = c.u32();
  c.u32();                                   // blockSize — advisory only
  c.vec3(); c.vec3();                        // static-mesh AABB, repeats above
  const buckets = SM.BUCKETS[version];
  if (!buckets) throw new Error(`${name}: unknown static-mesh version 0x${version.toString(16)}`);

  const meshes = [];
  for (const bucket of buckets) {
    const count = c.u32();
    if (count > 100000) throw new Error(`${name}: implausible mesh count`);
    for (let i = 0; i < count; i++) {
      c.str();                               // map key, repeats the tex name
      const m = SM.readSingleTexMesh(c, {});
      const walk = (nodes) => {
        for (const n of nodes || []) {
          const g = n.geometry;
          if (g && g.vertexCount > 0 && g.faceCount > 0) {
            meshes.push({ geometry: g, texture: m.texName || '', bucket });
          }
          if (n.children) walk(n.children);
        }
      };
      walk(m.octree && m.octree.nodes);
    }
  }
  return { vMax, vMin, meshes, bytesRead: c.p, fileSize: buf.length };
}

module.exports = { walkReplaceChain, parsePis };
