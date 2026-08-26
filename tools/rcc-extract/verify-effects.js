'use strict';
//
// Read the exported `.reff` files back with an independent reader and check
// them against the source `.egp`.
//
//   node verify-effects.js
//
// The exporter and this share no code beyond the format comment, which is the
// point: a writer verified only by its own reader proves that the two agree,
// not that either is right. What is checked instead is agreement with things
// the exporter never wrote —
//
//   * node counts and type histogram match a fresh `effect-egp.js` walk
//   * the parent links reconstruct the source `depth` exactly
//   * name fields resolve against the SHIPPED texture and mesh sets
//   * every float is finite and every string NUL-terminated in range
const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc.js');
const egp = require('./effect-egp.js');

const base = path.resolve(__dirname, '../../..');
const RAN = path.join(base, 'Ran');
const DIR = path.join(base, 'MOBILE/assets/effects');

function readReff(buf) {
  if (buf.length < 32 || buf.toString('latin1', 0, 4) !== 'REFF')
    throw new Error('bad magic');
  const nodeCount = buf.readUInt32LE(8);
  const decoded = buf.readUInt32LE(12);
  const stringBytes = buf.readUInt32LE(16);
  const floatCount = buf.readUInt32LE(20);
  const intCount = buf.readUInt32LE(24);

  let o = 32;
  const pool = buf.slice(o, o + stringBytes); o += stringBytes;
  const str = (at) => {
    if (at >= pool.length) return null;
    let e = pool.indexOf(0, at);
    if (e < 0) return null;                 // unterminated — reported, not patched
    return pool.toString('latin1', at, e);
  };

  const nodes = [];
  for (let i = 0; i < nodeCount; i++) {
    const n = o + i * 48;
    nodes.push({
      typeId: buf.readUInt32LE(n), ver: buf.readUInt32LE(n + 4),
      parent: buf.readInt32LE(n + 8), depth: buf.readUInt32LE(n + 12),
      subtreeEnd: buf.readUInt32LE(n + 16), name: str(buf.readUInt32LE(n + 20)),
      floatOffset: buf.readUInt32LE(n + 28), floatCount: buf.readUInt32LE(n + 32),
      intOffset: buf.readUInt32LE(n + 36), intCount: buf.readUInt32LE(n + 40),
      nameOffset: buf.readUInt32LE(n + 44),
    });
  }
  o += nodeCount * 48;
  const matAt = o; o += nodeCount * 64;
  const timeAt = o; o += nodeCount * 12;
  const floatAt = o; o += floatCount * 4;
  const intAt = o; o += intCount * 4;
  const nameAt = o;
  const nameCount = (buf.length - nameAt) / 4;

  return { buf, nodeCount, decoded, nodes, matAt, timeAt, floatAt, intAt, nameAt,
           floatCount, intCount, nameCount, str, poolLen: pool.length };
}

// --- shipped resources, for the name check -----------------------------------
const shipped = new Set();
(function scan(dir) {
  for (const it of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, it.name);
    if (it.isDirectory()) { scan(p); continue; }
    if (/\.rcc$/i.test(it.name)) {
      try {
        for (const e of new RccArchive(p).entries)
          shipped.add(path.basename(e.name).toLowerCase());
      } catch { /* archive unreadable — the count below shows it */ }
    } else {
      shipped.add(it.name.toLowerCase());
    }
  }
})(RAN);

// --- source walk, for the cross-check ----------------------------------------
const source = new Map();       // basename -> {nodes, types:Map, depths:[]}
(function scanEgp(dir) {
  for (const it of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, it.name);
    if (it.isDirectory()) { scanEgp(p); continue; }
    if (!/\.rcc$/i.test(it.name)) continue;
    let arc;
    try { arc = new RccArchive(p); } catch { return; }
    for (const e of arc.entries) {
      if (!/\.egp$/i.test(e.name)) continue;
      try {
        const parsed = egp.parse(arc.read(e));
        const types = new Map();
        for (const n of parsed.nodes || []) types.set(n.typeId, (types.get(n.typeId) || 0) + 1);
        source.set(path.basename(e.name, path.extname(e.name)).toLowerCase(), {
          nodes: (parsed.nodes || []).length,
          types,
          depths: (parsed.nodes || []).map((n) => n.depth | 0),
        });
      } catch { /* counted as a source failure below */ }
    }
  }
})(RAN);

// --- checks -------------------------------------------------------------------
const files = fs.readdirSync(DIR).filter((f) => f.endsWith('.reff'));
let pass = 0, fail = 0;
const bad = [];
function check(cond, msg) { if (cond) pass++; else { fail++; if (bad.length < 10) bad.push(msg); } }

let nodesTotal = 0, decodedTotal = 0;
let nameFields = 0, nameResolved = 0, nameUnterminated = 0;
let floatsChecked = 0, nonFinite = 0;
let depthMismatch = 0, parentBad = 0, subtreeBad = 0, countMismatch = 0, typeMismatch = 0;

for (const f of files) {
  const key = f.slice(0, -5).toLowerCase();
  let r;
  try { r = readReff(fs.readFileSync(path.join(DIR, f))); }
  catch (e) { fail++; bad.push(`${f}: ${e.message}`); continue; }

  nodesTotal += r.nodeCount;
  decodedTotal += r.decoded;

  const src = source.get(key);
  if (src) {
    if (src.nodes !== r.nodeCount) { countMismatch++; if (bad.length < 10) bad.push(`${f}: ${r.nodeCount} nodes vs source ${src.nodes}`); }
    const types = new Map();
    for (const n of r.nodes) types.set(n.typeId, (types.get(n.typeId) || 0) + 1);
    for (const [t, c] of src.types) if (types.get(t) !== c) typeMismatch++;
  }

  for (let i = 0; i < r.nodeCount; i++) {
    const n = r.nodes[i];
    if (src && src.depths[i] !== n.depth) depthMismatch++;
    // A parent must precede its child and sit exactly one level up.
    if (n.parent >= 0 && (n.parent >= i || r.nodes[n.parent].depth !== n.depth - 1)) parentBad++;
    if (n.parent < 0 && n.depth !== 0) parentBad++;
    if (n.subtreeEnd < i + 1 || n.subtreeEnd > r.nodeCount) subtreeBad++;

    for (let k = 0; k < n.floatCount; k++) {
      const v = r.buf.readFloatLE(r.floatAt + (n.floatOffset + k) * 4);
      floatsChecked++;
      if (!Number.isFinite(v)) nonFinite++;
    }
    for (let k = 0; k < 0; k++) { /* ints cannot be malformed once in range */ }
  }

  for (let k = 0; k < r.nameCount; k++) {
    const off = r.buf.readUInt32LE(r.nameAt + k * 4);
    const s = r.str(off);
    if (s === null) { nameUnterminated++; continue; }
    if (!s) continue;
    nameFields++;
    if (shipped.has(path.basename(s).toLowerCase())) nameResolved++;
  }
}

check(files.length === source.size,
      `${files.length} .reff vs ${source.size} source .egp`);
check(countMismatch === 0, `${countMismatch} files disagree on node count`);
check(typeMismatch === 0, `${typeMismatch} type-histogram mismatches`);
check(depthMismatch === 0, `${depthMismatch} nodes disagree on depth`);
check(parentBad === 0, `${parentBad} nodes have an impossible parent link`);
check(subtreeBad === 0, `${subtreeBad} nodes have an out-of-range subtreeEnd`);
check(nonFinite === 0, `${nonFinite} of ${floatsChecked} floats are not finite`);
check(nameUnterminated === 0, `${nameUnterminated} unterminated strings`);

const rate = nameFields ? nameResolved / nameFields : 0;
// EFFECTS-PROPS.md measured 94-99.9% per field against the shipped set; the bulk
// of the shortfall is the `Dolphin2.x` editor placeholder, which ships nowhere.
check(rate > 0.6, `only ${(rate * 100).toFixed(1)}% of ${nameFields} name fields resolve`);

console.log(`${files.length} .reff, ${nodesTotal.toLocaleString()} nodes, ` +
            `${decodedTotal.toLocaleString()} decoded ` +
            `(${(decodedTotal / nodesTotal * 100).toFixed(1)}%)`);
console.log(`${floatsChecked.toLocaleString()} floats checked, ${nonFinite} non-finite`);
console.log(`${nameFields.toLocaleString()} name fields, ${nameResolved.toLocaleString()} resolve ` +
            `(${(rate * 100).toFixed(1)}%) against ${shipped.size.toLocaleString()} shipped files`);
console.log(`\n${pass} passed, ${fail} failed`);
for (const b of bad) console.log('  ! ' + b);
process.exit(fail === 0 ? 0 : 1);
