'use strict';
//
// World — client-side view of nearby entities.
//
// Which messages matter here was decided by measurement, not by reading the
// 1615-message enum: `census.js` sits still in the field and reports what the
// server actually sends. The entity lifecycle is three messages:
//
//   NET_MSG_GCTRL_DROP_CROW   entity enters view   (SNETDROP_CROW)
//   NET_MSG_GCTRL_CROW_MOVETO entity moves         (SNETCROW_MOVETO)
//   NET_MSG_GCTRL_DROP_OUT    entities leave view  (SNETDROP_OUT, batched)
//
// Idle traffic in an empty area is ~885 B/s, dominated by DROP_CROW at ~484
// bytes each. That number is the budget a mobile client has to live inside on
// a metered connection, so it is worth keeping an eye on.
//
const { EventEmitter } = require('events');
const P = require('../spike/protocol');

const A = P.LAYOUT.allEnums;
const S = P.LAYOUT.allStructs;

// Absolute offset of a field inside a payload struct nested in a message.
// Both halves are compiler-measured; only the addition happens here.
function nested(msg, member, payload, field) {
  const outer = S[msg].fields[member].off;
  const inner = S[payload].fields[field].off;
  return outer + inner - P.HEADER_SIZE; // body-relative
}

const DROP = {
  nativeId: nested('GLMSG::SNETDROP_CROW', 'Data', 'SDROP_CROW', 'sNativeID'),
  globId: nested('GLMSG::SNETDROP_CROW', 'Data', 'SDROP_CROW', 'dwGlobID'),
  pos: nested('GLMSG::SNETDROP_CROW', 'Data', 'SDROP_CROW', 'vPos'),
  hp: nested('GLMSG::SNETDROP_CROW', 'Data', 'SDROP_CROW', 'dwNowHP'),
};

const MOVE = {
  globId: P.fieldOff('GLMSG::SNETCROW_MOVETO', 'dwGlobID'),
  cur: P.fieldOff('GLMSG::SNETCROW_MOVETO', 'vCurPos'),
  tar: P.fieldOff('GLMSG::SNETCROW_MOVETO', 'vTarPos'),
};

const OUT = {
  num: P.fieldOff('GLMSG::SNETDROP_OUT', 'cNUM'),
  cur: P.fieldOff('GLMSG::SNETDROP_OUT', 'cCUR'),
  list: P.fieldOff('GLMSG::SNETDROP_OUT', 'sOUTID'),
  stride: S.STARID.size,
  maxEntries: S['GLMSG::SNETDROP_OUT'].fields.sOUTID.size / S.STARID.size,
};

const vec3 = (b, o) => [b.readFloatLE(o), b.readFloatLE(o + 4), b.readFloatLE(o + 8)];

class World extends EventEmitter {
  constructor() {
    super();
    /** @type {Map<number, {globId,nativeId,pos,target,hp,lastSeen}>} */
    this.entities = new Map();
    this.stats = { added: 0, moved: 0, removed: 0, unknownMove: 0 };
  }

  /** Feed every field/agent message here; unrelated types are ignored. */
  handle(p) {
    switch (p.type) {
      case A.NET_MSG_GCTRL_DROP_CROW: return this._add(p.body);
      case A.NET_MSG_GCTRL_CROW_MOVETO: return this._move(p.body);
      case A.NET_MSG_GCTRL_DROP_OUT: return this._remove(p.body);
      default: return false;
    }
  }

  _add(body) {
    if (body.length < DROP.hp + 4) return false;
    const e = {
      globId: body.readUInt32LE(DROP.globId),
      nativeId: body.readUInt32LE(DROP.nativeId),
      pos: vec3(body, DROP.pos),
      target: null,
      hp: body.readUInt32LE(DROP.hp),
      lastSeen: Date.now(),
    };
    this.entities.set(e.globId, e);
    this.stats.added++;
    this.emit('enter', e);
    return true;
  }

  _move(body) {
    if (body.length < MOVE.tar + 12) return false;
    const id = body.readUInt32LE(MOVE.globId);
    const cur = vec3(body, MOVE.cur);
    const tar = vec3(body, MOVE.tar);
    const e = this.entities.get(id);
    if (!e) {
      // Movement for something never announced. Expected around the edges of
      // view: track it rather than dropping it, so the count stays honest.
      this.stats.unknownMove++;
      const ghost = { globId: id, nativeId: 0, pos: cur, target: tar,
                      hp: null, lastSeen: Date.now(), inferred: true };
      this.entities.set(id, ghost);
      this.emit('enter', ghost);
      return true;
    }
    e.pos = cur;
    e.target = tar;
    e.lastSeen = Date.now();
    this.stats.moved++;
    this.emit('move', e);
    return true;
  }

  // SNETDROP_OUT is a batch: cNUM entries of STARID, and dwSize is trimmed to
  // the entries actually used, so trust cNUM but clamp it to what arrived.
  _remove(body) {
    if (body.length <= OUT.num) return false;
    const declared = body.readUInt8(OUT.num);
    const fits = Math.floor((body.length - OUT.list) / OUT.stride);
    const n = Math.max(0, Math.min(declared, fits, OUT.maxEntries));
    for (let i = 0; i < n; i++) {
      const off = OUT.list + i * OUT.stride;
      const id = body.readUInt16LE(off + S.STARID.fields.wID.off);
      const crow = body.readUInt16LE(off + S.STARID.fields.wCrow.off);
      // wID is only 16 bits here while dwGlobID is 32 — match on the low word.
      for (const [gid, e] of this.entities) {
        if ((gid & 0xffff) === id) {
          this.entities.delete(gid);
          this.stats.removed++;
          this.emit('leave', { ...e, crow });
          break;
        }
      }
    }
    return true;
  }

  get count() { return this.entities.size; }

  nearest(pos, limit = 5) {
    return [...this.entities.values()]
      .map((e) => ({ ...e, dist: Math.hypot(e.pos[0] - pos[0], e.pos[2] - pos[2]) }))
      .sort((a, b) => a.dist - b.dist)
      .slice(0, limit);
  }
}

module.exports = { World, OFFSETS: { DROP, MOVE, OUT } };
