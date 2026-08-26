'use strict';
//
// Pins the crow build-list decode and the 158-capsule root cause.
//
//   node test-crowbuild.js
//
// Not part of test.js (that harness is not extended here). This file exists to
// pin the facts this pass decoded:
//   * the build list splits the 675 crow skins into canonical / clone / empty;
//   * the clone cohort is EXACTLY the set of referenced skins whose prefab is
//     serialized in a version other than the project's target — the reason 158
//     mobs render as red capsules even though every prefab file exists;
//   * mob_yoyoman is the one geometry-empty skin.
//
// The version check is an INVARIANT, not a snapshot: "every version-mismatched
// referenced prefab is a clone-cohort skin". That holds BEFORE the fix (158
// mismatches, all clones) and AFTER RebuildCrowClones (0 mismatches) — so it does
// not go stale, yet it still catches a canonical body drifting to the wrong
// editor version.
const fs = require('fs');
const path = require('path');
const { build, TARGET_SERIALIZED_VERSION } = require('./gen-crowbuildlist.js');

const base = path.resolve(__dirname, '../../..');
const proj = (...p) => path.join(base, 'MOBILE/unity/RanMobile/Assets/Ran', ...p);

let pass = 0, fail = 0;
const eq = (name, got, want) => {
  if (got === want) { pass++; console.log(`  ok   ${name}`); }
  else { fail++; console.log(`  FAIL ${name}: got ${JSON.stringify(got)}, want ${JSON.stringify(want)}`); }
};
const check = (name, cond) => eq(name, !!cond, true);

function prefabVersion(dir, stem) {
  const p = path.join(dir, stem + '.prefab');
  if (!fs.existsSync(p)) return null;
  const fd = fs.openSync(p, 'r'); const buf = Buffer.alloc(12);
  fs.readSync(fd, buf, 0, 12, 0); fs.closeSync(fd);
  return buf.readUInt32BE(8);   // SerializedFile version, big-endian u32 at offset 8
}

console.log('crow build list (gen-crowbuildlist.js)');
const { entries, stats } = build();
eq('build list total skins', entries.length, 675);
eq('clone cohort size', stats.clone, 158);
eq('canonical cohort size', stats.canonical, 516);
eq('geometry-empty cohort size', stats.empty, 1);
const emptySkins = entries.filter((e) => e.mode === 'empty').map((e) => e.skin);
eq('the one empty skin is mob_yoyoman', emptySkins.join(','), 'mob_yoyoman');
check('every clone entry names a twin', entries.filter((e) => e.mode === 'clone').every((e) => e.twin));

// The twin of every clone must itself be a build target (canonical), not another
// clone — else the re-clone source would be the wrong version too.
const byName = new Map(entries.map((e) => [e.skin, e]));
const clones = entries.filter((e) => e.mode === 'clone');
check('every clone twin is a canonical (or non-crow) build, never another clone',
  clones.every((e) => { const t = byName.get(e.twin.toLowerCase()); return !t || t.mode !== 'clone'; }));

console.log('\nserialized-version invariant (the 158-capsule cause)');
const chDir = proj('Characters');
const resDir = proj('Resources', 'Characters');
if (fs.existsSync(chDir)) {
  const cloneSet = new Set(clones.map((e) => e.skin));
  // Referenced skins whose Characters/ prefab is not the target version.
  const mismatched = [];
  for (const e of entries) {
    const v = prefabVersion(chDir, e.skin);
    if (v != null && v !== TARGET_SERIALIZED_VERSION) mismatched.push({ skin: e.skin, v, mode: e.mode });
  }
  console.log(`  (measured now: ${mismatched.length} referenced prefabs are not v${TARGET_SERIALIZED_VERSION})`);
  check('every version-mismatched referenced prefab is a clone-cohort skin',
    mismatched.every((m) => cloneSet.has(m.skin)));

  // Every clone's TWIN must be present and at the target version — that is the
  // source RebuildCrowClones copies, so a byte-exact copy stays v-target.
  const twinsOk = clones.every((e) => prefabVersion(chDir, e.twin.toLowerCase()) === TARGET_SERIALIZED_VERSION
                                   || prefabVersion(chDir, e.twin) === TARGET_SERIALIZED_VERSION);
  check(`every clone's canonical twin prefab exists at v${TARGET_SERIALIZED_VERSION}`, twinsOk);

  // Positive control: the check CAN fail — a fabricated canonical at v99 is not a
  // clone, so the invariant would reject it.
  const controlOk = ![{ skin: entries.find((e) => e.mode === 'canonical').skin, v: 99 }]
    .every((m) => cloneSet.has(m.skin));
  check('control: a version-mismatched CANONICAL would be flagged (not a clone)', controlOk);
} else {
  check('Characters/ present', false);
}

// The Resources copies the runtime actually loads must, after staging, match the
// Characters/ source. Informational when they diverge (staging not re-run yet).
if (fs.existsSync(resDir)) {
  let res23 = 0;
  for (const e of clones) { const v = prefabVersion(resDir, e.skin); if (v != null && v !== TARGET_SERIALIZED_VERSION) res23++; }
  console.log(`  (Resources/Characters clone copies not v${TARGET_SERIALIZED_VERSION}: ${res23} — re-run stage-characters.js after RebuildCrowClones)`);
}

console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
