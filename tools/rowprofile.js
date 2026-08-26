//  Per-row ink profile of a region, so "is this text centred" is a number.
//
//  Eyeballing a zoomed crop is how you convince yourself of the wrong answer.
//  This prints, for every row of a region, how many pixels are brighter than a
//  threshold - the button face is dark and its label is light, so the run of
//  rows with ink is the label, and the gap above and below it is the answer.
//
//  usage: node rowprofile.js <file.png> <x> <y> <w> <h> [threshold]
const fs = require('fs');
const zlib = require('zlib');

function readPng(file) {
  const buf = fs.readFileSync(file);
  let off = 8, w = 0, h = 0, colorType = 0;
  const idat = [];
  while (off < buf.length) {
    const len = buf.readUInt32BE(off);
    const type = buf.toString('ascii', off + 4, off + 8);
    const data = buf.slice(off + 8, off + 8 + len);
    if (type === 'IHDR') { w = data.readUInt32BE(0); h = data.readUInt32BE(4); colorType = data[9]; }
    else if (type === 'IDAT') idat.push(data);
    else if (type === 'IEND') break;
    off += 12 + len;
  }
  const ch = { 0: 1, 2: 3, 4: 2, 6: 4 }[colorType];
  const raw = zlib.inflateSync(Buffer.concat(idat));
  const stride = w * ch;
  const out = Buffer.alloc(h * stride);
  let p = 0;
  for (let y = 0; y < h; ++y) {
    const filter = raw[p++];
    const line = raw.slice(p, p + stride); p += stride;
    const cur = out.slice(y * stride, (y + 1) * stride);
    const prev = y > 0 ? out.slice((y - 1) * stride, y * stride) : null;
    for (let x = 0; x < stride; ++x) {
      const a = x >= ch ? cur[x - ch] : 0;
      const b = prev ? prev[x] : 0;
      const c = (prev && x >= ch) ? prev[x - ch] : 0;
      let v = line[x];
      if (filter === 1) v += a;
      else if (filter === 2) v += b;
      else if (filter === 3) v += (a + b) >> 1;
      else if (filter === 4) {
        const pp = a + b - c, pa = Math.abs(pp - a), pb = Math.abs(pp - b), pc = Math.abs(pp - c);
        v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
      }
      cur[x] = v & 0xff;
    }
  }
  return { w, h, ch, pixels: out };
}

const [file, xs, ys, ws, hs, ts] = process.argv.slice(2);
const img = readPng(file);
const x0 = parseInt(xs, 10), y0 = parseInt(ys, 10);
const cw = parseInt(ws, 10), chh = parseInt(hs, 10);
const thr = ts ? parseInt(ts, 10) : 150;

const rows = [];
for (let y = y0; y < y0 + chh && y < img.h; ++y) {
  let n = 0, sum = 0;
  for (let x = x0; x < x0 + cw && x < img.w; ++x) {
    const i = (y * img.w + x) * img.ch;
    const lum = (img.pixels[i] * 30 + img.pixels[i + 1] * 59 + img.pixels[i + 2] * 11) / 100;
    sum += lum;
    if (lum > thr) ++n;
  }
  rows.push({ y, n, avg: Math.round(sum / cw) });
}

for (const r of rows)
  console.log(String(r.y).padStart(5) + '  avg ' + String(r.avg).padStart(3) + '  ink ' +
              String(r.n).padStart(4) + ' ' + '#'.repeat(Math.min(60, r.n)));

const ink = rows.filter(r => r.n > 2);
if (ink.length) {
  const top = ink[0].y, bot = ink[ink.length - 1].y;
  console.log('\nink rows ' + top + '..' + bot + '  (height ' + (bot - top + 1) + ')');
  console.log('gap above ' + (top - y0) + ', gap below ' + (y0 + chh - 1 - bot));
}
