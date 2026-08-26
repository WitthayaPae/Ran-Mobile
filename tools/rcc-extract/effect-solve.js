'use strict';
//
// The declared `dwSize` on a property node is only trustworthy on versions the
// engine does NOT know — exactly the rule already found for `.wld0`. On known
// versions the engine parses structurally, and several old writers left sizes
// that disagree with the structural layout (e.g. SEQUENCE 0x0101 declares 568
// and occupies 572, because the current loader blits today's 460-byte PROPERTY
// over a body written when it was 456).
//
// Rather than hand-computing sizeof(PROPERTY) for 60+ (type,version) branches,
// this MEASURES the correction: for every node it tries a small set of
// candidate deltas, keeps only the walks that consume the file to the exact
// last byte, and then reports the delta each (type,version) actually needed.
// A delta that is the same in every file that contains that (type,version) is
// evidence; one that varies is reported as ambiguous and not claimed.
//
const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc');
const B = require('./bytecrypt');
const E = require('./effect-egp');

const RAN = path.join(__dirname, '..', '..', '..', 'Ran');
// 0 first, so an unambiguous file keeps the declared size.
const DELTAS = [0, 4, 36, 40, -4, 32, 8, 12, 72, 76];
const MAX_CANDS = 8;
// (type, node version) combinations already proven to need no correction,
// because a file containing them walked to the exact last byte with the
// declared size. Fixing those collapses the search space by orders of
// magnitude — without it the backtracking is exponential in nodes per file.
// NOT a constraint: the same (type,version) demonstrably occupies different
// numbers of bytes in different files, so pass-1 evidence only orders the
// candidates, it never removes them.
const PREFER_ZERO = new Set();
// Learned corrections, keyed the same way. Filled by the fixpoint loop below
// from files whose walk had exactly ONE solution, then fed back so that the
// files with several old-version nodes have only one free variable left.
const KNOWN = new Map();

function readSoundStrict(b, p) {
  if (p + 12 > b.length) return null;
  const movVer = b.readUInt16LE(p);
  const sndVer = b.readUInt16LE(p + 2);
  if (movVer > 1000 || sndVer > 1000) return null;
  const minR = b.readFloatLE(p + 4);
  const maxR = b.readFloatLE(p + 8);
  if (!Number.isFinite(minR) || !Number.isFinite(maxR)) return null;
  if (Math.abs(minR) > 1e7 || Math.abs(maxR) > 1e7) return null;
  p += 12;
  let name = '';
  if (sndVer === 100) {
    if (p + 256 + 2 + 4 > b.length) return null;
    const raw = b.subarray(p, p + 256);
    const nul = raw.indexOf(0);
    name = raw.toString('latin1', 0, nul === -1 ? 256 : nul);
    p += 256 + 2;
    const loop = b.readUInt32LE(p); p += 4;
    if (loop > 1) return null;
  } else {
    if (p + 4 > b.length) return null;
    const n = b.readInt32LE(p); p += 4;
    if (n < 0 || n > 256) return null;
    if (p + n + 4 > b.length) return null;
    if (n) {
      name = b.toString('latin1', p, p + n);
      if (!/^[\x20-\x7e]*$/.test(name)) return null;
      p += n;
    }
    const loop = b.readUInt32LE(p); p += 4;
    if (loop > 1) return null;
  }
  if (!/^[\x20-\x7e]*$/.test(name)) return null;
  return { p, movVer, sndVer, name };
}

/**
 * All plausible end positions for the subtree rooted at a node whose TypeID has
 * already been consumed. Returns [{p, nodes, deltas}].
 */
function nodeCands(b, p0, typeId, depth) {
  if (!E.INSTANTIABLE.has(typeId)) return [];
  if (depth > 64) return [];
  if (p0 + 8 > b.length) return [];
  const ver = b.readUInt32LE(p0);
  const size = b.readUInt32LE(p0 + 4);
  if (size > b.length || ver === 0 || ver > 0x0400) return [];
  const key = `${E.TYPE_NAMES[typeId]} 0x${ver.toString(16).padStart(4, '0')}`;
  const out = [];
  const choices = KNOWN.has(key)
    ? [KNOWN.get(key), ...DELTAS.filter((d) => d !== KNOWN.get(key))]
    : PREFER_ZERO.has(key) ? DELTAS : [...DELTAS.slice(1), 0];
  for (const d of choices) {
    const bodyEnd = p0 + 8 + size + d;
    if (bodyEnd < p0 + 8 || bodyEnd > b.length) continue;
    const snd = readSoundStrict(b, bodyEnd);
    if (!snd) continue;
    let q = snd.p;
    const self = { key, delta: d, type: E.TYPE_NAMES[typeId], ver, size, depth, sound: snd.name,
                   bodyAt: p0 + 8, bodyLen: size + d };
    // sibling
    if (q + 4 > b.length) continue;
    const hasSib = b.readUInt32LE(q); q += 4;
    let sibs = [{ p: q, nodes: [], deltas: [] }];
    if (hasSib === 1) {
      if (q + 4 > b.length) continue;
      const sTid = b.readUInt32LE(q);
      sibs = nodeCands(b, q + 4, sTid, depth).map((c) => c);
    } else if (hasSib !== 0) continue;
    for (const s of sibs) {
      let r = s.p;
      if (r + 4 > b.length) continue;
      const hasCh = b.readUInt32LE(r); r += 4;
      let chs = [{ p: r, nodes: [], deltas: [] }];
      if (hasCh === 1) {
        if (r + 4 > b.length) continue;
        const cTid = b.readUInt32LE(r);
        chs = nodeCands(b, r + 4, cTid, depth + 1);
      } else if (hasCh !== 0) continue;
      for (const c of chs) {
        out.push({
          p: c.p,
          nodes: [self, ...s.nodes, ...c.nodes],
          deltas: [[key, d], ...s.deltas, ...c.deltas],
        });
        if (out.length >= MAX_CANDS) return out;
      }
    }
  }
  return out;
}

function solve(buf, name) {
  const head = B.readHeader(buf);
  if (!head || head.type !== 'EFF_PROPGROUP') return { name, status: 'not-egp', type: head && head.type };
  const ver = buf.readUInt32LE(132);
  const layout = E.GROUP_LAYOUT[ver];
  if (!layout) return { name, ver, status: 'unknown-group-version' };
  let b = buf;
  if (ver >= 0x0200) { b = Buffer.from(buf); B.decode(b, 'EMBYTECRYPT_EGP', 136); }
  let p = 136 + 4 + (layout.length - 1) * 12;
  if (p + 4 > b.length) return { name, ver, status: 'truncated' };
  const hasRoot = b.readUInt32LE(p); p += 4;
  if (hasRoot === 0) return { name, ver, status: 'empty', nodes: [], deltas: [] };
  if (hasRoot !== 1) return { name, ver, status: 'bad-root-flag' };
  const tid = b.readUInt32LE(p); p += 4;
  const cands = nodeCands(b, p, tid, 0).filter((c) => c.p === b.length);
  if (!cands.length) return { name, ver, status: 'no-solution' };
  const distinct = new Set(cands.map((c) => c.deltas.map((d) => d.join('=')).join(',')));
  return {
    name, ver,
    status: distinct.size === 1 ? 'solved' : 'ambiguous',
    variants: distinct.size,
    nodes: cands[0].nodes,
    deltas: cands[0].deltas,
  };
}

// ---- run --------------------------------------------------------------------
const ar = new RccArchive(path.join(RAN, 'data/effect/Effect.rcc'));
const files = ar.entries.filter((e) => e.name.toLowerCase().endsWith('.egp'));
const only = process.argv[2] && !process.argv[2].startsWith('--') ? process.argv[2] : null;

const status = new Map();
const deltaByKey = new Map();       // key -> Map(delta -> count)
const unsolved = [];
const perFile = [];

// ---- pass 1: the plain declared-size walk. Every file it consumes to the
// exact last byte is proof that each (type,version) in it needs no correction.
const targets = files.filter((e) => !only || e.name.toLowerCase() === only.toLowerCase());
const bufs = new Map();
const pass1 = new Map();
for (const e of targets) {
  let buf;
  try { buf = ar.read(e); } catch { continue; }
  bufs.set(e.name, buf);
  const r = E.parse(buf, e.name);
  pass1.set(e.name, r);
  if (!r.ok || !r.nodes) continue;
  for (const n of r.nodes) PREFER_ZERO.add(`${n.type} 0x${n.ver.toString(16).padStart(4, '0')}`);
}
const clean1 = [...pass1.values()].filter((r) => r.ok).length;
console.log(`pass 1 (declared size, no correction): ${clean1} / ${targets.length} walked to exact EOF`);
// Only the files pass 1 could NOT walk need a search.
const search = targets.filter((e) => !pass1.get(e.name) || !pass1.get(e.name).ok);
console.log(`  files needing a size correction: ${search.length}`);

// ---- pass 2+: search a correction only where pass 1 left one open, then feed
// back every correction that came out of a file with a UNIQUE solution and
// solve again. Ambiguous files contribute nothing to the learned table — only
// files with exactly one walk that reaches the last byte do.
let round = 0;
let searched = [];
while (true) {
  round++;
  searched = search.map((e) => solve(bufs.get(e.name), e.name));
  const learned = new Map();          // key -> Map(delta -> count), from unique solutions
  for (const r of searched) {
    if (r.status !== 'solved') continue;
    for (const [key, d] of r.deltas) {
      if (!learned.has(key)) learned.set(key, new Map());
      learned.get(key).set(d, (learned.get(key).get(d) || 0) + 1);
    }
  }
  let added = 0;
  for (const [key, m] of learned) {
    if (KNOWN.has(key)) continue;
    if (m.size !== 1) continue;       // disagreement -> do not claim it
    KNOWN.set(key, [...m.keys()][0]);
    added++;
  }
  const solved = searched.filter((r) => r.status === 'solved').length;
  console.log(`  round ${round}: ${solved} unique-solution files, ${KNOWN.size} corrections learned (+${added})`);
  if (!added || round > 6) break;
}

// Merge: pass-1 clean files (delta 0 throughout) + the searched ones.
const results = targets.map((e) => {
  const p1 = pass1.get(e.name);
  if (p1 && p1.ok) {
    return {
      name: e.name, ver: p1.ver, status: 'solved', variants: 1,
      nodes: p1.nodes.map((n) => ({ key: `${n.type} 0x${n.ver.toString(16).padStart(4, '0')}`, delta: 0,
                                    type: n.type, ver: n.ver, size: n.size, depth: n.depth, sound: n.sound,
                                    bodyAt: 0, bodyLen: n.size })),
      deltas: p1.nodes.map((n) => [`${n.type} 0x${n.ver.toString(16).padStart(4, '0')}`, 0]),
      pass: 1,
    };
  }
  const s = searched[search.indexOf(e)];
  return s ? { ...s, pass: 2 } : { name: e.name, status: 'read-fail' };
});

for (const r of results) {
  perFile.push(r);
  status.set(r.status, (status.get(r.status) || 0) + 1);
  if (r.status !== 'solved') { if (r.status !== 'empty' && r.status !== 'ambiguous') unsolved.push(r); continue; }
  for (const [key, d] of r.deltas) {
    if (!deltaByKey.has(key)) deltaByKey.set(key, new Map());
    const m = deltaByKey.get(key);
    m.set(d, (m.get(d) || 0) + 1);
  }
}

console.log(`\nfiles: ${files.length}`);
for (const [s, n] of [...status.entries()].sort((a, b) => b[1] - a[1])) {
  console.log(`  ${s.padEnd(22)} ${n}`);
}
console.log(`\ndelta needed on top of the declared dwSize, per (type, node version):`);
const rows = [...deltaByKey.entries()].sort((a, b) => {
  const sa = [...a[1].values()].reduce((x, y) => x + y, 0);
  const sb = [...b[1].values()].reduce((x, y) => x + y, 0);
  return sb - sa;
});
for (const [key, m] of rows) {
  const tot = [...m.values()].reduce((x, y) => x + y, 0);
  const parts = [...m.entries()].sort((a, b) => b[1] - a[1]).map(([d, n]) => `${d >= 0 ? '+' : ''}${d}:${n}`);
  const flag = m.size > 1 ? '  <-- NOT CONSTANT' : '';
  console.log(`  ${key.padEnd(22)} ${String(tot).padStart(6)}  ${parts.join('  ')}${flag}`);
}
if (unsolved.length) {
  console.log(`\nunsolved (${unsolved.length}):`);
  for (const u of unsolved.slice(0, 20)) console.log(`  ${u.name.padEnd(34)} v0x${(u.ver >>> 0).toString(16)} ${u.status}`);
}
if (process.argv.includes('--json')) {
  const out = process.argv[process.argv.indexOf('--json') + 1];
  fs.writeFileSync(out, JSON.stringify(perFile, null, 1));
  console.log(`\nwrote ${out}`);
}
