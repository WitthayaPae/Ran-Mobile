'use strict';
//
// Animation `.bin` (`AnimContainer`) reader.
//
// Layout comes from SAnimationSaveLoad.cpp. Two levels of versioning, and they
// are independent:
//
//   * the CONTAINER version, in the CSerialFile header at byte 128
//   * a per-ANIMATION version, a DWORD at the head of each record
//
// Every keyframe array is a raw `ReadBuffer` of packed structs, so the element
// sizes must match MSVC exactly. They are taken from the layout probe, not
// computed here — `SMatrixKey` is the reason why: it holds a `D3DXMATRIXA16`,
// which is 16-byte aligned, so it is **80 bytes with the matrix at offset 16**,
// not the obvious 4+64=68. Hand-computing that silently shreds every matrix
// track.
//
// Some records carry `dwGarbageValue` DWORDs between the arrays — deliberate
// obfuscation. They are read and discarded, but they must be read.
//
const B = require('./bytecrypt');

const LAYOUT = require('../layout-probe/layout.json');
const S = LAYOUT.allStructs;
const sizeOf = (name) => {
  const s = S[name];
  if (!s) throw new Error(`layout.json has no struct ${name} — rebuild the probe`);
  return s.size;
};

const KEY = {
  position: sizeOf('SPositionKey'),  // 16
  rotate: sizeOf('SRotateKey'),      // 20
  scale: sizeOf('SScaleKey'),        // 16
  matrix: sizeOf('SMatrixKey'),      // 80  <- D3DXMATRIXA16 padding
  quatPos: sizeOf('SQuatPosKey'),    // 36
};

// Animation encoding is version-gated: SAnimationSaveLoad.cpp:401 only sets a
// table when the container version reaches VERSION_ENCODE (0x0200). Below that
// the body is plaintext; applying a table there destroys it.
const VERSION_ENCODE = 0x0200;

class Reader {
  constructor(buf, offset) { this.b = buf; this.p = offset; }
  get remaining() { return this.b.length - this.p; }
  u32() {
    if (this.p + 4 > this.b.length) throw new Error('animation: read past end');
    const v = this.b.readUInt32LE(this.p);
    this.p += 4;
    return v;
  }
  // CSerialFile::operator>>(std::string): DWORD length, then that many bytes.
  // The stored bytes include the terminating NUL.
  str() {
    const n = this.u32();
    if (n > 4096 || this.p + n > this.b.length) {
      throw new Error(`animation: implausible string length ${n}`);
    }
    const raw = this.b.subarray(this.p, this.p + n);
    this.p += n;
    const nul = raw.indexOf(0);
    return raw.toString('latin1', 0, nul === -1 ? raw.length : nul);
  }
  keys(kind) {
    const count = this.u32();
    const stride = KEY[kind];
    const bytes = count * stride;
    if (count > 1e6 || this.p + bytes > this.b.length) {
      throw new Error(`animation: implausible ${kind} key count ${count}`);
    }
    const data = this.b.subarray(this.p, this.p + bytes);
    this.p += bytes;
    return { count, stride, data };
  }
}

/**
 * One SAnimation record. The field ORDER differs per version, and 0102/0103
 * put the bone name first and interleave garbage DWORDs.
 */
function readAnimation(r) {
  const version = r.u32();
  const a = { version, bone: null, position: null, rotate: null,
              scale: null, matrix: null, quatPos: null };

  switch (version) {
    case 0x0100:
      a.position = r.keys('position');
      a.rotate = r.keys('rotate');
      a.scale = r.keys('scale');
      a.matrix = r.keys('matrix');
      a.bone = r.str();
      break;

    case 0x0101:
      a.position = r.keys('position');
      a.rotate = r.keys('rotate');
      a.scale = r.keys('scale');
      a.matrix = r.keys('matrix');
      a.quatPos = r.keys('quatPos');
      a.bone = r.str();
      break;

    case 0x0102:
      a.bone = r.str();
      a.rotate = r.keys('rotate');
      a.position = r.keys('position');
      r.u32();                        // dwGarbageValue
      a.quatPos = r.keys('quatPos');
      r.u32();                        // dwGarbageValue
      a.matrix = r.keys('matrix');
      a.scale = r.keys('scale');
      break;

    default: // 0x0103 and anything newer: SAnimation::LoadFromFile falls through
      a.bone = r.str();
      a.rotate = r.keys('rotate');
      r.u32();                        // dwGarbageValue
      a.position = r.keys('position');
      a.quatPos = r.keys('quatPos');
      a.matrix = r.keys('matrix');
      r.u32();                        // dwGarbageValue
      a.scale = r.keys('scale');
      break;
  }
  return a;
}

const readList = (r) => {
  const n = r.u32();
  if (n > 100000) throw new Error(`animation: implausible track count ${n}`);
  const out = [];
  for (let i = 0; i < n; i++) out.push(readAnimation(r));
  return out;
};

/**
 * Parse an AnimContainer buffer (already RCC-decrypted).
 * @returns {{version:number, encoded:boolean, tracks:object[], upperBody:object[]}}
 */
function parse(buf) {
  const head = B.readHeader(buf);
  if (!head || head.type !== 'AnimContainer') {
    throw new Error(`not an AnimContainer (type "${head && head.type}")`);
  }

  let body = buf;
  const encoded = head.version >= VERSION_ENCODE;
  if (encoded) {
    body = B.decode(Buffer.from(buf), 'EMBYTECRYPT_BIN2', head.bodyOffset);
  }

  const r = new Reader(body, head.bodyOffset);
  const result = { version: head.version, encoded, tracks: [], upperBody: [] };

  // Container field order differs per version (SAnimContainer::Load_*).
  // 0100 has a single list; 0102/0103/0200 put the upper-body list FIRST;
  // 0103 slips a garbage DWORD between the two lists.
  switch (head.version) {
    case 0x0100:
      result.tracks = readList(r);
      break;
    case 0x0101:
      result.tracks = readList(r);
      result.upperBody = readList(r);
      break;
    case 0x0102:
      result.upperBody = readList(r);
      result.tracks = readList(r);
      break;
    case 0x0103:
      result.upperBody = readList(r);
      r.u32();                        // dwGarbageValue between the lists
      result.tracks = readList(r);
      break;
    case 0x0104:
      result.tracks = readList(r);
      result.upperBody = readList(r);
      if (r.remaining >= 4) result.upperBody.push(...readList(r));
      break;
    default: // 0x0200
      result.upperBody = readList(r);
      result.tracks = readList(r);
      break;
  }

  result.bones = [...new Set([...result.tracks, ...result.upperBody]
    .map((t) => t.bone).filter(Boolean))];
  return result;
}

module.exports = { parse, readAnimation, KEY, VERSION_ENCODE };
