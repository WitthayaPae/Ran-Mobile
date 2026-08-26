'use strict';
//
// Extract terrain geometry from every `.wld0` sidecar in the deploy tree.
//
//   node extract-terrain.js --list            inventory, nothing written
//   node extract-terrain.js --out DIR         one .terrain + one .textures.json per map
//   node extract-terrain.js --out DIR --json  human-readable geometry instead
//   node extract-terrain.js --obj MAP --out DIR   one map as OBJ+MTL, to LOOK at
//
// 15.5M vertices and 10.9M triangles is far too much to want as JSON in a build
// pipeline, so the default is a compact binary that maps straight onto typed
// arrays. Everything is little-endian, matching the source files.
//
//   magic    "RTRN"                   4
//   u32      version = 2
//   u32      meshCount                draw batches, one texture each
//   u32      nodeCount                total across all meshes
//   u32      vertexCount              total, so a loader allocates once
//   u32      indexCount               total
//   f32[6]   map AABB                 vMax then vMin — computed from the node
//                                     boxes, NOT the header value, which is
//                                     stale in several source files
//   -- mesh table, meshCount entries, 16 bytes each --
//   u32      bucket                   0 mesh, 1 alpha, 2 soft, 3 soft01, 4 soft02
//   u32      textureIndex             into the .textures.json array
//   u32      firstNode
//   u32      nodeCount
//   -- node table, nodeCount entries, 40 bytes each --
//   f32[6]   node AABB                vMax then vMin
//   u32      vertexOffset             in vertices, not bytes
//   u32      vertexCount
//   u32      indexOffset              in indices
//   u32      indexCount
//   -- blobs --
//   f32[3] * totalVertices            positions
//   f32[2] * totalVertices            uv0
//   u16     * totalIndices            triangle list, node-local
//   -- v2 colour section (appended, so v1 field offsets are unchanged) --
//   u32      hasColors                0 or 1
//   u32      dayARGB  * totalVertices   only when hasColors=1
//   u32      nightARGB * totalVertices  only when hasColors=1
//
// Indices stay node-local so each node remains an independent draw call under
// the 16-bit index limit, which is what the octree exists for in the first
// place. `vertexOffset` is what a loader adds when merging nodes into one
// buffer.
//
// The day/night colour bake IS the lighting. v1 dropped it as "a PC-era
// day/night blend" on the assumption that the mobile renderer would light
// terrain itself — wrong on both counts. `DxLandMan::Render` draws the whole
// static mesh with `D3DRS_LIGHTING = FALSE` (DxLandMan.cpp:1073), so on PC the
// texture is modulated by exactly these baked vertex colours and nothing else;
// dropping them is why the port's terrain never matched. The engine blends
// `day*BlendFact + night*NightFact` per vertex (NsOCTree.cpp ComputeCOLOR) with
// factors from GLPeriod — and the LOGIN forces hour=1 (DxLobyStage.cpp:467),
// which is night in every season, so the login shows the pure NIGHT set.
// Both sets are written; the importer picks.
//
// The source WORDs (DIRECTPOINTCOLOR, NsOCTree.h:53 — alpha, dayRGB, nightRGB,
// temp) can exceed 255 because the editor bake ADDS light contributions
// (NsOCTree.cpp:327), and the engine clamps at blend time — so they are
// clamped here too. Meshes without a bake (landType != DAYNIGHT_ON) get opaque
// white, which multiplies to "texture unchanged", the same thing D3D shows for
// a mesh whose colours were never rewritten.
//
const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc');
const SM = require('./staticmesh');

const RAN = path.join(__dirname, '..', '..', '..', 'Ran');
const argv = process.argv.slice(2);
const has = (f) => argv.includes(f);
const val = (f, d) => { const i = argv.indexOf(f); return i === -1 ? d : argv[i + 1]; };

const outDir = val('--out', null);
const listOnly = has('--list');
const asJson = has('--json');
const objMap = val('--obj', null);

if (!outDir && !listOnly) {
  console.error('usage: node extract-terrain.js --list | --out DIR [--json] | --obj MAP --out DIR');
  process.exit(2);
}

const BUCKET_ID = { mesh: 0, alpha: 1, softAlpha: 2, softAlpha01: 3, softAlpha02: 4 };

/** Flatten the bucket/mesh/node tree into the draw batches a renderer wants. */
function collect(sm) {
  const textures = [];
  const texIndex = new Map();
  const meshes = [];
  const nodes = [];
  let totalVerts = 0, totalIndices = 0;

  for (const b of sm.buckets) {
    for (const m of b.meshes) {
      if (!m.octree) continue;
      const withGeom = m.octree.nodes.filter((n) => n.geometry && n.geometry.vertexCount);
      if (!withGeom.length) continue;

      // A mesh's own texture plus any its effects pull in (flow/alpha maps).
      const name = m.texName || m.key || '';
      const key = name.toLowerCase();
      if (!texIndex.has(key)) { texIndex.set(key, textures.length); textures.push(name); }
      for (const e of m.effects) {
        for (const t of e.textures) {
          const k = t.toLowerCase();
          if (!texIndex.has(k)) { texIndex.set(k, textures.length); textures.push(t); }
        }
      }

      meshes.push({ bucket: BUCKET_ID[b.name], texture: texIndex.get(key),
                    firstNode: nodes.length, nodeCount: withGeom.length });

      for (const n of withGeom) {
        const g = n.geometry;
        nodes.push({ vMax: n.vMax, vMin: n.vMin, g,
                     vertexOffset: totalVerts, indexOffset: totalIndices });
        totalVerts += g.vertexCount;
        totalIndices += g.indices.length;
      }
    }
  }
  return { textures, meshes, nodes, totalVerts, totalIndices };
}

function encodeBinary(sm, c) {
  const head = Buffer.alloc(24 + 24);
  head.write('RTRN', 0, 'latin1');
  head.writeUInt32LE(2, 4);
  head.writeUInt32LE(c.meshes.length, 8);
  head.writeUInt32LE(c.nodes.length, 12);
  head.writeUInt32LE(c.totalVerts, 16);
  head.writeUInt32LE(c.totalIndices, 20);
  // The map AABB is the union of the node boxes. The value in the source header
  // is stale in several files — blue_zone1's own geometry escapes it by 6,602
  // units — so it is recomputed here and never copied through.
  const lo = [Infinity, Infinity, Infinity];
  const hi = [-Infinity, -Infinity, -Infinity];
  for (const n of c.nodes) {
    for (let a = 0; a < 3; a++) {
      if (n.vMin[a] < lo[a]) lo[a] = n.vMin[a];
      if (n.vMax[a] > hi[a]) hi[a] = n.vMax[a];
    }
  }
  for (let i = 0; i < 3; i++) head.writeFloatLE(hi[i], 24 + i * 4);
  for (let i = 0; i < 3; i++) head.writeFloatLE(lo[i], 36 + i * 4);

  const meshTable = Buffer.alloc(c.meshes.length * 16);
  c.meshes.forEach((m, i) => {
    meshTable.writeUInt32LE(m.bucket, i * 16);
    meshTable.writeUInt32LE(m.texture, i * 16 + 4);
    meshTable.writeUInt32LE(m.firstNode, i * 16 + 8);
    meshTable.writeUInt32LE(m.nodeCount, i * 16 + 12);
  });

  const nodeTable = Buffer.alloc(c.nodes.length * 40);
  const positions = new Float32Array(c.totalVerts * 3);
  const uvs = new Float32Array(c.totalVerts * 2);
  const indices = new Uint16Array(c.totalIndices);

  // Day and night bakes, packed A<<24|R<<16|G<<8|B like D3DCOLOR. Written only
  // when at least one node actually carries a bake — a map with none (all
  // landType PIECE/DAYNIGHT_OFF) stays hasColors=0 rather than shipping two
  // blobs of white.
  const hasColors = c.nodes.some((n) => n.g.dayNightColors);
  const day = hasColors ? new Uint32Array(c.totalVerts) : null;
  const night = hasColors ? new Uint32Array(c.totalVerts) : null;
  if (hasColors) { day.fill(0xffffffff); night.fill(0xffffffff); }

  c.nodes.forEach((n, i) => {
    const o = i * 40;
    for (let a = 0; a < 3; a++) nodeTable.writeFloatLE(n.vMax[a], o + a * 4);
    for (let a = 0; a < 3; a++) nodeTable.writeFloatLE(n.vMin[a], o + 12 + a * 4);
    nodeTable.writeUInt32LE(n.vertexOffset, o + 24);
    nodeTable.writeUInt32LE(n.g.vertexCount, o + 28);
    nodeTable.writeUInt32LE(n.indexOffset, o + 32);
    nodeTable.writeUInt32LE(n.g.indices.length, o + 36);

    positions.set(n.g.positions, n.vertexOffset * 3);
    // Not every FVF carries texcoords; leave those at 0 rather than skewing the
    // stride, so the blob stays a flat vertexCount * 2 array.
    if (n.g.uvs) {
      // A handful of vertices carry NaN texcoords — 8 of 185,041 in
      // w_school_03, all inside untextured meshes, where the editor never
      // initialised memory nothing would sample. Harmless in the source, but a
      // NaN reaching a Unity mesh poisons its bounds and silently breaks
      // culling for the whole batch, so it is flattened here.
      const src = n.g.uvs;
      const base = n.vertexOffset * 2;
      for (let k = 0; k < src.length; k++) {
        const v = src[k];
        uvs[base + k] = Number.isFinite(v) ? v : 0;
      }
    }
    indices.set(n.g.indices, n.indexOffset);

    if (hasColors && n.g.dayNightColors) {
      // DIRECTPOINTCOLOR: 8 WORDs — alpha, dayR, dayG, dayB, nightR, nightG,
      // nightB, temp (NsOCTree.h:53). Clamped like ComputeCOLOR clamps.
      const t = n.g.dayNightColors;
      for (let v = 0; v < n.g.vertexCount; v++) {
        const b16 = v * 16;
        const cl = (x) => (x > 255 ? 255 : x);
        const a = cl(t.readUInt16LE(b16));
        const dr = cl(t.readUInt16LE(b16 + 2));
        const dg = cl(t.readUInt16LE(b16 + 4));
        const db = cl(t.readUInt16LE(b16 + 6));
        const nr = cl(t.readUInt16LE(b16 + 8));
        const ng = cl(t.readUInt16LE(b16 + 10));
        const nb = cl(t.readUInt16LE(b16 + 12));
        day[n.vertexOffset + v] = ((a << 24) | (dr << 16) | (dg << 8) | db) >>> 0;
        night[n.vertexOffset + v] = ((a << 24) | (nr << 16) | (ng << 8) | nb) >>> 0;
      }
    }
  });

  const view = (t) => Buffer.from(t.buffer, t.byteOffset, t.byteLength);
  const colorFlag = Buffer.alloc(4);
  colorFlag.writeUInt32LE(hasColors ? 1 : 0, 0);
  const tail = hasColors ? [colorFlag, view(day), view(night)] : [colorFlag];
  return Buffer.concat([head, meshTable, nodeTable,
                        view(positions), view(uvs), view(indices), ...tail]);
}

/**
 * One map as Wavefront OBJ + MTL.
 *
 * Structural validation says the numbers are self-consistent; it cannot say the
 * map looks like the map. Every other asset class here was checked by eye
 * (textures especially), so terrain gets the same treatment. OBJ is the cheapest
 * format that carries UVs and per-material grouping and opens in anything.
 *
 * Handedness: the engine is left-handed (DirectX), OBJ viewers are right-handed,
 * so X is negated. Without it the map loads mirrored and reads as "fine".
 * V is flipped for the same reason — D3D puts texture origin top-left.
 */
function encodeObj(base, c) {
  const obj = [`# ${base} — RAN EP9 terrain`, `mtllib ${base}.mtl`];
  const mtl = [];
  const written = new Set();
  const vBase = 1;                      // OBJ indices are 1-based and global

  // Positions and UVs first, in node order, so face indices are a running sum.
  for (const n of c.nodes) {
    const g = n.g;
    for (let i = 0; i < g.vertexCount; i++) {
      obj.push(`v ${-g.positions[i * 3]} ${g.positions[i * 3 + 1]} ${g.positions[i * 3 + 2]}`);
    }
  }
  for (const n of c.nodes) {
    const g = n.g;
    for (let i = 0; i < g.vertexCount; i++) {
      obj.push(g.uvs ? `vt ${g.uvs[i * 2]} ${1 - g.uvs[i * 2 + 1]}` : 'vt 0 0');
    }
  }

  for (const m of c.meshes) {
    const tex = c.textures[m.texture] || '';
    const name = tex.replace(/[^A-Za-z0-9_.-]/g, '_') || 'untextured';
    if (!written.has(name)) {
      written.add(name);
      mtl.push(`newmtl ${name}`, 'Kd 1 1 1');
      // 24 of 6,885 meshes ship with an empty name — key and texName both the
      // [u32 1]['\0'] empty string. That is real untextured geometry, so emit
      // the material without a map rather than a dangling map_Kd.
      if (tex) {
        // convert-textures.js produced PNGs; point at those, not the .dds the
        // map asks for.
        mtl.push(`map_Kd ${tex.replace(/\.[^.]+$/, '.png')}`);
      }
      mtl.push('');
    }
    obj.push(`usemtl ${name}`, `g ${name}_${m.firstNode}`);
    for (let k = 0; k < m.nodeCount; k++) {
      const n = c.nodes[m.firstNode + k];
      const off = vBase + n.vertexOffset;
      const idx = n.g.indices;
      for (let t = 0; t < idx.length; t += 3) {
        const a = off + idx[t], b = off + idx[t + 1], cc = off + idx[t + 2];
        // Reverse winding to match the negated X, or every face points inward.
        obj.push(`f ${a}/${a} ${cc}/${cc} ${b}/${b}`);
      }
    }
  }
  return { obj: obj.join('\n') + '\n', mtl: mtl.join('\n') + '\n' };
}

const stats = { seen: 0, ok: 0, empty: 0, failed: 0, meshes: 0, nodes: 0,
                vertices: 0, triangles: 0, bytes: 0, encrypted: 0 };
const rows = [];
const failures = [];
const allTextures = new Set();
// The same map ships loose and inside Map.rcc; keep the first copy only.
const seenNames = new Set();

function handle(name, raw) {
  if (!name.toLowerCase().endsWith('.wld0')) return;
  const key = path.basename(name).toLowerCase();
  if (seenNames.has(key)) return;
  if (objMap && path.basename(key, '.wld0') !== objMap.toLowerCase()) return;
  seenNames.add(key);
  stats.seen++;

  let sm;
  try {
    sm = SM.parse(raw, { headerOnly: listOnly });
  } catch (err) {
    stats.failed++;
    failures.push(`${key}: ${err.message}`);
    return;
  }
  if (sm.encrypted) stats.encrypted++;

  const s = SM.summarise(sm);
  for (const t of s.textures) allTextures.add(t);
  stats.meshes += s.meshes;
  stats.nodes += s.nodes;
  stats.vertices += s.verts;
  stats.triangles += s.tris;
  rows.push({ name: key, version: sm.version, enc: sm.encrypted,
              m: s.meshes, v: s.verts, t: s.tris, tex: s.textures.size });

  if (!s.verts) { stats.empty++; return; }
  stats.ok++;
  if (listOnly) return;

  const c = collect(sm);
  const base = path.basename(key, '.wld0');
  fs.mkdirSync(outDir, { recursive: true });

  if (objMap) {
    const o = encodeObj(base, c);
    fs.writeFileSync(path.join(outDir, base + '.obj'), o.obj);
    fs.writeFileSync(path.join(outDir, base + '.mtl'), o.mtl);
    stats.bytes += o.obj.length + o.mtl.length;
    console.log(`${base}.obj: ${c.totalVerts.toLocaleString()} verts, ` +
                `${(c.totalIndices / 3).toLocaleString()} tris, ` +
                `${c.textures.length} materials`);
    return;
  }

  let data, ext;
  if (asJson) {
    data = Buffer.from(JSON.stringify({
      map: base, version: sm.version, vMax: sm.vMax, vMin: sm.vMin,
      textures: c.textures,
      meshes: c.meshes,
      nodes: c.nodes.map((n) => ({
        vMax: n.vMax, vMin: n.vMin, fvf: n.g.fvf, landType: n.g.landType,
        positions: Array.from(n.g.positions),
        uvs: n.g.uvs ? Array.from(n.g.uvs) : null,
        indices: Array.from(n.g.indices),
      })),
    }));
    ext = '.terrain.json';
  } else {
    data = encodeBinary(sm, c);
    ext = '.terrain';
  }
  fs.writeFileSync(path.join(outDir, base + ext), data);
  // The texture list is what tells the mobile pipeline which of the converted
  // PNGs this map actually needs — the whole point of extracting names at all.
  fs.writeFileSync(path.join(outDir, base + '.textures.json'),
                   JSON.stringify(c.textures, null, 2));
  stats.bytes += data.length;
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
        if (!e.name.toLowerCase().endsWith('.wld0')) continue;
        try { handle(e.name, ar.read(e)); } catch (err) {
          stats.failed++;
          failures.push(`${e.name}: ${err.message}`);
        }
      }
    } else if (it.name.toLowerCase().endsWith('.wld0')) {
      try { handle(it.name, fs.readFileSync(p)); } catch { stats.failed++; }
    }
  }
})(RAN);

if (listOnly) {
  console.log('    verts       tris  meshes  tex   ver     map');
  console.log('---------  ---------  ------  ---  -----   ---------------------------');
  for (const r of rows.sort((a, b) => b.v - a.v).slice(0, 25)) {
    console.log(`${String(r.v).padStart(9)}  ${String(r.t).padStart(9)}  ` +
                `${String(r.m).padStart(6)}  ${String(r.tex).padStart(3)}  ` +
                `0x${r.version.toString(16).padStart(4, '0')}${r.enc ? '*' : ' '}  ${r.name}`);
  }
  if (rows.length > 25) console.log(`... and ${rows.length - 25} more`);
  console.log('\n(* = encrypted "Default_Crypt" variant)');
}

const fmt = (n) => n.toLocaleString('en-US');
console.log(`\n${stats.seen} sidecars: ${stats.ok} with geometry, ` +
            `${stats.empty} empty, ${stats.failed} failed`);
console.log(`${stats.encrypted} encrypted`);
console.log(`${fmt(stats.meshes)} draw batches, ${fmt(stats.nodes)} nodes, ` +
            `${fmt(stats.vertices)} vertices, ${fmt(stats.triangles)} triangles`);
console.log(`${allTextures.size} distinct terrain textures` +
            (listOnly ? '' : `, ${(stats.bytes / 1048576).toFixed(1)}M written`) +
            ` in ${((Date.now() - t0) / 1000).toFixed(1)}s`);
if (failures.length) {
  console.log('\nfailures:');
  for (const f of failures.slice(0, 10)) console.log(`  ! ${f}`);
}
process.exit(0);
