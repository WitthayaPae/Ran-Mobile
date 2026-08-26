'use strict';
//
// GLQUEST decoder — the static quest TEXT table the mobile quest log needs.
//
// Quest definitions ship as one `.qst` file per quest inside `Quest.rcc`, listed
// by `quest.lst` ("QUEST <id> <file> //<name>"). Each `.qst` is a CSerialFile:
//   [128-byte type string "default"][u32 header version=0][ body @132 ]
// The BODY is byte-substituted with EMBYTECRYPT_OLD (CSerialFile::read ->
// BYTECRYPT::byte_decode, SerialFile.cpp:233; the file is opened with
// emCRYPT=EMBYTECRYPT_OLD by GLQUEST::LOAD, GLQuest.cpp:1531). The substitution
// is stateless per byte, so decoding the whole [132..EOF] region at once equals
// the engine's per-read decode. m_DefaultOffSet is 132 (README, navmesh §), so
// the type string + header-version dwords are PLAINTEXT and the body starts at 132.
//
// The body is a `GLQUEST` serialised through `basestream` (little-endian, the same
// primitive widths as CByteStream, ByteStream.h): DWORD/int/float=4, WORD/short=2,
// BYTE/bool=1, __int64=8, std::string = [u32 len INCLUDING NUL][len bytes]. This
// mirrors GLQUEST::LOAD / GLQUEST_STEP::LOAD / GLQUEST_START::LOAD
// (GLQuest.cpp, GLQuestStep.cpp, GLQuestStart.cpp).
//
// EVERYTHING here is measured, not assumed:
//  - shipped `.qst` versions are 0x01,0x05,0x08,0x09,0x10,0x11,0x12,0x14,0x15,0x16
//    (a survey of all 831 files); the LOAD_00XX labels are HEX, so e.g. version
//    0x16 -> LOAD_0016. No 0x200+ ships, so only the "old" loaders are needed.
//  - blitted gift-item vectors read `sizeof(SITEMCUSTOM_xxx)` per element; those
//    sizes come from the layout probe (SITEMCUSTOM_103=40, _104=56, _105=48,
//    _106=48, _108=56, _109=56, _110=64, _111=64, _112=72, _QUEST_115=80,
//    _QUEST_116=88) — never hand-summed, because __time64_t forces 8-byte
//    alignment and padding.
//  - GLQUEST_START is `[u32 ver][u32 size][body]`; EndBlock writes the exact body
//    size (SerialFile.cpp:200), so it is size-skipped losslessly regardless of
//    its version.
//
// The proof the strides are right is the same as everywhere in this pipeline:
// every field is variable-length, so a wrong stride cannot reach EOF — and the
// extractor asserts each file is consumed to its exact final byte.
//
// Strings are kept RAW cp874 (latin1 1:1), decoded at runtime by
// RanText.DecodeCp874, exactly as itemdb.js does.

// --- layout-probe struct sizes (blitted vector element strides) --------------
const ITEMCUSTOM = {
  103: 40, 104: 56, 105: 48, 106: 48, 108: 56, 109: 56,
  110: 64, 111: 64, 112: 72, 115: 80, 116: 88, 119: 152,
};

const UINT_MAX = 0xffffffff;
const USHRT_MAX = 0xffff;

// A basestream-compatible little-endian reader over the decoded body.
class Reader {
  constructor(buf) { this.b = buf; this.p = 0; this.err = false; }
  get remaining() { return this.b.length - this.p; }
  _need(n) { if (this.p + n > this.b.length) { this.err = true; return false; } return true; }
  u8() { if (!this._need(1)) return 0; return this.b[this.p++]; }
  bool() { return this.u8() !== 0; }
  u16() { if (!this._need(2)) return 0; const v = this.b.readUInt16LE(this.p); this.p += 2; return v; }
  i16() { if (!this._need(2)) return 0; const v = this.b.readInt16LE(this.p); this.p += 2; return v; }
  u32() { if (!this._need(4)) return 0; const v = this.b.readUInt32LE(this.p); this.p += 4; return v; }
  i32() { if (!this._need(4)) return 0; const v = this.b.readInt32LE(this.p); this.p += 4; return v; }
  f32() { if (!this._need(4)) return 0; const v = this.b.readFloatLE(this.p); this.p += 4; return v; }
  i64() { if (!this._need(8)) return 0; const v = this.b.readBigInt64LE(this.p); this.p += 8; return Number(v); }
  skip(n) { if (!this._need(n)) return; this.p += n; }
  // std::string : [u32 len INCLUDING NUL][len bytes (last is NUL)]  -> raw latin1
  str() {
    const len = this.u32();
    if (len === 0) return '';
    if (!this._need(len)) return '';
    // engine reads `len` bytes then treats as C-string (stops at first NUL).
    let end = this.p;
    const stop = this.p + len;
    while (end < stop && this.b[end] !== 0) end++;
    const s = this.b.toString('latin1', this.p, end);
    this.p += len;
    return s;
  }
  // std::vector<DWORD> : [u32 count][count * u32]
  vecDword() {
    const n = this.u32();
    const out = [];
    for (let i = 0; i < n; i++) out.push(this.u32());
    return out;
  }
  // std::vector<SITEMCUSTOM_xxx> : [u32 count][count * size]; capture id (first u32)
  vecItem(size) {
    const n = this.u32();
    const out = [];
    for (let i = 0; i < n; i++) {
      if (!this._need(size)) break;
      const id = this.b.readUInt32LE(this.p);
      this.p += size;
      out.push({ m: id & 0xffff, s: (id >>> 16) & 0xffff });
    }
    return out;
  }
}

// SNATIVEID.dwID -> {m,s}
const nid = (dw) => ({ m: dw & 0xffff, s: (dw >>> 16) & 0xffff });

// GLQUEST_START::LOAD — [u32 ver][u32 size][body]; size-skip (EndBlock exact).
function skipStartOpt(r) {
  r.u32();                 // version
  const size = r.u32();    // body byte count (EndBlock)
  r.skip(size);
}

// SGENQUESTITEM::LOAD — [u32 ver][u32 nid][u16 num][f32 rate][vec<DWORD> mobs]
function readGenItem(r) {
  r.u32();                       // element version (0x0001)
  const id = r.u32();
  const num = r.u16();
  r.f32();                       // gen rate
  r.vecDword();                  // gen mobs
  return { m: id & 0xffff, s: (id >>> 16) & 0xffff, num };
}

// The MOBGEN_QITEM block: [u32 outerVer][u32 count][count * SGENQUESTITEM]
function readGenItemBlock(r) {
  r.u32();                       // outer SGENQUESTITEM::VERSION
  const n = r.u32();
  const out = [];
  for (let i = 0; i < n; i++) out.push(readGenItem(r));
  return out;
}

// GLQUEST_STEP::LOAD, dispatched on the step version (GLQuestStep.cpp).
// Returns a step object with only the fields the quest log shows.
function readStep(r) {
  const ver = r.u32();
  const step = {};
  const itemSize = {
    1: 103, 2: 104, 3: 105, 4: 106, 5: 106, 6: 108, 7: 109, 8: 110, 9: 111,
    0x10: 112, 0x11: 112, 0x12: 112, 0x13: 115, 0x15: 116,
  }[ver];
  const size = ITEMCUSTOM[itemSize];

  if (ver >= 0x12) {
    // Re-ordered layout (mobkill first), LOAD_0012/0013/0015.
    step.title = r.str();
    step.comment = r.str();
    step.mobKillId = r.u32();
    step.mobKillNum = r.u32();
    step.mobKillText = r.str();
    step.genItems = readGenItemBlock(r);
    step.genText = r.str();
    step.npcTalkId = r.u32();
    step.npcTalkText = r.str();
    step.reachMap = r.u32(); r.u16(); r.u16(); r.u16(); step.reachText = r.str();
    step.level = r.u16();
    step.guardId = r.u32(); step.guardText = r.str();
    step.defMap = r.u32(); r.u16(); r.u16(); r.u16(); step.defTime = r.f32(); step.defText = r.str();
    step.giftItems = r.vecItem(size);
    step.resetItems = r.vecDword();
    r.u32(); r.u16(); r.u16(); r.u32(); // stepMoveMap (nid, x, y, gate)
  } else {
    // Classic layout, LOAD_0001..LOAD_0011.
    step.title = r.str();
    step.comment = r.str();
    step.npcTalkText = r.str();
    step.npcTalkId = r.u32();
    step.genText = r.str();
    step.genItems = readGenItemBlock(r);
    step.mobKillText = r.str();
    step.mobKillId = r.u32();
    step.mobKillNum = r.u32();
    step.guardText = r.str();
    step.guardId = r.u32();
    step.reachText = r.str();
    step.reachMap = r.u32(); r.u16(); r.u16(); r.u16();
    step.defText = r.str();
    step.defMap = r.u32(); r.u16(); r.u16(); r.u16(); step.defTime = r.f32();
    step.level = (ver >= 0x05) ? r.u16() : USHRT_MAX;   // m_wLevel added at v5
    step.resetItems = r.vecDword();
    step.giftItems = r.vecItem(size);
    if (ver >= 0x11) { r.u32(); r.u16(); r.u16(); r.u32(); } // stepMoveMap at v0x11
  }
  return step;
}

// Read the reward block after the steps, for the "classic" quest versions
// (LOAD_0001..LOAD_0012). Order: EXP(32-bit), MONEY, ELEMENT, LIFEPOINT, DISPK,
// SKILLPOINT, STATSPOINT, giftITEM<size>, giftSKILL<DWORD>, [QUEST], [ProgressEvent].
function readRewardClassic(r, ver, itemSize) {
  const g = {};
  g.exp = r.u32();          // dwGiftEXP (32-bit -> llGiftEXP)
  g.money = r.u32();
  g.element = r.u32();
  g.lifePoint = r.u32();
  g.disPk = r.u32();
  g.skillPoint = r.u32();
  g.statPoint = r.u32();
  g.items = r.vecItem(ITEMCUSTOM[itemSize]);
  g.skills = r.vecDword();
  if (ver >= 0x05) g.quest = r.u32();
  if (ver >= 0x12) readProgressEvent(r);
  return g;
}

// Reward block for the "new" quest versions (LOAD_0014..LOAD_0016). Order:
// MONEY, EXP(64-bit), SKILLPOINT, STATSPOINT, LIFEPOINT, DISPK, ELEMENT, QUEST,
// giftITEM<size>, giftSKILL<DWORD>, ProgressEvent.
function readRewardNew(r, itemSize) {
  const g = {};
  g.money = r.u32();
  g.exp = r.i64();          // llGiftEXP via ReadBuffer(8)
  g.skillPoint = r.u32();
  g.statPoint = r.u32();
  g.lifePoint = r.u32();
  g.disPk = r.u32();
  g.element = r.u32();
  g.quest = r.u32();
  g.items = r.vecItem(ITEMCUSTOM[itemSize]);
  g.skills = r.vecDword();
  readProgressEvent(r);
  return g;
}

// GLQUEST_PROGRESS block: bool bUse; if set, 4 DWORD + 3 maps (DWORD,WORD,WORD,DWORD).
function readProgressEvent(r) {
  const use = r.bool();
  if (!use) return;
  r.u32(); r.u32(); r.u32(); r.u32();
  for (let i = 0; i < 3; i++) { r.u32(); r.u16(); r.u16(); r.u32(); }
}

// Parse a decoded GLQUEST body (Reader positioned at the version dword).
function parseQuest(r) {
  const ver = r.u32();
  const q = { ver, steps: [] };

  // Header, per version family.
  if (ver <= 0x12) {
    // LOAD_0001..LOAD_0012: editver, flags, title, comment, [startopt >=5], money...
    q.editVer = r.u32();
    q.flags = r.u32();
    q.title = r.str();
    q.comment = r.str();
    if (ver >= 0x05) skipStartOpt(r);
    q.beginMoney = r.u32();
    q.beginParty = r.u32();
    q.limitTime = r.u32();
    q.limitParty = r.u32();
    q.nonDie = r.bool();
  } else {
    // LOAD_0014..LOAD_0016: editver, title, comment, flags, [area >=0x16], startopt, ...
    q.editVer = r.u32();
    q.title = r.str();
    q.comment = r.str();
    q.flags = r.u32();
    if (ver >= 0x16) q.area = r.u32();
    skipStartOpt(r);
    q.beginParty = r.u32();
    q.beginMoney = r.u32();
    q.limitTime = r.u32();
    q.nonDie = r.bool();
    q.limitParty = r.u32();
  }

  // Steps.
  const nSteps = r.u32();
  for (let i = 0; i < nSteps; i++) q.steps.push(readStep(r));

  // Rewards.
  const questItemSize = {
    1: 103, 5: 106, 8: 109, 9: 110, 0x10: 111, 0x11: 112, 0x12: 112,
    0x14: 112, 0x15: 115, 0x16: 115,
  }[ver];
  if (ver <= 0x12) q.gift = readRewardClassic(r, ver, questItemSize);
  else q.gift = readRewardNew(r, questItemSize);

  return q;
}

// Top-level: decode + parse one raw `.qst` buffer (post-RCC, pre-bytecrypt).
// Returns { quest, bytesRead, bodyLen, eofExact, error }.
function decodeQst(raw, bytecrypt) {
  if (raw.length < 132) return { error: 'too short', eofExact: false };
  const type = raw.toString('latin1', 0, raw.indexOf(0) >= 0 ? raw.indexOf(0) : 8);
  const body = Buffer.from(raw.subarray(132));
  bytecrypt.decode(body, 'EMBYTECRYPT_OLD');
  const r = new Reader(body);
  let quest, error = null;
  try { quest = parseQuest(r); }
  catch (e) { error = e.message; }
  // Trailing zero padding is tolerated (some CSerialFile writers pad).
  let tail = r.p;
  while (tail < body.length && body[tail] === 0) tail++;
  const eofExact = !r.err && !error && tail === body.length;
  return { quest, type, bytesRead: r.p, bodyLen: body.length, eofExact, error };
}

module.exports = { decodeQst, parseQuest, Reader, ITEMCUSTOM, UINT_MAX, USHRT_MAX, nid };
