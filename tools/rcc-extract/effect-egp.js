'use strict';
//
// `.egp` — EFF_PROPGROUP, the shipped effect definition format.
//
// Chain, from `Lib_Engine/DxEffect/Single/DxEffSinglePropGManSaveLoad.cpp`:
//
//   [128-byte CSerialFile type string "EFF_PROPGROUP"][u32 FILEVERSION]
//   [u32 VERSION]                       <- the inner version, at byte 132
//   ... body, byte-encoded with EMBYTECRYPT_EGP when VERSION >= 0x0200,
//       starting at byte 136 (the inner version is written BEFORE
//       SetEncodeType, which is why the region starts at 136 and not 132)
//
//   group header (field order changes per version, see GROUP_LAYOUT)
//   [BOOL bValid][u32 TypeID][node]     <- m_pPropRoot
//
// A node is EFF_PROPERTY-derived and every one of the 13 concrete types has
// the same framing (verified: all 13 `*_PROPERTY::LoadFile` in
// DxEffect/Single/ open with `SFile >> dwVer; SFile >> dwSize;` and close with
// `EFF_PROPERTY::LoadFile`):
//
//   [u32 ver][u32 size][blitted body of `size` bytes]
//   [u16 MovSoundVER][u16 SoundVER][SMovSound][BOOL][sibling][BOOL][child]
//
// The two child flags are NOT adjacent — the whole sibling subtree is written
// between them (EFF_PROPERTY::LoadFile reads sibling first, then child), the
// same shape as the .wld0 collision tree.
//
// `size` is `GetSizeBase() + sizeof(PROPERTY)` = 76 + sizeof(PROPERTY). The
// engine only uses it to skip UNKNOWN versions; on a known version it parses
// structurally. So skipping by it is a hypothesis, not a guarantee — the walk
// therefore requires the file to be consumed EXACTLY to EOF, and reports the
// files where that fails instead of returning plausible garbage.
//
const B = require('./bytecrypt');

const TYPE_NAMES = {
  0x0001: 'ROOT',
  0x0002: 'PARTICLESYS',
  0x0003: 'GROUND',
  0x0004: 'MESH',
  0x0005: 'SEQUENCE',
  0x0006: 'DECAL',
  0x0007: 'CAMERA',
  0x0008: 'BLURSYS',
  0x0009: 'LIGHTNING',
  0x0010: 'MOVEROTATE',
  0x0011: 'SKINMESH',
  0x0012: 'WAVE',
  0x0013: 'POINTLIGHT',
  0x0014: 'MOVETARGET',
};
// EFFSINGLE_ROOT (1) has no *_PROPERTY class and is never constructed by
// EFF_PROPERTY::NEW_PROP, so it cannot legally appear in a file.
const INSTANTIABLE = new Set([2, 3, 4, 5, 6, 7, 8, 9, 16, 17, 18, 19, 20]);

// Group header field order, per EFF_PROPGROUP::LOAD_01xx. 0x0100/0x0101 have
// no m_vLocal; 0x0106/0x0200 reorder to (vLocal, vMin, vMax).
const GROUP_LAYOUT = {
  0x0100: ['flag', 'vMax', 'vMin'],
  0x0101: ['flag', 'vMax', 'vMin'],
  0x0102: ['flag', 'vMax', 'vMin', 'vLocal'],
  0x0103: ['flag', 'vMax', 'vMin', 'vLocal'],
  0x0104: ['flag', 'vMax', 'vMin', 'vLocal'],
  0x0105: ['flag', 'vMax', 'vMin', 'vLocal'],
  0x0106: ['flag', 'vLocal', 'vMin', 'vMax'],
  0x0200: ['flag', 'vLocal', 'vMin', 'vMax'],
};

const ENCODE_GATE = 0x0200;
const ENCODE_FROM = 136;

// ---- the declared node size is wrong, by a knowable amount --------------------
//
// `dwSize` is written as `GetSizeBase() + sizeof(m_Property)` where today
// `GetSizeBase()` is `matrix(64) + BOOL(4) + float(4) + float(4)` = 76
// (DxEffSinglePropGMan.cpp:136). Two things break that:
//
//  * Every pre-current version reads a 36-byte DXAFFINEPARTS after the matrix
//    and reconstitutes m_matLocal from it. The writer of that era counted
//    `matrix + DXAFFINEPARTS + 2 floats` = 108 and did NOT count `m_bMoveObj`,
//    which it wrote anyway. So those bodies are **4 bytes longer** than declared.
//  * DECAL is the one type whose LoadFile never reads `m_bMoveObj` at all
//    (DxEffectDecal.cpp) while `GetSizeBase()` still counts it, so its current
//    version is **4 bytes shorter** than declared.
//
// DECAL 0x0100 is both, and cancels to 0 — which is exactly what the data
// shows, and is the reason to trust the rule rather than a fitted table.
//
// The version each type calls "current" is its `VERSION` constant, all thirteen
// read out of SOURCE rather than inferred:
const CURRENT_VERSION = {
  BLURSYS: 0x0102, CAMERA: 0x0101, DECAL: 0x0101, GROUND: 0x0102,
  LIGHTNING: 0x0102, MESH: 0x0105, MOVEROTATE: 0x0103, MOVETARGET: 0x0101,
  PARTICLESYS: 0x0107, POINTLIGHT: 0x0101, SEQUENCE: 0x0102, SKINMESH: 0x0101,
  WAVE: 0x0104,
};
// Older branches that do NOT read a DXAFFINEPARTS. PARTICLESYS dropped it two
// versions before its current one; every other type dropped it exactly at
// `VERSION`. (Measured with effect-srcscan.js over all 13 LoadFile bodies.)
const NO_AFFINE_OLD = new Set(['PARTICLESYS 0x0105', 'PARTICLESYS 0x0106']);
// The only type that does not read m_bMoveObj.
const NO_MOVEOBJ = new Set(['DECAL']);

function sizeDelta(type, ver) {
  const cur = CURRENT_VERSION[type];
  const key = `${type} 0x${ver.toString(16).padStart(4, '0')}`;
  const affine = cur !== undefined && ver < cur && !NO_AFFINE_OLD.has(key);
  return (affine ? 4 : 0) + (NO_MOVEOBJ.has(type) ? -4 : 0);
}

class Reader {
  constructor(buf) { this.b = buf; this.p = 0; }
  need(n) { if (this.p + n > this.b.length) throw new Error(`EOF at ${this.p} needing ${n}`); }
  u32() { this.need(4); const v = this.b.readUInt32LE(this.p); this.p += 4; return v; }
  i32() { this.need(4); const v = this.b.readInt32LE(this.p); this.p += 4; return v; }
  u16() { this.need(2); const v = this.b.readUInt16LE(this.p); this.p += 2; return v; }
  f32() { this.need(4); const v = this.b.readFloatLE(this.p); this.p += 4; return v; }
  vec3() { return [this.f32(), this.f32(), this.f32()]; }
  skip(n) { if (n < 0) throw new Error(`negative skip ${n}`); this.need(n); this.p += n; }
  slice(n) { this.need(n); const s = this.b.subarray(this.p, this.p + n); this.p += n; return s; }
  get eof() { return this.p >= this.b.length; }
}

/** NUL-terminated fixed-width char array out of a blitted body. */
function cstr(buf, off, max) {
  if (off + max > buf.length) return null;
  const s = buf.subarray(off, off + max);
  const nul = s.indexOf(0);
  return s.toString('latin1', 0, nul === -1 ? max : nul);
}

// SSound::LoadSet / LoadSet100 (SuperSound.cpp:308). SoundVer 100 is a fixed
// 256-byte name; anything else is [int len][len chars][BOOL loop] and len 0
// means "no file", with no chars following.
function readSound(r) {
  const movSoundVer = r.u16();
  const soundVer = r.u16();
  r.f32(); // SMovSound::m_MinRange
  r.f32(); // SMovSound::m_MaxRange
  let name = '';
  if (soundVer === 100) {
    const raw = r.slice(256);
    const nul = raw.indexOf(0);
    name = raw.toString('latin1', 0, nul === -1 ? 256 : nul);
    r.u16();  // wBufferCount
    r.u32();  // BOOL bLoop
  } else {
    const n = r.i32();
    if (n < 0 || n > 4096) throw new Error(`sound name length ${n}`);
    if (n) name = r.slice(n).toString('latin1').replace(/\0.*$/, '');
    r.u32();  // BOOL bLoop
  }
  return { movSoundVer, soundVer, sound: name };
}

function readNode(r, typeId, out, depth, opts, parentIdx = -1) {
  if (depth > 512) throw new Error('prop tree deeper than 512');
  if (!INSTANTIABLE.has(typeId)) throw new Error(`unknown TypeID 0x${typeId.toString(16)} at ${r.p}`);
  const at = r.p - 4;
  const ver = r.u32();
  const size = r.u32();
  const type = TYPE_NAMES[typeId] || `0x${typeId.toString(16)}`;
  const delta = sizeDelta(type, ver);
  if (opts && opts.trace) {
    opts.trace(`${'  '.repeat(depth)}@${at} ${type} v0x${ver.toString(16)} size=${size}${delta ? ` (${delta >= 0 ? '+' : ''}${delta})` : ''}`);
  }
  // `opts.bodySize` lets a caller override the declared size with a measured
  // one (effect-props.js passes the layout probe's prefix + sizeof(PROPERTY)).
  // Off by default: the declared size is what the engine writes and the walk's
  // exact-EOF property is measured against it.
  let bodyLen = size + delta;
  if (opts && opts.bodySize) {
    const v = opts.bodySize(type, ver, size, delta);
    if (v !== null && v !== undefined) bodyLen = v;
  }
  if (bodyLen > r.b.length || bodyLen < 0) throw new Error(`node size ${size} exceeds file`);
  const body = r.slice(bodyLen);
  // `index` is this node's position in `out`; `parent` is the index of its tree
  // parent (-1 for the root). Siblings share a parent, so the sibling recursion
  // is passed THIS node's parentIdx, and only the child recursion is passed our
  // own index. Additive: existing consumers ignore these fields. Used by
  // extract-effects-json.js to fold a MOVEROTATE node's animated transform onto
  // its drawable descendants (MOVEROTATE itself renders nothing — DxEffectMoveRotate.cpp).
  const index = out.length;
  const node = { type, typeId, ver, size, delta, depth, body, index, parent: parentIdx };
  // Recorded before the sound record, so a file that dies partway still
  // reports the nodes it did read rather than nothing.
  out.push(node);
  Object.assign(node, readSound(r));
  if (opts && opts.trace) {
    opts.trace(`${'  '.repeat(depth)}   sound v${node.soundVer} "${node.sound}" -> ${r.p}`);
  }
  if (r.u32()) readNode(r, r.u32(), out, depth, opts, parentIdx);      // sibling — shares our parent
  if (r.u32()) readNode(r, r.u32(), out, depth + 1, opts, index);      // child — parented to us
  return node;
}

/**
 * Parse one .egp buffer.
 * @returns {{ok:boolean, type:string, fileVer:number, ver:number,
 *            encoded:boolean, nodes:Array, trailing:number, error?:string}}
 */
function parse(buf, name = '', opts) {
  const head = B.readHeader(buf);
  if (!head) return { ok: false, error: 'shorter than a CSerialFile header', name };
  const base = {
    name,
    type: head.type,
    fileVer: head.version,
    ver: buf.length >= 136 ? buf.readUInt32LE(132) : 0,
  };
  base.encoded = base.ver >= ENCODE_GATE;
  if (head.type !== 'EFF_PROPGROUP') {
    return { ...base, ok: false, nodes: [], error: `type "${head.type}"` };
  }
  const layout = GROUP_LAYOUT[base.ver];
  if (!layout) return { ...base, ok: false, nodes: [], error: `unhandled version 0x${base.ver.toString(16)}` };

  let body = buf;
  if (base.encoded) {
    body = Buffer.from(buf);
    B.decode(body, 'EMBYTECRYPT_EGP', ENCODE_FROM);
  }
  const r = new Reader(body);
  r.p = 136;
  const group = {};
  const nodes = [];
  try {
    for (const f of layout) group[f] = f === 'flag' ? r.u32() : r.vec3();
    if (r.u32()) readNode(r, r.u32(), nodes, 0, opts);
  } catch (e) {
    return { ...base, ok: false, nodes, group, at: r.p, error: e.message };
  }
  const trailing = body.length - r.p;
  return {
    ...base,
    ok: trailing === 0,
    group,
    nodes,
    trailing,
    error: trailing === 0 ? undefined : `${trailing} trailing bytes`,
  };
}

// ---- name fields inside the blitted body -------------------------------------
//
// Offsets are relative to the START of the node body (i.e. after [ver][size]),
// derived from the PROPERTY struct in the matching header and then CONFIRMED
// against the data by effect-fields.js, which histograms where NUL-delimited
// filename-shaped runs begin. The two agree on every entry below.
//
// Reading the field rather than regexing the body matters: a `char[256]` that
// was overwritten by a shorter name keeps the tail of the previous one after
// the NUL, so a free scan invents names like `re.tga`, `ing.dds` and `1.dds`.
//
// Body prefix is 76 (matrix 64 + BOOL + 2 floats), or 112 on the pre-current
// versions that carry a DXAFFINEPARTS, or 72 for DECAL (no m_bMoveObj).
//
// THIRD confirmation, added later: `effect-props.js` decodes the same fields
// from the LAYOUT PROBE (MSVC sizeof/offsetof over the real headers) rather
// than from either the histogram or the header read by eye, and every name
// offset below agrees with it exactly for the six types it covers — SEQUENCE
// 280, MESH 96/352/608/864, PARTICLESYS 192/448/704, GROUND 220, BLURSYS 140.
// Three independent derivations, one answer. See MOBILE/EFFECTS-PROPS.md.
const FIELDS = {
  // SEQUENCE::PROPERTY — m_szTexture is the last field.
  'SEQUENCE 0x0102': [[280, 'texture']],
  'SEQUENCE 0x0101': [[316, 'texture']],
  // MESH::PROPERTY — 3 mesh slots then the texture, 256 apart.
  'MESH 0x0105': [[96, 'mesh'], [352, 'mesh'], [608, 'mesh'], [864, 'texture']],
  'MESH 0x0104': [[132, 'mesh'], [388, 'mesh'], [644, 'mesh'], [900, 'texture']],
  'MESH 0x0103': [[132, 'mesh'], [388, 'mesh'], [644, 'mesh'], [900, 'texture']],
  // PARTICLESYS::PROPERTY — texture, mesh, and a nested .egp to spawn.
  'PARTICLESYS 0x0107': [[192, 'texture'], [448, 'mesh'], [704, 'effect']],
  'PARTICLESYS 0x0106': [[188, 'texture'], [444, 'mesh'], [700, 'effect']],
  'PARTICLESYS 0x0105': [[188, 'texture'], [444, 'mesh'], [700, 'effect']],
  'PARTICLESYS 0x0104': [[208, 'texture'], [464, 'mesh']],
  'PARTICLESYS 0x0103': [[208, 'texture'], [464, 'mesh']],
  'GROUND 0x0102': [[220, 'texture']],
  'GROUND 0x0101': [[256, 'texture']],
  'BLURSYS 0x0102': [[140, 'texture']],
  'BLURSYS 0x0101': [[176, 'texture']],
  'BLURSYS 0x0100': [[180, 'texture']],
  // LIGHTNING has an inner and an outer texture.
  'LIGHTNING 0x0102': [[164, 'texture'], [420, 'texture']],
  'LIGHTNING 0x0101': [[200, 'texture'], [456, 'texture']],
  'LIGHTNING 0x0100': [[200, 'texture'], [456, 'texture']],
  'DECAL 0x0101': [[204, 'texture']],
  'DECAL 0x0100': [[240, 'texture']],
  'SKINMESH 0x0101': [[80, 'skinchar']],
  'MOVETARGET 0x0101': [[104, 'effect']],
};
// Types whose PROPERTY declares no char array at all — measured as well as read:
// effect-fields.js finds zero filename-shaped runs in any of their bodies.
const NO_NAME_FIELDS = new Set(['MOVEROTATE', 'POINTLIGHT', 'WAVE', 'CAMERA']);

/** Named resources of one node: [{kind, name}]. Empty fields are dropped. */
function resources(node) {
  const key = `${node.type} 0x${node.ver.toString(16).padStart(4, '0')}`;
  const fields = FIELDS[key];
  if (!fields) return null;                 // version not mapped — say so, don't guess
  const out = [];
  for (const [off, kind] of fields) {
    const s = cstr(node.body, off, Math.min(260, node.body.length - off));
    if (s && s.trim()) out.push({ kind, name: s.trim(), off });
  }
  return out;
}

module.exports = {
  parse, TYPE_NAMES, INSTANTIABLE, GROUP_LAYOUT, cstr, Reader,
  sizeDelta, CURRENT_VERSION, NO_AFFINE_OLD, NO_MOVEOBJ,
  FIELDS, NO_NAME_FIELDS, resources,
};
