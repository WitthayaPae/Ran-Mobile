'use strict';
//
// Export the clip -> action mapping a Unity Animator is built from.
//
//   node extract-animtypes.js --list            inventory, nothing written
//   node extract-animtypes.js --out FILE        write the JSON map
//   node extract-animtypes.js --out FILE --pretty
//   node extract-animtypes.js --clip a_w_walk   dump one record and exit
//
// ---------------------------------------------------------------------------
// What the key is, and why it is NOT m_szName
// ---------------------------------------------------------------------------
//
// The engine addresses a clip by FILENAME, never by the name stored inside the
// record. `DxSkinCharData` holds a list of animation file names; `DxSkinAniMan::
// LoadAnimContainer` swaps `.x` for `.cfg` and loads that path, then loads the
// `.bin` of the same stem. So `<stem>.cfg`, `<stem>.bin` and the exported
// `<stem>.ranim` are one clip, and the stem is the identity.
//
// `SANIMCONINFO::m_szName` agrees with the stem 6,098 times out of 6,161 —
// close enough to look like the key, and wrong. The 63 exceptions are variant
// copies (`a_m_e_048_o_m3.cfg` stores `a_m_e_048.x`) plus one authoring slip
// (`a_m_17_die.cfg` stores `a_m_01_die.x`). Keying on m_szName would collapse
// seven distinct `a_m_e_048_o_m*` clips into one entry and mislabel a death
// animation. The stored name is kept as `recordName` for traceability.
//
// ---------------------------------------------------------------------------
// Coverage is partial by construction
// ---------------------------------------------------------------------------
//
// 6,161 `.cfg` ship against 6,498 `.bin`. A `.bin` with no `.cfg` is not a
// broken export — `LoadAnimContainer` explicitly tolerates it, synthesises a
// default SANIMCONINFO and writes the file out on the spot (DxSkinAniMan.cpp:
// 574-601). Those clips default to `AN_GUARD_N` / `AN_SUB_NONE` in the engine,
// and are reported here as absent rather than invented.
//
const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc');
const AI = require('./animinfo');

const RAN = path.join(__dirname, '..', '..', '..', 'Ran');
const ANIM_ASSETS = path.join(__dirname, '..', '..', 'assets', 'anim');

// Measured, not assumed — see MOBILE/README.md: every one of 14.3M inter-key
// gaps in the shipped corpus is an exact multiple of 160 ticks, i.e. 160 ticks
// per frame at 30 fps. `m_UNITTIME` in the .cfg is 160 for 6,031 of 6,037
// clips that have both files, which is an independent confirmation from the
// other side of the format.
const TICKS_PER_SECOND = 4800;

// Archives that carry `.cfg` records. SkinObject ships 30 of them alongside the
// character containers; they are real clips and are included.
const CFG_SOURCES = [
  path.join(RAN, 'data', 'animation', 'Animation.rcc'),
  path.join(RAN, 'data', 'skinobject', 'SkinObject.rcc'),
];
const CHAR_SOURCE = path.join(RAN, 'data', 'skinobject', 'SkinObject.rcc');

const argv = process.argv.slice(2);
const has = (f) => argv.includes(f);
const val = (f, d) => { const i = argv.indexOf(f); return i === -1 ? d : argv[i + 1]; };

const outFile = val('--out', null);
const listOnly = has('--list');
const oneClip = val('--clip', null);
const pretty = has('--pretty');

if (!outFile && !listOnly && !oneClip) {
  console.error('usage: node extract-animtypes.js --list | --out FILE [--pretty] | --clip NAME');
  process.exit(2);
}

const stemOf = (name) => name.toLowerCase().replace(/\.[^.\\/]*$/, '');

/** Read every `.cfg` out of the archives and key it by file stem. */
function collectClips() {
  const clips = new Map();
  const failures = [];
  const versions = new Map();

  for (const archivePath of CFG_SOURCES) {
    const rcc = new RccArchive(archivePath);
    const archive = path.basename(archivePath);
    for (const entry of rcc.entries) {
      if (!/\.cfg$/i.test(entry.name)) continue;
      const stem = stemOf(entry.name);
      let rec;
      try {
        rec = AI.parseAnimInfo(rcc.read(entry));
      } catch (err) {
        failures.push({ archive, file: entry.name, error: err.message });
        continue;
      }
      versions.set(rec.version, (versions.get(rec.version) || 0) + 1);

      // Later archives win only if the stem is new; Animation.rcc is canonical.
      if (clips.has(stem)) continue;

      clips.set(stem, {
        mainType: rec.mainType,
        subType: rec.subType,
        mainTypeName: rec.mainTypeName,
        subTypeName: rec.subTypeName,
        startTime: rec.startTime,
        endTime: rec.endTime,
        endTimeOrig: rec.endTimeOrig,
        unitTime: rec.unitTime,
        skeleton: rec.skeleton,
        loop: rec.loop,
        upperBody: rec.upperBody,
        lowerBody: rec.lowerBody,
        divFrames: rec.divFrames,
        strikeFrames: rec.strikes.map((s) => s.frame),
        soundFiles: rec.sound ? rec.sound.files : [],
        recordName: rec.name,
        sourceVersion: rec.version,
        sourceArchive: archive,
      });
    }
  }
  return { clips, failures, versions };
}

/** Read every `.chf`/`.abf` — the character -> clip-list half of the mapping. */
function collectCharacters() {
  const rcc = new RccArchive(CHAR_SOURCE);
  const characters = new Map();
  const failures = [];
  for (const entry of rcc.entries) {
    if (!/\.(chf|abf)$/i.test(entry.name)) continue;
    try {
      const rec = AI.parseCharContainer(rcc.read(entry), entry.name);
      characters.set(entry.name.toLowerCase(), {
        kind: /\.abf$/i.test(entry.name) ? 'abf' : 'chf',
        version: rec.version,
        skeleton: rec.skeleton,
        scale: rec.scale,
        height: rec.height,
        radius: rec.radius,
        pieces: rec.pieces.filter(Boolean),
        clips: rec.animStems,
      });
    } catch (err) {
      failures.push({ file: entry.name, error: err.message });
    }
  }
  return { characters, failures };
}

function shippedRanimStems() {
  if (!fs.existsSync(ANIM_ASSETS)) return null;
  return new Set(fs.readdirSync(ANIM_ASSETS)
    .filter((f) => f.endsWith('.ranim'))
    .map((f) => f.slice(0, -'.ranim'.length).toLowerCase()));
}

function main() {
  const { clips, failures, versions } = collectClips();

  if (oneClip) {
    const rec = clips.get(stemOf(oneClip));
    if (!rec) {
      console.error(`no clip "${oneClip}" — ${clips.size} known`);
      process.exit(1);
    }
    console.log(JSON.stringify({ clip: stemOf(oneClip), ...rec }, null, 2));
    return;
  }

  const { characters, failures: charFailures } = collectCharacters();
  const ranim = shippedRanimStems();

  // Distribution, and coverage against the clips that actually shipped.
  const byMain = new Map();
  for (const rec of clips.values()) {
    byMain.set(rec.mainType, (byMain.get(rec.mainType) || 0) + 1);
  }

  let matched = 0;
  if (ranim) for (const stem of clips.keys()) if (ranim.has(stem)) matched++;

  console.log(`.cfg records ......... ${clips.size} (${failures.length} failed)`);
  console.log(`.chf/.abf containers . ${characters.size} (${charFailures.length} failed)`);
  if (ranim) {
    console.log(`shipped .ranim clips . ${ranim.size}`);
    console.log(`typed ................ ${matched} (${(100 * matched / ranim.size).toFixed(1)}% of shipped clips)`);
  }
  console.log('');
  console.log('SANIMCONINFO versions:');
  for (const [v, n] of [...versions].sort((a, b) => a[0] - b[0])) {
    console.log(`  0x${v.toString(16).padStart(4, '0')}  ${String(n).padStart(5)}`);
  }
  console.log('');
  console.log('EMANI_MAINTYPE distribution:');
  for (const [t, n] of [...byMain].sort((a, b) => b[1] - a[1])) {
    console.log(`  ${String(t).padStart(3)}  ${(AI.mainTypeName(t) || '(unnamed)').padEnd(16)} ${String(n).padStart(5)}`);
  }
  for (const f of failures.slice(0, 10)) console.log(`  [fail] ${f.file}: ${f.error}`);

  if (listOnly) return;

  const mainTypes = {};
  AI.MAIN_TYPE_NAMES.forEach((name, i) => { mainTypes[i] = name; });
  const subTypes = {};
  AI.SUB_TYPE_NAMES.forEach((name, i) => { subTypes[i] = name; });

  const out = {
    note: 'clip -> EMANI_MAINTYPE/EMANI_SUBTYPE, from SANIMCONINFO .cfg records. '
        + 'Keys are file stems, matching <stem>.ranim and <stem>.bin. '
        + 'Enum values and struct layouts come from MOBILE/tools/layout-probe.',
    generated: new Date().toISOString(),
    ticksPerSecond: TICKS_PER_SECOND,
    counts: {
      clips: clips.size,
      clipFailures: failures.length,
      characters: characters.size,
      characterFailures: charFailures.length,
      shippedRanim: ranim ? ranim.size : null,
      typedShippedRanim: ranim ? matched : null,
    },
    mainTypes,
    subTypes,
    clips: Object.fromEntries([...clips].sort((a, b) => (a[0] < b[0] ? -1 : 1))),
    characters: Object.fromEntries([...characters].sort((a, b) => (a[0] < b[0] ? -1 : 1))),
  };

  fs.mkdirSync(path.dirname(path.resolve(outFile)), { recursive: true });
  fs.writeFileSync(outFile, JSON.stringify(out, null, pretty ? 2 : 0));
  const kb = (fs.statSync(outFile).size / 1024).toFixed(0);
  console.log('');
  console.log(`wrote ${outFile} (${kb} KB)`);
}

main();
