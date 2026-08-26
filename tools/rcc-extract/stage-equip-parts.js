'use strict';
//
// Stage the FULL resolvable attach-part set into Resources/Characters so
// RanEquipView can Resources.Load every worn .abf, including the ones reached
// only through an abfAlias.
//
//   node stage-equip-parts.js          # copy the prefabs; skip ones already there
//   node stage-equip-parts.js --dry    # measure, copy nothing
//
// WHY THIS EXISTS ALONGSIDE stage-characters.js --scope equip
// ----------------------------------------------------------
// `stage-characters.js --scope equip` stages the .abf prefab named by each item
// link. That misses two things equipmodels.json now encodes:
//   * abfAlias targets — the dedup REPRESENTATIVE a collapsed part resolves to.
//     Nearly all are themselves worn by some item and so already staged, but a
//     few (e.g. angel_wings_a, whose only wearers are angel_wings_b/…) are named
//     only as an alias target and would otherwise be absent at runtime.
// This stages the union: every own-name .abf that has a prefab, PLUS every
// abfAlias target. It is idempotent — existing copies are skipped — so it is
// safe to run after RanChfBuilder.BuildAll and after stage-characters.js.
//
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const base = path.resolve(__dirname, '../../..');
const ASSETS = path.join(base, 'MOBILE/unity/RanMobile/Assets/Ran');
const CH = path.join(ASSETS, 'Characters');
const OUT = path.join(ASSETS, 'Resources/Characters');
const EQUIPMODELS = path.join(ASSETS, 'Resources/equipmodels.json');
const EQUIPMODELS_FALLBACK = path.join(base, 'MOBILE/assets/equipmodels.json');
const DRY = process.argv.includes('--dry');

function loadEquip() {
  for (const p of [EQUIPMODELS, EQUIPMODELS_FALLBACK]) {
    try { return JSON.parse(fs.readFileSync(p, 'utf8')); } catch { /* try next */ }
  }
  return null;
}

function main() {
  const em = loadEquip();
  if (!em) { console.log('! equipmodels.json not found — run extract-equipmodels.js first'); return; }

  const prefabs = new Set(fs.readdirSync(CH)
    .filter((f) => f.endsWith('.prefab')).map((f) => f.slice(0, -7).toLowerCase()));

  // The runtime tries "<abf>.abf", then "<abf>.chf", then "<abf>".
  const candidatesFor = (abf) => [`${abf}.abf`, `${abf}.chf`, abf];

  const alias = em.abfAlias || {};
  const linkAbf = [...new Set(Object.values(em.links || {}).map((l) => String(l.abf).toLowerCase()))];

  // Every .abf that must LOAD at runtime resolves through one prefab: its own if
  // it has one, else its alias target's. The set of prefabs to STAGE is that
  // union of resolved prefabs.
  const need = new Set();          // prefab stems to stage
  const prefabFor = (abf) => candidatesFor(abf).find((c) => prefabs.has(c)) || null;
  let resolvedOwn = 0, resolvedAlias = 0, unresolved = 0;
  const missing = [];
  for (const abf of linkAbf) {
    let hit = prefabFor(abf);
    if (hit) { resolvedOwn++; need.add(hit); continue; }
    const rep = alias[abf];
    hit = rep ? prefabFor(rep) : null;
    if (hit) { resolvedAlias++; need.add(hit); continue; }
    unresolved++; if (missing.length < 20) missing.push(abf);
  }
  // A handful of alias TARGETS are named by no item directly (only as a target);
  // include them so the alias always resolves.
  for (const t of Object.values(alias)) { const h = prefabFor(String(t).toLowerCase()); if (h) need.add(h); }

  console.log(`distinct item-worn .abf: ${linkAbf.length}`);
  console.log(`  resolve to own prefab: ${resolvedOwn}; via abfAlias: ${resolvedAlias}; ` +
    `unresolvable (art not shipped): ${unresolved}`);
  console.log(`prefabs to stage (union of resolved): ${need.size}`);
  if (unresolved) console.log(`  unresolvable e.g.: ${missing.join(', ')}${unresolved > missing.length ? ' …' : ''}`);

  if (DRY) { console.log('(dry run — copied nothing)'); return; }

  fs.mkdirSync(OUT, { recursive: true });
  let copied = 0, skipped = 0;
  for (const stem of need) {
    const src = path.join(CH, `${stem}.prefab`);
    const dst = path.join(OUT, `${stem}.prefab`);
    if (fs.existsSync(dst)) { skipped++; continue; }
    fs.copyFileSync(src, dst);
    const guid = crypto.createHash('md5')
      .update(`Assets/Ran/Resources/Characters/${stem}.prefab`).digest('hex').slice(0, 32);
    fs.writeFileSync(`${dst}.meta`,
      `fileFormatVersion: 2\nguid: ${guid}\nPrefabImporter:\n` +
      `  externalObjects: {}\n  userData: \n  assetBundleName: \n  assetBundleVariant: \n`);
    copied++;
  }
  const onDisk = fs.readdirSync(OUT).filter((f) => f.endsWith('.prefab')).length;
  console.log(`staged ${copied} (${skipped} already present); ${onDisk} prefabs now under Resources/Characters`);
}

main();
