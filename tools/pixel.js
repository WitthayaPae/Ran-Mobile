//  Average colour of a rectangle in a PNG, so a script can tell which screen
//  the client is on instead of sleeping and hoping.
//
//  usage: node pixel.js <file.png> <x> <y> <w> <h>
//  prints: "R G B" (0-255, rounded)
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

const [file, xs, ys, ws, hs] = process.argv.slice(2);
const img = readPng(file);
const x0 = Math.max(0, parseInt(xs, 10) || 0), y0 = Math.max(0, parseInt(ys, 10) || 0);
const cw = Math.min(parseInt(ws, 10) || 1, img.w - x0), chh = Math.min(parseInt(hs, 10) || 1, img.h - y0);
let r = 0, g = 0, b = 0, n = 0;
for (let y = y0; y < y0 + chh; ++y)
  for (let x = x0; x < x0 + cw; ++x) {
    const i = (y * img.w + x) * img.ch;
    r += img.pixels[i]; g += img.pixels[i + 1]; b += img.pixels[i + 2]; ++n;
  }
console.log(Math.round(r / n) + ' ' + Math.round(g / n) + ' ' + Math.round(b / n));
