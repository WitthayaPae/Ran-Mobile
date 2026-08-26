'use strict';
//
// Per-format byte substitution used by every encrypted game-data file.
//
//   encode: b -> ARRAY[b]
//   decode: b -> INVERSE[b]
//
// (BYTECRYPT::byte_encode / byte_decode, Lib_Engine/Common/ByteCrypt.cpp)
//
// The 43 tables come from bytecrypt.json, generated out of SOURCE by
// gen-bytecrypt.js. They are never transcribed by hand — a single wrong byte in
// a 256-entry table corrupts 1/256 of every file it touches, which is exactly
// the kind of damage that survives a smoke test.
//
// Each table is a permutation of 0..255 (asserted below), so the inverse always
// exists and decode is lossless.
//
const TABLES = require('./bytecrypt.json').tables;

const inverses = new Map();

function inverseOf(name) {
  let inv = inverses.get(name);
  if (inv) return inv;
  const entry = TABLES[name];
  if (!entry) throw new Error(`unknown byte-crypt type "${name}"`);
  inv = new Uint8Array(256);
  const seen = new Uint8Array(256);
  for (let i = 0; i < 256; i++) {
    const v = entry.encode[i];
    if (seen[v]) throw new Error(`${name}: table is not a permutation`);
    seen[v] = 1;
    inv[v] = i;
  }
  inverses.set(name, inv);
  return inv;
}

/** Decode in place and return the buffer. `name` is an EMBYTECRYPT_* key. */
function decode(buf, name, start = 0, end = buf.length) {
  if (name === 'EMBYTECRYPT_NONE') return buf;
  const inv = inverseOf(name);
  for (let i = start; i < end; i++) buf[i] = inv[buf[i]];
  return buf;
}

/** Encode in place — the exact inverse of decode. */
function encode(buf, name, start = 0, end = buf.length) {
  if (name === 'EMBYTECRYPT_NONE') return buf;
  const table = TABLES[name];
  if (!table) throw new Error(`unknown byte-crypt type "${name}"`);
  for (let i = start; i < end; i++) buf[i] = table.encode[buf[i]];
  return buf;
}

const names = () => Object.keys(TABLES);
const idOf = (name) => TABLES[name] && TABLES[name].id;

/**
 * CSerialFile header: 128 bytes of type string, then a u32 version.
 * The encoded region starts immediately after (m_DefaultOffSet).
 */
const HEADER_SIZE = 128 + 4;

function readHeader(buf) {
  if (buf.length < HEADER_SIZE) return null;
  const raw = buf.subarray(0, 128);
  const nul = raw.indexOf(0);
  return {
    type: raw.toString('latin1', 0, nul === -1 ? 128 : nul),
    version: buf.readUInt32LE(128),
    bodyOffset: HEADER_SIZE,
  };
}

module.exports = { decode, encode, names, idOf, readHeader, HEADER_SIZE, TABLES };
