'use strict';
//
// The BUILD LIST: every crow skin -> the exact source files that assemble it,
// plus how RanCharacterBuilder must materialise it (canonical build, clone of a
// structural twin, or an honest empty).
//
//   node gen-crowbuildlist.js                 # write Assets/Ran/Resources/crowbuildlist.json
//   node gen-crowbuildlist.js --out PATH
//   node gen-crowbuildlist.js --stdout        # print stats, write nothing
//
// WHY THIS EXISTS
// ---------------
// A crow's model on the wire is only its `.chf` STEM (see extract-crowmodels.js).
// Turning that stem into a rendered body needs THREE source classes, none of
// which the crow table names:
//
//   .chf skeleton  -> Ran/data/skeleton/<k>.x   -> meshes/<k>.rmesh   (the ".chr")
//   .cps parts     -> the .x each piece draws    -> meshes/<mesh>.rmesh
//   .ranim clips   -> Animation.rcc <clip>.bin   -> anim/<clip>.ranim
//
// gen-characters.js already followed the .chf -> .cps -> .x -> clip chain and
// wrote charflat.json; this joins THAT to the crow table so the build list is
// keyed by the exact skin stem the runtime resolves, and records for each skin:
//
//   mode = "canonical"  RanChfBuilder.BuildAll assembles it directly (first char
//                       for its skeleton|parts key).
//   mode = "clone"      a DIFFERENT canonical char has the identical skeleton +
//                       part list (BuildAll dedups 1590 chars to ~722 models), so
//                       this skin is materialised by cloning that proven twin.
//                       `twin` names it.
//   mode = "empty"      the model has NO geometry on disk (skeleton .rmesh has 0
//                       vertices and no part supplies any) — an honest gap.
//
// WHY THE CLONE COHORT IS THE ONE THAT BREAKS
// -------------------------------------------
// The clones are materialised by a SEPARATE builder pass (BuildMissing /
// RebuildCrowClones) AFTER BuildAll. Measured on this tree, the 158 clone
// prefabs were serialised by a DIFFERENT Unity than the 517 canonical ones
// (format v23 / Unity 6000.5.8f1 vs v22 / Unity 2021.3.45f2). A prefab written
// by a newer Unity does not deserialise in the older editor, so its
// Resources.Load returns null and the mob falls back to a red capsule — the
// exact 158. The fix is to rebuild the clone cohort with the SAME Unity that
// wrote the canonicals; this list is what that pass consumes.
//
// The join is DATA only — every field comes from crowmodels.json + charflat.json
// (both already validated); nothing here re-parses a binary format.
//
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const base = path.resolve(__dirname, '../../..');
const ASSETS = path.join(base, 'MOBILE/unity/RanMobile/Assets/Ran');
const CROWMODELS = path.join(ASSETS, 'Resources/crowmodels.json');
const CHARFLAT = path.join(ASSETS, 'charflat.json');
const MESHES = path.join(ASSETS, 'meshes');          // imported .rmesh (build source)
const DEFAULT_OUT = 'MOBILE/unity/RanMobile/Assets/Ran/Resources/crowbuildlist.json';

// The serialized-file format version the project's target editor writes. The
// canonical crow prefabs and every other referenced body are this version; the
// clone cohort that renders as capsules is NOT, which is the whole point.
const TARGET_SERIALIZED_VERSION = 22;   // Unity 2021.3.45f2

// Read the total vertexCount out of a .rmesh header (offset 28). Returns null
// when the file is absent, -1 when the magic is wrong. Header layout is pinned
// in extract-meshes.js: "RMSH", u32 ver, flags, boneCount, meshCount,
// submeshCount, skinBoneCount, vertexCount, ...
function rmeshVerts(stem) {
  const p = path.join(MESHES, stem + '.rmesh');
  if (!fs.existsSync(p)) return null;
  const fd = fs.openSync(p, 'r');
  const buf = Buffer.alloc(44);
  fs.readSync(fd, buf, 0, 44, 0);
  fs.closeSync(fd);
  if (buf.toString('latin1', 0, 4) !== 'RMSH') return -1;
  return buf.readUInt32LE(28);
}

function charKey(c) {
  return c.k + '|' + (c.p || []).map((p) => p.m + ':' + (p.s || '')).join(',');
}

function build() {
  const cm = JSON.parse(fs.readFileSync(CROWMODELS, 'utf8'));
  const flat = JSON.parse(fs.readFileSync(CHARFLAT, 'utf8'));

  // charflat lookups
  const byName = new Map();
  const keyFirst = new Map();               // key -> canonical (first-in-order) name
  for (const c of flat.chars) {
    const n = c.n.toLowerCase();
    if (!byName.has(n)) byName.set(n, c);
    const k = charKey(c);
    if (!keyFirst.has(k)) keyFirst.set(k, c.n);
  }
  const clipsBySkel = new Map();
  for (const s of flat.skeletons || []) clipsBySkel.set(s.n, s.c || []);

  // distinct crow skin stems, in stable order
  const seen = new Set();
  const stems = [];
  for (let i = 0; i + 1 < cm.crows.length; i += 2) {
    const s = String(cm.crows[i + 1]).toLowerCase();
    if (!seen.has(s)) { seen.add(s); stems.push(s); }
  }
  stems.sort();

  const entries = [];
  const stats = { total: 0, canonical: 0, clone: 0, empty: 0, notInFlat: 0 };
  for (const skin of stems) {
    stats.total++;
    const c = byName.get(skin);
    if (!c) { stats.notInFlat++; entries.push({ skin, mode: 'missing', skeleton: '', scale: 1, twin: '', empty: true, parts: [], clips: [] }); continue; }

    const key = charKey(c);
    const canonical = keyFirst.get(key);
    const isClone = canonical.toLowerCase() !== skin;

    // geometry: sum part verts, else skeleton verts
    let verts = 0;
    if (c.p && c.p.length) { for (const pt of c.p) { const v = rmeshVerts(pt.m); if (v > 0) verts += v; } }
    else { const v = rmeshVerts(c.k); verts = v > 0 ? v : 0; }
    const empty = verts <= 0;

    let mode = isClone ? 'clone' : 'canonical';
    if (empty) mode = 'empty';
    if (mode === 'canonical') stats.canonical++;
    else if (mode === 'clone') stats.clone++;
    else if (mode === 'empty') stats.empty++;

    entries.push({
      skin,
      mode,
      skeleton: c.k,
      scale: c.s || 1,
      twin: isClone ? canonical : '',
      empty,
      parts: (c.p || []).map((p) => ({ mesh: p.m, submesh: p.s || '' })),
    });
  }

  // Clip lists are per SKELETON (a .ranim binds by bone name, not by character),
  // so emit them ONCE per skeleton the crow skins use rather than inlined per
  // skin — the same reason gen-characters.js keys clips by skeleton. Each entry's
  // `.ranim` sources are then clipsBySkeleton[entry.skeleton].
  const usedSkels = new Set(entries.map((e) => e.skeleton).filter(Boolean));
  const skeletons = [...usedSkels].sort().map((n) => ({ n, clips: clipsBySkel.get(n) || [] }));

  return { cm, flat, entries, skeletons, stats };
}

function main() {
  const { entries, skeletons, stats } = build();
  const out = {
    note: 'crow skin -> build recipe (skeleton .rmesh + part .rmesh; .ranim clips per skeleton) ' +
          'and how RanCharacterBuilder materialises it (canonical build / clone of a structural ' +
          'twin / empty gap). Generated by gen-crowbuildlist.js. Rebuild the "clone" cohort with ' +
          'the project target editor (Unity 2021.3.45f2) so all prefabs share serialized format v' +
          TARGET_SERIALIZED_VERSION + '.',
    targetSerializedVersion: TARGET_SERIALIZED_VERSION,
    skeletons,
    entries,
  };
  const j = JSON.stringify(out);

  console.log(`crow build list: ${stats.total} skins`);
  console.log(`  canonical (RanChfBuilder.BuildAll): ${stats.canonical}`);
  console.log(`  clone (RebuildCrowClones from a v${TARGET_SERIALIZED_VERSION} twin): ${stats.clone}`);
  console.log(`  empty (no geometry on disk — honest gap): ${stats.empty}` +
              (stats.empty ? ` [${entries.filter((e) => e.mode === 'empty').map((e) => e.skin).join(', ')}]` : ''));
  if (stats.notInFlat) console.log(`  missing from charflat.json: ${stats.notInFlat}`);

  if (process.argv.includes('--stdout')) { console.log(`(json ${(j.length / 1024).toFixed(0)} KB, not written)`); return; }
  const outArg = process.argv.indexOf('--out');
  const outRel = outArg > 0 ? process.argv[outArg + 1] : DEFAULT_OUT;
  const outPath = path.join(base, outRel);
  fs.mkdirSync(path.dirname(outPath), { recursive: true });
  fs.writeFileSync(outPath, j);
  // Stable .meta GUID (md5 of the Resources path) so a component/scene that
  // references the TextAsset survives a fresh checkout — same scheme as
  // extract-crowmodels.js. Only for the default in-project location.
  const metaPath = outPath + '.meta';
  if (!fs.existsSync(metaPath)) {
    const guid = crypto.createHash('md5')
      .update('Assets/Ran/Resources/' + path.basename(outPath)).digest('hex').slice(0, 32);
    fs.writeFileSync(metaPath,
      `fileFormatVersion: 2\nguid: ${guid}\nTextScriptImporter:\n` +
      `  externalObjects: {}\n  userData: \n  assetBundleName: \n  assetBundleVariant: \n`);
  }
  console.log(`wrote ${outRel} (${(j.length / 1024).toFixed(0)} KB, ${entries.length} entries, ${skeletons.length} skeletons)`);
}

module.exports = { build, charKey, rmeshVerts, TARGET_SERIALIZED_VERSION };

if (require.main === module) main();
