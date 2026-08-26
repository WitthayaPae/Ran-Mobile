'use strict';
//
// Port of SOURCE/Lib_Network/minTea.cpp
//
// Despite the name this is XXTEA (Corrected Block TEA, Wheeler & Needham 1998),
// not classic TEA — it operates on the whole buffer, not 64-bit blocks.
//
// The key is NOT the server-supplied one. `CNetClient::m_Tea` is default-
// constructed (s_NetClient.h:117) and nothing ever calls setKey(), so every
// client uses the hardcoded literal below. The server-supplied
// NET_MSG_SND_ENCRYPT_KEY value is encrypted *as data* with this key for the
// heartbeat — it is not itself a key.
//

const DEFAULT_KEY = 'Steven Seagal Neck Break';
const TEA_KEY_LENGTH = 16; // minTea.h:257 — 128-bit
const DELTA = 0x9e3779b9;

// minTea::setKey — repeat the passphrase to fill 16 bytes.
// "Steven Seagal Neck Break" is 24 chars, so this is just the first 16.
function buildKey(passphrase = DEFAULT_KEY) {
  const key = Buffer.alloc(TEA_KEY_LENGTH);
  for (let n = 0; n < TEA_KEY_LENGTH; n++) {
    key[n] = passphrase.charCodeAt(n % passphrase.length);
  }
  // realKey is cast to (UINT*) — little-endian on x86/ARM.
  const words = new Uint32Array(4);
  for (let i = 0; i < 4; i++) words[i] = key.readUInt32LE(i * 4);
  return words;
}

const REAL_KEY = buildKey();

// minTea::encrypt(UINT* v, UINT n, UINT* k) — minTea.cpp:128
//
// C precedence matters here. The original line is:
//   mx = (z>>5 ^ y<<2) + (y>>3 ^ z<<4) ^ (sum^y) + (k[p&3 ^ e] ^ z);
// `+` binds tighter than `^`, and `&` tighter than `^`, so it parses as:
//   mx = ((z>>>5 ^ y<<2) + (y>>>3 ^ z<<4)) ^ ((sum^y) + (k[(p&3) ^ e] ^ z));
// Getting this wrong produces plausible-looking ciphertext that the server
// rejects, so it is worth being explicit.
function mix(z, y, sum, k, p, e) {
  const a = (((z >>> 5) ^ (y << 2)) + ((y >>> 3) ^ (z << 4))) >>> 0;
  const b = (((sum ^ y) >>> 0) + ((k[(p & 3) ^ e] ^ z) >>> 0)) >>> 0;
  return (a ^ b) >>> 0;
}

function xxteaEncrypt(v, k) {
  const n = v.length;
  if (n < 2) return v;

  let z = v[n - 1];
  let y;
  let sum = 0;
  // q = floor(6 + 52.0f/n) — float division, then truncated.
  let q = Math.floor(6 + 52 / n);

  while (q-- > 0) {
    sum = (sum + DELTA) >>> 0;
    const e = (sum >>> 2) & 3;
    let p;
    for (p = 0; p < n - 1; p++) {
      y = v[p + 1];
      const mx = mix(z, y, sum, k, p, e);
      v[p] = z = (v[p] + mx) >>> 0;
    }
    y = v[0];
    const mx = mix(z, y, sum, k, p, e);
    v[n - 1] = z = (v[n - 1] + mx) >>> 0;
  }
  return v;
}

function xxteaDecrypt(v, k) {
  const n = v.length;
  if (n < 2) return v;

  let y = v[0];
  let z;
  const q = Math.floor(6 + 52 / n);
  let sum = (q * DELTA) >>> 0;

  while (sum !== 0) {
    const e = (sum >>> 2) & 3;
    let p;
    for (p = n - 1; p > 0; p--) {
      z = v[p - 1];
      const mx = mix(z, y, sum, k, p, e);
      v[p] = y = (v[p] - mx) >>> 0;
    }
    z = v[n - 1];
    const mx = mix(z, y, sum, k, 0, e);
    v[0] = y = (v[0] - mx) >>> 0;
    sum = (sum - DELTA) >>> 0;
  }
  return v;
}

// minTea::encrypt(char* szData, int nMaxLength) — minTea.cpp:182
//
// Operates in place on a fixed-size NUL-padded field (e.g. szUserid[21]).
// Returns a new Buffer of the same length.
//
//   1. trailing NULs are stripped to find the real length
//   2. pad to > 4 bytes, then up to a multiple of 4  (minTea.cpp:62-72)
//   3. XXTEA the whole thing as UINT words
//   4. write back, followed by a single NUL terminator
//
// THE OVERFLOW BAIL IS LOAD-BEARING (minTea.cpp:186-197). The wrapper builds
// `padded` ciphertext bytes and then PushLast(0), giving padded+1. If that
// exceeds nMaxLength it returns false and copies NOTHING — the caller's buffer
// is left holding PLAINTEXT. This is not a corner case: the China login calls
//   m_Tea.encrypt( nld.szPassword, USR_PASS_LENGTH )   // 20, note no +1
// on a 19-char MD5 hex string, which pads to 20 and needs 21. So the password
// is transmitted in the clear, and the server's decrypt no-ops in exactly the
// same way, so it round-trips. Reproducing the bail is mandatory.
function encryptField(plaintext, fieldSize) {
  const field = Buffer.alloc(fieldSize);
  Buffer.from(String(plaintext), 'latin1').copy(field);

  // while (szData[keyLen-1] == 0) --keyLen
  let len = fieldSize;
  while (len > 0 && field[len - 1] === 0) len--;

  // while (len <= 4) push 0;  then  while (len & 3) push 0;
  // Minimum 8 bytes, which also satisfies XXTEA's n >= 2.
  let padded = len;
  while (padded <= 4) padded++;
  while (padded & 3) padded++;

  // nEncryptedLength > nMaxLength -> no copy, buffer untouched.
  if (padded + 1 > fieldSize) return field;

  const words = new Uint32Array(padded / 4);
  for (let i = 0; i < words.length; i++) words[i] = field.readUInt32LE(i * 4);

  xxteaEncrypt(words, REAL_KEY);

  // memcpy of padded+1 bytes over the original; anything past that is
  // whatever the field already held (zeros, since len <= padded).
  const out = Buffer.from(field);
  for (let i = 0; i < words.length; i++) out.writeUInt32LE(words[i], i * 4);
  out[padded] = 0; // encryptedText.PushLast(0)
  return out;
}

// minTea::decrypt(char* szData, int nMaxLength) — minTea.cpp:200
// Mirrors the same bail. Note decrypt has NO `while (len <= 4)` step, so a
// short field can yield n < 2, in which case minTea::decrypt(UINT*,...) returns
// -1 and leaves the words untouched.
function decryptField(cipherField, fieldSize) {
  const field = Buffer.from(cipherField.subarray(0, fieldSize));

  let len = fieldSize;
  while (len > 0 && field[len - 1] === 0) len--;
  while (len & 3) len++;

  if (len + 1 > fieldSize) return field; // no copy — stays as-is

  const out = Buffer.from(field);
  if (len / 4 >= 2) {
    const words = new Uint32Array(len / 4);
    for (let i = 0; i < words.length; i++) words[i] = field.readUInt32LE(i * 4);
    xxteaDecrypt(words, REAL_KEY);
    for (let i = 0; i < words.length; i++) out.writeUInt32LE(words[i], i * 4);
  }
  out[len] = 0;
  return out;
}

// Convenience: decryptField returns the raw field; callers usually want the
// NUL-terminated string inside it.
function decryptString(cipherField, fieldSize) {
  const out = decryptField(cipherField, fieldSize);
  const nul = out.indexOf(0);
  return out.toString('latin1', 0, nul === -1 ? out.length : nul);
}

module.exports = {
  DEFAULT_KEY, TEA_KEY_LENGTH, REAL_KEY,
  buildKey, xxteaEncrypt, xxteaDecrypt,
  encryptField, decryptField, decryptString,
};
