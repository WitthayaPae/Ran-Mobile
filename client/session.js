'use strict';
//
// Session — the full login -> agent -> field state machine.
//
// Encodes the ordering constraints discovered in Phase 0. Each one produced a
// silent failure when violated, so they are asserted here rather than left to
// the caller to remember:
//
//   1. Credentials go to the AGENT, not the login server. The login server's
//      dispatch table drops them with `default: break` — no error, no close.
//   2. Wait for NET_MSG_SND_CRYT_KEY before sending credentials.
//   3. After the field handoff, keep BOTH sockets open. Lobby and spawn traffic
//      is relayed through the agent; only gameplay input goes to the field.
//   4. After the spawn, send LANDIN + READY on the AGENT socket, or the
//      character stays in EM_ACT_WAITING and can never act.
//
const { EventEmitter } = require('events');
const { Connection } = require('./connection');
const { World } = require('./world');
const M = require('./messages');
const P = require('../spike/protocol');

const E = P.E;
const A = P.LAYOUT.allEnums;

const LOOPBACK = new Set(['127.0.0.1', 'localhost', '::1']);

class Session extends EventEmitter {
  constructor(opts) {
    super();
    this.opts = {
      port: 12004,
      channel: 0,
      gameVer: 1,
      patchVer: 1,
      pwMode: 'plain',
      timeoutMs: 20000,
      ...opts,
    };
    if (!LOOPBACK.has(this.opts.host) && !this.opts.allowRemote) {
      throw new Error(
        `refusing non-loopback host "${this.opts.host}" without allowRemote: ` +
        `this points at a live production server`
      );
    }
    this.state = 'idle';
    this.servers = [];
    this.characters = [];
    this.spawn = null;
    this.position = null;
    this.conns = {};
    // Nearby-entity view, fed from every field/agent message.
    this.world = new World();
  }

  _setState(s) {
    this.state = s;
    this.emit('state', s);
  }

  // Resolve when `pred` is satisfied by an emitted event, else reject on timeout.
  _await(pred, what, ms = this.opts.timeoutMs) {
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.removeListener('progress', onProgress);
        reject(new Error(`timed out after ${ms}ms waiting for ${what}`));
      }, ms);
      const onProgress = () => {
        const v = pred();
        if (v) {
          clearTimeout(timer);
          this.removeListener('progress', onProgress);
          resolve(v);
        }
      };
      this.on('progress', onProgress);
      onProgress();
    });
  }

  _wire(conn, role) {
    conn.on('message', (p) => {
      this.emit('message', { role, ...p });
      // Raw frame accounting, before decode: a heartbeat that arrives and fails
      // to decode still has to be counted, or a decode regression would look
      // like a quiet server.
      this.emit('frame', { role, type: p.type, size: p.size !== undefined ? p.size
                                                  : (p.body ? p.body.length + 8 : 8) });
      this.world.handle(p);
      this._onMessage(role, p);
      this.emit('progress');
    });
    conn.on('reconnecting', (i) => this.emit('reconnecting', { role, ...i }));
    conn.on('desync', (e) => this.emit('error', e));
    conn.on('error', (e) => this.emit('error', e));
    return conn;
  }

  async run() {
    await this._loginPhase();
    await this._agentPhase();
    if (!this.characters.length) return this;
    await this._enterWorld();
    return this;
  }

  // ---- login server: version + server list ----
  async _loginPhase() {
    this._setState('login');
    const c = this._wire(new Connection({
      host: this.opts.host, port: this.opts.port,
      role: 'login', autoReconnect: false,
    }), 'login');
    this.conns.login = c;
    await c.connectWithRetry();

    c.send(...pkt(M.version(this.opts.gameVer, this.opts.patchVer)));
    c.send(...pkt(M.reqGameSvr()));
    await this._await(() => this.serverListDone, 'server list');
    c.close();

    if (!this.servers.length) throw new Error('server list was empty');
    return this.servers;
  }

  // ---- agent: credentials + character list ----
  async _agentPhase() {
    this._setState('agent');
    const target = this.servers[0];
    const c = this._wire(new Connection({
      host: target.ip, port: target.port, role: 'agent',
    }), 'agent');
    this.conns.agent = c;
    await c.connectWithRetry();

    // Constraint 2: the agent pushes the crypt key unprompted, and credentials
    // sent before it arrives are ignored.
    await this._await(() => this.keyReceived, 'crypt key');
    c.send(...pkt(M.chinaLogin(this.opts.user, this.opts.pass,
                               this.opts.channel, this.opts.pwMode)));

    const fb = await this._await(() => this.loginFeedback, 'login feedback');
    if (fb.result !== 0) {
      throw new Error(`login rejected: ${fb.result} (${fb.name})`);
    }
    this.emit('login', fb);

    c.send(...pkt(M.reqCharSlots()));
    const slots = await this._await(() => this.charSlots, 'character slots');
    if (!slots.length) return [];

    for (const n of slots) c.send(...pkt(M.reqCharInfo(n)));
    await this._await(() => this.characters.length >= slots.length &&
                            this.characters, 'character list');
    this.emit('characters', this.characters);
    return this.characters;
  }

  // ---- field: join, spawn, ready ----
  async _enterWorld(charIndex = 0) {
    this._setState('joining');
    const agent = this.conns.agent;
    const chaNum = this.charSlots[charIndex];
    agent.send(...pkt(M.gameJoin(chaNum)));

    const handoff = await this._await(() => this.fieldHandoff, 'field handoff');
    this.emit('handoff', handoff);

    // Constraint 3: the agent socket stays open.
    const f = this._wire(new Connection({
      host: handoff.ip, port: handoff.port, role: 'field',
    }), 'field');
    this.conns.field = f;
    await f.connectWithRetry();

    // MsgJoinInfoFromClient rejects us silently if the agent has not finished
    // registering this GaeaID against the slot, so give that a moment.
    await sleep(this.opts.fieldDelayMs ?? 1000);
    f.send(...pkt(M.fieldIdentity(handoff)));

    const spawn = await this._await(() => this.spawn, 'spawn');
    this.position = spawn.pos.slice();
    this.emit('spawn', spawn);

    // Constraint 4 — without this the character can never act.
    agent.send(...pkt(M.landIn()));
    agent.send(...pkt(M.ready()));
    this.ready = true;
    this._setState('playing');
    this.emit('ready', spawn);
    return spawn;
  }

  /**
   * Walk to a destination. Destination-based: one packet, no repeats.
   * Keeps the locally-tracked position in step so the next request stays
   * inside the server's 60-unit desync budget.
   */
  moveTo(target) {
    if (this.state !== 'playing') {
      throw new Error(`moveTo requires state "playing", got "${this.state}"`);
    }
    const from = this.position;
    const d = Math.hypot(target[0] - from[0], target[2] - from[2]);
    if (d > 60) {
      this.emit('warn', `move of ${d.toFixed(1)} units exceeds the server's ` +
                        `60-unit gate and will be rejected`);
    }
    this.conns.field.send(...pkt(M.goto(from, target)));
    this.position = target.slice();
    return d;
  }

  /**
   * Read the server's authoritative position. Works by deliberately claiming a
   * far-off vCurPos: MsgGoto answers SNET_GM_MOVE2GATE_FB with its real m_vPos
   * whenever the claim is more than 60 units out, and sends it directly to us
   * rather than view-broadcasting. There is no polite "where am I" message.
   * Side effect: forces GLAT_IDLE, stopping any walk in progress.
   */
  async syncPosition() {
    const far = [this.position[0] + 5000, this.position[1], this.position[2]];
    this.serverPos = null;
    this.conns.field.send(...pkt(M.goto(far, [far[0] + 20, far[1], far[2]])));
    const pos = await this._await(() => this.serverPos, 'position sync', 6000);
    this.position = pos.slice();
    return pos;
  }

  _onMessage(role, p) {
    switch (p.type) {
      case E.NET_MSG_SND_GAME_SVR:
        this.servers.push(M.parseServerEntry(p.body));
        break;
      case E.NET_MSG_SND_GAME_SVR_END:
        this.serverListDone = true;
        break;
      case E.NET_MSG_SND_CRYT_KEY:
        this.keyReceived = true;
        break;
      case E.NET_MSG_LOGIN_FB:
      case E.CHINA_NET_MSG_LOGIN_FB:
        this.loginFeedback = M.parseLoginFeedback(p.body);
        break;
      case E.NET_MSG_CHA_BAINFO:
        this.charSlots = M.parseCharSlots(p.body);
        if (!this.charSlots.length) this.charSlots = [];
        break;
      case E.NET_MSG_LOBBY_CHAR_SEL:
        this.characters.push(M.parseCharSummary(p.body));
        break;
      case E.NET_MSG_CONNECT_CLIENT_FIELD:
        this.fieldHandoff = M.parseFieldHandoff(p.body);
        break;
      case E.NET_MSG_LOBBY_CHAR_JOIN:
        this.spawn = M.parseSpawn(p.body);
        break;
      case A.NET_MSG_GM_MOVE2GATE_FB:
      case A.NET_MSG_GCTRL_JUMP_POS_BRD:
        if (p.body.length >= 12) {
          this.serverPos = [0, 4, 8].map((o) => p.body.readFloatLE(o));
          this.emit('snapback', this.serverPos);
        }
        break;
      case E.NET_MSG_HEARTBEAT_CLIENT_REQ:
        // Unanswered heartbeats get the session reaped.
        this.conns[role].send(E.NET_MSG_HEARTBEAT_CLIENT_ANS,
                              P.allocBody('NET_HEARTBEAT_CLIENT_ANS'));
        // Emitted so a soak run can measure the INTERVAL, not just the count.
        // A stall shows up as a widening gap well before it shows up as a
        // disconnect, which is the whole point of watching it.
        this.emit('heartbeat', { role, at: Date.now() });
        break;
      default:
        break;
    }
  }

  close() {
    for (const c of Object.values(this.conns)) c.close();
    this._setState('closed');
  }
}

const pkt = (m) => [m.type, m.body];
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

module.exports = { Session };
