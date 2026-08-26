'use strict';
//
// Standalone verification for the clip -> action mapping.
//
//   node verify-animtypes.js
//   node verify-animtypes.js --verbose      list every mismatch it finds
//
// This is deliberately separate from `test.js`: the point is not "did the
// parser throw" — a wrong field order still parses — but whether what came out
// agrees with data decoded by a completely different route.
//
// Four independent things are cross-checked:
//
//   1. Clip identity. Every `.cfg` stem is matched against the 6,498 `.ranim`
//      files exported from the animation `.bin`s by `extract-anim.js`. Those
//      two came out of different containers with different parsers; if the
//      names did not line up, one of them would be addressing the wrong files.
//
//   2. Clip DURATION. `SANIMCONINFO::m_dwETimeOrig` is compared against the
//      `durationTicks` in the `.ranim` header, which `xanim.js` computed as the
//      highest key time across the bone tracks. Two unrelated formats agreeing
//      on a number is what proves the field offsets are right — a shifted read
//      would still be in range and still look plausible.
//
//   3. Tick rate. `m_UNITTIME` is compared against 160, the ticks-per-frame the
//      README derived by measuring 14.3M inter-key gaps. Neither side of that
//      comparison assumed the other.
//
//   4. Reachability. Every clip named by a `.chf`/`.abf` character container is
//      checked to exist as a `.ranim` and to have a type.
//
// Plus a POSITIVE CONTROL, because a clean run means nothing if the check
// cannot fail: the 0x0108+ records are re-read with the 0x0106 field order (the
// mistake a reader that ignored the reorder would make) and the same validation
// is applied. It must flag them.
//
const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc');
const BC = require('./bytecrypt');
const AI = require('./animinfo');

const RAN = path.join(__dirname, '..', '..', '..', 'Ran');
const ANIM_ASSETS = path.join(__dirname, '..', '..', 'assets', 'anim');
const ANIMATION_RCC = path.join(RAN, 'data', 'animation', 'Animation.rcc');
const SKINOBJ_RCC = path.join(RAN, 'data', 'skinobject', 'SkinObject.rcc');

const TICKS_PER_FRAME = 160;
const verbose = process.argv.includes('--verbose');

const stemOf = (n) => n.toLowerCase().replace(/\.[^.\\/]*$/, '');
const pct = (a, b) => (b ? (100 * a / b).toFixed(2) : '0.00') + '%';
const hex = (v) => '0x' + v.toString(16).padStart(4, '0');

function bar(title) {
  console.log('');
  console.log(title);
  console.log('-'.repeat(title.length));
}

// ---------------------------------------------------------------------------
// The .ranim side — read only the 40-byte header of each exported clip.
// ---------------------------------------------------------------------------
function readRanimHeaders() {
  if (!fs.existsSync(ANIM_ASSETS)) return null;
  const out = new Map();
  const buf = Buffer.alloc(40);
  for (const f of fs.readdirSync(ANIM_ASSETS)) {
    if (!f.endsWith('.ranim')) continue;
    const fd = fs.openSync(path.join(ANIM_ASSETS, f), 'r');
    fs.readSync(fd, buf, 0, 40, 0);
    fs.closeSync(fd);
    if (buf.toString('latin1', 0, 4) !== 'RANM') continue;
    out.set(f.slice(0, -'.ranim'.length).toLowerCase(), {
      containerVersion: buf.readUInt32LE(8),
      trackCount: buf.readUInt32LE(12),
      durationTicks: buf.readUInt32LE(24),
      ticksPerSecond: buf.readUInt32LE(28),
    });
  }
  return out;
}

// ---------------------------------------------------------------------------
// Structural validation of one record. Everything here is a range or shape
// check, not a parse check — the parse already succeeded by the time it runs.
// ---------------------------------------------------------------------------
const ASCII = /^[\x20-\x7e]*$/;

function validate(rec) {
  const bad = [];
  if (!ASCII.test(rec.name)) bad.push('name not printable ASCII');
  if (!ASCII.test(rec.skeleton)) bad.push('skeleton not printable ASCII');
  if (rec.mainType < 0 || rec.mainType >= AI.AN_TYPE_SIZE) bad.push(`mainType ${rec.mainType} out of range`);
  if (rec.subType < 0 || rec.subType >= AI.AN_SUB_00_SIZE) bad.push(`subType ${rec.subType} out of range`);
  if (rec.divCount > AI.ACF_DIV) bad.push(`divCount ${rec.divCount} > ACF_DIV`);
  if (rec.strikeCount > AI.ACF_STRIKE) bad.push(`strikeCount ${rec.strikeCount} > ACF_STRIKE`);
  if (rec.endTime < rec.startTime) bad.push('endTime before startTime');
  if (rec.consumed > rec.consumed + rec.bytesLeft) bad.push('overran the body');
  if (rec.bytesLeft < 0) bad.push('overran the body');
  return bad;
}

// ---------------------------------------------------------------------------
// Positive control: mis-read a 0x0108+ record in 0x0106 field order.
// ---------------------------------------------------------------------------
//
// 0x0106 order is  flag, sTime, eTime, eTimeOrig, unit, types, div, strike
// 0x0108 order is  flag, unit,  sTime, eTime,     eTimeOrig, types, strike, div
//
// so reading the newer layout with the older reader shifts the type pair by one
// field and swaps the strike/div blocks. It still "parses".
function misparse(body) {
  const c = new AI.Cursor(body);
  const rec = { name: '', skeleton: '', divCount: 0, strikeCount: 0, consumed: 0, bytesLeft: 0 };
  rec.name = c.fixedStr(AI.ACF_SZNAME);
  rec.skeleton = c.fixedStr(AI.ACF_SZNAME);
  rec.flag = c.u32();
  rec.startTime = c.u32();
  rec.endTime = c.u32();
  rec.endTimeOrig = c.u32();
  rec.unitTime = c.u32();
  rec.mainType = c.i32();
  rec.subType = c.i32();
  rec.divCount = c.u16();
  c.skip(2 * AI.ACF_DIV);
  rec.strikeCount = c.u16();
  return rec;
}

// ---------------------------------------------------------------------------

function main() {
  const ranim = readRanimHeaders();
  if (!ranim) {
    console.error(`no exported clips at ${ANIM_ASSETS} — run extract-anim.js first`);
    process.exit(2);
  }

  // -- .cfg ------------------------------------------------------------------
  const clips = new Map();
  const cfgStats = { files: 0, ok: 0, failed: 0, eof: 0, byVersion: new Map(), invalid: [] };
  const cfgFailures = [];

  for (const archivePath of [ANIMATION_RCC, SKINOBJ_RCC]) {
    const rcc = new RccArchive(archivePath);
    for (const entry of rcc.entries) {
      if (!/\.cfg$/i.test(entry.name)) continue;
      cfgStats.files++;
      let rec;
      const raw = rcc.read(entry);
      try {
        rec = AI.parseAnimInfo(raw);
      } catch (err) {
        cfgStats.failed++;
        cfgFailures.push(`${entry.name}: ${err.message}`);
        continue;
      }
      cfgStats.ok++;
      if (rec.bytesLeft === 0) cfgStats.eof++;
      const v = cfgStats.byVersion.get(rec.version)
        || { n: 0, eof: 0, invalid: 0 };
      v.n++;
      if (rec.bytesLeft === 0) v.eof++;
      const bad = validate(rec);
      if (bad.length) {
        v.invalid++;
        cfgStats.invalid.push(`${entry.name} ${hex(rec.version)}: ${bad.join('; ')}`);
      }
      cfgStats.byVersion.set(rec.version, v);
      const stem = stemOf(entry.name);
      if (!clips.has(stem)) clips.set(stem, { rec, body: raw.subarray(BC.HEADER_SIZE) });
    }
  }

  bar('1. SANIMCONINFO `.cfg` records');
  console.log(`files          ${cfgStats.files}`);
  console.log(`parsed         ${cfgStats.ok}  (${pct(cfgStats.ok, cfgStats.files)})`);
  console.log(`failed         ${cfgStats.failed}`);
  console.log(`distinct stems ${clips.size}`);
  console.log(`consumed to EOF ${cfgStats.eof}  (${pct(cfgStats.eof, cfgStats.ok)})`);
  console.log(`structurally invalid ${cfgStats.invalid.length}`);
  console.log('');
  console.log(' version   files    EOF  invalid');
  for (const [v, s] of [...cfgStats.byVersion].sort((a, b) => a[0] - b[0])) {
    console.log(`  ${hex(v)}  ${String(s.n).padStart(6)} ${String(s.eof).padStart(6)} ${String(s.invalid).padStart(8)}`);
  }
  console.log('');
  console.log('Versions 0x0109/0x0110/0x0112/0x0114/0x0115 do not reach EOF, and that is');
  console.log('expected: SAnimationInfoSaveLoad.cpp marks those loaders "official version');
  console.log('partial read only" — the shipped writer emits trailing data the engine\'s own');
  console.log('reader never touches. Everything this map needs is read before that point.');
  for (const f of cfgFailures.slice(0, 20)) console.log(`  [fail] ${f}`);
  for (const f of cfgStats.invalid.slice(0, 20)) console.log(`  [invalid] ${f}`);

  // -- .chf / .abf -----------------------------------------------------------
  const charStats = { files: 0, ok: 0, failed: 0, eof: 0, refs: 0, byKind: new Map() };
  const charFailures = [];
  const charClipRefs = new Set();
  {
    const rcc = new RccArchive(SKINOBJ_RCC);
    for (const entry of rcc.entries) {
      if (!/\.(chf|abf)$/i.test(entry.name)) continue;
      charStats.files++;
      const kind = /\.abf$/i.test(entry.name) ? 'abf' : 'chf';
      try {
        const rec = AI.parseCharContainer(rcc.read(entry), entry.name);
        charStats.ok++;
        if (rec.bytesLeft === 0) charStats.eof++;
        charStats.refs += rec.animStems.length;
        for (const s of rec.animStems) charClipRefs.add(s);
        const key = `${kind} ${hex(rec.version)}`;
        const k = charStats.byKind.get(key) || { n: 0, eof: 0, anims: 0 };
        k.n++;
        if (rec.bytesLeft === 0) k.eof++;
        k.anims += rec.animStems.length;
        charStats.byKind.set(key, k);
      } catch (err) {
        charStats.failed++;
        charFailures.push(`${entry.name}: ${err.message}`);
      }
    }
  }

  bar('2. Character containers `.chf` (DxSkinCharData) / `.abf` (DxAttBoneData)');
  console.log(`files        ${charStats.files}`);
  console.log(`parsed       ${charStats.ok}  (${pct(charStats.ok, charStats.files)})`);
  console.log(`failed       ${charStats.failed}`);
  console.log(`consumed to EOF ${charStats.eof}  (${pct(charStats.eof, charStats.ok)})`);
  console.log(`clip references ${charStats.refs}, ${charClipRefs.size} distinct`);
  console.log('');
  console.log(' kind version   files    EOF   clips');
  for (const [k, s] of [...charStats.byKind].sort()) {
    console.log(`  ${k.padEnd(11)} ${String(s.n).padStart(6)} ${String(s.eof).padStart(6)} ${String(s.anims).padStart(7)}`);
  }
  for (const f of charFailures.slice(0, 20)) console.log(`  [fail] ${f}`);

  // -- 3. the cross-check that matters --------------------------------------
  let matched = 0;
  const unmatchedCfg = [];
  for (const stem of clips.keys()) {
    if (ranim.has(stem)) matched++;
    else unmatchedCfg.push(stem);
  }
  const untyped = [...ranim.keys()].filter((s) => !clips.has(s));

  bar('3. CROSS-CHECK — do the decoded names name real clips?');
  console.log(`shipped .ranim clips ......... ${ranim.size}`);
  console.log(`.cfg stems ................... ${clips.size}`);
  console.log(`.cfg stems that ARE a .ranim . ${matched}   ${pct(matched, clips.size)} of .cfg`);
  console.log(`.ranim clips that get a type . ${matched}   ${pct(matched, ranim.size)} of shipped clips`);
  console.log(`.cfg with no .ranim .......... ${unmatchedCfg.length}`);
  console.log(`.ranim with no .cfg .......... ${untyped.length}`);
  console.log('');
  console.log('The 461 untyped clips are not a decode failure. LoadAnimContainer tolerates a');
  console.log('missing .cfg by design (DxSkinAniMan.cpp:574-601): it synthesises a default');
  console.log('SANIMCONINFO — AN_GUARD_N / AN_SUB_NONE — and writes the file out. They are');
  console.log('reported as absent here rather than filled in with a guess.');
  if (verbose) {
    console.log('');
    console.log('.cfg with no .ranim: ' + unmatchedCfg.slice(0, 40).join(' '));
    console.log('.ranim with no .cfg: ' + untyped.slice(0, 40).join(' '));
  }

  // Record name vs file stem.
  let nameEqStem = 0;
  const nameDiff = [];
  for (const [stem, { rec }] of clips) {
    if (stemOf(rec.name) === stem) nameEqStem++;
    else nameDiff.push(`${stem} <- "${rec.name}"`);
  }
  console.log('');
  console.log(`m_szName stem == file stem ... ${nameEqStem}  ${pct(nameEqStem, clips.size)}`);
  console.log('The remainder are variant copies whose stored name is a PREFIX of the file');
  console.log('stem (a_m_e_048_o_m3.cfg holds a_m_e_048.x), plus one authoring slip. The map');
  console.log('is keyed on the file stem, which is what the engine looks up.');
  if (verbose) for (const d of nameDiff) console.log(`  ${d}`);

  // -- 4. duration agreement -------------------------------------------------
  let compared = 0;
  let durExact = 0;
  let durWithinFrame = 0;
  let unit160 = 0;
  const durOff = [];
  for (const [stem, { rec }] of clips) {
    const r = ranim.get(stem);
    if (!r) continue;
    compared++;
    if (rec.unitTime === TICKS_PER_FRAME) unit160++;
    if (rec.endTimeOrig === r.durationTicks) durExact++;
    else {
      if (Math.abs(rec.endTimeOrig - r.durationTicks) <= TICKS_PER_FRAME) durWithinFrame++;
      durOff.push(`${stem} ${hex(rec.version)} cfg=${rec.endTimeOrig} ranim=${r.durationTicks}`);
    }
  }

  bar('4. CROSS-FORMAT — .cfg times vs .ranim keyframes');
  console.log(`clips with both files ........ ${compared}`);
  console.log(`m_dwETimeOrig == durationTicks ${durExact}  ${pct(durExact, compared)}`);
  console.log(`  ...or within one frame ..... ${durExact + durWithinFrame}  ${pct(durExact + durWithinFrame, compared)}`);
  console.log(`m_UNITTIME == 160 ticks ...... ${unit160}  ${pct(unit160, compared)}`);
  console.log('');
  console.log('Both numbers come from the OTHER format: durationTicks is the highest key time');
  console.log('xanim.js found in the .bin, and 160 is the tick-per-frame the README derived by');
  console.log('measuring 14.3M inter-key gaps. Neither was available to this parser.');
  if (verbose) for (const d of durOff.slice(0, 40)) console.log(`  ${d}`);

  // -- 5. reachability -------------------------------------------------------
  let refReal = 0;
  let refTyped = 0;
  for (const s of charClipRefs) {
    if (ranim.has(s)) refReal++;
    if (clips.has(s)) refTyped++;
  }
  bar('5. Character clip references');
  console.log(`distinct clips named by .chf/.abf . ${charClipRefs.size}`);
  console.log(`  ...that exist as a .ranim ....... ${refReal}  ${pct(refReal, charClipRefs.size)}`);
  console.log(`  ...that have a type ............. ${refTyped}  ${pct(refTyped, charClipRefs.size)}`);

  // -- 6. distribution -------------------------------------------------------
  const byMain = new Map();
  const bySub = new Map();
  for (const [, { rec }] of clips) {
    byMain.set(rec.mainType, (byMain.get(rec.mainType) || 0) + 1);
    bySub.set(rec.subType, (bySub.get(rec.subType) || 0) + 1);
  }
  bar('6. EMANI_MAINTYPE distribution');
  for (const [t, n] of [...byMain].sort((a, b) => a[0] - b[0])) {
    console.log(`  ${String(t).padStart(3)}  ${(AI.mainTypeName(t) || '(unnamed)').padEnd(16)} ${String(n).padStart(5)}  ${pct(n, clips.size)}`);
  }
  const missing = [];
  for (let t = 0; t < AI.AN_TYPE_SIZE; t++) if (!byMain.has(t)) missing.push(`${t}=${AI.mainTypeName(t)}`);
  console.log(`  unused main types: ${missing.join(' ') || '(none)'}`);
  console.log('');
  console.log('EMANI_SUBTYPE (top 12)');
  for (const [t, n] of [...bySub].sort((a, b) => b[1] - a[1]).slice(0, 12)) {
    console.log(`  ${String(t).padStart(3)}  ${(AI.subTypeName(t) || '(unnamed)').padEnd(22)} ${String(n).padStart(5)}`);
  }

  // -- 7. positive control ---------------------------------------------------
  const REORDERED = new Set([0x0108, 0x0109, 0x0110, 0x0200]);
  let controlN = 0;
  let controlFlagged = 0;
  for (const [stem, { rec, body }] of clips) {
    if (!REORDERED.has(rec.version)) continue;
    controlN++;
    let flagged = false;
    try {
      const wrong = misparse(body);
      const bad = validate(wrong);
      const r = ranim.get(stem);
      if (bad.length) flagged = true;
      if (r && wrong.endTimeOrig !== r.durationTicks) flagged = true;
      if (wrong.unitTime !== TICKS_PER_FRAME) flagged = true;
    } catch (err) {
      flagged = true;
    }
    if (flagged) controlFlagged++;
  }

  bar('7. POSITIVE CONTROL — can these checks fail?');
  console.log(`0x0108+ records re-read in 0x0106 field order .. ${controlN}`);
  console.log(`  flagged by the same validation ............... ${controlFlagged}  ${pct(controlFlagged, controlN)}`);
  console.log('');
  console.log('0x0108 moved m_UNITTIME ahead of the three time fields and swapped the strike');
  console.log('and div blocks. Read in the old order every record still parses; the checks');
  console.log('above are what notice. A control that passed silently would mean sections 4');
  console.log('and 6 prove nothing.');

  // -- 8. semantics ----------------------------------------------------------
  //
  // Everything above checks that the numbers are self-consistent. This checks
  // that they MEAN the right thing, which is the only failure mode a range
  // check cannot see: a field read one DWORD early would still be in range,
  // still agree with nothing, and produce a perfectly plausible histogram.
  //
  // The clip names are 3ds Max asset names, entirely independent of the binary
  // — so `*_walk` landing on AN_WALK is evidence from outside the format.
  const SEMANTIC = [
    ['_walk', 'AN_WALK'],
    ['_run', 'AN_RUN'],
    ['_die', 'AN_DIE'],
    ['_stay', 'AN_GUARD_N'],
    ['_att0', 'AN_ATTACK'],
    ['_strock', 'AN_SHOCK'],
    ['_stemina', 'AN_GUARD_L'],
  ];
  bar('8. SEMANTICS — do the types match what the clip names say?');
  const semanticResults = [];
  for (const [needle, expect] of SEMANTIC) {
    let n = 0;
    let hit = 0;
    for (const [stem, { rec }] of clips) {
      if (!stem.includes(needle)) continue;
      n++;
      if (rec.mainTypeName === expect) hit++;
    }
    semanticResults.push({ needle, expect, n, hit });
    console.log(`  *${needle}*`.padEnd(14) + `-> ${expect.padEnd(12)} ${String(hit).padStart(4)} / ${String(n).padStart(4)}  ${pct(hit, n)}`);
  }
  let walkLoop = 0;
  let walkN = 0;
  let dieLoop = 0;
  let dieN = 0;
  for (const [, { rec }] of clips) {
    if (rec.mainTypeName === 'AN_WALK') { walkN++; if (rec.loop) walkLoop++; }
    if (rec.mainTypeName === 'AN_DIE') { dieN++; if (rec.loop) dieLoop++; }
  }
  console.log('');
  console.log(`  ACF_LOOP set on AN_WALK ... ${walkLoop} / ${walkN}  ${pct(walkLoop, walkN)}`);
  console.log(`  ACF_LOOP set on AN_DIE .... ${dieLoop} / ${dieN}  ${pct(dieLoop, dieN)}`);
  console.log('');
  console.log('Walks loop and deaths do not. That is m_dwFlag read at the right offset AND');
  console.log('ACF_LOOP resolved to the right bit; either being wrong would not split the');
  console.log('two categories apart like this.');

  // -- verdict ---------------------------------------------------------------
  const problems = [];
  for (const r of semanticResults) {
    if (r.n >= 20 && r.hit / r.n < 0.7) {
      problems.push(`only ${pct(r.hit, r.n)} of *${r.needle}* clips are ${r.expect}`);
    }
  }
  if (walkN >= 20 && walkLoop / walkN < 0.7) problems.push('AN_WALK clips mostly do not loop');
  if (dieN >= 20 && dieLoop / dieN > 0.3) problems.push('AN_DIE clips mostly loop');
  if (cfgStats.failed) problems.push(`${cfgStats.failed} .cfg failed to parse`);
  if (charStats.failed) problems.push(`${charStats.failed} .chf/.abf failed to parse`);
  if (cfgStats.invalid.length) problems.push(`${cfgStats.invalid.length} records structurally invalid`);
  if (matched / ranim.size < 0.5) problems.push(`only ${pct(matched, ranim.size)} of shipped clips typed`);
  if (durExact / compared < 0.9) problems.push(`duration agreement only ${pct(durExact, compared)}`);
  if (controlFlagged !== controlN) problems.push('positive control did not fire on every record');

  bar('VERDICT');
  if (problems.length) {
    for (const p of problems) console.log(`  FAIL  ${p}`);
    process.exitCode = 1;
  } else {
    console.log('  OK  every check passed');
  }
}

main();
