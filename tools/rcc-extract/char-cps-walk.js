'use strict';
//
// How many DxEffChar effects are embedded in the shipped `.cps` character
// pieces — measured by walking the piece record to EXACT EOF.
//
// Why this exists: `EffectChar.rcc` (198 `.effskin*`) is NOT the main consumer
// of DxEffect/Char. Every `DxSkinPiece::LoadPiece_*` in
// SOURCE/Lib_Engine/Meshs/DxSkinPieceSaveLoad.cpp ends with
//
//     [u32 nEffects] then nEffects x [u32 TypeID][ DxEffChar::LoadFile ]
//     [u32 m_dwFlag]                                   <- last field in the file
//
// so a character piece carries its own effect/material stack inline. Because
// the effect list is the second-to-last field, a walk that lands on the exact
// last byte proves the effect list was read correctly — the same
// consumed-to-EOF check used for item.isf and .wld0.
//
// Only the versions whose prefix is simple enough to walk are implemented.
// Everything else is reported as "not attempted" rather than guessed.
//
//   node char-cps-walk.js
//   node char-cps-walk.js --strings   also collect texture names per effect type
//
const path = require('path');
const { RccArchive } = require('./rcc.js');
const bytecrypt = require('./bytecrypt.js');
const eff = require('./char-effskin.js');

const RAN = path.resolve(__dirname, '../../../Ran');
const ARCHIVE = path.join(RAN, 'data/skinobject/SkinObject.rcc');

const VERSION_ENCRYPT = 0x0200;   // DxSkinPieceContainer.h:36
const BODY = 132;                 // CSerialFile header

class Reader {
  constructor(buf, off) { this.b = buf; this.o = off; }
  u32() { const v = this.b.readUInt32LE(this.o); this.o += 4; return v; }
  skip(n) {
    if (n < 0 || this.o + n > this.b.length) throw new Error(`skip ${n} past EOF`);
    this.o += n;
  }
  // CSerialFile std::string: [u32 len][len bytes, len INCLUDES the NUL]
  str() {
    const n = this.u32();
    if (n > 0x10000) throw new Error(`implausible string length ${n}`);
    const s = this.b.toString('latin1', this.o, this.o + n).replace(/\0.*$/, '');
    this.o += n;
    return s;
  }
  // char* read as [u32 len][len bytes]
  cstr() { return this.str(); }
  get eof() { return this.o === this.b.length; }
}

/**
 * SMATERIAL_PIECE::LoadFile (DxSkinPieceMaterial.cpp:52) — [ver][blockSize].
 * Parsed structurally on the five known versions, size-skipped otherwise,
 * exactly as the engine does.
 */
function skipMaterial(r) {
  const ver = r.u32();
  const size = r.u32();
  const start = r.o;
  switch (ver) {
    case 0x0105: case 0x0103: r.skip(size); return;   // current + unknown -> size
    case 0x0104: r.skip(4 + 4 + 12 + 12 + 12 + 4 + 8 + 8); r.cstr(); break;
    case 0x0102: r.skip(12); r.cstr(); break;
    case 0x0101: r.skip(8); r.cstr(); break;
    case 0x0100: r.cstr(); r.skip(8); break;
    default: r.skip(size); return;
  }
  if (r.o - start !== size) r.o = start + size;       // trust the block on drift
}

/**
 * SVERTEXINFLU::LoadFile (DxSkinMeshContainer9.cpp:90).
 *
 * THE DECLARED SIZE IS WRONG BY A KNOWABLE AMOUNT. SaveFile computes
 *   dwSize = 4 + 12 + 12 + 4 + 8*numBone   and then, inside the
 *   `if (numBone>0)` guard, adds 8*numBone AGAIN — so every record with bones
 * declares 8*numBone more than it writes. The engine never notices because
 * LoadFile parses structurally on the known version and only uses dwSize to
 * skip an unknown one. Skipping by dwSize desyncs the whole piece.
 */
function skipVertexInflu(r) {
  const ver = r.u32();
  const size = r.u32();
  if (ver !== 0x0101) { r.skip(size); return; }       // SVERTEXINFLU::VERSION
  r.skip(4 + 12 + 12);
  const nBone = r.u32();
  if (nBone > 0 && nBone !== 0xffffffff) {
    if (nBone > 64) throw new Error(`implausible bone count ${nBone}`);
    r.skip(nBone * 8);
  }
}

/** DXSUSER_SLOT::LoadFile — string + D3DXMATRIX + DWORD. */
function readUserSlot(r) { r.str(); r.skip(64); r.u32(); }

/**
 * The effect list. Advances each blob by its declared size plus a per
 * (TypeID, version) correction from `deltas` — see solveDeltas(): a handful of
 * DxEffChar::SaveFile implementations declare a size that does not match what
 * their LoadFile consumes, the same class of bug as SVERTEXINFLU above.
 * Returns the list; the caller validates by EOF.
 */
// Set during the solve pass: readEffects records what it saw and stops instead
// of throwing when a declared size runs past EOF, so a single-effect file can
// still yield the one measurement that fixes its (TypeID, version).
let CAPTURE = null;

function readEffects(r, deltas) {
  const n = r.u32();
  if (n > 64) throw new Error(`implausible effect count ${n}`);
  if (CAPTURE) CAPTURE.count = n;
  const out = [];
  for (let i = 0; i < n; i++) {
    if (CAPTURE && r.o + 12 > r.b.length) break;
    const typeId = r.u32();
    const ver = r.u32();
    const size = r.u32();
    if (size > 0x20000) throw new Error(`implausible effect size ${size}`);
    const d = deltas ? (deltas.get(`${typeId}/${ver}`) || 0) : 0;
    const bodyStart = r.o;
    if (CAPTURE) {
      CAPTURE.seen.push({ typeId, ver, size, bodyStart, index: i });
      if (bodyStart + size + d > r.b.length) break;
    }
    const body = r.b.subarray(r.o, r.o + size + d);
    r.skip(size + d);
    out.push({ typeId, ver, size, actual: size + d, bodyStart, body });
  }
  return out;
}

// Per-version prefix walkers, transcribed one-for-one from the matching
// DxSkinPiece::LoadPiece_* body. Each returns the effect list; the caller then
// reads m_dwFlag and requires EOF, which is what validates the whole walk.
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
  // ref, xfile, skeleton, pieceType, mesh, [10 fixed traces], effects
  0x0102: (r, d) => {
    r.u32(); r.cstr(); r.cstr(); r.u32(); r.cstr();
    for (let i = 0; i < 10; i++) skipVertexInflu(r);
    return readEffects(r, d);
  },
  0x0103: (r, d) => { r.u32(); r.cstr(); r.cstr(); r.u32(); r.cstr(); return trace(r, d); },
  0x0104: (r, d) => { r.u32(); r.cstr(); r.cstr(); r.u32(); r.cstr(); mats(r); return trace(r, d); },
  0x0106: (r, d) => {
    r.u32(); r.cstr(); r.cstr();   // ref, xfile, skeleton (order reverses at 0x0108)
    r.u32(); r.u32();              // piece type, weapon-where-back
    r.cstr(); mats(r); return trace(r, d);
  },
  // 0x0108: same prefix as 0x0109/0x0110 but NO trace list at all.
  0x0108: (r, d) => {
    r.u32(); r.cstr(); r.cstr(); r.cstr(); mats(r); r.u32(); r.u32();
    return readEffects(r, d);
  },
  0x0110: (r, d) => {
    r.u32(); r.cstr(); r.cstr(); r.cstr();   // ref, skeleton, skin, mesh
    mats(r); r.u32(); r.u32(); return trace(r, d);
  },
  0x0112: (r, d) => {
    r.u32(); r.cstr(); r.cstr(); r.cstr();
    r.u32();                       // added at 0x0112
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
 * Getting this wrong shifts the EOF check by exactly one field and makes a
 * correct effect walk look broken.
 */
function readTail(r, ver) {
  if (ver < 0x0105) return;
  r.u32();                                  // m_dwFlag
  if (ver === 0x0116) { r.u32(); r.str(); } // added at 0x0116
}

function walk(buf, name, deltas) {
  const ver = buf.readUInt32LE(BODY);
  let body = buf;
  if (ver >= VERSION_ENCRYPT) {
    body = Buffer.from(buf);
    bytecrypt.decode(body, 'EMBYTECRYPT_PIECE', BODY + 4);
  }
  const w = WALKERS[ver];
  if (!w) return { ver, attempted: false };

  const r = new Reader(body, BODY + 4);
  let effects;
  try {
    effects = w(r, deltas);
    readTail(r, ver);
  } catch (e) {
    return { ver, attempted: true, ok: false, why: e.message, buf: body };
  }
  if (!r.eof) {
    return { ver, attempted: true, ok: false, buf: body, effects,
             why: `consumed ${r.o} of ${body.length}`, over: r.o - body.length };
  }
  return { ver, attempted: true, ok: true, effects, buf: body };
}


/**
 * Solve the per-(TypeID, version) size correction, the same way effect-solve.js
 * did for `.egp`: never fit a table, derive each entry from a file where the
 * answer is forced.
 *
 * A `.cps` ends with the effect list then m_dwFlag. So in a piece carrying
 * exactly ONE effect, the true body length is forced:
 *     actual = (fileLength - 4) - bodyStart
 * Nothing is guessed and nothing is searched. Entries are only accepted when
 * every single-effect file that forces them agrees.
 */
function solveDeltas(files, readOne) {
  const evidence = new Map();          // "type/ver" -> Map(delta -> count)
  for (const e of files) {
    let buf;
    try { buf = readOne(e); } catch (_) { continue; }
    const ver = buf.readUInt32LE(BODY);
    if (!WALKERS[ver]) continue;
    let body = buf;
    if (ver >= VERSION_ENCRYPT) {
      body = Buffer.from(buf);
      bytecrypt.decode(body, 'EMBYTECRYPT_PIECE', BODY + 4);
    }
    CAPTURE = { seen: [], count: -1 };
    try { WALKERS[ver](new Reader(body, BODY + 4), null); } catch (_) { /* prefix failed */ }
    const cap = CAPTURE; CAPTURE = null;
    if (cap.count !== 1 || cap.seen.length !== 1) continue;
    if (ver < 0x0105 || ver === 0x0116) continue;   // tail is not a bare DWORD
    const fx = cap.seen[0];
    const actual = (body.length - 4) - fx.bodyStart;
    if (actual < 0 || actual > 0x20000) continue;
    const k = fx.typeId + '/' + fx.ver;
    if (!evidence.has(k)) evidence.set(k, new Map());
    const m = evidence.get(k);
    m.set(actual - fx.size, (m.get(actual - fx.size) || 0) + 1);
  }

  // Accept an entry only when EVERY file that forces it agrees AND at least
  // three do. One agreeing file is a coincidence waiting to happen.
  const deltas = new Map(), conflicts = [];
  for (const [k, m] of evidence) {
    const entries = [...m.entries()].sort((a, b) => b[1] - a[1]);
    if (m.size === 1 && entries[0][1] >= 3) {
      if (entries[0][0] !== 0) deltas.set(k, entries[0][0]);
      continue;
    }
    if (m.size > 1 || entries[0][0] !== 0) conflicts.push([k, entries]);
  }
  return { deltas, conflicts, evidence };
}

module.exports = { walk, solveDeltas, WALKERS, BODY, VERSION_ENCRYPT };

// ---------------------------------------------------------------------------

if (require.main === module) {
  const wantStrings = process.argv.indexOf('--strings') >= 0;
  const arc = new RccArchive(ARCHIVE);
  const cps = arc.entries.filter(function (e) { return /\.cps$/i.test(e.name); });
  const readOne = function (e) { return arc.read(e); };

  const solved = solveDeltas(cps, readOne);
  const deltas = solved.deltas;

  const verTotal = {}, verOk = {}, verFail = {};
  const failWhy = [];
  const typeCount = {}, typeFiles = {}, typeVers = {}, typeSizes = {};
  const texByType = {};
  let filesWithEffects = 0, totalEffects = 0, okFiles = 0, notAttempted = 0;
  const perFile = [];

  for (const e of cps) {
    let res;
    try { res = walk(arc.read(e), e.name, deltas); }
    catch (err) { res = { ver: -1, attempted: true, ok: false, why: err.message }; }

    const vk = res.ver < 0 ? 'bad' : '0x' + res.ver.toString(16).padStart(4, '0');
    verTotal[vk] = (verTotal[vk] || 0) + 1;
    if (!res.attempted) { notAttempted++; continue; }
    if (!res.ok) {
      verFail[vk] = (verFail[vk] || 0) + 1;
      if (failWhy.length < 12) failWhy.push(e.name + ' v' + vk + ': ' + res.why);
      continue;
    }
    verOk[vk] = (verOk[vk] || 0) + 1;
    okFiles++;
    if (res.effects.length) filesWithEffects++;
    totalEffects += res.effects.length;
    perFile.push({ name: e.name, n: res.effects.length });
    for (const fx of res.effects) {
      typeCount[fx.typeId] = (typeCount[fx.typeId] || 0) + 1;
      if (!typeFiles[fx.typeId]) typeFiles[fx.typeId] = new Set();
      typeFiles[fx.typeId].add(e.name);
      const k = fx.typeId + '/0x' + fx.ver.toString(16).padStart(4, '0');
      typeVers[k] = (typeVers[k] || 0) + 1;
      if (!typeSizes[fx.typeId]) typeSizes[fx.typeId] = [];
      typeSizes[fx.typeId].push(fx.actual);
      if (wantStrings) {
        if (!texByType[fx.typeId]) texByType[fx.typeId] = new Map();
        const m = texByType[fx.typeId];
        let cur = '';
        for (let i = 0; i < fx.body.length; i++) {
          const c = fx.body[i];
          if (c >= 0x20 && c < 0x7f) { cur += String.fromCharCode(c); continue; }
          if (c === 0 && cur.length >= 5 && /\.(dds|tga|png|bmp|jpg|x)$/i.test(cur)) {
            m.set(cur.toLowerCase(), (m.get(cur.toLowerCase()) || 0) + 1);
          }
          cur = '';
        }
      }
    }
  }

  const pad = function (s, w) { return String(s).padStart(w); };
  console.log('\n=== SkinObject.rcc — ' + cps.length + ' .cps entries');

  console.log('\n--- solved effect size corrections (forced by single-effect files) ---');
  if (!deltas.size) console.log('  (none)');
  for (const kv of [...deltas.entries()].sort()) {
    const t = Number(kv[0].split('/')[0]), v = Number(kv[0].split('/')[1]);
    console.log('  ' + (eff.TYPE_NAMES[t] || t).padEnd(16) +
                ' 0x' + v.toString(16).padStart(4, '0') +
                '  actual = declared ' + (kv[1] > 0 ? '+' : '') + kv[1]);
  }
  if (solved.conflicts.length) {
    console.log('  UNRESOLVED (evidence disagrees -> variable-length body):');
    for (const c of solved.conflicts) {
      const t = Number(c[0].split('/')[0]), v = Number(c[0].split('/')[1]);
      console.log('    ' + (eff.TYPE_NAMES[t] || t).padEnd(16) +
                  ' 0x' + v.toString(16).padStart(4, '0') + '  ' +
                  c[1].map(function (x) { return x[0] + 'x' + x[1]; }).join(' '));
    }
  }

  console.log('\n--- LoadPiece versions ---');
  console.log('  version      files   walked  failed  walker');
  for (const v of Object.keys(verTotal).sort()) {
    console.log('  ' + v.padEnd(10) + ' ' + pad(verTotal[v], 6) + '   ' +
                pad(verOk[v] || 0, 5) + '   ' + pad(verFail[v] || 0, 5) + '   ' +
                (WALKERS[parseInt(v, 16)] ? 'yes' : 'NO'));
  }
  console.log('\n  walked to exact EOF: ' + okFiles + ' of ' + cps.length +
              ' (' + (okFiles / cps.length * 100).toFixed(1) + '%);  ' +
              notAttempted + ' files whose version has no walker');
  console.log('  of those, ' + filesWithEffects +
              ' carry >= 1 embedded DxEffChar effect (' +
              (filesWithEffects / Math.max(okFiles, 1) * 100).toFixed(1) + '%)');
  console.log('  total embedded effect blobs: ' + totalEffects);

  if (failWhy.length) {
    console.log('\n  sample failures:');
    failWhy.forEach(function (f) { console.log('    ' + f); });
  }

  console.log('\n--- embedded effect types ---');
  console.log('  id  name              blobs   files    body min/med/max   versions');
  const ids = Object.keys(typeCount).map(Number).sort(function (a, b) {
    return typeCount[b] - typeCount[a];
  });
  for (const id of ids) {
    const s = typeSizes[id].slice().sort(function (x, y) { return x - y; });
    const vers = Object.keys(typeVers).filter(function (k) {
      return k.indexOf(id + '/') === 0;
    }).map(function (k) { return k.split('/')[1] + ':' + typeVers[k]; }).join(' ');
    console.log('  ' + pad(id, 3) + '  ' + (eff.TYPE_NAMES[id] || '???').padEnd(16) + ' ' +
                pad(typeCount[id], 6) + '  ' + pad(typeFiles[id].size, 5) + '   ' +
                pad(s[0], 6) + '/' + pad(s[Math.floor(s.length / 2)], 6) + '/' +
                pad(s[s.length - 1], 6) + '   ' + vers);
  }

  const bins = { 0: 0, 1: 0, 2: 0, '3-5': 0, '6+': 0 };
  for (const f of perFile) {
    const n = f.n;
    bins[n === 0 ? 0 : n === 1 ? 1 : n === 2 ? 2 : n <= 5 ? '3-5' : '6+']++;
  }
  console.log('\n  effects per walked file: ' + JSON.stringify(bins));

  if (wantStrings) {
    console.log('\n--- texture/mesh names per type ---');
    const all = new Set();
    for (const id of ids) {
      const m = texByType[id];
      if (!m || !m.size) { console.log('  ' + eff.TYPE_NAMES[id] + ': (none)'); continue; }
      for (const k of m.keys()) all.add(k);
      const list = [...m.entries()].sort(function (a, b) { return b[1] - a[1]; });
      console.log('  ' + (eff.TYPE_NAMES[id] || id).padEnd(16) + ' ' + pad(m.size, 4) +
                  ' distinct  ' + list.slice(0, 5).map(function (x) {
                    return x[0] + '(' + x[1] + ')';
                  }).join(' '));
    }
    console.log('  TOTAL distinct asset names in embedded effects: ' + all.size);
  }
}
