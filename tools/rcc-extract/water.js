'use strict';
//
// Decode the property blob DxFrame::LoadEffect carries for water — a per-frame
// effect COMPONENT (`DxEffectWater`/`DxEffectWater2`), not a `.egp` particle
// node. `mapobj.js`'s `readEffect` already walks past this blob for every
// effect type; this module turns the raw bytes into real parameters when the
// type ID is one of the two water effects.
//
// Source read in full: SOURCE/Lib_Engine/DxEffect/DxEffectWater[2].h/.cpp.
//
//   DEF_EFFECT_WATER  = 0x2001   DxEffectWater   (grid + cosine wave surface)
//   DEF_EFFECT_WATER2 = 0x3013   DxEffectWater2  (grid + bobbing height +
//                                                 UV-scroll + optional
//                                                 real-time reflection)
//
// Both attach via `AdaptToDxFrame`, which sets `m_matFrameComb =
// pFrame->matCombined` — the ENCLOSING DxFrame's world matrix. That is the
// placement authority, not the effect's own optional affine-parts block (that
// block feeds `m_pmatLocal`, applied on TOP of the frame matrix at render:
// `matCombined = m_pmatLocal * m_matFrameComb`, DxEffectWater.cpp:472). The
// caller (mapobj.js) supplies the frame's matCombined; this module only
// decodes the property blob itself.
//
// Struct sizes and field offsets are from the layout probe
// (tools/layout-probe/layout.json), never hand-computed — WATER_PROPERTY_100
// is 300 bytes, not the 292 a hand count of "8 floats + 2 ints + DWORD*2 +
// MAX_PATH" would suggest you check twice; WATER_PROPERTY_101 adds a 64-byte
// D3DXMATRIX and is 364; WATER2_PROPERTY carries SEVEN 260-byte name buffers
// (one base texture + six cubemap faces) and is 1988.
const LAYOUT = require('../layout-probe/layout.json');

const S100 = LAYOUT.allStructs.WATER_PROPERTY_100;
const S101 = LAYOUT.allStructs.WATER_PROPERTY_101;
const S2 = LAYOUT.allStructs.WATER2_PROPERTY;
const SRIVER = LAYOUT.allStructs.RIVER_PROPERTY;
if (!S100 || !S101 || !S2 || !SRIVER) {
  throw new Error('water.js: layout-probe has no WATER_PROPERTY/RIVER_PROPERTY structs — rebuild the probe');
}

const WATER_TYPE = LAYOUT.consts.DEF_EFFECT_WATER;   // 0x2001, compiler-verified
const WATER2_TYPE = LAYOUT.consts.DEF_EFFECT_WATER2; // 0x3013, compiler-verified
// River — DEF_EFFECT_RIVER, 0x2006. Measured (not assumed) to be the water
// system this server's shipped maps actually use: every standalone effect
// list (grasseff.js) that stops on an unrecognised type stops on 0x2006
// ver 0x107, never on Water/Water2. See decodeRiver below for scope.
const RIVER_TYPE = LAYOUT.consts.DEF_EFFECT_RIVER;

/** D3DCOLOR is 0xAARRGGBB. */
function argb(dw) {
  return {
    a: ((dw >>> 24) & 0xff) / 255, r: ((dw >>> 16) & 0xff) / 255,
    g: ((dw >>> 8) & 0xff) / 255, b: (dw & 0xff) / 255,
  };
}

function cstr(buf, off, len) {
  const slice = buf.subarray(off, off + len);
  const nul = slice.indexOf(0);
  return (nul >= 0 ? slice.subarray(0, nul) : slice).toString('latin1');
}

function f(buf, spec, name) { return buf.readFloatLE(spec.fields[name].off); }
function i(buf, spec, name) { return buf.readInt32LE(spec.fields[name].off); }
function u(buf, spec, name) { return buf.readUInt32LE(spec.fields[name].off); }
function s(buf, spec, name) { const fl = spec.fields[name]; return cstr(buf, fl.off, fl.size); }
/** 16 floats, row-major, same layout `mapobj.js`'s `matrix()` reads. */
function mat16(buf, spec, name) {
  const off = spec.fields[name].off;
  const m = new Float32Array(16);
  for (let k = 0; k < 16; k++) m[k] = buf.readFloatLE(off + k * 4);
  return m;
}

/**
 * `DxEffectWater::SetProperty` (DxEffectWater.cpp:37-65): version 0x101 is
 * memcpy'd directly; version 0x100 is remapped field-by-field into the same
 * shape (it lacks `m_matFrameComb`).
 *
 * `m_matFrameComb` is itself part of the union with the property, so this
 * memcpy is ALSO how placement survives when the effect is loaded through
 * `EffectLoadToList` (the standalone-list path — grasseff.js) rather than
 * `DxFrame::LoadEffect` (the frame-attached path — mapobj.js): the latter
 * calls `AdaptToDxFrame` afterwards and overwrites `m_matFrameComb` from a
 * live `DxFrame`, but the former never does, so whatever was memcpy'd from
 * the SAVED property blob is the effect's entire world transform. This is
 * exactly what DxLandMan.cpp:1656-1665 means by "their world transform is
 * CACHED in m_matFrameComb" — cached at author/save time, not re-derived.
 * `frameMatrix` here is that snapshot; null for 0x100, which predates it.
 */
function decodeWater(buf, version) {
  let spec;
  if (version === 0x101 && buf.length >= S101.size) spec = S101;
  else if (version === 0x100 && buf.length >= S100.size) spec = S100;
  else return null;

  return {
    kind: 1, version,
    sizeX: f(buf, spec, 'm_SizeX'), sizeZ: f(buf, spec, 'm_SizeZ'),
    col: i(buf, spec, 'm_col'), row: i(buf, spec, 'm_row'),
    waveCycle: f(buf, spec, 'm_fWaveCycle'),
    velocity: f(buf, spec, 'm_Velocity'),
    heightChange: f(buf, spec, 'm_HeightChange'),
    depth: f(buf, spec, 'm_Depth'),
    diffuse: argb(u(buf, spec, 'm_DiffuseColor')),
    texture: s(buf, spec, 'm_szTexture'),
    frameMatrix: spec === S101 ? mat16(buf, spec, 'm_matFrameComb') : null,
  };
}

/**
 * `DxEffectWater2::SetProperty` (DxEffectWater2.cpp:78-89): exact version+size
 * match only. Same cached-placement mechanism as Water — see decodeWater's note.
 */
function decodeWater2(buf, version) {
  if (version !== 0x100 || buf.length < S2.size) return null;
  const spec = S2;
  return {
    kind: 2, version,
    frameMatrix: mat16(buf, spec, 'm_matFrameComb'),
    col: i(buf, spec, 'm_nCol'), row: i(buf, spec, 'm_nRow'),
    size: f(buf, spec, 'm_fSize'),
    playType: i(buf, spec, 'm_nPlayType'),
    waveCycle: f(buf, spec, 'm_fWaveCycle'),
    refractionRate: f(buf, spec, 'm_fRefractionRate'),
    heightMax: f(buf, spec, 'm_fHeightMax'), heightMin: f(buf, spec, 'm_fHeightMin'),
    heightSpeed: f(buf, spec, 'm_fHeightSpeed'),
    textureMoveVelocity: f(buf, spec, 'm_fTextureMoveVelocity'),
    textureDirection: [
      buf.readFloatLE(spec.fields.m_vTextureDirection.off),
      buf.readFloatLE(spec.fields.m_vTextureDirection.off + 4),
      buf.readFloatLE(spec.fields.m_vTextureDirection.off + 8),
    ],
    // D3DXCOLOR is RGBA float, not D3DCOLOR — a different layout to m_DiffuseColor above.
    diffuseMin: {
      r: buf.readFloatLE(spec.fields.m_clrDiffuseMin.off),
      g: buf.readFloatLE(spec.fields.m_clrDiffuseMin.off + 4),
      b: buf.readFloatLE(spec.fields.m_clrDiffuseMin.off + 8),
      a: buf.readFloatLE(spec.fields.m_clrDiffuseMin.off + 12),
    },
    diffuseMax: {
      r: buf.readFloatLE(spec.fields.m_clrDiffuseMax.off),
      g: buf.readFloatLE(spec.fields.m_clrDiffuseMax.off + 4),
      b: buf.readFloatLE(spec.fields.m_clrDiffuseMax.off + 8),
      a: buf.readFloatLE(spec.fields.m_clrDiffuseMax.off + 12),
    },
    reflectionMode: u(buf, spec, 'm_bReflectionMode') !== 0,
    reflectionAlpha: f(buf, spec, 'm_fReflectionAlpha'),
    texture: s(buf, spec, 'm_szTexture'),
  };
}

/**
 * `DxEffectRiver::SetProperty` (DxEffectRiver.cpp:68-136). Only the LIVE
 * version (0x107 = `VERSION`, DxEffectRiver.cpp:46) is decoded — the only one
 * measured in this corpus. Unlike Water, `m_vMax`/`m_vMin` for THIS version
 * are used AS-IS with no transform (`RIVER_PROPERTY` v0x107 has no
 * `m_matFrameComb` member at all, and the v0x107 branch of SetProperty is a
 * plain memcpy with no `COLLISION::TransformAABB` call, unlike every older
 * version's branch) — so they are already the real WORLD-SPACE bounding box.
 * That is also why river needs no affine-parts / frameMatrix handling: there
 * is nothing to compose.
 *
 * Deliberately NOT decoded: the river's own mesh geometry (`DxWaterTree`,
 * loaded through `LoadBuffer`, which River overrides — unlike Water/Water2,
 * whose base-class LoadBuffer is a no-op DWORD(0)). That geometry follows the
 * real authored shoreline; what ships here instead is a rectangular grid
 * spanning `[vMin, vMax]` in XZ — a real, measured world-space placement and
 * size, but not the authored shoreline polygon. Flagged, not hidden.
 */
function decodeRiver(buf, version) {
  if (version !== 0x107 || buf.length < SRIVER.size) return null;
  const spec = SRIVER;
  const flag = u(buf, spec, 'm_dwFlag');
  return {
    kind: 3, version,
    flag,
    useDark: !!(flag & 0x001), useFlash: !!(flag & 0x002),
    useReflect: !!(flag & 0x004), useReflectNew: !!(flag & 0x008),
    useSee: !!(flag & 0x010), useSameHeight: !!(flag & 0x020),
    bumpAlpha: f(buf, spec, 'm_fBumpAlpha'),
    velocity: f(buf, spec, 'm_fVel'),
    scale: f(buf, spec, 'm_fScale'),
    color: {
      r: buf.readFloatLE(spec.fields.m_vColor.off) / 255,
      g: buf.readFloatLE(spec.fields.m_vColor.off + 4) / 255,
      b: buf.readFloatLE(spec.fields.m_vColor.off + 8) / 255,
    },
    darkScale: f(buf, spec, 'm_fDarkScale'),
    // The dark layer's own linear UV scroll (DxEffectRiverDraw.cpp:93:
    // `m_vAddTex_Dark += fElapsedTime * m_vDarkVel`) — independent of
    // m_fVel, which only drives the flash layer's circular drift.
    darkVel: [
      buf.readFloatLE(spec.fields.m_vDarkVel.off),
      buf.readFloatLE(spec.fields.m_vDarkVel.off + 4),
    ],
    darkColor: {
      r: buf.readFloatLE(spec.fields.m_vDarkColor.off) / 255,
      g: buf.readFloatLE(spec.fields.m_vDarkColor.off + 4) / 255,
      b: buf.readFloatLE(spec.fields.m_vDarkColor.off + 8) / 255,
    },
    max: [
      buf.readFloatLE(spec.fields.m_vMax.off),
      buf.readFloatLE(spec.fields.m_vMax.off + 4),
      buf.readFloatLE(spec.fields.m_vMax.off + 8),
    ],
    min: [
      buf.readFloatLE(spec.fields.m_vMin.off),
      buf.readFloatLE(spec.fields.m_vMin.off + 4),
      buf.readFloatLE(spec.fields.m_vMin.off + 8),
    ],
    textureDark: s(buf, spec, 'm_szTexDark'),
    textureFlash: s(buf, spec, 'm_szTexFlash'),
  };
}

/**
 * @param {number} typeId  the effect TypeID `readEffect` already extracted
 * @param {number} version the effect's own dwVer (not the file version)
 * @param {Buffer} buf     the raw property blob, length === dwSize
 * @returns {object|null}  decoded params, or null if not water / not decodable
 */
function decode(typeId, version, buf) {
  if (typeId === WATER_TYPE) return decodeWater(buf, version);
  if (typeId === WATER2_TYPE) return decodeWater2(buf, version);
  if (typeId === RIVER_TYPE) return decodeRiver(buf, version);
  return null;
}

/**
 * `DXAFFINEPARTS` (DxMethods.h:86-98): `vTrans, vRotate, vScale`, 12 bytes
 * each = 36 total (the probe's measured size). `vRotate` is (yaw, pitch,
 * roll) in THAT axis order — `D3DXQuaternionRotationYawPitchRoll(vRotate.x,
 * vRotate.y, vRotate.z)`, DxMethods.cpp:206 — not a plain XYZ Euler triple.
 *
 * mapobj.js's generic `readEffect` reads the LAST 12 bytes as translation and
 * gets (1,1,1) on every record — a scale vector — because translation is
 * actually FIRST. That comment is what sent this straight to the real struct
 * rather than guessing again.
 */
function decodeAffineParts(buf, off) {
  return {
    trans: [buf.readFloatLE(off), buf.readFloatLE(off + 4), buf.readFloatLE(off + 8)],
    rotateYPR: [buf.readFloatLE(off + 12), buf.readFloatLE(off + 16), buf.readFloatLE(off + 20)],
    scale: [buf.readFloatLE(off + 24), buf.readFloatLE(off + 28), buf.readFloatLE(off + 32)],
  };
}

function mul4(a, b) {
  const r = new Float32Array(16);
  for (let i = 0; i < 4; i++) {
    for (let j = 0; j < 4; j++) {
      let s = 0;
      for (let k = 0; k < 4; k++) s += a[i * 4 + k] * b[k * 4 + j];
      r[i * 4 + j] = s;
    }
  }
  return r;
}

/**
 * `D3DXMatrixCompX` (DxMethods.cpp:199-217): `Mat = matScale * matRotate *
 * matTrans`, row-major / row-vector D3D convention — the exact composition
 * `EffectLoadToList`-loaded water uses for `m_pmatLocal` (its ONLY placement:
 * this loader never calls `AdaptToDxFrame`/`AdaptToEffList`, so
 * `m_matFrameComb` stays the identity the constructor sets, and
 * `matCombined = m_pmatLocal * m_matFrameComb = m_pmatLocal`).
 *
 * Returned as 16 floats in the SAME row-major layout `mapobj.js`'s
 * `matrix()`/`extract-mapobj.js`'s object transforms already use, so the
 * Unity side decomposes it with the existing `RanAnimFile.Decompose` helper —
 * no new convention introduced.
 */
function composeAffine({ trans, rotateYPR, scale }) {
  const [tx, ty, tz] = trans;
  const [yaw, pitch, roll] = rotateYPR;
  const [sx, sy, sz] = scale;

  const sy2 = Math.sin(yaw / 2), cy2 = Math.cos(yaw / 2);
  const sp2 = Math.sin(pitch / 2), cp2 = Math.cos(pitch / 2);
  const sr2 = Math.sin(roll / 2), cr2 = Math.cos(roll / 2);
  const qx = cy2 * sp2 * cr2 + sy2 * cp2 * sr2;
  const qy = sy2 * cp2 * cr2 - cy2 * sp2 * sr2;
  const qz = cy2 * cp2 * sr2 - sy2 * sp2 * cr2;
  const qw = cy2 * cp2 * cr2 + sy2 * sp2 * sr2;

  const xx = qx * qx, yy = qy * qy, zz = qz * qz;
  const xy = qx * qy, xz = qx * qz, yz = qy * qz;
  const wx = qw * qx, wy = qw * qy, wz = qw * qz;
  // eslint-disable-next-line no-multi-spaces
  const matRotate = new Float32Array([
    1 - 2 * (yy + zz), 2 * (xy + wz), 2 * (xz - wy), 0,
    2 * (xy - wz), 1 - 2 * (xx + zz), 2 * (yz + wx), 0,
    2 * (xz + wy), 2 * (yz - wx), 1 - 2 * (xx + yy), 0,
    0, 0, 0, 1,
  ]);
  const matScale = new Float32Array([sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, sz, 0, 0, 0, 0, 1]);
  const matTrans = new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, tx, ty, tz, 1]);

  return mul4(mul4(matScale, matRotate), matTrans);
}

const IDENTITY = new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]);

module.exports = {
  decode, composeAffine, decodeAffineParts, mul4, IDENTITY,
  WATER_TYPE, WATER2_TYPE, RIVER_TYPE,
};
