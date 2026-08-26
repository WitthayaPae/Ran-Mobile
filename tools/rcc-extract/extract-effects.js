'use strict';
//
// Export `.egp` effects as data Unity can build from.
//
//   node extract-effects.js --list          inventory, nothing written
//   node extract-effects.js --out DIR       one .reff per effect
//
// An `.egp` is `EFF_PROPGROUP`: a tree of `EFF_PROPERTY` nodes in 13 types, of
// which six carry 95.5% of all shipped nodes (EFFECTS-SCOPE.md §3). `effect-egp.js`
// walks the tree; `effect-props.js` decodes each node's fields from layouts the
// MSVC probe measured. This joins them and writes the result.
//
// Format, little-endian throughout.
//
//   header 32 bytes
//     char[4] "REFF"
//     u32  version = 1
//     u32  nodeCount        every node, including ones with no decoded fields
//     u32  decodedCount     nodes whose fields were read
//     u32  stringBytes
//     u32  floatCount       total across all nodes
//     u32  intCount         total across all nodes
//     u32  reserved (0)
//   strings   stringBytes, NUL-terminated, referenced by byte offset
//   nodes     nodeCount * 48
//               u32 typeId, u32 version, i32 parent, u32 depth,
//               u32 subtreeEnd, u32 name, u32 flags, u32 floatOffset,
//               u32 floatCount, u32 intOffset, u32 intCount, u32 nameOffset
//   matrices  nodeCount * 64      the node's local D3DXMATRIX
//   timing    nodeCount * 12      i32 bMoveObj, f32 beginTime, f32 lifeTime
//   floats    floatCount * 4
//   ints      intCount * 4
//   names     one u32 string offset per named field, in field order
//
// **Children are addressed positionally**, which is what makes an unknown node
// type safe to skip: a node's direct children are the entries in
// `(i, subtreeEnd)` whose `parent` is `i`, so a consumer that cannot build a
// MOVEROTATE still plays that node's children.
//
// The source list is depth-ordered rather than a tree — `effect-egp.js` returns
// a flat array carrying `depth` — so parents are reconstructed with a depth
// stack. Pre-order means a subtree is contiguous even though direct children are
// not, which is why `subtreeEnd` is recorded and a child COUNT is not.
// EFFECTS-SCOPE.md measured the payoff — with six types built, all 4,272 effects
// load, 76.1% render every node and the rest render 95.5% of theirs.
//
// Field values are written in the layout's own field order rather than by name,
// with the name table alongside, so the importer needs no per-type switch to
// READ them — only to decide what to build.
const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc.js');
const egp = require('./effect-egp.js');
const props = require('./effect-props.js');

const base = path.resolve(__dirname, '../../..');
const RAN = path.join(base, 'Ran');

class Pool {
  constructor() { this.map = new Map(); this.parts = []; this.len = 0; }
  add(s) {
    s = s || '';
    if (this.map.has(s)) return this.map.get(s);
    const off = this.len;
    const buf = Buffer.from(s + '\0', 'latin1');
    this.parts.push(buf);
    this.len += buf.length;
    this.map.set(s, off);
    return off;
  }
  buffer() { return Buffer.concat(this.parts, this.len); }
}

/**
 * Reconstruct parent links from the depth-ordered node list.
 *
 * `effect-egp.js` emits pre-order with a `depth` per node and no explicit
 * links, which is exactly enough: the parent of a node at depth d is the most
 * recent node at depth d-1. `subtreeEnd` then falls out as the next node at
 * depth <= d.
 */
function linkNodes(list) {
  const nodes = list.map((n) => ({ n, parent: -1, depth: n.depth | 0, subtreeEnd: 0 }));
  const stack = [];
  for (let i = 0; i < nodes.length; i++) {
    const d = nodes[i].depth;
    stack.length = d;
    nodes[i].parent = d > 0 && stack[d - 1] !== undefined ? stack[d - 1] : -1;
    stack[d] = i;
  }
  for (let i = 0; i < nodes.length; i++) {
    let e = i + 1;
    while (e < nodes.length && nodes[e].depth > nodes[i].depth) e++;
    nodes[i].subtreeEnd = e;
  }
  return nodes;
}

function build(parsed) {
  const flat = linkNodes(parsed.nodes || []);
  const pool = new Pool();
  const floats = [];
  const ints = [];
  const nameOffsets = [];
  const records = [];
  let decoded = 0;

  for (const slot of flat) {
    const node = slot.n;
    const rec = {
      typeId: node.typeId, version: node.ver,
      parent: slot.parent, depth: slot.depth, subtreeEnd: slot.subtreeEnd,
      // The node itself is unnamed; the type name is the useful label and the
      // importer switches on typeId anyway, so this records the type.
      name: pool.add(node.type || ''),
      flags: 0,
      floatOffset: floats.length, intOffset: ints.length, nameOffset: nameOffsets.length,
      floatCount: 0, intCount: 0, nameCount: 0,
      matrix: null, timing: [0, 0, 0],
    };

    let d = null;
    try { d = props.decode(node); } catch { d = null; }
    if (d) {
      decoded++;
      rec.matrix = d.prefix.matLocal;
      rec.timing = [d.prefix.bMoveObj, d.prefix.fGBeginTime, d.prefix.fGLifeTime];
      // Written in the layout's own field order, classified by the probe's
      // measured KIND rather than by inspecting the value: an integer-valued
      // float would otherwise land in the int stream and shift every field
      // after it.
      for (const f of props.specsFor(d.struct)) {
        const v = d.props[f.name];
        if (v === undefined) continue;
        if (f.kind === 'string') { nameOffsets.push(pool.add(String(v))); rec.nameCount++; }
        else if (f.kind === 'f32') {
          for (const x of (Array.isArray(v) ? v : [v])) { floats.push(x); rec.floatCount++; }
        } else {
          for (const x of (Array.isArray(v) ? v : [v])) { ints.push(x); rec.intCount++; }
        }
      }
    }
    if (!rec.matrix) rec.matrix = [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1];
    records.push(rec);
  }

  const strings = pool.buffer();
  const head = Buffer.alloc(32);
  head.write('REFF', 0, 'latin1');
  head.writeUInt32LE(1, 4);
  head.writeUInt32LE(records.length, 8);
  head.writeUInt32LE(decoded, 12);
  head.writeUInt32LE(strings.length, 16);
  head.writeUInt32LE(floats.length, 20);
  head.writeUInt32LE(ints.length, 24);

  const nodeTable = Buffer.alloc(records.length * 48);
  const matTable = Buffer.alloc(records.length * 64);
  const timeTable = Buffer.alloc(records.length * 12);
  records.forEach((r, i) => {
    const o = i * 48;
    nodeTable.writeUInt32LE(r.typeId, o);
    nodeTable.writeUInt32LE(r.version, o + 4);
    nodeTable.writeInt32LE(r.parent, o + 8);
    nodeTable.writeUInt32LE(r.depth, o + 12);
    nodeTable.writeUInt32LE(r.subtreeEnd, o + 16);
    nodeTable.writeUInt32LE(r.name, o + 20);
    nodeTable.writeUInt32LE(r.flags, o + 24);
    nodeTable.writeUInt32LE(r.floatOffset, o + 28);
    nodeTable.writeUInt32LE(r.floatCount, o + 32);
    nodeTable.writeUInt32LE(r.intOffset, o + 36);
    nodeTable.writeUInt32LE(r.intCount, o + 40);
    nodeTable.writeUInt32LE(r.nameOffset, o + 44);
    for (let k = 0; k < 16; k++) matTable.writeFloatLE(r.matrix[k], i * 64 + k * 4);
    timeTable.writeInt32LE(r.timing[0] | 0, i * 12);
    timeTable.writeFloatLE(r.timing[1], i * 12 + 4);
    timeTable.writeFloatLE(r.timing[2], i * 12 + 8);
  });

  const floatBuf = Buffer.alloc(floats.length * 4);
  floats.forEach((v, i) => floatBuf.writeFloatLE(Number.isFinite(v) ? v : 0, i * 4));
  const intBuf = Buffer.alloc(ints.length * 4);
  ints.forEach((v, i) => intBuf.writeInt32LE(v | 0, i * 4));
  const nameBuf = Buffer.alloc(nameOffsets.length * 4);
  nameOffsets.forEach((v, i) => nameBuf.writeUInt32LE(v, i * 4));

  return {
    data: Buffer.concat([head, strings, nodeTable, matTable, timeTable,
                         floatBuf, intBuf, nameBuf]),
    nodes: records.length, decoded, floats: floats.length, ints: ints.length,
    names: nameOffsets.length,
  };
}

// ---------------------------------------------------------------------------

const outArg = process.argv.indexOf('--out');
const outDir = outArg > 0 ? process.argv[outArg + 1] : null;
if (outDir) fs.mkdirSync(path.join(base, outDir), { recursive: true });

const stats = { files: 0, ok: 0, failed: 0, nodes: 0, decoded: 0, bytes: 0, src: 0 };
const typeHist = {};
const failures = [];

function handle(name, raw) {
  if (!/\.egp$/i.test(name)) return;
  stats.files++;
  stats.src += raw.length;
  let parsed;
  try { parsed = egp.parse(raw); }
  catch (e) { stats.failed++; failures.push(`${name}: ${e.message}`); return; }

  let built;
  try { built = build(parsed); }
  catch (e) { stats.failed++; failures.push(`${name}: build ${e.message}`); return; }

  stats.ok++;
  stats.nodes += built.nodes;
  stats.decoded += built.decoded;
  stats.bytes += built.data.length;
  for (const r of parsed.nodes || []) {
    const t = r.type || r.typeId;
    typeHist[t] = (typeHist[t] || 0) + 1;
  }

  if (outDir) {
    fs.writeFileSync(path.join(base, outDir, path.basename(name, path.extname(name)) + '.reff'),
                     built.data);
  }
}

function walk(dir) {
  for (const it of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, it.name);
    if (it.isDirectory()) { walk(p); continue; }
    if (/\.rcc$/i.test(it.name)) {
      let arc;
      try { arc = new RccArchive(p); } catch { continue; }
      for (const e of arc.entries) {
        if (!/\.egp$/i.test(e.name)) continue;
        try { handle(e.name, arc.read(e)); }
        catch (err) { stats.failed++; failures.push(`${e.name}: ${err.message}`); }
      }
    } else if (/\.egp$/i.test(it.name)) {
      try { handle(it.name, fs.readFileSync(p)); } catch { stats.failed++; }
    }
  }
}

const t0 = Date.now();
walk(RAN);
const secs = ((Date.now() - t0) / 1000).toFixed(1);

const fmt = (n) => n.toLocaleString('en-US');
console.log(`${fmt(stats.files)} .egp: ${fmt(stats.ok)} written, ${stats.failed} failed`);
console.log(`${fmt(stats.nodes)} nodes, ${fmt(stats.decoded)} with decoded fields ` +
            `(${(stats.decoded / Math.max(stats.nodes, 1) * 100).toFixed(1)}%)`);
console.log(`${(stats.src / 1048576).toFixed(1)} MB source -> ` +
            `${(stats.bytes / 1048576).toFixed(1)} MB in ${secs}s`);

const top = Object.entries(typeHist).sort((a, b) => b[1] - a[1]);
console.log('node types: ' + top.map(([k, v]) => `${k} ${fmt(v)}`).join('  ·  '));
if (failures.length) {
  console.log(`\nfirst failures:`);
  for (const f of failures.slice(0, 8)) console.log('  ! ' + f);
}
