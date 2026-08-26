'use strict';
//
// Generate struct-layout dump code for the layout probe.
//
// Phase 1 of plan.md: "Codegen packet structs from s_NetGlobal.h + GLMSG
// headers. Do not hand-transcribe 2,700+ structs."
//
// Same principle as gen-enums.js: this script extracts NAMES only. Every size
// and offset is computed by MSVC compiling the real headers. A parser that
// tried to compute layout itself would have to model #pragma pack, alignment,
// unions, base classes and typedef chains — and would be silently wrong the
// first time any of those changed.
//
//   node gen-structs.js > structs.gen.inc
//
// Some declarations cannot be dumped (abstract types, members whose type is
// incomplete in this translation unit, bitfields — offsetof is invalid on
// those). Rather than hand-maintaining exclusions, build.cmd compiles, feeds
// the compiler's own error output back through --exclude, and retries. The
// blacklist is therefore derived from the compiler too.
//

const fs = require('fs');
const path = require('path');

const SRC = path.join(__dirname, '..', '..', '..', 'SOURCE');

// `ns` qualifies the emitted names. The GLMSG headers declare their structs
// inside `namespace GLMSG`, and several names there collide with unrelated
// types elsewhere, so they must stay qualified rather than be pulled in with
// a using-directive.
const HEADERS = [
  { file: path.join(SRC, 'Lib_Network', 's_NetGlobal.h'), ns: '' },
  { file: path.join(SRC, 'Lib_Client', 'G-Logic', 'GLContrlPcMsg.h'), ns: 'GLMSG' },
  { file: path.join(SRC, 'Lib_Client', 'G-Logic', 'GLContrlCharJoinMsg.h'), ns: 'GLMSG' },
  // Entity lifecycle in the field: DROP_CROW / DROP_OUT / CROW_MOVETO are the
  // messages a world model has to handle, measured from a live traffic census.
  { file: path.join(SRC, 'Lib_Client', 'G-Logic', 'GLContrlMsg.h'), ns: 'GLMSG' },
  { file: path.join(SRC, 'Lib_Client', 'G-Logic', 'GLContrlCrowMsg.h'), ns: 'GLMSG' },
  // GM_MOVE2GATE_FB: the server's answer to a rejected SNETPC_GOTO (fDist > 60,
  // GLCharMsg.cpp:256) — the position-correction/rubber-band the client must
  // apply, or a visually-illegal walk is never snapped back. vPOS is the
  // server's real m_vPos.
  { file: path.join(SRC, 'Lib_Client', 'G-Logic', 'GLContrlServerMsg.h'), ns: 'GLMSG' },
  // Club/guild inbound + outbound messages (SNET_CLUB_INFO_2CLT, ..._MEMBER_2CLT,
  // ..._MEMBER_STATE, SCLUBMEMBER, the two SNETLOBBY_CLUB_* join-burst variants),
  // and the player-to-player trade handshake (SNET_TRADE*, ..._ITEM_REGIST_TAR
  // which carries an embedded SINVENITEM). All #pragma pack(1).
  { file: path.join(SRC, 'Lib_Client', 'G-Logic', 'GLContrlClubMsg.h'), ns: 'GLMSG' },
  { file: path.join(SRC, 'Lib_Client', 'G-Logic', 'GLContrlTradeMsg.h'), ns: 'GLMSG' },
  // Personal stall / vending (PRIVATE MARKET), all #pragma pack(1). The sell-side
  // set (TITLE / REGITEM / DISITEM / OPEN / CLOSE / BUY / ITEM_INFO + their _FB
  // and _BRD replies) is what RanMarketModule handles. SNETPC_PMARKET_ITEM_INFO_BRD
  // carries an SITEMCUSTOM by value under an SNETPC_BROAD base, so the base
  // member offset (dwGaeaID) and the SITEMCUSTOM offset are measured, not summed.
  { file: path.join(SRC, 'Lib_Client', 'G-Logic', 'GLContrlPrivateMarket.h'), ns: 'GLMSG' },
  // SNETPC_BROAD — the {nmg, dwGaeaID} base the PMARKET _BRD replies inherit;
  // scanned so its own DUMP lines are emitted (already in the TU via the msg headers).
  // (SNATIVEID is a pure-union struct in GLDefine.h — already in this HEADERS list —
  // so the codegen emits nothing for it; its 4-byte size is confirmed by
  // SITEMCUSTOM.sNativeID and each PMARKET struct's own sSALEPOS/sINVENPOS offset.)
  { file: path.join(SRC, 'Lib_Client', 'G-Logic', 'GLContrlBaseMsg.h'), ns: 'GLMSG' },
  // SINVENITEM / SITEMCUSTOM — the item blob a trade offer carries by value.
  // SINVENITEM is a union whose {WORD,WORD,SITEMCUSTOM} arm forces 8-byte
  // alignment, so sItemCustom's offset is measured, never summed. GLItem.h is
  // already in the probe TU via GLContrlBaseMsg.h; scanning it here only emits
  // the DUMP lines.
  { file: path.join(SRC, 'Lib_Client', 'G-Logic', 'GLItem.h'), ns: '' },
  // Party + friend/whisper social systems. The SNET_PARTY_* / SNETPC_REQ_FRIEND*
  // messages are #pragma pack(1) in their headers, but the payloads they carry
  // are NOT: SFRIEND (GLContrlCharJoinMsg.h) is naturally aligned and has tail
  // padding, and GLPARTY_FNET embeds a WORD-aligned SNATIVEID — so both are
  // measured here rather than summed by hand. GLParty.h is pulled in for
  // GLPARTY_FNET's internal layout (the per-member repeat inside PARTY_FNEW).
  { file: path.join(SRC, 'Lib_Client', 'G-Logic', 'GLContrlPartyMsg.h'), ns: 'GLMSG' },
  { file: path.join(SRC, 'Lib_Client', 'G-Logic', 'GLContrlFriendMsg.h'), ns: 'GLMSG' },
  { file: path.join(SRC, 'Lib_Client', 'G-Logic', 'GLParty.h'), ns: '' },
  // NPC-interaction requests: the gate/regen (SNETPC_REQ_REGEN_GATE), quest
  // start (SNETPC_REQ_QUEST_START) and cure (SNETPC_REQ_CURE) bodies the NPC
  // module sends when a tapped NPC's menu option is chosen. All #pragma pack(1)
  // and all-DWORD, so the body offsets are unambiguous — measured here anyway so
  // RanNpcModule cites the compiler rather than a hand sum.
  { file: path.join(SRC, 'Lib_Client', 'G-Logic', 'GLContrlNpcMsg.h'), ns: 'GLMSG' },
  // CNPCShopWindow's buy request/reply (SNETPC_REQ_NPCSHOP_PURCHASE_MONEY[_FB],
  // GLContrlInvenMsg.h:2766-2798) — the SEPARATE, categorized/paged NPC-shop
  // system (EM_NPC_SHOP, basic id 29), distinct from the sale-slot MARKET flow
  // above. #pragma pack(1) like the rest of this file's namespace GLMSG block,
  // and already pulled into the probe TU transitively via GLContrlMsg.h, so
  // this entry only adds it to the struct-name SCAN list.
  { file: path.join(SRC, 'Lib_Client', 'G-Logic', 'GLContrlInvenMsg.h'), ns: 'GLMSG' },
  // Quest-STEP NPC talk (advancing a quest from an in-progress dialogue line,
  // EM_QUEST_STEP): SNET_QUEST_STEP_NPC_TALK is what
  // GLCharacter::ReqQuestStepNpcTalk (GLCharactorReq.cpp:4578) actually sends —
  // a DIFFERENT struct/opcode family (GLContrlQuestMsg.h,
  // NET_MSG_GCTRL_QUEST_PROG_NPCTALK) than quest-START's SNETPC_REQ_QUEST_START
  // (GLContrlNpcMsg.h, above). Measured here so RanNpcModule's quest-step
  // request cites the compiler rather than assuming the two share a shape.
  { file: path.join(SRC, 'Lib_Client', 'G-Logic', 'GLContrlQuestMsg.h'), ns: 'GLMSG' },
  // PK / duel systems. GLContrlPcMsg2.h carries SNETPC_UPDATE_PK_SCORE and
  // SNETPC_UPDATE_PK_DEATH (the PK kill/death broadcasts, #pragma pack(1) with a
  // trailing char szName[CHAR_SZNAME]); GLContrlConflictMsg.h carries the
  // "confront" (duel) handshake SNETPC_REQ_CONFRONT / _TAR / _ANS / _FB, each of
  // which embeds an SCONFT_OPTION by value. GLCharDefine.h is scanned for
  // SCONFT_OPTION itself so RanPkModule writes each option field at the
  // compiler's offset rather than summing widths across a pack(1) struct that
  // mixes bool/WORD/float. All three are already in the probe TU transitively;
  // scanning them here only emits the DUMP lines.
  { file: path.join(SRC, 'Lib_Client', 'G-Logic', 'GLContrlPcMsg2.h'), ns: 'GLMSG' },
  { file: path.join(SRC, 'Lib_Client', 'G-Logic', 'GLContrlConflictMsg.h'), ns: 'GLMSG' },
  { file: path.join(SRC, 'Lib_Client', 'G-Logic', 'GLCharDefine.h'), ns: '' },
  // Payload types carried inside those messages. Measured standalone; an
  // absolute offset is (offset of the member in the message) + (offset within
  // the payload), both compiler-derived.
  { file: path.join(SRC, 'Lib_Client', 'G-Logic', 'GLCrowData.h'), ns: '' },
  // SCROWBASIC (and its versioned predecessors) is the record blitted for each
  // crow in Crow.mnsf. m_emCrow (CROW_NPC=1 / CROW_MOB=2) is the NPC-vs-monster
  // discriminator the NPC-interaction module needs, and its offset is NOT the
  // obvious sum of field widths across the 18 struct versions. Measured here so
  // extract-crowmodels.js reads it at the compiler's offset, never a guess.
  { file: path.join(SRC, 'Lib_Client', 'G-Logic', 'GLCrowDataBasic.h'), ns: '' },
  // NPC dialogue (.ntk) blitted records. SNpcTalk::LOAD_0200 and
  // SNpcTalkCondition::LOAD_0010 ReadBuffer arrays of SNPC_ITEM / SSkillCondition
  // / SNPC_BUFF, so each {DWORD,WORD} stride (8 bytes) must be measured for the
  // extract-npcdialogue.js walk to stay in sync to EOF.
  { file: path.join(SRC, 'Lib_Client', 'NpcTalk', 'NpcTalkData.h'), ns: '' },
  { file: path.join(SRC, 'Lib_Engine', 'G-Logic', 'GLDefine.h'), ns: '' },
  // Animation keyframe records. SMatrixKey holds a D3DXMATRIXA16, which is
  // 16-byte aligned, so its size is NOT the obvious 4+64.
  { file: path.join(SRC, 'Lib_Engine', 'Meshs', 'DxAniKeys.h'), ns: '' },
  // Animation clip metadata. SANIMCONINFO_101..104 are blitted whole by
  // SANIMCONINFO::LoadFile_010x, so each of those sizes is a record stride;
  // SANIMSTRIKE and the three SChaSoundData generations are ReadBuffer strides
  // inside the newer field-by-field loaders.
  { file: path.join(SRC, 'Lib_Engine', 'Meshs', 'SAnimationInfo.h'), ns: '' },
  { file: path.join(SRC, 'Lib_Engine', 'DxSound', 'CharacterSound.h'), ns: '' },
  { file: path.join(SRC, 'Lib_Engine', 'Meshs', 'DxAniBoneScale.h'), ns: '' },
  // SBONESCALE_100 is blitted by DxSkinCharData::LOAD_0103/LOAD_0104, so it is
  // a stride in the `.chf`/`.abf` walk.
  { file: path.join(SRC, 'Lib_Engine', 'Meshs', 'DxSkinCharData.h'), ns: '' },
  // Navmesh types. Declared 'class', not 'struct' — hence the regex above.
  // Line2D is 28 bytes only because a trailing mutable bool gets 3 bytes of
  // tail padding; NavigationCell's 188-byte record is written field by field,
  // never blitted, so its size is a cross-check rather than a stride.
  // Map object placement. DxMeshes blits its material arrays with ReadBuffer,
  // so D3DEXMATERIAL's size is a stride: getting it wrong desyncs every object
  // in the map. D3DMATERIALQ is a typedef of D3DMATERIAL9 and is measured the
  // same way rather than assumed to be 68 bytes.
  { file: path.join(SRC, 'Lib_Engine', 'Meshs', 'DxFrameMesh.h'), ns: '' },
  { file: path.join(SRC, 'Lib_Engine', 'NaviMesh', 'plane.h'), ns: '' },
  { file: path.join(SRC, 'Lib_Engine', 'NaviMesh', 'line2d.h'), ns: '' },
  { file: path.join(SRC, 'Lib_Engine', 'NaviMesh', 'navigationcell.h'), ns: '' },
  // PVP Competition (Tyranny) in-battle protocol: state transitions, the
  // countdown timer, tower-capture/ownership broadcasts and the live ranking
  // feed. All #pragma pack(1) in GLContrlTyrannyMsg.h (opens line 9, closes
  // line 607 — confirmed by reading the pragmas directly, same as the
  // register/queue subset already measured). GLPVPTyrannyData.h is scanned
  // separately below because SNETPC_TYRANNY_F2C_RANKING_UPDATE embeds
  // TYRANNY_PLAYER_DATA BY VALUE and that type is declared in a header with
  // NO #pragma pack of its own (default/natural alignment), so its internal
  // layout — including any padding between its char[]/WORD/DWORD members —
  // must be measured, never summed by hand.
  { file: path.join(SRC, 'Lib_Client', 'G-Logic', 'GLContrlTyrannyMsg.h'), ns: 'GLMSG' },
  { file: path.join(SRC, 'Lib_Client', 'G-Logic', 'GLPVPTyrannyData.h'), ns: '' },
  // Water: WATER_PROPERTY_100/101 (DxEffectWater) and WATER2_PROPERTY
  // (DxEffectWater2) are blitted whole by SetProperty from the `dwSize` bytes
  // DxFrame::LoadEffect reads — every field offset here is a stride for the
  // map-object water extractor, not a hand count.
  { file: path.join(SRC, 'Lib_Engine', 'DxEffect', 'DxEffectWater.h'), ns: '' },
  { file: path.join(SRC, 'Lib_Engine', 'DxEffect', 'DxEffectWater2.h'), ns: '' },
  // River — DEF_EFFECT_RIVER (0x2006), the water system actually placed in
  // this server's shipped maps (measured: every reachable standalone-effect
  // list that stops on an unhandled type stops on 0x2006 v0x107, never on
  // Water/Water2). RIVER_PROPERTY (v0x107, the live VERSION) is the only
  // variant probed — v0x100/101-105/106 are read structurally by the engine
  // but never observed in this corpus, so their layout is not claimed here.
  { file: path.join(SRC, 'Lib_Engine', 'DxEffect', 'DxEffectRiver.h'), ns: '' },
];

// Members whose offsetof is not usable, or that are not data at all.
const SKIP_MEMBER = /^(operator|~|enum|struct|class|union|typedef|public|private|protected|friend|static|const|inline|virtual|template)\b/;

function stripComments(src) {
  let out = '';
  let i = 0;
  while (i < src.length) {
    if (src[i] === '/' && src[i + 1] === '*') {
      const end = src.indexOf('*/', i + 2);
      out += src.slice(i, end === -1 ? src.length : end + 2).replace(/[^\n]/g, ' ');
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

// Byte ranges covered by `namespace NS { ... }`. Needed because these headers
// are not uniformly namespaced: GLContrlCharJoinMsg.h, for example, declares
// GLCHARAG_DATA and SFRIEND at file scope and only the SNET* messages inside
// GLMSG. Qualifying by filename rather than by position produces names the
// compiler rejects.
function namespaceRanges(src, ns) {
  const ranges = [];
  const re = new RegExp(`\\bnamespace\\s+${ns}\\s*\\{`, 'g');
  let m;
  while ((m = re.exec(src)) !== null) {
    let depth = 1;
    let i = re.lastIndex;
    while (i < src.length && depth > 0) {
      if (src[i] === '{') depth++;
      else if (src[i] === '}') depth--;
      i++;
    }
    ranges.push([m.index, i]);
  }
  return ranges;
}

// Return [{name, body, at}] for each `struct NAME { ... }` or `class NAME {`.
function findStructs(src) {
  const found = [];
  // Capture the base-class list too: several message structs inherit (e.g.
  // SNETCROW_MOVETO : public SNETCROW), and the inherited members are the ones
  // carrying the entity id. Dumping only locally-declared members would leave
  // the id unreadable.
  //
  // `class` is matched as well as `struct`: the navmesh types (Plane, Line2D,
  // NavigationCell) are classes. They have methods but no virtuals, so they
  // stay standard-layout and offsetof remains valid — and where it is not, the
  // compiler rejects it and build-structs.js excludes it automatically.
  const re = /\b(?:struct|class)\s+([A-Za-z_]\w*)\s*(?::([^{]*))?\{/g;
  let m;
  while ((m = re.exec(src)) !== null) {
    let depth = 1;
    let i = re.lastIndex;
    const start = i;
    while (i < src.length && depth > 0) {
      if (src[i] === '{') depth++;
      else if (src[i] === '}') depth--;
      i++;
    }
    const bases = (m[2] || '')
      .split(',')
      .map((b) => b.replace(/\b(public|protected|private|virtual)\b/g, '').trim())
      .filter((b) => /^[A-Za-z_]\w*$/.test(b));
    found.push({ name: m[1], body: src.slice(start, i - 1), at: m.index, bases });
  }
  return found;
}

// Extract plain data member names from a struct body: `TYPE name;`,
// `TYPE name[N];`, `TYPE name[N][M];`. Skips constructors (which contain '('),
// nested braces, and anything matching SKIP_MEMBER.
function findMembers(body) {
  // Drop nested brace blocks (ctors, methods, nested types, unions) so their
  // internals are not mistaken for members of this struct.
  let flat = '';
  let depth = 0;
  for (const ch of body) {
    if (ch === '{') depth++;
    else if (ch === '}') depth--;
    else if (depth === 0) flat += ch;
  }

  const names = [];
  for (const raw of flat.split(';')) {
    const decl = raw.trim().replace(/\s+/g, ' ');
    if (!decl || decl.includes('(') || decl.includes(':')) continue;
    if (SKIP_MEMBER.test(decl)) continue;

    // One declaration may declare several members: `DWORD m_dwETime,
    // m_dwETimeOrig;` in SANIMCONINFO is exactly that, and taking only the
    // last identifier silently loses m_dwETime — a field whose offset the
    // clip parser needs. Split on commas, but ONLY when the declaration has no
    // angle brackets: `std::map<DWORD,float> m` would otherwise yield a
    // phantom member named DWORD.
    const parts = decl.includes('<') || decl.includes('>')
      ? [decl]
      : decl.split(',').map((p) => p.trim()).filter(Boolean);

    parts.forEach((part, i) => {
      // Last identifier before any array subscript is the member name.
      const m = part.match(/([A-Za-z_]\w*)\s*(\[[^\]]*\])*\s*$/);
      if (!m) return;
      // A bare type with no member name (e.g. "GLLandMan") has only one token.
      // Continuation declarators after a comma are single tokens by design.
      if (i === 0 && part.split(' ').length < 2) return;
      names.push(m[1]);
    });
  }
  return [...new Set(names)];
}

// Exclusions live in exclude.txt, one "Struct" or "Struct.member" per line.
// The file is written by build-structs.js from the compiler's own diagnostics,
// not maintained by hand.
const EXCLUDE_FILE = path.join(__dirname, 'exclude.txt');
const EXCLUDE = new Set();
if (fs.existsSync(EXCLUDE_FILE)) {
  for (const line of fs.readFileSync(EXCLUDE_FILE, 'utf8').split('\n')) {
    const t = line.split('#')[0].trim();
    if (t) EXCLUDE.add(t);
  }
}

const structs = [];
for (const h of HEADERS) {
  const src = stripComments(fs.readFileSync(h.file, 'utf8'));
  const ranges = h.ns ? namespaceRanges(src, h.ns) : [];
  for (const s of findStructs(src)) {
    const inNs = ranges.some(([a, b]) => s.at > a && s.at < b);
    structs.push({ ...s, qname: inNs ? `${h.ns}::${s.name}` : s.name });
  }
}
// Later definitions win, matching what the compiler sees.
const byName = new Map();
for (const s of structs) byName.set(s.qname, s);

let nStruct = 0;
let nField = 0;
const out = [];
// Members declared locally, plus everything inherited. offsetof on an
// inherited member is valid for these standard-layout structs, and the
// compiler rejects it if it is not — which the build loop then excludes.
const localMembers = new Map();
for (const [n, s] of byName) localMembers.set(n, findMembers(s.body));

function allMembers(qname, seen = new Set()) {
  if (seen.has(qname)) return [];
  seen.add(qname);
  const s = byName.get(qname);
  if (!s) return [];
  const out = [];
  for (const b of s.bases || []) {
    // Bases are written unqualified inside the namespace; try both spellings.
    const q = qname.includes('::') ? `${qname.split('::')[0]}::${b}` : b;
    out.push(...allMembers(byName.has(q) ? q : b, seen));
  }
  out.push(...(localMembers.get(qname) || []));
  return out;
}

for (const [name, s] of byName) {
  if (EXCLUDE.has(name)) continue;
  const members = [...new Set(allMembers(name))]
    .filter((f) => !EXCLUDE.has(`${name}.${f}`));
  if (!members.length) continue;
  out.push(`DUMP_STRUCT(${name});`);
  for (const f of members) out.push(`DUMP_MEMBER(${name}, ${f});`);
  out.push('END_STRUCT();');
  nStruct++;
  nField += members.length;
}

console.log('// GENERATED by gen-structs.js — do not edit.');
console.log(`// ${nStruct} structs, ${nField} members extracted from s_NetGlobal.h`);
console.log('// Sizes and offsets are computed by the compiler, never by a script.');
if (EXCLUDE.size) console.log(`// ${EXCLUDE.size} entr(ies) excluded by the build loop.`);
console.log(out.join('\n'));
