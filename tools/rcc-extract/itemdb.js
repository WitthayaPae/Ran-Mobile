'use strict';
//
// `itemdb.js` — the item DATABASE the inventory/shop/tooltip UI needs: a real
// display NAME, a description, an icon (atlas + grid cell), item type, grade and
// stack cap, keyed by the (mainId, subId) pair the wire protocol uses.
//
// This sits on top of `itemdata.js`, which already decodes the `item.isf`
// container byte-exact to EOF. Two things that tool deliberately does not do,
// and this one must:
//
//   1. RESOLVE the name. `SITEMBASIC::strName` is NOT a display string — it is a
//      lookup KEY like "IN_000_003" (GLStringTable::DeleteString formats it as
//      `IN_%03d_%03d`). `SITEM::GetName` (GLItem.cpp:773) resolves it through
//      `GLStringTable::GetString(key, ITEM)` and only falls back to the raw key
//      when the table has no entry. `strComment` ("ID_%03d_%03d") is the
//      description key, resolved the same way by `GetComment` (GLItem.cpp:781).
//      The table itself is `ItemStrTable.txt` (GLItemMan.cpp:451) inside
//      GLogic.rcc — a tab-separated `KEY<TAB>VALUE` text, AES-encrypted under a
//      4-byte version prefix (the `gamecrypt` layer; see loadStringTable).
//
//   2. Reach the STACK CAP. `wPileNum` is the per-item pile (stack) limit, and
//      it lives in the `FILE_SDRUG` block, not `FILE_SBASIC` — every item writes
//      an `ITEM::SDRUG` (default wPileNum = 1 for equipment), so it is present
//      for all of them (GLItem.cpp:43, SaveFile). It is BLITTED with ReadBuffer
//      (GLItem.cpp:422), so wPileNum's offset is a stride inside that blob and
//      comes from the layout probe (ITEM_SDRUG), never hand-computed — SDRUG
//      leads with a __time64_t after two DWORDs, forcing 8-byte alignment.
//
// The record walk mirrors `SITEM::SaveFile` (GLItem.cpp:21): a fixed sequence of
// `[type][ver][size]`-framed blocks — SBASIC(1), SSUIT(2), SDRUG(3),
// SSKILLBOOK(4), SGENERATE(6), SGRINDING(5) — then the self-framed types 7+
// (box, pet, vehicle…) that `itemdata.js` warns are NOT uniformly framed. We
// only walk the sized 1..6 range (drug is 3, always reached before the messy
// ones) and then resync on the `0xEDEDEDED` sentinel exactly as `itemdata.js`
// does, verifying the next record before accepting it.
//
const LAYOUT = require('../layout-probe/layout.json');
const B = require('./bytecrypt');
const gamecrypt = require('./gamecrypt.js');
const itemdata = require('./itemdata.js');

const { Cursor, FILE_SBASIC, FILE_END_DATA, SITEMBASIC_VERSION, SITEM_VERSION,
        SCHARSTATS } = itemdata;

const GLCI_NUM_8CLASS = 16;           // GLCharDefine.h:276
const ENCODE_VER = 0x0200;            // GLItemMan.h:46
const FILE_SDRUG = 3;                 // SITEM::FILE_SDRUG (GLItem.h:56)
const SDRUG_VERSION = 0x0102;         // ITEM::SDRUG::VERSION (GLItemDrug.h:53)
// The drug block is blitted; all offsets come from the probe, not
// arithmetic (wCureVolume shares an anonymous union with wArrowNum right
// after wPileNum, GLItemDrug.h:59-65 — see layout-probe/probe.cpp).
const SDRUG_SIZE = LAYOUT.structs.ITEM_SDRUG.size;                       // 32
const WPILENUM_OFF = LAYOUT.structs.ITEM_SDRUG.fields.wPileNum.off;      // 20
const CUREVOLUME_OFF = LAYOUT.structs.ITEM_SDRUG.fields.wCureVolume.off; // 22
// emDrug (EMITEM_DRUG) leads the struct (GLItemDrug.h:51-67), so it lands at
// offset 0 of the blitted blob — but read from the probe rather than assumed,
// same rule as the other two. This is the field
// GLCHARLOGIC::GET_REVIVE_ITEM (GLogixExPC.cpp:4287-4297) tests for
// ITEM_DRUG_CALL_REVIVE (=11) to decide whether a worn neck/ornament item
// shows PC's "revive in place" button, and more broadly which HP/MP/SP/cure
// drug an item is (ITEM_DRUG_HP=1 .. ITEM_DRUG_CP=15, GLItemDef.h:490-515).
const EMDRUG_OFF = LAYOUT.structs.ITEM_SDRUG.fields.emDrug.off;          // 0
// bInstance gates SITEM::ISPILE() (GLItem.h:115/125: bInstance && wPileNum>1),
// which SITEM::GETSELLPRICE checks before prorating the sell price by held
// quantity at all (GLItem.h:135-146) — measured, not assumed true whenever
// wPileNum>1.
const BINSTANCE_OFF = LAYOUT.structs.ITEM_SDRUG.fields.bInstance.off;    // 4

// Blocks framed as [type][ver][size] and thus size-skippable. Types 7+ delegate
// straight to their own LOAD and frame themselves however they like, so the walk
// stops the moment it sees one and hands off to the sentinel resync.
const SAFE_BLOCKS = new Set([1, 2, 3, 4, 5, 6]);

/**
 * `SITEMBASIC::LOAD` at VERSION 0x0205, read far enough to reach `strComment`.
 * Field order is the struct order in GLItemBasic.h:1183; `sReqStats` is a
 * ReadBuffer stride (SCHARSTATS, from the probe). This extends `itemdata.js`'s
 * `readBasic` by also capturing `emLevel` (the item grade/rarity), the two grade
 * caps, and `strComment` — the fields a tooltip needs that the audit did not.
 */
function readBasic(c) {
  const nativeId = c.u32();
  const groupId = c.u32();
  const emLevel = c.u32();       // EMITEMLEVEL: NORMAL/RARE/UNIQUE/INFINITY/FERVOR/LEGENDARY
  const itemType = c.u32();      // EMITEM_TYPE
  const name = c.str();          // strName  -> "IN_mmm_sss" key
  c.f32();                       // fExpMultiple
  const gradeAttack = c.u16();
  const gradeDefense = c.u16();
  const flags = c.u32();
  const buyPrice = c.u32();      // dwBuyPrice  (NPC buys FROM the player at this)
  const sellPrice = c.u32();     // dwSellPrice (NPC sells TO the player at this, the
                                  // per-unit base SITEM::GETSELLPRICE returns —
                                  // GLItemBasic.h:1196-1197, GLItem.h:135-146)
  c.skip(2 * 5, 'reserved');     // wReserved1..5
  c.skip(4 * 2, 'req class/school');
  c.skip(SCHARSTATS, 'sReqStats');
  c.skip(2 * 4, 'req levels/PA/SA');
  c.u32();                       // emReqBright
  c.u32();                       // dwCoolTime
  c.u32();                       // emCoolType
  const invenX = c.u16();
  const invenY = c.u16();
  const iconId = c.u32();        // sICONID: low word = column, high word = row
  const fieldFile = c.str();
  const inventoryFile = c.str(); // the icon ATLAS
  c.str();                       // strTargBodyEffect
  c.str();                       // strTargetEffect
  c.str();                       // strSelfBodyEffect
  for (let i = 0; i < GLCI_NUM_8CLASS; i++) c.str();   // strWearingFileRight[16]
  for (let i = 0; i < GLCI_NUM_8CLASS; i++) c.str();   // strWearingFileLeft[16]
  c.str();                       // strPetWearingFile
  const comment = c.str();       // strComment -> "ID_mmm_sss" key

  return { nativeId, groupId, emLevel, itemType, name, gradeAttack, gradeDefense,
           flags, buyPrice, sellPrice, invenX, invenY, iconId, fieldFile,
           inventoryFile, comment };
}

/** Offset just past this record's FILE_END_DATA, verified against what follows. */
function findRecordEnd(c, stats) {
  const b = c.b;
  // Byte-wise, NOT DWORD-aligned: length-prefixed strings leave nothing aligned
  // after the first one, so a 4-byte stride steps over most terminators. Same
  // trap `itemdata.js` documents.
  for (let p = c.p; p + 4 <= b.length; p++) {
    if (b.readUInt32LE(p) !== FILE_END_DATA) continue;
    const after = p + 4;
    if (after + 8 > b.length) return after;              // last record
    const nextVersion = b.readUInt32LE(after);
    const nextType = b.readUInt32LE(after + 4);
    if (nextVersion <= SITEM_VERSION && nextVersion > 0 && nextType === FILE_SBASIC) {
      return after;
    }
    stats.sentinelFalsePositives++;
  }
  throw new Error('itemdb: no record terminator found');
}

/**
 * One SITEM record. Decodes the SBASIC block and the SDRUG block's `wPileNum`,
 * then resyncs on the sentinel. Returns `{ basic, pile }` with `basic` null for
 * records carrying only an older SBASIC version (counted in stats).
 */
function readItem(c, stats) {
  const recordVersion = c.u32();
  if (recordVersion > SITEM_VERSION) {
    throw new Error(`itemdb: record version 0x${recordVersion.toString(16)} newer than 0x${SITEM_VERSION.toString(16)}`);
  }

  let basic = null;
  let pile = null;
  let cureVolume = null;
  let drug = null;
  let instance = null;

  // Walk the sized blocks (1..6). Every record writes them in a fixed order, so
  // the drug block is always reached before any self-framed type.
  while (true) {
    if (c.left < 4) break;
    const blockType = c.u32();
    if (blockType === FILE_END_DATA) return { basic, pile, cureVolume, drug, instance };
    if (!SAFE_BLOCKS.has(blockType)) { c.p -= 4; break; }   // hit a type 7+; hand off

    const blockVersion = c.u32();
    const blockSize = c.u32();
    const start = c.p;
    c.need(blockSize, 'sized block');

    if (blockType === FILE_SBASIC) {
      if (blockVersion === SITEMBASIC_VERSION) {
        try { basic = readBasic(new Cursor(c.b, start)); }
        catch (err) { stats.badBasic++; }
      } else {
        stats.oldBasicVersions.set(blockVersion,
          (stats.oldBasicVersions.get(blockVersion) || 0) + 1);
      }
    } else if (blockType === FILE_SDRUG && blockVersion === SDRUG_VERSION
               && blockSize === SDRUG_SIZE) {
      // wPileNum and wCureVolume are WORDs at probe-measured offsets inside the
      // blitted blob. wCureVolume is SITEM::GETAPPLYNUM's divisor for ammo-like
      // item types (GLItem.cpp:717-746) — see readItem's caller for how it is
      // used to reproduce GETSELLPRICE's proration by held quantity.
      pile = c.b.readUInt16LE(start + WPILENUM_OFF);
      cureVolume = c.b.readUInt16LE(start + CUREVOLUME_OFF);
      // emDrug (EMITEM_DRUG) is a DWORD-sized enum at offset 0 of the blob
      // (probe-measured, not assumed). GLCHARLOGIC::GET_REVIVE_ITEM
      // (GLogixExPC.cpp:4291/4294) tests this for ITEM_DRUG_CALL_REVIVE (11)
      // against whatever is worn in SLOT_NECK/SLOT_ORNAMENT.
      drug = c.b.readUInt32LE(start + EMDRUG_OFF);
      // bInstance is a BOOL (4-byte) gating ISPILE() — see BINSTANCE_OFF doc.
      instance = c.b.readUInt32LE(start + BINSTANCE_OFF) !== 0;
    }

    c.p = start + blockSize;
  }

  c.p = findRecordEnd(c, stats);
  return { basic, pile, cureVolume, drug, instance };
}

/**
 * Parse an item table (`item.isf` / `item1.isf`), already RCC-decrypted.
 * @param {Buffer} raw
 * @returns {{version, count, items, stats, bytesRead, fileSize}}
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
  if (count > 200000) throw new Error(`itemdb: implausible item count ${count}`);

  const stats = { badBasic: 0, failedItems: 0, sentinelFalsePositives: 0,
                  oldBasicVersions: new Map() };
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

  return { version: head.version, count, items, stats,
           bytesRead: c.p, fileSize: body.length };
}

/**
 * Decode `ItemStrTable.txt` into a `Map<key, rawCp874Value>`.
 *
 * The container is the `gamecrypt` AES layer: a 4-byte version prefix then
 * AES-256-ECB (StringMemory::Open, StringMemory.cpp:90-106). Once decoded it is
 * the plain text `GLStringTable::LOADFILE` reads (GLStringTable.cpp:99): each
 * line is `KEY<TAB>VALUE`, with a leading `/` starting a comment
 * (`SpanExcluding("/")`), tokenised on TAB. Duplicates keep the first, matching
 * the `std::map::insert` there.
 *
 * VALUE bytes are left as RAW cp874 (latin1 1:1) so the runtime can decode them
 * with `RanText.DecodeCp874` exactly as the PC renders through code page 874.
 *
 * @param {Buffer} raw the RCC-decrypted `ItemStrTable.txt`
 * @returns {Map<string,string>}
 */
function loadStringTable(raw) {
  const dec = gamecrypt.decode(raw);          // strip version + AES, or passthrough
  const map = new Map();
  for (const line of dec.toString('latin1').split(/\r?\n/)) {
    const noComment = line.split('/')[0];
    // CString::Tokenize skips leading/consecutive TAB delimiters (strtok-style),
    // so collapse empty tokens: key is the first, value the next (or "").
    const toks = noComment.split('\t').filter((t) => t !== '');
    if (toks.length === 0) continue;
    const key = toks[0];
    const val = toks.length > 1 ? toks[1] : '';
    if (!map.has(key)) map.set(key, val);
  }
  return map;
}

module.exports = { parse, readItem, readBasic, findRecordEnd, loadStringTable,
                   FILE_SDRUG, SDRUG_VERSION, SDRUG_SIZE, WPILENUM_OFF,
                   CUREVOLUME_OFF, EMDRUG_OFF, BINSTANCE_OFF,
                   ENCODE_VER, GLCI_NUM_8CLASS };
