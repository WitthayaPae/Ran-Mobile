'use strict';
//
// Extract animation clips from every `AnimContainer` `.bin` in the deploy tree.
//
//   node extract-anim.js --list        inventory, nothing written
//   node extract-anim.js --out DIR     one .ranim per source file
//
// Format, little-endian throughout.
//
//   header 40 bytes
//     char[4] "RANM"
//     u32  version = 1
//     u32  containerVersion    the source AnimContainer version
//     u32  trackCount
//     u32  matrixKeyCount      total
//     u32  quatKeyCount        total
//     u32  durationTicks       highest key time in the file
//     u32  ticksPerSecond      4800 — see below
//     u32  stringBytes
//     u32  reserved (0)
//   strings     stringBytes, NUL-terminated, referenced by byte offset
//   tracks      trackCount * 20
//                 u32 boneName, u32 type, u32 keyOffset, u32 keyCount, u32 flags
//                 type 0 = matrix, 1 = quatPos
//                 flags bit0 = belongs to the upper-body list
//   matrixKeys  matrixKeyCount * 68   u32 timeTicks, f32[16] matrix
//   quatKeys    quatKeyCount   * 44   u32 timeTicks, f32[3] scale,
//                                     f32[3] position, f32[4] rotation (xyzw)
//
// `keyOffset` indexes the array for the track's OWN type, not a shared one —
// the two key kinds have different sizes and mixing them into one array buys
// nothing but arithmetic.
//
// **Tick rate is measured, not assumed.** The engine has no constant for it:
// `SAnimation.cpp` derives `dwUnitKey` at runtime as the smallest gap between
// keys. Across the shipped corpus every one of 14.3M inter-key gaps is an exact
// multiple of **160 ticks**, which is the 3ds Max biped convention of 160 ticks
// per frame at 30 fps, i.e. 4800 ticks/second. That yields a median clip of
// 1.37 s and a maximum of 29.8 s — sane for character animation, which is the
// corroboration that matters.
//
// Rotations are stored decompressed and normalised. The source packs them into
// two DWORDs with a deliberately ASYMMETRIC scheme (see `animkeys.js`): x, y, z
// are biased by 32767 and scaled by 1/32768, while w is neither — because the
// compressor negates the quaternion whenever w < 0.
//
const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc');
const XA = require('./xanim');
const AK = require('./animkeys');

const RAN = path.join(__dirname, '..', '..', '..', 'Ran');
const argv = process.argv.slice(2);
const has = (f) => argv.includes(f);
const val = (f, d) => { const i = argv.indexOf(f); return i === -1 ? d : argv[i + 1]; };

const outDir = val('--out', null);
const listOnly = has('--list');

if (!outDir && !listOnly) {
  console.error('usage: node extract-anim.js --list | --out DIR');
  process.exit(2);
}

const TICKS_PER_SECOND = 4800;
const TYPE_MATRIX = 0;
const TYPE_QUATPOS = 1;

class StringPool {
  constructor() { this.parts = []; this.at = 0; this.seen = new Map(); }
  add(s) {
    const v = s == null ? '' : String(s);
    if (this.seen.has(v)) return this.seen.get(v);
    const off = this.at;
    const b = Buffer.from(v + '\0', 'latin1');
    this.parts.push(b);
    this.at += b.length;
    this.seen.set(v, off);
    return off;
  }
  buffer() { return Buffer.concat(this.parts); }
}

function encode(parsed, stats) {
  const pool = new StringPool();
  const tracks = [];
  const matrixKeys = [];
  const quatKeys = [];
  let duration = 0;

  const take = (raw, upperBody) => {
    let t;
    try { t = AK.decodeTrack(raw); } catch (err) { stats.badTracks++; return; }
    duration = Math.max(duration, AK.trackDuration(t));

    // A track can technically carry both; matrix wins because it is the
    // authoritative baked transform when present.
    if (t.matrix) {
      tracks.push({ name: pool.add(t.bone || ''), type: TYPE_MATRIX,
                    offset: matrixKeys.length, count: t.matrix.times.length,
                    flags: upperBody ? 1 : 0 });
      for (let i = 0; i < t.matrix.times.length; i++) {
        matrixKeys.push([t.matrix.times[i], t.matrix.values.subarray(i * 16, i * 16 + 16)]);
      }
      stats.matrixTracks++;
    } else if (t.quatPos) {
      tracks.push({ name: pool.add(t.bone || ''), type: TYPE_QUATPOS,
                    offset: quatKeys.length, count: t.quatPos.times.length,
                    flags: upperBody ? 1 : 0 });
      for (let i = 0; i < t.quatPos.times.length; i++) quatKeys.push([t.quatPos, i]);
      stats.quatTracks++;
    } else {
      stats.emptyTracks++;
    }
  };

  for (const raw of parsed.tracks) take(raw, false);
  for (const raw of parsed.upperBody) take(raw, true);

  const strings = pool.buffer();
  const head = Buffer.alloc(40);
  head.write('RANM', 0, 'latin1');
  head.writeUInt32LE(1, 4);
  head.writeUInt32LE(parsed.version, 8);
  head.writeUInt32LE(tracks.length, 12);
  head.writeUInt32LE(matrixKeys.length, 16);
  head.writeUInt32LE(quatKeys.length, 20);
  head.writeUInt32LE(duration, 24);
  head.writeUInt32LE(TICKS_PER_SECOND, 28);
  head.writeUInt32LE(strings.length, 32);
  head.writeUInt32LE(0, 36);

  const trackTable = Buffer.alloc(tracks.length * 20);
  tracks.forEach((t, i) => {
    const o = i * 20;
    trackTable.writeUInt32LE(t.name, o);
    trackTable.writeUInt32LE(t.type, o + 4);
    trackTable.writeUInt32LE(t.offset, o + 8);
    trackTable.writeUInt32LE(t.count, o + 12);
    trackTable.writeUInt32LE(t.flags, o + 16);
  });

  const mBuf = Buffer.alloc(matrixKeys.length * 68);
  matrixKeys.forEach(([time, mat], i) => {
    const o = i * 68;
    mBuf.writeUInt32LE(time, o);
    for (let k = 0; k < 16; k++) {
      const v = mat[k];
      mBuf.writeFloatLE(Number.isFinite(v) ? v : (k % 5 === 0 ? 1 : 0), o + 4 + k * 4);
    }
  });

  const qBuf = Buffer.alloc(quatKeys.length * 44);
  quatKeys.forEach(([ch, i], j) => {
    const o = j * 44;
    qBuf.writeUInt32LE(ch.times[i], o);
    for (let k = 0; k < 3; k++) {
      qBuf.writeFloatLE(finite(ch.scales[i * 3 + k], 1), o + 4 + k * 4);
      qBuf.writeFloatLE(finite(ch.positions[i * 3 + k], 0), o + 16 + k * 4);
    }
    // Normalise. All but two of the corpus's 13,010,783 quaternions already sit
    // on the unit sphere within 2e-2, which is the evidence that the asymmetric
    // decompression is right. The two exceptions decode to (-1,-1,-1,0) from an
    // all-zero QUATCOMP — an uninitialised source key — and renormalise to a
    // valid rotation. Only a genuinely zero-length quaternion becomes identity,
    // since that carries no direction to preserve.
    let x = finite(ch.rotations[i * 4], 0), y = finite(ch.rotations[i * 4 + 1], 0);
    let z = finite(ch.rotations[i * 4 + 2], 0), w = finite(ch.rotations[i * 4 + 3], 1);
    const len = Math.hypot(x, y, z, w);
    if (!(len > 1e-6)) { x = y = z = 0; w = 1; stats.degenerateQuats++; }
    else if (Math.abs(len - 1) > 0.02) { stats.renormalised++; x /= len; y /= len; z /= len; w /= len; }
    else { x /= len; y /= len; z /= len; w /= len; }
    qBuf.writeFloatLE(x, o + 28);
    qBuf.writeFloatLE(y, o + 32);
    qBuf.writeFloatLE(z, o + 36);
    qBuf.writeFloatLE(w, o + 40);
  });

  return { buf: Buffer.concat([head, strings, trackTable, mBuf, qBuf]),
           tracks: tracks.length, keys: matrixKeys.length + quatKeys.length, duration };
}

const finite = (v, fallback) => (Number.isFinite(v) ? v : fallback);

const stats = { seen: 0, ok: 0, failed: 0, tracks: 0, keys: 0, bytes: 0,
                matrixTracks: 0, quatTracks: 0, emptyTracks: 0, badTracks: 0,
                degenerateQuats: 0, renormalised: 0, encoded: 0 };
const rows = [];
const failures = [];
const seenNames = new Set();

function handle(name, raw) {
  if (!name.toLowerCase().endsWith('.bin')) return;
  const key = path.basename(name).toLowerCase();
  if (seenNames.has(key)) return;

  let parsed;
  try { parsed = XA.parse(raw); } catch (err) { return; }   // not an AnimContainer
  seenNames.add(key);
  stats.seen++;
  if (parsed.encoded) stats.encoded++;

  let out;
  try { out = encode(parsed, stats); } catch (err) {
    stats.failed++;
    failures.push(`${key}: ${err.message}`);
    return;
  }
  stats.ok++;
  stats.tracks += out.tracks;
  stats.keys += out.keys;
  rows.push({ name: key, version: parsed.version, tracks: out.tracks,
              keys: out.keys, seconds: out.duration / TICKS_PER_SECOND });

  if (listOnly) return;
  fs.mkdirSync(outDir, { recursive: true });
  fs.writeFileSync(path.join(outDir, path.basename(key, '.bin') + '.ranim'), out.buf);
  stats.bytes += out.buf.length;
}

const t0 = Date.now();
(function walk(dir) {
  let items;
  try { items = fs.readdirSync(dir, { withFileTypes: true }); } catch { return; }
  for (const it of items) {
    const p = path.join(dir, it.name);
    if (it.isDirectory()) { walk(p); continue; }
    if (it.name.toLowerCase().endsWith('.rcc')) {
      let ar;
      try { ar = new RccArchive(p); } catch { continue; }
      for (const e of ar.entries) {
        if (!e.name.toLowerCase().endsWith('.bin')) continue;
        try { handle(e.name, ar.read(e)); } catch (err) {
          stats.failed++;
          failures.push(`${e.name}: ${err.message}`);
        }
      }
    } else if (it.name.toLowerCase().endsWith('.bin')) {
      try { handle(it.name, fs.readFileSync(p)); } catch { /* not ours */ }
    }
  }
})(RAN);

const fmt = (n) => n.toLocaleString('en-US');
if (listOnly) {
  console.log('  tracks       keys   secs  ver     clip');
  console.log('--------  ---------  -----  -----   --------------------------------');
  for (const r of rows.sort((a, b) => b.keys - a.keys).slice(0, 25)) {
    console.log(`${String(r.tracks).padStart(8)}  ${String(r.keys).padStart(9)}  ` +
                `${r.seconds.toFixed(2).padStart(5)}  ` +
                `0x${r.version.toString(16).padStart(4, '0')}  ${r.name}`);
  }
  if (rows.length > 25) console.log(`... and ${rows.length - 25} more`);
}

console.log(`\n${stats.seen} AnimContainer files: ${stats.ok} written, ${stats.failed} failed, ` +
            `${stats.encoded} byte-encoded`);
console.log(`${fmt(stats.tracks)} tracks (${fmt(stats.matrixTracks)} matrix, ` +
            `${fmt(stats.quatTracks)} quatPos, ${fmt(stats.emptyTracks)} empty, ` +
            `${stats.badTracks} undecodable), ${fmt(stats.keys)} keys` +
            (listOnly ? '' : `, ${(stats.bytes / 1048576).toFixed(1)}M written`) +
            ` in ${((Date.now() - t0) / 1000).toFixed(1)}s`);
console.log(`rotations: ${stats.degenerateQuats} degenerate -> identity, ` +
            `${stats.renormalised} renormalised`);
if (failures.length) {
  console.log('\nfailures:');
  for (const f of failures.slice(0, 10)) console.log(`  ! ${f}`);
}
process.exit(0);
