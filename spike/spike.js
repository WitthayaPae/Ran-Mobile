'use strict';
//
// Phase 0 protocol spike — proves a non-MiniA.exe client can talk to an
// UNMODIFIED ServerLogin. Throwaway by design; the reusable output is the
// knowledge, not this code.
//
// Milestone 1 (this file): connect -> version -> login -> read response.
// Milestone 2: agent handoff (garbage layer on), server list, character select.
// Milestone 3: field handoff, spawn, SNETPC_GOTO walk.
//
// Usage:
//   node spike.js --host 127.0.0.1 --port 12000 --user USER --pass PASS
//
// SAFETY: defaults to localhost. Connecting to a non-loopback host requires
// --allow-remote, because cfg/ in this repo points at a live production IP and
// pointing a half-built client at real players is not a thing to do by accident.
//

const net = require('net');
const fs = require('fs');
const path = require('path');
const P = require('./protocol');
const tea = require('./tea');
const lzo = require('./lzo');
const crypto = require('crypto');

// Ports come from cfg/ (production values):
//   12001 Session   12002 Field   12003 Agent   12004 Login
const DEFAULT_LOGIN_PORT = 12004;

// ---------------------------------------------------------------------------
// local credentials
//
// Optional MOBILE/spike/local.json, git-ignored, so passwords never need to be
// typed on a command line or pasted into a chat/transcript:
//
//   { "host": "1.2.3.4", "port": 12004, "user": "acct", "pass": "secret" }
//
// Env vars RAN_SPIKE_USER / RAN_SPIKE_PASS work too. CLI args win over both.
// ---------------------------------------------------------------------------
const LOCAL_CONFIG = path.join(__dirname, 'local.json');

function loadLocalConfig() {
  if (!fs.existsSync(LOCAL_CONFIG)) return {};
  try {
    return JSON.parse(fs.readFileSync(LOCAL_CONFIG, 'utf8'));
  } catch (err) {
    console.error(`local.json is present but unreadable: ${err.message}`);
    process.exit(2);
  }
}

// ---------------------------------------------------------------------------
// args
// ---------------------------------------------------------------------------
function parseArgs(argv) {
  const cfg = loadLocalConfig();
  const a = {
    host: cfg.host || '127.0.0.1',
    port: cfg.port || DEFAULT_LOGIN_PORT,
    user: cfg.user || process.env.RAN_SPIKE_USER || '',
    pass: cfg.pass || process.env.RAN_SPIKE_PASS || '',
    channel: cfg.channel !== undefined ? cfg.channel : 0,
    gameVer: cfg.gameVer || 1,
    patchVer: cfg.patchVer || 1,
    allowRemote: cfg.allowRemote || false,
    // Must match the server's cfg `service_provider`. The Agent's per-provider
    // login handlers each bail on the first line if it doesn't match, so a
    // mismatch produces total silence rather than an error.
    service: cfg.service || 'china',
    // 'plain' is what this server's DB actually accepts (verified:
    // EM_LOGIN_FB_SUB_OK). ChinaSndLogin as written MD5s the password first,
    // but user_verify here compares against a plaintext column, so hashing
    // yields EM_LOGIN_FB_SUB_INCORRECT. Switch with --pw-mode md5.
    pwMode: cfg.pwMode || 'plain',
    // Enter the world after listing characters. --no-join stops at the lobby.
    join: cfg.join !== undefined ? cfg.join : true,
    // How long to sit in the field collecting spawn traffic before leaving.
    observeMs: cfg.observeMs || 8000,
    fieldDelayMs: cfg.fieldDelayMs !== undefined ? cfg.fieldDelayMs : 1500,
    timeout: 15000,
  };
  for (let i = 2; i < argv.length; i++) {
    const k = argv[i];
    const v = argv[i + 1];
    switch (k) {
      case '--host':         a.host = v; i++; break;
      case '--port':         a.port = parseInt(v, 10); i++; break;
      case '--user':         a.user = v; i++; break;
      case '--pass':         a.pass = v; i++; break;
      case '--channel':      a.channel = parseInt(v, 10); i++; break;
      case '--game-ver':     a.gameVer = parseInt(v, 10); i++; break;
      case '--patch-ver':    a.patchVer = parseInt(v, 10); i++; break;
      case '--timeout':      a.timeout = parseInt(v, 10); i++; break;
      case '--allow-remote': a.allowRemote = true; break;
      case '--version-only': a.versionOnly = true; break;
      case '--pw-mode':      a.pwMode = v; i++; break;
      case '--service':      a.service = v; i++; break;
      case '--no-join':      a.join = false; break;
      case '--observe-ms':   a.observeMs = parseInt(v, 10); i++; break;
      case '--field-delay-ms': a.fieldDelayMs = parseInt(v, 10); i++; break;
      case '--help':         a.help = true; break;
      default:
        throw new Error(`unknown argument: ${k}`);
    }
  }
  return a;
}

const LOOPBACK = new Set(['127.0.0.1', 'localhost', '::1']);

// ---------------------------------------------------------------------------
// logging
// ---------------------------------------------------------------------------
const t0 = Date.now();
const ts = () => `[${String(Date.now() - t0).padStart(6)}ms]`;
const log  = (...m) => console.log(ts(), ...m);
const warn = (...m) => console.log(ts(), 'WARN', ...m);

// Reverse enum lookup over ALL 1600+ enum members from s_NetGlobal.h, so an
// observed message ID is never just a number.
const nameOf = (t) => P.nameOfType(t);

// Full enum table, for message IDs the probe does not surface individually.
const A = P.LAYOUT.allEnums;

function hexDump(buf, max = 64) {
  const b = buf.subarray(0, max);
  const hex = b.toString('hex').replace(/(.{2})/g, '$1 ').trim();
  const ascii = Array.from(b)
    .map((c) => (c >= 0x20 && c < 0x7f ? String.fromCharCode(c) : '.'))
    .join('');
  return `${hex}${buf.length > max ? ' ...' : ''}  |${ascii}|`;
}

// ---------------------------------------------------------------------------
// packet builders
// ---------------------------------------------------------------------------

// NET_CLIENT_VERSION — s_NetGlobal.h:3878. Sent first, before login.
// s_NetClientMsg.cpp:262 SndVersion().
function buildVersion(gameVer, patchVer) {
  const body = P.allocBody('NET_CLIENT_VERSION');
  P.writeInt32(body, 'NET_CLIENT_VERSION', 'nPatchProgramVer', patchVer);
  P.writeInt32(body, 'NET_CLIENT_VERSION', 'nGameProgramVer', gameVer);
  return { type: P.E.NET_MSG_VERSION_INFO, body };
}

// NET_HEARTBEAT_CLIENT_ANS — CNetClient::SndHeartbeat, s_NetClient.cpp:1117.
// In the AGENT state the client TEA-encrypts its copy of the server-supplied
// key and echoes it back. Note the real client encrypts m_szEncryptKeyHeart
// *in place*, so successive heartbeats re-encrypt the previous ciphertext —
// stateful. We only need the first one here.
function buildHeartbeatAns(encryptKey) {
  const body = P.allocBody('NET_HEARTBEAT_CLIENT_ANS');
  const size = P.S.NET_HEARTBEAT_CLIENT_ANS.fields.szEnCrypt.size;
  const value = encryptKey
    ? tea.encryptField(encryptKey, size)
    : Buffer.alloc(size);
  P.writeBytes(body, 'NET_HEARTBEAT_CLIENT_ANS', 'szEnCrypt', value);
  return { type: P.E.NET_MSG_HEARTBEAT_CLIENT_ANS, body };
}

// Bare 8-byte header, no body (s_NetClientMsg.cpp:314-320).
// ServerLogin answers with one or more NET_MSG_SND_GAME_SVR then
// NET_MSG_SND_GAME_SVR_END.
function buildReqGameSvr() {
  return { type: P.E.NET_MSG_REQ_GAME_SVR, body: Buffer.alloc(0) };
}

// THAI_NET_LOGIN_DATA — s_NetGlobal.h:3088.
//
// Two things that are easy to get wrong:
//   1. On the wire szPassword comes BEFORE szUserid.
//   2. Both fields are XXTEA-encrypted in place before sending
//      (CNetClient::ThaiSndLogin, s_NetClientMsgLogin.cpp:311-312).
//      Sending them as plaintext gets no response at all — the server
//      just drops it silently.
// pwMode selects how the password is encoded before TEA:
//   'plain'  — ThaiSndLogin as written (s_NetClientMsgLogin.cpp:286)
//   'md5'    — MD5 hex, truncated to fill the field (21 -> 20 chars + NUL)
//   'md5-19' — China variant exactly: StringCchCopy uses USR_PASS_LENGTH (20),
//              NOT +1, so the 32-char hash lands as 19 chars + NUL, and TEA
//              runs over 20 bytes rather than 21 (s_NetClientMsgLogin.cpp:133-137)
//
// The server has RANPARAM::bFeatureRegisterUseMD5 gating an MD5 login path
// (s_CAgentServerMsgLogin.cpp:599-601), so the stored password may be a hash
// even though ThaiSndLogin itself does no hashing.
function encodePassword(pass, pwSize, pwMode) {
  if (pwMode === 'plain') return tea.encryptField(pass, pwSize);

  const hex = crypto.createHash('md5').update(pass, 'latin1').digest('hex');
  if (pwMode === 'md5-19') {
    // Mirror StringCchCopy(dst, USR_PASS_LENGTH, ...) -> 19 chars + NUL,
    // then TEA over USR_PASS_LENGTH bytes, written into the 21-byte field.
    const field = Buffer.alloc(pwSize);
    tea.encryptField(hex.slice(0, pwSize - 2), pwSize - 1).copy(field);
    return field;
  }
  return tea.encryptField(hex.slice(0, pwSize - 1), pwSize);
}

function buildThaiLogin(user, pass, channel, pwMode = 'plain') {
  const body = P.allocBody('THAI_NET_LOGIN_DATA');
  P.writeInt32(body, 'THAI_NET_LOGIN_DATA', 'nChannel', channel);

  const idSize = P.S.THAI_NET_LOGIN_DATA.fields.szUserid.size;
  const pwSize = P.S.THAI_NET_LOGIN_DATA.fields.szPassword.size;

  P.writeBytes(body, 'THAI_NET_LOGIN_DATA', 'szUserid', tea.encryptField(user, idSize));
  P.writeBytes(body, 'THAI_NET_LOGIN_DATA', 'szPassword',
               encodePassword(pass, pwSize, pwMode));

  return { type: P.E.THAI_NET_MSG_LOGIN, body };
}

// TEA over the first `teaLen` bytes of a `fieldSize` field, rest left zero.
//
// The China struct pads every string with RSA_ADD (4) extra bytes that are NOT
// part of the encrypted region, so field size and TEA length differ:
//   szUserid[25]         TEA over 21   (USR_ID_LENGTH+1)
//   szPassword[25]       TEA over 20   (USR_PASS_LENGTH -- no +1)
//   szRandomPassword[11] TEA over 7    (USR_RAND_PASS_LENGTH+1)
function teaPrefixField(value, fieldSize, teaLen) {
  const field = Buffer.alloc(fieldSize);
  tea.encryptField(value, teaLen).copy(field);
  return field;
}

// CHINA_NET_LOGIN_DATA — s_NetGlobal.h:3059, sent by ChinaSndLogin
// (s_NetClientMsgLogin.cpp:111). This is the login path the live server
// actually dispatches: cfg/[3]ServerAgent.cfg sets service_provider 3 = SP_CHINA,
// and CAgentServer::ThaiMsgLogin bails at its first line when the provider is
// not SP_THAILAND (s_CAgentServerMsgLogin.cpp:18) -- silently, with no console
// output and no reply, which is exactly the dead air we were seeing.
//
// Field order on the wire is random-pass, password, userid.
//
// The password is MD5 of the plaintext, hex, truncated to 19 chars by
// StringCchCopy(dst, USR_PASS_LENGTH, ...). TEA over 20 bytes then hits the
// minTea overflow bail and copies nothing, so it travels as PLAINTEXT hex and
// the server's matching decrypt no-ops the same way. See tea.js encryptField.
// pwMode:
//   'md5'   — ChinaSndLogin as written: MD5 hex, truncated to 19 chars
//   'plain' — the raw password, same truncation
//
// A full 32-char MD5 is impossible on this path regardless of what we send:
// the server re-truncates with StringCchCopy(dst, USR_PASS_LENGTH, ...) at
// s_CAgentServerMsgLogin.cpp:296. So the stored secret is either plaintext or
// a 19-char-truncated hash, and those are the only two worth trying.
function buildChinaLogin(user, pass, channel, pwMode = 'md5', randomPass = '') {
  const F = P.S.CHINA_NET_LOGIN_DATA.fields;
  const body = P.allocBody('CHINA_NET_LOGIN_DATA');
  P.writeInt32(body, 'CHINA_NET_LOGIN_DATA', 'nChannel', channel);

  const secret = pwMode === 'plain'
    ? pass
    : crypto.createHash('md5').update(pass, 'latin1').digest('hex');
  const pwPlain = secret.slice(0, P.C.USR_PASS_LENGTH - 1); // StringCchCopy -> 19 chars

  P.writeBytes(body, 'CHINA_NET_LOGIN_DATA', 'szRandomPassword',
               teaPrefixField(randomPass, F.szRandomPassword.size,
                              P.C.USR_RAND_PASS_LENGTH + 1));
  P.writeBytes(body, 'CHINA_NET_LOGIN_DATA', 'szPassword',
               teaPrefixField(pwPlain, F.szPassword.size, P.C.USR_PASS_LENGTH));
  P.writeBytes(body, 'CHINA_NET_LOGIN_DATA', 'szUserid',
               teaPrefixField(user, F.szUserid.size, P.C.USR_ID_LENGTH + 1));

  return { type: P.E.CHINA_NET_MSG_LOGIN, body };
}

// ---- lobby: character list ----
//
// Two steps (s_CAgentServerMsg.cpp:86,88):
//   REQ_CHA_BAINFO (bare header)  -> CHA_BAINFO   { nChaSNum, nChaNum[4] }
//   REQ_CHA_BINFO  { nChaNum }    -> LOBBY_CHAR_SEL (SCHARINFO_LOBBY)
// Both handlers call CloseClient if IsAccountPass is false, so these must not
// be sent before EM_LOGIN_FB_SUB_OK has landed.
function buildReqChaBaInfo() {
  return { type: P.E.NET_MSG_REQ_CHA_BAINFO, body: Buffer.alloc(0) };
}

function buildReqChaBInfo(chaNum) {
  const body = P.allocBody('NET_CHA_BA_INFO');
  P.writeInt32(body, 'NET_CHA_BA_INFO', 'nChaNum', chaNum);
  return { type: P.E.NET_MSG_REQ_CHA_BINFO, body };
}

// SCHARINFO_LOBBY (GLContrlBaseMsg.h:228) begins DWORD m_dwCharID then
// char m_szName[CHAR_SZNAME]. Those two are at fixed offsets 0 and 4 whatever
// the struct packing is, which is all the MVP needs. The remaining fields
// (class, level, stats, equipped items, save map) need the real layout and are
// left alone rather than guessed at.
function parseCharInfo(body) {
  const charId = body.readUInt32LE(0);
  const rest = body.subarray(4);
  const nul = rest.indexOf(0);
  return { charId, name: rest.toString('latin1', 0, nul === -1 ? 0 : nul) };
}

// NET_GAME_JOIN as NET_MSG_LOBBY_GAME_JOIN — "enter the world as this character".
function buildGameJoin(chaNum) {
  const body = P.allocBody('NET_GAME_JOIN');
  P.writeInt32(body, 'NET_GAME_JOIN', 'nChaNum', chaNum);
  return { type: P.E.NET_MSG_LOBBY_GAME_JOIN, body };
}

// NET_GAME_JOIN_FIELD_IDENTITY — first packet on the SECOND socket, to the
// field server (s_NetClientMsg.cpp:113-121). Echoes the GaeaID and slot the
// Agent just handed us, plus the crypt key.
//
// The field socket DOES use the garbage layer. ConnectFieldServer passes
// NET_STATE_FIELD (s_NetClient.cpp:398) and both Send and SendNormal add
// garbage whenever the state is anything other than NET_STATE_LOGIN
// (s_NetClient.cpp:919, :971 — the variant that would have exempted FIELD is
// commented out). This matters: the field server reads client slots with
// bClient=TRUE (s_CFieldServerThread.cpp:480), and a packet with no recognised
// filler gets the source IP firewall-blocked, not merely dropped.
function buildFieldIdentity(info) {
  const body = P.allocBody('NET_GAME_JOIN_FIELD_IDENTITY');
  P.writeInt32(body, 'NET_GAME_JOIN_FIELD_IDENTITY', 'emType', info.emType);
  P.writeInt32(body, 'NET_GAME_JOIN_FIELD_IDENTITY', 'dwGaeaID', info.gaeaId);
  P.writeInt32(body, 'NET_GAME_JOIN_FIELD_IDENTITY', 'dwSlotFieldAgent', info.slot);
  // MsgCryptKey hardcodes both halves to 1 regardless of what the server sent
  // (s_NetClientMsg.cpp:130-139).
  const dirOff = P.fieldOff('NET_GAME_JOIN_FIELD_IDENTITY', 'ck.nKeyDirection');
  const keyOff = P.fieldOff('NET_GAME_JOIN_FIELD_IDENTITY', 'ck.nKey');
  body.writeUInt16LE(1, dirOff);
  body.writeUInt16LE(1, keyOff);
  return { type: P.E.NET_MSG_JOIN_FIELD_IDENTITY, body };
}

// GLMSG::SNETPC_GOTO — destination-based movement (GLContrlPcMsg.h:734),
// #pragma pack(1). Sent on the FIELD socket.
//
// The server validates it in GLChar::MsgGoto (GLCharMsg.cpp:254-275):
//   fDist = |server_pos - vCurPos|;  if (fDist > 60.0f) -> REJECTED
// A rejection is not silent: the server answers SNET_GM_MOVE2GATE_FB and
// broadcasts SNETPC_JUMP_POS_BRD to snap the client back. That 60-unit gate is
// the hard limit on how far client-side prediction may drift before a
// correction, which is exactly the number the plan's joystick tuning needs.
//
// EM_ACT_RUN = 0x1 (GLCharDefine.h:1173); the server only reads that one bit
// out of dwActState here.
const EM_ACT_RUN = 0x00000001;

function buildGoto(curPos, tarPos, actState = EM_ACT_RUN) {
  const body = P.allocBody('GLMSG::SNETPC_GOTO');
  const off = (f) => P.fieldOff('GLMSG::SNETPC_GOTO', f);
  body.writeUInt32LE(actState >>> 0, off('dwActState'));
  for (const [i, v] of curPos.entries()) body.writeFloatLE(v, off('vCurPos') + i * 4);
  for (const [i, v] of tarPos.entries()) body.writeFloatLE(v, off('vTarPos') + i * 4);
  return { type: P.E.NET_MSG_GCTRL_GOTO, body };
}

// Prefix of SNETLOBBY_CHARJOIN — the spawn. Everything past vPos is
// variable-length and not needed here.
function parseCharJoin(body) {
  const S = 'GLMSG::SNETLOBBY_CHARJOIN_PREFIX';
  const o = (f) => P.fieldOff(S, f);
  const idRaw = body.subarray(o('szUserID'), o('szUserID') + 21);
  const nul = idRaw.indexOf(0);
  return {
    userId: idRaw.toString('latin1', 0, nul === -1 ? 21 : nul),
    clientId: body.readUInt32LE(o('dwClientID')),
    gaeaId: body.readUInt32LE(o('dwGaeaID')),
    mapId: body.readUInt32LE(o('sMapID')),
    pos: [body.readFloatLE(o('vPos')),
          body.readFloatLE(o('vPos') + 4),
          body.readFloatLE(o('vPos') + 8)],
  };
}

// ---------------------------------------------------------------------------
// connection
// ---------------------------------------------------------------------------
class SpikeClient {
  constructor(opts, phase = 'login') {
    this.opts = opts;
    this.phase = phase;
    this.sock = null;
    this.rx = Buffer.alloc(0);
    // Garbage is OFF while in NET_STATE_LOGIN and ON from NET_STATE_AGENT
    // onward (s_NetClient.cpp:970 — `if (m_nClientNetState != NET_STATE_LOGIN)`).
    // The field socket is NET_STATE_FIELD, which is also != NET_STATE_LOGIN,
    // so it carries garbage exactly like the agent socket.
    const obfuscated = phase === 'agent' || phase === 'field';
    this.state = obfuscated ? phase.toUpperCase() : 'LOGIN';
    this.garbage = obfuscated ? new P.Garbage() : null;
    this.sawVersionReply = false;
    this.sawLoginReply = false;
    this.encryptKey = null;
  }

  get garbageActive() { return this.state !== 'LOGIN'; }

  // Two sockets are live at once from the field handoff onward (agent + field),
  // and both log here. Without a tag it is impossible to tell which connection
  // a message arrived on — which is exactly how the char-join traffic gets
  // mistaken for field traffic.
  get tag() { return this.phase.toUpperCase().padEnd(5); }

  send(pkt, label) {
    const wire = P.frame(pkt.type, pkt.body, this.garbageActive ? this.garbage : null);
    log(`${this.tag} --> ${label || nameOf(pkt.type)} type=${pkt.type} size=${wire.length}`);
    this.sock.write(wire);
  }

  connect() {
    return new Promise((resolve, reject) => {
      const { host, port, timeout } = this.opts;
      log(`connecting to ${host}:${port} ...`);

      this.sock = net.createConnection({ host, port });
      this.sock.setNoDelay(true);

      const timer = setTimeout(() => {
        reject(new Error(
          `timed out after ${timeout}ms with no response.\n` +
          `  Is ServerLogin running and listening on ${host}:${port}?`
        ));
        this.sock.destroy();
      }, timeout);

      this.sock.on('connect', () => {
        log('TCP established');
        this.onConnect();
      });

      this.sock.on('data', (chunk) => {
        this.rx = Buffer.concat([this.rx, chunk]);
        try {
          const { packets, rest } = P.parse(this.rx, this.garbageActive);
          this.rx = rest;
          for (const p of packets) this.onPacket(p);
          this.maybeSendLogin();
        } catch (err) {
          clearTimeout(timer);
          reject(err);
          this.sock.destroy();
          return;
        }
        // The handshake arrives as several packets (version echo, then the
        // encryption key). Once anything meaningful lands, wait a short settle
        // window to collect the rest rather than resolving on the first one.
        const goal = this.opts.versionOnly ? this.sawVersionReply
                   : this.phase === 'field' ? this.sawFieldData
                   : this.phase === 'agent' ? this.lobbyDone
                   : this.sawServerList;
        if (goal && !this.settling) {
          // Clear the connect watchdog HERE, not when the settle window ends.
          // It exists to catch "server never answered"; once the goal is met it
          // has done its job. Leaving it armed makes it race the observe window
          // and abort a perfectly good session (field: 8s settle vs 15s timer).
          clearTimeout(timer);
          // The field server streams spawn + nearby-entity traffic for a while
          // after the join, so give it a longer window to be observed.
          const settleMs = this.phase === 'field' ? this.opts.observeMs : 1500;
          log(`    (goal reached — observing ${settleMs}ms)`);
          this.settling = setTimeout(() => resolve(this), settleMs);
        }
      });

      this.sock.on('error', (err) => {
        clearTimeout(timer);
        reject(new Error(
          `socket error: ${err.message}\n` +
          `  ECONNREFUSED usually means ServerLogin is not running.`
        ));
      });

      this.sock.on('close', () => log('connection closed'));
    });
  }

  onConnect() {
    const { gameVer, patchVer, versionOnly, user, pass, channel } = this.opts;

    if (this.phase === 'field') {
      // MsgJoinInfoFromClient rejects us SILENTLY if the Agent has not yet
      // registered this GaeaID against the slot on the field server
      // (s_CFieldServerMsg.cpp:155 — `GetGaeaID(slot) != dwGaeaID` -> return).
      // The Agent does that registration asynchronously via a DB job, so a
      // client that connects instantly can lose the race. The real client is
      // busy loading a map here, which hides the problem.
      const delay = this.opts.fieldDelayMs;
      log(`    waiting ${delay}ms for agent->field registration, then identifying`);
      setTimeout(() => {
        if (this.sock.destroyed) return;
        this.send(buildFieldIdentity(this.opts.fieldInfo),
                  'NET_GAME_JOIN_FIELD_IDENTITY');
      }, delay);
      return;
    }

    if (this.phase === 'agent') {
      // Do NOT send credentials yet.
      //
      // The Agent pushes NET_MSG_SND_CRYT_KEY unprompted (same pattern as the
      // Login server pushing NET_MSG_SND_ENCRYPT_KEY). CNetClient tracks that
      // with m_bKeyReceived, exposed via IsKeyReceived() — which has no callers
      // in the network layer, so the UI polls it to gate the login button.
      //
      // Sending login before the key arrives is what we did first, and the
      // Agent answered with the key and then ignored us.
      log('    waiting for crypt key before sending credentials...');
      return;
    }

    this.send(buildVersion(gameVer, patchVer), 'NET_CLIENT_VERSION');

    // --version-only stops here. Useful as a credential-free framing check:
    // if the server answers this at all, the header layout, byte order, and
    // dwSize accounting are all correct.
    if (versionOnly) {
      log('    (--version-only: not sending further requests)');
      return;
    }

    // ServerLogin's dispatch table (s_CLoginServerMsg.cpp:27-39) handles exactly
    // three client messages: VERSION_INFO, HEARTBEAT_CLIENT_ANS, REQ_GAME_SVR.
    // Credentials are NOT among them — THAI_NET_MSG_LOGIN is dispatched by
    // CAgentServer (s_CAgentServerMsg.cpp:65). Sending login here hits the
    // `default: break` and is silently dropped, which is exactly what we saw.
    //
    // So: ask Login for the server list, then take credentials to the Agent.
    this.send(buildReqGameSvr(), 'NET_MSG_REQ_GAME_SVR');
  }

  // Fires once, after a whole receive batch has been processed, so both the
  // crypt key and the random-pass slot from the same NET_MSG_COMPRESS wrapper
  // are known before the login packet is built.
  maybeSendLogin() {
    if (this.phase !== 'agent' || this.loginSent || !this.keyReceived) return;
    this.loginSent = true;

    const { user, pass, channel, service } = this.opts;
    if (service === 'thai') {
      const mode = this.opts.pwMode || 'plain';
      log(`    service=thai pwMode=${mode}`);
      this.send(buildThaiLogin(user, pass, channel, mode), 'THAI_NET_LOGIN_DATA');
    } else {
      const mode = this.opts.pwMode || 'plain';
      log(`    service=china pwMode=${mode} (empty random pass)`);
      this.send(buildChinaLogin(user, pass, channel, mode), 'CHINA_NET_LOGIN_DATA');
    }
  }

  onPacket(p) {
    log(`${this.tag} <-- ${nameOf(p.type)} type=${p.type} size=${p.size}` +
        (p.garbage ? ` garbage="${p.garbage}"` : ''));
    if (p.body.length) log(`${this.tag}     ${hexDump(p.body)}`);
    if (this.phase === 'field') {
      this.sawFieldData = true;
      this.fieldTypes = this.fieldTypes || new Map();
      this.fieldTypes.set(p.type, (this.fieldTypes.get(p.type) || 0) + 1);
    }

    switch (p.type) {
      case P.E.NET_MSG_VERSION_OK:
        log('    version accepted');
        this.sawVersionReply = true;
        break;

      // Observed against the live server: it answers our VERSION_INFO with its
      // OWN VERSION_INFO carrying (patch, game) rather than VERSION_OK.
      case P.E.NET_MSG_VERSION_INFO: {
        const off = P.fieldOff('NET_CLIENT_VERSION', 'nPatchProgramVer');
        const patch = p.body.readInt32LE(off);
        const game = p.body.readInt32LE(off + 4);
        log(`    server version: patch=${patch} game=${game}`);
        if (patch !== this.opts.patchVer || game !== this.opts.gameVer) {
          warn(`   version mismatch — rerun with --patch-ver ${patch} --game-ver ${game}`);
        }
        this.sawVersionReply = true;
        break;
      }

      case P.E.NET_MSG_VERSION_REQ:
        warn('   server wants a different client version — check cVer.bin / --game-ver');
        this.sawVersionReply = true;
        break;

      // Pushed unprompted after the version exchange. This is the TEA key for
      // credentials and heartbeats (NET_ENCRYPT_KEY, szEncryptKey[13]).
      case P.E.NET_MSG_SND_ENCRYPT_KEY: {
        const off = P.fieldOff('NET_ENCRYPT_KEY', 'szEncryptKey');
        const size = P.S.NET_ENCRYPT_KEY.fields.szEncryptKey.size;
        const raw = p.body.subarray(off, off + size);
        const nul = raw.indexOf(0);
        this.encryptKey = raw.toString('latin1', 0, nul === -1 ? size : nul);
        log(`    *** encryption key received (${this.encryptKey.length} chars) ***`);
        this.sawVersionReply = true;
        break;
      }

      case P.E.NET_MSG_SND_GAME_SVR: {
        const g = (f) => P.S.NET_CUR_INFO_LOGIN.fields[f].off - P.HEADER_SIZE;
        const ipRaw = p.body.subarray(g('gscil.szServerIP'),
                                      g('gscil.szServerIP') + 21);
        const nul = ipRaw.indexOf(0);
        const entry = {
          ip: ipRaw.toString('latin1', 0, nul === -1 ? 21 : nul),
          port: p.body.readInt32LE(g('gscil.nServicePort')),
          group: p.body.readInt32LE(g('gscil.nServerGroup')),
          number: p.body.readInt32LE(g('gscil.nServerNumber')),
          cur: p.body.readInt32LE(g('gscil.nServerCurrentClient')),
          max: p.body.readInt32LE(g('gscil.nServerMaxClient')),
          pk: p.body[g('gscil.bPK')] !== 0,
        };
        (this.servers = this.servers || []).push(entry);
        log(`    *** AGENT SERVER: ${entry.ip}:${entry.port} ` +
            `group=${entry.group} num=${entry.number} ` +
            `clients=${entry.cur}/${entry.max} pk=${entry.pk} ***`);
        break;
      }

      case P.E.NET_MSG_SND_GAME_SVR_END:
        log(`    *** SERVER LIST END (${(this.servers || []).length} entries) ***`);
        this.sawServerList = true;
        break;

      // The Agent replies with NET_MSG_LOGIN_FB, NOT THAI_NET_MSG_LOGIN_FB —
      // ThaiMsgLogin builds NET_LOGIN_FEEDBACK_DATA and sets
      // nlfd.nmg.nType = NET_MSG_LOGIN_FB (s_CAgentServerMsgLogin.cpp:57,76).
      // The THAI_* feedback constant exists but this path never uses it.
      case P.E.NET_MSG_LOGIN_FB:
      case P.E.CHINA_NET_MSG_LOGIN_FB:
      case P.E.THAI_NET_MSG_LOGIN_FB: {
        // nResult is a USHORT after szDaumGID, not at body offset 0.
        const off = P.fieldOff('NET_LOGIN_FEEDBACK_DATA', 'nResult');
        const result = p.body.length > off + 1 ? p.body.readUInt16LE(off) : null;
        const name = P.LOGIN_FB[String(result)] || 'unmapped';
        log(`    *** LOGIN FEEDBACK — nResult=${result} (${name}) ***`);
        this.loginResult = result;
        this.sawLoginReply = true;
        if (result === 0) {
          log('    requesting character list');
          this.send(buildReqChaBaInfo(), 'NET_CHA_REQ_BA_INFO');
        } else {
          this.lobbyDone = true; // nothing more to do on a rejected login
        }
        break;
      }

      // Batching wrapper. Despite the name, bCompress=false means the payload
      // is simply several messages concatenated — no LZO involved. Unwrap and
      // dispatch each one so the inner types are visible.
      case P.E.NET_MSG_COMPRESS: {
        const compressed = p.body[0] !== 0;
        let inner = p.body.subarray(P.S.NET_COMPRESS.size - P.HEADER_SIZE);
        log(`    wrapper: bCompress=${compressed}, ${inner.length} bytes payload`);
        if (compressed) {
          // Raw LZO1X-1, no length prefix. The server decompresses into a
          // NET_DATA_BUFSIZE buffer (RcvMsgBuffer.cpp:113); we do the same.
          try {
            inner = lzo.lzo1xDecompress(inner, P.C.NET_DATA_BUFSIZE);
            log(`    LZO decompressed -> ${inner.length} bytes`);
          } catch (err) {
            warn(`   LZO decompress failed: ${err.message}`);
            break;
          }
        }
        const { packets } = P.parse(inner, false);
        log(`    unwrapped ${packets.length} message(s):`);
        for (const q of packets) this.onPacket(q);
        break;
      }

      case P.E.NET_MSG_SND_CRYT_KEY:
        // MsgCryptKey ignores the received values and hardcodes nKey=1,
        // nKeyDirection=1 (s_NetClientMsg.cpp:130-139), then sets m_bKeyReceived.
        // Do NOT send login from here: the Agent batches SND_CRYT_KEY and
        // RANDOM_NUM into one NET_MSG_COMPRESS wrapper, and the random number
        // has to be recorded before the login is built. maybeSendLogin() runs
        // once the whole batch has been processed.
        this.keyReceived = true;
        log('    crypt key received — credentials now unblocked');
        break;

      // The account's character slots. nChaSNum entries are valid; the rest of
      // the fixed nChaNum[MAX_ONESERVERCHAR_NUM] array is padding.
      case P.E.NET_MSG_CHA_BAINFO: {
        const countOff = P.fieldOff('NET_CHA_BBA_INFO', 'nChaSNum');
        const listOff = P.fieldOff('NET_CHA_BBA_INFO', 'nChaNum');
        const declared = p.body.readInt32LE(countOff);
        const cap = P.C.MAX_ONESERVERCHAR_NUM;
        const count = Math.max(0, Math.min(declared, cap));
        if (declared !== count) {
          warn(`   nChaSNum=${declared} outside 0..${cap}, clamping`);
        }
        this.charNums = [];
        for (let i = 0; i < count; i++) {
          const n = p.body.readInt32LE(listOff + i * 4);
          if (n > 0) this.charNums.push(n);
        }
        log(`    *** CHARACTER SLOTS: ${this.charNums.length} ` +
            `[${this.charNums.join(', ')}] ***`);
        this.chars = [];
        if (this.charNums.length === 0) {
          log('    account has no characters — nothing to select');
          this.lobbyDone = true;
        } else {
          for (const n of this.charNums) {
            this.send(buildReqChaBInfo(n), `NET_MSG_REQ_CHA_BINFO(${n})`);
          }
        }
        break;
      }

      case P.E.NET_MSG_LOBBY_CHAR_SEL: {
        const info = parseCharInfo(p.body);
        this.chars.push(info);
        log(`    *** CHARACTER: id=${info.charId} name="${info.name}" ` +
            `(${p.body.length}b record) ***`);
        if (this.chars.length >= (this.charNums || []).length) {
          if (this.opts.join) {
            const pick = this.charNums[0];
            log(`    joining world as character ${pick}`);
            this.send(buildGameJoin(pick), 'NET_GAME_JOIN');
          } else {
            log('    --no-join: stopping at the character list');
            this.lobbyDone = true;
          }
        }
        break;
      }

      // Agent's answer to the game-join: where the field server is, plus the
      // GaeaID that identifies us on it.
      case P.E.NET_MSG_CONNECT_CLIENT_FIELD: {
        const g = (f) => P.fieldOff('NET_CONNECT_CLIENT_TO_FIELD', f);
        const ipSize = P.S.NET_CONNECT_CLIENT_TO_FIELD.fields.szServerIP.size;
        const ipRaw = p.body.subarray(g('szServerIP'), g('szServerIP') + ipSize);
        const nul = ipRaw.indexOf(0);
        this.fieldInfo = {
          emType: p.body.readInt32LE(g('emType')),
          gaeaId: p.body.readUInt32LE(g('dwGaeaID')),
          slot: p.body.readUInt32LE(g('dwSlotFieldAgent')),
          port: p.body.readInt32LE(g('nServicePort')),
          ip: ipRaw.toString('latin1', 0, nul === -1 ? ipSize : nul),
        };
        log(`    *** FIELD HANDOFF: ${this.fieldInfo.ip}:${this.fieldInfo.port} ` +
            `gaeaID=${this.fieldInfo.gaeaId} slot=${this.fieldInfo.slot} ` +
            `type=${this.fieldInfo.emType} ***`);
        this.lobbyDone = true;
        break;
      }

      // Our own spawn, relayed from the field server through the Agent.
      case P.E.NET_MSG_LOBBY_CHAR_JOIN: {
        this.spawn = parseCharJoin(p.body);
        const [x, y, z] = this.spawn.pos;
        log(`    *** SPAWN: "${this.spawn.userId}" gaeaID=${this.spawn.gaeaId} ` +
            `map=${this.spawn.mapId} pos=(${x.toFixed(1)}, ${y.toFixed(1)}, ${z.toFixed(1)}) ***`);
        break;
      }

      // Movement acknowledgement / rejection. Both come back on the agent
      // socket. GOTO_BRD means the server accepted and is broadcasting the
      // move; JUMP_POS_BRD + GM_MOVE2GATE_FB mean it rejected us as too far
      // out of sync and is snapping the position back (GLCharMsg.cpp:256-274).
      case A.NET_MSG_GCTRL_GOTO_BRD:
        this.gotoBrd = (this.gotoBrd || 0) + 1;
        log('    *** GOTO BROADCAST — server accepted the move ***');
        break;

      // Sent by MsgSendUpdateState at the end of a successful MsgGoto
      // (GLCharMsg.cpp:308) — the practical acknowledgement that a move landed.
      case A.NET_MSG_GCTRL_UPDATE_STATE:
        this.updateState = (this.updateState || 0) + 1;
        break;

      case A.NET_MSG_GCTRL_JUMP_POS_BRD:
      case A.NET_MSG_GM_MOVE2GATE_FB: {
        this.snapBack = (this.snapBack || 0) + 1;
        let where = '';
        if (p.body.length >= 12) {
          this.serverPos = [0, 4, 8].map((o) => p.body.readFloatLE(o));
          where = ` -> server says we are at ` +
                  `(${this.serverPos.map((v) => v.toFixed(1)).join(', ')})`;
        }
        log(`    *** POSITION SNAP-BACK — move rejected as out-of-sync${where} ***`);
        break;
      }

      // The server pings; the client must answer or the session is reaped.
      case P.E.NET_MSG_HEARTBEAT_CLIENT_REQ:
        log('    heartbeat request — answering');
        this.send(buildHeartbeatAns(this.encryptKey), 'NET_MSG_HEARTBEAT_CLIENT_ANS');
        break;

      // MsgSndRandomNumber (s_CAgentServerMsg.cpp:560). In the China flow the
      // server picks slot N and the client is expected to supply the matching
      // segment of a secondary "random password". The PC client sends an EMPTY
      // one — LoginPage.cpp:199 declares strRP and the block that would fill it
      // is commented out — so an account with no secondary password set works.
      case P.E.NET_MSG_RANDOM_NUM: {
        const off = P.fieldOff('NET_RANDOMPASS_NUMBER', 'nRandomNumber');
        this.randomNum = p.body.readInt32LE(off);
        log(`    random pass slot = ${this.randomNum}`);
        break;
      }

      default:
        // Unknown types are expected this early; the point is to observe them.
        break;
    }
  }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
async function main() {
  let opts;
  try {
    opts = parseArgs(process.argv);
  } catch (err) {
    console.error(err.message);
    process.exit(2);
  }

  if (opts.help) {
    console.log(`
RAN EP9 Phase 0 protocol spike

  node spike.js --user USER --pass PASS [options]

  --host H         default 127.0.0.1
  --port N         default 12004 (ServerLogin service port, from cfg/)
  --user U         account id       (required)
  --pass P         account password (required)

  Credentials may instead come from MOBILE/spike/local.json (git-ignored) or
  RAN_SPIKE_USER / RAN_SPIKE_PASS, so they stay off the command line.

  --channel N      default 0
  --game-ver N     default 1  (matches cfg server_version)
  --patch-ver N    default 1  (matches cfg patch_version)
  --timeout MS     default 15000
  --allow-remote   permit a non-loopback --host
`);
    process.exit(0);
  }

  log(`layout.json: ${Object.keys(P.S).length} structs, ` +
      `${Object.keys(P.E).length} enums (probe-generated)`);
  log(`service=${opts.service} ` +
      (opts.service === 'thai'
        ? `THAI_NET_MSG_LOGIN=${P.E.THAI_NET_MSG_LOGIN} ` +
          `sizeof=${P.S.THAI_NET_LOGIN_DATA.size}`
        : `CHINA_NET_MSG_LOGIN=${P.E.CHINA_NET_MSG_LOGIN} ` +
          `sizeof=${P.S.CHINA_NET_LOGIN_DATA.size}`) +
      ` NET_MSG_LOBBY=${P.C.NET_MSG_LOBBY}`);

  if (!LOOPBACK.has(opts.host) && !opts.allowRemote) {
    console.error(
      `\nRefusing to connect to non-loopback host "${opts.host}" without --allow-remote.\n\n` +
      `The cfg/ files in this repo point at a live production IP. A half-built\n` +
      `protocol client should be pointed at a local test server, not at real\n` +
      `players. Re-run with --allow-remote if you genuinely mean to.\n`
    );
    process.exit(2);
  }
  if (!opts.versionOnly && (!opts.user || !opts.pass)) {
    console.error(
      '--user and --pass are required (use --help), or run --version-only\n' +
      'for a credential-free framing check.'
    );
    process.exit(2);
  }

  // Don't let an unedited local.json fire a junk login at a live server —
  // failed attempts are recorded in RanLog.
  const isPlaceholder = (s) => /^PUT_.*_HERE$/.test(s);
  if (!opts.versionOnly && (isPlaceholder(opts.user) || isPlaceholder(opts.pass))) {
    console.error(
      '\nlocal.json still has placeholder credentials.\n' +
      'Edit MOBILE/spike/local.json and replace PUT_ACCOUNT_HERE / PUT_PASSWORD_HERE.\n'
    );
    process.exit(2);
  }

  if (!LOOPBACK.has(opts.host)) {
    log(`NOTE: ${opts.host} is remote. Footprint is one connection, two packets ` +
        `(version + login), then a clean disconnect.`);
  }

  // ---- Phase 1: ServerLogin — version + server list ----
  const loginClient = new SpikeClient(opts, 'login');
  try {
    await loginClient.connect();
  } catch (err) {
    console.error(`\n${ts()} FAILED (login phase): ${err.message}\n`);
    if (loginClient.sock) loginClient.sock.end();
    process.exit(1);
  }
  loginClient.sock.end();

  if (opts.versionOnly) {
    log('done (--version-only)');
    process.exit(0);
  }

  const agent = (loginClient.servers || [])[0];
  if (!agent) {
    console.error('\nno agent server in the list — cannot continue\n');
    process.exit(1);
  }

  // ---- Phase 2: ServerAgent — credentials ----
  log(`--- switching to agent ${agent.ip}:${agent.port} (garbage layer ON) ---`);
  const agentClient = new SpikeClient(
    { ...opts, host: agent.ip, port: agent.port }, 'agent');
  try {
    await agentClient.connect();
  } catch (err) {
    console.error(`\n${ts()} FAILED (agent phase): ${err.message}\n`);
    if (agentClient.sock) agentClient.sock.end();
    process.exit(1);
  }

  if (agentClient.loginResult !== 0) {
    console.error(`\nlogin rejected (nResult=${agentClient.loginResult}) — stopping\n`);
    agentClient.sock.end();
    process.exit(1);
  }
  log('*** MILESTONE 1: login accepted ***');
  log(`*** MILESTONE 2: ${(agentClient.chars || []).length} character(s) listed ***`);

  if (!agentClient.fieldInfo) {
    log('no field handoff (no characters, or --no-join) — stopping here');
    agentClient.sock.end();
    process.exit(0);
  }

  // ---- Phase 3: ServerField — a SECOND socket, agent stays open ----
  //
  // The real client keeps both connections alive: the agent socket continues to
  // carry lobby/account traffic while the field socket carries gameplay. Closing
  // the agent here would look like a disconnect to the server.
  log(`--- connecting to field ${agentClient.fieldInfo.ip}:${agentClient.fieldInfo.port} ---`);
  const fieldClient = new SpikeClient(
    { ...opts,
      host: agentClient.fieldInfo.ip,
      port: agentClient.fieldInfo.port,
      fieldInfo: agentClient.fieldInfo },
    'field');
  try {
    await fieldClient.connect();
    log('*** MILESTONE 3: field server accepted the join ***');
    const types = fieldClient.fieldTypes || new Map();
    const total = [...types.values()].reduce((a, b) => a + b, 0);
    log(`    field traffic: ${total} message(s), ${types.size} distinct type(s)`);
    for (const [t, n] of [...types.entries()].sort((a, b) => b[1] - a[1])) {
      log(`      ${String(n).padStart(4)} x  ${nameOf(t)} (${t})`);
    }

    // ---- Movement ----
    //
    // Sent on the FIELD socket; the acknowledging broadcast comes back on the
    // AGENT socket, because field->client traffic is relayed by the Agent
    // (CFieldServer::SendAgent, s_CFieldServer.cpp:678).
    if (!agentClient.spawn) {
      warn('   no spawn position received — skipping the movement test');
    } else {
      const from = agentClient.spawn.pos;
      const fmt = (p) => `(${p.map((v) => v.toFixed(1)).join(', ')})`;

      // ---- Leave the loading state ----
      //
      // GLChar::CreateChar puts a freshly-joined character into EM_ACT_WAITING
      // (GLChar.cpp:598), and while that bit is set IsValidBody() is FALSE
      // (GLChar.cpp:698) — the character is present but not yet "in play".
      //
      // The PC client clears it only after its map has finished loading: once
      // its own wait counter expires it sends SNETREQ_LANDIN then SNETREQ_READY
      // (GLCharacter.cpp:4170-4177), and the server's MsgReady does
      // ReSetSTATE(EM_ACT_WAITING) (GLCharMsg.cpp:169-174).
      //
      // Both go on the AGENT socket — they are sent with NETSEND, not
      // NETSENDTOFIELD — even though they are GCTRL-range messages.
      //
      // Nothing prompts the client to do this; there is no server-side nag. A
      // headless client that never loads a map simply never sends it, and then
      // sits in the world unable to act, which is exactly what we were seeing.
      log('--- leaving EM_ACT_WAITING: LANDIN + READY (agent socket) ---');
      agentClient.send({ type: A.NET_MSG_GCTRL_REQ_LANDIN, body: Buffer.alloc(0) },
                       'GLMSG::SNETREQ_LANDIN');
      agentClient.send({ type: A.NET_MSG_GCTRL_REQ_READY, body: Buffer.alloc(0) },
                       'GLMSG::SNETREQ_READY');
      await new Promise((r) => setTimeout(r, 1500));

      // Step 1 — a legitimate move, well inside the 60-unit gate.
      //
      // Note what silence means here: SNETPC_GOTO_BRD goes out via
      // SendMsgViewAround (GLCharMsg.cpp:305), i.e. only to players in view.
      // On an empty map nobody hears it, so NOT receiving a broadcast is not
      // evidence the move failed. That is why step 2 exists.
      // Replies to movement can land on EITHER socket, so tally across both.
      // Lobby/spawn data is relayed through the Agent, but in-field responses
      // (UPDATE_STATE, GM_MOVE2GATE_FB) come straight back on the field socket.
      const both = [agentClient, fieldClient];
      const tally = (k) => both.reduce((n, c) => n + (c[k] || 0), 0);
      for (const c of both) { c.gotoBrd = 0; c.snapBack = 0; c.updateState = 0; }

      // Read the server's authoritative position by deliberately desyncing.
      // GLChar::MsgGoto answers SNET_GM_MOVE2GATE_FB carrying m_vPos whenever
      // |m_vPos - vCurPos| > 60 (GLCharMsg.cpp:256-266), and unlike the success
      // path it is sent straight to us rather than view-broadcast. That makes it
      // a position oracle — the only way to observe server-side state from here.
      // Side effect: it also forces GLAT_IDLE, which conveniently stops any
      // in-progress walk before the next measurement.
      const probePosition = async () => {
        const far = [from[0] + 5000, from[1], from[2]];
        for (const c of both) c.serverPos = null;
        fieldClient.send(buildGoto(far, [far[0] + 20, far[1], far[2]]),
                         'GLMSG::SNETPC_GOTO (position probe)');
        await new Promise((r) => setTimeout(r, 2500));
        return both.map((c) => c.serverPos).find(Boolean) || null;
      };

      // Step 1 — walk. Destination-based: one packet, no repeats needed.
      const to = [from[0] + 20, from[1], from[2]];
      log(`--- movement: ${fmt(from)} -> ${fmt(to)} ---`);
      fieldClient.send(buildGoto(from, to), 'GLMSG::SNETPC_GOTO');
      await new Promise((r) => setTimeout(r, 4000));

      // Step 2 — read the server's authoritative position back.
      const at = await probePosition();
      const moved = at ? Math.hypot(at[0] - from[0], at[2] - from[2]) : NaN;
      log(`    server position now ${at ? fmt(at) : 'unknown'}` +
          `  displacement ${Number.isNaN(moved) ? '?' : moved.toFixed(1)}`);

      const snaps = tally('snapBack');
      const updates = tally('updateState');
      log(`    replies: ${updates} update-state, ${snaps} snap-back`);

      if (moved > 0.5) {
        log('*** MILESTONE 3b: movement CONFIRMED — character relocated ***');
      } else if (snaps > 0) {
        warn('   packets validated but no relocation.');
        warn('   Check the LANDIN/READY handshake above actually went out —');
        warn('   without it the character stays in EM_ACT_WAITING and cannot act.');
      } else {
        warn('   no movement response at all — unconfirmed');
      }
    }
  } catch (err) {
    console.error(`\n${ts()} FAILED (field phase): ${err.message}\n`);
  } finally {
    if (fieldClient.sock) fieldClient.sock.end();
    agentClient.sock.end();
  }
  process.exit(0);
}

main();
