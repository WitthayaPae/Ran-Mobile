'use strict';
//
// Build the mob/NPC DISPLAY NAME table every crow (monster or NPC) needs.
//
//   node extract-mobnames.js           write mobnames.json
//   node extract-mobnames.js --dry     parse + report, write nothing
//   node extract-mobnames.js --out DIR write the json into DIR
//
// WHY THIS EXISTS: SDROP_CROW (SOURCE/Lib_Client/G-Logic/GLCrowData.h:354-384)
// carries only sNativeID — no name string at all. The real client resolves a
// crow's floating-label name CLIENT-SIDE: CROWREN::INIT(GLCrowClient*)
// (GLCrowRenList.cpp:349-354) copies `pCROW->m_pCrowData->GetName()`, i.e. a
// lookup into the Crow.mnsf table already loaded at startup
// (GLCrowDataMan::LoadFile, GLogicData.cpp:1174). Until this script, the
// mobile port had no equivalent: RanWorldSession.EntityEnter fell back to a
// raw "#main:sub" ID tag for every single crow, permanently, because there
// was nowhere to look a name up.
//
// This is a narrower cut of exactly what extract-npcshops.js already builds
// (see that file's header for the full Crow.mnsf / CrowStrTable.txt pipeline
// citations) — it reuses crowdata.js (the verified, byte-exact Crow.mnsf
// parser: "1853 records, EOF-exact") and extract-npcshops.js's own
// parseCrowStrTable, but covers EVERY crow record, not just the NPC-shop
// subset extract-npcshops.js filters to. cp874 strings are kept latin1
// (bytes 1:1 as chars 0..255) — the shape RanText.DecodeCp874(string)
// expects, matching itemdb/questdb/npcshops.
//
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { RccArchive } = require('./rcc.js');
const gamecrypt = require('./gamecrypt.js');
const crowdata = require('./crowdata.js');
const npcshops = require('./extract-npcshops.js');   // reuses findRcc + parseCrowStrTable

function loadGlogic(arc, name) {
  let b = arc.read(name);
  if (gamecrypt.isEncoded(b)) b = gamecrypt.decode(b);
  return b;
}

const base = path.resolve(__dirname, '../../..');
const RAN = path.join(base, 'Ran');
const DEFAULT_RES = path.join(base, 'MOBILE/unity/RanMobile/Assets/Ran/Resources');

const argv = process.argv.slice(2);
const has = (f) => argv.includes(f);
const val = (f, d) => { const i = argv.indexOf(f); return i === -1 ? d : argv[i + 1]; };
const DRY = has('--dry');
const RES = val('--out', DEFAULT_RES);
const OUT = path.join(RES, 'mobnames.json');

function findRcc(re) {
  let found = null;
  (function scan(dir) {
    for (const it of fs.readdirSync(dir, { withFileTypes: true })) {
      if (found) return;
      const p = path.join(dir, it.name);
      if (it.isDirectory()) scan(p);
      else if (re.test(it.name)) found = p;
    }
  })(path.join(RAN, 'data'));
  return found;
}

function run() {
  const glogicRcc = findRcc(/^glogic\.rcc$/i);
  if (!glogicRcc) { console.log('! GLogic.rcc not found'); process.exit(1); }
  console.log('GLogic.rcc:', path.relative(base, glogicRcc));

  const gArc = new RccArchive(glogicRcc);

  const crowParsed = crowdata.parse(gArc.read('Crow.mnsf'));
  console.log(`Crow.mnsf: ${crowParsed.count} records, EOF-exact=${crowParsed.eofExact} (trailing ${crowParsed.trailing})`);

  let nameMap = new Map();
  const strTableName = gArc.list().find((n) => /^crowstrtable\.txt$/i.test(n));
  if (strTableName) {
    nameMap = npcshops.parseCrowStrTable(loadGlogic(gArc, strTableName));
  }
  console.log(`CrowStrTable.txt: ${nameMap.size} name keys`);

  const names = {};
  let resolved = 0, fallback = 0;
  for (const cr of crowParsed.crows) {
    const key = `${cr.main}:${cr.sub}`;
    const name = nameMap.get(cr.nameKey);
    if (name) { names[key] = name; resolved++; }
    else { fallback++; }   // no string-table entry for this key — leave unresolved, not guessed
  }
  console.log(`resolved ${resolved}/${crowParsed.count} crow names (${fallback} have no CrowStrTable entry)`);

  if (DRY) {
    const sample = Object.entries(names).slice(0, 8);
    console.log('sample:', sample);
    console.log('(dry run — wrote nothing)');
    return;
  }

  fs.mkdirSync(RES, { recursive: true });
  const note = 'Crow/NPC display names, keyed "main:sub" (SNATIVEID). Raw cp874 ' +
    '(latin1 bytes) — decode with RanText.DecodeCp874 before display. Built from ' +
    'Crow.mnsf + CrowStrTable.txt (see extract-mobnames.js header). A key absent ' +
    'here has no name in the real client\'s own string table either — the ' +
    'mobile HUD should fall back to the "#main:sub" tag, not invent a name.';
  fs.writeFileSync(OUT, JSON.stringify({ note, names }));
  console.log(`wrote ${path.relative(base, OUT)} (${(fs.statSync(OUT).size / 1024).toFixed(0)} KB)`);

  const metaPath = OUT + '.meta';
  if (!fs.existsSync(metaPath)) {
    const guid = crypto.createHash('md5').update('Assets/Ran/Resources/mobnames.json').digest('hex').slice(0, 32);
    fs.writeFileSync(metaPath, `fileFormatVersion: 2
guid: ${guid}
TextScriptImporter:
  externalObjects: {}
  userData:
  assetBundleName:
  assetBundleVariant:
`);
    console.log(`wrote mobnames.json.meta (guid ${guid})`);
  }
}

if (require.main === module) run();
module.exports = { run };
