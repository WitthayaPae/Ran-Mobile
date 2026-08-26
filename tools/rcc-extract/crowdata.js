'use strict';
//
// `Crow.mnsf` — the master NPC/mob database (`GLCrowDataMan`, GLCrowData.cpp).
//
// Every crow (NPC or mob) the world can spawn has a record here, keyed by its
// SNATIVEID. For the NPC shop/dialogue work only three things per NPC matter:
// its native id, its dialogue file (`m_strTalkFile`, a `.ntk`) and its up-to-3
// sale files (`m_strSaleFile[3]`, `.crowsale`) plus the newer `.npcshop`. This
// module pulls exactly those out and skips the rest.
//
// CONTAINER (GLCrowDataMan::LoadFile, GLCrowData.cpp:947):
//   132-byte CSerialFile header      type "GLCROW", FileID version @128
//   version >= ENCODE_VER_OLD(0x0102) -> EMBYTECRYPT_OLD, then
//   version >= ENCODE_VER(0x0200)     -> EMBYTECRYPT_CROW   (later gate wins)
//   DWORD bodyVersion                @132 (encoded)
//   DWORD crowCount
//   crowCount × SCROWDATA::LoadFile
//
// RECORD (SCROWDATA::LoadFile, GLCrowData.cpp:178). bPastLoad is TRUE here
// (GLogicData.cpp:1174), so old SBASIC versions read their versioned struct:
//   DWORD recordVersion
//   repeat until FILE_END_DATA(0xEDEDEDED):
//     DWORD dataType; DWORD blockVersion; DWORD blockSize
//     FILE_SBASIC(1):  [blitted SCROWBASIC]  -> sNativeID @0, name key @16
//                      size is authoritative (blit) -> skip by size
//     FILE_SACTION(2): SCROWACTION::LOAD (serialized) -> the file-name strings
//                      block written with BeginBlock/EndBlock, so size is exact
//     FILE_SGEN(3):    SCROWGEN::SAVE writes std::strings, so the DECLARED size
//                      (sizeof(SCROWGEN)) UNDERCOUNTS -> must parse structurally
//     FILE_SATTACK(4): BeginBlock/EndBlock -> size exact -> skip by size
//
// The proof this walk is right: the whole body is consumed to its exact final
// byte (1853 records, 0 bytes left).

const gamecrypt = require('./gamecrypt.js');
const bytecrypt = require('./bytecrypt.js');

const CROW_TABLE = 'EMBYTECRYPT_CROW';
const BODY_OFFSET = 132;
const FILE_SBASIC = 1, FILE_SACTION = 2, FILE_SGEN = 3, FILE_SATTACK = 4;
const FILE_END_DATA = 0xededed_ed;
const SALENUM = 3;
// SCROWBASIC (probe): sNativeID @0, name key m_szName @16.
const NAME_OFF = 16, NAME_LEN = 33;

class Cursor {
  constructor(buf, at) { this.b = buf; this.p = at; }
  get left() { return this.b.length - this.p; }
  _need(n) { if (this.p + n > this.b.length) throw new Error(`crowdata: read past end (need ${n}, have ${this.left})`); }
  u16() { this._need(2); const v = this.b.readUInt16LE(this.p); this.p += 2; return v; }
  u32() { this._need(4); const v = this.b.readUInt32LE(this.p); this.p += 4; return v; }
  f32() { this._need(4); const v = this.b.readFloatLE(this.p); this.p += 4; return v; }
  skip(n) { this._need(n); this.p += n; }
  // CSerialFile std::string: [DWORD count-incl-NUL][bytes]; value up to first NUL.
  str() {
    const n = this.u32();
    if (n > 1 << 20) throw new Error(`crowdata: implausible string length ${n}`);
    this._need(n);
    const raw = this.b.slice(this.p, this.p + n); this.p += n;
    const z = raw.indexOf(0);
    return (z < 0 ? raw : raw.slice(0, z)).toString('latin1');
  }
}

// SCROWACTION::LOAD (GLCrowDataAction.cpp:490). Only walked as far as the shop
// file; the enclosing block size (BeginBlock/EndBlock) carries us to the end.
function readAction(c, blockStart, blockSize) {
  c.u32();  // m_dwActFlag
  c.u32();  // m_emMoveType
  c.f32();  // m_fDriftHeight
  c.f32();  // m_fWalkVelo
  c.u32();  // m_bRun (BOOL)
  c.f32();  // m_fRunVelo
  c.u16();  // m_wBodyRadius
  c.str();  // m_strSkinObj
  const talkFile = c.str();
  c.u32();  // m_bAfterFall_NoBody (BOOL)
  c.str();  // m_strBirthEffect
  c.str();  // m_strFallingEffect
  c.str();  // m_strBlowEffect
  const saleFiles = [];
  for (let i = 0; i < SALENUM; i++) saleFiles.push(c.str());
  const shopFile = c.str();
  // Everything after (live-time, mob-link, patterns[], item-exchange) is skipped
  // by the exact block size rather than parsed.
  c.p = blockStart + blockSize;
  return { talkFile, saleFiles, shopFile };
}

// SCROWGEN::LOAD (GLCrowDataGen.cpp:78). Must be parsed structurally because its
// declared size (sizeof) undercounts the two std::strings it writes.
function skipGen(c) {
  c.u16();  // m_wGenItem_Rate
  c.u16();  // m_wGenMoney_Rate
  c.u32();  // m_sGenItemID.dwID
  c.u32();  // m_dwGenMoney
  c.str();  // m_strQtGenItem
  c.str();  // m_strGenItem
  c.f32();  // m_fMoneyLuckRate
  c.u16();  // m_wMoneyLuckMax
}

/**
 * Parse Crow.mnsf. `raw` is the RCC entry bytes (still gamecrypt-wrapped).
 * @returns {{bodyVersion, count, crows, consumed, eofExact}}
 */
function parse(raw) {
  let buf = raw;
  if (gamecrypt.isEncoded(buf)) buf = gamecrypt.decode(buf);
  const type = buf.slice(0, 128).toString('latin1').replace(/\0.*$/, '');
  if (type !== 'GLCROW') throw new Error(`crowdata: not a GLCROW file (type "${type}")`);
  const fileVer = buf.readUInt32LE(128);

  // The whole body is byte-substituted; decode from 132 once (later gate wins →
  // CROW table; OLD would only apply for 0x0102..0x01ff, none of which ship).
  const body = fileVer >= 0x0200
    ? bytecrypt.decode(Buffer.from(buf), CROW_TABLE, BODY_OFFSET, buf.length)
    : buf;

  const c = new Cursor(body, BODY_OFFSET);
  const bodyVersion = c.u32();
  const count = c.u32();
  if (count > 1_000_000) throw new Error(`crowdata: implausible crow count ${count}`);

  const crows = [];
  for (let i = 0; i < count; i++) {
    c.u32();  // record version
    let rec = { main: 0, sub: 0, nameKey: '', talkFile: '', saleFiles: ['', '', ''], shopFile: '' };
    let dt = c.u32();
    let guard = 0;
    while (dt !== FILE_END_DATA) {
      if (++guard > 64) throw new Error(`crowdata: runaway record ${i}`);
      const blockVer = c.u32();
      const blockSize = c.u32();
      const start = c.p;
      if (dt === FILE_SBASIC) {
        rec.main = c.b.readUInt16LE(start);
        rec.sub = c.b.readUInt16LE(start + 2);
        const nm = c.b.slice(start + NAME_OFF, start + NAME_OFF + NAME_LEN);
        const z = nm.indexOf(0);
        rec.nameKey = (z < 0 ? nm : nm.slice(0, z)).toString('latin1');
        c.p = start + blockSize;
      } else if (dt === FILE_SACTION) {
        const a = readAction(c, start, blockSize);
        rec.talkFile = a.talkFile;
        rec.saleFiles = a.saleFiles;
        rec.shopFile = a.shopFile;
      } else if (dt === FILE_SGEN) {
        skipGen(c);                       // size unreliable — structural
      } else if (dt === FILE_SATTACK) {
        c.p = start + blockSize;          // BeginBlock/EndBlock size is exact
      } else {
        throw new Error(`crowdata: unknown dataType 0x${dt.toString(16)} at record ${i}`);
      }
      dt = c.u32();
    }
    crows.push(rec);
  }

  return { type, fileVer, bodyVersion, count, crows, consumed: c.p, eofExact: c.p === body.length, trailing: body.length - c.p };
}

module.exports = { parse, CROW_TABLE, BODY_OFFSET };
