'use strict';
//
// Generate enum-dump code for the layout probe.
//
// Why this exists: s_NetGlobal.h defines message IDs as arithmetic on base
// macros, and several of those bases are defined TWICE — once in a live block
// and once in a commented-out or #if-disabled block. Resolving a message ID by
// reading the header has produced a wrong answer three times now
// (NET_MSG_LOBBY 2005-vs-1942, MAX_ONESERVERCHAR_NUM 16-vs-4,
// NET_MSG_GCTRL 3003-vs-2892). Every one of those failures was silent.
//
// So: extract the NAMES here, and let the C++ compiler compute the VALUES.
// This script never evaluates arithmetic itself — that is the entire point.
//
//   node gen-enums.js > enums.gen.inc
//
// Comments are stripped before scanning, so commented-out definitions cannot
// leak in. #if-disabled enum members would not compile; if the build breaks
// after a header change, an identifier in a disabled branch is the likely cause
// and belongs in SKIP below.
//

const fs = require('fs');
const path = require('path');

const SRC = path.join(__dirname, '..', '..', '..', 'SOURCE');

const HEADERS = [
  path.join(SRC, 'Lib_Network', 's_NetGlobal.h'),
  // EMANI_MAINTYPE / EMANI_SUBTYPE — the action classification an Animator is
  // built from. Read from the compiler for the same reason as everything else:
  // the enum overlays FOUR numbering schemes on one type (character, ABL,
  // vehicle, generic AN_SUB_nn), so several names share a value and the block
  // boundaries are not visible from any single line.
  path.join(SRC, 'Lib_Engine', 'Meshs', 'DxAniKeys.h'),
  // EMANICONINFO — ACF_SZNAME / ACF_DIV / ACF_STRIKE size the fixed arrays in
  // every SANIMCONINFO generation, and ACF_LOOP / ACF_UPBODY / ACF_DOWNBODY are
  // the clip flags an Animator reads.
  path.join(SRC, 'Lib_Engine', 'Meshs', 'SAnimationInfo.h'),
  // EMITEM_TYPE — item.sBasicOp.emItemType, what GLCharactorReq(2).cpp's cell-
  // tap dispatcher switches on (ITEM_CURE -> use-potion, ITEM_TAXI_CARD/
  // ITEM_TRANSFER_CARD/ITEM_*_LOCK_ENABLE/RECOVER -> open a client-side
  // window). A single unbroken enum with every member explicitly assigned
  // (no #define arithmetic, no duplicated block found by inspection), but
  // compiler-verified anyway rather than trusted by eye, same rule as
  // everywhere else in this project.
  path.join(SRC, 'Lib_Client', 'G-Logic', 'GLItemDef.h'),
  // TYRANNY_STATE / TYRANNY_SCHOOL_* / TYRANNY_TOWER_* — the PVP Competition
  // in-battle state machine and the fixed school/tower index spaces the
  // battle HUD reads (RanCompetitionPackets in-battle extension).
  path.join(SRC, 'Lib_Client', 'G-Logic', 'GLPVPTyrannyDefine.h'),
  // TYRANNY_REGISTER_FB / TYRANNY_REJOIN_FB — already read "by eye" from a
  // plain sequential explicit-literal enum (documented in
  // RanCompetitionPackets.cs's own class doc); scanned here too so that
  // reasoning is compiler-checked rather than merely asserted.
  path.join(SRC, 'Lib_Client', 'G-Logic', 'GLContrlTyrannyMsgEnum.h'),
  // EMNPCSHOP_PURCHASE_FB — the CNPCShopWindow buy result code
  // (SNETPC_REQ_NPCSHOP_PURCHASE_MONEY_FB.emFB). Declared at file scope (NOT
  // nested, unlike SNpcTalk::EM_BASIC — see probe.cpp's curated npcBasic
  // block for that one), so the normal DUMP_ENUM(bare name) mechanism works.
  path.join(SRC, 'Lib_Client', 'G-Logic', 'GLContrlInvenMsg.h'),
];

// Identifiers that exist in the text but not in every build configuration.
const SKIP = new Set();

function stripComments(src) {
  // Remove /* ... */ and // ... while preserving newlines, so a commented-out
  // "#define NET_MSG_LOBBY (NET_MSG_BASE + 1013)" can never be picked up.
  let out = '';
  let i = 0;
  while (i < src.length) {
    if (src[i] === '/' && src[i + 1] === '*') {
      const end = src.indexOf('*/', i + 2);
      const chunk = src.slice(i, end === -1 ? src.length : end + 2);
      out += chunk.replace(/[^\n]/g, ' ');
      i = end === -1 ? src.length : end + 2;
    } else if (src[i] === '/' && src[i + 1] === '/') {
      const end = src.indexOf('\n', i);
      i = end === -1 ? src.length : end;
    } else {
      out += src[i++];
    }
  }
  return out;
}

// Pull identifiers out of `enum [Name] { ... };` bodies only. #defines are
// deliberately ignored: they are the ambiguous ones, and the probe already
// dumps the handful that matter individually.
// Byte offsets at which the brace nesting depth is 0, i.e. namespace scope.
// An enum declared INSIDE a struct (`struct SANIMCONINFO { enum { VERSION =
// 0x0200 }; }`) names constants that are not addressable as bare identifiers,
// so DUMP_ENUM(VERSION) would not compile — and unlike the struct codegen there
// is no self-healing exclusion loop here to catch it.
function depthAt(src) {
  const depth = new Int32Array(src.length + 1);
  let d = 0;
  for (let i = 0; i < src.length; i++) {
    depth[i] = d;
    if (src[i] === '{') d++;
    else if (src[i] === '}') d--;
  }
  depth[src.length] = d;
  return depth;
}

function collectEnumMembers(src) {
  const names = [];
  const depth0 = depthAt(src);
  const re = /\benum\s+(?:\w+\s*)?\{/g;
  let m;
  while ((m = re.exec(src)) !== null) {
    if (depth0[m.index] !== 0) continue; // nested in a struct/class/namespace
    let depth = 1;
    let i = re.lastIndex;
    const start = i;
    while (i < src.length && depth > 0) {
      if (src[i] === '{') depth++;
      else if (src[i] === '}') depth--;
      i++;
    }
    const body = src.slice(start, i - 1);
    for (const raw of body.split(',')) {
      const id = raw.trim().split('=')[0].trim();
      if (/^[A-Za-z_]\w*$/.test(id) && !SKIP.has(id)) names.push(id);
    }
  }
  return [...new Set(names)].sort();
}

const names = [...new Set(
  HEADERS.flatMap((h) => collectEnumMembers(stripComments(fs.readFileSync(h, 'utf8'))))
)].sort();

console.log('// GENERATED by gen-enums.js — do not edit.');
console.log(`// ${names.length} enum members extracted from ${HEADERS.length} header(s)`);
console.log('// Values are computed by the compiler, never by a script.');
for (const n of names) {
  console.log(`DUMP_ENUM(${n});`);
}
