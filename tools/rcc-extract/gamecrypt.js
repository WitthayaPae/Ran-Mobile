'use strict';
//
// The second encryption layer, applied to most `data/glogic/` files underneath
// the RCC XOR pass.
//
// It is plain AES-256-ECB. The source makes it look like two other things
// first, and both readings are wrong:
//
//   * The class is `CRijndael` with a 32-byte key table, suggesting
//     Rijndael-256 — a 256-BIT BLOCK cipher node:crypto cannot do. It is not:
//     `Initialize(version, key, keyLen, chain, keylength, blockSize=16, ...)`
//     is called with only five arguments (StringMemory.cpp:98), so the fifth
//     is `keylength`, NOT `blockSize`, and the block stays 16 bytes.
//   * `sm_chain0` is passed as an IV and the decrypt loop has a CBC branch,
//     suggesting CBC. Also not: `iMode` defaults to ECB (Rijndael.h:103), so
//     the chain is never used.
//
// Both were settled by measurement, not reading — a harness linked against the
// real `CRijndael` produced the ground truth:
//
//   decrypt(32 zero bytes) = 996d7bae887a7a15daed63f31acfb503 x2
//
// The 16-byte repeat can only happen with a 128-bit block. CBC then still
// disagreed from the second block onward, and the difference was exactly the
// previous ciphertext block — i.e. no chaining at all. `test.js` pins both the
// zero-block vector and a real file.
//
// Key derivation (CRijndael::Initialize, Rijndael.cpp:980):
//   1. take sm_Version[7], stored XOR-obfuscated so it is not a plaintext
//      string in the shipped binary
//   2. XOR with the de-obfuscation pad
//   3. version >= 5 transform: b ^= 0x21; b += (0x21 ^ 0x10)
//
const crypto = require('crypto');

const V8_STORED = Buffer.from(
  '8468410d0ccd1d10cc29881e721537a924f138a81fca240f8bc90c270d2001a9', 'hex');
const V8_PAD = Buffer.from(
  'b635277868aa6756a754e469566c1af9609b5a9632bc7e52b1bd4962436b49c2', 'hex');

const V8_KEY = (() => {
  const k = Buffer.alloc(32);
  for (let i = 0; i < 32; i++) {
    k[i] = (((V8_STORED[i] ^ V8_PAD[i]) ^ 0x21) + (0x21 ^ 0x10)) & 0xff;
  }
  return k;
})();

const VERSION = 8; // CRijndael::VERSION == RANSEDIT_VERSION
// sm_chain0 exists but is unused: ECB has no chain.
const ZERO_IV = Buffer.alloc(16);

/** True if a buffer carries the 4-byte version prefix this layer uses. */
function isEncoded(buf) {
  return buf.length >= 4 + 16 && buf.readInt32LE(0) === VERSION;
}

/**
 * Strip the version prefix and AES-decrypt.
 *
 * The game decrypts `size - 4` bytes, so the payload is block-aligned by
 * construction; any trailing partial block is passed through rather than
 * dropped, which keeps this lossless on malformed input.
 *
 * @param {Buffer} buf output of the RCC XOR pass
 * @returns {Buffer} plaintext, or the input unchanged if not encoded
 */
function decode(buf) {
  if (!isEncoded(buf)) return buf;
  const payload = buf.subarray(4);
  const whole = payload.length - (payload.length % 16);
  const d = crypto.createDecipheriv('aes-256-ecb', V8_KEY, null);
  d.setAutoPadding(false);
  const out = Buffer.concat([d.update(payload.subarray(0, whole)), d.final()]);
  return whole === payload.length
    ? out
    : Buffer.concat([out, payload.subarray(whole)]);
}

module.exports = { decode, isEncoded, V8_KEY, VERSION, ZERO_IV };
