'use strict';
// Inventory of the shipped `.egp` effect definitions: versions, prop types,
// tree shapes and how far the walk gets. Throwaway analysis for EFFECTS-SCOPE.md.
const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc');
const E = require('./effect-egp');

const RAN = path.join(__dirname, '..', '..', '..', 'Ran');
const SOURCES = [
  ['data/effect/Effect.rcc', 'Effect.rcc'],
  ['data/effect/char/EffectChar.rcc', 'EffectChar.rcc'],
];

function collect() {
  const files = [];
  for (const [rel, label] of SOURCES) {
    const p = path.join(RAN, rel);
    if (!fs.existsSync(p)) continue;
    const ar = new RccArchive(p);
    for (const e of ar.entries) {
      if (!e.name.toLowerCase().endsWith('.egp')) continue;
      files.push({ archive: label, name: e.name, buf: ar.read(e) });
    }
  }
  // Loose .egp on disk, if any.
  for (const rel of ['data/effect', 'data/effect/char']) {
    const dir = path.join(RAN, rel);
    let names = [];
    try { names = fs.readdirSync(dir); } catch { continue; }
    for (const n of names) {
      if (!n.toLowerCase().endsWith('.egp')) continue;
      files.push({ archive: `loose:${rel}`, name: n, buf: fs.readFileSync(path.join(dir, n)) });
    }
  }
  return files;
}

const files = collect();
const byArchive = new Map();
const verCount = new Map();
const typeCount = new Map();          // node instances
const typeFiles = new Map();          // distinct files using a type
const typeVerCount = new Map();       // "TYPE 0xNNNN" -> node count
const typeSize = new Map();           // "TYPE 0xNNNN" -> Set of body sizes
const nodeCountHist = new Map();
const depthHist = new Map();
const failures = [];
const results = [];
let bytes = 0;

for (const f of files) {
  bytes += f.buf.length;
  byArchive.set(f.archive, (byArchive.get(f.archive) || 0) + 1);
  const res = E.parse(f.buf, f.name);
  res.archive = f.archive;
  res.bytes = f.buf.length;
  results.push(res);
  const vk = `0x${(res.ver >>> 0).toString(16).padStart(4, '0')}`;
  verCount.set(vk, (verCount.get(vk) || 0) + 1);
  if (!res.ok) failures.push(res);
  if (!res.nodes) continue;
  const seen = new Set();
  let maxDepth = 0;
  for (const n of res.nodes) {
    typeCount.set(n.type, (typeCount.get(n.type) || 0) + 1);
    const tv = `${n.type} 0x${n.ver.toString(16).padStart(4, '0')}`;
    typeVerCount.set(tv, (typeVerCount.get(tv) || 0) + 1);
    if (!typeSize.has(tv)) typeSize.set(tv, new Set());
    typeSize.get(tv).add(n.size);
    seen.add(n.type);
    if (n.depth > maxDepth) maxDepth = n.depth;
  }
  for (const t of seen) typeFiles.set(t, (typeFiles.get(t) || 0) + 1);
  if (res.ok) {
    const b = res.nodes.length;
    nodeCountHist.set(b, (nodeCountHist.get(b) || 0) + 1);
    depthHist.set(maxDepth, (depthHist.get(maxDepth) || 0) + 1);
  }
}

const pct = (n, d) => `${((n / d) * 100).toFixed(1)}%`;
const sortDesc = (m) => [...m.entries()].sort((a, b) => b[1] - a[1]);

console.log(`.egp files: ${files.length}, ${(bytes / 1048576).toFixed(1)} MB`);
for (const [a, n] of sortDesc(byArchive)) console.log(`  ${a.padEnd(18)} ${n}`);

console.log(`\nInner version (byte 132):`);
for (const [v, n] of [...verCount.entries()].sort()) {
  console.log(`  ${v}  ${String(n).padStart(5)}  ${pct(n, files.length)}`);
}

const okCount = results.filter((r) => r.ok).length;
console.log(`\nWalked to exact EOF: ${okCount} / ${files.length} (${pct(okCount, files.length)})`);
if (failures.length) {
  const reasons = new Map();
  for (const f of failures) {
    const key = f.error.replace(/\d+/g, 'N');
    reasons.set(key, (reasons.get(key) || 0) + 1);
  }
  console.log('  failure modes:');
  for (const [k, n] of sortDesc(reasons)) console.log(`    ${String(n).padStart(5)}  ${k}`);
  const byVer = new Map();
  for (const f of failures) {
    const v = `0x${(f.ver >>> 0).toString(16).padStart(4, '0')}`;
    byVer.set(v, (byVer.get(v) || 0) + 1);
  }
  console.log('  by group version:');
  for (const [v, n] of [...byVer.entries()].sort()) console.log(`    ${v}  ${n}`);
  console.log('  examples:');
  for (const f of failures.slice(0, 12)) {
    console.log(`    ${f.name.padEnd(34)} v0x${(f.ver >>> 0).toString(16)} ${f.error}`);
  }
}

const totalNodes = [...typeCount.values()].reduce((a, b) => a + b, 0);
console.log(`\nProperty nodes: ${totalNodes} (includes the nodes recovered from the ${files.length - okCount} files that do not walk cleanly)`);
console.log(`  ${'type'.padEnd(14)} ${'nodes'.padStart(7)} ${'share'.padStart(7)}  ${'files'.padStart(6)} ${'share'.padStart(7)}`);
for (const [t, n] of sortDesc(typeCount)) {
  const fcount = typeFiles.get(t) || 0;
  console.log(`  ${t.padEnd(14)} ${String(n).padStart(7)} ${pct(n, totalNodes).padStart(7)}  ${String(fcount).padStart(6)} ${pct(fcount, files.length).padStart(7)}`);
}

console.log(`\nProperty node versions (and blitted body sizes):`);
for (const [tv, n] of sortDesc(typeVerCount)) {
  const sizes = [...typeSize.get(tv)].sort((a, b) => a - b);
  console.log(`  ${tv.padEnd(22)} ${String(n).padStart(7)}  size ${sizes.join(',')}`);
}

console.log(`\nNodes per file:`);
for (const [k, n] of [...nodeCountHist.entries()].sort((a, b) => a[0] - b[0])) {
  console.log(`  ${String(k).padStart(3)} nodes  ${String(n).padStart(5)} files`);
}
console.log(`Max tree depth (0 = flat sibling chain):`);
for (const [k, n] of [...depthHist.entries()].sort((a, b) => a[0] - b[0])) {
  console.log(`  depth ${String(k).padStart(2)}  ${String(n).padStart(5)} files`);
}

if (process.argv.includes('--json')) {
  const out = process.argv[process.argv.indexOf('--json') + 1];
  fs.writeFileSync(out, JSON.stringify(results.map((r) => ({
    name: r.name, archive: r.archive, ver: r.ver, ok: r.ok, error: r.error,
    bytes: r.bytes,
    nodes: (r.nodes || []).map((n) => ({ type: n.type, ver: n.ver, size: n.size, depth: n.depth, sound: n.sound })),
  })), null, 1));
  console.log(`\nwrote ${out}`);
}
