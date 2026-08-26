'use strict';
//
// Stage the SKINNED equipment piece meshes into Resources/Characters so
// RanEquipView.MountPiece can Resources.Load and mount them, and copy
// equippieces.json alongside. This is the asset step that flips the
// RanEquipView.EnableSkinnedPieces feature from inert to live.
//
//   node stage-equip-pieces.js          # copy piece .rmesh + equippieces.json
//   node stage-equip-pieces.js --dry    # measure, copy nothing
//   node stage-equip-pieces.js --clean  # remove the staged piece meshes
//
// WHY COPY THE .rmesh DIRECTLY (not build a prefab)
// -------------------------------------------------
// A .rmesh imports (RanMeshImporter, ScriptedImporter "rmesh") straight into a
// prefab GameObject: the skeleton as Transforms and one Skinned/MeshRenderer per
// sub-mesh, each renderer NAMED after its sub-mesh. That is exactly what
// MountPiece instantiates, filters to the wanted sub-mesh, and rebinds onto the
// body skeleton — so no separate prefab-build pass is needed. Copying the .rmesh
// into a Resources/ folder makes it Resources.Load("Characters/<mesh>")-able;
// Unity pulls its generated material + the shared Assets/Ran/Textures PNGs into
// the build automatically (they are referenced by path/GUID at import).
//
// The copy carries a FRESH deterministic .meta GUID (md5 of its Resources path,
// the scheme stage-characters.js uses) — two assets may not share a GUID — and
// the SAME RanMeshImporter settings as the editor meshes, read from a template
// meta so this stays in sync with the project rather than hard-coding them.
//
// SOURCE: MOBILE/assets/meshes (the full extraction) rather than
// Assets/Ran/meshes, because ~331 of the 1,063 piece meshes were never imported
// into the editor project — only a subset of the extracted meshes are. Sourcing
// from the extraction stages every piece the item tables reference.
//
// UNITY STEP: after this runs, the copied .rmesh must be IMPORTED. Any batch
// Unity invocation refreshes the AssetDatabase, but the explicit command is:
//
//   "C:/Program Files/Unity 2021.3.45f2/Editor/Unity.exe" -batchmode -quit \
//     -projectPath MOBILE/unity/RanMobile \
//     -executeMethod Ran.Mobile.Assets.Editor.RanReimport.All \
//     -logFile <log>
//
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const base = path.resolve(__dirname, '../../..');
const ASSETS = path.join(base, 'MOBILE/unity/RanMobile/Assets/Ran');
const SRC_MESHES = path.join(base, 'MOBILE/assets/meshes');           // full extraction (all pieces)
const EDITOR_MESHES = path.join(ASSETS, 'meshes');                    // template meta source
const OUT = path.join(ASSETS, 'Resources/Characters');
const OUT_PREFABS = OUT;                                              // detect prefab name collisions here
const EQUIPPIECES_SRC = path.join(base, 'MOBILE/assets/equippieces.json');
const EQUIPPIECES_RES = path.join(ASSETS, 'Resources/equippieces.json');

const args = process.argv.slice(2);
const DRY = args.includes('--dry');
const CLEAN = args.includes('--clean');

// Read the RanMeshImporter meta body (everything under "ScriptedImporter:") from
// an existing editor .rmesh.meta so the staged copies use identical importer
// settings and the correct script GUID — no hard-coded values to drift.
function importerTemplate() {
  let sample = null;
  try {
    const f = fs.readdirSync(EDITOR_MESHES).find((x) => x.endsWith('.rmesh.meta'));
    if (f) sample = fs.readFileSync(path.join(EDITOR_MESHES, f), 'utf8');
  } catch { /* fall through */ }
  if (!sample) return null;
  const idx = sample.indexOf('ScriptedImporter:');
  return idx >= 0 ? sample.slice(idx) : null;
}

function metaFor(destAssetPath, tmpl) {
  const guid = crypto.createHash('md5').update(destAssetPath).digest('hex').slice(0, 32);
  return `fileFormatVersion: 2\nguid: ${guid}\n${tmpl}`;
}

function loadPieceMeshes() {
  let ep = null;
  for (const p of [EQUIPPIECES_RES, EQUIPPIECES_SRC]) {
    try { ep = JSON.parse(fs.readFileSync(p, 'utf8')); break; } catch { /* next */ }
  }
  if (!ep || !ep.pieceLinks) return null;
  return [...new Set(Object.values(ep.pieceLinks).map((l) => l.m).filter(Boolean))];
}

function copyEquipPieces() {
  // Mirror equippieces.json into Resources with a TextScriptImporter meta (same as
  // extract-equipmodels.js --stage-pieces), so the table loads at runtime.
  if (!fs.existsSync(EQUIPPIECES_SRC)) { console.log('! equippieces.json missing — run extract-equipmodels.js first'); return; }
  const json = fs.readFileSync(EQUIPPIECES_SRC);
  fs.mkdirSync(path.dirname(EQUIPPIECES_RES), { recursive: true });
  fs.writeFileSync(EQUIPPIECES_RES, json);
  const metaPath = EQUIPPIECES_RES + '.meta';
  if (!fs.existsSync(metaPath)) {
    const guid = crypto.createHash('md5').update('Assets/Ran/Resources/equippieces.json').digest('hex').slice(0, 32);
    fs.writeFileSync(metaPath,
      `fileFormatVersion: 2\nguid: ${guid}\nTextScriptImporter:\n` +
      `  externalObjects: {}\n  userData: \n  assetBundleName: \n  assetBundleVariant: \n`);
  }
  console.log(`copied equippieces.json -> ${path.relative(base, EQUIPPIECES_RES)} (${(json.length / 1024).toFixed(0)} KB)`);
}

function main() {
  const meshes = loadPieceMeshes();
  if (!meshes) { console.log('! equippieces.json has no pieceLinks — run extract-equipmodels.js first'); return; }
  console.log(`piece meshes referenced by items: ${meshes.length}`);

  if (CLEAN) {
    let removed = 0;
    if (fs.existsSync(OUT)) {
      for (const m of meshes) {
        for (const suf of ['.rmesh', '.rmesh.meta']) {
          const p = path.join(OUT, m + suf);
          if (fs.existsSync(p)) { fs.rmSync(p); removed++; }
        }
      }
    }
    console.log(`removed ${removed} staged piece files from Resources/Characters`);
    return;
  }

  const tmpl = importerTemplate();
  if (!tmpl && !DRY) { console.log('! could not read a RanMeshImporter template meta from Assets/Ran/meshes'); return; }

  const prefabStems = fs.existsSync(OUT_PREFABS)
    ? new Set(fs.readdirSync(OUT_PREFABS).filter((f) => f.endsWith('.prefab')).map((f) => f.slice(0, -7).toLowerCase()))
    : new Set();

  let toStage = 0, already = 0, missingSrc = 0, collide = 0;
  const missing = [], collisions = [];
  for (const m of meshes) {
    const src = path.join(SRC_MESHES, m + '.rmesh');
    if (!fs.existsSync(src)) { missingSrc++; if (missing.length < 12) missing.push(m); continue; }
    // A staged CHARACTER prefab of the same stem would make Resources.Load
    // ambiguous — skip and report; the piece would otherwise load the character.
    if (prefabStems.has(m)) { collide++; if (collisions.length < 12) collisions.push(m); continue; }
    const dst = path.join(OUT, m + '.rmesh');
    if (fs.existsSync(dst)) { already++; continue; }
    toStage++;
  }

  console.log(`to stage: ${toStage}  already staged: ${already}  ` +
    `source .rmesh missing: ${missingSrc}  prefab-name collision (skipped): ${collide}`);
  if (missingSrc) console.log(`  missing e.g.: ${missing.join(', ')}${missingSrc > missing.length ? ' …' : ''}`);
  if (collide) console.log(`  collisions e.g.: ${collisions.join(', ')}${collide > collisions.length ? ' …' : ''}`);

  if (DRY) {
    console.log(`(dry run — would stage ${toStage} piece .rmesh into Resources/Characters and copy equippieces.json)`);
    return;
  }

  fs.mkdirSync(OUT, { recursive: true });
  let copied = 0;
  for (const m of meshes) {
    const src = path.join(SRC_MESHES, m + '.rmesh');
    if (!fs.existsSync(src) || prefabStems.has(m)) continue;
    const dst = path.join(OUT, m + '.rmesh');
    if (fs.existsSync(dst)) continue;
    fs.copyFileSync(src, dst);
    fs.writeFileSync(dst + '.meta', metaFor(`Assets/Ran/Resources/Characters/${m}.rmesh`, tmpl));
    copied++;
  }
  const onDisk = fs.readdirSync(OUT).filter((f) => f.endsWith('.rmesh')).length;
  console.log(`staged ${copied} piece .rmesh (${already} already present); ` +
    `${onDisk} piece meshes now under Resources/Characters`);

  copyEquipPieces();

  console.log('\nNEXT: import the copied meshes in Unity (I run this), then flip the flag:');
  console.log('  "C:/Program Files/Unity 2021.3.45f2/Editor/Unity.exe" -batchmode -quit \\');
  console.log('    -projectPath MOBILE/unity/RanMobile \\');
  console.log('    -executeMethod Ran.Mobile.Assets.Editor.RanReimport.All -logFile reimport.log');
  console.log('  then set RanEquipView.EnableSkinnedPieces = true.');
}

main();
