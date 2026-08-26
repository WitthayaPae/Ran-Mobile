'use strict';
//
// The login camera path, from log_in.wld.
//
//   node extract-logincam.js
//   node extract-logincam.js --out MOBILE/assets/logincam.json
//
// The PC login (DxLobyStage) loads log_in.wld and plays its "Camera01" keyframe
// animation via DxCamAniMan in CAMERA_TFREE mode. This extracts that exact path
// so the mobile login camera moves like the client's, not an approximation.
//
// The data is a DxAnimation matrix-key track (SMatrixKey: dwTime u32 at 0, a
// 16-byte-aligned D3DXMATRIXA16 at offset 16 → the struct is 80 bytes). The
// camera position is the matrix translation (_41.._43, i.e. matrix bytes
// 48..56). The look point is computed by the engine as (0,-40,0) transformed by
// the same matrix (FrameMove/CAMERA_TFREE), which reduces to
// pos + (-40)*row2 because only the Y component is non-zero — row2 is the
// matrix's second row (_21.._23). Row2 is constant across all keys here, so the
// look direction is fixed while the camera dollies.
//
// Times are in animation units; UNITANIKEY_PERSEC = 4800 converts to seconds
// (DxAniKeys.h). The final keys return to the first position, so the path is a
// loop.
const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc.js');

const base = path.resolve(__dirname, '../../..');
const ARCHIVE = path.join(base, 'CLIENT/data/map/Map.rcc');
const UNITANIKEY_PERSEC = 4800;
const KEY_SIZE = 80;        // SMatrixKey
const MAT_OFF = 16;         // matrix within the key
const LOOK_Y = -40;         // the (0,-40,0) look offset the engine applies

/**
 * Find the camera matrix-key track: a count followed by N 80-byte keys whose
 * times increase and whose translations span a real distance (a path, not a
 * single identity key). Scanned rather than hard-offset so a different login
 * map still resolves.
 */
function findCameraTrack(b) {
  for (let cnt = 0; cnt < b.length - 8; cnt++) {
    const n = b.readUInt32LE(cnt);
    if (n < 4 || n > 4000) continue;
    const s = cnt + 4;
    if (s + n * KEY_SIZE > b.length) continue;

    let ok = true, prevT = -1;
    let minX = Infinity, maxX = -Infinity, minZ = Infinity, maxZ = -Infinity;
    for (let i = 0; i < n; i++) {
      const k = s + i * KEY_SIZE;
      const t = b.readUInt32LE(k);
      const m44 = b.readFloatLE(k + MAT_OFF + 60);
      const x = b.readFloatLE(k + MAT_OFF + 48);
      const z = b.readFloatLE(k + MAT_OFF + 56);
      if (t < prevT || t > 500000 || Math.abs(m44 - 1) > 0.01 || !Number.isFinite(x)) { ok = false; break; }
      prevT = t;
      minX = Math.min(minX, x); maxX = Math.max(maxX, x);
      minZ = Math.min(minZ, z); maxZ = Math.max(maxZ, z);
    }
    if (ok && Math.max(maxX - minX, maxZ - minZ) > 100) return { at: cnt, count: n, keysAt: s };
  }
  return null;
}

// --map NAME extracts NAME.wld's camera instead of log_in (for cha_select).
const mapArg = (() => { const i = process.argv.indexOf('--map'); return i >= 0 ? process.argv[i + 1] : 'log_in'; })();
const mapRe = new RegExp('(^|/)' + mapArg.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + '\\.wld$', 'i');

const arc = new RccArchive(ARCHIVE);
const entry = arc.entries.find((e) => mapRe.test(e.name));
if (!entry) throw new Error(`${mapArg}.wld not found in Map.rcc`);
const b = arc.read(entry);

const track = findCameraTrack(b);
if (!track) throw new Error(`no camera matrix-key track found in ${mapArg}.wld`);

const keys = [];
for (let i = 0; i < track.count; i++) {
  const k = track.keysAt + i * KEY_SIZE;
  const t = b.readUInt32LE(k);
  const m = k + MAT_OFF;
  const pos = [b.readFloatLE(m + 48), b.readFloatLE(m + 52), b.readFloatLE(m + 56)];
  const row2 = [b.readFloatLE(m + 16), b.readFloatLE(m + 20), b.readFloatLE(m + 24)];
  keys.push({ t: t / UNITANIKEY_PERSEC, pos, row2 });
}

// The look offset the engine adds: (0,LOOK_Y,0) transformed = LOOK_Y * row2.
// row2 is constant, so take the first key's.
const r2 = keys[0].row2;
const lookOffset = [LOOK_Y * r2[0], LOOK_Y * r2[1], LOOK_Y * r2[2]];

// Drop a trailing return-to-start key. The camera flag is CAMERA_FREESET with
// NO CAMERA_ANILOOP, so the engine plays the path ONCE and stops — it does not
// loop. The authored track still ends by snapping back to the first position
// (a leftover of the loop variant); replaying that would add a jump the client
// never shows. Kept up to and including the HOLD (last key that matches the
// settle position), so playback dollies in and settles on the composed view.
let end = keys.length;
const settle = keys[keys.length - 1].pos;
const dist = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
if (keys.length >= 3 && dist(keys[keys.length - 1].pos, keys[0].pos) < 2) {
  end = keys.length - 1;   // drop the loop-back key; keep the hold before it
}
const played = keys.slice(0, end);

const out = {
  keys: played.map((k) => ({ t: k.t, pos: k.pos })),
  lookOffset,
  duration: played[played.length - 1].t,
  loop: false,             // CAMERA_FREESET: play once, then hold
};

console.log(`${keys.length} keys, ${out.duration.toFixed(2)}s, loop=${out.loop}`);
console.log(`  from (${keys[0].pos.map((v) => v.toFixed(0)).join(',')}) ` +
            `to (${keys[track.count > 6 ? 5 : keys.length - 1].pos.map((v) => v.toFixed(0)).join(',')})`);
console.log(`  look offset (${lookOffset.map((v) => v.toFixed(1)).join(',')})`);

const at = process.argv.indexOf('--out');
if (at > 0) {
  fs.writeFileSync(path.join(base, process.argv[at + 1]), JSON.stringify(out));
  console.log(`wrote ${process.argv[at + 1]}`);
}

module.exports = { findCameraTrack };
