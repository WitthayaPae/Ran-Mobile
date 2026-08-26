'use strict';
//
// Per-map sky/cloud, from the `.wld`.
//
//   node extract-sky.js
//   node extract-sky.js --out MOBILE/assets/sky.json
//
// The audit that started this: in-world maps render with zero sky/cloud
// geometry, while a working DxSkyMan port (RanLoginSky.cs) already exists but
// was only ever wired to the login map's HARDCODED enable flags. This decodes
// the REAL per-map `SKY_PROPERTY` (DxSkyMan.h:28-47) so gameplay maps can
// drive the same renderer with their own data instead of a constant.
//
// POSITION, measured (DxLandManSaveLoad.cpp, every LoadFile_VER* from 108 to
// 200 agrees byte-for-byte on this local order):
//
//   [[collision block, skipped by declared size]]
//   [bExist:u32] [FOG_PROPERTY: ver, size, fields...]   <- only if bExist
//   [bExist:u32] [SKY_PROPERTY: ver, size, fields...]   <- only if bExist
//
// i.e. SKY_PROPERTY sits IMMEDIATELY after FOG_PROPERTY, with a single BOOL
// gate in front of it. If the fog bExist gate is FALSE, DxLandMan::LoadFile_*
// returns before ever reaching the sky gate — so a map with no fog block
// genuinely carries no sky block either; that is the file format, not a gap
// in this reader.
//
// So sky extraction rides on fog extraction: locate the fog record (the same
// signature-search extract-fog.js already validated against 92/132 shipped
// maps — versions 0x0107/0x0108 are the only ones actually shipped, measured
// by running extract-fog.js), walk forward by the EXACT byte count that
// version's FOG_PROPERTY::LoadSet consumes (mirrored from DxFogMan.cpp, not
// guessed — both 0x0107 and 0x0108 read 7 dwords/floats = 28 bytes after
// their own [ver][size] header), and land exactly on the sky bExist gate.
//
// SKY_PROPERTY::LoadSet (DxSkyMan.cpp:32-85) is then parsed structurally per
// its own version: 0x0100 (1 field), 0x0101 (2), 0x0102/0x0103 (4, VERSION is
// 0x0103), 0x0104 (5, leading spacer BOOL). Any other version is not a format
// this reader (or the shipping engine's "else" branch) can validate byte
// contents for, so it is reported unsupported and skipped rather than guessed.
const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc.js');
const wld = require('./wld.js');

const base = path.resolve(__dirname, '../../..');
const ARCHIVE = path.join(base, 'Ran/data/map/Map.rcc');

/**
 * Locate the fog record by signature (identical search to extract-fog.js),
 * then let the caller validate it. Returns the offset of the first FIELD,
 * past [version][size], and how many bytes those fields occupy for the
 * version found — both needed to step past the record to the sky gate.
 */
function findFog(b, from) {
  const limit = Math.min(b.length - 40, from + 4 * 1024 * 1024);
  for (let p = from; p < limit; p += 4) {
    const ver = b.readUInt32LE(p);
    if (ver < 0x0106 || ver > 0x0108) continue;
    if (b.readUInt32LE(p + 4) !== 32) continue;      // declared block size

    const start = b.readFloatLE(p + 16);
    const end = b.readFloatLE(p + 20);
    // Physical plausibility, exactly as extract-fog.js: fog starts at or after
    // zero, ends after it starts, and both sit inside the world.
    if (!(start >= 0 && end > start && end <= 40000 && start < 40000)) continue;
    if (!Number.isFinite(start) || !Number.isFinite(end)) continue;
    // 0x0106/0x0107/0x0108 all read 7 dwords (28 bytes) of fields — measured
    // from DxFogMan.cpp's three matching branches.
    return { ver, at: p + 8, fieldBytes: 28 };
  }
  return null;
}

/** BOOL/int/float readers are all little-endian u32-sized fields on the wire. */
function readSky(b, o) {
  const ver = b.readUInt32LE(o);
  const size = b.readUInt32LE(o + 4);
  let p = o + 8;

  if (ver === 0x0104) {
    // BOOL spacer (unused legacy field), then the four real fields.
    p += 4;
    const skyEnable = b.readUInt32LE(p); p += 4;
    const cloudEnable = b.readUInt32LE(p); p += 4;
    const axisValue = b.readFloatLE(p); p += 4;
    const radioAxis = b.readUInt32LE(p); p += 4;
    return { ver, skyEnable, cloudEnable, axisValue, radioAxis, end: p };
  }
  if (ver === 0x0103) {           // == SKY_PROPERTY::VERSION
    const skyEnable = b.readUInt32LE(p); p += 4;
    const cloudEnable = b.readUInt32LE(p); p += 4;
    const axisValue = b.readFloatLE(p); p += 4;
    const radioAxis = b.readUInt32LE(p); p += 4;
    return { ver, skyEnable, cloudEnable, axisValue, radioAxis, end: p };
  }
  if (ver === 0x0102) {
    const skyEnable = b.readUInt32LE(p); p += 4;
    const cloudEnable = b.readUInt32LE(p); p += 4;
    const radioAxis = b.readUInt32LE(p); p += 4;    // order swapped vs 0x0103
    const axisValue = b.readFloatLE(p); p += 4;
    return { ver, skyEnable, cloudEnable, axisValue, radioAxis, end: p };
  }
  if (ver === 0x0101) {
    const skyEnable = b.readUInt32LE(p); p += 4;
    const cloudEnable = b.readUInt32LE(p); p += 4;
    return { ver, skyEnable, cloudEnable, axisValue: 0, radioAxis: 0, end: p };
  }
  if (ver === 0x0100) {
    const skyEnable = b.readUInt32LE(p); p += 4;
    const cloudEnable = skyEnable ? 1 : 0;          // LoadSet's own rule
    return { ver, skyEnable, cloudEnable, axisValue: 0, radioAxis: 0, end: p };
  }
  return { unsupportedVersion: ver, end: o + 8 + size };
}

function readSkyForWld(f) {
  const b = f.buf;
  const BODY = 132;
  let o = BODY + f.marks.coll;
  if (o + 16 > b.length) return null;

  const found = findFog(b, o);
  if (!found) return { hasSky: false, reason: 'fog-not-found' };

  const gateAt = found.at + found.fieldBytes;
  if (gateAt + 4 > b.length) return { hasSky: false, reason: 'truncated' };
  const bExist = b.readUInt32LE(gateAt);
  if (!bExist) return { hasSky: false, reason: 'sky-gate-false', fogVersion: found.ver };

  const skyAt = gateAt + 4;
  if (skyAt + 8 > b.length) return { hasSky: false, reason: 'truncated' };
  const sky = readSky(b, skyAt);
  if (sky.unsupportedVersion !== undefined) {
    return { hasSky: false, reason: 'unsupported-sky-version', version: sky.unsupportedVersion };
  }

  // Validate structurally, not just "it parsed": both flags are BOOLs (the
  // engine's BOOL is a full DWORD, but the writer only ever stores 0/1),
  // radioAxis selects X/Y/Z (DxSkyMan.cpp:428-430), axisValue is a degree
  // angle the engine feeds through D3DXMatrixRotation* with no range check
  // in the source, but every shipped writer is a UI-driven degree field.
  const skyEnable01 = sky.skyEnable === 0 || sky.skyEnable === 1;
  const cloudEnable01 = sky.cloudEnable === 0 || sky.cloudEnable === 1;
  const axisOk = sky.radioAxis >= 0 && sky.radioAxis <= 2;
  const angleOk = Number.isFinite(sky.axisValue) && Math.abs(sky.axisValue) <= 3600;
  if (!skyEnable01 || !cloudEnable01 || !axisOk || !angleOk) {
    return { hasSky: false, reason: 'implausible', raw: sky };
  }

  return {
    hasSky: true,
    fogVersion: found.ver,
    skyVersion: sky.ver,
    skyEnable: !!sky.skyEnable,
    cloudEnable: !!sky.cloudEnable,
    radioAxis: sky.radioAxis,
    axisValue: sky.axisValue,
  };
}

const arc = new RccArchive(ARCHIVE);
const out = {};
const seen = new Set();      // dedup by basename regardless of what was found —
                              // RanMapZipTemp-style stale duplicates must not be
                              // re-counted even for maps with no sky record.
const stats = { maps: 0 };
let nSky = 0, nGateFalse = 0, nFogNotFound = 0, nImplausible = 0, nFailed = 0, nUnsupportedSky = 0;
const skyVers = {};

for (const e of arc.entries) {
  if (!/\.wld$/i.test(e.name)) continue;
  const name = path.basename(e.name).replace(/\.wld$/i, '').toLowerCase();
  if (seen.has(name)) continue;
  seen.add(name);
  stats.maps++;

  let f;
  try { f = wld.open(arc.read(e)); } catch { nFailed++; continue; }

  let sky;
  try { sky = readSkyForWld(f); } catch { sky = null; }
  if (!sky) { nFailed++; continue; }

  if (!sky.hasSky) {
    if (sky.reason === 'sky-gate-false') nGateFalse++;
    else if (sky.reason === 'fog-not-found') nFogNotFound++;
    else if (sky.reason === 'implausible') nImplausible++;
    else if (sky.reason === 'unsupported-sky-version') nUnsupportedSky++;
    continue;
  }

  nSky++;
  skyVers['0x' + sky.skyVersion.toString(16)] = (skyVers['0x' + sky.skyVersion.toString(16)] || 0) + 1;
  out[name] = sky;
}

console.log(`${stats.maps} maps, ${nSky} carry a sky record ` +
            `(${Object.entries(skyVers).map(([k, v]) => `${k}:${v}`).join(' ')})`);
console.log(`  sky gate false (no sky, by format): ${nGateFalse}`);
console.log(`  fog anchor not found (no sky data reachable): ${nFogNotFound}`);
console.log(`  implausible sky record (rejected): ${nImplausible}`);
console.log(`  unsupported sky version (rejected): ${nUnsupportedSky}`);
console.log(`  unreadable .wld: ${nFailed}`);

const enabled = Object.values(out).filter((s) => s.skyEnable);
const clouded = Object.values(out).filter((s) => s.cloudEnable);
console.log(`  skyEnable=true: ${enabled.length}/${nSky}, cloudEnable=true: ${clouded.length}/${nSky}`);

const sample = out['w_school_03'];
if (sample) {
  console.log(`\nw_school_03: v0x${sample.skyVersion.toString(16)} ` +
              `sky=${sample.skyEnable} cloud=${sample.cloudEnable} ` +
              `axis=${sample.radioAxis} value=${sample.axisValue.toFixed(2)}`);
} else {
  console.log('\nw_school_03: no sky record extracted');
}

const outArg = process.argv.indexOf('--out');
if (outArg > 0) {
  fs.writeFileSync(path.join(base, process.argv[outArg + 1]),
                   JSON.stringify({ maps: out }));
  console.log(`\nwrote ${process.argv[outArg + 1]}`);
}

module.exports = { findFog, readSky, readSkyForWld };
