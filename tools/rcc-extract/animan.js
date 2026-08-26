'use strict';
//
// The DxAnimationMan section of a `.wld` — the map's ANIMATED frame meshes.
//
// `DxLandMan::LoadFile_VER116` reads it right after the replace-piece chain
// (DxAnimationManagerSaveLoad.cpp:50):
//
//   [BOOL exists] and then per manager:
//     u32  ANIMATETYPE            (LOOP / RND / HIT)
//     f32  fCurTime
//     [BOOL hasFrame]  -> DxFrame::LoadFile   (mapobj.readFrame decodes this)
//     [BOOL hasAni]    -> DxAnimation::LoadFile chain
//     [BOOL hasNext]   -> next manager, same shape
//
// On log_in this is where the rest of the scenery lives: the "Camera01" path
// the login camera plays, four "Ground[Mesh]" frames, and the swaying grass
// (srp_grass_d_01/02.dds). A static port takes each frame at its authored
// matCombined — time zero of the loop.
//
// DxAnimation::LoadFile (DxAnimationSaveLoad.cpp:68) is skippable by size:
//   u32 cPos    -> SPositionKey[c]   16 bytes each
//   u32 cRot    -> SRotateKey[c]     20 bytes each
//   u32 cScale  -> SScaleKey[c]      16 bytes each
//   u32 cMatrix -> SMatrixKey[c]     80 bytes each (A16-aligned matrix)
//   [BOOL next] -> chain
//   [BOOL hasName] -> [i32 len][len bytes]
//
const MO = require('./mapobj');

function skipAnimationChain(c, depth = 0) {
  // The name trails each NODE after its next-chain: for A -> B the wire is
  // [A keys][1][B keys][0][B name?][A name?] — DxAnimation::LoadFile recurses
  // into pAnimNext before it reads its own szName. Mirror the recursion.
  if (depth > 10000) throw new Error('animan: animation chain too deep');
  c.p += 4 + c.b.readUInt32LE(c.p) * 16;         // position keys
  c.p += 4 + c.b.readUInt32LE(c.p) * 20;         // rotate keys
  c.p += 4 + c.b.readUInt32LE(c.p) * 16;         // scale keys
  c.p += 4 + c.b.readUInt32LE(c.p) * 80;         // matrix keys
  const next = c.u32();
  if (next === 1) skipAnimationChain(c, depth + 1);
  else if (next !== 0) throw new Error(`animan: bad chain flag ${next}`);
  if (c.u32() === 1) {
    const len = c.u32();
    if (len > 4096) throw new Error('animan: implausible name length');
    c.p += len;
  }
}

/**
 * Walk the manager chain. Returns the frames of every manager (flat) and the
 * offset of the first byte after the section.
 */
function walkAniMan(buf, at, stats) {
  const c = new MO.Cursor(buf, at);
  const frames = [];
  let exists = c.u32();
  let managers = 0;

  while (exists === 1) {
    if (++managers > 10000) throw new Error('animan: implausible manager count');
    c.u32();                                     // ANIMATETYPE
    c.p += 4;                                    // fCurTime
    if (c.bool()) MO.readFrame(c, frames, stats);
    if (c.bool()) {
      // The name at the chain tail binds a track to a frame; the skip below
      // consumes it with the keys.
      skipAnimationChain(c);
    }
    exists = c.u32();
  }
  return { frames, end: c.p, managers };
}

module.exports = { walkAniMan, skipAnimationChain };
