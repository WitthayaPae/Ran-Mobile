'use strict';
//
// RCC archive writer — rebuild an archive with some entries replaced.
//
//   node rcc-pack.js <in.rcc> <out.rcc> <name>=<file> [<name>=<file> ...]
//
// The GUI ships packed (Gui.rcc) and the mobile client arms the zip path
// unconditionally (ran_app.cpp), so a loose XML edit is invisible on the
// device: the loose-file path is structurally unreachable once the flag is on.
// Every GUI change therefore has to go back into the archive, and the only
// existing way to do that was Editor_RCC.exe's "Pack RCC" button — a Windows
// dialog, which is no use from a script.
//
// Rules taken from SOURCE_RCC.md, not guessed:
//
//  * exactly ONE XOR pass total. Entries that are being replaced are encrypted
//    here, once. Entries that are not are copied through as stored bytes, so
//    they cannot gain or lose a pass.
//  * entry names are flat, bare and lowercase; CUnzipper looks an entry up as
//    zipPath + bareFilename.
//  * store/deflate only, which is what the reader supports.
//
// The result is verified before it is written: every entry is read back out of
// the new archive and compared with what went in.
//
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');
const { RccArchive, decrypt, encrypt, readDirectory } = require('./rcc.js');

const SIG_CEN = 0x02014b50;
const SIG_LOC = 0x04034b50;
const SIG_EOCD = 0x06054b50;

//  CRC-32, the zip flavour. Node has no public crc32, and pulling zlib's out
//  through a stream is slower than the table.
const CRC_TABLE = (() => {
  const t = new Int32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c;
  }
  return t;
})();

function crc32(buf) {
  let c = 0 ^ -1;
  for (let i = 0; i < buf.length; i++) c = (c >>> 8) ^ CRC_TABLE[(c ^ buf[i]) & 0xff];
  return (c ^ -1) >>> 0;
}

/**
 * Build a zip from a list of { name, data } where data is the STORED payload
 * (already RCC-encrypted). Deflated, because the source archives are.
 */
function buildZip(items) {
  const locals = [];
  const centrals = [];
  let offset = 0;

  for (const it of items) {
    const deflated = zlib.deflateRawSync(it.data, { level: 9 });
    //  Only take the compression if it actually helps; a stored entry reads
    //  back the same either way.
    const useDeflate = deflated.length < it.data.length;
    const payload = useDeflate ? deflated : it.data;
    const method = useDeflate ? 8 : 0;
    const crc = crc32(it.data);
    const nameBuf = Buffer.from(it.name, 'latin1');

    const loc = Buffer.alloc(30 + nameBuf.length);
    loc.writeUInt32LE(SIG_LOC, 0);
    loc.writeUInt16LE(20, 4);           // version needed
    loc.writeUInt16LE(0, 6);            // flags
    loc.writeUInt16LE(method, 8);
    loc.writeUInt16LE(0, 10);           // mod time
    loc.writeUInt16LE(0, 12);           // mod date
    loc.writeUInt32LE(crc, 14);
    loc.writeUInt32LE(payload.length, 18);
    loc.writeUInt32LE(it.data.length, 22);
    loc.writeUInt16LE(nameBuf.length, 26);
    loc.writeUInt16LE(0, 28);           // extra
    nameBuf.copy(loc, 30);

    const cen = Buffer.alloc(46 + nameBuf.length);
    cen.writeUInt32LE(SIG_CEN, 0);
    cen.writeUInt16LE(20, 4);           // version made by
    cen.writeUInt16LE(20, 6);           // version needed
    cen.writeUInt16LE(0, 8);            // flags
    cen.writeUInt16LE(method, 10);
    cen.writeUInt16LE(0, 12);
    cen.writeUInt16LE(0, 14);
    cen.writeUInt32LE(crc, 16);
    cen.writeUInt32LE(payload.length, 20);
    cen.writeUInt32LE(it.data.length, 24);
    cen.writeUInt16LE(nameBuf.length, 28);
    cen.writeUInt16LE(0, 30);           // extra
    cen.writeUInt16LE(0, 32);           // comment
    cen.writeUInt16LE(0, 34);           // disk
    cen.writeUInt16LE(0, 36);           // internal attrs
    cen.writeUInt32LE(0, 38);           // external attrs
    cen.writeUInt32LE(offset, 42);
    nameBuf.copy(cen, 46);

    locals.push(loc, payload);
    centrals.push(cen);
    offset += loc.length + payload.length;
  }

  const cenBuf = Buffer.concat(centrals);
  const eocd = Buffer.alloc(22);
  eocd.writeUInt32LE(SIG_EOCD, 0);
  eocd.writeUInt16LE(0, 4);
  eocd.writeUInt16LE(0, 6);
  eocd.writeUInt16LE(items.length, 8);
  eocd.writeUInt16LE(items.length, 10);
  eocd.writeUInt32LE(cenBuf.length, 12);
  eocd.writeUInt32LE(offset, 16);
  eocd.writeUInt16LE(0, 20);

  return Buffer.concat([...locals, cenBuf, eocd]);
}

function pack(inPath, outPath, replacements) {
  const src = fs.readFileSync(inPath);
  const entries = readDirectory(src);
  const srcArchive = new RccArchive(inPath);

  const wanted = new Map();
  for (const [name, file] of replacements) wanted.set(name.toLowerCase(), file);

  const items = [];
  const used = new Set();

  for (const e of entries) {
    const key = e.name.toLowerCase();
    if (wanted.has(key)) {
      const plain = fs.readFileSync(wanted.get(key));
      items.push({ name: e.name, data: encrypt(Buffer.from(plain)), plain });
      used.add(key);
      console.log(`  replace ${e.name} (${plain.length} bytes)`);
    } else {
      //  Untouched: hand back the encrypted bytes exactly as they were stored.
      const plain = srcArchive.read(e);
      items.push({ name: e.name, data: encrypt(Buffer.from(plain)), plain });
    }
  }

  //  Anything asked for that the archive did not already hold is an addition.
  for (const [key, file] of wanted) {
    if (used.has(key)) continue;
    const plain = fs.readFileSync(file);
    items.push({ name: path.basename(file).toLowerCase(), data: encrypt(Buffer.from(plain)), plain });
    console.log(`  add     ${path.basename(file)} (${plain.length} bytes)`);
  }

  const out = buildZip(items);

  //  Verify before writing: read every entry back and compare.
  const tmp = outPath + '.verify';
  fs.writeFileSync(tmp, out);
  let bad = 0;
  try {
    const back = new RccArchive(tmp);
    if (back.length !== items.length) {
      console.error(`entry count ${back.length} != ${items.length}`);
      bad++;
    }
    for (const it of items) {
      const got = back.read(it.name);
      if (Buffer.compare(got, it.plain) !== 0) {
        console.error(`MISMATCH ${it.name}`);
        bad++;
      }
    }
  } finally {
    fs.unlinkSync(tmp);
  }
  if (bad) {
    console.error(`${bad} problem(s) — not written`);
    process.exit(1);
  }

  fs.writeFileSync(outPath, out);
  console.log(`${outPath}: ${items.length} entries, ${(out.length / 1048576).toFixed(2)} MB, verified`);
}

if (require.main === module) {
  const [, , inPath, outPath, ...rest] = process.argv;
  if (!inPath || !outPath) {
    console.error('usage: node rcc-pack.js <in.rcc> <out.rcc> [<entry>=<file> ...]');
    process.exit(2);
  }
  const replacements = rest.map((a) => {
    const i = a.indexOf('=');
    if (i < 0) throw new Error(`bad replacement "${a}", expected name=file`);
    return [a.slice(0, i), a.slice(i + 1)];
  });
  pack(inPath, outPath, replacements);
}

module.exports = { pack, buildZip, crc32 };
