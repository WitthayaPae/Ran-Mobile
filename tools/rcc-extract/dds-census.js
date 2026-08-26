'use strict';
//
// DDS format census.
//
// 15,445 textures / 2.6 GB is the largest tractable asset class, and the
// conversion target depends entirely on what is actually in them: DXT1/3/5
// transcode to ETC2/ASTC cleanly, uncompressed ARGB needs encoding from
// scratch, and cubemaps/volume textures need different handling again.
//
//   node dds-census.js            summary by pixel format
//   node dds-census.js --sizes    also break down by dimensions
//
const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc');

const RAN = path.join(__dirname, '..', '..', '..', 'Ran');
const withSizes = process.argv.includes('--sizes');

// DDS_HEADER, 124 bytes after the 4-byte magic.
const DDPF_FOURCC = 0x4;
const DDPF_RGB = 0x40;
const DDPF_ALPHAPIXELS = 0x1;
const DDSCAPS2_CUBEMAP = 0x200;
const DDSCAPS2_VOLUME = 0x200000;

function parseDds(buf) {
  if (buf.length < 128 || buf.readUInt32LE(0) !== 0x20534444) return null; // 'DDS '
  const h = 4;
  const flags = buf.readUInt32LE(h + 4);
  const height = buf.readUInt32LE(h + 8);
  const width = buf.readUInt32LE(h + 12);
  const mipCount = buf.readUInt32LE(h + 24);
  const pf = h + 72;
  const pfFlags = buf.readUInt32LE(pf + 4);
  const fourCC = buf.toString('latin1', pf + 8, pf + 12);
  const rgbBits = buf.readUInt32LE(pf + 12);
  const caps2 = buf.readUInt32LE(h + 108);

  let format;
  if (pfFlags & DDPF_FOURCC) format = fourCC.replace(/\0/g, '') || '(fourcc 0)';
  else if (pfFlags & DDPF_RGB) {
    format = `RGB${rgbBits}${(pfFlags & DDPF_ALPHAPIXELS) ? 'A' : ''}`;
  } else format = `raw(flags 0x${pfFlags.toString(16)})`;

  return {
    format, width, height,
    mips: (flags & 0x20000) ? mipCount : 1,
    cubemap: !!(caps2 & DDSCAPS2_CUBEMAP),
    volume: !!(caps2 & DDSCAPS2_VOLUME),
  };
}

/**
 * Identify a file by CONTENT, not by name.
 *
 * 247 files named `.dds` are not DDS at all — 109 PNG, 133 TGA, 2 PSD, plus a
 * text file and an empty one. The engine loads textures through D3DX, which
 * sniffs the header, so the extension was never load-bearing. Any converter
 * that trusts the name will fail on 1.6% of the texture set.
 */
function sniff(buf) {
  if (buf.length === 0) return 'empty';
  if (buf.length >= 4 && buf.readUInt32LE(0) === 0x20534444) return 'DDS';
  if (buf.length >= 8 && buf.toString('hex', 0, 8) === '89504e470d0a1a0a') return 'PNG';
  if (buf.length >= 4 && buf.toString('latin1', 0, 4) === '8BPS') return 'PSD';
  if (buf.length >= 2 && buf.toString('latin1', 0, 2) === 'BM') return 'BMP';
  if (buf.length >= 3 && buf[0] === 0xff && buf[1] === 0xd8 && buf[2] === 0xff) return 'JPEG';
  // TGA has no magic; identify by a plausible header. Byte 2 is the image type
  // (2 = uncompressed true-colour, 10 = RLE true-colour) and the colour-map
  // fields must be consistent.
  if (buf.length >= 18) {
    const cmType = buf[1];
    const imgType = buf[2];
    if ((cmType === 0 || cmType === 1) && [1, 2, 3, 9, 10, 11].includes(imgType)) {
      return 'TGA';
    }
  }
  return 'unknown';
}

const byFormat = new Map();
const byActual = new Map();
const bySize = new Map();
let total = 0, bytes = 0, unparsed = 0, cubes = 0, npot = 0, big = 0;

const isPow2 = (n) => n > 0 && (n & (n - 1)) === 0;

function consider(name, data) {
  if (!name.toLowerCase().endsWith('.dds')) return;
  total++;
  bytes += data.length;
  const actual = sniff(data);
  byActual.set(actual, (byActual.get(actual) || 0) + 1);
  const d = parseDds(data);
  if (!d) { unparsed++; return; }

  const f = byFormat.get(d.format) || { n: 0, b: 0, mips: 0 };
  f.n++; f.b += data.length; if (d.mips > 1) f.mips++;
  byFormat.set(d.format, f);

  if (d.cubemap || d.volume) cubes++;
  if (!isPow2(d.width) || !isPow2(d.height)) npot++;
  if (d.width > 1024 || d.height > 1024) big++;

  const key = `${d.width}x${d.height}`;
  bySize.set(key, (bySize.get(key) || 0) + 1);
}

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
        if (!e.name.toLowerCase().endsWith('.dds')) continue;
        try { consider(e.name, ar.read(e)); } catch { unparsed++; total++; }
      }
    } else if (it.name.toLowerCase().endsWith('.dds')) {
      try { consider(it.name, fs.readFileSync(p)); } catch { unparsed++; total++; }
    }
  }
})(RAN);

console.log('   count         MB  withMips  format');
console.log('--------  ---------  --------  ----------');
for (const [f, v] of [...byFormat.entries()].sort((a, b) => b[1].b - a[1].b)) {
  console.log(`${String(v.n).padStart(8)}  ${(v.b / 1048576).toFixed(1).padStart(9)}  ` +
              `${String(v.mips).padStart(8)}  ${f}`);
}

console.log(`\n${total} DDS files, ${(bytes / 1048576).toFixed(1)}M`);
console.log('\nactual content type (sniffed, not trusted from the extension):');
for (const [k, n] of [...byActual.entries()].sort((a, b) => b[1] - a[1])) {
  console.log(`${String(n).padStart(8)}  ${k}`);
}
console.log(`\nnamed .dds but not DDS: ${unparsed}`);
console.log(`cubemap/volume: ${cubes}`);
console.log(`non-power-of-two: ${npot}`);
console.log(`larger than 1024: ${big}`);

if (withSizes) {
  console.log('\ntop dimensions:');
  for (const [k, n] of [...bySize.entries()].sort((a, b) => b[1] - a[1]).slice(0, 15)) {
    console.log(`${String(n).padStart(7)}  ${k}`);
  }
}
