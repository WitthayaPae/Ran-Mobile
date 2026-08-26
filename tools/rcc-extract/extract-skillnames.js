'use strict';
//
// Real skill NAMES, DESCRIPTIONS and ICONS for the in-game skill book.
//
//   node extract-skillnames.js            build + stage skillinfo.json (+ icons)
//   node extract-skillnames.js --dry      build + report, write nothing
//
// `extract-skills.js` already reads `skill.csv` and produces the flat targeting
// table (skillflat.json). What it CANNOT do is name a skill: `szNAME` in the CSV
// is not a display string, it is a KEY — "SN_%03d_%03d" — that the client resolves
// through a separate localized string table. GetName() proves it:
//
//     const char* GLSKILL::GetName() {                       // GLSkill.cpp:749
//       const char* szName =
//         GLStringTable::GetInstance().GetString( m_sBASIC.szNAME, SKILL );
//       if ( !szName ) return m_sBASIC.szNAME;               // fall back to the key
//       return szName;
//     }
//
// That table is `SkillStrTable.txt` (GLSkillMan::_STRINGTABLE, GLSkill.cpp:1047),
// shipped inside GLogic.rcc. It is a tab-separated `KEY<TAB>VALUE` file with two
// rows per skill — `SN_mid_sid` the name, `SD_mid_sid` the description — encrypted
// with the same AES-256-ECB layer as every other glogic file (a 4-byte version
// prefix of 8, see gamecrypt.js).
//
// MEASURED, not assumed:
//   * Names are ENGLISH (ASCII). Descriptions are THAI, single-byte TIS-620 /
//     windows-874 — this is the Thai service build, matching the Thai gamewords
//     already staged (extract-gameword.js). Descriptions are transcoded to UTF-8
//     here so the runtime shows them verbatim.
//   * Only 561 of 1167 skills have an entry. Brawler/Swordsman/Archer/Shaman/
//     Gunner resolve 100%; Extreme partially; **Assassin has ZERO** SN_043_*
//     rows — so on this build even the PC client shows the bare "SN_043_000" key
//     for Assassin. Those stay flagged as placeholders (p:1) rather than invented.
//
// Icons: SkillImage.cpp draws `m_sEXT_DATA.strICONFILE` (an atlas) at the cell
// `GetIconTexurePos(sICONINDEX)` — a 35x35 grid where left = wMainID*35,
// top = wSubID*35 on a 512x256 sheet (SkillFunc.cpp:6, SkillFunc.h:8-12). Those
// three fields (strICONFILE, sICONINDEX.wMainID/wSubID) live in part-A of skill.csv
// at columns 223/221/222, so they come out with everything else.
//
// Scope: only the eight PLAYER class groups are emitted (the 32 skill-class main
// ids the skill window can show); NPC/pet/event skills never reach the book.
const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc.js');
const gamecrypt = require('./gamecrypt.js');
const { parse } = require('./extract-skills.js');
// stage-ui-atlases.js is required LAZILY inside the run block below, not here:
// its module body stages the HUD atlas set as a side effect, which must not fire
// when this file is merely `require`d (e.g. by test.js) for its functions.

const base = path.resolve(__dirname, '../../..');
const ARCHIVE = path.join(base, 'CLIENT/data/glogic/GLogic.rcc');

// EMSKILLCLASS bases for the eight PC classes (GLCharDefine.h:1194-1263). Each PC
// class owns four consecutive main ids — the four page tabs of the skill window.
const PLAYER_BASES = [0, 4, 8, 12, 30, 36, 43, 47];
const CLASS_NAME = {
  0: 'Brawler', 4: 'Swordsman', 8: 'Archer', 12: 'Shaman',
  30: 'Extreme', 36: 'Gunner', 43: 'Assassin', 47: 'Tricker/ETC',
};

function classBaseOf(mainId) {
  for (const b of PLAYER_BASES) if (mainId >= b && mainId <= b + 3) return b;
  return -1;
}

/**
 * TIS-620 / windows-874 -> Unicode. ASCII passes through; 0xA1..0xFB are the Thai
 * block mapped by a fixed +0x0D60 offset (0xA1 'ก' -> U+0E01); 0xA0 is NBSP.
 * Anything else in 0x80..0xFF is not defined for Thai text and is dropped.
 */
function tis620(buf) {
  let s = '';
  for (const b of buf) {
    if (b < 0x80) s += String.fromCharCode(b);
    else if (b >= 0xA1 && b <= 0xFB) s += String.fromCharCode(0x0E00 + (b - 0xA0));
    else if (b === 0xA0) s += ' ';
  }
  return s;
}

/**
 * Decrypt and parse SkillStrTable.txt into { names, descs } keyed by SN_/SD_ key.
 * Works on the raw decoded BYTES: names are ASCII, descriptions are TIS-620, so
 * reading the whole line as latin1 and re-encoding would corrupt the Thai.
 */
function loadStringTable(arc) {
  const entry = arc.entries.find((e) => /SkillStrTable\.txt$/i.test(e.name));
  if (!entry) throw new Error('SkillStrTable.txt not found in GLogic.rcc');
  const dec = gamecrypt.decode(arc.read(entry));

  const names = {}, descs = {};
  let start = 0;
  for (let i = 0; i <= dec.length; i++) {
    if (i !== dec.length && dec[i] !== 0x0A) continue;
    let end = i;
    if (end > start && dec[end - 1] === 0x0D) end--;                 // strip CR
    // Skip blank lines and `//` comments (GLStringTable strips on "/" too).
    if (end > start && !(dec[start] === 0x2F && dec[start + 1] === 0x2F)) {
      let tab = -1;
      for (let j = start; j < end; j++) if (dec[j] === 0x09) { tab = j; break; }
      if (tab >= 0) {
        const key = dec.toString('latin1', start, tab).trim();
        const valBuf = dec.subarray(tab + 1, end);
        if (/^SN_/.test(key)) names[key] = valBuf.toString('latin1').trim();
        else if (/^SD_/.test(key)) descs[key] = tis620(valBuf).trim();
      }
    }
    start = i + 1;
  }
  return { names, descs, count: Object.keys(names).length };
}

function build() {
  const arc = new RccArchive(ARCHIVE);

  const csv = arc.entries.find((e) => /skill\.csv$/i.test(e.name));
  if (!csv) throw new Error('skill.csv not found in GLogic.rcc');
  const { header, rows } = parse(arc.read(csv).toString('latin1'));
  const col = {};
  header.forEach((h, i) => { col[h.trim()] = i; });
  const need = (n) => { if (!(n in col)) throw new Error('column not found: ' + n); return col[n]; };
  const C = {
    main: need('sNATIVEID wMainID'), sub: need('sNATIVEID wSubID'),
    name: need('szNAME'), grade: need('dwGRADE'), role: need('emROLE'),
    iconMain: need('sICONINDEX wMainID'), iconSub: need('sICONINDEX wSubID'),
    iconFile: need('strICONFILE'), comment: need('strCOMMENTS'),
  };

  const { names, descs, count: tableCount } = loadStringTable(arc);

  const entries = [];
  const iconFiles = new Set();
  const perClass = {};   // base -> { total, resolved }
  let resolved = 0;

  for (const r of rows) {
    const m = parseInt(r[C.main], 10);
    const s = parseInt(r[C.sub], 10);
    if (!Number.isFinite(m) || !Number.isFinite(s)) continue;
    const b = classBaseOf(m);
    if (b < 0) continue;                    // player classes only

    const key = (r[C.name] || '').trim();       // the SN_ key
    const descKey = (r[C.comment] || '').trim();  // the SD_ key
    const realName = names[key];
    const desc = descs[descKey] || '';
    const placeholder = !realName;

    const iconFile = (r[C.iconFile] || '').trim().toLowerCase();
    if (iconFile) iconFiles.add(iconFile);

    perClass[b] = perClass[b] || { total: 0, resolved: 0 };
    perClass[b].total++;
    if (!placeholder) { perClass[b].resolved++; resolved++; }

    entries.push({
      m, s,
      n: placeholder ? key : realName,        // real name, else the bare key
      d: desc,
      g: parseInt(r[C.grade], 10) || 0,
      r: parseInt(r[C.role], 10) || 0,
      f: iconFile,
      x: parseInt(r[C.iconMain], 10) || 0,    // atlas COLUMN (wMainID)
      y: parseInt(r[C.iconSub], 10) || 0,     // atlas ROW    (wSubID)
      p: placeholder ? 1 : 0,
    });
  }

  return { entries, iconFiles: [...iconFiles], perClass, resolved, tableCount };
}

// --- run ------------------------------------------------------------------
if (require.main === module) {
  const dry = process.argv.includes('--dry');
  const { entries, iconFiles, perClass, resolved, tableCount } = build();

  console.log(`SkillStrTable.txt: ${tableCount} name entries decoded (AES v8)`);
  console.log(`player-class skills: ${entries.length}, real names: ${resolved}, ` +
              `placeholders: ${entries.length - resolved}`);
  for (const b of PLAYER_BASES) {
    const p = perClass[b];
    if (p) console.log(`  ${CLASS_NAME[b].padEnd(12)} ${p.resolved}/${p.total}`);
  }

  const json = JSON.stringify({ entries });
  const outs = [
    path.join(base, 'MOBILE/assets/skillinfo.json'),
    path.join(base, 'MOBILE/unity/RanMobile/Assets/Ran/Resources/skillinfo.json'),
  ];
  if (!dry) {
    for (const p of outs) {
      fs.mkdirSync(path.dirname(p), { recursive: true });
      fs.writeFileSync(p, json);
      console.log('wrote ' + path.relative(base, p));
    }
    // Stage the icon atlases the player skills reference, the same
    // dependency-free DDS->PNG way the HUD atlases are staged.
    const stageAtlases = require('./stage-ui-atlases.js');
    console.log('icon atlases:');
    for (const f of iconFiles.sort()) {
      const r = stageAtlases.stage(f);
      console.log(`  ${r.atlas.padEnd(24)} ${r.status}`);
    }
  } else {
    console.log(`(dry) would write ${json.length} bytes; icon atlases: ` +
                iconFiles.sort().join(', '));
  }
}

module.exports = { build, loadStringTable, tis620, classBaseOf, PLAYER_BASES, CLASS_NAME };
