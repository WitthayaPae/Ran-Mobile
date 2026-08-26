'use strict';
//
// Bus station data — `CLIENT/data/glogic/busmain.ini` (per-NPC; a given bus NPC's
// SNpcTalk carries m_strBusFile naming ITS OWN file, e.g. "busmain.ini" -- there
// can be more than one, one per bus-network NPC, but only busmain.ini ships).
//
// Unlike taxi (a binary CSerialFile blob), bus is a plain CIniLoader text file
// under the SAME AES layer gamecrypt.js already decodes for param.ini/config.ini
// (GLBusList::LOAD, GLBusList.cpp -- CIniLoader over `data/glogic/<file>`, no
// EMBYTECRYPT substitution cipher involved at all).
//
// [STATION_INFO]
// Random_Flag = 0        -- confirmed 0 in the shipped file: NOT randomized, so
// Random_Count = 0          the on-wire station id (see below) is fully stable,
// Statin_Info = MID,SID,PROBABILITY,LINKID,MAP_NAME,STATION_NAME   (repeated key)
//
// Field order is READ order (GLBusList.cpp:88-95), not the file's own inline
// comment (which is stale -- it lists 5 names for what is actually 6 fields):
//   0 MID, 1 SID            -> SNATIVEID  (destination map id)
//   2 PROBABILITY           -> only meaningful when Random_Flag=1 (it is not)
//   3 LINKID                -> SSTATION.dwLINKID -- see below, this is what
//                               ships on the wire, NOT an array index
//   4 MAP_NAME, 5 STATION_NAME -> display strings, raw cp874 (Thai), decoded
//                               client-side by RanText.DecodeCp874 same as
//                               every other extractor in this port
//
// THE WIRE ID. CBusWindow::LoadStopList (BusWindow.cpp:161-164) tags each stop
// list row with `pSTATION->dwLINKID` via CBasicTextBoxEx::SetTextData, and
// BUS_MOVEBUTTON's handler reads that same tag back and sends it AS-IS
// (BusWindow.cpp:280-291): `ReqBusStation(m_dwGaeaID, dwStationID)` where
// dwStationID == dwLINKID. This is confirmed distinct from GLBusList's own
// internal by-array-index GetStation(dwID) (used only to populate the list);
// the actual server-side validator, GLBusStation::GetStation(GLCharactorReq.cpp:
// 4662), does a LINEAR SEARCH matching `dwLINKID == dwID` against a completely
// separate master file (STATION_LIST/STATION in a different ini, not shipped /
// not needed here since the server owns that copy) -- so dwLINKID is the one
// value that has to round-trip byte-for-byte, and it is read straight off each
// Statin_Info row, no derivation.
//
// GROUPING. CBusWindow shows a two-level list: village (by dwMAPID, in
// insertion order) -> stop. GLBusList::insert (GLBusList.cpp:21-47) builds
// exactly that grouping from the filtered station list, in file order (no
// sort). Mirrored here 1:1 by groupByVillage().
//
// MAP-ENABLED FILTER (deliberately NOT reproduced here): GLBusList::LOAD drops
// any station whose destination map is disabled (`!GLGaeaClient::FindMapNode`)
// BEFORE grouping, so a disabled-map row never reaches the client's list at
// all. This port has no loaded copy of MapsList's enabled/disabled state to
// filter against, so every row in the file is kept; an entry pointing at a
// disabled map would show in the picker but the server (which DOES have that
// state) will simply reject the request like any other disallowed move --
// consistent with this codebase's "let the server be the one source of truth
// for things the client cannot itself verify" convention used throughout.
//
//   node extract-bus.js
//   node extract-bus.js --out MOBILE/assets/bus.json
const fs = require('fs');
const path = require('path');
const G = require('./gamecrypt.js');

const base = path.resolve(__dirname, '../../..');
const CLIENT = path.join(base, 'CLIENT');

/** Repeated-key INI reader: returns every value of `key` in `section`, in file
 * order, each split on ','. extract-config.js's parseIni only keeps the LAST
 * value per key, which is wrong here -- Statin_Info repeats. */
function readRepeatedCsv(text, section, key) {
  const out = [];
  let cur = '';
  for (const raw of text.split(/\r?\n/)) {
    const line = raw.trim();
    if (!line || line.startsWith(';')) continue;
    const sec = /^\[(.+)\]$/.exec(line);
    if (sec) { cur = sec[1].trim(); continue; }
    if (cur !== section) continue;
    const eq = line.indexOf('=');
    if (eq < 0) continue;
    const k = line.slice(0, eq).trim();
    if (k !== key) continue;
    out.push(line.slice(eq + 1).split(','));
  }
  return out;
}

function readSingle(text, section, key) {
  let cur = '';
  for (const raw of text.split(/\r?\n/)) {
    const line = raw.trim();
    if (!line || line.startsWith(';')) continue;
    const sec = /^\[(.+)\]$/.exec(line);
    if (sec) { cur = sec[1].trim(); continue; }
    if (cur !== section) continue;
    const eq = line.indexOf('=');
    if (eq < 0) continue;
    if (line.slice(0, eq).trim() !== key) continue;
    return line.slice(eq + 1).trim();
  }
  return null;
}

function parseBusFile(rawBuf) {
  const dec = G.isEncoded(rawBuf) ? G.decode(rawBuf) : rawBuf;
  // latin1: byte-preserving (Thai cp874 bytes pass through untouched, exactly
  // like extract-npcshops.js / other text extractors in this port); trailing
  // NUL padding from the AES block alignment is trimmed by the line split.
  const text = dec.toString('latin1');

  const randomFlag = readSingle(text, 'STATION_INFO', 'Random_Flag') === '1';
  const rows = readRepeatedCsv(text, 'STATION_INFO', 'Statin_Info');

  const stations = [];
  for (const f of rows) {
    if (f.length < 6) continue;
    const mapId = ((parseInt(f[1], 10) || 0) << 16) | (parseInt(f[0], 10) || 0); // SNATIVEID: wSubID<<16 | wMainID
    stations.push({
      mapId,
      probability: parseInt(f[2], 10) || 0,
      linkId: parseInt(f[3], 10) || 0,
      mapName: f[4] != null ? f[4].trim() : '',
      stationName: f[5] != null ? f[5].trim() : '',
    });
  }

  return { randomFlag, stations };
}

// GLBusList::insert (GLBusList.cpp:21-47): group by dwMAPID, first-seen order.
function groupByVillage(stations) {
  const villages = [];
  const byMapId = new Map();
  for (const s of stations) {
    let v = byMapId.get(s.mapId);
    if (!v) {
      v = { mapId: s.mapId, mapName: s.mapName, stops: [] };
      byMapId.set(s.mapId, v);
      villages.push(v);
    }
    v.stops.push({ linkId: s.linkId, name: s.stationName });
  }
  return villages;
}

if (require.main === module) {
  const p = path.join(CLIENT, 'data', 'glogic', 'busmain.ini');
  const raw = fs.readFileSync(p);
  const data = parseBusFile(raw);
  const villages = groupByVillage(data.stations);

  console.log(`randomFlag=${data.randomFlag}  stations=${data.stations.length}  villages=${villages.length}`);
  for (const v of villages.slice(0, 5)) {
    console.log(`  [${v.mapId}] "${v.mapName}"  (${v.stops.length} stops)`);
    for (const st of v.stops.slice(0, 3)) console.log(`      link=${st.linkId} "${st.name}"`);
  }
  if (data.randomFlag) {
    console.log('WARNING: Random_Flag=1 in this file -- station ids are drawn with ' +
                'srand(time(NULL)) each PC client load; this extractor snapshots the ' +
                'file order instead of replicating that non-determinism (see file header).');
  }

  const at = process.argv.indexOf('--out');
  if (at > 0) {
    const outp = path.join(base, process.argv[at + 1]);
    const out = {
      villages: villages.map(v => ({
        m: v.mapId, n: v.mapName,
        stops: v.stops.map(s => ({ l: s.linkId, n: s.name })),
      })),
    };
    fs.writeFileSync(outp, JSON.stringify(out));
    console.log(`wrote ${outp}`);
  }
}

module.exports = { parseBusFile, groupByVillage };
