'use strict';
//
// Resolve every `.cps` character piece to the `.x` mesh it actually draws.
//
//   node cps-refs.js                report
//   node cps-refs.js --out FILE     write piece -> mesh map
//
// A `.cps` holds NO geometry. `DxSkinPiece::LoadPiece_*` reads a reference id,
// then a run of names — skeleton, skin, mesh depending on version — then
// materials, per-bone traces and the embedded effect list. The vertices live in
// an `.x`, which is already extracted to `.rmesh`.
//
// That matters because the character-side gap looked much larger than it is:
// 2,352 distinct pieces are named by the 1,608 shipped `.chf` characters and
// only 35 exist as `.rmesh`, which reads like 2,317 missing meshes. They are
// not missing — they are indirections that were never followed.
//
// Version handling is lifted from char-cps-walk.js, which validates each walk
// by landing on the file's exact last byte (82.7% of 12,879 pieces). Here the
// same walkers run, but the name fields are captured instead of discarded, and
// only pieces that still reach EOF are trusted — a walk that desyncs would
// produce plausible-looking garbage names otherwise.
const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc.js');
const bytecrypt = require('./bytecrypt.js');

const RAN = path.resolve(__dirname, '../../../Ran');
const ARCHIVE = path.join(RAN, 'data/skinobject/SkinObject.rcc');
const MESH_DIR = path.resolve(__dirname, '../../..', 'MOBILE/assets/meshes');

const VERSION_ENCRYPT = 0x0200;
const BODY = 132;

class Reader {
  constructor(buf, off) { this.b = buf; this.o = off; this.names = []; }
  u32() { const v = this.b.readUInt32LE(this.o); this.o += 4; return v; }
  skip(n) {
    if (n < 0 || this.o + n > this.b.length) throw new Error(`skip ${n} past EOF`);
    this.o += n;
  }
  str() {
    const n = this.u32();
    if (n > 0x10000) throw new Error(`implausible string length ${n}`);
    const s = this.b.toString('latin1', this.o, this.o + n).replace(/\0.*$/, '');
    this.o += n;
    return s;
  }
  // Every name field the walker reads is recorded in order; which slot means
  // "mesh" moves between versions, so the classification is done afterwards
  // against what actually exists rather than hard-coded per version.
  cstr() { const s = this.str(); this.names.push(s); return s; }
  get eof() { return this.o === this.b.length; }
}

function skipMaterial(r) {
  const ver = r.u32();
  const size = r.u32();
  if (size > 0x100000) throw new Error(`implausible material size ${size}`);
  r.skip(size);
}

function skipVertexInflu(r) {
  const ver = r.u32();
  const size = r.u32();
  if (ver !== 0x0101) { r.skip(size); return; }
  r.skip(4 + 12 + 12);
  const nBone = r.u32();
  if (nBone > 0 && nBone !== 0xffffffff) {
    if (nBone > 64) throw new Error(`implausible bone count ${nBone}`);
    r.skip(nBone * 8);
  }
}

function readUserSlot(r) { r.str(); r.skip(64); r.u32(); }

function readEffects(r, deltas) {
  const n = r.u32();
  if (n > 64) throw new Error(`implausible effect count ${n}`);
  for (let i = 0; i < n; i++) {
    const typeId = r.u32();
    const ver = r.u32();
    const size = r.u32();
    if (size > 0x20000) throw new Error(`implausible effect size ${size}`);
    const d = deltas ? (deltas.get(`${typeId}/${ver}`) || 0) : 0;
    r.skip(size + d);
  }
}

const trace = (r, d) => {
  const n = r.u32();
  if (n > 4096) throw new Error(`implausible trace count ${n}`);
  for (let i = 0; i < n; i++) { r.str(); skipVertexInflu(r); }
  return readEffects(r, d);
};
const mats = (r) => {
  const n = r.u32();
  if (n > 4096) throw new Error(`implausible material count ${n}`);
  for (let i = 0; i < n; i++) skipMaterial(r);
};

const WALKERS = {
  0x0102: (r, d) => {
    r.u32(); r.cstr(); r.cstr(); r.u32(); r.cstr();
    for (let i = 0; i < 10; i++) skipVertexInflu(r);
    return readEffects(r, d);
  },
  0x0103: (r, d) => { r.u32(); r.cstr(); r.cstr(); r.u32(); r.cstr(); return trace(r, d); },
  0x0104: (r, d) => { r.u32(); r.cstr(); r.cstr(); r.u32(); r.cstr(); mats(r); return trace(r, d); },
  0x0106: (r, d) => {
    r.u32(); r.cstr(); r.cstr(); r.u32(); r.u32(); r.cstr(); mats(r); return trace(r, d);
  },
  0x0108: (r, d) => {
    r.u32(); r.cstr(); r.cstr(); r.cstr(); mats(r); r.u32(); r.u32();
    return readEffects(r, d);
  },
  0x0110: (r, d) => {
    r.u32(); r.cstr(); r.cstr(); r.cstr(); mats(r); r.u32(); r.u32(); return trace(r, d);
  },
  0x0112: (r, d) => {
    r.u32(); r.cstr(); r.cstr(); r.cstr(); r.u32();
    mats(r); r.u32(); r.u32(); readUserSlot(r); return trace(r, d);
  },
  0x0200: (r, d) => {
    r.u32(); r.cstr(); r.cstr(); r.cstr();
    mats(r); r.u32(); r.u32(); readUserSlot(r); return trace(r, d);
  },
};
WALKERS[0x0105] = WALKERS[0x0104];
WALKERS[0x0107] = WALKERS[0x0106];
WALKERS[0x0109] = WALKERS[0x0110];
WALKERS[0x0113] = WALKERS[0x0112];
WALKERS[0x0114] = WALKERS[0x0112];
WALKERS[0x0115] = WALKERS[0x0112];
WALKERS[0x0116] = WALKERS[0x0112];
WALKERS[0x0201] = WALKERS[0x0200];

/**
 * What each version reads AFTER the effect list. 0x0101..0x0104 read nothing;
 * 0x0105 onward read m_dwFlag; 0x0116 adds a DWORD and a string after it.
 *
 * Reading a bare m_dwFlag everywhere shifts the EOF check by exactly one field
 * and makes a correct effect walk look broken — which is what put 2,305 pieces
 * in the desync bucket here before this was applied.
 */
function readTail(r, ver) {
  if (ver < 0x0105) return;
  r.u32();                                  // m_dwFlag
  if (ver === 0x0116) { r.u32(); r.str(); } // added at 0x0116
}

// ---------------------------------------------------------------------------

const stem = (s) => s.toLowerCase().replace(/\.[a-z0-9]+$/, '');

const arc = new RccArchive(ARCHIVE);
// `.aps` accessory pieces use the same container and the same version range, so
// they walk with the same code. They are 528 of the 2,352 pieces the shipped
// characters name — roughly a fifth of the character-side gap.
// `.abl` is deliberately NOT included. It sits in the same archive and eight
// character pieces name one, so it looks like the same container — it is not.
// Adding it moved "unreadable" from 75 to 1,046 and "no walker" from 86 to 483
// while resolving zero additional character pieces, which is what a different
// format under a different crypt gate looks like.
const entries = arc.entries.filter(e => /\.(cps|aps)$/i.test(e.name));
console.log(`${ARCHIVE.split(/[\\/]/).pop()}: ${entries.length} .cps/.aps`);

const haveMesh = new Set(fs.existsSync(MESH_DIR)
  ? fs.readdirSync(MESH_DIR).filter(f => f.endsWith('.rmesh')).map(f => stem(f))
  : []);
console.log(`${haveMesh.size} extracted .rmesh on disk`);

// Sub-mesh names inside each .rmesh. A piece names an .x AND a mesh within it —
// `s_13.X` carries both `13body` and `13weapon` — so resolving only the file
// would draw the whole model where one part was meant.
const submeshNames = new Map();
function meshNamesOf(fileStem) {
  if (submeshNames.has(fileStem)) return submeshNames.get(fileStem);
  let set = null;
  try {
    const b = fs.readFileSync(path.join(MESH_DIR, `${fileStem}.rmesh`));
    if (b.toString('latin1', 0, 4) === 'RMSH') {
      const boneCount = b.readUInt32LE(12);
      const meshCount = b.readUInt32LE(16);
      const stringBytes = b.readUInt32LE(36);   // 40 written into a 44-byte header
      const pool = b.slice(44, 44 + stringBytes);
      const readStr = (o) => {
        if (o >= pool.length) return '';
        let e = pool.indexOf(0, o); if (e < 0) e = pool.length;
        return pool.toString('latin1', o, e);
      };
      const tbl = 44 + stringBytes + boneCount * 72;
      set = new Set();
      for (let i = 0; i < meshCount; i++)
        set.add(readStr(b.readUInt32LE(tbl + i * 40)).toLowerCase());
    }
  } catch { /* absent or malformed — reported as unresolved, not guessed */ }
  submeshNames.set(fileStem, set);
  return set;
}

/**
 * Read just the three name fields.
 *
 * Every `LoadPiece_*` opens with a u32 reference id followed by the names, so
 * the prefix is reachable without parsing materials, traces or the embedded
 * effect list — which is what the full walk spends all its effort on and what
 * makes it fail on 3,000 pieces.
 *
 * The walk-to-EOF check is unavailable here, so the names are validated instead
 * by the thing they are for: the geometry name must be an extracted mesh and
 * the sub-mesh name must be one of the meshes inside it. Two independent
 * lookups against data this function never sees is a stronger check for THIS
 * purpose than reaching the last byte, because a desync at the effect list
 * cannot corrupt names already read.
 */
function readNames(buf, ver) {
  const r = new Reader(buf, BODY + 4);
  r.u32();                                  // reference id
  if (ver <= 0x0107) {
    // [ref][geometry][skeleton][pieceType][mesh name] — and on 0x0106/0x0107 a
    // second u32 for weapon-on-back sits inside that gap. Reading three strings
    // back to back walks straight through the integers and returns the piece
    // type's bytes as a name, which is why the naive version only matched half.
    const a = r.str(), b = r.str();
    r.u32();
    if (ver >= 0x0106) r.u32();
    return [a, b, r.str()];
  }
  // [ref][skeleton][geometry][mesh name] — contiguous from 0x0108 on.
  return [r.str(), r.str(), r.str()];
}

const out = {};
const stats = { ok: 0, desync: 0, noWalker: 0, failed: 0, prefix: 0 };
const slotHits = {};       // which name slot resolves to a real mesh
const versions = {};

for (const e of entries) {
  let buf, ver;
  try {
    buf = arc.read(e);
    ver = buf.readUInt32LE(BODY);
    versions[ver] = (versions[ver] || 0) + 1;
    // decode() mutates in place and starts AFTER the version dword, which is
    // itself never encoded — that is how the version is readable at all.
    if (ver >= VERSION_ENCRYPT) bytecrypt.decode(buf, 'EMBYTECRYPT_PIECE', BODY + 4);
  } catch { stats.failed++; continue; }

  // Try the full walk first — it is the stronger check when it succeeds — and
  // fall back to reading only the name prefix when it does not. A piece whose
  // effect list will not parse still has perfectly readable names.
  let named = null, walked = false;
  const w = WALKERS[ver];
  if (w) {
    try {
      const r = new Reader(buf, BODY + 4);
      w(r, null);
      readTail(r, ver);
      if (r.eof) { named = r.names.filter(Boolean); walked = true; stats.ok++; }
      else stats.desync++;
    } catch { stats.desync++; }
  } else {
    stats.noWalker++;
  }
  if (!named) {
    try { named = readNames(buf, ver); stats.prefix++; } catch { stats.failed++; continue; }
  }

  {
    // The two name slots SWAP at 0x0108, which is exactly the reversal the
    // walker's own comment records:
    //   <= 0x0107   [geometry .x][skeleton .x][mesh name]
    //   >= 0x0108   [skeleton .x][geometry .x][mesh name]
    // Picking "the first name that exists on disk" instead looks like it works
    // — it resolved 100% of pieces — but on 0x0114 it silently returns the
    // SKELETON, which has no meshes at all. That failure is only visible
    // because the sub-mesh lookup then finds nothing to match against.
    const slot = ver >= 0x0108 ? 1 : 0;
    const mesh = named[slot] ? stem(named[slot]) : null;
    const skeleton = named[slot ^ 1] ? stem(named[slot ^ 1]) : null;
    if (mesh && haveMesh.has(mesh)) slotHits[slot] = (slotHits[slot] || 0) + 1;

    // The sub-mesh name is the remaining slot. `[Mesh]` is a suffix the
    // exporter appends and some names carry a leading underscore the frame does
    // not, so both are stripped before comparing.
    let sub = named[2] || null, subOk = null;
    if (mesh && sub) {
      const names = meshNamesOf(mesh);
      if (names) {
        const cands = [];
        const a = sub.toLowerCase();
        cands.push(a);
        const b2 = a.replace(/\[mesh\]$/, '');
        cands.push(b2, b2.replace(/^_/, ''), `_${b2}`);
        const found = cands.find(c => names.has(c));
        subOk = !!found;
        if (found) sub = found;
      }
    }
    // Keyed by FULL name, not stem: 10 pieces ship as both `.aps` and `.cps`
    // (`event_Athena`), and a stem key silently keeps whichever the archive
    // lists last. A `.chf` names its pieces with the extension, so the exact
    // key is both available and correct.
    out[e.name.toLowerCase()] = { version: ver, mesh, skeleton, submesh: sub, submeshFound: subOk, walked };
  }
}

const resolved = Object.values(out).filter(v => v.mesh && haveMesh.has(v.mesh)).length;
console.log(`\nwalked to EOF ${stats.ok}, desynced ${stats.desync}, ` +
            `no walker ${stats.noWalker}, prefix-only ${stats.prefix}, unreadable ${stats.failed}`);
console.log(`${resolved} of ${Object.keys(out).length} pieces resolve to an extracted mesh ` +
            `(${(resolved / Math.max(Object.keys(out).length, 1) * 100).toFixed(1)}%)`);
// Prefix-only names must hold up as well as walked ones. If they did not, the
// fallback would be manufacturing plausible garbage.
const pw = Object.values(out).filter(v => v.walked && v.submeshFound !== null);
const pp = Object.values(out).filter(v => !v.walked && v.submeshFound !== null);
const rate = (a) => a.length ? `${(a.filter(v => v.submeshFound).length / a.length * 100).toFixed(1)}%` : 'n/a';
console.log(`sub-mesh hit rate: walked ${rate(pw)} (${pw.length}), ` +
            `prefix-only ${rate(pp)} (${pp.length})`);
console.log(`geometry name slot: ${JSON.stringify(slotHits)}`);

const withSub = Object.values(out).filter(v => v.submeshFound !== null);
const subHit = withSub.filter(v => v.submeshFound).length;
console.log(`sub-mesh name found in the .rmesh: ${subHit} of ${withSub.length} ` +
            `(${(subHit / Math.max(withSub.length, 1) * 100).toFixed(1)}%)`);
const skelHit = Object.values(out).filter(v => v.skeleton).length;
console.log(`skeleton resolved: ${skelHit} of ${Object.keys(out).length}`);

const distinctMeshes = new Set(Object.values(out).filter(v => v.mesh).map(v => v.mesh));
console.log(`${distinctMeshes.size} distinct meshes referenced by pieces`);

// Which piece names the shipped characters actually need, and how many of those
// now resolve. This is the number the gap was measured in.
const TYPES = path.resolve(__dirname, '../../..', 'MOBILE/assets/animtypes.json');
if (fs.existsSync(TYPES)) {
  const chars = JSON.parse(fs.readFileSync(TYPES, 'utf8')).characters;
  const needed = new Set();
  for (const k of Object.keys(chars)) for (const p of chars[k].pieces || []) needed.add(p.toLowerCase());
  let hit = 0, walked = 0;
  for (const p of needed) {
    if (!out[p]) continue;
    walked++;
    if (out[p].mesh) hit++;
  }
  console.log(`\ncharacter pieces: ${needed.size} distinct, ${walked} walked, ` +
              `${hit} resolve to a mesh`);
}

const outArg = process.argv.indexOf('--out');
if (outArg > 0) {
  fs.writeFileSync(process.argv[outArg + 1], JSON.stringify({ pieces: out }));
  console.log(`\nwrote ${process.argv[outArg + 1]}`);
}
