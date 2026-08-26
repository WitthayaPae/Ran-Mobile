'use strict';
//
// RCC archive reader.
//
// A .rcc is a standard PKZIP archive whose entries are additionally passed
// through ONE pass of a fixed XOR+ADD byte cipher (CCrypt, Lib_Engine/Crypt.cpp).
// Read order is: unzip -> decrypt. Exactly one decrypt pass; SOURCE_RCC.md
// records a multi-hour outage caused by double-encrypting at pack time.
//
// Two facts that matter for the mobile pipeline:
//
//  * ONE pass is all that is ever needed, including for `data/glogic/`.
//    SOURCE_RCC.md notes that loose glogic files are already XOR-encrypted at
//    rest, which reads like the archive would hold two layers — it does not.
//    Packing stores those already-encrypted bytes as-is, so the total is still
//    one layer. Verified: Quest.rcc/00001B.qst decrypts to "default..." in one
//    pass and turns back into noise if a second is applied.
//  * Entries are stored as FLAT bare filenames, lowercased, with no folder
//    prefix — CUnzipper looks them up as zipPath + bareFilename.
//
// Zip parsing is done here rather than with a dependency: Node ships raw
// inflate, and the archives use only store/deflate.
//
const fs = require('fs');
const zlib = require('zlib');

// CCrypt keys. The C++ operates on BYTE, so both constants truncate to 8 bits:
//   EN       = 9183 = 0x23DF -> 0xDF
//   EN ^ EN2 = 9183 ^ 729    -> 0x06
const EN = 9183 & 0xff;
const ADD = (9183 ^ 729) & 0xff;

/** CCrypt::Decryption — in place, one pass. */
function decrypt(buf) {
  for (let i = 0; i < buf.length; i++) buf[i] = ((buf[i] - ADD) & 0xff) ^ EN;
  return buf;
}

/** CCrypt::Encryption — the exact inverse, for round-trip tests and repacking. */
function encrypt(buf) {
  for (let i = 0; i < buf.length; i++) buf[i] = ((buf[i] ^ EN) + ADD) & 0xff;
  return buf;
}

const SIG_EOCD = 0x06054b50;
const SIG_CEN = 0x02014b50;
const SIG_LOC = 0x04034b50;

function findEocd(buf) {
  // The EOCD is at the end but may be followed by a comment, so scan back.
  const min = Math.max(0, buf.length - 0x10000 - 22);
  for (let i = buf.length - 22; i >= min; i--) {
    if (buf.readUInt32LE(i) === SIG_EOCD) return i;
  }
  throw new Error('not a zip archive: no end-of-central-directory record');
}

/**
 * Read the central directory.
 * @returns {{name:string, method:number, compressedSize:number,
 *            size:number, localOffset:number}[]}
 */
function readDirectory(buf) {
  const eocd = findEocd(buf);
  const count = buf.readUInt16LE(eocd + 10);
  let off = buf.readUInt32LE(eocd + 16);

  const entries = [];
  for (let i = 0; i < count; i++) {
    if (buf.readUInt32LE(off) !== SIG_CEN) {
      throw new Error(`corrupt central directory at entry ${i} (offset ${off})`);
    }
    const nameLen = buf.readUInt16LE(off + 28);
    const extraLen = buf.readUInt16LE(off + 30);
    const commentLen = buf.readUInt16LE(off + 32);
    entries.push({
      name: buf.toString('latin1', off + 46, off + 46 + nameLen),
      method: buf.readUInt16LE(off + 10),
      compressedSize: buf.readUInt32LE(off + 20),
      size: buf.readUInt32LE(off + 24),
      localOffset: buf.readUInt32LE(off + 42),
    });
    off += 46 + nameLen + extraLen + commentLen;
  }
  return entries;
}

/** Raw bytes of one entry: unzipped but still RCC-encrypted. */
function readRaw(buf, entry) {
  const lo = entry.localOffset;
  if (buf.readUInt32LE(lo) !== SIG_LOC) {
    throw new Error(`bad local header for ${entry.name}`);
  }
  const nameLen = buf.readUInt16LE(lo + 26);
  const extraLen = buf.readUInt16LE(lo + 28);
  const start = lo + 30 + nameLen + extraLen;
  const raw = buf.subarray(start, start + entry.compressedSize);

  if (entry.method === 0) return Buffer.from(raw);
  if (entry.method === 8) return zlib.inflateRawSync(raw);
  throw new Error(`${entry.name}: unsupported compression method ${entry.method}`);
}

class RccArchive {
  constructor(filePath) {
    this.path = filePath;
    this.buf = fs.readFileSync(filePath);
    this.entries = readDirectory(this.buf);
    this._byName = new Map(this.entries.map((e) => [e.name.toLowerCase(), e]));
  }

  get length() { return this.entries.length; }

  list() { return this.entries.map((e) => e.name); }

  /**
   * Extract one entry, unzipped and RCC-decrypted.
   * @param {string|object} nameOrEntry
   * @param {boolean} [raw] skip the decrypt pass (to inspect the stored bytes)
   */
  read(nameOrEntry, raw = false) {
    const entry = typeof nameOrEntry === 'string'
      ? this._byName.get(nameOrEntry.toLowerCase())
      : nameOrEntry;
    if (!entry) throw new Error(`no entry "${nameOrEntry}" in ${this.path}`);
    const bytes = readRaw(this.buf, entry);
    if (bytes.length !== entry.size) {
      throw new Error(
        `${entry.name}: inflated ${bytes.length} bytes, directory says ${entry.size}`);
    }
    return raw ? bytes : decrypt(bytes);
  }
}

module.exports = { RccArchive, decrypt, encrypt, readDirectory, EN, ADD };
