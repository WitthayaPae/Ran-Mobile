'use strict';
//
// Stage extracted assets into the Unity project tree.
//
//   node stage-unity.js --map w_school_03            one map and its textures
//   node stage-unity.js --all                        everything
//   node stage-unity.js --map X --project DIR        override the project path
//
// The importers resolve textures by BASENAME (`RanMeshImporter.textureFolder` +
// the name from the file), while `convert-textures.js` mirrors the source
// directory layout. So staging flattens.
//
// Flattening is not free: 16,072 converted PNGs share only 15,183 distinct
// basenames, so **889 names collide**. The engine has the same ambiguity — its
// references carry no directory either — and resolves by search order. This
// picks deterministically with the same preference, and reports the count so a
// wrong pick is visible rather than silent.
//
const fs = require('fs');
const path = require('path');

const ASSETS = path.join(__dirname, '..', '..', 'assets');
const argv = process.argv.slice(2);
const has = (f) => argv.includes(f);
const val = (f, d) => { const i = argv.indexOf(f); return i === -1 ? d : argv[i + 1]; };

const mapName = val('--map', null);
const all = has('--all');
const meshesOnly = has('--meshes');
const skinnedOnly = has('--skinned');
const project = val('--project', path.join(__dirname, '..', '..', 'unity', 'RanMobile'));

if (!mapName && !all && !meshesOnly) {
  console.error('usage: node stage-unity.js --map NAME | --all | --meshes [--skinned] [--project DIR]');
  process.exit(2);
}

const DEST = path.join(project, 'Assets', 'Ran');
const TEX_SRC = path.join(ASSETS, 'textures');

// Preference order when a basename collides. `textures/` is the runtime texture
// root, `data/` holds per-format working copies, `cache/` is generated.
const PREFERENCE = ['textures', 'data', 'cache'];

function buildTextureIndex() {
  const index = new Map();     // basename.png -> full path
  let files = 0, collisions = 0;
  (function walk(dir) {
    let entries;
    try { entries = fs.readdirSync(dir, { withFileTypes: true }); } catch { return; }
    for (const e of entries) {
      const p = path.join(dir, e.name);
      if (e.isDirectory()) { walk(p); continue; }
      if (!e.name.toLowerCase().endsWith('.png')) continue;
      files++;
      const key = e.name.toLowerCase();
      const existing = index.get(key);
      if (!existing) { index.set(key, p); continue; }
      collisions++;
      if (rank(p) < rank(existing)) index.set(key, p);
    }
  })(TEX_SRC);
  return { index, files, collisions };
}

/** Lower is preferred. */
function rank(p) {
  const rel = path.relative(TEX_SRC, p).split(path.sep)[0].toLowerCase();
  const i = PREFERENCE.indexOf(rel);
  return i === -1 ? PREFERENCE.length : i;
}

function copy(src, destDir, name) {
  fs.mkdirSync(destDir, { recursive: true });
  const dest = path.join(destDir, name);
  fs.copyFileSync(src, dest);
  return fs.statSync(dest).size;
}

const { index, files, collisions } = buildTextureIndex();
console.log(`texture index: ${files.toLocaleString()} PNGs, ` +
            `${index.size.toLocaleString()} distinct names, ${collisions} collisions resolved`);

/**
 * Stage character meshes and the animation clips they can play.
 *
 * Meshes and clips are staged whole rather than per-map: a `.ranim` names bones
 * only, so any clip can drive any skeleton with matching names — the pairing is
 * not knowable from the files, and picking a subset would silently decide it.
 */
/**
 * Texture names referenced by a `.rmesh`.
 *
 * Reads the string pool and the submesh table only — positions, indices and
 * skin weights are skipped, because staging needs the NAMES and a full parse of
 * 1,993 meshes to reach them would be minutes of work for bytes that are
 * already at a known offset.
 *
 * Header is 44 bytes (see extract-meshes.js): "RMSH", version, flags,
 * boneCount, meshCount, submeshCount, skinBoneCount, vertexCount, indexCount,
 * stringBytes — then the string pool.
 */
function meshTextures(file) {
  const fd = fs.openSync(file, 'r');
  try {
    const head = Buffer.alloc(44);
    if (fs.readSync(fd, head, 0, 44, 0) < 44) return [];

    const boneCount = head.readUInt32LE(12);
    const meshCount = head.readUInt32LE(16);
    const submeshCount = head.readUInt32LE(20);
    const stringBytes = head.readUInt32LE(36);
    if (!submeshCount || !stringBytes) return [];

    const strings = Buffer.alloc(stringBytes);
    fs.readSync(fd, strings, 0, stringBytes, 44);

    // Submeshes sit after strings, bones and meshes.
    const at = 44 + stringBytes + boneCount * 72 + meshCount * 40;
    const sub = Buffer.alloc(submeshCount * 12);
    if (fs.readSync(fd, sub, 0, sub.length, at) < sub.length) return [];

    const names = new Set();
    for (let i = 0; i < submeshCount; i++) {
      const off = sub.readUInt32LE(i * 12);
      if (off >= stringBytes) continue;          // out of pool: not a name
      let end = off;
      while (end < stringBytes && strings[end] !== 0) end++;
      const n = strings.toString('latin1', off, end).trim();
      if (n) names.add(n);
    }
    return [...names];
  } catch {
    return [];
  } finally {
    fs.closeSync(fd);
  }
}

function stageMeshes() {
  const meshDir = path.join(ASSETS, 'meshes');
  const animDir = path.join(ASSETS, 'anim');
  let meshes = 0, clips = 0, bytes = 0, skipped = 0;
  const wantTex = new Set();

  for (const f of fs.existsSync(meshDir) ? fs.readdirSync(meshDir) : []) {
    if (!f.endsWith('.rmesh')) continue;
    const src = path.join(meshDir, f);
    if (skinnedOnly) {
      // flags bit0 marks skinning; skip the 2,474 rigid props when only
      // characters are wanted.
      const head = Buffer.alloc(12);
      const fd = fs.openSync(src, 'r');
      fs.readSync(fd, head, 0, 12, 0);
      fs.closeSync(fd);
      if ((head.readUInt32LE(8) & 1) === 0) { skipped++; continue; }
    }
    bytes += copy(src, path.join(DEST, 'meshes'), f);
    meshes++;
    for (const t of meshTextures(src)) wantTex.add(t);
  }

  for (const f of fs.existsSync(animDir) ? fs.readdirSync(animDir) : []) {
    if (!f.endsWith('.ranim')) continue;
    bytes += copy(path.join(animDir, f), path.join(DEST, 'anim'), f);
    clips++;
  }

  // Stage the textures those meshes NAME.
  //
  // This was the gap behind 15% of character materials rendering untextured:
  // meshes and clips were staged here, but textures only ever arrived through
  // the MAP path, off a map manifest. A character texture therefore landed
  // only if some map happened to reference it, and the ones no map used —
  // costumes, NPC skins — silently came out white. White is the worst
  // possible symptom because it looks like a lighting bug, not a missing file.
  let tex = 0, texMissing = 0;
  const missingTex = [];
  for (const name of wantTex) {
    const png = name.replace(/\.[^.]+$/, '') + '.png';
    const src = index.get(png.toLowerCase());
    if (!src) { texMissing++; if (missingTex.length < 8) missingTex.push(name); continue; }
    bytes += copy(src, path.join(DEST, 'Textures'), png);
    tex++;
  }

  console.log(`staged ${meshes} meshes${skipped ? ` (${skipped} rigid skipped)` : ''}, ` +
              `${clips} clips, ${tex} textures` +
              (texMissing ? ` (${texMissing} not found)` : '') +
              `, ${(bytes / 1048576).toFixed(1)}M`);
}

if (meshesOnly) { stageMeshes(); process.exit(0); }

const maps = all
  ? fs.readdirSync(path.join(ASSETS, 'maps')).filter((f) => f.endsWith('.map.json'))
      .map((f) => f.replace(/\.map\.json$/, ''))
  : [mapName];

let staged = 0, missing = 0, bytes = 0;
const missingNames = [];

for (const name of maps) {
  const manifestPath = path.join(ASSETS, 'maps', name + '.map.json');
  if (!fs.existsSync(manifestPath)) {
    console.error(`  ! no manifest for ${name}`);
    continue;
  }
  const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));

  // The manifest itself, and whichever asset files it actually names.
  //
  // Renamed to a SINGLE extension on the way in. Unity's ScriptedImporter
  // matches only the last one, so `w_school_03.map.json` registers as `json` —
  // `RanMapImporter` declares `mapjson` and would never fire, and claiming
  // `json` instead would hijack every JSON asset in the project.
  copy(manifestPath, path.join(DEST, 'Maps'), name + '.mapjson');
  // Each part goes into the directory its own `file` field names — `terrain/`,
  // `mapobj/`, `navmesh/`. Not a prettier capitalised layout: `RanMapImporter`
  // resolves `assetRoot + part.file` and AssetDatabase paths are CASE-SENSITIVE
  // even on Windows, so `Terrain/` here silently fails to load every part.
  for (const part of [manifest.terrain, manifest.objects, manifest.navmesh]) {
    if (!part || !part.file) continue;
    const src = path.join(ASSETS, part.file);
    if (!fs.existsSync(src)) { console.error(`  ! missing ${part.file}`); continue; }
    bytes += copy(src, path.join(DEST, path.dirname(part.file)),
                  path.basename(part.file));
  }
  // The terrain importer derives this sidecar from the .terrain asset path, so
  // it has to sit beside it.
  const sidecar = path.join(ASSETS, 'terrain', name + '.textures.json');
  if (fs.existsSync(sidecar)) {
    bytes += copy(sidecar, path.join(DEST, 'terrain'), name + '.textures.json');
  }

  for (const tex of manifest.textures || []) {
    // References name .dds/.tga/...; conversion produced .png.
    const png = path.basename(tex).replace(/\.[^.]*$/, '') + '.png';
    const src = index.get(png.toLowerCase());
    if (!src) {
      missing++;
      if (missingNames.length < 10) missingNames.push(tex);
      continue;
    }
    bytes += copy(src, path.join(DEST, 'Textures'), png);
    staged++;
  }
}

console.log(`staged ${maps.length} map(s): ${staged.toLocaleString()} textures, ` +
            `${missing} unresolved, ${(bytes / 1048576).toFixed(1)}M written`);
console.log(`  -> ${DEST}`);
for (const n of missingNames) console.log(`  ? ${n}`);
