'use strict';
//
// `.egp` property-node FIELD decoding, for the six types that carry 95.5% of
// all 34,609 shipped property nodes (EFFECTS-SCOPE.md §3):
//
//   SEQUENCE  MESH  PARTICLESYS  MOVEROTATE  GROUND  BLURSYS
//
// `effect-egp.js` walks the tree and hands each node a `body` — the blob a
// `*_PROPERTY::LoadFile` blits. This turns that blob into named fields.
//
// EVERY offset and size here comes from the layout probe. None is computed in
// this file, and none is written down as a literal:
//
//   MOBILE/tools/layout-probe/gen-effectprops.js  ->  effectprops.gen.inc
//                                                 ->  effectprops.types.json
//   MOBILE/tools/layout-probe/build-structs.js    ->  layout.json .effectProps
//
// The probe measures `sizeof`/`offsetof` with MSVC over the real headers; the
// generator additionally records each member's declared TYPE SPELLING, and this
// file refuses to decode when a spelling and the measured size disagree. That
// pairing is what makes the decode safe: `float` must be 4 bytes, `D3DXVECTOR3`
// 12, `D3DXCOLOR` 16, `char[256]` 256.
//
// Why it matters here specifically: five of these six structs end with (or
// contain) `char[256]` filename fields, and EFFECTS-SCOPE.md §5 already
// documented that a free regex over the body invents names — a short string
// written over a longer one leaves the old tail alive after the NUL. Four bytes
// of drift in the struct prefix produces names that look almost right. So the
// test is not "does it parse" but "do the names resolve against the shipped
// texture and mesh sets", which `main()` below measures over all 4,294 files.
//
// The node body layout, from every LoadFile in DxEffect/Single/:
//
//   current version : [D3DXMATRIX m_matLocal][BOOL m_bMoveObj][float][float][PROPERTY]
//   older versions  : [D3DXMATRIX m_matLocal][DXAFFINEPARTS][BOOL][float][float][PROPERTY]
//
// (the affine parts are decomposed back into m_matLocal by D3DXMatrixCompX, so
// on an older node the matrix and the affine parts are two spellings of the
// same transform.) Both prefix sizes are measured — `EFFPROP_PREFIX` 76 and
// `EFFPROP_PREFIX_AFFINE` 112 — never written as literals.
//
const fs = require('fs');
const path = require('path');

const PROBE = path.join(__dirname, '..', 'layout-probe');
const LAYOUT = require(path.join(PROBE, 'layout.json'));
const TYPES = require(path.join(PROBE, 'effectprops.types.json')).types;

const L = LAYOUT.effectProps;
if (!L) {
  throw new Error('layout.json has no effectProps — run `node build-structs.js` in tools/layout-probe');
}

const PREFIX = L.EFFPROP_PREFIX.size;
const PREFIX_AFFINE = L.EFFPROP_PREFIX_AFFINE.size;

// ---- which struct each (type, version) blits --------------------------------
//
// Read straight off the version dispatch in each LoadFile. `affine` says the
// branch reads a DXAFFINEPARTS between the matrix and m_bMoveObj, which is the
// same fact effect-egp.js's sizeDelta already depends on.
//
//   SEQUENCE     DxEffectSequence.cpp:159      0x0102 | 0x0101 | 0x0100
//   MESH         DxEffectMeshPROP.cpp:417      0x0105 | 0x0104 | 0x0103..0x0100
//   PARTICLESYS  DxEffectParticleSysPROP.cpp:646  0x0107 | 0x0106 | 0x0105 | 0x0104
//   MOVEROTATE   DxEffectMoveRotate.cpp:84     0x0103 | 0x0102 | 0x0101 | 0x0100
//   GROUND       DxEffectGround.cpp:133        0x0102 | 0x0101 | 0x0100
//   BLURSYS      DxEffectBlurSys.cpp:92        0x0102 | 0x0101 | 0x0100
//
// The `EFFLEGACY_*` structs are the ones declared at file scope in
// DxEffectMeshPROP.cpp / DxEffectParticleSysPROP.cpp, which no header exposes;
// gen-effectprops.js copies them verbatim into effectprops.legacy.gen.h so the
// probe can measure them. PARTICLESYS 0x0105 and 0x0106 read NO DXAFFINEPARTS
// while 0x0104 and below do — the one place this type breaks the house pattern,
// and the same fact effect-egp.js's NO_AFFINE_OLD already encodes.
const LAYOUTS = {
  'SEQUENCE 0x0102': { struct: 'SEQUENCE_PROPERTY::PROPERTY', affine: false },
  'SEQUENCE 0x0101': { struct: 'SEQUENCE_PROPERTY::PROPERTY', affine: true },
  'SEQUENCE 0x0100': { struct: 'SEQUENCE_PROPERTY::PROPERTY_100', affine: true },
  'MESH 0x0105': { struct: 'MESH_PROPERTY::PROPERTY', affine: false },
  'MESH 0x0104': { struct: 'MESH_PROPERTY::PROPERTY', affine: true },
  'MESH 0x0103': { struct: 'EFFLEGACY_MESH::PROPERTY_103', affine: true },
  'MESH 0x0102': { struct: 'EFFLEGACY_MESH::PROPERTY_102', affine: true },
  'MESH 0x0101': { struct: 'EFFLEGACY_MESH::PROPERTY_101', affine: true },
  'MESH 0x0100': { struct: 'EFFLEGACY_MESH::PROPERTY_100', affine: true },
  'PARTICLESYS 0x0107': { struct: 'PARTICLESYS_PROPERTY::PROPERTY', affine: false },
  'PARTICLESYS 0x0106': { struct: 'EFFLEGACY_PARTICLESYS::PROPERTY_106', affine: false },
  'PARTICLESYS 0x0105': { struct: 'EFFLEGACY_PARTICLESYS::PROPERTY_105', affine: false },
  'PARTICLESYS 0x0104': { struct: 'EFFLEGACY_PARTICLESYS::PROPERTY_104', affine: true },
  'PARTICLESYS 0x0103': { struct: 'EFFLEGACY_PARTICLESYS::PROPERTY_102_103', affine: true },
  'PARTICLESYS 0x0102': { struct: 'EFFLEGACY_PARTICLESYS::PROPERTY_102_103', affine: true },
  'PARTICLESYS 0x0101': { struct: 'EFFLEGACY_PARTICLESYS::PROPERTY_101', affine: true },
  'PARTICLESYS 0x0100': { struct: 'EFFLEGACY_PARTICLESYS::PROPERTY_100', affine: true },
  'MOVEROTATE 0x0103': { struct: 'MOVEROTATE_PROPERTY::PROPERTY', affine: false },
  'MOVEROTATE 0x0102': { struct: 'MOVEROTATE_PROPERTY::PROPERTY', affine: true },
  'MOVEROTATE 0x0101': { struct: 'MOVEROTATE_PROPERTY::PROPERTY_101', affine: true },
  'MOVEROTATE 0x0100': { struct: 'MOVEROTATE_PROPERTY::PROPERTY_100', affine: true },
  'GROUND 0x0102': { struct: 'GROUND_PROPERTY::PROPERTY', affine: false },
  'GROUND 0x0101': { struct: 'GROUND_PROPERTY::PROPERTY', affine: true },
  'GROUND 0x0100': { struct: 'GROUND_PROPERTY::PROPERTY_100', affine: true },
  'BLURSYS 0x0102': { struct: 'BLURSYS_PROPERTY::PROPERTY', affine: false },
  'BLURSYS 0x0101': { struct: 'BLURSYS_PROPERTY::PROPERTY', affine: true },
  'BLURSYS 0x0100': { struct: 'BLURSYS_PROPERTY::PROPERTY_100', affine: true },
  // Not one of the six. Measured because sk_dfly.egp — one of the five files
  // EFFECTS-SCOPE.md §8 could not explain — contains one, and with the other
  // types measured it was the only node left whose size was still a guess.
  'POINTLIGHT 0x0101': { struct: 'POINTLIGHT_PROPERTY::PROPERTY', affine: false },
  'POINTLIGHT 0x0100': { struct: 'POINTLIGHT_PROPERTY::PROPERTY', affine: true },
};

const verKey = (type, ver) => `${type} 0x${ver.toString(16).padStart(4, '0')}`;

/** The body size this (type, version) must have, entirely from measured sizes. */
function expectedBodySize(type, ver) {
  const spec = LAYOUTS[verKey(type, ver)];
  if (!spec) return null;
  return (spec.affine ? PREFIX_AFFINE : PREFIX) + L[spec.struct].size;
}

// ---- reading one field ------------------------------------------------------
//
// Dispatch is on the declared type spelling, and the measured size must agree
// with it or the field is refused. Nothing infers a type from a size.
const SPELLING = {
  float: { n: 1, read: 'f32' },
  int: { n: 1, read: 'i32' },
  DWORD: { n: 1, read: 'u32' },
  BOOL: { n: 1, read: 'i32' },
  D3DXVECTOR2: { n: 2, read: 'f32' },
  D3DXVECTOR3: { n: 3, read: 'f32' },
  D3DXVECTOR4: { n: 4, read: 'f32' },
  D3DXCOLOR: { n: 4, read: 'f32' },
  'float[4]': { n: 4, read: 'f32' },
};

function fieldSpec(struct, name) {
  const decl = TYPES[struct] && TYPES[struct][name];
  const meas = L[struct].fields[name];
  if (!decl || !meas) return null;
  if (/^char\[/.test(decl)) {
    return { kind: 'string', off: meas.off, size: meas.size };
  }
  const s = SPELLING[decl];
  if (!s) throw new Error(`${struct}.${name}: unhandled declared type "${decl}"`);
  const unit = s.read === 'f32' ? 4 : 4;
  if (s.n * unit !== meas.size) {
    // The compiler and the declaration disagree — refuse rather than guess.
    throw new Error(`${struct}.${name}: "${decl}" but the probe measured ${meas.size} bytes`);
  }
  return { kind: s.read, n: s.n, off: meas.off, size: meas.size };
}

// Cached per struct so the whole corpus costs one pass over the tables.
const SPECS = new Map();
function specsFor(struct) {
  if (SPECS.has(struct)) return SPECS.get(struct);
  const out = [];
  for (const name of Object.keys(L[struct].fields)) {
    const s = fieldSpec(struct, name);
    if (s) out.push({ name, ...s });
  }
  SPECS.set(struct, out);
  return out;
}

/** NUL-terminated fixed-width name, plus whether the tail after the NUL is dirty. */
function readString(buf, off, size) {
  const s = buf.subarray(off, off + size);
  const nul = s.indexOf(0);
  if (nul === -1) return { value: s.toString('latin1'), terminated: false, tail: false };
  const value = s.toString('latin1', 0, nul);
  // A shorter name written over a longer one leaves the previous tail intact.
  // That debris is exactly what a regex over the body picks up as a filename.
  let tail = false;
  for (let i = nul + 1; i < size; i++) if (s[i] !== 0) { tail = true; break; }
  return { value, terminated: true, tail };
}

/**
 * Decode one node from effect-egp.js.
 * @returns {null|{struct, version, affine, prefix:{...}, props:{...}}}
 *          null when this (type, version) has no measured layout.
 */
function decode(node) {
  const spec = LAYOUTS[verKey(node.type, node.ver)];
  if (!spec) return null;
  const base = spec.affine ? PREFIX_AFFINE : PREFIX;
  const b = node.body;
  const want = base + L[spec.struct].size;
  if (b.length < want) return null;

  const matLocal = [];
  for (let i = 0; i < 16; i++) matLocal.push(b.readFloatLE(i * 4));
  const affineAt = 16 * 4;
  const flagsAt = spec.affine ? affineAt + (PREFIX_AFFINE - PREFIX) : affineAt;
  const prefix = {
    matLocal,
    bMoveObj: b.readInt32LE(flagsAt),
    fGBeginTime: b.readFloatLE(flagsAt + 4),
    fGLifeTime: b.readFloatLE(flagsAt + 8),
  };

  const props = {};
  for (const f of specsFor(spec.struct)) {
    const at = base + f.off;
    if (f.kind === 'string') {
      const s = readString(b, at, f.size);
      props[f.name] = s.value;
      props[`${f.name}$`] = { terminated: s.terminated, tail: s.tail };
    } else if (f.n === 1) {
      props[f.name] = f.kind === 'f32' ? b.readFloatLE(at)
        : f.kind === 'i32' ? b.readInt32LE(at) : b.readUInt32LE(at);
    } else {
      const v = [];
      for (let i = 0; i < f.n; i++) v.push(b.readFloatLE(at + i * 4));
      props[f.name] = v;
    }
  }
  return { struct: spec.struct, version: node.ver, affine: spec.affine, prefix, props };
}

// ---- flag bits --------------------------------------------------------------
//
// The USE* masks are `#define`s, so gen-enums.js (which only reads namespace-
// scope enums) cannot reach them. They are extracted here as LITERALS from the
// headers — the value is copied, never computed — and comments are stripped
// first, because this codebase's single most repeated bug is a constant read
// out of a commented-out line (README: NET_MSG_LOBBY 2005 vs 1942). Two of
// these are in fact commented out — MESH's USEMATRIAL/USELIGHTING and
// SEQUENCE's USEBILLDIRECT — and must not appear in the mask.
const SRC_SINGLE = path.join(__dirname, '..', '..', '..', 'SOURCE',
                             'Lib_Engine', 'DxEffect', 'Single');
const FLAG_HEADER = {
  SEQUENCE: 'DxEffectSequence.h',
  MESH: 'DxEffectMesh.h',
  PARTICLESYS: 'DxEffectParticleSys.h',
  MOVEROTATE: 'DxEffectMoveRotate.h',
  GROUND: 'DxEffectGround.h',
  BLURSYS: 'DxEffectBlurSys.h',
};

function stripComments(src) {
  return src.replace(/\/\*[\s\S]*?\*\//g, ' ').replace(/\/\/[^\n]*/g, '');
}

const FLAGS = {};
for (const [type, file] of Object.entries(FLAG_HEADER)) {
  const p = path.join(SRC_SINGLE, file);
  if (!fs.existsSync(p)) continue;
  const src = stripComments(fs.readFileSync(p, 'latin1'));
  const bits = {};
  const re = /^[ \t]*#define[ \t]+([A-Z][A-Z0-9_]*)[ \t]+(0x[0-9a-fA-F]{8})[ \t]*$/gm;
  let m;
  while ((m = re.exec(src)) !== null) bits[m[1]] = parseInt(m[2], 16) >>> 0;
  FLAGS[type] = bits;
}

/** Named flags of a node, plus any bit no #define in its header accounts for. */
function flagNames(type, dw) {
  const bits = FLAGS[type] || {};
  const on = [];
  let known = 0;
  // `>>> 0` on both sides: USENEWEFF_END is 0x80000000, and JS bitwise AND
  // yields a SIGNED 32-bit result, so `(dw & v) === v` is false for that bit
  // however it is set. The first run of this file had exactly that bug and
  // silently reported the flag as never used.
  for (const [n, v] of Object.entries(bits)) {
    known |= v;
    if (v !== 0 && ((dw & v) >>> 0) === (v >>> 0)) on.push(n);
  }
  return { on, unknown: (dw & ~known) >>> 0 };
}

// ---- enumerated fields ------------------------------------------------------
//
// The domains are the `case` labels of the render-state switch that consumes
// each field. Anything outside falls through to the default state, so a value
// off the list is not a crash — it is a silent visual difference, which is
// precisely why it is worth counting.
//
//   SEQUENCE   m_nBlend  DxEffectSequence.cpp:1010        1,2,3,5
//   GROUND     m_nBlend  DxEffectGround.cpp:689           1,2,3,5
//   BLURSYS    m_nBlend  DxEffectBlurSys.cpp:587          1,2,3,5
//   MESH       m_nBlend  DxEffectMesh.cpp:545             0..6
//   MESH       m_nPower  DxEffectMesh.cpp:597             0,1,2
//   MESH       m_nRotationType DxEffectMesh.cpp:472       0..6
//   PARTICLESYS m_nBlend DxEffectParticleSysDraw.cpp:144  0..6
//   PARTICLESYS m_nPower DxEffectParticleSysDraw.cpp:195  0,1,2
//   PARTICLESYS m_iCenterPoint DxEffectParticleSysDraw.cpp:663  0,1,2
//
// `range` is the authored range the editor can produce, which is WIDER than the
// case list for the three types whose switch has a hole at 4: value 4 is a real
// authored value that simply takes the default alpha blend. Counting it as
// invalid would report a layout bug that does not exist, so validity is
// measured against `range` and the off-case values are reported separately.
const ENUMS = {
  SEQUENCE: { m_nBlend: { cases: [1, 2, 3, 5], range: [0, 6] } },
  GROUND: { m_nBlend: { cases: [1, 2, 3, 5], range: [0, 6] } },
  BLURSYS: { m_nBlend: { cases: [1, 2, 3, 5], range: [0, 6] } },
  MESH: {
    m_nBlend: { cases: [0, 1, 2, 3, 4, 5, 6], range: [0, 6] },
    m_nPower: { cases: [0, 1, 2], range: [0, 2] },
    m_nRotationType: { cases: [0, 1, 2, 3, 4, 5, 6], range: [0, 6] },
  },
  PARTICLESYS: {
    m_nBlend: { cases: [0, 1, 2, 3, 4, 5, 6], range: [0, 6] },
    m_nPower: { cases: [0, 1, 2], range: [0, 2] },
    m_iCenterPoint: { cases: [0, 1, 2], range: [0, 2] },
  },
};

// Fields the writer stores and the runtime never reads. Two flavours, both
// found by grepping the whole of SOURCE for the member name:
//
//  * DEAD_UNINIT — never read AND never initialised by the PROPERTY
//    constructor, so what lands on disk is whatever was on the stack/heap.
//    SEQUENCE's m_bTexRotateUse and m_bGIsColliding are assigned only in the
//    0x0100 upgrade path; MESH's m_fMaterial[4]/m_fMaterialRatio0/1 appear in
//    no expression anywhere in DxEffect/. Their values are garbage BY
//    CONSTRUCTION, and an importer must ignore them rather than "fix" them.
//    (The live equivalents are the flag bits USETEXROTATE and USECOLLISION.)
//  * DEAD — never read, but the constructor does initialise it, so the bytes
//    are stable and meaningless rather than random.
const DEAD_UNINIT = new Set([
  'SEQUENCE.m_bTexRotateUse', 'SEQUENCE.m_bGIsColliding',
  'MESH.m_fMaterial', 'MESH.m_fMaterialRatio0', 'MESH.m_fMaterialRatio1',
]);
const DEAD = new Set(['MESH.m_nTexRotateType', 'PARTICLESYS.m_dwDummy']);

// Which asset set each name field must resolve against. Routed by field, not by
// extension: `m_szEffFile` naming a `.egp` and `m_szTexture` naming a `.tga`
// are different questions and merging them hid a real result once already
// (README, "Items name much more than textures").
const NAME_KIND = {
  m_szTexture: 'texture',
  m_szMeshFile: 'mesh',
  m_szMeshFile1: 'mesh',
  m_szMeshFile2: 'mesh',
  m_szEffFile: 'effect',
};

module.exports = {
  LAYOUTS, PREFIX, PREFIX_AFFINE, FLAGS, ENUMS, NAME_KIND, DEAD, DEAD_UNINIT,
  decode, expectedBodySize, flagNames, verKey, specsFor,
};

// =============================================================================
// Validation run: `node effect-props.js [--verbose]`
// =============================================================================
if (require.main === module) main();

function main() {
  const { RccArchive } = require('./rcc');
  const E = require('./effect-egp');
  const verbose = process.argv.includes('--verbose');
  const RAN = path.join(__dirname, '..', '..', '..', 'Ran');

  // ---- what ships, indexed the way the engine looks names up ---------------
  // Case-insensitive, basename only, with an extension-agnostic fallback pass:
  // the engine loads through D3DX, which sniffs the header rather than trusting
  // the name, so `eff08.dds` legitimately resolves to `eff08.tga`
  // (audit-assets.js, README "Texture reference audit").
  const TEXTURE_EXT = new Set(['.dds', '.tga', '.png', '.bmp', '.jpg', '.jpeg', '.psd']);
  const MESH_EXT = new Set(['.x', '.chf', '.abf']);
  const shipped = { texture: new Set(), mesh: new Set(), effect: new Set(), any: new Set() };
  const stems = { texture: new Set(), mesh: new Set(), effect: new Set() };
  const addShipped = (name) => {
    const k = path.basename(String(name)).toLowerCase();
    const ext = path.extname(k);
    const stem = k.replace(/\.[^.]*$/, '');
    shipped.any.add(k);
    if (TEXTURE_EXT.has(ext)) { shipped.texture.add(k); stems.texture.add(stem); }
    if (MESH_EXT.has(ext)) { shipped.mesh.add(k); stems.mesh.add(stem); }
    if (ext === '.egp') { shipped.effect.add(k); stems.effect.add(stem); }
  };
  (function walk(dir) {
    let items;
    try { items = fs.readdirSync(dir, { withFileTypes: true }); } catch { return; }
    for (const it of items) {
      const p = path.join(dir, it.name);
      if (it.isDirectory()) {
        if (it.name.toLowerCase() === 'ranmapziptemp') continue;   // stale per-pid copies
        walk(p);
      } else if (it.name.toLowerCase().endsWith('.rcc')) {
        let ar;
        try { ar = new RccArchive(p); } catch { continue; }
        for (const e of ar.entries) addShipped(e.name);
      } else {
        addShipped(it.name);
      }
    }
  })(RAN);

  const resolve = (kind, name) => {
    const k = path.basename(name).toLowerCase();
    if (shipped[kind].has(k)) return 'exact';
    if (stems[kind].has(k.replace(/\.[^.]*$/, ''))) return 'stem';
    return null;
  };

  // ---- parse every shipped .egp -------------------------------------------
  const ar = new RccArchive(path.join(RAN, 'data/effect/Effect.rcc'));
  const nodes = [];
  let files = 0;
  for (const e of ar.entries) {
    if (!e.name.toLowerCase().endsWith('.egp')) continue;
    files++;
    const r = E.parse(ar.read(e), e.name);
    for (const n of r.nodes || []) { n.file = e.name; n.fileOk = r.ok; nodes.push(n); }
  }

  const SIX = ['SEQUENCE', 'MESH', 'PARTICLESYS', 'MOVEROTATE', 'GROUND', 'BLURSYS'];
  const fmt = (n) => n.toLocaleString('en-US');
  const pct = (a, b) => (b ? `${(100 * a / b).toFixed(2)}%` : '—');

  console.log(`== effect PROPERTY field validation ==`);
  console.log(`${fmt(files)} .egp, ${fmt(nodes.length)} property nodes\n`);

  // ---- (1) the body size check --------------------------------------------
  // The strongest single check available: the size the file declares for the
  // node must equal (measured prefix) + (measured sizeof PROPERTY). It is a
  // whole-struct assertion against real bytes, and it cannot pass by luck —
  // a 4-byte error in either term fails on every node of the type.
  console.log('-- (1) declared node size == measured prefix + sizeof(PROPERTY) --');
  const sizeStat = new Map();
  for (const n of nodes) {
    const key = verKey(n.type, n.ver);
    const want = expectedBodySize(n.type, n.ver);
    if (want === null) continue;
    if (!sizeStat.has(key)) sizeStat.set(key, { ok: 0, bad: 0, got: new Set() });
    const s = sizeStat.get(key);
    if (n.body.length === want) s.ok++;
    else { s.bad++; s.got.add(n.body.length); }
  }
  console.log(`${'type / version'.padEnd(22)} ${'nodes'.padStart(7)} ${'expect'.padStart(7)} ${'match'.padStart(7)}  mismatched sizes`);
  let sizeOk = 0;
  let sizeAll = 0;
  for (const [k, s] of [...sizeStat].sort((a, b) => (b[1].ok + b[1].bad) - (a[1].ok + a[1].bad))) {
    const [t, v] = k.split(' ');
    sizeOk += s.ok; sizeAll += s.ok + s.bad;
    console.log(`${k.padEnd(22)} ${String(s.ok + s.bad).padStart(7)} ` +
                `${String(expectedBodySize(t, parseInt(v, 16))).padStart(7)} ` +
                `${pct(s.ok, s.ok + s.bad).padStart(7)}  ${[...s.got].join(',') || '-'}`);
  }
  console.log(`TOTAL ${fmt(sizeOk)}/${fmt(sizeAll)} = ${pct(sizeOk, sizeAll)}`);

  // ---- (1b) what the measured size FIXES -----------------------------------
  // Every mismatch above is exactly 4 bytes SHORT, and they cluster in the
  // handful of files EFFECTS-SCOPE.md §8 could not explain. If the measured
  // size is the truth and the declared one is the bug, re-walking with the
  // measured size must recover those files to an exact EOF. That is a
  // falsifiable prediction, so it is run rather than argued.
  const hook = {
    bodySize: (t, v) => {
      const w = expectedBodySize(t, v);
      return w === null ? null : w;   // null = keep the declared size
    },
  };
  let eofDeclared = 0;
  let eofMeasured = 0;
  const flipped = [];
  for (const e of ar.entries) {
    if (!e.name.toLowerCase().endsWith('.egp')) continue;
    const buf = ar.read(e);
    const a = E.parse(buf, e.name);
    const b = E.parse(buf, e.name, hook);
    if (a.ok) eofDeclared++;
    if (b.ok) eofMeasured++;
    if (a.ok !== b.ok) flipped.push(`${e.name} ${a.ok ? 'ok' : 'FAIL'} -> ${b.ok ? 'ok' : 'FAIL'}  (${a.error || b.error})`);
  }
  console.log(`\nwalk to the exact last byte: ${fmt(eofDeclared)} files with the DECLARED size, ` +
              `${fmt(eofMeasured)} with the MEASURED one (of ${fmt(files)})`);
  for (const f of flipped) console.log(`   ${f}`);
  console.log();

  // ---- (2) coverage --------------------------------------------------------
  console.log('-- (2) layout coverage of the six types --');
  const cover = new Map();
  for (const n of nodes) {
    if (!SIX.includes(n.type)) continue;
    const c = cover.get(n.type) || { total: 0, mapped: 0, vers: new Map() };
    c.total++;
    if (LAYOUTS[verKey(n.type, n.ver)]) c.mapped++;
    else c.vers.set(n.ver, (c.vers.get(n.ver) || 0) + 1);
    cover.set(n.type, c);
  }
  let covM = 0;
  let covT = 0;
  for (const t of SIX) {
    const c = cover.get(t);
    if (!c) continue;
    covM += c.mapped; covT += c.total;
    const un = [...c.vers].map(([v, n]) => `0x${v.toString(16).padStart(4, '0')}:${n}`).join(' ');
    console.log(`${t.padEnd(12)} ${String(c.mapped).padStart(6)}/${String(c.total).padEnd(6)} ${pct(c.mapped, c.total).padStart(7)}  unmapped: ${un || '-'}`);
  }
  console.log(`TOTAL ${fmt(covM)}/${fmt(covT)} = ${pct(covM, covT)} of six-type nodes\n`);

  // ---- (3) per-field validity ---------------------------------------------
  // Numeric plausibility rules, by field-name family. Deliberately loose: the
  // point is to catch a shredded layout, not to second-guess an artist. A wrong
  // offset does not produce a slightly odd number, it produces 1e-38 / 1e+38 /
  // NaN and unprintable filenames.
  const RANGE = [
    // Authored alpha is nominally 0..1 and overwhelmingly is, with a small
    // deliberate overbright tail — 652 SEQUENCE nodes carry exactly 1.2, and a
    // few dozen carry 4, 5, 50 or 100. Every one of those is a ROUND number,
    // which is the point: a field read at the wrong offset does not produce
    // 1.2 and 50, it produces 1e-38 and 3.4e38. The bound is therefore set at
    // the observed authored maximum rather than at the nominal 1.
    [/^m_fAlpha(Start|Mid1|Mid2|End)$/, 0, 100],
    [/Rate[12]$/, 0, 100],
    [/^m_fAniTime$/, 0, 100],
    [/^m_fLife/, 0, 1000],
    [/^m_fG?(Begin|Life)Time$/, 0, 10000],
    [/^m_fTheta$/, -720, 720],
    [/Angel|Angle/, -100000, 100000],
  ];
  const GENERIC = [-1e7, 1e7];
  const rangeFor = (name) => {
    for (const [re, lo, hi] of RANGE) if (re.test(name)) return [lo, hi];
    return GENERIC;
  };

  const stat = new Map();   // "TYPE.field" -> {n, ok, badClean, notes:Map}
  // `clean` = the node came from a file that walks to the exact last byte. A
  // field failure inside a file that is already known to be broken says nothing
  // about the layout; one inside a clean file does. Keeping them apart is the
  // difference between a validation and a vibe.
  let fileClean = true;
  const bump = (key, ok, note) => {
    if (!stat.has(key)) stat.set(key, { n: 0, ok: 0, nClean: 0, okClean: 0, notes: new Map() });
    const s = stat.get(key);
    s.n++;
    if (ok) s.ok++;
    if (fileClean) { s.nClean++; if (ok) s.okClean++; }
    if (note) s.notes.set(note, (s.notes.get(note) || 0) + 1);
  };

  const names = new Map();   // kind -> Map(name -> count)
  const noteName = (kind, name) => {
    if (!names.has(kind)) names.set(kind, new Map());
    const m = names.get(kind);
    m.set(name, (m.get(name) || 0) + 1);
  };

  let decoded = 0;
  const flagUnknown = new Map();
  const hist = new Map();       // enumerated field -> value histogram
  const morph = { slots: 0, resolved: 0 };  // MESH slots 1/2 under USEBLENDMESH
  // Corpus-wide totals. `live` excludes the fields the writer never initialises
  // (DEAD_UNINIT), because whatever is in those bytes is by definition not a
  // statement about the layout.
  const tot = { floats: 0, nonFinite: 0, maxAbs: 0, maxAt: '',
                liveFloats: 0, liveNonFinite: 0, liveMaxAbs: 0, liveMaxAt: '',
                strings: 0, unterminated: 0, nonPrintable: 0, dirtyTail: 0 };
  for (const n of nodes) {
    const d = decode(n);
    if (!d) continue;
    decoded++;
    fileClean = n.fileOk;
    const T = n.type;
    // MESH morph targets only mean anything when USEBLENDMESH is set; 6,950 of
    // 8,144 nodes leave the editor's `Dolphin2.x`/`Dolphin3.x` defaults in
    // slots 1 and 2 and neither file exists anywhere in Ran/ (EFFECTS-SCOPE.md
    // §5). Judging those slots unconditionally measures the editor's default,
    // not the decode.
    if (T === 'MESH' && (FLAGS.MESH.USEBLENDMESH & (d.props.m_dwFlag >>> 0))) {
      for (const f of ['m_szMeshFile1', 'm_szMeshFile2']) {
        const v = d.props[f];
        if (v === undefined || !v) continue;
        morph.slots++;
        if (resolve('mesh', v)) morph.resolved++;
      }
    }

    // prefix: the transform and the two times
    bump(`${T}.<matLocal>`, d.prefix.matLocal.every(Number.isFinite));
    bump(`${T}.<bMoveObj>`, d.prefix.bMoveObj === 0 || d.prefix.bMoveObj === 1);
    bump(`${T}.<fGBeginTime>`, Number.isFinite(d.prefix.fGBeginTime) &&
                               d.prefix.fGBeginTime >= 0 && d.prefix.fGBeginTime < 10000);
    bump(`${T}.<fGLifeTime>`, Number.isFinite(d.prefix.fGLifeTime) &&
                              d.prefix.fGLifeTime >= 0 && d.prefix.fGLifeTime < 10000);

    for (const f of specsFor(d.struct)) {
      const key = `${T}.${f.name}`;
      const v = d.props[f.name];
      if (f.kind === 'f32') {
        const live = !DEAD_UNINIT.has(key);
        for (const x of (Array.isArray(v) ? v : [v])) {
          tot.floats++;
          if (live) tot.liveFloats++;
          if (!Number.isFinite(x)) {
            tot.nonFinite++;
            if (live) { tot.liveNonFinite++; tot.nonFiniteAt = `${n.file} ${key}`; }
          } else {
            if (Math.abs(x) > tot.maxAbs) { tot.maxAbs = Math.abs(x); tot.maxAt = `${n.file} ${key}`; }
            if (live && Math.abs(x) > tot.liveMaxAbs) { tot.liveMaxAbs = Math.abs(x); tot.liveMaxAt = `${n.file} ${key}`; }
          }
        }
      } else if (f.kind === 'string') {
        const m = d.props[`${f.name}$`];
        tot.strings++;
        if (!m.terminated) tot.unterminated++;
        if (!/^[\x20-\x7e]*$/.test(v)) tot.nonPrintable++;
        if (m.tail) tot.dirtyTail++;
      }
      if (f.kind === 'string') {
        const meta = d.props[`${f.name}$`];
        const kind = NAME_KIND[f.name];
        const printable = /^[\x20-\x7e]*$/.test(v);
        if (!v) { bump(key, true, 'empty'); continue; }
        if (!printable || !meta.terminated) { bump(key, false, 'not-a-name'); continue; }
        if (!kind) { bump(key, printable, 'unrouted'); continue; }
        const r = resolve(kind, v);
        noteName(kind, v.toLowerCase());
        bump(key, !!r, r === 'stem' ? 'resolved-by-stem' : r ? null : 'not-shipped');
        if (meta.tail) bump(`${key}<tail>`, true);
      } else if (f.kind === 'i32' || f.kind === 'u32') {
        const dom = (ENUMS[T] || {})[f.name];
        if (f.name === 'm_dwFlag') {
          const { unknown } = flagNames(T, v >>> 0);
          bump(key, unknown === 0, unknown ? `unknown-bits` : null);
          if (unknown) flagUnknown.set(`${T} 0x${unknown.toString(16)}`,
                                       (flagUnknown.get(`${T} 0x${unknown.toString(16)}`) || 0) + 1);
        } else if (dom) {
          if (!hist.has(key)) hist.set(key, new Map());
          const h = hist.get(key);
          h.set(v, (h.get(v) || 0) + 1);
          const inRange = v >= dom.range[0] && v <= dom.range[1];
          bump(key, inRange, inRange
            ? (dom.cases.includes(v) ? null : `off-case:${v}(default state)`)
            : `out-of-range:${v}`);
        } else if (DEAD_UNINIT.has(key)) {
          bump(key, true, 'uninitialised-by-writer');
        } else if (TYPES[d.struct][f.name] === 'BOOL') {
          bump(key, v === 0 || v === 1);
        } else {
          bump(key, Number.isInteger(v) && Math.abs(v) < 1e7);
        }
      } else if (DEAD_UNINIT.has(key)) {
        bump(key, true, 'uninitialised-by-writer');
      } else {
        const [lo, hi] = rangeFor(f.name);
        const arr = Array.isArray(v) ? v : [v];
        bump(key, arr.every((x) => Number.isFinite(x) && x >= lo && x <= hi));
      }
    }
  }

  console.log(`-- (3) per-field validity over ${fmt(decoded)} decoded nodes --`);
  console.log('"clean" = restricted to the files that walk to the exact last byte.\n');
  console.log(`${'field'.padEnd(30)} ${'nodes'.padStart(7)} ${'valid'.padStart(8)} ${'clean'.padStart(8)}  notes`);
  for (const t of SIX) {
    const rows = [...stat].filter(([k]) => k.startsWith(`${t}.`) && !k.endsWith('<tail>'));
    if (!rows.length) continue;
    for (const [k, s] of rows) {
      const bad = s.ok < s.n;
      if (verbose || bad) {
        const notes = [...s.notes].sort((a, b) => b[1] - a[1]).slice(0, 3)
          .map(([nn, c]) => `${nn}×${fmt(c)}`).join(' ');
        console.log(`${k.padEnd(30)} ${String(s.n).padStart(7)} ${pct(s.ok, s.n).padStart(8)} ` +
                    `${pct(s.okClean, s.nClean).padStart(8)}  ${notes}`);
      }
    }
    const tot = rows.reduce((a, [, s]) => a + s.n, 0);
    const okk = rows.reduce((a, [, s]) => a + s.ok, 0);
    const totC = rows.reduce((a, [, s]) => a + s.nClean, 0);
    const okC = rows.reduce((a, [, s]) => a + s.okClean, 0);
    console.log(`${(t + ' — all fields').padEnd(30)} ${String(tot).padStart(7)} ` +
                `${pct(okk, tot).padStart(8)} ${pct(okC, totC).padStart(8)}`);
  }
  if (!verbose) console.log('(only fields with at least one failure are listed; --verbose for all)');

  console.log('\n-- (3b) enumerated fields: the value histogram --');
  for (const [k, h] of hist) {
    const rows = [...h].sort((a, b) => b[1] - a[1])
      .map(([v, c]) => `${v}:${fmt(c)}`).join('  ');
    console.log(`${k.padEnd(28)} ${rows}`);
  }

  console.log('\n-- (3d) every float and every string, corpus-wide --');
  console.log(`floats read           ${fmt(tot.floats)}   non-finite ${fmt(tot.nonFinite)} ` +
              `(${pct(tot.nonFinite, tot.floats)})   max |v| ${tot.maxAbs.toExponential(2)}  ${tot.maxAt}`);
  console.log(`  excluding the fields the writer never initialises:`);
  console.log(`floats read           ${fmt(tot.liveFloats)}   non-finite ${fmt(tot.liveNonFinite)} ` +
              `(${pct(tot.liveNonFinite, tot.liveFloats)}${tot.nonFiniteAt ? `, all in ${tot.nonFiniteAt}` : ''})` +
              `   max |v| ${tot.liveMaxAbs.toExponential(2)}  ${tot.liveMaxAt}`);
  console.log(`char[] fields read    ${fmt(tot.strings)}   unterminated ${fmt(tot.unterminated)}   ` +
              `non-printable ${fmt(tot.nonPrintable)}`);
  console.log(`  ...of which carry live bytes AFTER the NUL: ${fmt(tot.dirtyTail)} ` +
              `(${pct(tot.dirtyTail, tot.strings)}) — the debris a regex over the body harvests as filenames`);

  console.log('\n-- (3c) MESH morph slots, only where USEBLENDMESH is set --');
  console.log(`  slots 1+2 filled and used: ${fmt(morph.slots)}   resolve to a shipped mesh: ` +
              `${fmt(morph.resolved)} (${pct(morph.resolved, morph.slots)})`);

  // ---- (4) name resolution -------------------------------------------------
  console.log('\n-- (4) do the decoded names resolve against what ships? --');
  for (const kind of ['texture', 'mesh', 'effect']) {
    const m = names.get(kind);
    if (!m) continue;
    let ok = 0;
    let stem = 0;
    const miss = [];
    for (const nm of m.keys()) {
      const r = resolve(kind, nm);
      if (r === 'exact') ok++;
      else if (r === 'stem') stem++;
      else miss.push(nm);
    }
    console.log(`${kind.padEnd(8)} distinct ${String(m.size).padStart(5)}  ` +
                `exact ${String(ok).padStart(5)}  by-stem ${String(stem).padStart(3)}  ` +
                `not shipped ${String(miss.length).padStart(4)}  (${pct(ok + stem, m.size)} resolve)`);
    if (verbose) console.log(`   missing: ${miss.slice(0, 20).join(' ')}${miss.length > 20 ? ' …' : ''}`);
  }

  // ---- (4b) which flag bits the shipped data actually uses -----------------
  // An importer only has to implement the bits that appear. EFFECTS-SCOPE.md §5
  // measured this for SEQUENCE and MESH; the other four are new here.
  console.log('\n-- (4b) flag bits set, per type --');
  const flagUse = new Map();
  for (const n of nodes) {
    const d = decode(n);
    if (!d || d.props.m_dwFlag === undefined) continue;
    const { on } = flagNames(n.type, d.props.m_dwFlag >>> 0);
    if (!flagUse.has(n.type)) flagUse.set(n.type, new Map());
    const m = flagUse.get(n.type);
    for (const f of on) m.set(f, (m.get(f) || 0) + 1);
  }
  for (const t of SIX) {
    const m = flagUse.get(t);
    if (!m) continue;
    const rows = [...m].sort((a, b) => b[1] - a[1]).map(([f, c]) => `${f} ${fmt(c)}`);
    console.log(`${t}:\n   ${rows.join('  ·  ') || '(none set)'}`);
  }

  if (flagUnknown.size) {
    console.log('\nflag words with bits no #define in the type\'s header accounts for:');
    for (const [k, c] of [...flagUnknown].sort((a, b) => b[1] - a[1]).slice(0, 10)) {
      console.log(`   ${k.padEnd(26)} ${fmt(c)} nodes`);
    }
  }

  // ---- (5) positive control ------------------------------------------------
  // A check that cannot fail proves nothing. Decode the same nodes with the
  // property body shifted by 4 bytes — the exact error the +4/−4 size rule
  // exists to prevent — and confirm the name resolution collapses. If this
  // prints a high rate, the checks above are not measuring anything.
  console.log('\n-- (5) positive control: same nodes, property start shifted +4 --');
  for (const off of [4, -4]) {
    let tried = 0;
    let res = 0;
    for (const n of nodes) {
      const spec = LAYOUTS[verKey(n.type, n.ver)];
      if (!spec) continue;
      const base = (spec.affine ? PREFIX_AFFINE : PREFIX) + off;
      if (base < 0) continue;
      for (const f of specsFor(spec.struct)) {
        if (f.kind !== 'string' || !NAME_KIND[f.name]) continue;
        if (base + f.off + f.size > n.body.length) continue;
        const s = readString(n.body, base + f.off, f.size);
        if (!s.value) continue;
        tried++;
        if (/^[\x20-\x7e]+$/.test(s.value) && resolve(NAME_KIND[f.name], s.value)) res++;
      }
    }
    console.log(`  shift ${off > 0 ? '+' : ''}${off}: ${fmt(res)}/${fmt(tried)} = ${pct(res, tried)} of name fields still resolve`);
  }
}
