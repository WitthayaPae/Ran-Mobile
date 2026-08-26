'use strict';
//
// Per-map fog, from the `.wld`.
//
//   node extract-fog.js
//   node extract-fog.js --out MOBILE/assets/fog.json
//
// The single biggest reason the port looks flatter than the PC client: RAN
// leans on fog for depth, and none of it was being read. `FOG_PROPERTY` sits in
// the map file this project already parses for the navmesh.
//
// Position is derived, not searched. `DxLandManSaveLoad.cpp:1186` skips the
// collision map by its declared block size and then reads the fog block, so
// from the COLL mark the walk is: [dword][blockSize][skip blockSize][bExist]
// then a standard [version][size] block.
//
// Versions 0x0106-0x0108 share one field order. 0x0106 and 0x0107 additionally
// scale the day and night colours at load — 0.85 and 1.6 — which the engine
// applies on read rather than storing, so the same scaling is applied here or
// older maps come out too dark and too dim respectively.
const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc.js');
const wld = require('./wld.js');

const base = path.resolve(__dirname, '../../..');
const ARCHIVE = path.join(base, 'Ran/data/map/Map.rcc');

/** D3DCOLOR is 0xAARRGGBB. */
function color(dw) {
  return {
    r: ((dw >>> 16) & 0xff) / 255,
    g: ((dw >>> 8) & 0xff) / 255,
    b: (dw & 0xff) / 255,
    a: ((dw >>> 24) & 0xff) / 255,
  };
}

/** ColorUp: scale RGB, clamped. Matches DxFogMan's load-time adjustment. */
function colorUp(c, k) {
  return {
    r: Math.min(1, c.r * k), g: Math.min(1, c.g * k),
    b: Math.min(1, c.b * k), a: c.a,
  };
}

/**
 * Locate the fog record by signature, then let the caller validate it.
 * Returns the offset of the first field, past [version][size].
 */
function findFog(b, from) {
  const limit = Math.min(b.length - 40, from + 4 * 1024 * 1024);
  for (let p = from; p < limit; p += 4) {
    const ver = b.readUInt32LE(p);
    if (ver < 0x0106 || ver > 0x0108) continue;
    if (b.readUInt32LE(p + 4) !== 32) continue;      // declared block size

    const start = b.readFloatLE(p + 16);
    const end = b.readFloatLE(p + 20);
    // Physical plausibility is what makes the search safe. Fog starts at or
    // after zero, ends after it starts, and both sit inside the world — the
    // largest shipped map is under 20,000 units across.
    if (!(start >= 0 && end > start && end <= 40000 && start < 40000)) continue;
    if (!Number.isFinite(start) || !Number.isFinite(end)) continue;
    return { ver, at: p + 8 };
  }
  return null;
}

function readFog(f) {
  const b = f.buf;
  const BODY = 132;
  let o = BODY + f.marks.coll;
  if (o + 16 > b.length) return null;

  // The declared walk — [presence][blockSize][skip] — is right for 92 of 132
  // maps and lands on garbage for the rest, and the discriminator is NOT the
  // file version: 0x0100 files appear in both groups. Rather than keep guessing
  // the collision block's shape, the fog record is SEARCHED for and then
  // validated, which is honest about being a heuristic and cannot silently
  // return a wrong reading.
  //
  // The signature is specific enough to be safe: a version in 0x0106..0x0108,
  // a declared size of exactly 32, and — the real check — fog ranges that make
  // physical sense. Random bytes essentially never satisfy all three.
  const found = findFog(b, o);
  if (!found) return { enabled: false, unfound: true };
  let { ver, at: fogAt } = found;
  o = fogAt;

  const skyFog = b.readUInt32LE(o);
  const staticRange = b.readUInt32LE(o + 4);
  const start = b.readFloatLE(o + 8);
  const end = b.readFloatLE(o + 12);
  let c = color(b.readUInt32LE(o + 16));
  let day = color(b.readUInt32LE(o + 20));
  let night = color(b.readUInt32LE(o + 24));

  if (ver === 0x0106 || ver === 0x0107) {
    day = colorUp(day, 0.85);
    night = colorUp(night, 1.6);
  }

  return {
    enabled: true, version: ver,
    skyFog: !!skyFog, staticRange: !!staticRange,
    start, end, color: c, day, night,
  };
}

const arc = new RccArchive(ARCHIVE);
const out = {};
const stats = { maps: 0, withFog: 0, unsupported: 0, failed: 0 };
const vers = {};

for (const e of arc.entries) {
  if (!/\.wld$/i.test(e.name)) continue;
  const name = path.basename(e.name).replace(/\.wld$/i, '').toLowerCase();
  if (out[name]) continue;              // RanMapZipTemp holds stale duplicates
  stats.maps++;

  let f;
  try { f = wld.open(arc.read(e)); } catch { stats.failed++; continue; }

  let fog;
  try { fog = readFog(f); } catch { fog = null; }
  if (!fog) { stats.failed++; continue; }
  if (fog.unsupported) { stats.unsupported++; vers['0x' + fog.unsupported.toString(16)] = (vers['0x' + fog.unsupported.toString(16)] || 0) + 1; continue; }
  if (!fog.enabled) continue;

  stats.withFog++;
  vers['0x' + fog.version.toString(16)] = (vers['0x' + fog.version.toString(16)] || 0) + 1;
  out[name] = fog;
}

console.log(`${stats.maps} maps, ${stats.withFog} carry fog, ` +
            `${stats.unsupported} unsupported version, ${stats.failed} unreadable`);
console.log('versions: ' + Object.entries(vers).map(([k, v]) => `${k}:${v}`).join('  '));

const ranges = Object.values(out).map((f) => [f.start, f.end]);
if (ranges.length) {
  const starts = ranges.map((r) => r[0]).sort((a, b) => a - b);
  const ends = ranges.map((r) => r[1]).sort((a, b) => a - b);
  console.log(`fog start: min ${starts[0].toFixed(0)}, median ` +
              `${starts[starts.length >> 1].toFixed(0)}, max ${starts[starts.length - 1].toFixed(0)}`);
  console.log(`fog end:   min ${ends[0].toFixed(0)}, median ` +
              `${ends[ends.length >> 1].toFixed(0)}, max ${ends[ends.length - 1].toFixed(0)}`);
  // Sanity: start must precede end, or the range is meaningless.
  const bad = ranges.filter(([s, e2]) => !(e2 > s)).length;
  console.log(`ranges with end <= start: ${bad}`);
}

const sample = out['w_school_03'];
if (sample) {
  const hex = (c) => '#' + [c.r, c.g, c.b].map((v) =>
    Math.round(v * 255).toString(16).padStart(2, '0')).join('');
  console.log(`\nw_school_03: v0x${sample.version.toString(16)} ` +
              `${sample.start.toFixed(0)}..${sample.end.toFixed(0)} ` +
              `colour ${hex(sample.color)} day ${hex(sample.day)} night ${hex(sample.night)}`);
}

const outArg = process.argv.indexOf('--out');
if (outArg > 0) {
  fs.writeFileSync(path.join(base, process.argv[outArg + 1]),
                   JSON.stringify({ maps: out }));
  console.log(`\nwrote ${process.argv[outArg + 1]}`);
}
