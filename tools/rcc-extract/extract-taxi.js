'use strict';
//
// Taxi station data — `CLIENT/data/glogic/taxistation.tsf`.
//
// Unlike bus (`busmain.ini`, a text CIniLoader file wrapped in the AES layer
// from gamecrypt.js), taxi is a binary `CSerialFile`/`basestream` blob with
// its OWN, older per-byte substitution cipher (`EMBYTECRYPT_TAXISTATION`,
// ByteCrypt.cpp) — no AES here at all (confirmed: the 14-byte file header
// "GLTAXI_STATION" sits in plaintext at offset 0, un-touched by gamecrypt.js).
//
// File layout (GLTaxiStation::LoadFile, GLTaxiStation.cpp; STAXI_STATION::LOAD,
// GLTaxiData.cpp):
//   [0..128)    char m_szFileType[128]   -- "GLTAXI_STATION\0...", NOT encoded
//   [128..132)  DWORD FileID             -- version, e.g. 0x0200; NOT encoded
//   [132..)     substitution-decoded payload:
//     DWORD m_dwBasicCharge
//     DWORD dwNum
//     dwNum x STAXI_STATION:
//       DWORD dwVer            (must == 0x0100, STAXI_STATION::VERSION)
//       DWORD dwLINKID
//       BYTE  bUsed            (sizeof(bool)==1 in MSVC)
//       DWORD dwMAPID
//       WORD  wPosX
//       WORD  wPosY
//       DWORD dwNPCID
//       DWORD dwMapCharge      -- extra charge when the destination map != current map
//       string strMAP          (DWORD byteLen, then byteLen bytes, NUL-terminated)
//       string strSTATION      (DWORD byteLen, then byteLen bytes, NUL-terminated)
//
// The substitution cipher is a fixed 256-byte lookup table applied per-byte,
// stateless and order-independent (ByteCrypt.cpp byte_decode: builds the
// INVERSE of the encode table, then `buf[i] = inverse[buf[i]]`), so decoding
// the whole payload as one buffer is equivalent to the engine's per-field
// decode-as-you-read.
//
//   node extract-taxi.js
//   node extract-taxi.js --out MOBILE/assets/taxistation.json
const fs = require('fs');
const path = require('path');

const base = path.resolve(__dirname, '../../..');
const CLIENT = path.join(base, 'CLIENT');

// ARRAY_TAXISTATION_VAR1, ByteCryptDefVer1.h:347-365 -- the ENCODE table.
const ENCODE = Uint8Array.from([
  0x00, 0xF0, 0xA2, 0xE2, 0xED, 0x88, 0xE3, 0x98, 0xBC, 0x66, 0xC1, 0x7C, 0xF4, 0xDB, 0x47, 0x96,
  0x06, 0xBB, 0x77, 0xF3, 0x80, 0x5A, 0x7F, 0xFF, 0xCA, 0x55, 0x37, 0xB2, 0xDD, 0xE5, 0x0A, 0x0D,
  0x6A, 0x3E, 0x7B, 0xCC, 0xF7, 0xB7, 0xAD, 0x62, 0xB9, 0xD0, 0xF8, 0xF1, 0x0E, 0x1B, 0xCB, 0xDA,
  0x9E, 0xF9, 0x5C, 0x23, 0xB6, 0x6B, 0xEB, 0x7E, 0x1F, 0x02, 0xFE, 0x85, 0xE9, 0x12, 0xC9, 0xFC,
  0x1E, 0xA9, 0x9A, 0xDF, 0x70, 0x8C, 0x4E, 0x4B, 0xDC, 0xEE, 0x36, 0x8F, 0xC3, 0xAE, 0x08, 0x9F,
  0xE6, 0x2B, 0x60, 0xE8, 0xA6, 0x05, 0xEC, 0x89, 0x49, 0xFD, 0xD9, 0xD6, 0xD4, 0x45, 0x6F, 0x4F,
  0x69, 0xB1, 0x3A, 0xCD, 0x3C, 0xEA, 0xD7, 0xF5, 0xCF, 0x90, 0x92, 0x2D, 0xB5, 0x5D, 0x93, 0x99,
  0xBD, 0x64, 0x2A, 0xBF, 0x34, 0x48, 0x35, 0x43, 0x21, 0x15, 0xE0, 0xC5, 0x0F, 0x78, 0xC4, 0xEF,
  0x6D, 0x0C, 0xA1, 0x7D, 0x39, 0x4A, 0xBE, 0xFA, 0xAB, 0xD1, 0xC7, 0x28, 0x8E, 0x56, 0xA4, 0x1D,
  0x42, 0x41, 0x07, 0xDE, 0x4C, 0x8A, 0x63, 0x4D, 0x51, 0xC0, 0x9B, 0x83, 0x82, 0xE4, 0x8B, 0x79,
  0x44, 0xAC, 0xA7, 0x18, 0xA0, 0x61, 0x13, 0xB3, 0xB4, 0xC6, 0x6C, 0x5E, 0x11, 0xAF, 0x2F, 0x40,
  0x52, 0x25, 0x71, 0x09, 0x3F, 0x29, 0x68, 0x3B, 0x1A, 0xE7, 0x91, 0x59, 0x7A, 0x6E, 0x87, 0xA8,
  0x50, 0x74, 0x72, 0x31, 0x04, 0x58, 0x10, 0xAA, 0x17, 0x46, 0x95, 0xA3, 0x94, 0xFB, 0xBA, 0xD3,
  0xB8, 0x33, 0x57, 0xD8, 0x22, 0x32, 0x8D, 0xF2, 0x9C, 0x86, 0x1C, 0xB0, 0x76, 0x30, 0x01, 0xD2,
  0xF6, 0x16, 0xC2, 0x81, 0x0B, 0x73, 0xA5, 0x20, 0x84, 0x5B, 0x24, 0x9D, 0x75, 0xE1, 0xCE, 0x14,
  0x2C, 0x53, 0x26, 0x2E, 0x67, 0x54, 0x5F, 0xD5, 0xC8, 0x38, 0x27, 0x19, 0x03, 0x97, 0x65, 0x3D,
]);
const DECODE = (() => {
  const d = new Uint8Array(256);
  for (let i = 0; i < 256; i++) d[ENCODE[i]] = i;
  return d;
})();

function byteDecode(buf) {
  const out = Buffer.alloc(buf.length);
  for (let i = 0; i < buf.length; i++) out[i] = DECODE[buf[i]];
  return out;
}

const FILETYPESIZE = 128;
const VERSION_ENCODE = 0x0200;
const VERSION_ENCODE_OLD = 0x0100;
const STAXI_STATION_VERSION = 0x0100;

function readString(buf, off) {
  const len = buf.readUInt32LE(off);
  off += 4;
  const bytes = buf.subarray(off, off + len);
  const nul = bytes.indexOf(0);
  const str = (nul >= 0 ? bytes.subarray(0, nul) : bytes).toString('latin1');
  return [str, off + len];
}

function parseTaxiStation(raw) {
  const fileType = raw.subarray(0, FILETYPESIZE).toString('latin1').replace(/\0.*$/s, '');
  const fileVer = raw.readUInt32LE(FILETYPESIZE);
  if (fileType !== 'GLTAXI_STATION') {
    throw new Error(`unexpected file header ${JSON.stringify(fileType)}`);
  }

  let payload = raw.subarray(FILETYPESIZE + 4);
  // GLTaxiStation::LoadFile: only decoded once dwFILEVER >= VERSION_ENCODE(_OLD).
  // Every shipped taxistation.tsf is 0x0200; EMBYTECRYPT_OLD (a different,
  // older table) is left unimplemented here since no sample file needs it.
  if (fileVer >= VERSION_ENCODE) {
    payload = byteDecode(payload);
  } else if (fileVer >= VERSION_ENCODE_OLD) {
    throw new Error(`taxistation.tsf uses EMBYTECRYPT_OLD (fileVer=0x${fileVer.toString(16)}) -- not implemented`);
  }

  let off = 0;
  const basicCharge = payload.readUInt32LE(off); off += 4;
  const num = payload.readUInt32LE(off); off += 4;

  const stations = [];
  for (let i = 0; i < num; i++) {
    const ver = payload.readUInt32LE(off); off += 4;
    if (ver !== STAXI_STATION_VERSION) {
      throw new Error(`record ${i}: unexpected STAXI_STATION version 0x${ver.toString(16)}`);
    }
    const dwLINKID = payload.readUInt32LE(off); off += 4;
    const bUsed = payload.readUInt8(off) !== 0; off += 1;
    const dwMAPID = payload.readUInt32LE(off); off += 4;
    const wPosX = payload.readUInt16LE(off); off += 2;
    const wPosY = payload.readUInt16LE(off); off += 2;
    const dwNPCID = payload.readUInt32LE(off); off += 4;
    const dwMapCharge = payload.readUInt32LE(off); off += 4;
    let strMAP, strSTATION;
    [strMAP, off] = readString(payload, off);
    [strSTATION, off] = readString(payload, off);

    if (!bUsed) continue; // GLTaxiStation::LoadFile skips !bUsed records via insert()
    stations.push({ dwLINKID, dwMAPID, wPosX, wPosY, dwNPCID, dwMapCharge, strMAP, strSTATION });
  }

  return { basicCharge, stations };
}

// GLTaxiStation::insert (GLTaxiStation.cpp:30-51): group the (already bUsed-
// filtered, FILE-ORDER) station list by dwMAPID, first-seen order, appending
// within an existing group. CTaxiWindow's picker indexes THESE two arrays --
// nSelectMap is the index into the returned `maps` array, nSelectStop the
// index into that map's `stops` array (TaxiWindow.cpp LoadMapList/LoadStopList
// iterate GetTaxiMapNum()/GetTaxiMap(i)->GetStationNum()/GetStation(i) by
// position, never by dwLINKID) -- and GLCharacter::ReqTaxiStation /
// GetCalcTaxiCharge take those SAME positional indices, not ids. So this
// grouping must reproduce the engine's insertion order exactly, not just any
// grouping, or the mobile client and the (identically-loading) server would
// disagree on what index N means.
function groupByMap(stations) {
  const maps = [];
  const byMapId = new Map();
  for (const s of stations) {
    let m = byMapId.get(s.dwMAPID);
    if (!m) {
      m = { mapId: s.dwMAPID, mapName: s.strMAP, stops: [] };
      byMapId.set(s.dwMAPID, m);
      maps.push(m);
    }
    m.stops.push({ name: s.strSTATION, mapCharge: s.dwMapCharge, npcId: s.dwNPCID });
  }
  return maps;
}

if (require.main === module) {
  const p = path.join(CLIENT, 'data', 'glogic', 'taxistation.tsf');
  const raw = fs.readFileSync(p);
  const data = parseTaxiStation(raw);
  const maps = groupByMap(data.stations);

  console.log(`basicCharge=${data.basicCharge}  stations=${data.stations.length}  maps=${maps.length}`);
  for (const m of maps.slice(0, 5)) {
    console.log(`  [${m.mapId}] "${m.mapName}"  (${m.stops.length} stops)`);
    for (const st of m.stops.slice(0, 3)) console.log(`      "${st.name}" +${st.mapCharge}`);
  }

  const at = process.argv.indexOf('--out');
  if (at > 0) {
    const outp = path.join(base, process.argv[at + 1]);
    const out = {
      charge: data.basicCharge,
      maps: maps.map(m => ({
        m: m.mapId, n: m.mapName,
        stops: m.stops.map(s => ({ n: s.name, c: s.mapCharge })),
      })),
    };
    fs.writeFileSync(outp, JSON.stringify(out));
    console.log(`wrote ${outp}`);
  }
}

module.exports = { parseTaxiStation, groupByMap, byteDecode };
