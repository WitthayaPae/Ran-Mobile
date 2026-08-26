'use strict';
//
// `.ntk` — the per-NPC dialogue tree (`CNpcDialogueSet`, Lib_Client/NpcTalk).
//
// This is what the PC's NPC talk window is built from: the greeting line an NPC
// speaks, and which service options it offers (shop / save-point / quest / cure
// / storage / ...). The mobile NPC dialog needs exactly two things out of it —
// the real greeting text and the true set of options — so a tapped NPC shows its
// authored script instead of a generic stand-in.
//
// CONTAINER. `.ntk` ships in NpcTalk.rcc, which is plaintext after the RCC XOR
// pass (no gamecrypt/AES layer — only GLogic.rcc has that). The file is opened as
// a CSerialMemory: OpenFile() reads a 128-byte type string ("default") + a DWORD
// FileID and leaves the cursor at byte 132 (SerialMemory.cpp:138-146). So the
// BODY starts at 132:
//
//   WORD  version                       @132, PLAINTEXT (read before SetEncodeType)
//   <body>                              @134, EMBYTECRYPT_NPCTALK when version>=0x0200
//
// The encode gate is version>=ENCODE_VER(0x0200) (NpcDialogueSet.cpp:829). Shipped
// data: 651 files 0x0203, 11 files 0x0013 (plaintext), 1 file 0x0001 (plaintext).
//
// TREE (all cites in Lib_Client/NpcTalk):
//   CNpcDialogueSet  [22 bool flags per LOAD_0203][int nCount][nCount × Dialogue]
//     CNpcDialogue   WORD ver; DWORD nid; DWORD caseCount; caseCount × Case
//                    (ver 0x0001 has a different, positive/negative-case shape)
//       Case         WORD ver; string basicTalk; BOOL?→Condition; BOOL?→TalkControl
//         TalkControl WORD ver; int nTalk; nTalk × Talk
//           Talk     WORD ver; nid; string talk; int action; DWORD actionNo,
//                    param1, param2; 5+5 SNPC_ITEM (blitted); quest/random ids;
//                    randomTime; string busFile; BOOL?→Condition
//           Condition WORD ver; version-gated scalar+vector block
//
// Two CSerialFile conventions the walk depends on:
//   - std::string  = [DWORD byteCount][byteCount bytes], count INCLUDES the NUL
//     (SerialFile.cpp:593). The string is the bytes up to the first NUL.
//   - bool = 1 byte, BOOL/int/DWORD = 4, WORD = 2 (SerialFile.cpp operator>>).
// SNPC_ITEM / SSkillCondition / SNPC_BUFF are blitted 8-byte strides — measured by
// the layout probe (NpcTalkData.h), never summed.
//
// The greeting is the basic talk of dialogue **NID 1**, the fixed entry node
// (NPCDialoguePage.cpp:45 `nSTARTINDEX = 1`), specifically its non-conditional
// case (FindNonCondition) — the same default the engine falls back to when no
// character-gated case matches.
//
// Correctness proof: every file is consumed to its exact final byte. A wrong
// stride still "parses"; only EOF-exactness across all 663 files shows the walk
// is right.

const bytecrypt = require('./bytecrypt.js');
const LAYOUT = require('../layout-probe/layout.json');

const NPCTALK_TABLE = 'EMBYTECRYPT_NPCTALK';
const HEADER_TYPE_SIZE = 128;
const BODY_OFFSET = 132;          // 128 type + 4 FileID (CSerialMemory::ReadFileType)
const ENCODE_VER = 0x0200;        // NpcDialogueSet.h ENCODE_VER

// Blitted strides — from the probe, not arithmetic.
const SZ_SNPC_ITEM = LAYOUT.allStructs.SNPC_ITEM.size;         // 8
const SZ_SSKILLCOND = LAYOUT.allStructs.SSkillCondition.size;  // 8
const SZ_SNPC_BUFF = LAYOUT.allStructs.SNPC_BUFF.size;         // 8

const UINT_MAX = 0xffffffff;

// SNpcTalk::EM_TALK — the action kind (NpcTalk.h:29).
const EM_DO_NOTHING = 0, EM_PAGE_MOVE = 1, EM_BASIC = 2, EM_QUEST_START = 3, EM_QUEST_STEP = 4;
// SNpcTalk::EM_BASIC — the m_dwACTION_NO when nACTION==EM_BASIC (NpcTalk.h:40).
const EM_STORAGE = 0, EM_MARKET = 1, EM_CURE = 2, EM_STARTPOINT = 3, EM_CHAR_RESET = 4, EM_ITEM_TRADE = 5;

// Per-answer icon index — the SAME switch the PC runs in
// CDialogueWindow::LoadButtonTalk (DialogueWindow.cpp:728-784). The index keys
// into the 10-entry DIALOGUE_ICON_* keyword table (DialogueWindow.cpp:667):
//   0 HEAL  1 TRADE  2 STARTPOINT  3 STORAGE  4 MARKET  5 RESETPOINT
//   6 BUS   7 QUEST_START  8 QUEST_ING  9 TALK
// Note the quirks reproduced verbatim: QUEST_STEP uses QUEST_START(7) not
// QUEST_ING(8); and the EM_BASIC market/storage/trade/resetpoint cases are
// COMMENTED OUT in this build, so those basics fall to TALK(9) — only CURE(0)
// and STARTPOINT(2) get their own icon. DO_NOTHING keeps the initial 0 (HEAL).
function iconFor(t) {
  let ic = 0; // initial nImageType (LoadButtonTalk:728)
  switch (t.action) {
    case EM_QUEST_START: ic = 7; break;   // ICON_QUEST_START
    case EM_QUEST_STEP:  ic = 7; break;   // ICON_QUEST_START (not QUEST_ING)
    case EM_BASIC:
      if (t.actionNo === EM_CURE) ic = 0;            // ICON_HEAL
      else if (t.actionNo === EM_STARTPOINT) ic = 2; // ICON_STARTPOINT
      else ic = 9;                                    // default → ICON_TALK
      break;
    case EM_PAGE_MOVE: ic = 9; break;     // ICON_TALK
    default: ic = 0; break;               // EM_DO_NOTHING → initial 0
  }
  return ic >= 10 ? 9 : ic;              // clamp, mirroring LoadButtonTalk:784
}

// One answer entry as staged for the runtime. Fields kept terse for JSON size:
//   t  answer text, raw cp874 (latin1 1:1, decoded at display)
//   k  action KIND  (EM_TALK: 0 nothing,1 pageMove,2 basic,3 questStart,4 questStep)
//   b  EM_BASIC sub-action when k==2 (0 storage,1 market,2 cure,3 startpoint,...), else -1
//   tg target PAGE nid to LoadNode for k in {1,3,4} (m_dwACTION_NO), else -1
//   ic per-line icon index (iconFor, above)
//   gid talk GLOB id (AssignTalkGlobID order) — the dwTalkID a quest request carries
//   q  quest id: for quest-start (best-effort, questStartIds[0] else param1), and for
//      quest-step m_dwACTION_PARAM1 — CDialogueWindow::TranslateQuestStepMessage
//      (DialogueWindow.cpp:629) passes it straight as dwQUESTID to
//      ReqQuestStepNpcTalk. Else -1. (m_dwACTION_PARAM2, the quest STEP, is read by
//      that same call but the engine's own SNET_QUEST_STEP_NPC_TALK never carries it
//      — GLContrlQuestMsg.h has no step field on that struct — so it is not staged.)
//   gv give-back item id (SNATIVEID-packed DWORD, low word main/high word sub) —
//      only when b==5 (EM_ITEM_TRADE): m_dwACTION_PARAM2, "2. B, STEP" (NpcTalk.h:98)
//      — the item GLCharacter::ReqItemTrade sends as dwB_NID (GLCharactorReq.cpp:
//      4244). Else -1.
//   ni need-item list [{id,n},...] — only when b==5: m_sNeedItem, up to 5 slots the
//      player must GIVE (SNPC_ITEM id+qty; id UINT_MAX slots dropped — "unused",
//      NpcTalkData.h:17 / ReqItemTrade's own UINT_MAX skip, GLCharactorReq.cpp:4225).
//      Omitted (absent key) when b!=5.
function makeAnswer(t, gid) {
  const textBuf = t.talk || Buffer.alloc(0);
  const textStr = Buffer.isBuffer(textBuf) ? textBuf.toString('latin1') : String(textBuf);
  const isBasic = t.action === EM_BASIC;
  const isMove = t.action === EM_PAGE_MOVE || t.action === EM_QUEST_START || t.action === EM_QUEST_STEP;
  let q = -1;
  if (t.action === EM_QUEST_START) {
    if (t.questStartIds && t.questStartIds.length && t.questStartIds[0] !== UINT_MAX) q = t.questStartIds[0];
    else if (t.param1 !== undefined && t.param1 !== UINT_MAX) q = t.param1 >>> 0;
  } else if (t.action === EM_QUEST_STEP) {
    if (t.param1 !== undefined && t.param1 !== UINT_MAX) q = t.param1 >>> 0;
  }
  const isTrade = isBasic && t.actionNo === EM_ITEM_TRADE;
  const out = {
    t: textStr,
    k: t.action,
    b: isBasic ? (t.actionNo >>> 0) : -1,
    tg: isMove ? (t.actionNo >>> 0) : -1,
    ic: iconFor(t),
    gid: gid >>> 0,
    q,
  };
  if (isTrade) {
    out.gv = (t.param2 !== undefined && t.param2 !== UINT_MAX) ? (t.param2 >>> 0) : -1;
    out.ni = (t.needItems || [])
      .filter((it) => it.id !== UINT_MAX)
      .map((it) => ({ id: it.id >>> 0, n: it.qty }));
  }
  return out;
}

class Cursor {
  constructor(buf, at) { this.b = buf; this.p = at; this.err = false; }
  get left() { return this.b.length - this.p; }
  _need(n) {
    if (this.p + n > this.b.length) { this.err = true; throw new Error(`ntk: read past end (need ${n}, have ${this.left})`); }
  }
  u8() { this._need(1); return this.b[this.p++]; }
  bool() { return this.u8() !== 0; }        // C++ `bool`  = 1 byte
  bool4() { return this.u32() !== 0; }       // C++ `BOOL`  = 4 bytes (int)
  u16() { this._need(2); const v = this.b.readUInt16LE(this.p); this.p += 2; return v; }
  i32() { this._need(4); const v = this.b.readInt32LE(this.p); this.p += 4; return v; }
  u32() { this._need(4); const v = this.b.readUInt32LE(this.p); this.p += 4; return v; }
  skip(n) { this._need(n); this.p += n; }
  // CSerialFile std::string: length INCLUDES the NUL; value is bytes up to NUL.
  str() {
    const n = this.u32();
    if (n > 1 << 20) throw new Error(`ntk: implausible string length ${n}`);
    this._need(n);
    const raw = this.b.slice(this.p, this.p + n);
    this.p += n;
    let z = raw.indexOf(0);
    return { raw, text: (z < 0 ? raw : raw.slice(0, z)) };  // raw kept for cp874 decode downstream
  }
}

// ---- SNpcTalkCondition::LOAD (NpcTalkCondition.cpp) --------------------------
function loadCondition(c) {
  const ver = c.u16();
  const items = (n) => c.skip(n);
  const vecItemById = () => { const n = c.i32(); c.skip(n * 4); };      // reads DWORD dwItemID each
  const vecBlit = (stride) => { const n = c.i32(); c.skip(n * stride); };
  const vecDword = () => { const n = c.i32(); c.skip(n * 4); };
  switch (ver) {
    case 0x0001: {
      c.i32(); c.u16(); c.u32();          // bLevel, wLevel, signLevel
      c.i32(); c.u32();                    // bClass, dwClass
      c.i32(); c.u32();                    // bElemental, nElemental (first pair)
      c.i32(); c.u32(); c.u32();           // bElemental, nElemental, signElemental
      c.i32(); c.u32(); c.u32();           // bActionPoint, nActionPoint, signActionPoint
      c.i32(); vecItemById();              // bHaveItem, haveItem (DWORD id each)
      c.i32(); vecBlit(SZ_SSKILLCOND);     // bLearnSkill, learnSkill
      c.i32(); c.u32(); c.u32();           // bTime, timeBegin, timeEnd
      break;
    }
    case 0x0002: {
      c.u32(); c.u16();                    // signLevel, wLevel
      c.u32();                             // dwClass
      c.u32(); c.u32();                    // signElemental, nElemental
      c.u32(); c.u32();                    // signActionPoint, nActionPoint
      c.i32(); c.u32(); c.u32();           // bTime, timeBegin, timeEnd
      c.u32(); c.u32();                    // questNid, questStep
      vecItemById();                       // haveItem
      vecBlit(SZ_SSKILLCOND);              // learnSkill
      break;
    }
    case 0x0003: {
      c.u32(); c.u16(); c.u32();           // signLevel, wLevel, dwClass
      c.u32(); c.u32();                    // signElemental, nElemental
      c.u32(); c.u32();                    // signActionPoint, nActionPoint
      c.i32(); c.u32(); c.u32();           // bTime, timeBegin, timeEnd
      c.u32(); c.u32();                    // questNid, questStep
      vecItemById();                       // haveItem
      vecBlit(SZ_SSKILLCOND);              // learnSkill
      vecDword();                          // disSkill
      vecDword();                          // disQuest
      c.u32(); c.u32();                    // money, partymen
      break;
    }
    case 0x0004: {
      c.u32(); c.u16(); c.u32();
      c.u32(); c.u32();
      c.u32(); c.u32();
      c.i32(); c.u32(); c.u32();
      c.u32(); c.u32();
      vecItemById();
      vecBlit(SZ_SSKILLCOND);
      vecDword();                          // completeQuest
      vecDword();                          // disSkill
      vecDword();                          // disQuest
      c.u32(); c.u32();
      break;
    }
    case 0x0005:
    case 0x0006: {
      c.u32(); c.u16();                    // signLevel, wLevel
      c.u32(); c.u16();                    // dwClass, wSchool
      c.u32(); c.u32();                    // signElemental, nElemental
      c.u32(); c.u32();                    // signActionPoint, nActionPoint
      c.i32(); c.u32(); c.u32();           // bTime, timeBegin, timeEnd
      c.u32(); c.u32();                    // questNid, questStep
      vecItemById();
      vecBlit(SZ_SSKILLCOND);
      vecDword();                          // completeQuest
      vecDword();                          // disSkill
      vecDword();                          // disQuest
      c.u32(); c.u32();
      break;
    }
    case 0x0007: {
      c.u32(); c.u16();
      c.u32(); c.u16();
      c.u32(); c.u32();
      c.u32(); c.u32();
      c.i32(); c.u32(); c.u32();
      c.u32(); c.u32();                    // money, partymen
      c.u32(); c.u32();                    // questNid, questStep
      vecDword();                          // completeQuest
      vecItemById();                       // haveItem
      vecBlit(SZ_SSKILLCOND);              // learnSkill
      vecDword();                          // disQuest
      vecDword();                          // disSkill
      break;
    }
    case 0x0008:
    case 0x0009:
    case 0x0010: {
      c.bool();                            // m_bEnable is `bool` (1 byte)
      c.u32(); c.u16();                    // signLevel, wLevel
      c.u32(); c.u16();                    // dwClass, wSchool
      c.u32(); c.u32();                    // signElemental, nElemental
      c.u32(); c.u32();                    // signActionPoint, nActionPoint
      c.i32(); c.u32(); c.u32();           // bTime, timeBegin, timeEnd
      c.u32(); c.u32();                    // money, partymen
      c.u32(); c.u32();                    // questNid, questStep
      vecDword();                          // completeQuest
      if (ver === 0x0008) vecItemById();   // haveItem: DWORD id each (0008)
      else vecBlit(SZ_SNPC_ITEM);          // haveItem: blitted SNPC_ITEM (0009/0010)
      vecBlit(SZ_SSKILLCOND);              // learnSkill
      vecDword();                          // disQuest
      vecDword();                          // disSkill
      if (ver === 0x0010) vecBlit(SZ_SNPC_BUFF);  // buff (0x0010 only)
      break;
    }
    default:
      throw new Error(`ntk: unknown SNpcTalkCondition version 0x${ver.toString(16)}`);
  }
}

// ---- SNpcTalk::LOAD (NpcTalk.cpp) -------------------------------------------
// Returns { nid, action, actionNo, param1, param2, talk, questStartIds }.
function loadTalk(c) {
  const ver = c.u16();
  // needItems: EM_ITEM_TRADE's "A" side — up to 5 {id,qty} the player must GIVE
  // (SNPC_ITEM, blitted 8-byte stride: DWORD dwItemID @0, WORD wItemNum @4, 2
  // pad). A slot with id===UINT_MAX is unused (SNPC_ITEM's own default ctor,
  // NpcTalkData.h:17) — mirrors GLCharacter::ReqItemTrade's own loop, which
  // treats dwItemID==UINT_MAX as "no item this slot" (GLCharactorReq.cpp:4225).
  const t = { nid: 0, action: EM_DO_NOTHING, actionNo: UINT_MAX, param1: UINT_MAX, param2: UINT_MAX, talk: null, questStartIds: [], needItems: null, ver };
  const readCondFlag = () => { if (c.bool4()) loadCondition(c); };   // BOOL bExist
  // Blitted SNPC_ITEM[5]: capture id+qty (id UINT_MAX = unused slot, qty
  // meaningless then). Only 0x0200 ships (measured: every referenced .ntk's
  // talks are ver 0x0200 — 6690/6690), so this is the path that matters; older
  // shapes below are read structurally (never skipped past) but their item
  // arrays are id-only in the file (no per-item quantity was ever written by
  // those save versions) so quantity defaults to SNPC_ITEM's ctor value (1).
  const readNeed5 = () => {
    const items = [];
    for (let i = 0; i < 5; i++) {
      const id = c.u32(); const qty = c.u16(); c.skip(2); // 2 pad bytes (stride 8)
      items.push({ id, qty });
    }
    t.needItems = items;
  };
  const readItemIds5 = () => {
    const items = [];
    for (let i = 0; i < 5; i++) items.push({ id: c.u32(), qty: 1 });
    t.needItems = items;
  };
  const readQuest5 = () => { for (let i = 0; i < 5; i++) t.questStartIds.push(c.u32()); };
  const readRandom5 = () => c.skip(5 * 4);

  switch (ver) {
    case 0x0001:
      t.nid = c.u32(); t.talk = c.str().text; t.action = c.i32(); t.actionNo = c.u32();
      break;
    case 0x0002:
      t.nid = c.u32(); t.talk = c.str().text; t.action = c.i32(); t.actionNo = c.u32();
      t.param1 = c.u32();                 // (engine then overwrites param1=UINT_MAX)
      readCondFlag();
      break;
    case 0x0003:
      t.nid = c.u32(); t.talk = c.str().text; t.action = c.i32(); t.actionNo = c.u32();
      t.param1 = c.u32(); t.param2 = c.u32();
      readCondFlag();
      break;
    case 0x0004:
      t.nid = c.u32(); t.talk = c.str().text; t.action = c.i32(); t.actionNo = c.u32();
      t.param1 = c.u32(); t.param2 = c.u32();
      c.str();                            // m_strBusFile
      readCondFlag();
      break;
    case 0x0005:
      t.nid = c.u32(); t.talk = c.str().text; t.action = c.i32(); t.actionNo = c.u32();
      if (t.action === EM_BASIC && t.actionNo === 5 /*EM_ITEM_TRADE*/) readItemIds5();
      else t.param1 = c.u32();
      t.param2 = c.u32();
      c.str();                            // m_strBusFile
      readCondFlag();
      break;
    case 0x0006:
      t.nid = c.u32(); t.talk = c.str().text; t.action = c.i32(); t.actionNo = c.u32();
      readItemIds5();                     // 5 × DWORD need-item id
      readQuest5();
      t.param1 = c.u32(); t.param2 = c.u32();
      c.str();                            // m_strBusFile
      readCondFlag();
      break;
    case 0x0007:
      t.nid = c.u32(); t.talk = c.str().text; t.action = c.i32(); t.actionNo = c.u32();
      readItemIds5();
      readQuest5();
      readRandom5();
      c.u32();                            // m_dwRandomTime
      t.param1 = c.u32(); t.param2 = c.u32();
      c.str();                            // m_strBusFile
      readCondFlag();
      break;
    case 0x0008:
      t.nid = c.u32(); t.talk = c.str().text; t.action = c.i32(); t.actionNo = c.u32();
      t.param1 = c.u32(); t.param2 = c.u32();
      readItemIds5();
      readQuest5();
      readRandom5();
      c.u32();                            // randomTime
      c.str();                            // busFile
      readCondFlag();
      break;
    case 0x0009:
    case 0x000A: {
      t.nid = c.u32(); t.talk = c.str().text; t.action = c.i32(); t.actionNo = c.u32();
      t.param1 = c.u32(); t.param2 = c.u32();
      let n = c.u32(); c.skip(n * SZ_SNPC_ITEM);   // need items (blitted, count-prefixed)
      n = c.u32(); c.skip(n * SZ_SNPC_ITEM);       // result items
      readQuest5();
      readRandom5();
      c.u32();                            // randomTime
      c.str();                            // busFile
      if (ver === 0x000A) c.u32();        // m_sHireSummon.dwID
      readCondFlag();
      break;
    }
    case 0x0200:
      t.nid = c.u32(); t.talk = c.str().text; t.action = c.i32(); t.actionNo = c.u32();
      t.param1 = c.u32(); t.param2 = c.u32();
      readNeed5();                        // 5 × SNPC_ITEM need (blitted)
      c.skip(5 * SZ_SNPC_ITEM);           // 5 × SNPC_ITEM result (blitted)
      readQuest5();
      readRandom5();
      c.u32();                            // m_dwRandomTime
      c.str();                            // m_strBusFile
      readCondFlag();
      break;
    default:
      throw new Error(`ntk: unknown SNpcTalk version 0x${ver.toString(16)}`);
  }
  return t;
}

// ---- CNpcTalkControl::LoadFile ----------------------------------------------
function loadTalkControl(c) {
  const ver = c.u16();                    // 0x0001 or 0x0002 (identical bodies)
  if (ver !== 0x0001 && ver !== 0x0002)
    throw new Error(`ntk: unknown CNpcTalkControl version 0x${ver.toString(16)}`);
  const n = c.i32();
  const talks = [];
  for (let i = 0; i < n; i++) talks.push(loadTalk(c));
  return talks;
}

// ---- CNpcDialogueCase::LoadFile ---------------------------------------------
// Returns { basicTalk:{raw,text}, hasCondition, talks:[...] }.
function loadCase(c) {
  const ver = c.u16();
  let basicTalk = { raw: Buffer.alloc(0), text: Buffer.alloc(0) };
  let hasCondition = false, talks = [];
  switch (ver) {
    case 0x0001:
      basicTalk = c.str();
      if (c.bool4()) talks = loadTalkControl(c);
      break;
    case 0x0002:
      if (c.bool4()) { hasCondition = true; loadCondition(c); }
      basicTalk = c.str();
      if (c.bool4()) talks = loadTalkControl(c);
      break;
    case 0x0003:
      basicTalk = c.str();
      if (c.bool4()) { hasCondition = true; loadCondition(c); }
      if (c.bool4()) talks = loadTalkControl(c);
      break;
    default:
      throw new Error(`ntk: unknown CNpcDialogueCase version 0x${ver.toString(16)}`);
  }
  return { basicTalk, hasCondition, talks };
}

// ---- CNpcDialogue::LoadFile -------------------------------------------------
function loadDialogue(c) {
  const ver = c.u16();
  const nid = c.u32();
  const cases = [];
  switch (ver) {
    case 0x0001: {
      // positive/negative shape: [BOOL cond][BOOL pos→case][BOOL neg→case]
      let hasCond = false;
      if (c.bool4()) { hasCond = true; loadCondition(c); }
      if (c.bool4()) { const cs = loadCase(c); if (hasCond) cs.hasCondition = true; cases.push(cs); }
      if (c.bool4()) cases.push(loadCase(c));
      break;
    }
    case 0x0002:
    case 0x0003: {
      const n = c.u32();
      for (let i = 0; i < n; i++) cases.push(loadCase(c));
      break;
    }
    default:
      throw new Error(`ntk: unknown CNpcDialogue version 0x${ver.toString(16)}`);
  }
  return { nid, cases };
}

// Flag field order per set version (NpcDialogueSet.cpp LOAD_00xx / LOAD_02xx).
// Only the versions that ship are enumerated; each name is a field, read as 1 byte.
const FLAGS_0203 = ['storage', 'market', 'cure', 'startPoint', 'charReset', 'itemTrade',
  'busStation', 'clubNew', 'clubRankUp', 'cdCertify', 'commission', 'clubStorage',
  'itemRebuild', 'oddEven', 'recoveryExp', 'randomPage', 'itemSearch', 'attendanceBook',
  'itemMix', 'npcShop', 'npcItemExchange', 'hireSummon'];
const FLAGS_0013 = ['storage', 'market', 'cure', 'startPoint', 'charReset', 'busStation',
  'clubNew', 'clubRankUp', 'clubStorage', 'itemRebuild', 'oddEven', 'recoveryExp',
  'randomPage', 'itemSearch', 'attendanceBook', 'itemMix'];

function emptyFlags() {
  const f = {};
  for (const k of FLAGS_0203) f[k] = false;
  return f;
}

/**
 * Parse a `.ntk` buffer (already RCC-decrypted, still with its 128-byte header).
 * @returns {{version, flags, dialogues, greetingRaw, hasQuest, questStartIds, consumed, eofExact}}
 */
function parse(buf) {
  if (buf.length < BODY_OFFSET + 2) throw new Error('ntk: file too short for header');
  const type = buf.slice(0, HEADER_TYPE_SIZE).toString('latin1').replace(/\0.*$/, '');
  const version = buf.readUInt16LE(BODY_OFFSET);          // WORD, plaintext

  // Body from 134: encoded with NPCTALK only when version >= 0x0200.
  let body = buf;
  if (version >= ENCODE_VER) {
    body = Buffer.from(buf);
    bytecrypt.decode(body, NPCTALK_TABLE, BODY_OFFSET + 2, body.length);
  }
  const c = new Cursor(body, BODY_OFFSET + 2);

  const flags = emptyFlags();
  let flagFields = null;
  switch (version) {
    case 0x0203: flagFields = FLAGS_0203; break;
    case 0x0013: flagFields = FLAGS_0013; break;
    case 0x0001: flagFields = []; break;                  // LOAD_0001: no flags
    default: throw new Error(`ntk: unsupported set version 0x${version.toString(16)}`);
  }
  for (const name of flagFields) flags[name] = c.bool();

  const nCount = c.i32();
  if (nCount < 0 || nCount > 100000) throw new Error(`ntk: implausible dialogue count ${nCount}`);
  const dialogues = [];
  for (let i = 0; i < nCount; i++) dialogues.push(loadDialogue(c));

  // Derived: quest availability = any talk that starts a quest.
  let hasQuest = false;
  const questStartIds = new Set();
  for (const d of dialogues)
    for (const cs of d.cases)
      for (const t of cs.talks) {
        if (t.action === EM_QUEST_START) {
          hasQuest = true;
          for (const q of t.questStartIds) if (q !== UINT_MAX) questStartIds.add(q);
          if (t.param1 !== undefined && t.param1 !== UINT_MAX && t.action === EM_QUEST_START) questStartIds.add(t.param1);
        }
      }

  // Greeting: dialogue NID 1's non-conditional case (nSTARTINDEX=1, FindNonCondition).
  const greetingRaw = pickGreeting(dialogues);

  return {
    type, version, flags, dialogues,
    hasQuest, questStartIds: Array.from(questStartIds),
    greetingRaw,
    consumed: c.p,
    eofExact: c.p === body.length,
    trailing: body.length - c.p,
  };
}

// The entry greeting: dialogue nid==1 preferred (nSTARTINDEX), else the first
// dialogue; within it the first non-conditional case, else the first case.
function pickGreeting(dialogues) {
  if (!dialogues.length) return Buffer.alloc(0);
  let entry = dialogues.find((d) => d.nid === 1) || dialogues[0];
  if (!entry.cases.length) return Buffer.alloc(0);
  const cs = entry.cases.find((x) => !x.hasCondition) || entry.cases[0];
  return cs.basicTalk.text;   // bytes up to the NUL, not the NUL-inclusive raw
}

// Flatten a parsed set into the per-NPC PAGE TREE the runtime renders:
//   { startPage, pages: { <nid>: { g: greeting(cp874), a: [answer,...] } } }
//
// This mirrors the engine's own iteration order so glob ids and answer order
// match the PC exactly:
//   - Dialogues live in std::map<DWORD,CNpcDialogue*> keyed by NID; AddDialogue
//     is first-wins on a duplicate NID. Iteration is ASCENDING NID.
//   - A dialogue's cases are a std::vector in file order (AddCase always pushes).
//   - Talks live in std::map<DWORD,SNpcTalk*> keyed by talk NID; AddTalk is
//     first-wins. Iteration (and thus LoadButtonTalk's answer order) is
//     ASCENDING talk NID.
//   - Glob ids (AssignTalkGlobID, NpcDialogueSet.cpp:1116) are assigned across
//     ALL cases of ALL dialogues in (dlgNID, caseVectorOrder, talkNID) order.
//
// The displayed page uses the dialogue's DEFAULT case — the first with no
// condition (FindNonCondition), else case 0 — exactly as LoadNode/GetDlgText do.
//
// NOTE (honest divergence): the PC further filters each page's answers through
// SNpcTalk::DoTEST at draw time — hiding quest-start/quest-step lines whose live
// per-character quest state does not currently qualify, and condition-gated
// lines. This offline extractor has no character/quest state, so it emits the
// FULL authored answer list. The runtime collapses visually-identical lines and
// the server re-validates every action, but the exact quest-state filtering the
// PC applies is not reproduced here.
function buildNpcTree(parsed) {
  // dialogues: first-wins by nid, then ascending nid
  const byNid = new Map();
  for (const d of parsed.dialogues) if (!byNid.has(d.nid)) byNid.set(d.nid, d);
  const dlgNids = [...byNid.keys()].sort((a, b) => a - b);

  // talk-nid-sorted, first-wins view of a case's talks (mirrors std::map + AddTalk)
  const talkMapOf = (cs) => {
    const m = new Map();
    for (const t of cs.talks) if (!m.has(t.nid)) m.set(t.nid, t);
    return [...m.keys()].sort((a, b) => a - b).map((k) => m.get(k));
  };

  // glob ids across every case of every dialogue
  const glob = new Map();
  let g = 0;
  for (const nid of dlgNids)
    for (const cs of byNid.get(nid).cases)
      for (const t of talkMapOf(cs)) glob.set(t, g++);

  const pages = {};
  for (const nid of dlgNids) {
    const d = byNid.get(nid);
    const def = d.cases.find((c) => !c.hasCondition) || d.cases[0];
    if (!def) { pages[nid] = { g: '', a: [] }; continue; }
    const greeting = def.basicTalk.text.toString('latin1');
    const answers = talkMapOf(def).map((t) => makeAnswer(t, glob.get(t) >>> 0));
    pages[nid] = { g: greeting, a: answers };
  }

  const startPage = byNid.has(1) ? 1 : (dlgNids.length ? dlgNids[0] : 1);
  return { startPage, pages };
}

module.exports = { parse, buildNpcTree, iconFor, makeAnswer, BODY_OFFSET, NPCTALK_TABLE };
