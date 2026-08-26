'use strict';
//
// `.wld` terrain container reader.
//
// Only the header and the section marks are decoded here. The terrain GEOMETRY
// is deliberately out of scope: it lives in the `.wld0` sidecar (DxStaticMesh)
// and reaching it needs nine per-version parsers. The navigation mesh, which is
// what this exists for, is byte-identical across every `.wld` version — only
// its position moves, and the position is a mark in the header.
//
// Layout (GLLandManSet.cpp:71-147):
//
//   0    128 B  type string   "LAND.MAN" plain / "Land.Man" encrypted
//   128    4 B  u32 version
//   --- 132 = m_DefaultOffSet; ALL marks are relative to here ---
//   +0     4 B  u32 map id
//   +4   128 B  char szMapName[128]
//   +132   4 B  u32 filemark version
//   +136   4 B  u32 filemark size
//   +140  16 B  the four marks
//
const B = require('./bytecrypt');

const HEADER_SIZE = 128;
const BODY_OFFSET = 132;          // CSerialFile m_DefaultOffSet
const MAP_NAME_SIZE = 128;        // MAXLANDNAME, DxLandDef.h:16

// Common/WLDCrypt.h:6-11 — the file-type string IS the encryption flag, and
// each extension has its own pair of spellings:
//
//   .wld    "LAND.MAN"  plain      "Land.Man"       encrypted
//   .wld0   "default"   plain      "Default_Crypt"  encrypted
//
// Only the ENCRYPTED spellings are load-bearing. Treating any unexpected string
// as encrypted is wrong: `login_2.wld0` ships with the `.wld` spelling
// "LAND.MAN" on a plaintext body, and decrypting it produces garbage. Test for
// the encrypted names explicitly, never by exclusion.
const TYPE_PLAIN = 'LAND.MAN';
const TYPE_ENCRYPTED = 'Land.Man';
const TYPE_PLAIN_0 = 'default';
const TYPE_ENCRYPTED_0 = 'Default_Crypt';

const ENCRYPTED_TYPES = new Set([TYPE_ENCRYPTED, TYPE_ENCRYPTED_0]);

/** True only for the two known encrypted spellings. */
const isEncryptedType = (type) => ENCRYPTED_TYPES.has(type);

/**
 * The `.wld` cipher — NOT the 43-table byte substitution used elsewhere.
 *
 * `.wld` calls SetEncode(EMBYTECRYPT_OLD, EMENCODE_WLD), where the
 * EMBYTECRYPT_OLD argument is a dummy non-zero value that exists purely to
 * enter the branch; the EMENCODE_WLD test wins and byte_decode is never reached
 * (SerialFile.cpp:227-234). EMBYTECRYPT_WLD / _WLD2 are unused for .wld — every
 * SetEncodeType(EMBYTECRYPT_WLD) call site in the tree is commented out.
 *
 * The real cipher (Common/WLDCrypt.cpp:16-52) uses constants 0x99701AE and
 * 0x92617BE, but they are ints assigned into a BYTE, so only the low bytes
 * survive: 0xAE and 0xBE, whose XOR is 0x10. It reduces to a stateless,
 * position-independent per-byte transform — which is exactly why seeking to an
 * arbitrary mark needs no crypto state fixup.
 */
function decrypt(buf, start = BODY_OFFSET, end = buf.length) {
  for (let i = start; i < end; i++) buf[i] = ((buf[i] - 0x10) & 0xff) ^ 0x10;
  return buf;
}

function encrypt(buf, start = BODY_OFFSET, end = buf.length) {
  for (let i = start; i < end; i++) buf[i] = ((buf[i] ^ 0x10) + 0x10) & 0xff;
  return buf;
}

const readTypeString = (buf) => {
  const raw = buf.subarray(0, HEADER_SIZE);
  const nul = raw.indexOf(0);
  return raw.toString('latin1', 0, nul === -1 ? HEADER_SIZE : nul);
};

/**
 * Section marks. The two on-disk layouts differ in FIELD ORDER, not size —
 * both are 16 bytes (confirmed by the layout probe):
 *
 *   0x0101  NAVI, WEATHER, GATE, COLL
 *   0x0100  NAVI, GATE, COLL, WEATHER    <- what every shipped file uses
 *
 * NAVI is field 0 in both, so the navmesh is reachable either way. Reading a
 * 0x0100 file with the 0x0101 layout silently swaps WEATHER and GATE.
 *
 * dwCOLL_MARK is decoded for completeness but is write-only in the whole
 * engine — no reader anywhere consumes it.
 */
function readMarks(buf, at, version) {
  const g = (i) => buf.readUInt32LE(at + i * 4);
  if (version === 0x0100) {
    return { navi: g(0), gate: g(1), coll: g(2), weather: g(3) };
  }
  return { navi: g(0), weather: g(1), gate: g(2), coll: g(3) };
}

/**
 * Open a `.wld`. Decrypts in place when the type string says so.
 *
 * @param {Buffer} raw whole file (already RCC-decrypted if it came from Map.rcc)
 * @returns {{type,version,encrypted,mapId,mapName,filemarkVersion,marks,buf}}
 */
function open(raw) {
  if (raw.length < BODY_OFFSET + 4 + MAP_NAME_SIZE + 8 + 16) {
    throw new Error('.wld too short');
  }
  const type = readTypeString(raw);
  const encrypted = isEncryptedType(type);
  if (!encrypted && type !== TYPE_PLAIN) {
    throw new Error(`not a .wld map (type "${type}")`);
  }

  // The header is written by raw fwrite, bypassing the codec — which is why the
  // magic stays readable and can act as the flag. Only the body is enciphered.
  const buf = encrypted ? decrypt(Buffer.from(raw)) : raw;

  const version = buf.readUInt32LE(HEADER_SIZE);
  let p = BODY_OFFSET;
  const mapId = buf.readUInt32LE(p); p += 4;
  const nameRaw = buf.subarray(p, p + MAP_NAME_SIZE); p += MAP_NAME_SIZE;
  const nul = nameRaw.indexOf(0);
  const mapName = nameRaw.toString('latin1', 0, nul === -1 ? MAP_NAME_SIZE : nul);

  const filemarkVersion = buf.readUInt32LE(p); p += 4;
  p += 4; // declared size; the loader trusts the version, not this
  const marks = readMarks(buf, p, filemarkVersion);
  // The DxOctree scene graph — map object placement — starts immediately after
  // the marks. It has no mark of its own; LoadFile_VER200 reads the header, the
  // filemark, and then goes straight into it, so the offset is positional.
  const objectSectionOffset = p + 16;

  return { type, version, encrypted, mapId, mapName, filemarkVersion, marks,
           objectSectionOffset, buf };
}

/**
 * Absolute file offset of the navmesh section.
 * Marks are recorded with GetfTell() and consumed with SetOffSet(), both biased
 * by m_DefaultOffSet (SerialFile.cpp:164-176) — so they are header-relative and
 * 132 must be added. Treating them as absolute makes ~57% of maps look corrupt.
 */
const navmeshOffset = (wld) => wld.marks.navi + BODY_OFFSET;

/** IsLandManSupported, DxLandManSaveLoad.cpp:3313-3337. 0x0118 is absent. */
const SUPPORTED_VERSIONS = new Set([
  0x0108, 0x0109, 0x0110, 0x0111, 0x0112, 0x0113,
  0x0114, 0x0115, 0x0116, 0x0117, 0x0119, 0x0200,
]);
const isSupportedVersion = (v) => SUPPORTED_VERSIONS.has(v);

module.exports = {
  open, decrypt, encrypt, readMarks, navmeshOffset, isSupportedVersion,
  readTypeString, isEncryptedType, SUPPORTED_VERSIONS,
  HEADER_SIZE, BODY_OFFSET, MAP_NAME_SIZE,
  TYPE_PLAIN, TYPE_ENCRYPTED, TYPE_PLAIN_0, TYPE_ENCRYPTED_0,
};
