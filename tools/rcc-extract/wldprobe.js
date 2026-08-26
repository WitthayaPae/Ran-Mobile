'use strict';
//
// Reference reader for the .wld land file, following DxLandMan::LoadFile_VER110
// byte for byte with WIN32 type sizes (the sizes the file was written with).
//
// Its job is not to load a map — it is to say where each phase of the load
// starts, so the same offsets logged on device show which read goes wrong on a
// 64-bit build.
//
// Usage: node wldprobe.js <path-to.wld> [--dynamic]
//
const fs = require('fs');

const FILETYPESIZE = 128;
const MAXLANDNAME = 128;

class Reader {
  constructor(buf, start) { this.b = buf; this.p = start; }
  u32() { const v = this.b.readUInt32LE(this.p); this.p += 4; return v; }
  i32() { const v = this.b.readInt32LE(this.p); this.p += 4; return v; }
  f32() { const v = this.b.readFloatLE(this.p); this.p += 4; return v; }
  skip(n) { this.p += n; }
  str(n) { const s = this.b.toString('latin1', this.p, this.p + n); this.p += n; return s.replace(/\0.*$/, ''); }
  left() { return this.b.length - this.p; }
}

function octree(r, dynamic, depth, stats) {
  stats.nodes++;
  r.u32();                       // BOOL m_bSubDivided
  r.skip(12 + 12);               // vMax, vMin
  r.u32();                       // m_DataAddress
  const dataSize = r.u32();      // m_DataSize
  if (dynamic) {
    r.skip(dataSize);            // the frame payload is loaded on demand
  } else {
    const exists = r.u32();
    if (exists) throw new Error('non-dynamic frame parse not implemented (offset ' + r.p + ')');
  }
  for (let i = 0; i < 8; ++i) {
    const exists = r.u32();
    if (exists) octree(r, dynamic, depth + 1, stats);
  }
}

function main() {
  const path = process.argv[2];
  const dynamic = process.argv.includes('--dynamic');
  const buf = fs.readFileSync(path);

  const type = buf.toString('latin1', 0, FILETYPESIZE).replace(/\0.*$/, '');
  const version = buf.readUInt32LE(FILETYPESIZE);
  console.log(`file      ${path}`);
  console.log(`type      "${type}"  version 0x${version.toString(16)}  size ${buf.length}`);
  if (type !== 'LAND.MAN') {
    console.log('(encrypted or unknown header — this probe only reads plain LAND.MAN)');
    return;
  }

  const r = new Reader(buf, FILETYPESIZE + 4);
  console.log(`@${r.p}  header end`);

  const mapId = r.u32();
  const name = r.str(MAXLANDNAME);
  console.log(`@${r.p}  map id 0x${mapId.toString(16)} name "${name}"`);

  const markVer = r.u32(), markSize = r.u32();
  console.log(`@${r.p}  filemark version 0x${markVer.toString(16)} size ${markSize}`);
  if (markVer === 0x0101) r.skip(16);
  else if (markVer === 0x0100) r.skip(16);
  else r.skip(markSize);
  console.log(`@${r.p}  after filemark`);

  const stats = { nodes: 0 };
  try {
    octree(r, dynamic, 0, stats);
    console.log(`@${r.p}  after octree (${stats.nodes} nodes, dynamic=${dynamic})`);
  } catch (e) {
    console.log(`octree stopped: ${e.message}`);
    return;
  }

  const hasAni = r.u32();
  console.log(`@${r.p}  animation manager present = ${hasAni}`);
  if (hasAni) { console.log('(animation manager body not parsed by this probe)'); return; }

  let pieces = 0;
  let exists = r.u32();
  while (exists) {
    r.f32();                                  // fCurTime
    if (r.u32()) { const n = r.i32(); r.skip(n); }     // file name
    if (r.u32()) { if (r.u32()) { const n = r.i32(); r.skip(n); } }  // frame name
    r.skip(64);                               // D3DXMATRIX
    ++pieces;
    exists = r.u32();
  }
  console.log(`@${r.p}  after ${pieces} replace pieces`);

  const effectCount = r.u32();
  console.log(`@${r.p}  effect count ${effectCount}  (bytes left ${r.left()})`);
}

main();
