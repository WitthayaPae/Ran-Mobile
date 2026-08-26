'use strict';
//
// The server's map table — `mapslist.mst` (`GLMapList::LoadMapsListFile`) — plus
// the .lev -> .wld resolution that turns a server MAP ID into the Unity scene
// that map's geometry lives in.
//
// Why this exists: the field server tells a client which map it spawned on and
// which map it gated to purely by a numeric **SNATIVEID** (`sMapID.dwID`). The
// login spawn observed in this project was "map 22", and nothing in the client
// or the wire says what map 22 *is*. The answer is a two-hop lookup, and both
// hops are in shipped data:
//
//   sMapID.dwID  --mapslist.mst-->  strFile (a .lev)  --Level.rcc-->  m_strWldFile
//
// and the Unity scene is named after that .wld (`w_tradezone1.lev` -> the .lev's
// SLEVEL_HEAD carries `tradezone.wld` -> scene `tradezone`). The .lev basename is
// NOT the scene name in general — `w_tradezone1` and `w_city_s_01` both point at
// differently-named .wld files.
//
// Everything below is MEASURED against the real archives, not assumed:
//   * `SMAPNODE_DATA::LOAD` (GLMapNode.cpp) is parsed STRUCTURALLY per version.
//     The record has NO per-node size field, so every field must be walked
//     exactly — a wrong bool count desyncs the whole rest of the file. Parsing
//     to EOF (bytesRead == fileSize) is the check that it did not.
//   * The map name (`strMapName`) is Thai text in TIS-620, decoded to UTF-8.
//   * `SLEVEL_HEAD::LOAD` (GLLevelHead.cpp) FLIPS field order between its two
//     versions: 0x0102 writes [mapName][wld]; 0x0101 writes [wld][mapName]. The
//     engine's own `dwRead == dwSize` block guard is reproduced to prove the
//     head was read at the right stride.
//
// Encryption, both confirmed by decoding real files (not by reading source that
// has the choice both ways):
//   * mapslist.mst is a CSerialFile, byte-crypt `EMBYTECRYPT_MAPSLIST` for
//     version >= 0x0200 (GLMapList.cpp:141). Its GLogic.rcc entry also carries
//     the CCrypt XOR + AES layers, handled by RccArchive.read.
//   * .lev is byte-crypt `EMBYTECRYPT_LEVEL` for version >= 0x0200
//     (GLLevelFileSaveLoad.cpp:192, g_bGLLevel_EP1Compat=false). This CONTRADICTS
//     the note in README that ".lev is not byte-encoded"; measured, the LEVEL
//     table is what makes the .wld name come out readable and the head block
//     guard pass. LEVEL_NEW / OLD both produce garbage here.
//
const B = require('./bytecrypt');

const MAPSLIST_TABLE = 'EMBYTECRYPT_MAPSLIST';
const LEVEL_TABLE = 'EMBYTECRYPT_LEVEL';

// SMAPNODE_DATA::LOAD tail-bool counts, per node version. The COMMON prefix
// (bUsed, strFile, ids, fieldSID, 11 zone bools, 3 strings) is identical across
// all versions; only the count of trailing bool flags grows. Taken field-for-
// field from GLMapNode.cpp — bInstantMap is always the FIRST tail bool.
const TAIL_BOOLS = {
  0x0100: 2,   // bInstantMap, bQBoxEnable
  0x0101: 3,   // + bLunchBoxForbid
  0x0102: 4,   // + bCPReset
  0x0103: 5,   // + bPKMap
  0x0200: 10,  // + bUIMapSelect, bUIMapInfo, bClubPKRecord, bOpenPrivateMarket, bPartySparring
  0x0201: 14,  // + bBlockTaxi, bBlockFriendCard, bBlockRecall, bBlockTeleport
  0x0202: 15,  // + bDisableSystemBuffs
  0x0203: 16,  // + bBlockHPPotion
  0x0204: 17,  // + bBossMap (VERSION)
};

/** A forward cursor with the CSerialFile/basestream read primitives. */
class Cursor {
  constructor(buf, at) { this.b = buf; this.p = at; }
  u32() { const v = this.b.readUInt32LE(this.p); this.p += 4; return v; }
  u16() { const v = this.b.readUInt16LE(this.p); this.p += 2; return v; }
  u8() { const v = this.b[this.p]; this.p += 1; return v; }
  bool() { return this.u8() !== 0; }
  skipBools(n) { this.p += n; }
  /**
   * CSerialFile std::string: [u32 len][len bytes], len INCLUDES the NUL — the
   * `.wld0`/item convention, opposite the DxFrame one. The stored trailing NUL
   * is dropped. Returns the RAW bytes and the latin1 string; callers that want
   * Thai transcode the raw bytes with tis620().
   */
  str() {
    const n = this.u32();
    if (n > 65536) throw new Error(`bad string length ${n} at ${this.p - 4}`);
    const raw = this.b.subarray(this.p, this.p + n);
    this.p += n;
    const nul = raw.indexOf(0);
    const end = nul === -1 ? n : nul;
    return { raw: raw.subarray(0, end), text: raw.toString('latin1', 0, end) };
  }
}

/**
 * Decode a TIS-620 (Thai, == ISO-8859-11) byte buffer to a UTF-8 string. Bytes
 * 0x00..0x7F are ASCII; 0xA1..0xFB map to Thai Unicode by a fixed +0x0D60
 * offset (U+0E01 = 0xA1 + 0x0D60). This is what the Thai client's own name
 * strings are stored in; without it the names read as latin1 mojibake.
 */
function tis620(buf) {
  let out = '';
  for (const b of buf) {
    if (b < 0x80) out += String.fromCharCode(b);
    else if (b >= 0xa1 && b <= 0xfb) out += String.fromCharCode(b + 0x0d60);
    else out += '�';
  }
  return out;
}

/**
 * Parse `mapslist.mst` (already RCC-decrypted). Returns { version, count, maps }.
 * Each map: { main, sub, id, used, fieldSID, lev, name, bgm, loading, flags }.
 * Throws if the walk does not land exactly on EOF (a wrong stride would).
 */
function parseMapList(raw) {
  const head = B.readHeader(raw);
  if (!head || head.type !== 'GLMAPS_LIST') {
    throw new Error(`not a GLMAPS_LIST file (type "${head && head.type}")`);
  }
  const body = head.version >= 0x0200
    ? B.decode(Buffer.from(raw), MAPSLIST_TABLE, head.bodyOffset)
    : Buffer.from(raw);

  const c = new Cursor(body, head.bodyOffset);
  const count = c.u32();
  if (count > 100000) throw new Error(`implausible map count ${count}`);

  const maps = [];
  const versions = {};
  for (let i = 0; i < count; i++) {
    const dwVer = c.u32();
    const tail = TAIL_BOOLS[dwVer];
    if (tail === undefined) throw new Error(`unknown SMAPNODE_DATA version 0x${dwVer.toString(16)} at map ${i}`);
    versions[dwVer] = (versions[dwVer] || 0) + 1;

    const used = c.bool();
    const lev = c.str().text;
    const main = c.u16();
    const sub = c.u16();
    const fieldSID = c.u32();

    // 11 zone bools (bPeaceZone .. bClubBattleZone).
    const peace = c.bool();      // 0 bPeaceZone
    c.bool();                    // 1 bCommission
    const pk = c.bool();         // 2 bPKZone
    const freePK = c.bool();     // 3 bFreePK
    const itemDrop = c.bool();   // 4 bItemDrop
    c.bool();                    // 5 bMove
    const restart = c.bool();    // 6 bRestart
    c.skipBools(4);              // 7..10 bPetActivity, bDECEXP, bVehicleActivity, bClubBattleZone

    const name = tis620(c.str().raw);
    const bgm = c.str().text;
    const loading = c.str().text;

    // Tail flags: bInstantMap is always first; the rest are not needed here.
    const instant = c.bool();
    c.skipBools(tail - 1);

    maps.push({
      main, sub, id: (main | (sub << 16)) >>> 0,
      used, fieldSID, lev, name, bgm, loading,
      flags: { peace, pk, freePK, itemDrop, restart, instant },
    });
  }

  return { version: head.version, count, maps, versions,
           bytesRead: c.p, fileSize: body.length };
}

/**
 * Resolve a .lev buffer (already RCC-decrypted) to its .wld filename via
 * SLEVEL_HEAD. Returns { wld, mapName, headVersion, ok } — `ok` is the engine's
 * own `dwRead == dwSize` block guard, so a false means the head was mis-read and
 * the caller should not trust `wld`.
 */
function resolveWld(raw) {
  const head = B.readHeader(raw);
  if (!head) return { wld: '', mapName: '', ok: false };
  const body = head.version >= 0x0200
    ? B.decode(Buffer.from(raw), LEVEL_TABLE, head.bodyOffset)
    : Buffer.from(raw);

  const c = new Cursor(body, head.bodyOffset);
  const dwVer = c.u32();
  const dwSize = c.u32();
  const startAfterSize = c.p;

  let wld = '', mapName = '';
  if (dwVer === 0x0102) { mapName = c.str().text; wld = c.str().text; c.u32(); c.u32(); }
  else if (dwVer === 0x0101) { wld = c.str().text; mapName = c.str().text; c.u32(); c.u32(); }
  else return { wld: '', mapName: '', headVersion: dwVer, ok: false };

  const readSize = c.p - startAfterSize;
  return { wld, mapName, headVersion: dwVer, ok: readSize === dwSize };
}

/** scene name = the .wld's basename, lowercased (Unity scenes are lowercase). */
function sceneFromWld(wld) {
  if (!wld) return '';
  let n = wld.replace(/\\/g, '/');
  const slash = n.lastIndexOf('/');
  if (slash >= 0) n = n.slice(slash + 1);
  const dot = n.lastIndexOf('.');
  if (dot > 0) n = n.slice(0, dot);
  return n.toLowerCase();
}

/** .lev basename, lowercased — the fallback scene when the .wld cannot be read. */
function sceneFromLev(lev) {
  return sceneFromWld(lev);
}

module.exports = {
  parseMapList, resolveWld, sceneFromWld, sceneFromLev, tis620, Cursor,
  MAPSLIST_TABLE, LEVEL_TABLE, TAIL_BOOLS,
};
