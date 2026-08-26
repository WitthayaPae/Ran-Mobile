'use strict';
//
// `item.isf` / `item1.isf` — the item table (`GLItemMan::LoadFile`).
//
// This exists to answer one question the texture audit could not: **which icon
// sheet does each item use?** Item icons are addressed by `strInventoryFile`
// plus a sub-image `sICONID`, so no scan for literal filenames can recover the
// relationship — the sheets on disk are numeric (`103.dds`, `103_p.dds`).
//
// Container (`GLItemMan.cpp:69`):
//
//   132-byte CSerialFile header    type "GLITEM"
//   DWORD count
//   count x SITEM::LoadFile
//
// Encoding is version-gated twice, later gate wins (`GLItemMan.h:45`):
//   version >= 0x0100 -> EMBYTECRYPT_OLD
//   version >= 0x0200 -> EMBYTECRYPT_ITEM
//
// `SITEM::LoadFile` (`GLItem.cpp:124`) is the reason this is tractable at all:
//
//   DWORD recordVersion
//   repeat:
//     DWORD blockType            0xEDEDEDED ends the record
//     DWORD blockVersion
//     DWORD blockSize
//     blockSize bytes            <- ALWAYS skippable
//
// **Every block is sized**, and the engine's own unknown-version path is
// `SetOffSet(GetfTell() + dwSize)`. So all 24 `LOAD_1xx`/`LOAD_2xx` variants of
// SITEMBASIC — plus suit, drug, skillbook, pet, vehicle and the rest — can be
// skipped wholesale. Only `FILE_SBASIC` at the CURRENT version is decoded, and
// any other version of it is skipped rather than guessed at.
//
const LAYOUT = require('../layout-probe/layout.json');
const B = require('./bytecrypt');

const FILE_SBASIC = 1;
const FILE_END_DATA = 0xededed_ed;
const SITEMBASIC_VERSION = 0x0205;    // GLItemBasic.h:1185
const SITEM_VERSION = 0x0104;         // GLItem.h:52
const GLCI_NUM_8CLASS = 16;           // GLCharDefine.h:276
const ENCODE_VER = 0x0200;            // GLItemMan.h:46
// sReqStats is blitted with ReadBuffer, so this is a stride, not a guess.
const SCHARSTATS = LAYOUT.structs.MAPOBJ_SIZES.fields.SCHARSTATS.size;   // 12

class Cursor {
  constructor(buf, at) { this.b = buf; this.p = at; }
  get left() { return this.b.length - this.p; }
  need(n, what) {
    if (n < 0 || this.p + n > this.b.length) {
      throw new Error(`itemdata: ${what} runs past end (need ${n}, have ${this.left})`);
    }
  }
  u32() { this.need(4, 'u32'); const v = this.b.readUInt32LE(this.p); this.p += 4; return v; }
  u16() { this.need(2, 'u16'); const v = this.b.readUInt16LE(this.p); this.p += 2; return v; }
  f32() { this.need(4, 'f32'); const v = this.b.readFloatLE(this.p); this.p += 4; return v; }
  skip(n, what) { this.need(n, what); this.p += n; }
  /**
   * CSerialFile std::string: [u32 byteCount][bytes], count INCLUDES the NUL.
   * The same convention as `.wld0`, and the opposite of the `DxFrame` strings
   * in map objects — worth keeping straight.
   */
  str() {
    const n = this.u32();
    if (n === 0 || n > 4096) throw new Error(`itemdata: bad string length ${n}`);
    const raw = this.b.subarray(this.p, this.p + n);
    this.need(n, 'string');
    this.p += n;
    const nul = raw.indexOf(0);
    return raw.toString('latin1', 0, nul === -1 ? n : nul);
  }
}

/**
 * `SITEMBASIC::LOAD` at VERSION 0x0205. Only decoded far enough to reach the two
 * fields that matter here; everything past `strComment` is left alone because
 * the enclosing block size carries us out.
 */
function readBasic(c) {
  const nativeId = c.u32();
  const groupId = c.u32();
  c.u32();                       // emLevel
  const itemType = c.u32();
  const name = c.str();
  c.f32();                       // fExpMultiple
  c.skip(2 * 2, 'grades');       // wGradeAttack, wGradeDefense
  const flags = c.u32();
  const buyPrice = c.u32();      // dwBuyPrice — the NPC shop base price
  c.skip(4, 'sellPrice');        // dwSellPrice
  c.skip(2 * 5, 'reserved');     // wReserved1..5
  c.skip(4 * 2, 'req class/school');
  c.skip(SCHARSTATS, 'sReqStats');
  c.skip(2 * 4, 'req levels/PA/SA');
  c.u32();                       // emReqBright
  c.u32();                       // dwCoolTime
  c.u32();                       // emCoolType
  const invenX = c.u16();
  const invenY = c.u16();
  const iconId = c.u32();
  const fieldFile = c.str();
  const inventoryFile = c.str();
  const targBodyEffect = c.str();
  const targetEffect = c.str();
  const selfBodyEffect = c.str();
  const wearingRight = [];
  for (let i = 0; i < GLCI_NUM_8CLASS; i++) wearingRight.push(c.str());
  const wearingLeft = [];
  for (let i = 0; i < GLCI_NUM_8CLASS; i++) wearingLeft.push(c.str());
  const petWearingFile = c.str();

  return { nativeId, groupId, itemType, name, flags, buyPrice, invenX, invenY, iconId,
           fieldFile, inventoryFile, targBodyEffect, targetEffect,
           selfBodyEffect, wearingRight, wearingLeft, petWearingFile };
}

/**
 * One SITEM record.
 *
 * Only the FIRST block is decoded — `FILE_SBASIC`, which is always written
 * first and is the only one carrying icon and texture names. The record is then
 * resynchronised by scanning for the `FILE_END_DATA` sentinel rather than
 * walking the remaining blocks.
 *
 * That is a deliberate choice, not laziness. Block framing is **not uniform**:
 * types 1..6 have `[version][size]` read by the dispatcher and are skippable,
 * but types 7+ (box, random box, pet, vehicle, rv card, self-buff, …) delegate
 * straight to their own `LOAD`, which frames itself however it likes. Walking
 * them all would mean porting nine more loaders to reach data this tool does
 * not want.
 *
 * The sentinel could in principle occur inside payload bytes, so the resync is
 * verified rather than trusted: the next record must start with a plausible
 * `SITEM` version and its first block must be `FILE_SBASIC`. If it does not,
 * the scan continues to the next candidate.
 */
function readItem(c, stats) {
  const recordVersion = c.u32();
  if (recordVersion > SITEM_VERSION) {
    throw new Error(`itemdata: record version 0x${recordVersion.toString(16)} is newer than 0x${SITEM_VERSION.toString(16)}`);
  }

  let basic = null;
  const blockType = c.u32();
  if (blockType === FILE_SBASIC) {
    const blockVersion = c.u32();
    const blockSize = c.u32();
    const start = c.p;
    c.need(blockSize, 'SBASIC block');
    if (blockVersion === SITEMBASIC_VERSION) {
      try {
        basic = readBasic(new Cursor(c.b, start));
      } catch (err) {
        stats.badBasic++;
      }
    } else {
      // An older SITEMBASIC. The engine keeps 24 LOAD_* variants for these;
      // skip rather than guess, and count them so the gap stays visible.
      stats.oldBasicVersions.set(blockVersion,
        (stats.oldBasicVersions.get(blockVersion) || 0) + 1);
    }
    c.p = start + blockSize;
  } else if (blockType === FILE_END_DATA) {
    return { recordVersion, basic: null };
  } else {
    stats.unexpectedFirstBlock++;
  }

  c.p = findRecordEnd(c, stats);
  return { recordVersion, basic };
}

/** Offset just past this record's FILE_END_DATA, verified against what follows. */
function findRecordEnd(c, stats) {
  const b = c.b;
  // Byte-wise, NOT in 4-byte steps. The stream carries length-prefixed strings
  // of arbitrary length, so nothing after the first one is DWORD-aligned; an
  // aligned scan silently steps over most terminators and swallows whole
  // records. That failure is quiet — it yields fewer items, all of them valid.
  for (let p = c.p; p + 4 <= b.length; p++) {
    if (b.readUInt32LE(p) !== FILE_END_DATA) continue;
    const after = p + 4;
    // Last record in the file: nothing follows to verify against.
    if (after + 8 > b.length) return after;
    const nextVersion = b.readUInt32LE(after);
    const nextType = b.readUInt32LE(after + 4);
    if (nextVersion <= SITEM_VERSION && nextVersion > 0 && nextType === FILE_SBASIC) {
      return after;
    }
    stats.sentinelFalsePositives++;
  }
  throw new Error('itemdata: no record terminator found');
}

/**
 * Parse an item table.
 * @param {Buffer} raw the file bytes (already RCC-decrypted)
 * @returns {{version, count, items, stats}}
 */
function parse(raw) {
  const head = B.readHeader(raw);
  if (!head || head.type !== 'GLITEM') {
    throw new Error(`not a GLITEM file (type "${head && head.type}")`);
  }
  const body = head.version >= ENCODE_VER
    ? B.decode(Buffer.from(raw), 'EMBYTECRYPT_ITEM', head.bodyOffset)
    : raw;

  const c = new Cursor(body, head.bodyOffset);
  const count = c.u32();
  if (count > 200000) throw new Error(`itemdata: implausible item count ${count}`);

  const stats = { badBasic: 0, failedItems: 0, unexpectedFirstBlock: 0,
                  sentinelFalsePositives: 0, oldBasicVersions: new Map() };
  const items = [];
  for (let i = 0; i < count; i++) {
    try {
      items.push(readItem(c, stats));
    } catch (err) {
      stats.failedItems++;
      stats.firstError = stats.firstError || `item ${i}: ${err.message}`;
      break;                      // no resync point once a record desyncs
    }
  }

  return { version: head.version, count, items, stats, bytesRead: c.p,
           fileSize: body.length };
}

/** Every texture an item names, across icon sheet and equipped-model files. */
function textures(item) {
  const out = [];
  const b = item.basic;
  if (!b) return out;
  for (const s of [b.inventoryFile, b.fieldFile, b.petWearingFile,
                   ...b.wearingRight, ...b.wearingLeft]) {
    if (s) out.push(s);
  }
  return out;
}

module.exports = { parse, readItem, readBasic, textures, Cursor,
                   FILE_SBASIC, FILE_END_DATA, SITEMBASIC_VERSION, SITEM_VERSION,
                   SCHARSTATS };
