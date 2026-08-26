'use strict';
//
// Connection — one socket to one RAN server, with framing and reconnect.
//
// This is Phase 1 (permanent), not the Phase 0 spike. The spike proved the
// protocol; this is the layer a real client is built on. Differences that
// matter:
//
//   * reconnect/backoff is first-class, not an afterthought. plan.md calls this
//     out because mobile networks drop constantly — a client that treats a
//     dropped TCP connection as fatal is unusable on a train.
//   * no process.exit, no console logging policy baked in: it emits events.
//   * the outbound safety guard from the spike is kept, because the server
//     still firewall-bans IPs that send malformed packets.
//
const net = require('net');
const { EventEmitter } = require('events');
const P = require('../spike/protocol');
const lzo = require('../spike/lzo');

// NET_STATE_LOGIN sends bare; every later state carries the garbage filler
// (s_NetClient.cpp:919,971 — the variant exempting FIELD is commented out).
const OBFUSCATED = new Set(['agent', 'field']);

class Connection extends EventEmitter {
  /**
   * @param {object} opts
   * @param {string} opts.host
   * @param {number} opts.port
   * @param {'login'|'agent'|'field'} opts.role
   * @param {boolean} [opts.autoReconnect]
   */
  constructor(opts) {
    super();
    this.host = opts.host;
    this.port = opts.port;
    this.role = opts.role || 'login';
    this.autoReconnect = opts.autoReconnect !== false;

    // Backoff: exponential with full jitter, capped. Full jitter (rather than
    // a fixed sequence) matters when a server restarts and every client in the
    // field reconnects at once — without it they retry in lockstep and the
    // server gets a thundering herd on every wake-up.
    this.backoffBaseMs = opts.backoffBaseMs || 500;
    this.backoffMaxMs = opts.backoffMaxMs || 30000;
    this.maxAttempts = opts.maxAttempts || Infinity;

    this.sock = null;
    this.rx = Buffer.alloc(0);
    this.attempt = 0;
    this.closedByUs = false;
    this.garbage = OBFUSCATED.has(this.role) ? new P.Garbage() : null;
  }

  get obfuscated() { return this.garbage !== null; }

  nextDelay() {
    const ceiling = Math.min(this.backoffMaxMs,
                             this.backoffBaseMs * 2 ** this.attempt);
    return Math.floor(Math.random() * ceiling);
  }

  connect() {
    this.closedByUs = false;
    return new Promise((resolve, reject) => {
      const sock = net.createConnection({ host: this.host, port: this.port });
      this.sock = sock;
      sock.setNoDelay(true);

      const onError = (err) => {
        sock.removeListener('connect', onConnect);
        this.emit('error', err);
        reject(err);
      };
      const onConnect = () => {
        sock.removeListener('error', onError);
        this.attempt = 0;
        // A reconnect starts a fresh obfuscation stream: the server tracks the
        // last two fillers per connection, and a new socket is a new slot.
        if (this.obfuscated) this.garbage = new P.Garbage();
        this.rx = Buffer.alloc(0);
        this.emit('connect');
        resolve(this);
      };

      sock.once('error', onError);
      sock.once('connect', onConnect);
      sock.on('data', (chunk) => this._onData(chunk));
      sock.on('close', () => this._onClose());
    });
  }

  // Reconnect with backoff until connected or attempts are exhausted.
  async connectWithRetry() {
    for (;;) {
      try {
        return await this.connect();
      } catch (err) {
        this.attempt++;
        if (this.attempt >= this.maxAttempts) throw err;
        const delay = this.nextDelay();
        this.emit('reconnecting', { attempt: this.attempt, delay, error: err });
        await new Promise((r) => setTimeout(r, delay));
      }
    }
  }

  _onClose() {
    this.emit('close', { byUs: this.closedByUs });
    if (this.closedByUs || !this.autoReconnect) return;
    const delay = this.nextDelay();
    this.attempt++;
    this.emit('reconnecting', { attempt: this.attempt, delay, error: null });
    setTimeout(() => {
      if (!this.closedByUs) {
        this.connect().catch(() => { /* _onClose will schedule the next try */ });
      }
    }, delay);
  }

  _onData(chunk) {
    this.rx = Buffer.concat([this.rx, chunk]);
    let packets;
    try {
      ({ packets, rest: this.rx } = P.parse(this.rx, this.obfuscated));
    } catch (err) {
      // A desync means the stream can no longer be trusted. Surface it and
      // drop the socket rather than guessing at a resync point.
      this.emit('desync', err);
      this.destroy();
      return;
    }
    for (const p of packets) this._dispatch(p);
    this.emit('drain');
  }

  // NET_MSG_COMPRESS is two different things depending on bCompress: a batching
  // wrapper around concatenated messages, or an LZO1X payload. Unwrap either
  // and dispatch the contents, so consumers never see the wrapper.
  _dispatch(p) {
    if (p.type !== P.E.NET_MSG_COMPRESS) {
      this.emit('message', p);
      return;
    }
    const compressed = p.body[0] !== 0;
    let inner = p.body.subarray(P.S.NET_COMPRESS.size - P.HEADER_SIZE);
    if (compressed) {
      try {
        inner = lzo.lzo1xDecompress(inner, P.C.NET_DATA_BUFSIZE);
      } catch (err) {
        this.emit('error', new Error(`LZO decompress failed: ${err.message}`));
        return;
      }
    }
    let packets;
    try {
      ({ packets } = P.parse(inner, false));
    } catch (err) {
      this.emit('error', new Error(`bad wrapper payload: ${err.message}`));
      return;
    }
    for (const q of packets) this._dispatch(q);
  }

  send(type, body = Buffer.alloc(0)) {
    if (!this.sock || this.sock.destroyed) {
      throw new Error(`${this.role}: cannot send, socket is not open`);
    }
    // frame() applies the server's own reject rules to our output, so a bug
    // here raises locally instead of getting this IP firewall-banned.
    const wire = P.frame(type, body, this.garbage);
    this.sock.write(wire);
    this.emit('sent', { type, size: wire.length });
    return wire.length;
  }

  destroy() {
    this.closedByUs = true;
    if (this.sock) this.sock.destroy();
  }

  close() {
    this.closedByUs = true;
    if (this.sock) this.sock.end();
  }
}

module.exports = { Connection };
