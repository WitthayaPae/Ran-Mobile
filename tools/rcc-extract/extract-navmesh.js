'use strict';
//
// Extract navigation meshes from every `.wld` in the deploy tree.
//
//   node extract-navmesh.js --list            inventory, nothing written
//   node extract-navmesh.js --out DIR         one .navmesh per map
//   node extract-navmesh.js --out DIR --json  human-readable instead
//
// Output is a compact binary by default. 1.6M vertices and 1.37M cells across
// the set is far too much to want as JSON in a build pipeline, and the binary
// maps straight onto typed arrays at load:
//
//   magic   "RNAV"        4
//   u32     version       1
//   u32     vertexCount
//   u32     cellCount
//   f32[3]  * vertexCount
//   u32[3]  * cellCount   corner indices
//   i32[3]  * cellCount   neighbour cell ids, -1 = wall
//
const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc');
const W = require('./wld');
const N = require('./navmesh');

const RAN = path.join(__dirname, '..', '..', '..', 'Ran');
const argv = process.argv.slice(2);
const has = (f) => argv.includes(f);
const val = (f, d) => { const i = argv.indexOf(f); return i === -1 ? d : argv[i + 1]; };

const outDir = val('--out', null);
const listOnly = has('--list');
const asJson = has('--json');

if (!outDir && !listOnly) {
  console.error('usage: node extract-navmesh.js --list | --out DIR [--json]');
  process.exit(2);
}

function encodeBinary(mesh) {
  const head = Buffer.alloc(16);
  head.write('RNAV', 0, 'latin1');
  head.writeUInt32LE(1, 4);
  head.writeUInt32LE(mesh.vertexCount, 8);
  head.writeUInt32LE(mesh.cellCount, 12);
  return Buffer.concat([
    head,
    Buffer.from(mesh.vertices.buffer, mesh.vertices.byteOffset, mesh.vertices.byteLength),
    Buffer.from(mesh.corners.buffer, mesh.corners.byteOffset, mesh.corners.byteLength),
    Buffer.from(mesh.links.buffer, mesh.links.byteOffset, mesh.links.byteLength),
  ]);
}

const stats = { seen: 0, opened: 0, withMesh: 0, empty: 0, failed: 0,
                vertices: 0, cells: 0, bytes: 0, encrypted: 0, unsupported: 0 };
const rows = [];
const failures = [];
// The same map ships both loose and inside Map.rcc; keep the first and skip the
// rest so counts are per-map rather than per-copy.
const seenNames = new Set();

function handle(name, raw) {
  if (!name.toLowerCase().endsWith('.wld')) return;
  const key = path.basename(name).toLowerCase();
  if (seenNames.has(key)) return;
  stats.seen++;

  let wld;
  try {
    wld = W.open(raw);
  } catch (err) {
    stats.failed++;
    failures.push(`${name}: ${err.message}`);
    return;
  }
  seenNames.add(key);
  stats.opened++;
  if (wld.encrypted) stats.encrypted++;
  if (!W.isSupportedVersion(wld.version)) stats.unsupported++;

  let mesh;
  try {
    mesh = N.parse(wld.buf, W.navmeshOffset(wld));
  } catch (err) {
    stats.failed++;
    failures.push(`${name}: ${err.message}`);
    return;
  }

  if (!mesh) {
    stats.empty++;
    rows.push({ name: key, version: wld.version, enc: wld.encrypted, v: 0, c: 0 });
    return;
  }

  const problems = N.validate(mesh);
  if (problems.length) {
    stats.failed++;
    failures.push(`${name}: ${problems[0]}`);
    return;
  }

  stats.withMesh++;
  stats.vertices += mesh.vertexCount;
  stats.cells += mesh.cellCount;
  rows.push({ name: key, version: wld.version, enc: wld.encrypted,
              v: mesh.vertexCount, c: mesh.cellCount });

  if (!listOnly) {
    const base = path.basename(key, '.wld');
    fs.mkdirSync(outDir, { recursive: true });
    let data;
    let ext;
    if (asJson) {
      const b = N.bounds(mesh);
      data = Buffer.from(JSON.stringify({
        map: base, version: wld.version, bounds: b,
        vertexCount: mesh.vertexCount, cellCount: mesh.cellCount,
        vertices: Array.from(mesh.vertices),
        corners: Array.from(mesh.corners),
        links: Array.from(mesh.links),
      }));
      ext = '.navmesh.json';
    } else {
      data = encodeBinary(mesh);
      ext = '.navmesh';
    }
    fs.writeFileSync(path.join(outDir, base + ext), data);
    stats.bytes += data.length;
  }
}

const t0 = Date.now();
(function walk(dir) {
  let items;
  try { items = fs.readdirSync(dir, { withFileTypes: true }); } catch { return; }
  for (const it of items) {
    const p = path.join(dir, it.name);
    if (it.isDirectory()) { walk(p); continue; }
    if (it.name.toLowerCase().endsWith('.rcc')) {
      let ar;
      try { ar = new RccArchive(p); } catch { continue; }
      for (const e of ar.entries) {
        if (!e.name.toLowerCase().endsWith('.wld')) continue;
        try { handle(e.name, ar.read(e)); } catch { stats.failed++; }
      }
    } else if (it.name.toLowerCase().endsWith('.wld')) {
      try { handle(it.name, fs.readFileSync(p)); } catch { stats.failed++; }
    }
  }
})(RAN);

if (listOnly) {
  console.log('   verts     cells  ver     map');
  console.log('--------  --------  -----   ----------------------------------');
  for (const r of rows.sort((a, b) => b.c - a.c).slice(0, 25)) {
    console.log(`${String(r.v).padStart(8)}  ${String(r.c).padStart(8)}  ` +
                `0x${r.version.toString(16).padStart(4, '0')}${r.enc ? '*' : ' '}  ${r.name}`);
  }
  if (rows.length > 25) console.log(`... and ${rows.length - 25} more`);
  console.log('\n(* = encrypted "Land.Man" variant)');
}

console.log(`\n${stats.seen} maps: ${stats.withMesh} with a navmesh, ` +
            `${stats.empty} without, ${stats.failed} failed`);
console.log(`${stats.encrypted} encrypted, ${stats.unsupported} on a version ` +
            `the game itself would reject`);
console.log(`${stats.vertices.toLocaleString()} vertices, ` +
            `${stats.cells.toLocaleString()} cells` +
            (listOnly ? '' : `, ${(stats.bytes / 1048576).toFixed(1)}M written`) +
            ` in ${((Date.now() - t0) / 1000).toFixed(1)}s`);
if (failures.length) {
  console.log('\nfailures:');
  for (const f of failures.slice(0, 10)) console.log(`  ! ${f}`);
}
process.exit(0);
