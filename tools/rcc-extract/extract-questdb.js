'use strict';
//
// Build the QUEST database the mobile quest-log UI needs, from Quest.rcc.
//
//   node extract-questdb.js            emit questdb.json into Resources
//   node extract-questdb.js --out P    override the json path
//   node extract-questdb.js --dry      parse + report, write nothing
//
// Pipeline (all cites in questdata.js):
//   quest.lst  -->  QUEST <id> <file> //<cp874 name>   (the id -> file map)
//   <file>.qst --decodeQst-->  GLQUEST { title, comment, area, flags, steps[],
//                                        gift{ money,exp,points, items[], skills[] } }
//
// The `.qst` body is EMBYTECRYPT_OLD-substituted after a 132-byte CSerialFile
// header; every field is variable-length, so the correctness proof is that each
// file is consumed to its exact final byte (EOF-exact). Strings are RAW cp874,
// decoded at runtime by RanText.DecodeCp874 — the same contract itemdb.json uses.
//
// Reward item ids are the (mainId, subId) pair keyed by RanItemDb, so the log can
// draw each reward's real icon + name.

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { RccArchive } = require('./rcc.js');
const bytecrypt = require('./bytecrypt.js');
const questdata = require('./questdata.js');

const base = path.resolve(__dirname, '../../..');
const RAN = path.join(base, 'Ran');
const OUT_JSON = path.join(base,
  'MOBILE/unity/RanMobile/Assets/Ran/Resources/questdb.json');

const argv = process.argv.slice(2);
const has = (f) => argv.includes(f);
const val = (f, d) => { const i = argv.indexOf(f); return i === -1 ? d : argv[i + 1]; };

const UINT_MAX = questdata.UINT_MAX;
const USHRT_MAX = questdata.USHRT_MAX;

// --- locate Quest.rcc in the deploy tree -------------------------------------
function findQuestRcc() {
  let found = null;
  (function scan(dir) {
    for (const it of fs.readdirSync(dir, { withFileTypes: true })) {
      if (found) return;
      const p = path.join(dir, it.name);
      if (it.isDirectory()) { scan(p); continue; }
      if (/^quest\.rcc$/i.test(it.name)) found = p;
    }
  })(path.join(RAN, 'data'));
  return found;
}

// --- parse quest.lst: id -> { file, name(raw cp874) } ------------------------
function parseList(buf) {
  const map = new Map();
  const text = buf.toString('latin1');           // keep bytes 1:1 for the name
  for (const line of text.split(/\r?\n/)) {
    if (!/^QUEST\b/.test(line)) continue;
    // QUEST <id> <file> //<name>
    const m = line.match(/^QUEST[\s,]+(\d+)[\s,]+([^\s,]+)\s*(?:\/\/(.*))?$/);
    if (!m) continue;
    map.set(parseInt(m[1], 10), { file: m[2], name: (m[3] || '').trim() });
  }
  return map;
}

// --- build the compact record for one quest ----------------------------------
function buildRecord(id, listEntry, Q, eofExact) {
  const flags = Q.flags | 0;
  const rec = {
    id,
    ver: Q.ver,
    t: Q.title || '',
    c: Q.comment || '',
    ln: listEntry ? listEntry.name : '',   // quest.lst display name (fallback)
    area: Q.area | 0,
    fl: flags,
    money: Q.beginMoney | 0,
    time: Q.limitTime | 0,
  };
  // gift block
  const g = Q.gift || {};
  rec.g = {
    money: g.money | 0,
    exp: Number(g.exp || 0),
    sp: g.skillPoint | 0,
    stp: g.statPoint | 0,
    lp: g.lifePoint | 0,
    el: g.element | 0,
    items: (g.items || []).filter((it) => it.m || it.s),
    skills: (g.skills || []).filter((x) => x && x !== UINT_MAX),
  };
  // steps: keep only the active objectives (mirrors GLQUEST_STEP::IsNEED_*)
  rec.steps = (Q.steps || []).map((s) => {
    const o = { t: s.title || '', c: s.comment || '' };
    if (s.mobKillId !== undefined && s.mobKillId !== UINT_MAX)
      o.mob = { m: s.mobKillId & 0xffff, s: (s.mobKillId >>> 16) & 0xffff, n: s.mobKillNum | 0, o: s.mobKillText || '' };
    if (s.npcTalkId !== undefined && s.npcTalkId !== UINT_MAX)
      o.npc = { id: s.npcTalkId >>> 0, o: s.npcTalkText || '' };
    if (s.genItems && s.genItems.length)
      o.gen = { o: s.genText || '', items: s.genItems.map((it) => ({ m: it.m, s: it.s, n: it.num | 0 })) };
    if (s.reachMap !== undefined && s.reachMap !== 0 && s.reachMap !== UINT_MAX)
      o.rz = { map: s.reachMap >>> 0, o: s.reachText || '' };
    if (s.level !== undefined && s.level !== USHRT_MAX)
      o.lv = s.level | 0;
    if (s.defMap !== undefined && s.defMap !== 0 && s.defMap !== UINT_MAX)
      o.dz = { o: s.defText || '', time: s.defTime || 0 };
    return o;
  });
  if (!eofExact) rec.partial = true;   // rewards may be unreliable (see questdata.js)
  return rec;
}

// --- run ---------------------------------------------------------------------
function run() {
  const rccPath = findQuestRcc();
  if (!rccPath) { console.log('! Quest.rcc not found under Ran/data'); process.exit(1); }
  console.log('Quest.rcc:', path.relative(base, rccPath));

  const arc = new RccArchive(rccPath);
  const lstName = arc.list().find((n) => /^quest\.lst$/i.test(n));
  if (!lstName) { console.log('! quest.lst missing from Quest.rcc'); process.exit(1); }
  const list = parseList(arc.read(lstName));
  console.log(`quest.lst: ${list.size} entries`);

  const records = [];
  const rep = { total: 0, eofExact: 0, partial: 0, failed: 0, byVer: {}, withSteps: 0, withRewards: 0 };
  const failures = [];

  for (const [id, entry] of [...list.entries()].sort((a, b) => a[0] - b[0])) {
    // entry.file is a bare name; RCC entries are flat + lowercased.
    let raw;
    try { raw = arc.read(entry.file.toLowerCase()); }
    catch { failures.push(`${id} ${entry.file}: not in archive`); rep.failed++; continue; }

    const res = questdata.decodeQst(raw, bytecrypt);
    if (!res.quest) { failures.push(`${id} ${entry.file}: ${res.error || 'no quest'}`); rep.failed++; continue; }

    rep.total++;
    const vk = '0x' + res.quest.ver.toString(16);
    rep.byVer[vk] = (rep.byVer[vk] || 0) + 1;
    if (res.eofExact) rep.eofExact++; else { rep.partial++; failures.push(`${id} ${entry.file} v${vk}: read ${res.bytesRead}/${res.bodyLen} (partial)`); }

    const rec = buildRecord(id, entry, res.quest, res.eofExact);
    if (rec.steps.length) rep.withSteps++;
    if (rec.g.items.length || rec.g.exp || rec.g.money) rep.withRewards++;
    records.push(rec);
  }

  console.log(`\nquests: ${rep.total}  EOF-exact: ${rep.eofExact}  partial: ${rep.partial}  failed: ${rep.failed}`);
  console.log('by version:', JSON.stringify(rep.byVer));
  console.log(`with steps: ${rep.withSteps}  with rewards: ${rep.withRewards}`);
  if (failures.length) {
    console.log(`\nanomalies (${failures.length}):`);
    for (const f of failures.slice(0, 12)) console.log('  ' + f);
  }

  if (has('--dry')) { console.log('\n--dry: nothing written'); return; }

  const outPath = val('--out', OUT_JSON);
  fs.writeFileSync(outPath, JSON.stringify({ quests: records }));
  console.log(`\nwrote ${path.relative(base, outPath)} ` +
    `(${(fs.statSync(outPath).size / 1024).toFixed(0)} KB, ${records.length} quests)`);

  const metaPath = outPath + '.meta';
  if (!fs.existsSync(metaPath)) {
    const guid = crypto.createHash('md5')
      .update('Assets/Ran/Resources/questdb.json').digest('hex').slice(0, 32);
    fs.writeFileSync(metaPath, `fileFormatVersion: 2
guid: ${guid}
TextScriptImporter:
  externalObjects: {}
  userData:
  assetBundleName:
  assetBundleVariant:
`);
    console.log(`wrote questdb.json.meta (guid ${guid})`);
  }
}

if (require.main === module) run();
module.exports = { parseList, buildRecord, findQuestRcc };
