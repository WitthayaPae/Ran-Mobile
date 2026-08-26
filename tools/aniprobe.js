//  Read RAN animation .cfg / .bin headers offline.
//
//  The client splits every animation into an "up body" track list and a main
//  list, and applies only one of them when an upper-body animation is playing.
//  When a character's arms sit in bind pose while its legs move, the question
//  is which list the arm tracks are actually in - this answers that from the
//  shipped data, with no device involved.
//
//  usage: node aniprobe.js <CLIENT/data/animation> [name-substring]
const fs = require('fs');
const path = require('path');

const ACF_UPBODY = 0x0100;

function readCfg(file) {
  const b = fs.readFileSync(file);
  //  CSerialFile header: a 128-byte type string, then a DWORD version, then
  //  the struct written whole.
  const type = b.toString('latin1', 0, 32).replace(/\0.*$/, '').trim();
  // The name/skeleton fields are the two 128-byte strings that follow.
  const name = b.toString('latin1', 130, 258).replace(/\0.*$/, '').trim();
  const ske  = b.toString('latin1', 258, 386).replace(/\0.*$/, '').trim();
  //  Scan a small window for the flag DWORD: the layout after the two strings
  //  is flag, sTime, eTime, eTimeOrig, unitTime.
  const off = 386;
  const flag = b.readUInt32LE(off);
  return { type, name, ske, flag, sTime: b.readUInt32LE(off + 4), eTime: b.readUInt32LE(off + 8) };
}

function readBinCounts(file) {
  const b = fs.readFileSync(file);
  //  File type string then version, then (v0200) the up-body count and the
  //  main count, each followed by that many serialised animations.
  let off = 0;
  const type = b.toString('latin1', 0, 32).replace(/\0.*$/, '').trim();
  //  The type block is fixed width; find the version DWORD right after it by
  //  looking for the first plausible version value.
  for (let p = 32; p < 200; p += 2) {
    const v = b.readUInt32LE(p);
    if (v === 0x0200 || v === 0x0104 || v === 0x0103 || v === 0x0102 || v === 0x0101 || v === 0x0100) {
      off = p + 4;
      return { type, version: v.toString(16), verOff: p, up: b.readUInt32LE(off), main: b.readUInt32LE(off + 4) };
    }
  }
  return { type, version: '?', up: -1, main: -1 };
}

const dir = process.argv[2] || '.';
const filter = (process.argv[3] || '').toLowerCase();
const files = fs.readdirSync(dir).filter(f => f.toLowerCase().endsWith('.cfg'))
  .filter(f => !filter || f.toLowerCase().includes(filter));

let upFlagged = 0;
for (const f of files.slice(0, 40)) {
  const cfg = readCfg(path.join(dir, f));
  const binPath = path.join(dir, f.replace(/\.cfg$/i, '.bin'));
  const bin = fs.existsSync(binPath) ? readBinCounts(binPath) : { version: '-', up: '-', main: '-' };
  if (cfg.flag & ACF_UPBODY) ++upFlagged;
  console.log(
    f.padEnd(28) +
    ' flag=0x' + cfg.flag.toString(16).padStart(4, '0') +
    (cfg.flag & ACF_UPBODY ? ' UPBODY' : '       ') +
    ' ske=' + String(cfg.ske).padEnd(10) +
    ' ver=' + bin.version +
    ' tracks up=' + String(bin.up).padStart(4) + ' main=' + String(bin.main).padStart(4));
}
console.log('\n' + files.length + ' cfg files matched, ' + upFlagged + ' of the first 40 flagged UPBODY');
