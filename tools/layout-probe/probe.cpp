// layout-probe — dumps authoritative sizeof/offsetof for RAN EP9 packet structs.
//
// Purpose: the mobile client must reproduce MSVC struct layout exactly. Guessing
// is how you get a client that connects and then silently corrupts every packet.
// This includes the REAL headers from SOURCE/ (read-only) and prints ground truth
// as JSON for the spike client to consume.
//
// SOURCE/ is frozen — this project lives outside it and only #includes.
//
// Build: build.cmd

// The SOURCE/ headers assume a precompiled-header prelude (stdafx.h) is already
// in scope — they use std::string and D3DXVECTOR3 without including them. We
// replicate the minimum of that prelude here rather than pulling in MFC.
// Mirrors SOURCE/Lib_Client/StdAfx.h. The G-Logic headers assume MFC and the
// DirectX PCH are already in scope — GLContrlPcMsg.h transitively needs
// LPDIRECT3DDEVICEQ (dxstdafx.h) and CStringArray (afxcoll.h). With this
// prelude and the engine include paths in build.cmd, the real GLMSG message
// headers compile here, so their layouts can be measured directly instead of
// being mirrored by hand.
#ifndef WINVER
#define WINVER 0x0501
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#define _CRT_SECURE_NO_WARNINGS
#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS

#include <afx.h>
#include <afxwin.h>
#include <afxext.h>
#include <afxdisp.h>
#include <afxcoll.h>
#include "dxstdafx.h"

#include <cstdio>
#include <cstddef>
#include <algorithm>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <list>
#include <deque>
#include <queue>
#include <fstream>

#include <strsafe.h>  // GLItemLink.h uses StringCchCopy without including this
#include "d3dx9.h"    // SOURCE/Tik/DXInclude — D3DXVECTOR3 used by G-Logic structs

#include "s_NetGlobal.h"
#include "minTea.h"
// Real GLMSG gameplay messages. These are #pragma pack(1); the MIRROR_* structs
// below are kept as an independent cross-check that the mirrors were correct
// (selftest.js asserts the two agree).
#include "GLContrlPcMsg.h"
#include "GLContrlCharJoinMsg.h"
// Entity lifecycle in view: DROP_CROW / DROP_OUT / CROW_MOVETO.
#include "GLContrlMsg.h"
#include "GLContrlCrowMsg.h"
#include "GLCrowData.h"
#include "GLCharDefine.h"  // SCHARSTATS: a ReadBuffer stride in SITEMBASIC::LOAD
#include "GLItemDrug.h"    // ITEM::SDRUG: blitted by SITEM::LoadFile, wPileNum = stack cap
// SNPC_ITEM / SSkillCondition / SNPC_BUFF — blitted with ReadBuffer inside the
// .ntk (CNpcDialogueSet) tree: SNpcTalk::LOAD_0200 reads 5 need + 5 result
// SNPC_ITEMs, and SNpcTalkCondition::LOAD_0010 reads vectors of all three. Each
// is {DWORD,WORD} so its stride (8 bytes: 4+2+2 tail pad) is measured, not summed.
#include "NpcTalkData.h"
// SNpcTalk::EM_BASIC — the sub-action id an answer's EM_BASIC dwACTION_PARAM1
// carries (NpcTalk.h:40). Nested inside struct SNpcTalk, so its members are
// NOT namespace-scope identifiers — gen-enums.js's DUMP_ENUM(bare name) cannot
// resolve them (they'd need SNpcTalk:: qualification), which is exactly the
// "no self-healing exclusion loop" caveat in README's enum section. Dumped by
// a curated qualified block below instead (npcBasic), same treatment as the
// loginFb block. This is what pins CNPCShopWindow's basic id (EM_NPC_SHOP) at
// 29 by compiler count rather than hand-counting a 31-member sequential enum.
#include "NpcTalk.h"
// Party + friend/whisper messages. #pragma pack(1) on the wire structs, but the
// SFRIEND / GLPARTY_FNET payloads they carry are naturally aligned — measured,
// not summed. GLContrlPartyMsg.h transitively pulls in GLPartyClient.h/GLParty.h.
#include "GLContrlPartyMsg.h"
#include "GLContrlFriendMsg.h"
// Club/guild + player-to-player trade messages. Both are #pragma pack(1) on the
// wire. The trade item-regist message embeds a whole SINVENITEM by value, and
// SINVENITEM is a union of SINVENITEM_SAVE and {WORD wPosX, WORD wPosY,
// SITEMCUSTOM sItemCustom}; SITEMCUSTOM is 8-byte aligned (it holds __time64_t /
// LONGLONG), so sItemCustom does NOT sit at the naive offset 4 — it is measured
// here rather than summed. GLItem.h (SINVENITEM/SITEMCUSTOM) is already pulled in
// transitively by GLContrlBaseMsg.h; it is added to the gen-structs.js scan list
// so its DUMP lines are emitted, not re-included here.
#include "GLContrlClubMsg.h"
#include "GLContrlTradeMsg.h"
// Personal stall / vending (PRIVATE MARKET). All #pragma pack(1). The browse
// broadcast SNETPC_PMARKET_ITEM_INFO_BRD embeds a whole SITEMCUSTOM by value and
// derives from SNETPC_BROAD (nmg + dwGaeaID), so both the base member offset and
// the SITEMCUSTOM offset are measured here, never summed. SSEARCHITEMDATA /
// SSEARCHITEMRESULT (used by the search variants in this header) and
// MAX_SEARCH_RESULT are already in the TU via GLCharDefine.h / GLContrlBaseMsg.h.
#include "GLContrlPrivateMarket.h"
// PVP Competition (Tyranny) in-battle protocol: state transitions, tower
// capture/ownership, and the live ranking feed. Transitively pulls in
// GLContrlTyrannyMsgEnum.h (TYRANNY_REGISTER_FB/TYRANNY_REJOIN_FB) and
// GLPVPTyrannyData.h (TYRANNY_STATE machine data + TYRANNY_PLAYER_DATA, the
// per-player ranking row embedded by value in SNETPC_TYRANNY_F2C_RANKING_UPDATE).
#include "GLPVPTyrannyDefine.h"
#include "GLContrlTyrannyMsg.h"
#include "DxAniKeys.h"
// Animation clip metadata (.cfg / SANIMCONINFO). Versions 0x0101-0x0104 are
// read with a single ReadBuffer of the whole versioned struct, so every one of
// those sizes is a stride; 0x0105+ still blit SANIMSTRIKE[9] and SChaSoundData.
#include "SAnimationInfo.h"
#include "CharacterSound.h"
#include "DxAniBoneScale.h"
// SBONESCALE_100: a ReadBuffer stride inside DxSkinCharData::LOAD_0103/0104.
#include "DxSkinCharData.h"
#include "DxFrameMesh.h"   // D3DEXMATERIAL: a ReadBuffer stride for map objects
#include "DxMethods.h"     // DXAFFINEPARTS: blitted by DxFrame::LoadEffect
#include "plane.h"
#include "line2d.h"
#include "navigationcell.h"
#include "DxLandDef.h"
// `.egp` effect property structs. Every one of the thirteen *_PROPERTY::LoadFile
// bodies blits its whole nested `PROPERTY` with a single ReadBuffer, so each
// sizeof is a record stride and each offsetof is a field address inside the
// node body. These six types are 95.5% of all 34,609 shipped property nodes
// (EFFECTS-SCOPE.md §3). Included by relative path so build.cmd needs no new
// -I: Lib_Engine is already on the search path.
#include "DxEffect/Single/DxEffectSequence.h"
#include "DxEffect/Single/DxEffectMesh.h"
#include "DxEffect/Single/DxEffectParticleSys.h"
#include "DxEffect/Single/DxEffectMoveRotate.h"
#include "DxEffect/Single/DxEffectGround.h"
#include "DxEffect/Single/DxEffectBlurSys.h"
// Not one of the six, measured because it is the only unmeasured node type in
// sk_dfly.egp — one of the five files EFFECTS-SCOPE.md §8 could not explain.
#include "DxEffect/Single/DxEffectPointLight.h"
// MESH 0x0100-0x0103 and PARTICLESYS 0x0100-0x0106 blit PROPERTY_10x structs
// that are declared at file scope inside DxEffectMeshPROP.cpp /
// DxEffectParticleSysPROP.cpp and appear in no header. gen-effectprops.js
// copies those declarations out verbatim, one namespace per file, so they can
// be measured here too — 4,120 shipped nodes that would otherwise be unreadable.
#include "effectprops.legacy.gen.h"
// Water: a per-frame effect COMPONENT (DxFrame::LoadEffect), not a `.egp`
// particle node — WATER_PROPERTY_100/101 and WATER2_PROPERTY are blitted whole
// by DxEffectWater(2)::SetProperty, so their sizeof/offsetof are strides for
// the map-object water extractor.
#include "DxEffect/DxEffectDefine.h"   // DEF_EFFECT_WATER / DEF_EFFECT_WATER2
#include "DxEffect/DxEffectWater.h"
#include "DxEffect/DxEffectWater2.h"
// River — DEF_EFFECT_RIVER, the water system actually placed in this
// server's shipped maps (measured, not assumed — see gen-structs.js).
#include "DxEffect/DxEffectRiver.h"

// The MIRROR_* structs below predate including the real GLMSG headers. They are
// kept deliberately: selftest.js asserts each one still matches the real struct,
// so a mirror that drifts from upstream fails offline instead of on the wire.
// MIRROR_SNETLOBBY_CHARJOIN is also a genuine prefix — the real struct trails
// variable-length inventory the client does not need to address.
// SNATIVEID is a union of a DWORD and two WORDs — 4 bytes (GLDefine.h:102).
#pragma pack(1)
struct MIRROR_SNATIVEID { DWORD dwID; };

// Prefix of GLMSG::SNETLOBBY_CHARJOIN (GLContrlCharJoinMsg.h:278). The full
// struct trails SCHARDATA, quick slots and variable-length inventory, none of
// which the spike needs; the spawn POSITION is what matters and it sits in the
// fixed prefix. GLContrlCharJoinMsg.h is #pragma pack(1) (line 190), so this
// mirror is exact as long as the field order above vPos is unchanged.
struct MIRROR_SNETLOBBY_CHARJOIN
{
    NET_MSG_GENERIC  nmg;
    char             szUserID[USR_ID_LENGTH + 1];
    DWORD            dwClientID;
    DWORD            dwGaeaID;
    MIRROR_SNATIVEID sMapID;
    D3DXVECTOR3      vPos;
};
#pragma pack()

#pragma pack(1)
struct MIRROR_SNETPC_GOTO
{
    NET_MSG_GENERIC nmg;
    DWORD           dwActState;
    D3DXVECTOR3     vCurPos;
    D3DXVECTOR3     vTarPos;
};
struct MIRROR_SNETPC_MOVESTATE
{
    NET_MSG_GENERIC nmg;
    DWORD           dwActState;
};
#pragma pack()

static bool g_first = true;

static void beginStruct(const char* name, size_t size)
{
    if (!g_first) printf(",\n");
    g_first = false;
    printf("    \"%s\": { \"size\": %zu, \"fields\": {", name, size);
}

static void endStruct()
{
    printf(" } }");
}

#define DUMP_FIELD(TYPE, FIELD) \
    printf(" \"%s\": { \"off\": %zu, \"size\": %zu },", \
           #FIELD, offsetof(TYPE, FIELD), sizeof(((TYPE*)0)->FIELD))

// Trailing commas are invalid JSON; emit a sentinel and strip it consumer-side
// rather than tracking per-field state in a throwaway tool.
#define END_FIELDS() printf(" \"_\": null")

int main()
{
    printf("{\n  \"note\": \"generated by layout-probe; authoritative MSVC layout\",\n");
    printf("  \"structs\": {\n");

    // ---- s_NetGlobal.h : NO #pragma pack -> natural alignment ----
    beginStruct("NET_MSG_GENERIC", sizeof(NET_MSG_GENERIC));
    DUMP_FIELD(NET_MSG_GENERIC, dwSize);
    DUMP_FIELD(NET_MSG_GENERIC, nType);
    END_FIELDS(); endStruct();

    beginStruct("NET_CLIENT_VERSION", sizeof(NET_CLIENT_VERSION));
    DUMP_FIELD(NET_CLIENT_VERSION, nmg);
    DUMP_FIELD(NET_CLIENT_VERSION, nPatchProgramVer);
    DUMP_FIELD(NET_CLIENT_VERSION, nGameProgramVer);
    END_FIELDS(); endStruct();

    beginStruct("THAI_NET_LOGIN_DATA", sizeof(THAI_NET_LOGIN_DATA));
    DUMP_FIELD(THAI_NET_LOGIN_DATA, nmg);
    DUMP_FIELD(THAI_NET_LOGIN_DATA, nChannel);
    DUMP_FIELD(THAI_NET_LOGIN_DATA, szPassword);
    DUMP_FIELD(THAI_NET_LOGIN_DATA, szUserid);
    END_FIELDS(); endStruct();

    beginStruct("NET_HEARTBEAT_CLIENT_ANS", sizeof(NET_HEARTBEAT_CLIENT_ANS));
    DUMP_FIELD(NET_HEARTBEAT_CLIENT_ANS, nmg);
    DUMP_FIELD(NET_HEARTBEAT_CLIENT_ANS, szEnCrypt);
    END_FIELDS(); endStruct();

    // Server pushes this unprompted right after the version exchange; it carries
    // the 12-char TEA key used for credentials and heartbeats.
    beginStruct("NET_ENCRYPT_KEY", sizeof(NET_ENCRYPT_KEY));
    DUMP_FIELD(NET_ENCRYPT_KEY, nmg);
    DUMP_FIELD(NET_ENCRYPT_KEY, szEncryptKey);
    END_FIELDS(); endStruct();

    // Batching/compression wrapper. When bCompress is false the payload is a
    // plain run of concatenated messages, not LZO data.
    beginStruct("NET_COMPRESS", sizeof(NET_COMPRESS));
    DUMP_FIELD(NET_COMPRESS, nmg);
    DUMP_FIELD(NET_COMPRESS, bCompress);
    END_FIELDS(); endStruct();

    // Server-list entry. Offsets are relative to the whole message, so the
    // nested gscil fields are addressed through the outer struct.
    beginStruct("NET_CUR_INFO_LOGIN", sizeof(NET_CUR_INFO_LOGIN));
    DUMP_FIELD(NET_CUR_INFO_LOGIN, nmg);
    DUMP_FIELD(NET_CUR_INFO_LOGIN, gscil.szServerIP);
    DUMP_FIELD(NET_CUR_INFO_LOGIN, gscil.nServicePort);
    DUMP_FIELD(NET_CUR_INFO_LOGIN, gscil.nServerGroup);
    DUMP_FIELD(NET_CUR_INFO_LOGIN, gscil.nServerNumber);
    DUMP_FIELD(NET_CUR_INFO_LOGIN, gscil.nServerCurrentClient);
    DUMP_FIELD(NET_CUR_INFO_LOGIN, gscil.nServerMaxClient);
    DUMP_FIELD(NET_CUR_INFO_LOGIN, gscil.bPK);
    END_FIELDS(); endStruct();

    // The live server runs service_provider 3 == SP_CHINA (cfg/[3]ServerAgent.cfg),
    // so CHINA_NET_LOGIN_DATA -- not THAI_NET_LOGIN_DATA -- is the login message
    // this server actually dispatches. Note the RSA_ADD padding on every string
    // field: the fields are 4 bytes LARGER than the region that gets encrypted.
    beginStruct("CHINA_NET_LOGIN_DATA", sizeof(CHINA_NET_LOGIN_DATA));
    DUMP_FIELD(CHINA_NET_LOGIN_DATA, nmg);
    DUMP_FIELD(CHINA_NET_LOGIN_DATA, nChannel);
    DUMP_FIELD(CHINA_NET_LOGIN_DATA, szRandomPassword);
    DUMP_FIELD(CHINA_NET_LOGIN_DATA, szPassword);
    DUMP_FIELD(CHINA_NET_LOGIN_DATA, szUserid);
    END_FIELDS(); endStruct();

    // Agent -> Client login verdict. Arrives LZO-compressed inside a
    // NET_MSG_COMPRESS wrapper. nResult is a USHORT sitting after a fixed
    // Daum-specific string field, so it is NOT at the start of the body.
    beginStruct("NET_LOGIN_FEEDBACK_DATA", sizeof(NET_LOGIN_FEEDBACK_DATA));
    DUMP_FIELD(NET_LOGIN_FEEDBACK_DATA, nmg);
    DUMP_FIELD(NET_LOGIN_FEEDBACK_DATA, szDaumGID);
    DUMP_FIELD(NET_LOGIN_FEEDBACK_DATA, nResult);
    DUMP_FIELD(NET_LOGIN_FEEDBACK_DATA, uChaRemain);
    DUMP_FIELD(NET_LOGIN_FEEDBACK_DATA, nCheckFlag);
    DUMP_FIELD(NET_LOGIN_FEEDBACK_DATA, nPatchProgramVer);
    DUMP_FIELD(NET_LOGIN_FEEDBACK_DATA, nGameProgramVer);
    END_FIELDS(); endStruct();

    // Agent -> Client, sent unprompted on connect in the China flow.
    // ---- Navmesh strides ----
    //
    // Plane / Line2D / NavigationCell are CLASSES with protected members, so
    // offsetof on their fields is ill-formed and the codegen loop correctly
    // excludes them. Only the SIZES are needed here: the navmesh cell record is
    // written field-by-field (NavigationCell::LoadFile), never blitted, so the
    // 188-byte record is a SUM of component sizes rather than sizeof(cell).
    //
    // Line2D being 28 rather than 24 is the trap — a trailing `mutable bool`
    // takes 3 bytes of tail padding, so ~9 bytes of uninitialised memory are
    // baked into every cell of every shipped .wld.
    beginStruct("NAVMESH_SIZES", 0);
    printf(" \"Plane\": { \"off\": 0, \"size\": %zu },", sizeof(Plane));
    printf(" \"Line2D\": { \"off\": 0, \"size\": %zu },", sizeof(Line2D));
    printf(" \"D3DXVECTOR3\": { \"off\": 0, \"size\": %zu },", sizeof(D3DXVECTOR3));
    printf(" \"NavigationCell\": { \"off\": 0, \"size\": %zu },", sizeof(NavigationCell));
    // The record as serialised: CellID + Vertex[3] + Side[3] + Plane +
    // CenterPoint + WallMidpoint[3] + WallDistance[3].
    printf(" \"CELL_RECORD\": { \"off\": 0, \"size\": %zu },",
           sizeof(DWORD) + sizeof(DWORD) * 3 + sizeof(Line2D) * 3 + sizeof(Plane)
           + sizeof(D3DXVECTOR3) + sizeof(D3DXVECTOR3) * 3 + sizeof(float) * 3);
    END_FIELDS(); endStruct();

    // Map object placement (DxOctree -> DxFrame -> DxMeshes). These are all
    // ReadBuffer STRIDES, not field offsets: DxMeshes blits whole arrays of
    // D3DMATERIALQ and D3DEXMATERIAL, and DxFrame blits three D3DXMATRIXA16 in
    // a row. Every one of them desyncs the entire remaining map if it is off by
    // a byte, and D3DXMATRIXA16's 16-byte alignment is exactly the kind of thing
    // that makes the intuitive answer wrong.
    beginStruct("MAPOBJ_SIZES", 0);
    printf(" \"D3DMATERIALQ\": { \"off\": 0, \"size\": %zu },", sizeof(D3DMATERIALQ));
    printf(" \"D3DEXMATERIAL\": { \"off\": 0, \"size\": %zu },", sizeof(D3DEXMATERIAL));
    printf(" \"D3DXMATRIXA16\": { \"off\": 0, \"size\": %zu },", sizeof(D3DXMATRIXA16));
    printf(" \"D3DXMATRIX\": { \"off\": 0, \"size\": %zu },", sizeof(D3DXMATRIX));
    printf(" \"BOOL\": { \"off\": 0, \"size\": %zu },", sizeof(BOOL));
    // DxOctreeMesh blits its attribute table with ReadBuffer too.
    printf(" \"D3DXATTRIBUTERANGE\": { \"off\": 0, \"size\": %zu },", sizeof(D3DXATTRIBUTERANGE));
    // SITEMBASIC::LOAD blits sReqStats with ReadBuffer. Every field after it —
    // including sICONID and strInventoryFile, the two the asset audit needs —
    // shifts if this size is wrong.
    printf(" \"SCHARSTATS\": { \"off\": 0, \"size\": %zu },", sizeof(SCHARSTATS));
    // DxFrame::LoadEffect blits this whole struct after the property blob.
    printf(" \"DXAFFINEPARTS\": { \"off\": 0, \"size\": %zu },", sizeof(DXAFFINEPARTS));
    // The fixed prefix of DxFrame::LoadFile: vTreeMax, vTreeMin, then the three
    // matrices, all read with ReadBuffer before the first BOOL.
    printf(" \"DXFRAME_PREFIX\": { \"off\": 0, \"size\": %zu },",
           sizeof(D3DXVECTOR3) * 2 + sizeof(D3DXMATRIXA16) * 3);
    END_FIELDS(); endStruct();

    // Item stack cap. SITEM::LoadFile blits the whole ITEM::SDRUG with ReadBuffer
    // (GLItem.cpp:422) and wPileNum is the per-item maximum pile (stack) size —
    // consumables carry their real limit, equipment defaults to 1. SDRUG leads
    // with a __time64_t after two 4-byte fields, forcing 8-byte alignment, so
    // wPileNum does NOT sit at the naive byte count of the fields before it.
    // Measured here per rule #1 rather than hand-computed.
    // emDrug (EMITEM_DRUG) is the field GLCHARLOGIC::GET_REVIVE_ITEM
    // (GLogixExPC.cpp:4287-4297) checks for ITEM_DRUG_CALL_REVIVE to decide
    // whether a worn neck/ornament item shows the "revive in place" button —
    // and the same enum RanAutoPotModule/RanRevivePanel need to tell a revive
    // necklace apart from an HP/MP/SP potion (emDrug 1/2/3) or a cure item
    // (emDrug 7). Measured here, not hand-computed, per rule #1.
    // wCureVolume shares SDRUG's anonymous union with wArrowNum (GLItemDrug.h:61-65)
    // and is SITEM::GETAPPLYNUM's divisor for ammo-like types (arrow/charm/bullet/
    // ticket/recall/... GLItem.cpp:717-746) when prorating GETSELLPRICE by the
    // held stack count (wTurnNum) — same blitted blob as wPileNum, so measured
    // here rather than hand-computed too.
    // bInstance gates SITEM::ISPILE() (GLItem.h:115/125: bInstance && wPileNum>1),
    // which is what SITEM::GETSELLPRICE checks before prorating by held quantity
    // at all — measured rather than assumed-true-whenever-wPileNum>1.
    beginStruct("ITEM_SDRUG", sizeof(ITEM::SDRUG));
    DUMP_FIELD(ITEM::SDRUG, wPileNum);
    DUMP_FIELD(ITEM::SDRUG, emDrug);
    DUMP_FIELD(ITEM::SDRUG, wCureVolume);
    DUMP_FIELD(ITEM::SDRUG, bInstance);
    END_FIELDS(); endStruct();

    beginStruct("SLAND_FILEMARK", sizeof(SLAND_FILEMARK));
    DUMP_FIELD(SLAND_FILEMARK, dwNAVI_MARK);
    DUMP_FIELD(SLAND_FILEMARK, dwWEATHER_MARK);
    DUMP_FIELD(SLAND_FILEMARK, dwGATE_MARK);
    DUMP_FIELD(SLAND_FILEMARK, dwCOLL_MARK);
    END_FIELDS(); endStruct();

    beginStruct("SLAND_FILEMARK_100", sizeof(SLAND_FILEMARK_100));
    DUMP_FIELD(SLAND_FILEMARK_100, dwNAVI_MARK);
    DUMP_FIELD(SLAND_FILEMARK_100, dwGATE_MARK);
    DUMP_FIELD(SLAND_FILEMARK_100, dwCOLL_MARK);
    DUMP_FIELD(SLAND_FILEMARK_100, dwWEATHER_MARK);
    END_FIELDS(); endStruct();

    beginStruct("NET_RANDOMPASS_NUMBER", sizeof(NET_RANDOMPASS_NUMBER));
    DUMP_FIELD(NET_RANDOMPASS_NUMBER, nmg);
    DUMP_FIELD(NET_RANDOMPASS_NUMBER, nRandomNumber);
    END_FIELDS(); endStruct();

    // ---- Lobby: character list ----
    //
    // Two-step. REQ_CHA_BAINFO (bare header) -> CHA_BAINFO carrying the account's
    // character NUMBERS; then one REQ_CHA_BINFO per number -> LOBBY_CHAR_SEL with
    // the full record. s_CAgentServerMsg.cpp:86,88.
    //
    // NET_CHA_BBA_INFO depends on MAX_ONESERVERCHAR_NUM, which is 16 under any of
    // the *_PARAM build macros and 4 otherwise (s_NetGlobal.h:193-198). Neither
    // Lib_Network.vcxproj nor ServerAgent.vcxproj defines one, and no header does
    // either, so the live value is 4 -- and this probe, also defining none,
    // agrees with the server by construction. If the server build ever adds a
    // PARAM macro, add it to build.cmd too or this struct silently diverges.
    beginStruct("NET_CHA_REQ_BA_INFO", sizeof(NET_CHA_REQ_BA_INFO));
    DUMP_FIELD(NET_CHA_REQ_BA_INFO, nmg);
    END_FIELDS(); endStruct();

    beginStruct("NET_CHA_BBA_INFO", sizeof(NET_CHA_BBA_INFO));
    DUMP_FIELD(NET_CHA_BBA_INFO, nmg);
    DUMP_FIELD(NET_CHA_BBA_INFO, nChaSNum);
    DUMP_FIELD(NET_CHA_BBA_INFO, nChaNum);
    END_FIELDS(); endStruct();

    beginStruct("NET_CHA_BA_INFO", sizeof(NET_CHA_BA_INFO));
    DUMP_FIELD(NET_CHA_BA_INFO, nmg);
    DUMP_FIELD(NET_CHA_BA_INFO, nChaNum);
    END_FIELDS(); endStruct();

    // ---- Field handoff ----
    //
    // Client sends NET_GAME_JOIN{nChaNum} as NET_MSG_LOBBY_GAME_JOIN; the Agent
    // does a DB round-trip and answers NET_CONNECT_CLIENT_TO_FIELD with the
    // field server's address plus the GaeaID. The client then opens a SECOND
    // socket to the field server and identifies itself with
    // NET_GAME_JOIN_FIELD_IDENTITY (s_NetClientMsg.cpp:104-121).
    beginStruct("NET_GAME_JOIN", sizeof(NET_GAME_JOIN));
    DUMP_FIELD(NET_GAME_JOIN, nmg);
    DUMP_FIELD(NET_GAME_JOIN, nChaNum);
    END_FIELDS(); endStruct();

    beginStruct("NET_CONNECT_CLIENT_TO_FIELD", sizeof(NET_CONNECT_CLIENT_TO_FIELD));
    DUMP_FIELD(NET_CONNECT_CLIENT_TO_FIELD, nmg);
    DUMP_FIELD(NET_CONNECT_CLIENT_TO_FIELD, emType);
    DUMP_FIELD(NET_CONNECT_CLIENT_TO_FIELD, dwGaeaID);
    DUMP_FIELD(NET_CONNECT_CLIENT_TO_FIELD, dwSlotFieldAgent);
    DUMP_FIELD(NET_CONNECT_CLIENT_TO_FIELD, nServicePort);
    DUMP_FIELD(NET_CONNECT_CLIENT_TO_FIELD, szServerIP);
    END_FIELDS(); endStruct();

    beginStruct("NET_GAME_JOIN_FIELD_IDENTITY", sizeof(NET_GAME_JOIN_FIELD_IDENTITY));
    DUMP_FIELD(NET_GAME_JOIN_FIELD_IDENTITY, nmg);
    DUMP_FIELD(NET_GAME_JOIN_FIELD_IDENTITY, emType);
    DUMP_FIELD(NET_GAME_JOIN_FIELD_IDENTITY, dwGaeaID);
    DUMP_FIELD(NET_GAME_JOIN_FIELD_IDENTITY, dwSlotFieldAgent);
    DUMP_FIELD(NET_GAME_JOIN_FIELD_IDENTITY, ck);
    // CRYPT_KEY is two USHORTs. MsgCryptKey ignores what the server sent and
    // hardcodes both to 1 (s_NetClientMsg.cpp:130-139), so that is what the
    // field server expects to see echoed here.
    DUMP_FIELD(NET_GAME_JOIN_FIELD_IDENTITY, ck.nKeyDirection);
    DUMP_FIELD(NET_GAME_JOIN_FIELD_IDENTITY, ck.nKey);
    END_FIELDS(); endStruct();

    // ---- GLContrlPcMsg.h mirrors : #pragma pack(1) -> byte-packed ----
    // Different packing from the block above. That contrast is exactly what this
    // probe exists to prove, so both appear in one report.
    beginStruct("GLMSG::SNETPC_GOTO", sizeof(MIRROR_SNETPC_GOTO));
    DUMP_FIELD(MIRROR_SNETPC_GOTO, nmg);
    DUMP_FIELD(MIRROR_SNETPC_GOTO, dwActState);
    DUMP_FIELD(MIRROR_SNETPC_GOTO, vCurPos);
    DUMP_FIELD(MIRROR_SNETPC_GOTO, vTarPos);
    END_FIELDS(); endStruct();

    // Only the prefix is dumped; the real struct is much larger.
    beginStruct("GLMSG::SNETLOBBY_CHARJOIN_PREFIX", sizeof(MIRROR_SNETLOBBY_CHARJOIN));
    DUMP_FIELD(MIRROR_SNETLOBBY_CHARJOIN, nmg);
    DUMP_FIELD(MIRROR_SNETLOBBY_CHARJOIN, szUserID);
    DUMP_FIELD(MIRROR_SNETLOBBY_CHARJOIN, dwClientID);
    DUMP_FIELD(MIRROR_SNETLOBBY_CHARJOIN, dwGaeaID);
    DUMP_FIELD(MIRROR_SNETLOBBY_CHARJOIN, sMapID);
    DUMP_FIELD(MIRROR_SNETLOBBY_CHARJOIN, vPos);
    END_FIELDS(); endStruct();

    beginStruct("GLMSG::SNETPC_MOVESTATE", sizeof(MIRROR_SNETPC_MOVESTATE));
    DUMP_FIELD(MIRROR_SNETPC_MOVESTATE, nmg);
    DUMP_FIELD(MIRROR_SNETPC_MOVESTATE, dwActState);
    END_FIELDS(); endStruct();

    printf("\n  },\n");

    // Enum values the spike needs. Computed by the compiler, not by hand.
    printf("  \"enums\": {\n");
    printf("    \"NET_MSG_VERSION_OK\": %d,\n",     (int)NET_MSG_VERSION_OK);
    printf("    \"NET_MSG_VERSION_INFO\": %d,\n",   (int)NET_MSG_VERSION_INFO);
    printf("    \"NET_MSG_VERSION_REQ\": %d,\n",    (int)NET_MSG_VERSION_REQ);
    printf("    \"THAI_NET_MSG_LOGIN\": %d,\n",     (int)THAI_NET_MSG_LOGIN);
    printf("    \"THAI_NET_MSG_LOGIN_FB\": %d,\n",  (int)THAI_NET_MSG_LOGIN_FB);
    printf("    \"CHINA_NET_MSG_LOGIN\": %d,\n",    (int)CHINA_NET_MSG_LOGIN);
    printf("    \"CHINA_NET_MSG_LOGIN_FB\": %d,\n", (int)CHINA_NET_MSG_LOGIN_FB);
    printf("    \"NET_MSG_REQ_RAND_KEY\": %d,\n",   (int)NET_MSG_REQ_RAND_KEY);
    printf("    \"NET_MSG_SND_ENCRYPT_KEY\": %d,\n",(int)NET_MSG_SND_ENCRYPT_KEY);
    printf("    \"NET_MSG_REQ_GAME_SVR\": %d,\n",   (int)NET_MSG_REQ_GAME_SVR);
    printf("    \"NET_MSG_SND_GAME_SVR\": %d,\n",   (int)NET_MSG_SND_GAME_SVR);
    printf("    \"NET_MSG_SND_GAME_SVR_END\": %d,\n",(int)NET_MSG_SND_GAME_SVR_END);
    printf("    \"NET_MSG_COMPRESS\": %d,\n",       (int)NET_MSG_COMPRESS);
    printf("    \"NET_MSG_LOGIN_FB\": %d,\n",       (int)NET_MSG_LOGIN_FB);
    printf("    \"NET_MSG_REQ_CRYT_KEY\": %d,\n",   (int)NET_MSG_REQ_CRYT_KEY);
    printf("    \"NET_MSG_SND_CRYT_KEY\": %d,\n",   (int)NET_MSG_SND_CRYT_KEY);
    printf("    \"NET_MSG_RANDOM_NUM\": %d,\n",     (int)NET_MSG_RANDOM_NUM);
    printf("    \"NET_MSG_HEARTBEAT_CLIENT_REQ\": %d,\n",(int)NET_MSG_HEARTBEAT_CLIENT_REQ);
    printf("    \"NET_MSG_HEARTBEAT_CLIENT_ANS\": %d,\n",(int)NET_MSG_HEARTBEAT_CLIENT_ANS);
    printf("    \"NET_MSG_REQ_CHA_BAINFO\": %d,\n",(int)NET_MSG_REQ_CHA_BAINFO);
    printf("    \"NET_MSG_CHA_BAINFO\": %d,\n",    (int)NET_MSG_CHA_BAINFO);
    printf("    \"NET_MSG_REQ_CHA_BINFO\": %d,\n", (int)NET_MSG_REQ_CHA_BINFO);
    printf("    \"NET_MSG_LOBBY_CHAR_SEL\": %d,\n",(int)NET_MSG_LOBBY_CHAR_SEL);
    printf("    \"NET_MSG_LOBBY_GAME_JOIN\": %d,\n",(int)NET_MSG_LOBBY_GAME_JOIN);
    printf("    \"NET_MSG_LOBBY_CHAR_JOIN\": %d,\n",(int)NET_MSG_LOBBY_CHAR_JOIN);
    printf("    \"NET_MSG_LOBBY_CHAR_JOIN_FB\": %d,\n",(int)NET_MSG_LOBBY_CHAR_JOIN_FB);
    printf("    \"NET_MSG_CONNECT_CLIENT_FIELD\": %d,\n",(int)NET_MSG_CONNECT_CLIENT_FIELD);
    printf("    \"NET_MSG_JOIN_FIELD_IDENTITY\": %d,\n",(int)NET_MSG_JOIN_FIELD_IDENTITY);
    printf("    \"NET_MSG_GCTRL_GOTO\": %d,\n",     (int)NET_MSG_GCTRL_GOTO);
    printf("    \"NET_MSG_GCTRL_MOVESTATE\": %d\n", (int)NET_MSG_GCTRL_MOVESTATE);
    printf("  },\n");

    // TEA cross-check vectors.
    //
    // Produced by the REAL minTea from SOURCE/Lib_Network. The JS port in
    // spike/tea.js must reproduce these byte for byte. Round-tripping in JS
    // only proves self-consistency; this proves compatibility with the server.
    printf("  \"teaVectors\": {\n");
    {
        const char* samples[] = { "xx11", "1234", "testuser", "a" };
        for (int i = 0; i < 4; i++)
        {
            char field[USR_ID_LENGTH + 1];
            memset(field, 0, sizeof(field));
            strcpy(field, samples[i]);

            minTea tea; // default ctor -> hardcoded key, same as CNetClient::m_Tea
            tea.encrypt(field, USR_ID_LENGTH + 1);

            printf("    \"%s\": \"", samples[i]);
            for (int b = 0; b < USR_ID_LENGTH + 1; b++)
                printf("%02x", (unsigned char)field[b]);
            printf("\"%s\n", i == 3 ? "" : ",");
        }
    }
    printf("  },\n");

    // Every struct in s_NetGlobal.h, with compiler-computed sizes and offsets.
    // Generated by gen-structs.js; build.cmd regenerates and self-heals the
    // exclusion list from the compiler's own errors. This is the Phase 1
    // codegen input — a reimplementation reads layout from here rather than
    // transcribing 2,700+ structs by hand.
    printf("  \"allStructs\": {\n");
    {
        bool first = true;
#define DUMP_STRUCT(S) \
        do { if (!first) printf(",\n"); first = false; \
             printf("    \"%s\": { \"size\": %zu, \"fields\": {", #S, sizeof(S)); \
             bool ffirst = true;
#define DUMP_MEMBER(S, M) \
             if (!ffirst) printf(","); ffirst = false; \
             printf(" \"%s\": { \"off\": %zu, \"size\": %zu }", \
                    #M, offsetof(S, M), sizeof(((S*)0)->M));
#define END_STRUCT() \
             printf(" } }"); } while (0);
#include "structs.gen.inc"
#undef DUMP_STRUCT
#undef DUMP_MEMBER
#undef END_STRUCT
        printf("\n");
    }
    printf("  },\n");

    // ---- `.egp` effect property layouts ----
    //
    // Generated by gen-effectprops.js, which emits QUALIFIED names
    // (SEQUENCE_PROPERTY::PROPERTY) because all six types name their nested
    // struct `PROPERTY` and unqualified names would collide.
    //
    // The node body an .egp stores is:
    //     [D3DXMATRIX m_matLocal][BOOL m_bMoveObj][float][float][PROPERTY]
    // on the current version of each type, with a 36-byte DXAFFINEPARTS wedged
    // in after the matrix on every older version (EFFECTS-SCOPE.md §2). Both
    // prefixes are emitted below as sums of compiler-measured sizes rather than
    // as the literals 76 and 112, so a char[256] can never land 4 bytes off and
    // start inventing filenames out of the previous string's tail.
    printf("  \"effectProps\": {\n");
    {
        bool first = true;
#define DUMP_STRUCT(S) \
        do { if (!first) printf(",\n"); first = false; \
             printf("    \"%s\": { \"size\": %zu, \"fields\": {", #S, sizeof(S)); \
             bool ffirst = true;
#define DUMP_MEMBER(S, M) \
             if (!ffirst) printf(","); ffirst = false; \
             printf(" \"%s\": { \"off\": %zu, \"size\": %zu }", \
                    #M, offsetof(S, M), sizeof(((S*)0)->M));
#define END_STRUCT() \
             printf(" } }"); } while (0);
#include "effectprops.gen.inc"
#undef DUMP_STRUCT
#undef DUMP_MEMBER
#undef END_STRUCT
        // EFF_PROPERTY::GetSizeBase() is
        // sizeof(float)+sizeof(float)+sizeof(BOOL)+sizeof(D3DXMATRIX)
        // (DxEffSinglePropGMan.cpp:136). Reproduced as the same sum, not as 76.
        printf(",\n    \"EFFPROP_PREFIX\": { \"size\": %zu, \"fields\": {} }",
               sizeof(D3DXMATRIX) + sizeof(BOOL) + sizeof(float) * 2);
        printf(",\n    \"EFFPROP_PREFIX_AFFINE\": { \"size\": %zu, \"fields\": {} }",
               sizeof(D3DXMATRIX) + sizeof(DXAFFINEPARTS) + sizeof(BOOL) + sizeof(float) * 2);
        printf("\n");
    }
    printf("  },\n");

    // Every enum member in s_NetGlobal.h, name -> compiler-computed value.
    // Generated by gen-enums.js; regenerate after header changes:
    //   node gen-enums.js > enums.gen.inc
    // This is what makes an unrecognised message ID impossible to mis-resolve.
    printf("  \"allEnums\": {\n");
#define DUMP_ENUM(E) printf("    \"%s\": %d,\n", #E, (int)(E))
#include "enums.gen.inc"
#undef DUMP_ENUM
    printf("    \"_\": 0\n  },\n");

    // Login feedback result codes, so nResult is readable instead of a bare int.
    printf("  \"loginFb\": {\n");
#define DUMP_FB(E) printf("    \"%d\": \"%s\",\n", (int)E, #E)
    DUMP_FB(EM_LOGIN_FB_SUB_OK);
    DUMP_FB(EM_LOGIN_FB_SUB_FAIL);
    DUMP_FB(EM_LOGIN_FB_SUB_SYSTEM);
    DUMP_FB(EM_LOGIN_FB_SUB_USAGE);
    DUMP_FB(EM_LOGIN_FB_SUB_DUP);
    DUMP_FB(EM_LOGIN_FB_SUB_INCORRECT);
    DUMP_FB(EM_LOGIN_FB_SUB_IP_BAN);
    DUMP_FB(EM_LOGIN_FB_SUB_BLOCK);
    DUMP_FB(EM_LOGIN_FB_CH_FULL);
    DUMP_FB(EM_LOGIN_FB_SUB_RANDOM_PASS);
    DUMP_FB(EM_LOGIN_FB_SUB_PASS_OK);
    DUMP_FB(EM_LOGIN_FB_SUB_ALREADYOFFLINE);
    DUMP_FB(EM_LOGIN_FB_SUB_REQUIRETIME);
#undef DUMP_FB
    printf("    \"_\": null\n  },\n");

    // SNpcTalk::EM_BASIC — the EM_BASIC sub-action id an answer carries
    // (NpcTalk.h:40..81). Nested inside struct SNpcTalk, so DUMP_ENUM's bare
    // #E stringification would not compile unqualified; this curated block
    // qualifies each member instead of hand-counting the 31-entry sequential
    // enum (RanNpcData's Basic* constants and CNPCShopWindow's EM_NPC_SHOP
    // are cross-checked against this at test time).
    printf("  \"npcBasic\": {\n");
#define DUMP_NPCBASIC(E) printf("    \"%s\": %d,\n", #E, (int)(SNpcTalk::E))
    DUMP_NPCBASIC(EM_STORAGE);
    DUMP_NPCBASIC(EM_MARKET);
    DUMP_NPCBASIC(EM_CURE);
    DUMP_NPCBASIC(EM_STARTPOINT);
    DUMP_NPCBASIC(EM_CHAR_RESET);
    DUMP_NPCBASIC(EM_ITEM_TRADE);
    DUMP_NPCBASIC(EM_BUSSTATION);
    DUMP_NPCBASIC(EM_CLUB_NEW);
    DUMP_NPCBASIC(EM_CLUB_UP);
    DUMP_NPCBASIC(EM_CD_CERTIFY);
    DUMP_NPCBASIC(EM_COMMISSION);
    DUMP_NPCBASIC(EM_CLUB_STORAGE);
    DUMP_NPCBASIC(EM_ITEM_REBUILD);
    DUMP_NPCBASIC(EM_ODDEVEN);
    DUMP_NPCBASIC(EM_RECOVERY_EXP);
    DUMP_NPCBASIC(EM_RANDOM_PAGE);
    DUMP_NPCBASIC(EM_ITEMSEARCH_PAGE);
    DUMP_NPCBASIC(EM_ATTENDANCE_BOOK);
    DUMP_NPCBASIC(EM_ITEM_MIX);
    DUMP_NPCBASIC(EM_RESERVE_00);
    DUMP_NPCBASIC(EM_RESERVE_01);
    DUMP_NPCBASIC(EM_RESERVE_02);
    DUMP_NPCBASIC(EM_RESERVE_03);
    DUMP_NPCBASIC(EM_RESERVE_04);
    DUMP_NPCBASIC(EM_RESERVE_05);
    DUMP_NPCBASIC(EM_RESERVE_06);
    DUMP_NPCBASIC(EM_RESERVE_07);
    DUMP_NPCBASIC(EM_RESERVE_08);
    DUMP_NPCBASIC(EM_RESERVE_09);
    DUMP_NPCBASIC(EM_NPC_SHOP);
    DUMP_NPCBASIC(EM_ITEM_EXCHANGE);
#undef DUMP_NPCBASIC
    printf("    \"_\": 0\n  },\n");

    // The China login encrypts each field over a DIFFERENT length than the field
    // size, and the lengths are not all N+1:
    //   szUserid         TEA over USR_ID_LENGTH+1        (21)  field is 25
    //   szPassword       TEA over USR_PASS_LENGTH        (20)  field is 25  <-- no +1
    //   szRandomPassword TEA over USR_RAND_PASS_LENGTH+1 (7)   field is 11
    // The empty-string case matters: the PC client sends an empty random password
    // (LoginPage.cpp:199 -- the TW_PARAM block that would fill it is commented out).
    printf("  \"teaVectorsByLen\": {\n");
    {
        struct { int len; const char* sample; } cases[] = {
            { USR_PASS_LENGTH,          "5f4dcc3b5aa765d61d8" }, // md5 hex, 19 chars
            { USR_PASS_LENGTH,          "81dc9bdb52d04dc2003" },
            { USR_RAND_PASS_LENGTH + 1, "" },                    // empty random pass
            { USR_ID_LENGTH + 1,        "xx11" },
        };
        const int n = (int)(sizeof(cases) / sizeof(cases[0]));
        for (int i = 0; i < n; i++)
        {
            char field[64];
            memset(field, 0, sizeof(field));
            strcpy(field, cases[i].sample);

            minTea tea;
            tea.encrypt(field, cases[i].len);

            printf("    \"%d:%s\": \"", cases[i].len, cases[i].sample);
            for (int b = 0; b < cases[i].len; b++)
                printf("%02x", (unsigned char)field[b]);
            printf("\"%s\n", i == n - 1 ? "" : ",");
        }
    }
    printf("  },\n");

    printf("  \"consts\": {\n");
    printf("    \"USR_RAND_PASS_LENGTH\": %d,\n", (int)USR_RAND_PASS_LENGTH);
    printf("    \"MAX_ONESERVERCHAR_NUM\": %d,\n",(int)MAX_ONESERVERCHAR_NUM);
    printf("    \"RSA_ADD\": %d,\n",              (int)RSA_ADD);
    printf("    \"SP_CHINA\": %d,\n",             (int)SP_CHINA);
    printf("    \"SP_THAILAND\": %d,\n",          (int)SP_THAILAND);
    printf("    \"ENCRYPT_KEY\": %d,\n",     (int)ENCRYPT_KEY);
    printf("    \"USR_ID_LENGTH\": %d,\n",   (int)USR_ID_LENGTH);
    printf("    \"USR_PASS_LENGTH\": %d,\n", (int)USR_PASS_LENGTH);
    printf("    \"NET_MSG_BASE\": %d,\n",    (int)NET_MSG_BASE);
    printf("    \"NET_MSG_LGIN\": %d,\n",    (int)NET_MSG_LGIN);
    // Server-side reject/block thresholds. The spike validates its own outbound
    // packets against these so it can never trip the IP auto-blocker.
    printf("    \"NET_DATA_BUFSIZE\": %d,\n",            (int)NET_DATA_BUFSIZE);
    printf("    \"NET_DATA_CLIENT_MSG_BUFSIZE\": %d,\n", (int)NET_DATA_CLIENT_MSG_BUFSIZE);
    printf("    \"NET_MSG_LOBBY\": %d,\n",    (int)NET_MSG_LOBBY);
    // Water effect type IDs (DxEffectDefine.h) — read by the compiler rather
    // than by eye, same reasoning as every other constant in this block.
    // (VERSION is a plain `= 0x101` literal assignment in DxEffectWater.cpp,
    // a single definition with no arithmetic and no #if-disabled duplicate —
    // unlike NET_MSG_LOBBY, so it is not re-derived here; it is not a link-time
    // symbol available to this TU since the .cpp is not compiled into probe.)
    printf("    \"DEF_EFFECT_WATER\": %d,\n",  (int)DEF_EFFECT_WATER);
    printf("    \"DEF_EFFECT_WATER2\": %d,\n", (int)DEF_EFFECT_WATER2);
    printf("    \"DEF_EFFECT_RIVER\": %d\n",   (int)DEF_EFFECT_RIVER);
    printf("  }\n}\n");

    return 0;
}
