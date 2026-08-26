'use strict';
//
// Skills, from `skill.csv` in GLogic.rcc.
//
//   node extract-skills.js
//   node extract-skills.js --out MOBILE/assets/skills.json
//
// The skill book needs a name, a level cap, a targeting mode and a range for
// every skill, and all four are DECLARED in this CSV — so this reads the
// declared table rather than inferring anything from the binary `.ssf`.
//
// The file's shape is the whole difficulty. It is not a CSV with one header and
// N rows; it is a repeating FOUR-LINE block, and the header is re-emitted before
// every single record:
//
//     sNATIVEID wMainID,...       header, part A   (322 fields)
//     emBASIC_TYPE,...            header, part B   (719 fields)
//     0,0,SN_000_000,...          record, part A   (322 fields)
//     11,0,0,...                  record, part B   (719 fields)
//
// So a record is 1,041 columns split across two physical lines, and 1,167
// records occupy 4,668 lines. Reading it as "one header, then rows" — or
// accumulating fields until some fixed width — drifts out of alignment and
// every field afterwards reads its neighbour's value. That failure is not
// subtle when you look for it: `emIMPACT_TAR` is a 0..4 enum, and a misaligned
// read returns 65535 and -40. Which is exactly what the first attempt produced.
//
// Only part A is parsed here. Everything the skill book needs is in the first
// 322 columns; part B is per-level tuning (322 columns of `sDATA_LVL n ...`).
const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc.js');

const base = path.resolve(__dirname, '../../..');
const ARCHIVE = path.join(base, 'CLIENT/data/glogic/GLogic.rcc');

/** `EMIMPACT_TAR` — GLCharDefine.h:876. Not invented; the UI keys off these. */
const TAR = ['SELF', 'SPEC', 'SELF_TOSPEC', 'ZONE', 'SPECIFIC'];

const HDR_A = 'sNATIVEID wMainID';
const HDR_B = 'emBASIC_TYPE';

// Per-level tuning lives in part B as 9 repeated `sDATA_LVL n <FIELD>` blocks
// (SKILL::MAX_LEVEL == 9, GLSkillDefine.h:16). Only the 3 fields the client
// actually needs for cooldown/cost are read: fDELAYTIME feeds
// GLOGICEX::SKILLDELAY (GLogicEx.cpp:150) — the real per-cast recharge, not a
// flat constant — and wUSE_MP/wUSE_SP are the resource cost shown on cast.
const MAX_LEVEL = 9;

/**
 * Walk the four-line blocks, returning [headerFields, recordAFields[]].
 *
 * The header is taken from the FIRST block and then verified against every
 * later one, because a table that changes its own column order midway would
 * otherwise be read with the wrong offsets from that point on.
 */
function parse(text) {
  const lines = text.split(/\r?\n/);
  let header = null, headerB = null;
  const rows = [];
  const rowsB = [];
  let mismatched = 0;

  for (let i = 0; i < lines.length; i++) {
    if (!lines[i].startsWith(HDR_A)) continue;
    // A block is header-A, header-B, record-A, record-B.
    if (i + 3 >= lines.length) break;
    if (!lines[i + 1].startsWith(HDR_B)) continue;

    const h = lines[i].split(',');
    if (header === null) { header = h; headerB = lines[i + 1].split(','); }
    else if (h.length !== header.length || h[8] !== header[8]) mismatched++;

    rows.push(lines[i + 2].split(','));
    rowsB.push(lines[i + 3].split(','));
    i += 3;
  }
  return { header, headerB, rows, rowsB, mismatched };
}

const arc = new RccArchive(ARCHIVE);
const entry = arc.entries.find((e) => /skill\.csv$/i.test(e.name));
if (!entry) throw new Error('skill.csv not found in GLogic.rcc');

const { header, headerB, rows, rowsB, mismatched } = parse(arc.read(entry).toString('latin1'));
if (!header) throw new Error('no header block found');
if (mismatched) console.log(`WARNING: ${mismatched} blocks disagree with the first header`);

const col = {};
header.forEach((h, i) => { col[h.trim()] = i; });
function need(name) {
  if (!(name in col)) throw new Error(`column not found: ${name}`);
  return col[name];
}

const colB = {};
headerB.forEach((h, i) => { colB[h.trim()] = i; });
function needB(name) {
  if (!(name in colB)) throw new Error(`column not found (part B): ${name}`);
  return colB[name];
}

const C = {
  main: need('sNATIVEID wMainID'),
  sub: need('sNATIVEID wSubID'),
  name: need('szNAME'),
  maxLevel: need('dwMAXLEVEL'),
  role: need('emROLE'),
  tar: need('emIMPACT_TAR'),
  range: need('wTARRANGE'),
  cls: need('dwCLASS'),
  grade: need('dwGRADE'),
  // SKILL::SLEARN (GLSkillLearn.h) — the LEARN gate, not the cast data above.
  // sSKILL is the prerequisite skill id; sLVL_STEP[i] (CSV label i+1, see
  // GLSkillLearn.cpp:17/44 — the header loop starts at 1 but the value loop
  // that WRITES it starts at 0, so CSV label "sLVL_STEP 1" IS array index 0)
  // is what SkillSlot::Update (SkillSlot.cpp:144-323) reads to grey/red the
  // LEVEL-UP button: dwSKP (real per-level point cost, not a flat 1), dwLEVEL
  // (character level floor), dwSKILL_LVL (prerequisite skill's required
  // level), and 6 stat floors.
  needMain: need('sSKILL wMainID'),
  needSub: need('sSKILL wSubID'),
};

// sLVL_STEP[1..9] sSTATS wPow/wStr/wSpi/wDex/wInt/wSta — the 6-stat learn
// floor (SkillSlot.cpp:215-223, `sCharData.m_sSUMSTATS.wX < sLVL.sSTATS.wX`).
// Column order matches EMSTATS (GLCharDefine.h:328-339: Pow0 Str1 Spi2 Dex3
// Int4 Sta5) — MEASURED against the live CSV header (see MOBILE task notes):
// row `sNATIVEID wMainID`=0 sub=0 step1 is {wPow:0,wStr:0,wSpi:0,wDex:26,
// wInt:0,wSta:0}.
const STAT_KEYS = ['wPow', 'wStr', 'wSpi', 'wDex', 'wInt', 'wSta'];

// Per-level (1..9) cooldown base + MP/SP cost columns, part B.
const CB_DELAY = [], CB_MP = [], CB_SP = [];
for (let lvl = 1; lvl <= MAX_LEVEL; lvl++) {
  CB_DELAY.push(needB(`sDATA_LVL ${lvl} fDELAYTIME`));
  CB_MP.push(needB(`sDATA_LVL ${lvl} wUSE_MP`));
  CB_SP.push(needB(`sDATA_LVL ${lvl} wUSE_SP`));
}

// sLVL_STEP[0..8] learn-requirement rows, part A (see C.needMain doc above).
// The 6-stat floor (sSTATS wPow/wStr/wSpi/wDex/wInt/wSta) IS now extracted
// (CA_STATS below) — it was left out originally because mobile had no
// reachable summed-stats source; RanCharStatPackets' char-join snapshot now
// feeds RanSkillBook.CanLearn a best-effort proxy (see that file's doc for the
// exact, disclosed gap between this proxy and the PC's real m_sSUMSTATS).
const CA_SKP = [], CA_LEVEL = [], CA_SKILLLVL = [];
const CA_STATS = []; // CA_STATS[step][statIndex], step 0..8, statIndex 0..5 (EMSTATS order)
for (let step = 1; step <= MAX_LEVEL; step++) {
  CA_SKP.push(need(`sLVL_STEP ${step} dwSKP`));
  CA_LEVEL.push(need(`sLVL_STEP ${step} dwLEVEL`));
  CA_SKILLLVL.push(need(`sLVL_STEP ${step} dwSKILL_LVL`));
  CA_STATS.push(STAT_KEYS.map((k) => need(`sLVL_STEP ${step} sSTATS ${k}`)));
}

const out = [];
const tarCount = {};
let placeholders = 0, outOfRange = 0;

for (let ri = 0; ri < rows.length; ri++) {
  const r = rows[ri];
  const rb = rowsB[ri];
  const main = parseInt(r[C.main], 10);
  const sub = parseInt(r[C.sub], 10);
  if (!Number.isFinite(main) || !Number.isFinite(sub)) continue;

  const name = (r[C.name] || '').trim();
  // `SN_xxx_xxx` is the string-table KEY, shipped when the display name was
  // never filled in. Counted rather than dropped: they are real rows the server
  // can still reference, and hiding them would misreport the table's size.
  const placeholder = /^SN_\d+_\d+$/.test(name);
  if (placeholder) placeholders++;

  const tar = parseInt(r[C.tar], 10);
  // The enum has five members. Anything else means the column moved, and it is
  // worth failing loudly rather than shipping a skill book that mis-targets.
  if (!(tar >= 0 && tar < TAR.length)) outOfRange++;
  tarCount[tar] = (tarCount[tar] || 0) + 1;

  // Per-level cooldown base + MP/SP cost (1-indexed levels, index 0 unused so
  // level N reads at array index N directly — matches SKILL::CDATA_LVL[wLevel]
  // being indexed by the 1-based learned level everywhere in G-Logic, e.g.
  // GLogixExPC.cpp:4119 `sDATA_LVL[sSkill.wLevel]`).
  const dly = [0], mp = [0], sp = [0];
  for (let lvl = 1; lvl <= MAX_LEVEL; lvl++) {
    dly.push(parseFloat(rb[CB_DELAY[lvl - 1]]) || 0);
    mp.push(parseInt(rb[CB_MP[lvl - 1]], 10) || 0);
    sp.push(parseInt(rb[CB_SP[lvl - 1]], 10) || 0);
  }

  // Learn-requirement steps, sLVL_STEP[0..8] (see C.needMain's doc for the
  // CSV-label-vs-array-index mapping). MEASURED from SkillSlot.cpp: the
  // unlearned case reads sLVL_STEP[0] (line 182); the learned case reads
  // sLVL_STEP[pCharSkill->wLevel + 1] where pCharSkill->wLevel is the
  // CURRENT learned rank 0-based (line 151/155). Both collapse to one rule:
  // stepIndex == the current 1-based learned level (0 when not learned) —
  // exactly RanSkillBook.Learned.level's own convention, so the C# side can
  // index this array directly with that field.
  const skp = [], lvlReq = [], skillLvlReq = [];
  // Flat [step0stat0..step0stat5, step1stat0..step1stat5, ...] — 9*6 = 54
  // entries, kept flat (not nested) because RanSkillTable.Parse's hand-rolled
  // reader only handles a single-level `[n,n,n]` array (see its IntArray doc).
  const stats = [];
  for (let step = 0; step < MAX_LEVEL; step++) {
    skp.push(parseInt(r[CA_SKP[step]], 10) || 0);
    lvlReq.push(parseInt(r[CA_LEVEL[step]], 10) || 0);
    skillLvlReq.push(parseInt(r[CA_SKILLLVL[step]], 10) || 0);
    for (let stat = 0; stat < STAT_KEYS.length; stat++) {
      stats.push(parseInt(r[CA_STATS[step][stat]], 10) || 0);
    }
  }
  const needMain = parseInt(r[C.needMain], 10) || 0;
  const needSub = parseInt(r[C.needSub], 10) || 0;

  out.push({
    m: main, s: sub, n: name,
    l: parseInt(r[C.maxLevel], 10) || 0,
    r: parseInt(r[C.role], 10) || 0,
    t: tar,
    g: parseInt(r[C.range], 10) || 0,
    c: parseInt(r[C.cls], 10) || 0,
    p: placeholder ? 1 : 0,
    gr: parseInt(r[C.grade], 10) || 0,
    dly, mp, sp,
    nm: needMain, ns: needSub,
    skp, lv: lvlReq, sl: skillLvlReq, st: stats,
  });
}

console.log(`${rows.length} records -> ${out.length} skills ` +
            `(${placeholders} carry a placeholder name)`);
console.log('targeting: ' + Object.entries(tarCount)
  .sort((a, b) => b[1] - a[1])
  .map(([k, v]) => `${TAR[k] !== undefined ? TAR[k] : k}:${v} ` +
                   `(${(v / out.length * 100).toFixed(1)}%)`)
  .join('  '));

if (outOfRange) {
  console.log(`FAIL: ${outOfRange} skills have emIMPACT_TAR outside 0..4 — ` +
              'the column is misaligned.');
  process.exitCode = 1;
} else {
  const needsTarget = out.filter((s) => s.t === 1 || s.t === 2).length;
  console.log(`need a target (TAR_SPEC | TAR_SELF_TOSPEC): ${needsTarget} ` +
              `(${(needsTarget / out.length * 100).toFixed(1)}%)`);
}

const outArg = process.argv.indexOf('--out');
if (outArg > 0) {
  const p = process.argv[outArg + 1];
  fs.writeFileSync(path.join(base, p), JSON.stringify({ entries: out }));
  console.log(`wrote ${p}`);
}

module.exports = { parse, TAR };
