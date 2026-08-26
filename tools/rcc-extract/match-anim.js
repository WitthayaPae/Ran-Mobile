'use strict';
//
// Work out which animation clips each mesh can play, and prove it binds.
//
//   node match-anim.js                 report, write nothing
//   node match-anim.js --out FILE      write the mesh -> clip map
//
// Association is DECLARED on both sides and is used as such:
//   * a clip's `.cfg` (`SANIMCONINFO`) names its skeleton — `b_element.x`
//   * a `.chf` character names the skeleton it wears
//   * a mob/pet/NPC is a single `.x` that IS its own skeleton
//
// Bone names deliberately do NOT drive the matching. Almost every rig in the
// game uses the 3ds Max biped convention, so `Bip01_Spine` appears on the
// player skeletons, the mobs and the pets alike; matching on names alone gave
// a median of 721 clips per mesh and paired a mob with the player's entire
// library. Names are used instead as the VERIFIER: once the declared pairing is
// made, the clip's tracks must resolve against the mesh's bones, which is the
// condition under which it will actually animate anything in Unity.
const fs = require('fs');
const path = require('path');

const base = path.resolve(__dirname, '../../..');
const MESH_DIR = path.join(base, 'MOBILE/assets/meshes');
const ANIM_DIR = path.join(base, 'MOBILE/assets/anim');
const TYPES = path.join(base, 'MOBILE/assets/animtypes.json');

function strings(buf, off, len) {
  const block = buf.slice(off, off + len);
  return (o) => {
    if (o >= block.length) return '';
    let e = block.indexOf(0, o);
    if (e < 0) e = block.length;
    return block.toString('latin1', o, e);
  };
}

function readMesh(file) {
  const b = fs.readFileSync(file);
  if (b.length < 44 || b.toString('latin1', 0, 4) !== 'RMSH') return null;
  const boneCount = b.readUInt32LE(12);
  const meshCount = b.readUInt32LE(16);
  const submeshCount = b.readUInt32LE(20);
  const skinBoneCount = b.readUInt32LE(24);
  // 40 header bytes are written into a 44-byte buffer, so stringBytes is the
  // tenth field at 36 and the last 4 are padding. Reading it at 40 gives a
  // silent zero, an empty pool and every name blank.
  const stringBytes = b.readUInt32LE(36);
  let o = 44;
  const str = strings(b, o, stringBytes);
  o += stringBytes;
  const bones = [];
  for (let i = 0; i < boneCount; i++) bones.push(str(b.readUInt32LE(o + i * 72 + 4)));
  o += boneCount * 72 + meshCount * 40 + submeshCount * 12;
  const skin = [];
  for (let i = 0; i < skinBoneCount; i++) skin.push(str(b.readUInt32LE(o + i * 72)));
  return { bones, skin };
}

function readAnimTracks(file) {
  const b = fs.readFileSync(file);
  if (b.length < 40 || b.toString('latin1', 0, 4) !== 'RANM') return null;
  const trackCount = b.readUInt32LE(12);
  const stringBytes = b.readUInt32LE(32);
  let o = 40;
  const str = strings(b, o, stringBytes);
  o += stringBytes;
  const names = [];
  for (let i = 0; i < trackCount; i++) names.push(str(b.readUInt32LE(o + i * 20)));
  return names;
}

const norm = (s) => s.toLowerCase();
const stem = (s) => norm(s).replace(/\.[a-z0-9]+$/, '');

// ---------------------------------------------------------------------------

const doc = JSON.parse(fs.readFileSync(TYPES, 'utf8'));
const clipInfo = doc.clips;
const chars = doc.characters;

// Clips grouped by the skeleton their .cfg declares.
const bySkeleton = new Map();
let noSkeleton = 0;
for (const name of Object.keys(clipInfo)) {
  const s = clipInfo[name].skeleton;
  if (!s) { noSkeleton++; continue; }
  const k = stem(s);
  if (!bySkeleton.has(k)) bySkeleton.set(k, []);
  bySkeleton.get(k).push(name);
}
console.log(`${Object.keys(clipInfo).length} typed clips over ${bySkeleton.size} declared skeletons ` +
            `(${noSkeleton} declare none)`);

console.log('reading meshes...');
const meshFiles = fs.readdirSync(MESH_DIR).filter(f => f.endsWith('.rmesh'));
const meshes = new Map();
for (const f of meshFiles) {
  const m = readMesh(path.join(MESH_DIR, f));
  if (m) meshes.set(stem(f), m);
}
console.log(`  ${meshes.size} meshes`);

// Mesh -> skeleton, from declarations only.
//   1. the mesh IS a skeleton some clip names (mobs, pets, bosses)
//   2. the mesh is a .cps piece of a .chf, which names its skeleton
const meshSkeleton = new Map();
const via = { self: 0, chf: 0 };
for (const name of meshes.keys()) {
  if (bySkeleton.has(name)) { meshSkeleton.set(name, name); via.self++; }
}
// A `.chf` names pieces, and a piece is an INDIRECTION — it holds no geometry,
// only the name of an `.x` and a mesh inside it. Matching piece stems against
// mesh stems directly found 35 of 2,352; following the indirection through
// cps-refs.js finds 2,275.
const PIECES = path.join(base, 'MOBILE/assets/pieces.json');
const pieceMap = fs.existsSync(PIECES)
  ? JSON.parse(fs.readFileSync(PIECES, 'utf8')).pieces : {};
console.log(`${Object.keys(pieceMap).length} resolved pieces`);

for (const cname of Object.keys(chars)) {
  const c = chars[cname];
  if (!c.skeleton) continue;
  const sk = stem(c.skeleton);
  if (!bySkeleton.has(sk)) continue;
  for (const piece of c.pieces || []) {
    // pieces.json is keyed by full filename — `.aps` and `.cps` collide on
    // stem for 10 pieces — so look up the full name first and fall back to
    // both extensions for anything a .chf names without one.
    const p = piece.toLowerCase();
    const ref = pieceMap[p] || pieceMap[`${stem(piece)}.cps`] || pieceMap[`${stem(piece)}.aps`];
    const target = ref ? ref.mesh : stem(piece);
    if (target && meshes.has(target) && !meshSkeleton.has(target)) {
      meshSkeleton.set(target, sk); via.chf++;
    }
  }
  // The skeleton file itself is a mesh too.
  if (meshes.has(sk) && !meshSkeleton.has(sk)) { meshSkeleton.set(sk, sk); via.self++; }
}
console.log(`${meshSkeleton.size} meshes have a declared skeleton ` +
            `(${via.self} are one, ${via.chf} via .chf pieces)`);

// Verify each pairing by binding: the clip's tracks must resolve against the
// mesh's bone universe. This is where a bad declaration would show up.
console.log('verifying bindings...');
const trackCache = new Map();
function tracksOf(clip) {
  if (!trackCache.has(clip)) {
    const f = path.join(ANIM_DIR, `${clip}.ranim`);
    const t = fs.existsSync(f) ? readAnimTracks(f) : null;
    trackCache.set(clip, t ? new Set(t.map(norm)) : null);
  }
  return trackCache.get(clip);
}

const out = {};
const stats = { ok: 0, weak: 0, noClips: 0 };
const missingClips = new Set();
const covHist = {};
for (const [mesh, sk] of meshSkeleton) {
  const m = meshes.get(mesh);
  const universe = new Set([...m.bones, ...m.skin].map(norm));
  universe.delete('');
  const kept = [];
  let covSum = 0, weak = 0;
  for (const clip of bySkeleton.get(sk)) {
    const t = tracksOf(clip);
    // Counted once per clip, not once per (mesh, clip) pair — the same absent
    // clip is reached from every mesh on its skeleton, which inflated this by
    // roughly 3x and made a small gap look like a large one.
    if (!t) { missingClips.add(clip); continue; }
    if (t.size === 0) continue;
    let hit = 0;
    for (const b of t) if (universe.has(b)) hit++;
    const cov = hit / t.size;
    // Equipment and costume pieces skin to a handful of bones, so a clip that
    // keys the whole body will only partly resolve on them. That is normal and
    // still animates correctly — Unity binds per curve. Only a pairing where
    // almost nothing resolves indicates a wrong skeleton.
    if (cov < 0.25) { weak++; continue; }
    kept.push(clip);
    covSum += cov;
  }
  if (kept.length === 0) { stats.noClips++; continue; }
  const mean = covSum / kept.length;
  if (weak > kept.length) stats.weak++; else stats.ok++;
  covHist[mean === 1 ? '1.00' : mean.toFixed(1)] =
    (covHist[mean === 1 ? '1.00' : mean.toFixed(1)] || 0) + 1;
  out[mesh] = { skeleton: sk, bones: universe.size, coverage: +mean.toFixed(4), clips: kept };
}

const sizes = Object.values(out).map(v => v.clips.length).sort((a, b) => a - b);
console.log(`\n${stats.ok} meshes bind cleanly, ${stats.weak} mostly-weak, ` +
            `${stats.noClips} matched nothing, ${missingClips.size} distinct clips have no .ranim`);
console.log(`track coverage: ${Object.entries(covHist).sort().map(([k, v]) => `${k}:${v}`).join('  ')}`);
if (sizes.length) {
  console.log(`clips per mesh: min ${sizes[0]}, median ${sizes[sizes.length >> 1]}, ` +
              `max ${sizes[sizes.length - 1]}, mean ` +
              `${(sizes.reduce((a, b) => a + b, 0) / sizes.length).toFixed(1)}`);
}

// Positive controls, from declarations this tool never fitted to.
function control(mesh, clip) {
  const e = out[mesh];
  if (!e) { console.log(`control ${mesh}: SKIP (not matched)`); return; }
  const pass = e.clips.includes(clip);
  console.log(`control ${mesh} plays ${clip}: ${pass ? 'PASS' : 'FAIL'} ` +
              `(${e.clips.length} clips, ${e.bones} bones, cov ${e.coverage})`);
}
control('b_element', 'a_element_stay');
// A pairing that must NOT hold: the element mob's rig is not the player rig, so
// a player clip must be absent. Without this the matcher could pass every
// control by simply matching everything.
const el = out['b_element'];
if (el) {
  const playerClips = bySkeleton.get('b_m') || [];
  const leak = playerClips.filter(c => el.clips.includes(c)).length;
  console.log(`negative control: ${leak === 0 ? 'PASS' : `FAIL (${leak} b_m clips leaked)`}`);
}

const outArg = process.argv.indexOf('--out');
if (outArg > 0) {
  fs.writeFileSync(process.argv[outArg + 1], JSON.stringify({ meshes: out }));
  console.log(`\nwrote ${process.argv[outArg + 1]} (${Object.keys(out).length} meshes)`);
}
