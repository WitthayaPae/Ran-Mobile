'use strict';
//
// Per-skill CAST ANIMATION key, from `skill.csv` in GLogic.rcc.
//
//   node extract-skillanim.js
//   node extract-skillanim.js --out MOBILE/unity/RanMobile/Assets/Ran/Resources/skillanim.json
//
// The PC does NOT play a generic AN_ATTACK on a skill cast. When a character
// turns to GLAT_SKILL it calls
//
//   m_pSkinChar->SELECTSKILLANI( m_emANIMAINSKILL, m_emANISUBSKILL )   (GLCharacter.cpp:3515)
//
// and those two fields are copied straight from the skill's own definition:
//
//   m_emANIMAINSKILL = pSkill->m_sEXT_DATA.emANIMTYPE;   (EMANI_MAINTYPE)
//   m_emANISUBSKILL  = pSkill->m_sEXT_DATA.emANISTYPE;   (EMANI_SUBTYPE)
//     -- GLCharClient.cpp:814-815, GLCharacterSkill.cpp:92-93
//
// SELECTSKILLANI then GETANI(MType,SType) on the character's OWN skin, so the
// (mainType, subType) pair selects a character-specific clip. That is exactly
// what the animflat clip table is keyed by (EMANI_MAINTYPE/EMANI_SUBTYPE from
// the .cfg SANIMCONINFO), so the pair is a portable clip selector.
//
// Both fields are DECLARED as plain columns of skill.csv — no binary decode.
// emANIMTYPE and emANISTYPE sit in header part A (the 322-field record line;
// see extract-skills.js for the four-line-block shape and why a naive "one
// header, then rows" read doubles the record count and shreds every column).
// Measured, not assumed: in the shipped file emANIMTYPE is column 107 and
// emANISTYPE column 108, but they are resolved BY NAME here, never by index.
//
// Output: { entries: [ {m,s,am,as,k}, ... ] } where
//   m,s  = skill id   (sNATIVEID wMainID / wSubID)
//   am,as= EMANI_MAINTYPE / EMANI_SUBTYPE the cast plays
//   k    = am*100 + as   -- the packed anim key. AN_SUB_00_SIZE = 100 caps the
//          sub range at 0..99 (DxAniKeys.h), so this packing is unambiguous and
//          is the same value RanCharacterDriver keys its Cast_<am>_<as> states by.
const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc.js');

const base = path.resolve(__dirname, '../../..');
const ARCHIVE = path.join(base, 'CLIENT/data/glogic/GLogic.rcc');

const HDR_A = 'sNATIVEID wMainID';
const HDR_B = 'emBASIC_TYPE';

// EMANI_MAINTYPE names for the values skills actually use (DxAniKeys.h:149).
// Only for reporting; the numeric key is what drives everything.
const MAIN_NAMES = {
  4: 'AN_ATTACK', 9: 'AN_SKILL_A', 16: 'AN_SKILL_B', 17: 'AN_SKILL_C',
  18: 'AN_SKILL_D', 19: 'AN_SKILL_E', 26: 'AN_SKILL_F', 27: 'AN_SKILL_G',
  28: 'AN_SKILL_H',
};

// EMANI_MAINTYPE valid range is 0..AN_TYPE_SIZE-1 (34); EMANI_SUBTYPE 0..99
// (AN_SUB_00_SIZE). A value outside either means the column moved — the same
// misalignment signature extract-skills.js guards emIMPACT_TAR against.
const MAIN_MAX = 34;
const SUB_MAX = 100;

function parse(text) {
  const lines = text.split(/\r?\n/);
  let header = null;
  const rows = [];
  for (let i = 0; i < lines.length; i++) {
    if (!lines[i].startsWith(HDR_A)) continue;
    if (i + 3 >= lines.length) break;
    if (!lines[i + 1].startsWith(HDR_B)) continue;
    const h = lines[i].split(',');
    if (header === null) header = h.map((s) => s.trim());
    rows.push(lines[i + 2].split(','));
    i += 3;
  }
  return { header, rows };
}

/**
 * Map parsed header+rows to entries. Columns are resolved BY NAME, so a table
 * that reorders its columns still reads correctly; a value outside the enum
 * range is the misalignment signature and is counted, not emitted.
 */
function toEntries(header, rows) {
  const col = {};
  header.forEach((h, i) => { col[h] = i; });
  const need = (name) => {
    if (!(name in col)) throw new Error(`column not found: ${name}`);
    return col[name];
  };
  const cMain = need('sNATIVEID wMainID');
  const cSub = need('sNATIVEID wSubID');
  const cAM = need('emANIMTYPE');
  const cAS = need('emANISTYPE');

  const out = [];
  let outOfRange = 0;
  for (const r of rows) {
    const m = parseInt(r[cMain], 10);
    const s = parseInt(r[cSub], 10);
    if (!Number.isFinite(m) || !Number.isFinite(s)) continue;
    const am = parseInt(r[cAM], 10);
    const as = parseInt(r[cAS], 10);
    if (!(am >= 0 && am < MAIN_MAX && as >= 0 && as < SUB_MAX)) { outOfRange++; continue; }
    out.push({ m, s, am, as, k: am * 100 + as });
  }
  return { entries: out, outOfRange, records: rows.length };
}

function build() {
  const arc = new RccArchive(ARCHIVE);
  const entry = arc.entries.find((e) => /skill\.csv$/i.test(e.name));
  if (!entry) throw new Error('skill.csv not found in GLogic.rcc');

  const { header, rows } = parse(arc.read(entry).toString('latin1'));
  if (!header) throw new Error('no header block found');
  return toEntries(header, rows);
}

function main() {
  const { entries, outOfRange, records } = build();

  const mainCount = {};
  const keySet = new Set();
  for (const e of entries) {
    mainCount[e.am] = (mainCount[e.am] || 0) + 1;
    keySet.add(e.k);
  }

  console.log(`${records} records -> ${entries.length} skills mapped ` +
              `(${outOfRange} out of enum range)`);
  console.log(`distinct anim keys (am:as): ${keySet.size}`);
  console.log('by EMANI_MAINTYPE: ' + Object.entries(mainCount)
    .sort((a, b) => b[1] - a[1])
    .map(([k, v]) => `${MAIN_NAMES[k] || k}:${v}`)
    .join('  '));

  if (outOfRange) {
    console.log(`FAIL: ${outOfRange} skills have emANIMTYPE/emANISTYPE outside ` +
                'the enum — the column is misaligned.');
    process.exitCode = 1;
    return;
  }

  const outArg = process.argv.indexOf('--out');
  if (outArg > 0) {
    const p = process.argv[outArg + 1];
    const abs = path.isAbsolute(p) ? p : path.join(base, p);
    fs.mkdirSync(path.dirname(abs), { recursive: true });
    const doc = {
      note: 'per-skill cast animation key from skill.csv emANIMTYPE/emANISTYPE '
          + '(SELECTSKILLANI, GLCharacter.cpp:3515). m,s = skill id; am,as = '
          + 'EMANI_MAINTYPE/EMANI_SUBTYPE; k = am*100+as, the RanCharacterDriver '
          + 'Cast state key. Enum ranges from DxAniKeys.h.',
      generated: new Date().toISOString(),
      counts: { skills: entries.length, distinctKeys: keySet.size },
      entries,
    };
    fs.writeFileSync(abs, JSON.stringify(doc));
    console.log(`wrote ${p} (${entries.length} entries)`);
  }
  return entries;
}

if (require.main === module) main();
module.exports = { parse, toEntries, build, MAIN_NAMES, MAIN_MAX, SUB_MAX };
