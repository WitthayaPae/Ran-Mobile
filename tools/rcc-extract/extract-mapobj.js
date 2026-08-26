'use strict';
//
// Extract map object placement + geometry from every `.wld`.
//
//   node extract-mapobj.js --list       inventory, nothing written
//   node extract-mapobj.js --out DIR    one .rmapobj per map
//
// Terrain (`.wld0`) is the ground; this is everything standing on it. Map
// objects carry their geometry INLINE — they are not references to the `.x`
// files — so this is self-contained.
//
// Format, little-endian throughout.
//
//   header 44 bytes
//     char[4] "ROBJ"
//     u32  version = 2
//     u32  objectCount
//     u32  meshCount
//     u32  submeshCount
//     u32  vertexCount
//     u32  indexCount
//     u32  stringBytes
//     f32[3] boundsMax, f32[3] boundsMin      <- union of object AABBs
//   strings     stringBytes, NUL-terminated, referenced by byte offset
//   objects     objectCount * 76
//                 u32 name, f32[16] transform, u32 firstMesh, u32 meshCount
//   meshes      meshCount * 32
//                 u32 firstSubmesh, u32 submeshCount, u32 vertexOffset,
//                 u32 vertexCount, u32 indexOffset, u32 indexCount,
//                 u32 fvf, u32 flags   (bit0 = unlit, bit1 = alpha cutout)
//   submeshes   submeshCount * 20
//                 u32 textureName, u32 faceStart, u32 faceCount,
//                 u32 vertexStart, u32 vertexCount
//   positions   vertexCount * 12
//   uvs         vertexCount * 8
//   indices     indexCount * 2      u16, MESH-local
//   -- v2 tails --
//   u32         hasColors            0 or 1
//   u32 ARGB * vertexCount           in-VB diffuse bake, when hasColors = 1
//   u32         hasNormals           0 or 1
//   f32[3] * vertexCount             authored normals, when hasNormals = 1
//
// v2 additionally APPENDS the replace-piece instances (see pieces.js): each
// referenced .pis contributes its meshes once, and every DXPIECEOBJ in the
// map's chain becomes an object whose mesh range points at that shared
// geometry. Objects were never required to own their ranges exclusively.
//
// The transform is `matCombined` as stored: a row-major D3D matrix in the
// engine's left-handed Y-up space, which is Unity's convention too. It needs
// transposing for Unity's column-vector matrices but no axis conversion.
//
// Submesh ranges are D3DXATTRIBUTERANGE entries, so `faceStart`/`faceCount` are
// in FACES, not indices — multiply by 3.
//
const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc');
const W = require('./wld');
const MO = require('./mapobj');
const PIECES = require('./pieces');
const ANIMAN = require('./animan');
const GRASSEFF = require('./grasseff');

const RAN = path.join(__dirname, '..', '..', '..', 'Ran');
const argv = process.argv.slice(2);
const has = (f) => argv.includes(f);
const val = (f, d) => { const i = argv.indexOf(f); return i === -1 ? d : argv[i + 1]; };

const outDir = val('--out', null);
const listOnly = has('--list');

if (!outDir && !listOnly) {
  console.error('usage: node extract-mapobj.js --list | --out DIR');
  process.exit(2);
}

class StringPool {
  constructor() { this.parts = []; this.at = 0; this.seen = new Map(); }
  add(s) {
    const v = s == null ? '' : String(s);
    if (this.seen.has(v)) return this.seen.get(v);
    const off = this.at;
    const b = Buffer.from(v + '\0', 'latin1');
    this.parts.push(b);
    this.at += b.length;
    this.seen.set(v, off);
    return off;
  }
  buffer() { return Buffer.concat(this.parts); }
}

function encode(scene, stats, pieceData) {
  const pool = new StringPool();
  const objects = [];
  const meshes = [];
  const submeshes = [];
  const geo = [];
  let vertexTotal = 0, indexTotal = 0;
  const lo = [Infinity, Infinity, Infinity];
  const hi = [-Infinity, -Infinity, -Infinity];

  for (const node of scene.nodes) {
    for (const frame of node.frames) {
      const withGeom = frame.meshes.filter(
        (m) => m.geometry && m.geometry.vertexCount > 0 && m.geometry.faceCount > 0);
      if (!withGeom.length) continue;

      for (let a = 0; a < 3; a++) {
        if (frame.vTreeMin[a] < lo[a]) lo[a] = frame.vTreeMin[a];
        if (frame.vTreeMax[a] > hi[a]) hi[a] = frame.vTreeMax[a];
      }

      objects.push({ name: pool.add(frame.name || ''), transform: frame.matCombined,
                     firstMesh: meshes.length, meshCount: withGeom.length });

      for (const m of withGeom) {
        const g = m.geometry;
        // An empty attribute table still needs one submesh, or the mesh has no
        // draw range at all. Fall back to the whole index range, material 0.
        const ranges = g.attribs.length ? g.attribs
          : [{ attribId: 0, faceStart: 0, faceCount: g.faceCount,
               vertexStart: 0, vertexCount: g.vertexCount }];

        meshes.push({ firstSubmesh: submeshes.length, submeshCount: ranges.length,
                      vertexOffset: vertexTotal, vertexCount: g.vertexCount,
                      indexOffset: indexTotal, indexCount: g.indices.length,
                      fvf: g.fvf, flags: g.unlit ? 1 : 0 });

        for (const r of ranges) {
          // AttribId indexes the mesh's own material/texture list.
          const tex = (m.textures && m.textures[r.attribId]) || '';
          submeshes.push({ texture: pool.add(tex), faceStart: r.faceStart,
                           faceCount: r.faceCount, vertexStart: r.vertexStart,
                           vertexCount: r.vertexCount });
          if (tex) stats.textures.add(tex.toLowerCase());
        }

        geo.push(g);
        vertexTotal += g.vertexCount;
        indexTotal += g.indices.length;
      }
    }
  }

  // Replace-piece instances: the geometry of each referenced .pis is written
  // ONCE, and every instance is an object whose (firstMesh, meshCount) points
  // at that shared range — the object table never required exclusive ranges.
  // This is what the engine itself does: one DXREPLACEPIECE, many DXPIECEOBJ.
  if (pieceData && pieceData.instances.length) {
    const rangeByPis = new Map();
    for (const [pisName, pis] of pieceData.lib) {
      const firstMesh = meshes.length;
      for (const pm of pis.meshes) {
        const g = pm.geometry;
        meshes.push({ firstSubmesh: submeshes.length, submeshCount: 1,
                      vertexOffset: vertexTotal, vertexCount: g.vertexCount,
                      indexOffset: indexTotal, indexCount: g.indices.length,
                      fvf: g.fvf,
                      // bit1 marks the ALPHA bucket: foliage that must render
                      // as a cutout, exactly like terrain bucket 1.
                      flags: pm.bucket === 'mesh' ? 0 : 2 });
        submeshes.push({ texture: pool.add(pm.texture), faceStart: 0,
                         faceCount: g.faceCount, vertexStart: 0,
                         vertexCount: g.vertexCount });
        if (pm.texture) stats.textures.add(pm.texture.toLowerCase());
        geo.push(g);
        vertexTotal += g.vertexCount;
        indexTotal += g.indices.length;
      }
      rangeByPis.set(pisName, { firstMesh, meshCount: meshes.length - firstMesh });
    }

    for (const inst of pieceData.instances) {
      const range = rangeByPis.get(inst.file);
      if (!range || !range.meshCount) continue;
      objects.push({ name: pool.add(inst.file), transform: inst.matrix,
                     firstMesh: range.firstMesh, meshCount: range.meshCount });

      // Instance AABB: the piece's local box through its row-vector matrix,
      // so the map bounds cover the placed trees, not just their pivots.
      const pis = pieceData.lib.get(inst.file);
      const M = inst.matrix;
      for (let corner = 0; corner < 8; corner++) {
        const p = [corner & 1 ? pis.vMax[0] : pis.vMin[0],
                   corner & 2 ? pis.vMax[1] : pis.vMin[1],
                   corner & 4 ? pis.vMax[2] : pis.vMin[2]];
        for (let j = 0; j < 3; j++) {
          const w = p[0] * M[j] + p[1] * M[4 + j] + p[2] * M[8 + j] + M[12 + j];
          if (Number.isFinite(w)) {
            if (w < lo[j]) lo[j] = w;
            if (w > hi[j]) hi[j] = w;
          }
        }
      }
    }
  }

  if (!objects.length) return null;

  // The in-VB diffuse — the baked lighting for pieces (the land path renders
  // with D3DRS_LIGHTING=FALSE). White wherever a geometry has no colour
  // channel, which multiplies to "texture unchanged".
  const hasColors = geo.some((g) => g.vbColors);
  const colors = hasColors ? new Uint32Array(vertexTotal) : null;
  if (colors) {
    colors.fill(0xffffffff);
    let cAt = 0;
    for (const g of geo) {
      if (g.vbColors) colors.set(g.vbColors, cAt);
      cAt += g.vertexCount;
    }
  }

  // Authored normals — see octree.js: foliage normals are up/outward by
  // design, and recalculated ones turn trees black-side-on. Zero marks
  // "none in the file" so the importer knows to recalculate just those.
  const hasNormals = geo.some((g) => g.vbNormals);
  const normals = hasNormals ? new Float32Array(vertexTotal * 3) : null;
  if (normals) {
    let nAt = 0;
    for (const g of geo) {
      if (g.vbNormals) normals.set(g.vbNormals, nAt * 3);
      nAt += g.vertexCount;
    }
  }

  const strings = pool.buffer();
  const head = Buffer.alloc(44 + 24);
  head.write('ROBJ', 0, 'latin1');
  head.writeUInt32LE(2, 4);
  head.writeUInt32LE(objects.length, 8);
  head.writeUInt32LE(meshes.length, 12);
  head.writeUInt32LE(submeshes.length, 16);
  head.writeUInt32LE(vertexTotal, 20);
  head.writeUInt32LE(indexTotal, 24);
  head.writeUInt32LE(strings.length, 28);
  for (let i = 0; i < 3; i++) head.writeFloatLE(hi[i], 44 + i * 4);
  for (let i = 0; i < 3; i++) head.writeFloatLE(lo[i], 56 + i * 4);

  const objTable = Buffer.alloc(objects.length * 76);
  objects.forEach((o, i) => {
    const at = i * 76;
    objTable.writeUInt32LE(o.name, at);
    for (let k = 0; k < 16; k++) {
      const v = o.transform[k];
      // A non-finite transform would place the object at infinity and poison
      // any bounds computed from it; fall back to identity.
      objTable.writeFloatLE(Number.isFinite(v) ? v : (k % 5 === 0 ? 1 : 0), at + 4 + k * 4);
    }
    objTable.writeUInt32LE(o.firstMesh, at + 68);
    objTable.writeUInt32LE(o.meshCount, at + 72);
  });

  const meshTable = Buffer.alloc(meshes.length * 32);
  meshes.forEach((m, i) => {
    const at = i * 32;
    meshTable.writeUInt32LE(m.firstSubmesh, at);
    meshTable.writeUInt32LE(m.submeshCount, at + 4);
    meshTable.writeUInt32LE(m.vertexOffset, at + 8);
    meshTable.writeUInt32LE(m.vertexCount, at + 12);
    meshTable.writeUInt32LE(m.indexOffset, at + 16);
    meshTable.writeUInt32LE(m.indexCount, at + 20);
    meshTable.writeUInt32LE(m.fvf, at + 24);
    meshTable.writeUInt32LE(m.flags, at + 28);
  });

  const subTable = Buffer.alloc(submeshes.length * 20);
  submeshes.forEach((s, i) => {
    const at = i * 20;
    subTable.writeUInt32LE(s.texture, at);
    subTable.writeUInt32LE(s.faceStart, at + 4);
    subTable.writeUInt32LE(s.faceCount, at + 8);
    subTable.writeUInt32LE(s.vertexStart, at + 12);
    subTable.writeUInt32LE(s.vertexCount, at + 16);
  });

  const positions = new Float32Array(vertexTotal * 3);
  const uvs = new Float32Array(vertexTotal * 2);
  const indices = new Uint16Array(indexTotal);
  let vAt = 0, iAt = 0;
  for (const g of geo) {
    for (let i = 0; i < g.vertexCount * 3; i++) {
      const v = g.positions[i];
      positions[vAt * 3 + i] = Number.isFinite(v) ? v : 0;
    }
    if (g.uvs) {
      for (let i = 0; i < g.vertexCount * 2; i++) {
        const v = g.uvs[i];
        uvs[vAt * 2 + i] = Number.isFinite(v) ? v : 0;
      }
    }
    indices.set(g.indices, iAt);
    vAt += g.vertexCount;
    iAt += g.indices.length;
  }

  const view = (t) => Buffer.from(t.buffer, t.byteOffset, t.byteLength);
  // v2 tails, mirroring the .terrain v2 shape: a flag, then the blob only
  // when set — colours first, then normals.
  const colorFlag = Buffer.alloc(4);
  colorFlag.writeUInt32LE(colors ? 1 : 0, 0);
  const normalFlag = Buffer.alloc(4);
  normalFlag.writeUInt32LE(normals ? 1 : 0, 0);
  const tail = [colorFlag, ...(colors ? [view(colors)] : []),
                normalFlag, ...(normals ? [view(normals)] : [])];
  return { buf: Buffer.concat([head, strings, objTable, meshTable, subTable,
                               view(positions), view(uvs), view(indices), ...tail]),
           objects: objects.length, meshes: meshes.length, vertices: vertexTotal,
           triangles: indexTotal / 3 };
}

// One shared .pis cache across every map — trees repeat between maps, and the
// engine's own DxReplaceContainer caches by name the same way.
const pisCache = new Map();
const pisFailures = new Map();

function loadPieceData(wldBuf, chainStart, mapKey) {
  let chain;
  try {
    chain = PIECES.walkReplaceChain(wldBuf, chainStart);
  } catch (err) {
    return { instances: [], lib: new Map(), error: `chain: ${err.message}` };
  }

  const lib = new Map();
  const instances = [];
  for (const inst of chain.instances) {
    if (!inst.file) continue;
    if (!pisCache.has(inst.file) && !pisFailures.has(inst.file)) {
      const p = path.join(RAN, 'data', 'piece', inst.file);
      try {
        pisCache.set(inst.file, PIECES.parsePis(fs.readFileSync(p), inst.file));
      } catch (err) {
        pisFailures.set(inst.file, err.message);
      }
    }
    const pis = pisCache.get(inst.file);
    if (!pis || !pis.meshes.length) continue;
    lib.set(inst.file, pis);
    instances.push(inst);
  }

  // Past the chain: the animation managers, then the three standalone effect
  // lists — DEF_EFFECT_GRASS entries there are the ground vegetation (the
  // tufts the login shows all over the field). The aniMan walk exists mostly
  // to reach them; its frames on the lobby maps are camera rigs with no
  // geometry. An unknown effect type ends the effect walk with everything
  // before it intact.
  let grassNote = null;
  let shadowData = null;
  let aniStatsWater = [];
  try {
    const aniStats = { nodes: [], frames: 0, meshes: 0, effects: 0, placed: [], water: [],
                       failedFrames: 0, vertices: 0, triangles: 0, collisionNodes: 0,
                       effectTypes: new Map(), textureNames: new Set(), firstFrameError: null };
    const ani = ANIMAN.walkAniMan(wldBuf, chain.end, aniStats);
    aniStatsWater = aniStats.water;
    const eff = GRASSEFF.walkEffectLists(wldBuf, ani.end);
    if (eff.grass.length) {
      const meshes = GRASSEFF.grassToMeshes(eff.grass);
      if (meshes.length) {
        const blades = eff.grass.reduce((s, g) => s + g.blades.length, 0);
        lib.set('__grass__', { meshes,
                               vMax: [0, 0, 0], vMin: [0, 0, 0] });
        instances.push({ file: '__grass__',
                         matrix: [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1] });
        grassNote = `${blades} grass blades in ${meshes.length} mesh(es)`;
      }
    }
    if (eff.shadow && eff.shadow.meshes.length) {
      shadowData = eff.shadow;
    }
    if (eff.water.length) aniStatsWater = aniStatsWater.concat(eff.water);
  } catch (err) {
    // The section after the chain did not parse on this map — the pieces
    // themselves are unaffected, so keep them and say what was skipped.
    return { instances, lib, error: `vegetation: ${err.message}`, water: aniStatsWater };
  }
  return { instances, lib, grassNote, shadow: shadowData, water: aniStatsWater };
}

const placedByMap = {};
const waterByMap = {};
const stats = { seen: 0, ok: 0, empty: 0, failed: 0, failedFrames: 0,
                objects: 0, meshes: 0, vertices: 0, triangles: 0,
                collisionNodes: 0, effects: 0, bytes: 0, textures: new Set() };
const rows = [];
const failures = [];
const seenNames = new Set();

function handle(name, raw) {
  if (!name.toLowerCase().endsWith('.wld')) return;
  const key = path.basename(name).toLowerCase();
  if (seenNames.has(key)) return;

  let wld;
  try { wld = W.open(raw); } catch { return; }
  seenNames.add(key);
  stats.seen++;

  let scene;
  try {
    scene = MO.parse(wld);
  } catch (err) {
    stats.failed++;
    failures.push(`${key}: ${err.message}`);
    return;
  }
  stats.failedFrames += scene.failedFrames;
  stats.collisionNodes += scene.collisionNodes;
  stats.effects += scene.effects;
  // Placed effects go to a sidecar rather than into .rmapobj. The binary format
  // would need a version bump plus matching changes in the C# reader and the
  // importer; a sidecar reaches the same scene with one moving part, the same
  // way fog.json does.
  if (scene.placed && scene.placed.length) {
    placedByMap[path.basename(name).replace(/.wld$/i, '').toLowerCase()] =
      scene.placed.map((e) => ({ n: e.name, t: e.translate }));
  }

  // The replace-piece chain starts where the octree section ends — the
  // scenery log_in.wld keeps entirely outside the octree (116 trees), and
  // other maps mix with their octree objects.
  const pieceData = loadPieceData(wld.buf, scene.end, key);
  if (pieceData.error) failures.push(`${key} pieces: ${pieceData.error}`);

  // Water — see water.js and the mapwater.json sidecar note near the end of
  // this file. Collected from both the main octree walk and the ani-man walk
  // (lobby/camera-rig frames can carry it too).
  const waterAll = [...scene.water, ...(pieceData.water || [])];
  if (waterAll.length) {
    waterByMap[path.basename(name).replace(/.wld$/i, '').toLowerCase()] =
      waterAll.map((w) => ({ ...w, frameMatrix: w.frameMatrix ? Array.from(w.frameMatrix) : null }));
    stats.water = (stats.water || 0) + waterAll.length;
  }

  // The ground-shadow decal is its own sidecar (unique blend, its own texture),
  // written even when the map has no other objects.
  if (pieceData.shadow && !listOnly) {
    fs.mkdirSync(outDir, { recursive: true });
    fs.writeFileSync(path.join(outDir, path.basename(key, '.wld') + '.rshadow'),
                     encodeShadow(pieceData.shadow));
    stats.shadows = (stats.shadows || 0) + 1;
  }

  const enc = encode(scene, stats, pieceData);
  if (!enc) {
    stats.empty++;
    rows.push({ name: key, objects: 0, meshes: 0, v: 0 });
    return;
  }
  stats.ok++;
  stats.objects += enc.objects;
  stats.meshes += enc.meshes;
  stats.vertices += enc.vertices;
  stats.triangles += enc.triangles;
  rows.push({ name: key, objects: enc.objects, meshes: enc.meshes, v: enc.vertices });

  if (listOnly) return;
  fs.mkdirSync(outDir, { recursive: true });
  fs.writeFileSync(path.join(outDir, path.basename(key, '.wld') + '.rmapobj'), enc.buf);
  stats.bytes += enc.buf.length;
}

// The .rshadow sidecar — the ground-shadow decal geometry.
//
//   "RSHD" u32, version=1 u32, shadowD f32,
//   texNameLen u32, texName bytes,
//   meshCount u32, totalVerts u32, totalIndices u32,
//   mesh table  meshCount * 16 (vertexOffset, vertexCount, indexOffset, indexCount)
//   f32[3]*totalVerts positions, f32[2]*totalVerts uvs (planar-projected),
//   u16*totalIndices  mesh-local indices.
function encodeShadow(shadow) {
  const tex = Buffer.from((shadow.texture || '') + '\0', 'latin1');
  let totalVerts = 0, totalIndices = 0;
  for (const m of shadow.meshes) { totalVerts += m.vertexCount; totalIndices += m.faceCount * 3; }

  const head = Buffer.alloc(12);
  head.write('RSHD', 0, 'latin1');
  head.writeUInt32LE(1, 4);
  head.writeFloatLE(shadow.shadowD, 8);

  const texLen = Buffer.alloc(4); texLen.writeUInt32LE(tex.length, 0);
  const counts = Buffer.alloc(12);
  counts.writeUInt32LE(shadow.meshes.length, 0);
  counts.writeUInt32LE(totalVerts, 4);
  counts.writeUInt32LE(totalIndices, 8);

  const meshTable = Buffer.alloc(shadow.meshes.length * 16);
  const positions = new Float32Array(totalVerts * 3);
  const uvs = new Float32Array(totalVerts * 2);
  const indices = new Uint16Array(totalIndices);
  let vAt = 0, iAt = 0;
  shadow.meshes.forEach((m, i) => {
    meshTable.writeUInt32LE(vAt, i * 16);
    meshTable.writeUInt32LE(m.vertexCount, i * 16 + 4);
    meshTable.writeUInt32LE(iAt, i * 16 + 8);
    meshTable.writeUInt32LE(m.faceCount * 3, i * 16 + 12);
    positions.set(m.positions, vAt * 3);
    uvs.set(m.uvs, vAt * 2);
    indices.set(m.indices, iAt);   // node-local, kept per mesh
    vAt += m.vertexCount;
    iAt += m.faceCount * 3;
  });

  const view = (t) => Buffer.from(t.buffer, t.byteOffset, t.byteLength);
  return Buffer.concat([head, texLen, tex, counts, meshTable,
                        view(positions), view(uvs), view(indices)]);
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
        try { handle(e.name, ar.read(e)); } catch (err) {
          stats.failed++;
          failures.push(`${e.name}: ${err.message}`);
        }
      }
    } else if (it.name.toLowerCase().endsWith('.wld')) {
      try { handle(it.name, fs.readFileSync(p)); } catch { stats.failed++; }
    }
  }
})(RAN);

const fmt = (n) => n.toLocaleString('en-US');
if (listOnly) {
  console.log(' objects  meshes     verts  map');
  console.log('--------  ------  --------  --------------------------------------');
  for (const r of rows.sort((a, b) => b.v - a.v).slice(0, 25)) {
    console.log(`${String(r.objects).padStart(8)}  ${String(r.meshes).padStart(6)}  ` +
                `${String(r.v).padStart(8)}  ${r.name}`);
  }
  if (rows.length > 25) console.log(`... and ${rows.length - 25} more`);
}

console.log(`\n${stats.seen} maps: ${stats.ok} with objects, ${stats.empty} without, ` +
            `${stats.failed} failed`);
{
  const fxPath = path.join(__dirname, '..', '..', 'assets', 'mapfx.json');
  const total = Object.values(placedByMap).reduce((n, a) => n + a.length, 0);
  fs.writeFileSync(fxPath, JSON.stringify({ maps: placedByMap }));
  console.log(`${total} placed effects across ${Object.keys(placedByMap).length} maps -> MOBILE/assets/mapfx.json`);
}
{
  // Water is a sidecar for the same reason placed effects are: reaching the
  // scene with one moving part rather than a `.rmapobj` version bump. See
  // water.js for the decode and DxEffectWater[2]/River source citations.
  //
  // Written to TWO places, matching the fog.json/sky.json precedent: the
  // canonical MOBILE/assets copy, and directly into the Unity project so
  // extract-mapcatalog.js's water merge (same shape as its fog/sky merge)
  // can pick it up without a manual copy step.
  const waterJson = JSON.stringify({ maps: waterByMap });
  const waterPath = path.join(__dirname, '..', '..', 'assets', 'mapwater.json');
  fs.writeFileSync(waterPath, waterJson);
  const waterUnityPath = path.join(__dirname, '..', '..', 'unity', 'RanMobile', 'Assets', 'Ran', 'water.json');
  if (fs.existsSync(path.dirname(waterUnityPath))) fs.writeFileSync(waterUnityPath, waterJson);
  const total = Object.values(waterByMap).reduce((n, a) => n + a.length, 0);
  console.log(`${total} water instances across ${Object.keys(waterByMap).length} maps -> MOBILE/assets/mapwater.json (+ Unity copy)`);
}
console.log(`${fmt(stats.objects)} objects, ${fmt(stats.meshes)} meshes, ` +
            `${fmt(stats.vertices)} vertices, ${fmt(stats.triangles)} triangles` +
            (listOnly ? '' : `, ${(stats.bytes / 1048576).toFixed(1)}M written`) +
            ` in ${((Date.now() - t0) / 1000).toFixed(1)}s`);
console.log(`${stats.textures.size} distinct object textures, ` +
            `${fmt(stats.collisionNodes)} collision nodes, ${fmt(stats.effects)} effects, ` +
            `${stats.failedFrames} frame decode failures`);
if (failures.length) {
  console.log('\nfailures:');
  for (const f of failures.slice(0, 10)) console.log(`  ! ${f}`);
}
if (pisFailures.size) {
  // A piece that fails to parse silently disappears from every map placing
  // it — say which, or the gap looks like map data.
  console.log(`\n${pisFailures.size} piece file(s) not decoded:`);
  for (const [n, e] of [...pisFailures].slice(0, 10)) console.log(`  ! ${n}: ${e}`);
}
process.exit(0);
