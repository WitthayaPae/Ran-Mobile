'use strict';
//
// Build itemmix.json — the PRODUCT catalog CProductWindow browses
// (SOURCE/Lib_ClientUI/Interface/ProductWindow*.cpp), from GLogic.rcc's
// itemmix.ims (GLItemMixMan::LoadFile, GLItemMixMan.cpp:90-142).
//
//   node extract-itemmix.js            write itemmix.json
//   node extract-itemmix.js --dry      parse + report, write nothing
//   node extract-itemmix.js --out DIR  write into DIR
//
// CONTAINER (measured, not assumed):
//   132-byte CSerialFile header    type "GLITEM_MIX", version @128 (real file: 1)
//   version >= VERSION_ENCODE_OLD(0x0100) -> EMBYTECRYPT_OLD, then
//   version >= VERSION_ENCODE(0x0200)     -> EMBYTECRYPT_ITEMIX (later gate wins,
//     same "later gate wins" rule already established for Crow.mnsf). Measured,
//     not assumed: the real shipped itemmix.ims has fileVer=0x0200, so
//     EMBYTECRYPT_ITEMIX applies, and decoding under that table is what parses
//     the body to EOF-exact (300 records, 0 bytes left) — decoding it as
//     plaintext does not.
//   DWORD count
//   count x ITEM_MIX::LOAD (GLItemMix.cpp:9-133) — plain field-by-field stream,
//     dispatched on a per-record DWORD version (0x0100/0x0101/0x0102/0x0200 seen
//     in source; only the branch matching the real shipped version is needed —
//     measured below, not guessed).
//
// ITEM_MIX (GLItemMix.h) v0x0200 fields, in order:
//   dwKey(DWORD), 5x [main(WORD) sub(WORD) num(BYTE)] sMeterialItem,
//   [main(WORD) sub(WORD) num(BYTE)] sResultItem, dwRate(DWORD), dwPrice(DWORD),
//   fTime(float), dwData1(DWORD), dwData2(DWORD), nData1(BYTE), nData2(BYTE),
//   wLevelReq(WORD), wCategory(WORD), wData1(WORD), wData2(WORD), wData3(WORD),
//   cDAMAGE..cRESIST_SPIRIT (7x BYTE), bGenerateRandomValue(BOOL, 4 bytes).
//
// GLItemMixMan::insert() puts EVERY loaded record into m_mapKeyProduct
// unconditionally (both client and server builds) — GetProduct(dwKey), the
// only lookup CProductWindowList/CProductWindowProduct ever call, has no
// m_bServer gate either. So the whole file is legitimately client-readable;
// this is not the server-only .genitem drop-table situation documented in
// RanBossDetailsPackets.

const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc.js');
const gamecrypt = require('./gamecrypt.js');
const bytecrypt = require('./bytecrypt.js');

const base = path.resolve(__dirname, '../../..');
const RAN = path.join(base, 'Ran');
const RES = path.join(base, 'MOBILE/unity/RanMobile/Assets/Ran/Resources');

const BODY_OFFSET = 132;
const VERSION_ENCODE_OLD = 0x0100;
const VERSION_ENCODE = 0x0200;

class Cursor {
  constructor(buf, at) { this.b = buf; this.p = at; }
  get left() { return this.b.length - this.p; }
  _need(n) { if (this.p + n > this.b.length) throw new Error(`itemmix: read past end (need ${n}, have ${this.left})`); }
  u8() { this._need(1); const v = this.b.readUInt8(this.p); this.p += 1; return v; }
  u16() { this._need(2); const v = this.b.readUInt16LE(this.p); this.p += 2; return v; }
  u32() { this._need(4); const v = this.b.readUInt32LE(this.p); this.p += 4; return v; }
  f32() { this._need(4); const v = this.b.readFloatLE(this.p); this.p += 4; return v; }
  skip(n) { this._need(n); this.p += n; }
}

const ITEMMIX_ITEMNUM = 5;

function readItemMixData(c) {
  const main = c.u16(), sub = c.u16(), num = c.u8();
  return { main, sub, num };
}

// ITEM_MIX::LOAD, version-dispatched (GLItemMix.cpp:9-133).
function readItemMix(c) {
  const ver = c.u32();
  const rec = { ver };
  if (ver === 0x0200) {
    rec.key = c.u32();
    rec.materials = [];
    for (let i = 0; i < ITEMMIX_ITEMNUM; i++) rec.materials.push(readItemMixData(c));
    rec.result = readItemMixData(c);
    rec.rate = c.u32();
    rec.price = c.u32();
    rec.time = c.f32();
    c.u32(); c.u32();      // dwData1, dwData2 (point req / point reward — unused)
    c.u8(); c.u8();        // nData1, nData2
    rec.levelReq = c.u16();
    rec.category = c.u16();
    c.u16(); c.u16(); c.u16();  // wData1..3
    c.skip(7);              // cDAMAGE..cRESIST_SPIRIT
    c.u32();                 // bGenerateRandomValue (BOOL, 4 bytes)
  } else if (ver === 0x0102) {
    rec.key = c.u32();
    rec.materials = [];
    for (let i = 0; i < ITEMMIX_ITEMNUM; i++) rec.materials.push(readItemMixData(c));
    rec.result = readItemMixData(c);
    rec.rate = c.u32();
    rec.price = c.u32();
    rec.time = c.f32();
    c.u32(); c.u32();
    c.u8(); c.u8();
    rec.levelReq = c.u16();
    rec.category = c.u16();
    c.u16(); c.u16(); c.u16();
  } else if (ver === 0x0101) {
    rec.key = c.u32();
    rec.materials = [];
    for (let i = 0; i < ITEMMIX_ITEMNUM; i++) rec.materials.push(readItemMixData(c));
    rec.result = readItemMixData(c);
    rec.rate = c.u32();
    rec.price = c.u32();
    rec.time = c.f32();
    c.u32(); c.u32();
    rec.levelReq = 0; rec.category = 9; // PRODUCT_TYPE_ETC default
  } else if (ver === 0x0100) {
    rec.key = c.u32();
    rec.materials = [];
    for (let i = 0; i < ITEMMIX_ITEMNUM; i++) rec.materials.push(readItemMixData(c));
    rec.result = readItemMixData(c);
    rec.rate = c.u32();
    rec.price = c.u32();
    rec.time = 1.0; rec.levelReq = 0; rec.category = 9;
  } else {
    throw new Error(`itemmix: unknown ITEM_MIX version 0x${ver.toString(16)}`);
  }
  return rec;
}

function parse(raw) {
  let buf = raw;
  if (gamecrypt.isEncoded(buf)) buf = gamecrypt.decode(buf);
  const type = buf.slice(0, 128).toString('latin1').replace(/\0.*$/, '');
  if (type !== 'GLITEM_MIX') throw new Error(`itemmix: not a GLITEM_MIX file (type "${type}")`);
  const fileVer = buf.readUInt32LE(128);

  let body = buf;
  if (fileVer >= VERSION_ENCODE) body = bytecrypt.decode(Buffer.from(buf), 'EMBYTECRYPT_ITEMIX', BODY_OFFSET, buf.length);
  else if (fileVer >= VERSION_ENCODE_OLD) body = bytecrypt.decode(Buffer.from(buf), 'EMBYTECRYPT_OLD', BODY_OFFSET, buf.length);

  const c = new Cursor(body, BODY_OFFSET);
  const count = c.u32();
  if (count > 100000) throw new Error(`itemmix: implausible count ${count}`);

  const items = [];
  for (let i = 0; i < count; i++) items.push(readItemMix(c));

  return { fileVer, count, items, consumed: c.p, eofExact: c.p === body.length, trailing: body.length - c.p };
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
  const raw = arc.read('itemmix.ims');
  const parsed = parse(raw);

  console.log(`itemmix.ims: fileVer=0x${parsed.fileVer.toString(16)} count=${parsed.count} ` +
    `EOF-exact=${parsed.eofExact} (trailing ${parsed.trailing})`);

  const byCategory = new Map();
  for (const it of parsed.items) byCategory.set(it.category, (byCategory.get(it.category) || 0) + 1);
  console.log('by category:', Object.fromEntries(byCategory));

  if (dry) return;

  const out = {
    note: 'PRODUCT catalog (ProductWindow) from GLogic.rcc/itemmix.ims, ' +
      'GLItemMixMan::GetProduct() — client-readable, no server gate. ' +
      'k=key m=materials(m,s,n) r=result(m,s,n) rate=% price=gold t=seconds lvl=levelReq cat=PRODUCT_TYPE',
    items: parsed.items.map((it) => ({
      k: it.key,
      m: it.materials.filter((mm) => mm.main !== 0 || mm.sub !== 0),
      r: it.result,
      rate: it.rate, price: it.price, t: it.time,
      lvl: it.levelReq, cat: it.category,
    })),
  };

  fs.mkdirSync(outDir, { recursive: true });
  const outPath = path.join(outDir, 'itemmix.json');
  fs.writeFileSync(outPath, JSON.stringify(out));
  console.log(`wrote ${outPath} (${out.items.length} products)`);
}

if (require.main === module) run();
module.exports = { parse };
