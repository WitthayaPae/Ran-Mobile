'use strict';
//
// Decode the packed keyframe structs that `xanim.js` returns as raw buffers.
//
// `xanim.js` deliberately stops at `{ count, stride, data }` — it proves the
// stream was walked correctly without committing to what the bytes mean. This
// turns those bytes into values.
//
// Struct layouts are from `Lib_Engine/Meshs/DxAniKeys.h`, with the sizes coming
// from the layout probe rather than being hand-computed:
//
//   SPositionKey  16   DWORD dwTime @0, D3DXVECTOR3 vPos      @4
//   SScaleKey     16   DWORD dwTime @0, D3DXVECTOR3 vScale    @4
//   SRotateKey    20   DWORD dwTime @0, D3DXQUATERNION quat   @4   (x,y,z,w)
//   SMatrixKey    80   DWORD dwTime @0, D3DXMATRIXA16 mat     @16
//   SQuatPosKey   36   DWORD @0, vScale @4, vPos @16, QUATCOMP @28
//
// `SMatrixKey` is the one worth staring at: the matrix sits at offset **16**,
// not 4, because `D3DXMATRIXA16` is 16-byte aligned. 4+64=68 is the intuitive
// answer and it is wrong; the struct is 80 bytes.
//
const X = require('./xanim');

// SAnimation.cpp:22-23. Note these are NOT reciprocals of the same number.
const DIV_1_65535 = 0.000015259021896;
const DIV_1_32768 = 0.000030517578125;

/**
 * `DecompressionQuaternion` (SAnimation.cpp:51-67), replicated exactly.
 *
 * x, y and z are stored biased by 32767 and scaled by 1/32768; **w is neither
 * biased nor scaled the same way** — it uses 1/65535 with no subtraction. That
 * asymmetry is deliberate: `CompressionQuaternion` negates the whole quaternion
 * whenever w < 0, so w is always in 0..1 and needs no sign range. "Tidying" this
 * into four symmetric channels yields rotations that look almost right, which is
 * the worst possible failure.
 */
function decompressQuat(first, second) {
  return [
    ((first >>> 16) - 32767) * DIV_1_32768,
    ((first & 0xffff) - 32767) * DIV_1_32768,
    ((second >>> 16) - 32767) * DIV_1_32768,
    (second & 0xffff) * DIV_1_65535,
  ];
}

/** Guard: a decoded track must have the stride the probe says it has. */
function checkStride(track, expected, what) {
  if (!track) return false;
  if (track.stride !== expected) {
    throw new Error(`animkeys: ${what} stride ${track.stride}, expected ${expected}`);
  }
  if (track.data.length < track.count * track.stride) {
    throw new Error(`animkeys: ${what} has ${track.data.length} bytes for ${track.count} keys`);
  }
  return track.count > 0;
}

function readVec3Keys(track, expected, what) {
  if (!checkStride(track, expected, what)) return null;
  const times = new Uint32Array(track.count);
  const values = new Float32Array(track.count * 3);
  for (let i = 0; i < track.count; i++) {
    const o = i * track.stride;
    times[i] = track.data.readUInt32LE(o);
    values[i * 3] = track.data.readFloatLE(o + 4);
    values[i * 3 + 1] = track.data.readFloatLE(o + 8);
    values[i * 3 + 2] = track.data.readFloatLE(o + 12);
  }
  return { times, values };
}

function readRotateKeys(track) {
  if (!checkStride(track, X.KEY.rotate, 'rotate')) return null;
  const times = new Uint32Array(track.count);
  const values = new Float32Array(track.count * 4);
  for (let i = 0; i < track.count; i++) {
    const o = i * track.stride;
    times[i] = track.data.readUInt32LE(o);
    for (let k = 0; k < 4; k++) values[i * 4 + k] = track.data.readFloatLE(o + 4 + k * 4);
  }
  return { times, values };
}

function readMatrixKeys(track) {
  if (!checkStride(track, X.KEY.matrix, 'matrix')) return null;
  const times = new Uint32Array(track.count);
  const values = new Float32Array(track.count * 16);
  for (let i = 0; i < track.count; i++) {
    const o = i * track.stride;
    times[i] = track.data.readUInt32LE(o);
    for (let k = 0; k < 16; k++) {
      values[i * 16 + k] = track.data.readFloatLE(o + 16 + k * 4);  // @16, not @4
    }
  }
  return { times, values };
}

function readQuatPosKeys(track) {
  if (!checkStride(track, X.KEY.quatPos, 'quatPos')) return null;
  const times = new Uint32Array(track.count);
  const scales = new Float32Array(track.count * 3);
  const positions = new Float32Array(track.count * 3);
  const rotations = new Float32Array(track.count * 4);
  for (let i = 0; i < track.count; i++) {
    const o = i * track.stride;
    times[i] = track.data.readUInt32LE(o);
    for (let k = 0; k < 3; k++) {
      scales[i * 3 + k] = track.data.readFloatLE(o + 4 + k * 4);
      positions[i * 3 + k] = track.data.readFloatLE(o + 16 + k * 4);
    }
    const q = decompressQuat(track.data.readUInt32LE(o + 28), track.data.readUInt32LE(o + 32));
    for (let k = 0; k < 4; k++) rotations[i * 4 + k] = q[k];
  }
  return { times, scales, positions, rotations };
}

/**
 * Decode one bone track into plain arrays.
 * Every channel is optional and most tracks carry only one kind.
 */
function decodeTrack(a) {
  return {
    version: a.version,
    bone: a.bone,
    position: readVec3Keys(a.position, X.KEY.position, 'position'),
    scale: readVec3Keys(a.scale, X.KEY.scale, 'scale'),
    rotate: readRotateKeys(a.rotate),
    matrix: readMatrixKeys(a.matrix),
    quatPos: readQuatPosKeys(a.quatPos),
  };
}

/** Highest key time across every channel — the clip length in engine ticks. */
function trackDuration(t) {
  let max = 0;
  for (const ch of [t.position, t.scale, t.rotate, t.matrix, t.quatPos]) {
    if (ch && ch.times.length) max = Math.max(max, ch.times[ch.times.length - 1]);
  }
  return max;
}

module.exports = { decodeTrack, decompressQuat, trackDuration,
                   readVec3Keys, readRotateKeys, readMatrixKeys, readQuatPosKeys,
                   DIV_1_65535, DIV_1_32768 };
