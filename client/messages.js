'use strict';
//
// Message builders and parsers.
//
// Every offset comes from the probe-generated layout (442 structs). Nothing
// here hand-computes a size, an offset or an enum value — that rule is what
// this whole toolchain exists to enforce.
//
const crypto = require('crypto');
const P = require('../spike/protocol');
const tea = require('../spike/tea');

const E = P.E;
const A = P.LAYOUT.allEnums;

// EM_ACT_RUN, GLCharDefine.h:1173. The server reads only this bit out of
// dwActState in MsgGoto.
const EM_ACT_RUN = 0x00000001;

// --- login server ---------------------------------------------------------

function version(gameVer = 1, patchVer = 1) {
  const body = P.allocBody('NET_CLIENT_VERSION');
  P.writeInt32(body, 'NET_CLIENT_VERSION', 'nPatchProgramVer', patchVer);
  P.writeInt32(body, 'NET_CLIENT_VERSION', 'nGameProgramVer', gameVer);
  return { type: E.NET_MSG_VERSION_INFO, body };
}

const reqGameSvr = () => ({ type: E.NET_MSG_REQ_GAME_SVR, body: Buffer.alloc(0) });

function parseServerEntry(body) {
  const g = (f) => P.fieldOff('NET_CUR_INFO_LOGIN', f);
  const size = P.S.NET_CUR_INFO_LOGIN.fields['gscil.szServerIP'].size;
  const raw = body.subarray(g('gscil.szServerIP'), g('gscil.szServerIP') + size);
  const nul = raw.indexOf(0);
  return {
    ip: raw.toString('latin1', 0, nul === -1 ? size : nul),
    port: body.readInt32LE(g('gscil.nServicePort')),
    group: body.readInt32LE(g('gscil.nServerGroup')),
    number: body.readInt32LE(g('gscil.nServerNumber')),
    current: body.readInt32LE(g('gscil.nServerCurrentClient')),
    max: body.readInt32LE(g('gscil.nServerMaxClient')),
    pk: body[g('gscil.bPK')] !== 0,
  };
}

// --- agent: login ---------------------------------------------------------

// TEA over the first `teaLen` bytes of a `fieldSize` field. The China struct
// pads every string with RSA_ADD(4) bytes that are NOT encrypted, so field size
// and TEA length differ per field.
function teaPrefix(value, fieldSize, teaLen) {
  const field = Buffer.alloc(fieldSize);
  tea.encryptField(value, teaLen).copy(field);
  return field;
}

/**
 * CHINA_NET_LOGIN_DATA. This server runs service_provider 3 (SP_CHINA), so this
 * — not the Thai variant — is the login it dispatches; every other provider
 * handler bails silently on the first line.
 *
 * pwMode 'plain' is what this server's DB accepts. 'md5' matches ChinaSndLogin
 * as written.
 *
 * Note the password field is encrypted only when the secret is <= 16 chars:
 * longer values pad to 20, need 21 with the NUL, exceed USR_PASS_LENGTH and hit
 * minTea's bail, so they travel as plaintext. The server's decrypt mirrors the
 * same bail, so both cases round-trip. The 19-char MD5 hex always takes the
 * plaintext path.
 */
function chinaLogin(user, pass, channel = 0, pwMode = 'plain', randomPass = '') {
  const F = P.S.CHINA_NET_LOGIN_DATA.fields;
  const body = P.allocBody('CHINA_NET_LOGIN_DATA');
  P.writeInt32(body, 'CHINA_NET_LOGIN_DATA', 'nChannel', channel);

  const secret = pwMode === 'plain'
    ? pass
    : crypto.createHash('md5').update(pass, 'latin1').digest('hex');
  // StringCchCopy(dst, USR_PASS_LENGTH, ...) -> 19 chars + NUL. The server
  // re-truncates identically, so a longer secret can never match.
  const pw = secret.slice(0, P.C.USR_PASS_LENGTH - 1);

  P.writeBytes(body, 'CHINA_NET_LOGIN_DATA', 'szRandomPassword',
               teaPrefix(randomPass, F.szRandomPassword.size,
                         P.C.USR_RAND_PASS_LENGTH + 1));
  P.writeBytes(body, 'CHINA_NET_LOGIN_DATA', 'szPassword',
               teaPrefix(pw, F.szPassword.size, P.C.USR_PASS_LENGTH));
  P.writeBytes(body, 'CHINA_NET_LOGIN_DATA', 'szUserid',
               teaPrefix(user, F.szUserid.size, P.C.USR_ID_LENGTH + 1));
  return { type: E.CHINA_NET_MSG_LOGIN, body };
}

function parseLoginFeedback(body) {
  const off = P.fieldOff('NET_LOGIN_FEEDBACK_DATA', 'nResult');
  const result = body.length > off + 1 ? body.readUInt16LE(off) : null;
  return { result, name: P.LOGIN_FB[String(result)] || 'unmapped' };
}

// --- agent: lobby ---------------------------------------------------------

const reqCharSlots = () => ({ type: E.NET_MSG_REQ_CHA_BAINFO, body: Buffer.alloc(0) });

function reqCharInfo(chaNum) {
  const body = P.allocBody('NET_CHA_BA_INFO');
  P.writeInt32(body, 'NET_CHA_BA_INFO', 'nChaNum', chaNum);
  return { type: E.NET_MSG_REQ_CHA_BINFO, body };
}

function parseCharSlots(body) {
  const countOff = P.fieldOff('NET_CHA_BBA_INFO', 'nChaSNum');
  const listOff = P.fieldOff('NET_CHA_BBA_INFO', 'nChaNum');
  const cap = P.C.MAX_ONESERVERCHAR_NUM;
  const n = Math.max(0, Math.min(body.readInt32LE(countOff), cap));
  const out = [];
  for (let i = 0; i < n; i++) {
    const v = body.readInt32LE(listOff + i * 4);
    if (v > 0) out.push(v);
  }
  return out;
}

// SCHARINFO_LOBBY starts DWORD m_dwCharID then char m_szName[]. Both are at
// fixed offsets regardless of packing.
function parseCharSummary(body) {
  const rest = body.subarray(4);
  const nul = rest.indexOf(0);
  return {
    charId: body.readUInt32LE(0),
    name: rest.toString('latin1', 0, nul === -1 ? 0 : nul),
  };
}

function gameJoin(chaNum) {
  const body = P.allocBody('NET_GAME_JOIN');
  P.writeInt32(body, 'NET_GAME_JOIN', 'nChaNum', chaNum);
  return { type: E.NET_MSG_LOBBY_GAME_JOIN, body };
}

function parseFieldHandoff(body) {
  const g = (f) => P.fieldOff('NET_CONNECT_CLIENT_TO_FIELD', f);
  const size = P.S.NET_CONNECT_CLIENT_TO_FIELD.fields.szServerIP.size;
  const raw = body.subarray(g('szServerIP'), g('szServerIP') + size);
  const nul = raw.indexOf(0);
  return {
    joinType: body.readInt32LE(g('emType')),
    gaeaId: body.readUInt32LE(g('dwGaeaID')),
    slot: body.readUInt32LE(g('dwSlotFieldAgent')),
    port: body.readInt32LE(g('nServicePort')),
    ip: raw.toString('latin1', 0, nul === -1 ? size : nul),
  };
}

// --- field ----------------------------------------------------------------

function fieldIdentity(handoff) {
  const S = 'NET_GAME_JOIN_FIELD_IDENTITY';
  const body = P.allocBody(S);
  P.writeInt32(body, S, 'emType', handoff.joinType);
  P.writeInt32(body, S, 'dwGaeaID', handoff.gaeaId);
  P.writeInt32(body, S, 'dwSlotFieldAgent', handoff.slot);
  // MsgCryptKey hardcodes both halves to 1 regardless of what the server sent.
  body.writeUInt16LE(1, P.fieldOff(S, 'ck.nKeyDirection'));
  body.writeUInt16LE(1, P.fieldOff(S, 'ck.nKey'));
  return { type: E.NET_MSG_JOIN_FIELD_IDENTITY, body };
}

// Sent on the AGENT socket after the spawn, and REQUIRED: a joined character
// sits in EM_ACT_WAITING and cannot act until MsgReady clears it. Nothing on
// the server prompts for these; the PC client sends them once its map has
// finished loading (GLCharacter.cpp:4170-4177).
const landIn = () => ({ type: A.NET_MSG_GCTRL_REQ_LANDIN, body: Buffer.alloc(0) });
const ready = () => ({ type: A.NET_MSG_GCTRL_REQ_READY, body: Buffer.alloc(0) });

function parseSpawn(body) {
  const S = 'GLMSG::SNETLOBBY_CHARJOIN_PREFIX';
  const o = (f) => P.fieldOff(S, f);
  const raw = body.subarray(o('szUserID'), o('szUserID') + 21);
  const nul = raw.indexOf(0);
  return {
    userId: raw.toString('latin1', 0, nul === -1 ? 21 : nul),
    clientId: body.readUInt32LE(o('dwClientID')),
    gaeaId: body.readUInt32LE(o('dwGaeaID')),
    mapId: body.readUInt32LE(o('sMapID')),
    pos: [body.readFloatLE(o('vPos')),
          body.readFloatLE(o('vPos') + 4),
          body.readFloatLE(o('vPos') + 8)],
  };
}

/**
 * SNETPC_GOTO — destination-based movement, sent on the FIELD socket.
 * The server rejects it if |server_pos - curPos| > 60 and snaps us back, so
 * curPos must track the server's belief within that budget.
 */
function goto_(curPos, tarPos, actState = EM_ACT_RUN) {
  const S = 'GLMSG::SNETPC_GOTO';
  const body = P.allocBody(S);
  const o = (f) => P.fieldOff(S, f);
  body.writeUInt32LE(actState >>> 0, o('dwActState'));
  for (let i = 0; i < 3; i++) body.writeFloatLE(curPos[i], o('vCurPos') + i * 4);
  for (let i = 0; i < 3; i++) body.writeFloatLE(tarPos[i], o('vTarPos') + i * 4);
  return { type: E.NET_MSG_GCTRL_GOTO, body };
}

module.exports = {
  EM_ACT_RUN,
  version, reqGameSvr, parseServerEntry,
  chinaLogin, parseLoginFeedback,
  reqCharSlots, reqCharInfo, parseCharSlots, parseCharSummary,
  gameJoin, parseFieldHandoff,
  fieldIdentity, landIn, ready, parseSpawn, goto: goto_,
};
