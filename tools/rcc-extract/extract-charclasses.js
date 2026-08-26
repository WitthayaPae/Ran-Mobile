'use strict';
//
// Per-class data the character CREATE screen needs: how many face/hair
// options each of the 16 EMCHARINDEX (class+gender) combinations actually
// has, the 3 school names, and the 16x20 hair-colour palette.
//
// SOURCES (measured/read, not guessed):
//
//  * `CLIENT/data/glogic/default.charclass` — the wSCHOOLNUM=3 school names
//    (SacredGate/MysticPeak/Phoenix) and the BRAWLER_M.SETFILE.. table that
//    names each class's `classX.classconst` file
//    (`GLCONST_CHARCLASS::LOADFILE`, GLogicDataLoad.cpp:1160 on).
//  * `CLIENT/data/glogic/class{0..F}.classconst` — one `dwHEADNUM_SELECT` /
//    `dwHAIRNUM_SELECT` pair per class (`ShiftCharFace`/`ShiftCharHair`,
//    DxLobyStageCreateChar.cpp:69-129, bound the create-screen steppers by
//    exactly these two fields), AND — same file, added for the walk/run fix —
//    `fWALKVELO`/`fRUNVELO`: `GLCONST_CHARCLASS::LOADFILE` (GLogicDataLoad.cpp
//    :1161-1199) reads these two keys FIRST, before any face/hair field, from
//    whichever file `default.charclass`'s own `<CLASS>.SETFILE` key names —
//    and that value, decoded and read directly (not assumed), IS
//    `classN.classconst` for every one of the 16 classes (e.g.
//    `BRAWLER_M.SETFILE  "class0.classconst"`), the SAME file this extractor
//    already opens for head/hair — so no second file family exists to chase.
//    These are `Actor::SetMaxSpeed`'s world-units/sec input
//    (`GLCharacter::GetMoveVelo`, GLCharacter.cpp:3814, picks fRUNVELO when
//    `EM_ACT_RUN` is set else fWALKVELO), the same unit space `Actor::Update`
//    (actor.cpp:311, `max_distance = m_MaxSpeed * elapsedTime`) already
//    multiplies against DXUT's own per-frame elapsed-seconds float.
//
// Both file families are wrapped in the SAME second-layer AES-256-ECB cipher
// as the rest of `data/glogic/` (`gamecrypt.js`) — confirmed by the `08 00
// 00 00` VERSION=8 prefix on every file read here, not assumed from the
// GLogic.rcc case extract-skills.js already handles.
//
// The class ORDER (EMCHARINDEX 0-15) and the `.classconst` file each maps to
// is `CCharacterCreatePageSet`'s own `szSETFILE` table
// (CharacterCreatePageSet.cpp:523-539) cross-checked against
// `GLCharDefine.h`'s `EMCHARINDEX` enum — GLCI_BRAWLER_M=0 ... GLCI_TRICKER_W=15.
//
// The hair-colour table (HAIRCOLOR::wHairColor, GLogicData.cpp:1021-1043) is
// a COMPILE-TIME C++ array, not data-file content — transcribed here from the
// source literal (cited), not measured from a probe, because it is not a
// struct offset/size (rule 1's concern) but plain constant data visible
// unambiguously in the header.
//
//   node extract-charclasses.js
//   node extract-charclasses.js --out MOBILE/assets/charclasses.json
const fs = require('fs');
const path = require('path');
const G = require('./gamecrypt.js');

const base = path.resolve(__dirname, '../../..');
const GLOGIC = path.join(base, 'CLIENT/data/glogic');

function loadText(name) {
  const buf = fs.readFileSync(path.join(GLOGIC, name));
  return G.decode(buf).toString('latin1');
}

function field(text, key) {
  const re = new RegExp('\\b' + key + '\\s+(\\S+)');
  const m = re.exec(text);
  return m ? m[1] : null;
}

function strField(text, key) {
  const re = new RegExp('\\b' + key + '\\s+"([^"]*)"');
  const m = re.exec(text);
  return m ? m[1] : null;
}

// EMCHARINDEX order (GLCharDefine.h) <-> classconst file letter
// (CharacterCreatePageSet.cpp:322-337, BRAWLER_M.SETFILE "class0.classconst" ..
// TRICKER_W.SETFILE "classF.classconst").
const CLASSES = [
  { name: 'BRAWLER_M',   file: 'class0' },
  { name: 'SWORDSMAN_M', file: 'class1' },
  { name: 'ARCHER_W',    file: 'class2' },
  { name: 'SHAMAN_W',    file: 'class3' },
  { name: 'EXTREME_M',   file: 'class4' },
  { name: 'EXTREME_W',   file: 'class5' },
  { name: 'BRAWLER_W',   file: 'class6' },
  { name: 'SWORDSMAN_W', file: 'class7' },
  { name: 'ARCHER_M',    file: 'class8' },
  { name: 'SHAMAN_M',    file: 'class9' },
  { name: 'GUNNER_M',    file: 'classA' },
  { name: 'GUNNER_W',    file: 'classB' },
  { name: 'ASSASSIN_M',  file: 'classC' },
  { name: 'ASSASSIN_W',  file: 'classD' },
  { name: 'TRICKER_M',   file: 'classE' },
  { name: 'TRICKER_W',   file: 'classF' },
];

const defaultText = loadText('default.charclass');
const schoolNames = [0, 1, 2].map((i) =>
  strField(defaultText, `strSCHOOLNAME0${i}`));

// Comma-separated CPS list fields (strHEAD_CPS/strHAIR_CPS): the per-class
// face and hair piece files GLCharClient::UpdateSuit mounts by the character's
// wFace/wHair index (GLCharClient.cpp:380-404). Emitted as lowercase stems
// (".cps" stripped) so the runtime can key pieces.json/equippieces directly.
function cpsList(text, key) {
  const re = new RegExp('\\b' + key + '\\s+(\\S+)');
  const m = re.exec(text);
  if (!m) return [];
  return m[1].split(',')
    .map((s) => s.trim().toLowerCase().replace(/\.cps$/, ''))
    .filter((s) => s.length > 0);
}

const perClass = CLASSES.map(({ name, file }) => {
  const text = loadText(file + '.classconst');
  return {
    name,
    headNumSelect: parseInt(field(text, 'dwHEADNUM_SELECT'), 10),
    hairNumSelect: parseInt(field(text, 'dwHAIRNUM_SELECT'), 10),
    // world-units/sec, GLCONST_CHARCLASS::LOADFILE's first two reads
    // (GLogicDataLoad.cpp:1198-1199) — see this file's header comment.
    walkVelo: parseFloat(field(text, 'fWALKVELO')),
    runVelo: parseFloat(field(text, 'fRUNVELO')),
    headCps: cpsList(text, 'strHEAD_CPS'),
    hairCps: cpsList(text, 'strHAIR_CPS'),
  };
});

// HAIRCOLOR::wHairColor[GLCI_NUM_8CLASS][MAX_HAIR], transcribed verbatim from
// GLogicData.cpp:1023-1040 (RGB555-packed WORDs, MAX_HAIR=20 columns, one row
// per EMCHARINDEX in the same 0..15 order as CLASSES above).
const hairColorTable = [
  [22923, 30719, 32486, 24447, 32258, 32594, 31743, 32767, 0, 31744, 31775, 992, 1023, 31, 32736, 32224, 15391, 12794, 32499, 20831],
  [30719, 22923, 32486, 24447, 32258, 32594, 31743, 32767, 0, 31744, 31775, 992, 1023, 31, 32736, 32224, 15391, 12794, 32499, 20831],
  [18008, 30278, 31503, 24004, 32488, 27245, 28364, 32767, 0, 31744, 31775, 992, 1023, 31, 32736, 32224, 15391, 12794, 32499, 20831],
  [30278, 18008, 31503, 24004, 32488, 27245, 28364, 32767, 0, 31744, 31775, 992, 1023, 31, 32736, 32224, 15391, 12794, 32499, 20831],
  [22923, 30719, 32486, 24447, 32258, 32594, 31743, 32767, 0, 31744, 31775, 992, 1023, 31, 32736, 32224, 15391, 12794, 32499, 20831],
  [18008, 30278, 31503, 24004, 32488, 27245, 28364, 32767, 0, 31744, 31775, 992, 1023, 31, 32736, 32224, 15391, 12794, 32499, 20831],
  [30278, 18008, 31503, 24004, 32488, 27245, 28364, 32767, 0, 31744, 31775, 992, 1023, 31, 32736, 32224, 15391, 12794, 32499, 20831],
  [18008, 30278, 31503, 24004, 32488, 27245, 28364, 32767, 0, 31744, 31775, 992, 1023, 31, 32736, 32224, 15391, 12794, 32499, 20831],
  [30719, 22923, 32486, 24447, 32258, 32594, 31743, 32767, 0, 31744, 31775, 992, 1023, 31, 32736, 32224, 15391, 12794, 32499, 20831],
  [22923, 30719, 32486, 24447, 32258, 32594, 31743, 32767, 0, 31744, 31775, 992, 1023, 31, 32736, 32224, 15391, 12794, 32499, 20831],
  [30719, 22923, 32486, 24447, 32258, 32594, 31743, 32767, 0, 31744, 31775, 992, 1023, 31, 32736, 32224, 15391, 12794, 32499, 20831],
  [18008, 30278, 31503, 24004, 32488, 27245, 28364, 32767, 0, 31744, 31775, 992, 1023, 31, 32736, 32224, 15391, 12794, 32499, 20831],
  [30719, 22923, 32486, 24447, 32258, 32594, 31743, 32767, 0, 31744, 31775, 992, 1023, 31, 32736, 32224, 15391, 12794, 32499, 20831],
  [18008, 30278, 31503, 24004, 32488, 27245, 28364, 32767, 0, 31744, 31775, 992, 1023, 31, 32736, 32224, 15391, 12794, 32499, 20831],
  [30719, 22923, 32486, 24447, 32258, 32594, 31743, 32767, 0, 31744, 31775, 992, 1023, 31, 32736, 32224, 15391, 12794, 32499, 20831],
  [18008, 30278, 31503, 24004, 32488, 27245, 28364, 32767, 0, 31744, 31775, 992, 1023, 31, 32736, 32224, 15391, 12794, 32499, 20831],
];

// SCALERANGE_MIN/MAX (GLCharData.h:37-38) and the UI stepper delta
// (CCharacterCreatePageStyle::CharacterScale{Increase,Decrease}, 0.015f)
// are compile-time constants, transcribed here rather than probed for the
// same reason as the hair-colour table.
const out = {
  schoolNames,
  scaleMin: 0.88,
  scaleMax: 1.12,
  scaleStep: 0.015,
  scaleDefault: 1.0,
  nameMax: 33,        // CHR_ID_LENGTH, s_NetGlobal.h:172
  nameUiLimit: 16,     // CCharacterCreatePageStyle::nLIMITCHAR
  maxHairColorSlots: 20, // MAX_HAIR, GLogicData.h:43
  classes: perClass,
  hairColorTable,
};

console.log(`${perClass.length} classes, schools: ${schoolNames.join(', ')}`);
for (const c of perClass)
  console.log(`  ${c.name.padEnd(12)} head=${c.headNumSelect} hair=${c.hairNumSelect} ` +
              `walk=${c.walkVelo} run=${c.runVelo}`);

const oi = process.argv.indexOf('--out');
if (oi > 0) {
  const outPath = process.argv[oi + 1];
  fs.writeFileSync(path.join(base, outPath), JSON.stringify(out));
  console.log(`\nwrote ${outPath}`);
}

module.exports = { CLASSES, hairColorTable };
