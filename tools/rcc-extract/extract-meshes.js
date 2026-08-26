'use strict';
//
// Extract meshes and skeletons from every `.x` in the deploy tree.
//
//   node extract-meshes.js --list          inventory, nothing written
//   node extract-meshes.js --out DIR       one .rmesh per source file
//   node extract-meshes.js --skinned-only  restrict to files that have bones
//
// Format, little-endian throughout. Sections are contiguous and every count is
// in the header, so a loader can size all its buffers before reading anything.
//
//   header 44 bytes
//     char[4] "RMSH"
//     u32  version = 1
//     u32  flags            bit0 = has skinning data
//     u32  boneCount
//     u32  meshCount
//     u32  submeshCount     total across meshes
//     u32  skinBoneCount    total across meshes
//     u32  vertexCount      total
//     u32  indexCount       total
//     u32  stringBytes
//   strings      stringBytes, NUL-terminated, referenced by byte offset
//   bones        boneCount * 72   i32 parent, u32 name, f32[16] local
//   meshes       meshCount * 40   u32 name, u32 vertexOffset, u32 vertexCount,
//                                 u32 indexOffset, u32 indexCount,
//                                 u32 submeshOffset, u32 submeshCount,
//                                 u32 skinBoneOffset, u32 skinBoneCount, u32 flags
//   submeshes    submeshCount*12  u32 textureName, u32 indexStart, u32 indexCount
//   skinBones    skinBoneCount*72 u32 name, i32 localBone, f32[16] bindPose
//   positions    vertexCount * 12
//   normals      vertexCount * 12
//   uvs          vertexCount * 8
//   boneIndices  vertexCount * 8  u16[4]      (skinned files only)
//   boneWeights  vertexCount * 16 f32[4]      (skinned files only)
//   indices      indexCount * 4   u32, mesh-local
//
// Indices are mesh-local, like `.terrain`'s are node-local: add the mesh's
// vertexOffset when merging.
//
// **Bone indices address the MESH's skin-bone table, not the file's bone
// table.** That indirection is load-bearing, not tidiness: equipment and costume
// meshes skin onto the *character's* skeleton, which lives in a different file
// entirely — `s_cos_psy_dancer.x` carries 7 frames of its own but skins to
// `Bip01_Pelvis`, `Bip01_Spine`, `Bip01_L_Thigh`. Resolving names to indices at
// extraction time would silently drop every one of those influences. Each skin
// bone therefore keeps its NAME, plus `localBone` (an index into this file's
// bone table, or -1 when the bone is external) so the consumer can bind against
// whichever skeleton it is assembling onto.
//
const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc');
const XM = require('./xmesh');

const RAN = path.join(__dirname, '..', '..', '..', 'Ran');
const argv = process.argv.slice(2);
const has = (f) => argv.includes(f);
const val = (f, d) => { const i = argv.indexOf(f); return i === -1 ? d : argv[i + 1]; };

const outDir = val('--out', null);
const listOnly = has('--list');
const skinnedOnly = has('--skinned-only');

if (!outDir && !listOnly) {
  console.error('usage: node extract-meshes.js --list | --out DIR [--skinned-only]');
  process.exit(2);
}

const MAX_INFLUENCES = 4;   // what Unity's BoneWeight carries

/** Interns strings and hands back byte offsets into one blob. */
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

const IDENTITY = Float32Array.from([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]);

/**
 * Resolve a SkinWeights bone name to an index in the flattened bone table.
 *
 * Frame names are NOT unique — `arch_w_bg.x` carries `Bone01` fifteen times —
 * so this is genuinely ambiguous and the count of ambiguous hits is reported
 * rather than hidden. First match wins, which matches how the engine's own
 * `D3DXFrameFind` walks the hierarchy.
 */
function buildBoneIndex(bones) {
  const byName = new Map();
  let duplicates = 0;
  bones.forEach((b, i) => {
    if (!b.name) return;
    if (byName.has(b.name)) { duplicates++; return; }
    byName.set(b.name, i);
  });
  return { byName, duplicates };
}

/**
 * Turn the per-bone sparse influence lists into per-vertex weights.
 *
 * The .x format stores skinning bone-major (one list of affected vertices per
 * bone); GPUs and Unity want it vertex-major with a fixed cap. Vertices with
 * more than four influences keep the four heaviest and are renormalised, which
 * is what the extra weight would have contributed anyway.
 *
 * Indices produced here are positions in `mesh.skin` — the mesh's own skin-bone
 * table — so an influence on a bone this file does not contain survives intact.
 */
function buildVertexWeights(mesh, boneIndex, stats) {
  const n = mesh.vertexCount;
  const idx = new Uint16Array(n * MAX_INFLUENCES);
  const wgt = new Float32Array(n * MAX_INFLUENCES);
  const perVertex = new Array(n);

  (mesh.skin || []).forEach((s, bi) => {
    if (s.bone == null || boneIndex.byName.get(s.bone) === undefined) {
      stats.externalBones++;      // resolved later, against the target skeleton
    }
    for (let i = 0; i < s.indices.length; i++) {
      const v = s.indices[i];
      if (v >= n) { stats.badVertexRefs++; continue; }
      const w = s.weights[i];
      if (!(w > 0)) continue;
      (perVertex[v] || (perVertex[v] = [])).push([bi, w]);
    }
  });

  for (let v = 0; v < n; v++) {
    const list = perVertex[v];
    if (!list || !list.length) {
      // Unweighted vertex. Bind it rigidly to bone 0 with full weight — a zero
      // total weight collapses the vertex to the origin in every skinning
      // implementation, which reads as "the model exploded".
      idx[v * MAX_INFLUENCES] = 0;
      wgt[v * MAX_INFLUENCES] = 1;
      stats.unweightedVertices++;
      continue;
    }
    if (list.length > MAX_INFLUENCES) stats.overInfluenced++;
    list.sort((a, b) => b[1] - a[1]);
    let total = 0;
    const take = Math.min(list.length, MAX_INFLUENCES);
    for (let k = 0; k < take; k++) total += list[k][1];
    if (!(total > 0)) { idx[v * MAX_INFLUENCES] = 0; wgt[v * MAX_INFLUENCES] = 1; continue; }
    for (let k = 0; k < take; k++) {
      idx[v * MAX_INFLUENCES + k] = list[k][0];
      wgt[v * MAX_INFLUENCES + k] = list[k][1] / total;
    }
  }
  return { idx, wgt };
}

/** Split a mesh's triangles into runs of equal material — Unity submeshes. */
function buildSubmeshes(mesh, pool) {
  const triCount = mesh.indices.length / 3;
  const names = mesh.textures || [];
  if (!mesh.faceMaterials || mesh.faceMaterials.length !== triCount || names.length <= 1) {
    return [{ texture: pool.add(names[0] || ''), start: 0, count: mesh.indices.length }];
  }
  // Group by material rather than emitting a run per change: a mesh whose faces
  // alternate materials would otherwise produce thousands of one-triangle draws.
  const byMaterial = new Map();
  for (let t = 0; t < triCount; t++) {
    const m = mesh.faceMaterials[t];
    (byMaterial.get(m) || byMaterial.set(m, []).get(m)).push(t);
  }
  const order = new Uint32Array(mesh.indices.length);
  const out = [];
  let at = 0;
  for (const [m, tris] of [...byMaterial].sort((a, b) => a[0] - b[0])) {
    const start = at;
    for (const t of tris) {
      order[at++] = mesh.indices[t * 3];
      order[at++] = mesh.indices[t * 3 + 1];
      order[at++] = mesh.indices[t * 3 + 2];
    }
    out.push({ texture: pool.add(names[m] || ''), start, count: at - start });
  }
  mesh.indices = order;    // reordered to make each submesh contiguous
  return out;
}

function encode(parsed, stats) {
  const pool = new StringPool();
  const bones = parsed.bones || [];
  const skinned = parsed.meshes.some((m) => m.skinBones > 0);
  const boneIndex = buildBoneIndex(bones);
  stats.ambiguousBoneNames += boneIndex.duplicates;

  const meshRecords = [];
  const submeshes = [];
  const skinBones = [];
  let vertexTotal = 0, indexTotal = 0;

  for (const m of parsed.meshes) {
    const subs = buildSubmeshes(m, pool);
    const rec = {
      // The exporter leaves the Mesh instance name blank on nearly every
      // shipped file, so fall back to the enclosing Frame's name — that is the
      // name a `.cps` piece uses to select one mesh out of a shared `.x`.
      name: pool.add(m.name || m.frameName || ''),
      vertexOffset: vertexTotal,
      vertexCount: m.vertexCount,
      indexOffset: indexTotal,
      indexCount: m.indices.length,
      submeshOffset: submeshes.length,
      submeshCount: subs.length,
      skinBoneOffset: skinBones.length,
      skinBoneCount: (m.skin || []).length,
      flags: m.skinBones > 0 ? 1 : 0,
      mesh: m,
    };
    for (const s of subs) submeshes.push(s);
    for (const s of m.skin || []) {
      const local = s.bone == null ? undefined : boneIndex.byName.get(s.bone);
      skinBones.push({
        name: pool.add(s.bone || ''),
        local: local === undefined ? -1 : local,
        bindPose: s.bindPose && s.bindPose.length === 16 ? s.bindPose : IDENTITY,
      });
    }
    meshRecords.push(rec);
    vertexTotal += m.vertexCount;
    indexTotal += m.indices.length;
  }

  // The bone table interns names too, so it must be built BEFORE the string
  // blob is frozen — otherwise every bone name points past the end of it.
  const boneTable = Buffer.alloc(bones.length * 72);
  bones.forEach((b, i) => {
    const o = i * 72;
    boneTable.writeInt32LE(b.parent, o);
    boneTable.writeUInt32LE(pool.add(b.name || ''), o + 4);
    const t = b.transform || IDENTITY;
    for (let k = 0; k < 16; k++) boneTable.writeFloatLE(t[k], o + 8 + k * 4);
  });
  const strings = pool.buffer();

  const head = Buffer.alloc(44);
  head.write('RMSH', 0, 'latin1');
  head.writeUInt32LE(1, 4);
  head.writeUInt32LE(skinned ? 1 : 0, 8);
  head.writeUInt32LE(bones.length, 12);
  head.writeUInt32LE(meshRecords.length, 16);
  head.writeUInt32LE(submeshes.length, 20);
  head.writeUInt32LE(skinBones.length, 24);
  head.writeUInt32LE(vertexTotal, 28);
  head.writeUInt32LE(indexTotal, 32);
  head.writeUInt32LE(strings.length, 36);

  const meshTable = Buffer.alloc(meshRecords.length * 40);
  meshRecords.forEach((r, i) => {
    const o = i * 40;
    meshTable.writeUInt32LE(r.name, o);
    meshTable.writeUInt32LE(r.vertexOffset, o + 4);
    meshTable.writeUInt32LE(r.vertexCount, o + 8);
    meshTable.writeUInt32LE(r.indexOffset, o + 12);
    meshTable.writeUInt32LE(r.indexCount, o + 16);
    meshTable.writeUInt32LE(r.submeshOffset, o + 20);
    meshTable.writeUInt32LE(r.submeshCount, o + 24);
    meshTable.writeUInt32LE(r.skinBoneOffset, o + 28);
    meshTable.writeUInt32LE(r.skinBoneCount, o + 32);
    meshTable.writeUInt32LE(r.flags, o + 36);
  });

  const skinTable = Buffer.alloc(skinBones.length * 72);
  skinBones.forEach((s, i) => {
    const o = i * 72;
    skinTable.writeUInt32LE(s.name, o);
    skinTable.writeInt32LE(s.local, o + 4);
    for (let k = 0; k < 16; k++) skinTable.writeFloatLE(s.bindPose[k], o + 8 + k * 4);
  });

  const subTable = Buffer.alloc(submeshes.length * 12);
  submeshes.forEach((s, i) => {
    subTable.writeUInt32LE(s.texture, i * 12);
    subTable.writeUInt32LE(s.start, i * 12 + 4);
    subTable.writeUInt32LE(s.count, i * 12 + 8);
  });

  const positions = new Float32Array(vertexTotal * 3);
  const normals = new Float32Array(vertexTotal * 3);
  const uvs = new Float32Array(vertexTotal * 2);
  const boneIdx = skinned ? new Uint16Array(vertexTotal * MAX_INFLUENCES) : null;
  const boneWgt = skinned ? new Float32Array(vertexTotal * MAX_INFLUENCES) : null;
  const indices = new Uint32Array(indexTotal);

  for (const r of meshRecords) {
    const m = r.mesh;
    positions.set(m.positions, r.vertexOffset * 3);
    // Normals and UVs are per-vertex here only when their counts match; the .x
    // format allows independent index sets, and a mismatched array copied
    // blindly would silently misalign every vertex after the first.
    if (m.normals && m.normals.length === m.vertexCount * 3) {
      normals.set(m.normals, r.vertexOffset * 3);
    } else if (m.normals) stats.normalCountMismatch++;
    if (m.uvs && m.uvs.length === m.vertexCount * 2) {
      uvs.set(m.uvs, r.vertexOffset * 2);
    } else if (m.uvs) stats.uvCountMismatch++;

    for (let i = 0; i < m.indices.length; i++) indices[r.indexOffset + i] = m.indices[i];

    if (skinned) {
      if (m.skinBones > 0) {
        const w = buildVertexWeights(m, boneIndex, stats);
        boneIdx.set(w.idx, r.vertexOffset * MAX_INFLUENCES);
        boneWgt.set(w.wgt, r.vertexOffset * MAX_INFLUENCES);
      } else {
        // Rigid mesh inside a skinned file: bind every vertex to bone 0.
        for (let v = 0; v < m.vertexCount; v++) {
          boneWgt[(r.vertexOffset + v) * MAX_INFLUENCES] = 1;
        }
      }
    }
  }

  // Non-finite data reaching Unity poisons mesh bounds and kills culling for the
  // whole renderer — the same failure the terrain NaN UVs would have caused.
  const scrub = (arr, counter) => {
    for (let i = 0; i < arr.length; i++) {
      if (!Number.isFinite(arr[i])) { arr[i] = 0; stats[counter]++; }
    }
  };
  scrub(positions, 'nonFinitePositions');
  scrub(normals, 'nonFiniteNormals');
  scrub(uvs, 'nonFiniteUvs');

  const view = (t) => Buffer.from(t.buffer, t.byteOffset, t.byteLength);
  const parts = [head, strings, boneTable, meshTable, subTable, skinTable,
                 view(positions), view(normals), view(uvs)];
  if (skinned) parts.push(view(boneIdx), view(boneWgt));
  parts.push(view(indices));
  return Buffer.concat(parts);
}

const stats = { seen: 0, ok: 0, failed: 0, skinnedFiles: 0, bones: 0, meshes: 0,
                vertices: 0, triangles: 0, bytes: 0, truncated: 0,
                externalBones: 0, badVertexRefs: 0, unweightedVertices: 0,
                overInfluenced: 0, ambiguousBoneNames: 0, normalCountMismatch: 0,
                uvCountMismatch: 0, nonFinitePositions: 0, nonFiniteNormals: 0,
                nonFiniteUvs: 0 };
const rows = [];
const failures = [];
const seenNames = new Set();

function handle(name, raw) {
  if (!name.toLowerCase().endsWith('.x')) return;
  const key = path.basename(name).toLowerCase();
  if (seenNames.has(key)) return;
  seenNames.add(key);

  let parsed;
  try {
    parsed = XM.parse(raw, { tolerant: true });
  } catch (err) {
    stats.failed++;
    failures.push(`${key}: ${err.message}`);
    return;
  }
  const skinned = parsed.meshes.some((m) => m.skinBones > 0);
  if (skinnedOnly && !skinned) return;

  stats.seen++;
  if (parsed.truncated) stats.truncated++;
  if (skinned) stats.skinnedFiles++;

  const verts = parsed.meshes.reduce((a, m) => a + m.vertexCount, 0);
  const tris = parsed.meshes.reduce((a, m) => a + m.indices.length / 3, 0);
  stats.bones += (parsed.bones || []).length;
  stats.meshes += parsed.meshes.length;
  stats.vertices += verts;
  stats.triangles += tris;
  rows.push({ name: key, bones: (parsed.bones || []).length,
              meshes: parsed.meshes.length, v: verts, t: tris, skinned });

  if (listOnly) { stats.ok++; return; }

  let data;
  try {
    data = encode(parsed, stats);
  } catch (err) {
    stats.failed++;
    failures.push(`${key}: encode: ${err.message}`);
    return;
  }
  stats.ok++;
  fs.mkdirSync(outDir, { recursive: true });
  fs.writeFileSync(path.join(outDir, path.basename(key, '.x') + '.rmesh'), data);
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
        if (!e.name.toLowerCase().endsWith('.x')) continue;
        try { handle(e.name, ar.read(e)); } catch (err) {
          stats.failed++;
          failures.push(`${e.name}: ${err.message}`);
        }
      }
    } else if (it.name.toLowerCase().endsWith('.x')) {
      try { handle(it.name, fs.readFileSync(p)); } catch { stats.failed++; }
    }
  }
})(RAN);

const fmt = (n) => n.toLocaleString('en-US');
if (listOnly) {
  console.log('   verts       tris  meshes  bones  skin  file');
  console.log('--------  ---------  ------  -----  ----  ------------------------');
  for (const r of rows.sort((a, b) => b.v - a.v).slice(0, 25)) {
    console.log(`${String(r.v).padStart(8)}  ${String(r.t).padStart(9)}  ` +
                `${String(r.meshes).padStart(6)}  ${String(r.bones).padStart(5)}  ` +
                `${r.skinned ? ' yes' : '  - '}  ${r.name}`);
  }
  if (rows.length > 25) console.log(`... and ${rows.length - 25} more`);
}

console.log(`\n${stats.seen} .x files: ${stats.ok} written, ${stats.failed} failed, ` +
            `${stats.skinnedFiles} skinned, ${stats.truncated} truncated but recovered`);
console.log(`${fmt(stats.meshes)} meshes, ${fmt(stats.bones)} bones, ` +
            `${fmt(stats.vertices)} vertices, ${fmt(stats.triangles)} triangles` +
            (listOnly ? '' : `, ${(stats.bytes / 1048576).toFixed(1)}M written`) +
            ` in ${((Date.now() - t0) / 1000).toFixed(1)}s`);
console.log(`skin: ${fmt(stats.unweightedVertices)} unweighted vertices, ` +
            `${fmt(stats.overInfluenced)} over ${MAX_INFLUENCES} influences, ` +
            `${fmt(stats.externalBones)} external bone refs (resolved at assembly), ` +
            `${stats.ambiguousBoneNames} ambiguous names, ${stats.badVertexRefs} bad vertex refs`);
console.log(`data: ${stats.normalCountMismatch} normal-count mismatches, ` +
            `${stats.uvCountMismatch} uv-count mismatches, ` +
            `${stats.nonFinitePositions + stats.nonFiniteNormals + stats.nonFiniteUvs} non-finite values scrubbed`);
if (failures.length) {
  console.log('\nfailures:');
  for (const f of failures.slice(0, 10)) console.log(`  ! ${f}`);
}
process.exit(0);
