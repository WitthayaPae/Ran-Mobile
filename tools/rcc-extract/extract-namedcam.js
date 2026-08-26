'use strict';
//
// A named lobby camera's held viewpoint.
//
//   node extract-namedcam.js <map> <cameraName> --out FILE.json
//   e.g. node extract-namedcam.js cha_select select_character --out ../../assets/chaselectcam.json
//
// The login and character-select cameras are DxCameraAni entries with
// STime=ETime=0 — frozen at their track's first key (DxCamAniMan::FrameMove
// clamps the age into an empty window). extract-logincam handles the login
// case (its track is scanned heuristically); this reads a NAMED camera's local
// DxAnimation track and emits its first key, which is the whole viewpoint.
//
// DxCameraAni::Load ver 0x0102 (DxCamAniMan.cpp:245):
//   [u32 nameLen][name][u32 type][u32 flag][f32 STime][f32 ETime]
//   [u32 fromLen][fromName][BOOL localFrom] -> DxAnimation (from/eye track)
//   [u32 targetLen][targetName][BOOL localTarget] -> DxAnimation (look track)
//
// DxAnimation::LoadFile (DxAnimationSaveLoad.cpp:68):
//   [u32 cPos] SPositionKey[16], [u32 cRot] SRotateKey[20], [u32 cScale]
//   SScaleKey[16], [u32 cMatrix] SMatrixKey[80]  (in-memory key sizes)
//
// The engine's look point is (0,-40,0) transformed by the eye matrix — with a
// constant row-2 that reduces to pos + (-40)*row2 (extract-logincam's note).
//
const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc');
const W = require('./wld');

const LOOK_Y = -40;
const MAT_OFF = 16;   // SMatrixKey: u32 time, 12 pad to A16, then the matrix
const KEY_SIZE = 80;

const [map, camName] = process.argv.slice(2);
if (!map || !camName) {
  console.error('usage: node extract-namedcam.js <map> <cameraName> --out FILE.json');
  process.exit(2);
}
const RAN = path.join(__dirname, '..', '..', '..', 'Ran');
const arc = new RccArchive(path.join(RAN, 'data', 'map', 'Map.rcc'));
let entry = null;
for (const e of arc.list()) {
  if (path.basename(e.name || e).toLowerCase() === `${map}.wld`) { entry = e; break; }
}
if (!entry) throw new Error(`${map}.wld not found`);
const b = W.open(arc.read(entry)).buf;

const needle = Buffer.from(camName + '\0', 'latin1');
const at = b.indexOf(needle);
if (at < 0) throw new Error(`camera "${camName}" not found in ${map}.wld`);

let o = at - 4;                              // the u32 name-length prefix
const nameLen = b.readUInt32LE(o); o += 4;
o += nameLen;                                // past the name (incl. NUL)
const u32 = () => { const v = b.readUInt32LE(o); o += 4; return v; };
const f32 = () => { const v = b.readFloatLE(o); o += 4; return v; };

const type = u32(), flag = u32(), st = f32(), et = f32();
const fromLen = u32();
const fromName = b.toString('latin1', o, o + fromLen - 1); o += fromLen;
const localFrom = u32();
if (localFrom !== 1) throw new Error('camera has no local eye track');

const np = u32(); o += np * 16;
const nr = u32(); o += nr * 20;
const nsc = u32(); o += nsc * 16;
const nm = u32();
if (nm < 1) throw new Error('camera eye track has no matrix keys');

const k = o, m = k + MAT_OFF;
const pos = [b.readFloatLE(m + 48), b.readFloatLE(m + 52), b.readFloatLE(m + 56)];
const row2 = [b.readFloatLE(m + 16), b.readFloatLE(m + 20), b.readFloatLE(m + 24)];
const lookOffset = [LOOK_Y * row2[0], LOOK_Y * row2[1], LOOK_Y * row2[2]];

// Emit in the same shape RanLoginBackdrop reads: a single held key, hold=true.
const out = {
  keys: [{ t: 0, pos }],
  lookOffset,
  duration: 0,
  loop: false,
  holdFirstKey: true,
};

console.log(`${camName}: from="${fromName}" type=${type} flag=${flag} keys=${nm} ` +
            `(STime=${st}, ETime=${et} -> frozen)`);
console.log(`  pos (${pos.map((v) => v.toFixed(1)).join(',')}) ` +
            `look (${lookOffset.map((v) => v.toFixed(1)).join(',')})`);

const oi = process.argv.indexOf('--out');
if (oi >= 0) {
  fs.writeFileSync(path.resolve(process.argv[oi + 1]), JSON.stringify(out));
  console.log(`-> ${process.argv[oi + 1]}`);
}

module.exports = { pos, lookOffset };
