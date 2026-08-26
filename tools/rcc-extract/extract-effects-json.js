'use strict';
//
// Simplified effect definitions -> effects.json, for the Unity runtime player
// (Runtime/RanEffect.cs).  This is a PARALLEL, deliberately small track next to
// the full `.reff` pipeline (extract-effects.js): where that exports every one
// of the six modelled node types as a binary a ScriptedImporter builds prefabs
// from, this exports only the two types a simple billboard/particle player can
// reproduce faithfully with NO invention:
//
//   SEQUENCE (0x05)     a camera-facing quad running a col x row UV flipbook
//   PARTICLESYS (0x02)  a CPU particle emitter (rate/life/speed/size/colour)
//
// Both decode from the SAME measured layouts the rest of the effect tooling
// uses — effect-egp.js walks the tree, effect-props.js turns each node body into
// named fields from the MSVC layout probe. Nothing here computes an offset.
//
// MESH, GROUND, LIGHTNING and BLURSYS are ALSO emitted now (as "mesh"/"decal"/
// "beam"/"trail" layers) — see meshLayer/decalLayer/beamLayer/trailLayer below,
// each with its own approximation notes. MOVEROTATE draws nothing in the
// engine and is folded onto descendant layers instead of emitted on its own.
// The seven remaining rarer types are COUNTED but not emitted: faking them
// would be exactly the "plausible but fabricated" output the project rules
// forbid. The runtime treats a missing layer as a no-op, so an effect that
// also has an unmodelled type still plays every layer that IS here.
//
//   node extract-effects-json.js            write effects.json + report
//   node extract-effects-json.js --dry      report only, write nothing
//   node extract-effects-json.js --out P    write to a specific path
//
// Output: unity/RanMobile/Assets/Ran/Resources/effects.json, loaded at runtime
// by RanEffectDb via Resources.Load<TextAsset>("effects").
//
const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc');
const egp = require('./effect-egp');
const props = require('./effect-props');

const base = path.resolve(__dirname, '../../..');
const RAN = path.join(base, 'Ran');
const EFFECT_RCC = path.join(RAN, 'data/effect/Effect.rcc');
const OUT_DEFAULT = path.join(base,
  'MOBILE/unity/RanMobile/Assets/Ran/Resources/effects.json');

// Round to 4 significant decimals; drop -0 and integers' trailing noise. Keeps
// the JSON a third the size of full float32 text with no visible loss for a
// sprite's size/colour/timing.
function r4(x) {
  if (!Number.isFinite(x)) return 0;
  const v = Math.round(x * 1e4) / 1e4;
  return Object.is(v, -0) ? 0 : v;
}
function r4v(a) { return a.map(r4); }

// Texture id the runtime asks Resources for: basename, lowercased, no extension.
// The engine loads through D3DX (sniffs headers, ignores the extension), so the
// name on disk may be .dds where the effect names .tga; the stem is the stable
// key and stage-effect-textures.js resolves it against what actually ships.
function texId(name) {
  if (!name) return '';
  return path.basename(String(name)).toLowerCase().replace(/\.[^.]*$/, '').trim();
}

// SEQUENCE flag bits actually consumed by the runtime (DxEffectSequence.h).
const SEQ = {
  USEANI: 0x00000001, USEROTATE: 0x00000004, USEDIRECTION: 0x00000100,
  USETEXROTATE: 0x00000400, USESEQUENCELOOP: 0x00020000,
  USEBILLBOARD: 0x00040000, USEBILLBOARDUP: 0x00080000, USELIGHTING: 0x00100000,
};
// PARTICLESYS flag bits (DxEffectParticleSys.h).
const PAR = {
  USECOLLISION: 0x00000020, USEDIRECTION: 0x00000100, USEMESH: 0x00000800,
  USESEQUENCE: 0x00010000, USESEQUENCELOOP: 0x00020000,
  USEBILLBOARDALL: 0x00040000, USEBILLBOARDUP: 0x00080000, USETEXTURE: 0x00100000,
  USEGROUND: 0x00400000, USERANDOMLIFE: 0x01000000,
};
// MESH flag bits actually consumed by the runtime (DxEffectMesh.h). Only the two
// the CPU billboard can honour are named: USESCALE (size ramp is live, else the
// mesh keeps size 1) and USEBILLBOARD (face the camera vs use the node matrix).
const MESH = {
  USESCALE: 0x00000002, USESIZEXYZ: 0x00000004, USEBILLBOARD: 0x00040000,
  USEBILLBOARDUP: 0x00080000,
};
// GROUND flag bits (DxEffectGround.h): USEANI drives the flipbook.
const GRND = { USEANI: 0x00000001, USEROTATE: 0x00000004, USEHEIGHT: 0x00000010 };
// MOVEROTATE flag bits (DxEffectMoveRotate.h). USERANDROTATE adds a random static
// tilt; USEGOTOCENTER pulls the child toward the origin over life (not modelled).
const MOVR = { USERANDROTATE: 0x00000001, USEGOTOCENTER: 0x00004000 };
// BLURSYS flag bits (DxEffectBlurSys.h). Only USEABSOLUTE is surfaced (as
// `tabs`); NOUSE_BEZIER/USE_LOOP_RESET/USEREFRACT are not modelled by
// RanEffectBlurSys (see that file's class remarks).
const BLUR = { USEABSOLUTE: 0x00000001 };

// ---- LIGHTNING (beam) field layout -------------------------------------------
//
// LIGHTNING is NOT one of the six types the MSVC layout probe measures
// (effect-props.js), and this project's own file-ownership boundary forbids
// touching tools/layout-probe here — so its fields are derived STRUCTURALLY from
// LIGHTNING_PROPERTY::PROPERTY (DxEffectLighting.h) and CROSS-VALIDATED, not
// blindly hand-computed:
//
//  * the struct is entirely 4-byte members (DWORD/int/float + D3DXCOLOR, which is
//    four floats naturally 4-aligned), so there is no alignment trap: every
//    offset is the running sum of sizes and sizeof(PROPERTY) == 88 + 512 == 600.
//  * the two char[256] fields land at struct-offset 88 (In) and 344 (Out). Added
//    to the MEASURED body prefixes (76 non-affine current 0x0102, 112 affine for
//    0x0100/0x0101) that is body offsets 164/420 and 200/456 — EXACTLY the values
//    effect-egp.js FIELDS already confirmed against the data (test.js pins this).
//
// So the numeric fields below are anchored to the same offsets an independent
// derivation already agreed on. PROPERTY_100 (0x0100) has the same field
// positions (m_dwMaxLenth/m_dwVelocity are DWORD not float, same 4 bytes).
const LGT_OFF = {
  m_dwFlag: 0, m_dwDivision: 8, m_fWidth_In: 12, m_fWidth_Out: 16,
  m_fMaxLenth: 20, m_fAlphaRate1: 28, m_fAlphaRate2: 32,
  m_fAlphaStart: 36, m_fAlphaMid1: 40, m_fAlphaMid2: 44, m_fAlphaEnd: 48,
  m_cColorStart: 52, m_cColorEnd: 68, m_nBlend: 84,
  m_szTexture_In: 88, m_szTexture_Out: 344,
};
const LGT_SIZE = 600;
const LGT_IN = 0x00000001;   // USEIN  (DxEffectLighting.h) — inner beam present
const LGT_OUT = 0x00000002;  // USEOUT — outer glow beam present

// A tree node's own local translation (matLocal _41/_42/_43 = row-major 12/13/14).
// Parent transforms are NOT composed: see the header note. For the common shallow
// effect (max depth 0 or 1 for 78% of files, EFFECTS-SCOPE.md §1) the own offset
// plus the group's vLocal is the placement; deeper nesting is approximated.
function localOffset(d) {
  const m = d.prefix.matLocal;
  return [m[12], m[13], m[14]];
}

// ---- start / mid1 / mid2 / end ramps -----------------------------------------
//
// Sequence, Mesh and Ground all animate alpha (and size, and Ground's height) as
// a PIECEWISE-LINEAR curve with breakpoints at Rate1% and Rate2% of the layer
// lifetime: start -> mid1 over [0,Rate1], mid1 -> mid2 over [Rate1,Rate2],
// mid2 -> end over [Rate2,100] (DxEffectSequence.cpp:99-107, DxEffectGround.cpp:47-68).
// The original extractor kept only start/end, and MEASURED over the corpus that
// collapses ~53% of SEQUENCE layers to alpha 0->0 (start==end==0, peak in the
// mids) — i.e. INVISIBLE. So the mid points are emitted, but only when the curve
// is not already a straight start->end line, to keep the JSON lean.
function isTrivialRamp(v0, m1, m2, v1, r1, r2) {
  // value of the straight start->end line at the two breakpoints
  const t1 = r1 / 100, t2 = r2 / 100;
  const line1 = v0 + (v1 - v0) * t1;
  const line2 = v0 + (v1 - v0) * t2;
  return Math.abs(m1 - line1) <= 0.02 && Math.abs(m2 - line2) <= 0.02;
}
// Returns { m:[mid1,mid2], r:[rate1,rate2] } when the ramp adds information over
// a plain start->end lerp, else null. Rates are clamped to a sane 0..100.
function ramp(v0, m1, m2, v1, r1, r2) {
  r1 = Math.min(100, Math.max(0, r1));
  r2 = Math.min(100, Math.max(0, r2));
  if (isTrivialRamp(v0, m1, m2, v1, r1, r2)) return null;
  return { m: r4v([m1, m2]), r: r4v([r1, r2]) };
}
function attachAlpha(l, p) {
  const rr = ramp(p.m_fAlphaStart, p.m_fAlphaMid1, p.m_fAlphaMid2, p.m_fAlphaEnd,
                  p.m_fAlphaRate1, p.m_fAlphaRate2);
  if (rr) { l.am = rr.m; l.ar = rr.r; }
}
function attachSize(l, p) {
  const rr = ramp(p.m_fSizeStart, p.m_fSizeMid1, p.m_fSizeMid2, p.m_fSizeEnd,
                  p.m_fSizeRate1, p.m_fSizeRate2);
  if (rr) { l.sm = rr.m; l.sr = rr.r; }
}

// ---- MOVEROTATE folding ------------------------------------------------------
//
// MOVEROTATE draws nothing (DxEffectMoveRotate::Render has an empty body); it
// only animates the transform its CHILD subtree inherits: translate by
// m_vVelocity*t and spin by m_fRotateAngel{X,Y,Z}*t (radians/sec). Since the
// mobile player is flat (no per-node matrix composition), the motion is FOLDED
// onto each drawable descendant as mvel/mrot fields. Multiple mover ancestors
// accumulate. APPROXIMATION: velocity is taken in effect-local space (the
// intermediate parent rotations that DxEffectMoveRotate applies before
// translating are not composed), and only the descendants under a mover carry
// it. USEGOTOCENTER (pull-to-origin) is not modelled.
function moverOf(nodes, node) {
  const vel = [0, 0, 0];
  const rot = [0, 0, 0];
  let rand = false;
  let found = false;
  let p = node.parent;
  let guard = 0;
  while (p >= 0 && guard++ < 600) {
    const anc = nodes[p];
    if (anc.type === 'MOVEROTATE') {
      const d = props.decode(anc);
      if (d) {
        const v = d.props.m_vVelocity || [0, 0, 0];
        vel[0] += v[0]; vel[1] += v[1]; vel[2] += v[2];
        rot[0] += d.props.m_fRotateAngelX || 0;
        rot[1] += d.props.m_fRotateAngelY || 0;
        rot[2] += d.props.m_fRotateAngelZ || 0;
        if (((d.props.m_dwFlag || 0) >>> 0) & MOVR.USERANDROTATE) rand = true;
        found = true;
      }
    }
    p = anc.parent;
  }
  if (!found) return null;
  const has = vel.some((x) => x) || rot.some((x) => x) || rand;
  return has ? { vel, rot, rand } : null;
}
function attachMover(l, nodes, node) {
  const m = moverOf(nodes, node);
  if (!m) return false;
  if (m.vel.some((x) => x)) l.mvel = r4v(m.vel);
  if (m.rot.some((x) => x)) l.mrot = r4v(m.rot);
  if (m.rand) l.mrand = 1;
  return true;
}

// ---- LIGHTNING (beam) decode -------------------------------------------------
// Reads the structurally-derived, cross-validated fields (see LGT_OFF above)
// straight off the node body. Returns null if the body is too short.
function decodeLightning(node) {
  const b = node.body;
  const base = node.ver < 0x0102 ? 112 : 76;   // measured affine / non-affine prefixes
  if (b.length < base + LGT_SIZE) return null;
  const f = (o) => b.readFloatLE(base + o);
  const u = (o) => b.readUInt32LE(base + o);
  const i = (o) => b.readInt32LE(base + o);
  const str = (o) => {
    const s = b.subarray(base + o, base + o + 256);
    const z = s.indexOf(0);
    return s.toString('latin1', 0, z === -1 ? 256 : z).trim();
  };
  const matLocal = [];
  for (let k = 0; k < 16; k++) matLocal.push(b.readFloatLE(k * 4));
  return {
    off: [matLocal[12], matLocal[13], matLocal[14]],
    flag: u(LGT_OFF.m_dwFlag) >>> 0,
    div: u(LGT_OFF.m_dwDivision),
    widthIn: f(LGT_OFF.m_fWidth_In),
    widthOut: f(LGT_OFF.m_fWidth_Out),
    maxLen: node.ver < 0x0101 ? u(LGT_OFF.m_fMaxLenth) : f(LGT_OFF.m_fMaxLenth),
    aStart: f(LGT_OFF.m_fAlphaStart), aMid1: f(LGT_OFF.m_fAlphaMid1),
    aMid2: f(LGT_OFF.m_fAlphaMid2), aEnd: f(LGT_OFF.m_fAlphaEnd),
    aRate1: f(LGT_OFF.m_fAlphaRate1), aRate2: f(LGT_OFF.m_fAlphaRate2),
    colStart: [f(52), f(56), f(60), f(64)],
    colEnd: [f(68), f(72), f(76), f(80)],
    blend: i(LGT_OFF.m_nBlend),
    texIn: str(LGT_OFF.m_szTexture_In),
    texOut: str(LGT_OFF.m_szTexture_Out),
    beginTime: b.readFloatLE(base - 8),   // fGBeginTime sits just before PROPERTY
    lifeTime: b.readFloatLE(base - 4),    // fGLifeTime
  };
}

function seqLayer(d) {
  const p = d.props;
  const flag = p.m_dwFlag >>> 0;
  const l = {
    k: 'seq',
    tex: texId(p.m_szTexture),
    cols: p.m_iCol | 0,
    rows: p.m_iRow | 0,
    at: r4(p.m_fAniTime),                    // seconds per flipbook frame
    begin: r4(d.prefix.fGBeginTime),
    life: r4(d.prefix.fGLifeTime),
    size: r4(p.m_fSizeStart),
    sizeEnd: r4(p.m_fSizeEnd),
    a0: r4(p.m_fAlphaStart),
    a1: r4(p.m_fAlphaEnd),
    col: r4v(p.m_cColorStart),
    colEnd: r4v(p.m_cColorEnd),
    blend: p.m_nBlend | 0,
    off: r4v(localOffset(d)),
    bill: (flag & SEQ.USEBILLBOARD) !== 0 || (flag & SEQ.USEBILLBOARDUP) !== 0,
    loop: (flag & SEQ.USESEQUENCELOOP) !== 0,
  };
  attachAlpha(l, p);   // piecewise alpha ramp — the fix for invisible sequences
  attachSize(l, p);    // piecewise size ramp
  return l;
}

function parLayer(d) {
  const p = d.props;
  const flag = p.m_dwFlag >>> 0;
  const l = {
    k: 'par',
    tex: texId(p.m_szTexture),
    cols: p.m_nCol | 0,
    rows: p.m_nRow | 0,
    at: r4(p.m_fAniTime),
    begin: r4(d.prefix.fGBeginTime),
    life: r4(d.prefix.fGLifeTime),           // emitter lifetime
    rate: p.m_uParticlesPerSec >>> 0,
    speed: r4(p.m_fSpeed),
    speedVar: r4(p.m_fSpeedVar),
    plife: r4(p.m_fLife),                    // per-particle life
    plifeVar: r4(p.m_fLifeVar),
    theta: r4(p.m_fTheta),                   // emission cone, degrees
    range: r4v(p.m_vRange),
    size: r4(p.m_fSizeStart),
    sizeEnd: r4(p.m_fSizeEnd),
    grav: r4(p.m_fGravityStart),
    a0: r4(p.m_fAlphaStart),
    a1: r4(p.m_fAlphaEnd),
    col: r4v(p.m_cColorStart),
    colEnd: r4v(p.m_cColorEnd),
    blend: p.m_nBlend | 0,
    off: r4v(localOffset(d)),
    mesh: (flag & PAR.USEMESH) !== 0,        // particle is a mesh, not a sprite
  };
  attachAlpha(l, p);   // PARTICLESYS has an alpha ramp (no size mids)
  return l;
}

// MESH (0x0100..0x0105) -> a textured, camera-facing quad. The engine draws the
// actual .x mesh; a CPU billboard reduces that to its diffuse TEXTURE on a quad,
// with the same size / alpha / colour ramps, flipbook (m_nCol x m_nRow) and a
// spin about the view axis (m_fRotationAngle rad/s). APPROXIMATIONS: the mesh
// geometry itself is not reproduced (quad stand-in); m_vSizeXYZ per-axis growth
// and the m_nRotationType axis choice collapse to a uniform size and a roll.
function meshLayer(d) {
  const p = d.props;
  const flag = p.m_dwFlag >>> 0;
  const scale = (flag & MESH.USESCALE) !== 0;
  const l = {
    k: 'mesh',
    tex: texId(p.m_szTexture),
    cols: p.m_nCol | 0,
    rows: p.m_nRow | 0,
    at: r4(p.m_fAniTime),
    begin: r4(d.prefix.fGBeginTime),
    life: r4(d.prefix.fGLifeTime),
    size: r4(scale ? p.m_fSizeStart : 1),
    sizeEnd: r4(scale ? p.m_fSizeEnd : 1),
    a0: r4(p.m_fAlphaStart),
    a1: r4(p.m_fAlphaEnd),
    col: r4v(p.m_clrStart),                  // MESH uses m_clrStart/End, not m_cColor*
    colEnd: r4v(p.m_clrEnd),
    blend: p.m_nBlend | 0,
    off: r4v(localOffset(d)),
    bill: true,
  };
  if (p.m_fRotationAngle) l.spin = r4(p.m_fRotationAngle);   // rad/sec roll
  attachAlpha(l, p);
  if (scale) attachSize(l, p);
  return l;
}

// GROUND (0x0100..0x0102) -> a horizontal, ground-projected decal quad. Textured,
// with size / alpha / colour ramps, an optional flipbook, a fixed yaw
// (m_fRotateAngel, radians) and a height above the ground that ramps over life
// (m_fHeightStart..End). The engine draws the decal at HALF the authored size
// (DxEffectGround.cpp:57 m_fSizeStart/2), so that /2 is baked in here.
function decalLayer(d) {
  const p = d.props;
  const l = {
    k: 'decal',
    tex: texId(p.m_szTexture),
    cols: p.m_iCol | 0,
    rows: p.m_iRow | 0,
    at: r4(p.m_fAniTime),
    begin: r4(d.prefix.fGBeginTime),
    life: r4(d.prefix.fGLifeTime),
    size: r4(p.m_fSizeStart * 0.5),
    sizeEnd: r4(p.m_fSizeEnd * 0.5),
    a0: r4(p.m_fAlphaStart),
    a1: r4(p.m_fAlphaEnd),
    col: r4v(p.m_cColorStart),
    colEnd: r4v(p.m_cColorEnd),
    blend: p.m_nBlend | 0,
    off: r4v(localOffset(d)),
  };
  if (p.m_fRotateAngel) l.rot = r4(p.m_fRotateAngel);          // fixed yaw, radians
  if (p.m_fHeightStart) l.hgt = r4(p.m_fHeightStart);          // height above ground
  if (p.m_fHeightEnd) l.hgtEnd = r4(p.m_fHeightEnd);
  attachAlpha(l, p);
  // GROUND size ramp mids are halved to match the /2 above.
  const sr = ramp(p.m_fSizeStart * 0.5, p.m_fSizeMid1 * 0.5, p.m_fSizeMid2 * 0.5,
                  p.m_fSizeEnd * 0.5, p.m_fSizeRate1, p.m_fSizeRate2);
  if (sr) { l.sm = sr.m; l.sr = sr.r; }
  return l;
}

// LIGHTNING (0x0100..0x0102) -> a beam: a camera-facing quad chain stretched from
// the node's local offset to the effect origin (the group's m_vGNowPos, which the
// runtime treats as the play position — DxEffectLighting.cpp:346-351). Two textures
// (inner width_in, outer glow width_out) are drawn in the engine; the port renders
// the INNER beam and, when USEOUT is set, notes the outer as a second wider pass.
// APPROXIMATION: the per-frame jitter (m_dwDivision segments randomly perturbed)
// is drawn straight; alpha uses the real 4-point ramp (beams are authored 0->peak->0
// and would be invisible on a 2-point lerp).
function beamLayer(g) {
  const l = {
    k: 'beam',
    tex: texId(g.texIn),
    begin: r4(g.beginTime),
    life: r4(g.lifeTime),
    a0: r4(g.aStart),
    a1: r4(g.aEnd),
    col: r4v(g.colStart),
    colEnd: r4v(g.colEnd),
    blend: g.blend | 0,
    off: r4v(g.off),
    w0: r4(g.widthIn),
    w1: r4(g.widthOut),
    len: r4(g.maxLen),
  };
  const rr = ramp(g.aStart, g.aMid1, g.aMid2, g.aEnd, g.aRate1, g.aRate2);
  if (rr) { l.am = rr.m; l.ar = rr.r; }
  if ((g.flag & LGT_OUT) !== 0) l.outer = 1;   // second, wider glow pass
  return l;
}

// BLURSYS (0x0100..0x0102) -> a motion-trail ribbon: RanEffectBlurSys builds its
// geometry at runtime from a throttled history of the layer's own position, so
// this only needs to carry the authored per-point life/width/colour ramp and
// the texture/blend — no geometry is precomputed here. See
// RanEffectBlurSys.cs's class remarks for exactly what was measured off
// DxEffectBlurSys.cpp and what is approximated.
function trailLayer(d) {
  const p = d.props;
  const flag = p.m_dwFlag >>> 0;
  const l = {
    k: 'trail',
    tex: texId(p.m_szTexture),
    begin: r4(d.prefix.fGBeginTime),
    life: r4(d.prefix.fGLifeTime),
    tnum: p.m_nNum | 0,             // decoded only — see RanEffectBlurSys remarks
    tlife: r4(p.m_fLife),
    tlen0: r4(p.m_fLengthStart),
    tlen1: r4(p.m_fLengthEnd),
    a0: r4(p.m_fAlphaStart),
    a1: r4(p.m_fAlphaEnd),
    col: r4v(p.m_cColorStart),
    colEnd: r4v(p.m_cColorEnd),
    blend: p.m_nBlend | 0,
    off: r4v(localOffset(d)),
    tabs: (flag & BLUR.USEABSOLUTE) !== 0,
  };
  return l;
}

// ---- compaction --------------------------------------------------------------
// Drop keys whose value is the RUNTIME default, so a 6.9 MB JSON does not have to
// carry (cols:1, rows:1, at:0, off:[0,0,0], bill:false, colEnd==col) on tens of
// thousands of layers. RanEffectDb reconstructs the defaults: a missing array is
// null (ToVec3 -> zero, Color -> white), cols/rows<1 mean 1 frame, and colEnd ==
// null falls back to col (Color1 => Color0). Only unambiguous defaults are dropped.
const WHITE = (a) => a && a.length >= 4 && a[0] === 1 && a[1] === 1 && a[2] === 1 && a[3] === 1;
const ZERO3 = (a) => a && a.every((x) => x === 0);
const SAME = (a, b) => a && b && a.length === b.length && a.every((x, i) => x === b[i]);
function compact(l) {
  if (l.cols <= 1) delete l.cols;
  if (l.rows <= 1) delete l.rows;
  if (l.at === 0) delete l.at;
  if (l.blend === 0) delete l.blend;
  if (ZERO3(l.off)) delete l.off;
  if (l.bill === false) delete l.bill;
  if (l.loop === false) delete l.loop;
  if (l.mesh === false) delete l.mesh;
  if (l.tabs === false) delete l.tabs;
  if (l.a0 === 0) delete l.a0;
  if (l.a1 === 0) delete l.a1;
  if (SAME(l.colEnd, l.col)) delete l.colEnd;   // Color1 falls back to Color0
  if (WHITE(l.col)) delete l.col;               // ToColor(null) -> white
  if (WHITE(l.colEnd)) delete l.colEnd;
  if (ZERO3(l.range)) delete l.range;
  return l;
}

function run(opts = {}) {
  const ar = new RccArchive(EFFECT_RCC);
  const effects = {};
  const texUse = new Map();          // texId -> node count
  const stat = {
    files: 0, parsed: 0, emitted: 0, nodesSeq: 0, nodesPar: 0,
    nodesMesh: 0, nodesDecal: 0, nodesBeam: 0, nodesTrail: 0,
    skipTypes: new Map(), meshParticle: 0,
    moverNodes: 0, moverFolded: 0, meshNoTex: 0, trailNoTex: 0,
  };

  for (const e of ar.entries) {
    if (!e.name.toLowerCase().endsWith('.egp')) continue;
    stat.files++;
    let res;
    try { res = egp.parse(ar.read(e), e.name); } catch { continue; }
    // A file that desyncs mid-walk still reports the nodes it read; use them.
    if (!res.nodes || res.nodes.length === 0) continue;
    stat.parsed++;

    const nodes = res.nodes;
    const layers = [];
    let dur = 0;
    // Fold MOVEROTATE motion onto whatever drawable layer we just built.
    const fold = (l, n) => { if (attachMover(l, nodes, n)) stat.moverFolded++; };
    for (const n of nodes) {
      if (n.type === 'MOVEROTATE') stat.moverNodes++;
      if (n.type === 'SEQUENCE') {
        const d = props.decode(n);
        if (!d) { continue; }
        const l = seqLayer(d);
        fold(l, n);
        layers.push(compact(l));
        stat.nodesSeq++;
        dur = Math.max(dur, l.begin + l.life);
        if (l.tex) texUse.set(l.tex, (texUse.get(l.tex) || 0) + 1);
      } else if (n.type === 'PARTICLESYS') {
        const d = props.decode(n);
        if (!d) { continue; }
        const l = parLayer(d);
        fold(l, n);
        layers.push(compact(l));
        stat.nodesPar++;
        if (l.mesh) stat.meshParticle++;
        dur = Math.max(dur, l.begin + l.life + l.plife);
        if (l.tex && !l.mesh) texUse.set(l.tex, (texUse.get(l.tex) || 0) + 1);
      } else if (n.type === 'MESH') {
        const d = props.decode(n);
        if (!d) { continue; }
        const l = meshLayer(d);
        if (!l.tex) { stat.meshNoTex++; continue; }   // no diffuse -> nothing to billboard
        fold(l, n);
        layers.push(compact(l));
        stat.nodesMesh++;
        dur = Math.max(dur, l.begin + l.life);
        texUse.set(l.tex, (texUse.get(l.tex) || 0) + 1);
      } else if (n.type === 'GROUND') {
        const d = props.decode(n);
        if (!d) { continue; }
        const l = decalLayer(d);
        if (!l.tex) { continue; }
        fold(l, n);
        layers.push(compact(l));
        stat.nodesDecal++;
        dur = Math.max(dur, l.begin + l.life);
        texUse.set(l.tex, (texUse.get(l.tex) || 0) + 1);
      } else if (n.type === 'LIGHTNING') {
        const g = decodeLightning(n);
        if (!g || !g.texIn) { stat.skipTypes.set(n.type, (stat.skipTypes.get(n.type) || 0) + 1); continue; }
        const l = beamLayer(g);
        fold(l, n);
        layers.push(compact(l));
        stat.nodesBeam++;
        dur = Math.max(dur, l.begin + l.life);
        texUse.set(l.tex, (texUse.get(l.tex) || 0) + 1);
      } else if (n.type === 'BLURSYS') {
        const d = props.decode(n);
        if (!d) { continue; }
        const l = trailLayer(d);
        if (!l.tex) { stat.trailNoTex++; continue; }   // no texture -> nothing to draw
        fold(l, n);
        layers.push(compact(l));
        stat.nodesTrail++;
        dur = Math.max(dur, l.begin + l.life);
        texUse.set(l.tex, (texUse.get(l.tex) || 0) + 1);
      } else if (n.type === 'MOVEROTATE') {
        // Folded onto descendant drawables above — neither a standalone layer
        // nor "skipped".
      } else {
        stat.skipTypes.set(n.type, (stat.skipTypes.get(n.type) || 0) + 1);
      }
    }
    if (layers.length === 0) continue;

    const id = path.basename(e.name).toLowerCase().replace(/\.egp$/, '');
    const g = res.group || {};
    effects[id] = {
      ver: res.ver,
      flag: (g.flag || 0) >>> 0,
      local: g.vLocal ? r4v(g.vLocal) : [0, 0, 0],
      dur: r4(dur),
      layers,
    };
    stat.emitted++;
  }

  const out = {
    format: 'ran-effects',
    version: 1,
    note: 'Simplified effect layers for the CPU billboard player: seq(flipbook), '
        + 'par(emitter), mesh(textured-quad stand-in), decal(GROUND horizontal quad), '
        + 'beam(LIGHTNING quad chain), trail(BLURSYS motion-trail ribbon, geometry '
        + 'built at runtime by RanEffectBlurSys from a position history). MOVEROTATE '
        + 'draws nothing in the engine and is folded onto descendant layers as '
        + 'mvel/mrot. The rare types (POINTLIGHT/WAVE/DECAL[0x06]/SKINMESH/CAMERA/'
        + 'MOVETARGET) are still skipped; see extract-effects.js for the full .reff set.',
    count: stat.emitted,
    textureCount: texUse.size,
    effects,
  };
  return { out, stat, texUse };
}

if (require.main === module) main();

function main() {
  const argv = process.argv.slice(2);
  const dry = argv.includes('--dry');
  const outArg = argv.indexOf('--out');
  const outPath = outArg >= 0 ? argv[outArg + 1] : OUT_DEFAULT;

  const { out, stat, texUse } = run();
  const json = JSON.stringify(out);
  const fmt = (n) => n.toLocaleString('en-US');

  console.log('== extract-effects-json ==');
  console.log(`.egp files              ${fmt(stat.files)}`);
  console.log(`  with >=1 node parsed  ${fmt(stat.parsed)}`);
  console.log(`effects emitted         ${fmt(stat.emitted)} (>=1 drawable layer)`);
  console.log(`  SEQUENCE layers       ${fmt(stat.nodesSeq)}`);
  console.log(`  PARTICLESYS layers    ${fmt(stat.nodesPar)}  (${fmt(stat.meshParticle)} are mesh-particles, no sprite)`);
  console.log(`  MESH layers           ${fmt(stat.nodesMesh)}  (${fmt(stat.meshNoTex)} skipped, no diffuse texture)`);
  console.log(`  GROUND decal layers   ${fmt(stat.nodesDecal)}`);
  console.log(`  LIGHTNING beam layers ${fmt(stat.nodesBeam)}`);
  console.log(`  BLURSYS trail layers  ${fmt(stat.nodesTrail)}  (${fmt(stat.trailNoTex)} skipped, no texture)`);
  console.log(`  MOVEROTATE nodes      ${fmt(stat.moverNodes)}  (folded onto ${fmt(stat.moverFolded)} drawable layers)`);
  console.log(`distinct textures used  ${fmt(texUse.size)}`);
  const skip = [...stat.skipTypes].sort((a, b) => b[1] - a[1])
    .map(([t, n]) => `${t}:${fmt(n)}`).join('  ');
  console.log(`skipped node types      ${skip}`);
  console.log(`effects.json size       ${fmt(json.length)} bytes (${(json.length / 1048576).toFixed(2)} MB)`);

  if (dry) { console.log('\n--dry: nothing written'); return; }
  fs.mkdirSync(path.dirname(outPath), { recursive: true });
  fs.writeFileSync(outPath, json);
  console.log(`\nwrote ${outPath}`);
  // Emit the texture list next to the tool so stage-effect-textures.js does not
  // have to re-walk 4,294 files just to learn which textures to stage.
  const texList = [...texUse.keys()].sort();
  fs.writeFileSync(path.join(__dirname, 'effect-textures.list'),
    texList.join('\n') + '\n');
  console.log(`wrote effect-textures.list (${fmt(texList.length)} names)`);
}

module.exports = {
  run, texId, seqLayer, parLayer, meshLayer, decalLayer, beamLayer, trailLayer,
  decodeLightning, moverOf, ramp, LGT_OFF, LGT_SIZE,
};
