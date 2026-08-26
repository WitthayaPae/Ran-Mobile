'use strict';
//
// Build activitydb.json — the ACTIVITY catalog CStudentRecordWindow's Activity
// page browses (SOURCE/Lib_ClientUI/Interface/ActivityPage*.cpp), from
// GLogic.rcc's activity.asf (GLActivity::LoadFile, GLActivity.cpp:35-85).
//
//   node extract-activitydb.js            write activitydb.json
//   node extract-activitydb.js --dry      parse + report, write nothing
//   node extract-activitydb.js --out DIR  write into DIR
//
// CONTAINER (measured):
//   132-byte CSerialFile header    type "GLACTIVITY", CONTAINER version @128
//     (real file: 1 — GLActivity::VERSION)
//   version >= VERSION_ENCODE(0x0001) -> EMBYTECRYPT_ACTIVITY (GLActivity.h:
//     VERSION = VERSION_ENCODE = 0x0001, so this table applies to every
//     container version this loader accepts — there is no plaintext path)
//   DWORD count
//   count x SACTIVITY_FILE_DATA::LOAD (GLActivityData.cpp:13-56), each
//     prefixed with its OWN per-RECORD version DWORD — a second, independent
//     version from the container's. SACTIVITY_FILE_DATA::VERSION = 0x0009
//     (GLActivityData.h:28); every record in the real file measures 9, so
//     only the dwVer==VERSION(9) branch (GLActivityData.cpp:18-55) is
//     implemented, not the older 0x0008/dead branches below it in source.
//
// SACTIVITY_FILE_DATA v9 fields, in order (GLActivityData.cpp:18-55):
//   dwActivityID(DWORD), strActivityTitle/strBadgeString/strDescription
//   (CSerialFile std::string: DWORD len INCLUDING the NUL, then bytes),
//   emType(DWORD), emNotify(DWORD), dwRewardPoint(DWORD), bRewardBadge(BOOL,4),
//   wProgressLevel(WORD), sidMobKill.dwID(DWORD) wProgressMobKill(WORD),
//   sidMapKill.dwID(DWORD) wProgressMapKill(WORD),
//   sidMapReach.dwID(DWORD) wProgressMapReach(WORD),
//   sidItemGet.dwID(DWORD) wProgressItemGet(WORD),
//   sidItemUse.dwID(DWORD) wProgressItemUse(WORD),
//   dwQBoxType(DWORD) wQBoxProgress(WORD), dwQuestID(DWORD) wQuestProgress(WORD),
//   dwActivityProgress(DWORD).
//
// Only the fields CActivityPageMainSlot actually renders are kept: title,
// description, badge text/flag, reward point, and type (for the page's
// category filter, EMACTIVITY_TYPE). The per-target progress fields
// (sidMobKill, wProgressLevel, ...) describe HOW the server tracks progress
// server-side; the client never needs them itself — its own progress comes
// from the SEPARATE SACTIVITY_CHAR_DATA wire push (RanActivityPackets), not
// from this local file.

const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc.js');
const gamecrypt = require('./gamecrypt.js');
const bytecrypt = require('./bytecrypt.js');

const base = path.resolve(__dirname, '../../..');
const RAN = path.join(base, 'Ran');
const RES = path.join(base, 'MOBILE/unity/RanMobile/Assets/Ran/Resources');

const BODY_OFFSET = 132;
const VERSION_ENCODE = 0x0001;

class Cursor {
  constructor(buf, at) { this.b = buf; this.p = at; }
  get left() { return this.b.length - this.p; }
  _need(n) { if (this.p + n > this.b.length) throw new Error(`activitydb: read past end (need ${n}, have ${this.left})`); }
  u16() { this._need(2); const v = this.b.readUInt16LE(this.p); this.p += 2; return v; }
  u32() { this._need(4); const v = this.b.readUInt32LE(this.p); this.p += 4; return v; }
  skip(n) { this._need(n); this.p += n; }
  // CSerialFile std::string: [DWORD count-incl-NUL][bytes]; value up to first NUL.
  str() {
    const n = this.u32();
    if (n > 1 << 20) throw new Error(`activitydb: implausible string length ${n}`);
    this._need(n);
    const raw = this.b.slice(this.p, this.p + n); this.p += n;
    const z = raw.indexOf(0);
    return (z < 0 ? raw : raw.slice(0, z)).toString('latin1');
  }
}

function readActivity(c) {
  const ver = c.u32();
  // SACTIVITY_FILE_DATA::VERSION = 0x0009 (GLActivityData.h:28) — a per-RECORD
  // version distinct from the container's own file version (GLActivity::VERSION
  // = 0x0001, checked separately at byte 128). Measured on the real file: every
  // record is 9, so only that branch (GLActivityData.cpp:18-55) is implemented.
  if (ver !== 9) throw new Error(`activitydb: unknown SACTIVITY_FILE_DATA version ${ver}`);

  const id = c.u32();
  const title = c.str();
  const badge = c.str();
  const desc = c.str();
  const emType = c.u32();
  const emNotify = c.u32();
  const rewardPoint = c.u32();
  const rewardBadge = c.u32() !== 0;   // BOOL, 4 bytes

  c.u16();                              // wProgressLevel
  c.u32(); c.u16();                     // sidMobKill.dwID, wProgressMobKill
  c.u32(); c.u16();                     // sidMapKill.dwID, wProgressMapKill
  c.u32(); c.u16();                     // sidMapReach.dwID, wProgressMapReach
  c.u32(); c.u16();                     // sidItemGet.dwID, wProgressItemGet
  c.u32(); c.u16();                     // sidItemUse.dwID, wProgressItemUse
  c.u32(); c.u16();                     // dwQBoxType, wQBoxProgress
  c.u32(); c.u16();                     // dwQuestID, wQuestProgress
  c.u32();                              // dwActivityProgress

  return { id, title, badge, desc, emType, emNotify, rewardPoint, rewardBadge };
}

function parse(raw) {
  let buf = raw;
  if (gamecrypt.isEncoded(buf)) buf = gamecrypt.decode(buf);
  const type = buf.slice(0, 128).toString('latin1').replace(/\0.*$/, '');
  if (type !== 'GLACTIVITY') throw new Error(`activitydb: not a GLACTIVITY file (type "${type}")`);
  const fileVer = buf.readUInt32LE(128);

  const body = fileVer >= VERSION_ENCODE
    ? bytecrypt.decode(Buffer.from(buf), 'EMBYTECRYPT_ACTIVITY', BODY_OFFSET, buf.length)
    : buf;

  const c = new Cursor(body, BODY_OFFSET);
  const count = c.u32();
  if (count > 100000) throw new Error(`activitydb: implausible count ${count}`);

  const items = [];
  const dupSkipped = [];
  const seen = new Set();
  for (let i = 0; i < count; i++) {
    const a = readActivity(c);
    if (seen.has(a.id)) { dupSkipped.push(a.id); continue; }  // GetActivity() dedup, mirrors LoadFile
    seen.add(a.id);
    items.push(a);
  }

  return { fileVer, count, items, dupSkipped, consumed: c.p, eofExact: c.p === body.length, trailing: body.length - c.p };
}

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
  const argv = process.argv.slice(2);
  const dry = argv.includes('--dry');
  const outIdx = argv.indexOf('--out');
  const outDir = outIdx !== -1 ? argv[outIdx + 1] : RES;

  const glogicRcc = findRcc(/^glogic\.rcc$/i);
  if (!glogicRcc) { console.log('! GLogic.rcc not found'); process.exit(1); }
  const arc = new RccArchive(glogicRcc);
  const raw = arc.read('activity.asf');
  const parsed = parse(raw);

  console.log(`activity.asf: fileVer=${parsed.fileVer} count=${parsed.count} kept=${parsed.items.length} ` +
    `dupSkipped=${parsed.dupSkipped.length} EOF-exact=${parsed.eofExact} (trailing ${parsed.trailing})`);

  const byType = new Map();
  for (const it of parsed.items) byType.set(it.emType, (byType.get(it.emType) || 0) + 1);
  console.log('by type:', Object.fromEntries(byType));

  if (dry) return;

  const out = {
    note: 'ACTIVITY catalog (StudentRecordWindow -> ActivityPage) from ' +
      'GLogic.rcc/activity.asf, GLActivity::GetActivity() — client-side static ' +
      'table (title/description/badge/reward), joined at runtime with the ' +
      'per-character SACTIVITY_CHAR_DATA wire push (RanActivityPackets) for ' +
      'progress. Text is raw cp874, decode with RanText.DecodeCp874. ' +
      't=EMACTIVITY_TYPE b=badge text bb=hasBadge p=rewardPoint',
    items: parsed.items.map((it) => ({
      id: it.id, title: it.title, desc: it.desc,
      t: it.emType, b: it.badge, bb: it.rewardBadge, p: it.rewardPoint,
    })),
  };

  fs.mkdirSync(outDir, { recursive: true });
  const outPath = path.join(outDir, 'activitydb.json');
  fs.writeFileSync(outPath, JSON.stringify(out));
  console.log(`wrote ${outPath} (${out.items.length} activities)`);
}

if (require.main === module) run();
module.exports = { parse };
