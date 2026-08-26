'use strict';
//
// The ground-shadow overlay — DEF_EFFECT_SHADOW (0x3015), the static tree and
// tower shadows cast onto the terrain.
//
// It lives in the map's _AFTER_1 effect list (the pass the engine reserves for
// ground decals). The entry envelope is the usual EffectLoadToList shape; then:
//
//   SHADOW_PROPERTY (852 bytes, DxEffectShadow.h:26)
//     u32 flag, vec3 viewMax, viewMin, vMax, vMin, u32 texSize,
//     f32 shadowD, shadowP, viewMoveX, viewMoveZ,
//     char[260] filter, char[260] texDay, char[260] texNight
//   [BOOL affine] (+36)
//   [u32 loadBufferSize]                     <- DxEffectShadow::LoadBuffer
//   DxShadowAABB tree (DxEffectShadowEX.cpp:205):
//     [BOOL bUse] and if set a DxShadowTree node, then LEFT subtree, then RIGHT
//
//   DxShadowTree::Load (DxEffectShadowDraw.cpp:348):
//     vec3 vMin, vMax; f32 disX, disY, disZ; vec3 viewMin, viewMax;
//     f32 microSize; u32 vertNUM, faceNUM; u32 bufferSize;
//     then bufferSize bytes of geometry (skipped by Load, the DynamicLoad path
//     reads it): vec3[vertNUM] positions, then u16[faceNUM*3] node-local indices.
//
// The texture is texDay (log_in.dds), a grey-on-black lightmap. UVs are a
// planar projection using each node's OWN view box (MakeStaticShadowUV,
// DxEffectShadowDraw.cpp:269):
//
//   u = 1 - (x - viewMin.x)/(viewMax.x-viewMin.x) + microSize
//   v =     (z - viewMin.z)/(viewMax.z-viewMin.z) + microSize
//
// Grey shadow on pure black, no alpha channel -> the engine draws it
// SUBTRACTIVELY (dest - texel): black subtracts nothing, grey darkens. That is
// the standard grey-on-black shadow-decal blend and the only one consistent
// with this data (COLOROP=SELECTARG1 feeding an inherited blend, ZWrite off,
// depth-biased; DxRenderStates.cpp SetShadow).
//
const SHADOW_TYPE = 0x3015;

/**
 * Decode the shadow entry at `entryStart` (found by walking the effect lists).
 * Returns { texture, shadowD, meshes:[{positions,uvs,indices}] } or null.
 */
function decodeShadow(buf, entryStart) {
  let o = entryStart;
  const u32 = () => { const v = buf.readUInt32LE(o); o += 4; return v; };

  // envelope
  if (u32() === 1) { const n = u32(); o += n; }         // adapt-frame name
  const typeId = u32();
  const ver = u32();
  const propSize = u32();
  if (typeId !== SHADOW_TYPE || propSize !== 852) return null;
  const P = o;
  o += propSize;
  if (u32() === 1) o += 36;                              // affine

  const shadowD = buf.readFloatLE(P + 56);
  const texture = buf.toString('latin1', P + 72 + 260, P + 72 + 520).split('\0')[0];

  u32();                                                 // loadBuffer size (advisory)

  const meshes = [];
  // Recursive AABB walk, modelled with an explicit stack (LEFT before RIGHT,
  // matching DxShadowAABB::Load's recursion order).
  const stack = [{ stage: 0 }];
  let guard = 0;
  while (stack.length) {
    if (++guard > 2000000) throw new Error('shadow: tree walk did not terminate');
    const fr = stack[stack.length - 1];
    if (fr.stage === 0) {
      fr.stage = 1;
      const bUse = u32();
      if (bUse !== 1) { stack.pop(); continue; }        // empty child, no subtrees
      readNode(buf, () => o, (no) => { o = no; }, meshes);
      // fall through to push children
    } else if (fr.stage === 1) {
      fr.stage = 2;
      stack.push({ stage: 0 });                          // left
    } else if (fr.stage === 2) {
      fr.stage = 3;
      stack.push({ stage: 0 });                          // right
    } else {
      stack.pop();
    }
  }
  return { texture, shadowD, meshes, end: o };
}

function readNode(buf, getO, setO, meshes) {
  let o = getO();
  const f = () => { const v = buf.readFloatLE(o); o += 4; return v; };
  const u = () => { const v = buf.readUInt32LE(o); o += 4; return v; };

  o += 12;                                               // vMin
  o += 12;                                               // vMax
  o += 12;                                               // disX/Y/Z
  const viewMin = [f(), f(), f()];
  const viewMax = [f(), f(), f()];
  const micro = f();
  const vertNUM = u();
  const faceNUM = u();
  const bufSize = u();
  const bufAt = o;

  if (vertNUM > 0 && faceNUM > 0) {
    if (vertNUM > 0x100000 || faceNUM > 0x100000)
      throw new Error(`shadow: implausible node counts v=${vertNUM} f=${faceNUM}`);
    const positions = new Float32Array(vertNUM * 3);
    const uvs = new Float32Array(vertNUM * 2);
    const dx = viewMax[0] - viewMin[0];
    const dz = viewMax[2] - viewMin[2];
    let p = bufAt;
    for (let i = 0; i < vertNUM; i++) {
      const x = buf.readFloatLE(p), y = buf.readFloatLE(p + 4), z = buf.readFloatLE(p + 8);
      p += 12;
      positions[i * 3] = x; positions[i * 3 + 1] = y; positions[i * 3 + 2] = z;
      uvs[i * 2] = dx !== 0 ? 1 - (x - viewMin[0]) / dx + micro : 0;
      uvs[i * 2 + 1] = dz !== 0 ? (z - viewMin[2]) / dz + micro : 0;
    }
    const indices = new Uint16Array(faceNUM * 3);
    for (let i = 0; i < faceNUM * 3; i++) indices[i] = buf.readUInt16LE(p + i * 2);
    meshes.push({ positions, uvs, indices, vertexCount: vertNUM, faceCount: faceNUM });
  }

  setO(bufAt + bufSize);                                 // skip the geometry buffer
}

module.exports = { decodeShadow, SHADOW_TYPE };
