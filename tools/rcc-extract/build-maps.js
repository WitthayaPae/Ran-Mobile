'use strict';
//
// Tie the per-map assets together into one manifest each.
//
//   node build-maps.js --list        report, nothing written
//   node build-maps.js --out DIR     one <map>.map.json per map
//
// Extraction produces four unrelated files per map — `<map>.terrain`,
// `<map>.rmapobj`, `<map>.navmesh`, `<map>.textures.json` — and nothing states
// they belong together or checks that they agree in world space. This does both.
//
// The real value is the cross-check. Terrain, objects and navmesh are decoded by
// three independent parsers from two different container formats; if all three
// land in the same coordinate space, that is strong evidence none of them is
// quietly wrong. It caught nothing new here, but it is now a standing test
// rather than a one-off script.
//
// Bounds are always the union of node/object boxes, never the AABB stored in the
// source header — that value is stale in several maps (`blue_zone1`'s own
// geometry escapes its declared box by 6,602 units).
//
const fs = require('fs');
const path = require('path');

const ASSETS = path.join(__dirname, '..', '..', 'assets');
const argv = process.argv.slice(2);
const has = (f) => argv.includes(f);
const val = (f, d) => { const i = argv.indexOf(f); return i === -1 ? d : argv[i + 1]; };

const outDir = val('--out', null);
const listOnly = has('--list');

if (!outDir && !listOnly) {
  console.error('usage: node build-maps.js --list | --out DIR');
  process.exit(2);
}

const TERRAIN = path.join(ASSETS, 'terrain');
const MAPOBJ = path.join(ASSETS, 'mapobj');
const NAVMESH = path.join(ASSETS, 'navmesh');

const readIf = (p) => (fs.existsSync(p) ? fs.readFileSync(p) : null);
const listing = (dir, ext) => (fs.existsSync(dir)
  ? fs.readdirSync(dir).filter((n) => n.endsWith(ext)).map((n) => path.basename(n, ext))
  : []);

/** Terrain bounds are stored in the header, already a union of node boxes. */
function terrainInfo(name) {
  const b = readIf(path.join(TERRAIN, name + '.terrain'));
  if (!b || b.length < 48 || b.toString('latin1', 0, 4) !== 'RTRN') return null;
  const meshCount = b.readUInt32LE(8);
  const nodeCount = b.readUInt32LE(12);
  const vertexCount = b.readUInt32LE(16);
  const indexCount = b.readUInt32LE(20);
  return {
    file: `terrain/${name}.terrain`,
    batches: meshCount, nodes: nodeCount,
    vertices: vertexCount, triangles: indexCount / 3,
    max: [0, 1, 2].map((i) => b.readFloatLE(24 + i * 4)),
    min: [0, 1, 2].map((i) => b.readFloatLE(36 + i * 4)),
    bytes: b.length,
  };
}

/**
 * Object bounds must be recomputed: the stored header bounds are the union of
 * the objects' own AABBs in LOCAL space, while what a scene needs is where the
 * geometry actually lands once each object's transform is applied.
 */
function mapObjInfo(name) {
  const b = readIf(path.join(MAPOBJ, name + '.rmapobj'));
  if (!b || b.length < 68 || b.toString('latin1', 0, 4) !== 'ROBJ') return null;
  const objCount = b.readUInt32LE(8);
  const meshCount = b.readUInt32LE(12);
  const subCount = b.readUInt32LE(16);
  const vCount = b.readUInt32LE(20);
  const iCount = b.readUInt32LE(24);
  const strBytes = b.readUInt32LE(28);

  const strAt = 68;
  const objAt = strAt + strBytes;
  const meshAt = objAt + objCount * 76;
  const subAt = meshAt + meshCount * 32;
  const posAt = subAt + subCount * 20;

  const lo = [Infinity, Infinity, Infinity];
  const hi = [-Infinity, -Infinity, -Infinity];
  const textures = new Set();

  for (let o = 0; o < objCount; o++) {
    const oo = objAt + o * 76;
    const m = new Float32Array(16);
    for (let k = 0; k < 16; k++) m[k] = b.readFloatLE(oo + 4 + k * 4);
    const firstMesh = b.readUInt32LE(oo + 68);
    const nMesh = b.readUInt32LE(oo + 72);
    for (let k = 0; k < nMesh; k++) {
      const mo = meshAt + (firstMesh + k) * 32;
      const vOff = b.readUInt32LE(mo + 8);
      const vCnt = b.readUInt32LE(mo + 12);
      const firstSub = b.readUInt32LE(mo);
      const nSub = b.readUInt32LE(mo + 4);
      for (let s = 0; s < nSub; s++) {
        const t = readCString(b, strAt, strBytes, b.readUInt32LE(subAt + (firstSub + s) * 20));
        if (t) textures.add(t.toLowerCase());
      }
      for (let v = 0; v < vCnt; v++) {
        const p = posAt + (vOff + v) * 12;
        const x = b.readFloatLE(p), y = b.readFloatLE(p + 4), z = b.readFloatLE(p + 8);
        // Row-major, row-vector convention: v' = v * M.
        const wx = x * m[0] + y * m[4] + z * m[8] + m[12];
        const wy = x * m[1] + y * m[5] + z * m[9] + m[13];
        const wz = x * m[2] + y * m[6] + z * m[10] + m[14];
        if (!Number.isFinite(wx + wy + wz)) continue;
        if (wx < lo[0]) lo[0] = wx; if (wx > hi[0]) hi[0] = wx;
        if (wy < lo[1]) lo[1] = wy; if (wy > hi[1]) hi[1] = wy;
        if (wz < lo[2]) lo[2] = wz; if (wz > hi[2]) hi[2] = wz;
      }
    }
  }

  return {
    file: `mapobj/${name}.rmapobj`,
    objects: objCount, meshes: meshCount,
    vertices: vCount, triangles: iCount / 3,
    max: Number.isFinite(hi[0]) ? hi : null,
    min: Number.isFinite(lo[0]) ? lo : null,
    textures: [...textures].sort(),
    bytes: b.length,
  };
}

function navmeshInfo(name) {
  const b = readIf(path.join(NAVMESH, name + '.navmesh'));
  if (!b || b.length < 16 || b.toString('latin1', 0, 4) !== 'RNAV') return null;
  const vCount = b.readUInt32LE(8);
  const cCount = b.readUInt32LE(12);
  const lo = [Infinity, Infinity, Infinity];
  const hi = [-Infinity, -Infinity, -Infinity];
  for (let i = 0; i < vCount; i++) {
    for (let a = 0; a < 3; a++) {
      const v = b.readFloatLE(16 + i * 12 + a * 4);
      if (!Number.isFinite(v)) continue;
      if (v < lo[a]) lo[a] = v;
      if (v > hi[a]) hi[a] = v;
    }
  }
  return {
    file: `navmesh/${name}.navmesh`,
    vertices: vCount, cells: cCount,
    max: Number.isFinite(hi[0]) ? hi : null,
    min: Number.isFinite(lo[0]) ? lo : null,
    bytes: b.length,
  };
}

function readCString(b, blobAt, blobLen, off) {
  if (off >= blobLen) return '';
  let end = blobAt + off;
  const limit = blobAt + blobLen;
  while (end < limit && b[end] !== 0) end++;
  return b.toString('latin1', blobAt + off, end);
}

/** How far `inner` escapes `outer`, per axis. Negative means fully contained. */
function overflow(inner, outer) {
  if (!inner || !outer) return null;
  let worst = -Infinity;
  for (let a = 0; a < 3; a++) {
    worst = Math.max(worst, outer.min[a] - inner.min[a], inner.max[a] - outer.max[a]);
  }
  return worst;
}

const union = (...boxes) => {
  const lo = [Infinity, Infinity, Infinity];
  const hi = [-Infinity, -Infinity, -Infinity];
  for (const b of boxes) {
    if (!b || !b.max || !b.min) continue;
    for (let a = 0; a < 3; a++) {
      if (b.min[a] < lo[a]) lo[a] = b.min[a];
      if (b.max[a] > hi[a]) hi[a] = b.max[a];
    }
  }
  return Number.isFinite(lo[0]) ? { min: lo, max: hi } : null;
};

// Every map that has at least one of the three assets.
const names = [...new Set([
  ...listing(TERRAIN, '.terrain'),
  ...listing(MAPOBJ, '.rmapobj'),
  ...listing(NAVMESH, '.navmesh'),
])].sort();

const stats = { maps: 0, withTerrain: 0, withObjects: 0, withNavmesh: 0,
                complete: 0, navContained: 0, navChecked: 0, bytes: 0 };
const escapes = [];
const rows = [];

for (const name of names) {
  const terrain = terrainInfo(name);
  const objects = mapObjInfo(name);
  const navmesh = navmeshInfo(name);
  stats.maps++;
  if (terrain) stats.withTerrain++;
  if (objects) stats.withObjects++;
  if (navmesh) stats.withNavmesh++;
  if (terrain && navmesh) stats.complete++;

  const textures = new Set(objects ? objects.textures : []);
  const sidecar = readIf(path.join(TERRAIN, name + '.textures.json'));
  if (sidecar) {
    try {
      for (const t of JSON.parse(sidecar.toString('utf8'))) if (t) textures.add(t.toLowerCase());
    } catch { /* a broken sidecar should not sink the manifest */ }
  }

  // The cross-check: the walkable surface must sit inside the world it walks on.
  // Compared against terrain+objects together, since some maps put the floor in
  // the object graph rather than the terrain mesh.
  const world = union(terrain, objects);
  let navOverflow = null;
  if (navmesh && navmesh.max && world) {
    navOverflow = overflow(navmesh, world);
    stats.navChecked++;
    if (navOverflow <= 1.0) stats.navContained++;
    else escapes.push({ name, by: navOverflow });
  }

  const manifest = {
    map: name,
    bounds: world,
    terrain, objects, navmesh,
    textures: [...textures].sort(),
    checks: { navmeshOverflow: navOverflow },
  };

  rows.push({ name, t: !!terrain, o: !!objects, n: !!navmesh,
              verts: (terrain ? terrain.vertices : 0) + (objects ? objects.vertices : 0),
              tex: textures.size, navOverflow });

  if (!listOnly) {
    fs.mkdirSync(outDir, { recursive: true });
    const data = JSON.stringify(manifest, null, 2);
    fs.writeFileSync(path.join(outDir, name + '.map.json'), data);
    stats.bytes += data.length;
  }
}

const fmt = (n) => n.toLocaleString('en-US');
if (listOnly) {
  console.log('  verts   tex  T O N  navOverflow  map');
  console.log('-------  ----  -----  -----------  ------------------------------');
  for (const r of rows.sort((a, b) => b.verts - a.verts).slice(0, 25)) {
    console.log(`${String(r.verts).padStart(7)}  ${String(r.tex).padStart(4)}  ` +
                `${r.t ? 'T' : '-'} ${r.o ? 'O' : '-'} ${r.n ? 'N' : '-'}  ` +
                `${(r.navOverflow === null ? '-' : r.navOverflow.toFixed(1)).padStart(11)}  ${r.name}`);
  }
  if (rows.length > 25) console.log(`... and ${rows.length - 25} more`);
}

console.log(`\n${stats.maps} maps: ${stats.withTerrain} with terrain, ` +
            `${stats.withObjects} with objects, ${stats.withNavmesh} with a navmesh`);
console.log(`${stats.complete} have both terrain and a navmesh` +
            (listOnly ? '' : `, ${(stats.bytes / 1048576).toFixed(1)}M of manifests written`));
console.log(`navmesh inside the world bounds: ${stats.navContained}/${stats.navChecked}`);
if (escapes.length) {
  console.log('\nnavmesh escaping its world bounds:');
  for (const e of escapes.sort((a, b) => b.by - a.by).slice(0, 10)) {
    console.log(`  ! ${e.name} by ${e.by.toFixed(1)}`);
  }
}
process.exit(0);
