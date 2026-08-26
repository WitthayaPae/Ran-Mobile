//  Crop and magnify a PNG, with no dependencies beyond node's own zlib.
//
//  Device screenshots are 2560x1440 and the thing worth looking at is often a
//  120-pixel character, which survives neither the terminal nor an image
//  downscale. This cuts a region out and nearest-neighbour zooms it so the
//  pose, the seams and the texture are actually visible.
//
//  usage: node pngcrop.js <in.png> <out.png> <x> <y> <w> <h> [zoom]
const fs = require('fs');
const zlib = require('zlib');

function readPng(file) {
  const buf = fs.readFileSync(file);
  if (buf.readUInt32BE(0) !== 0x89504e47) throw new Error('not a png: ' + file);
  let off = 8, w = 0, h = 0, bitDepth = 0, colorType = 0;
  const idat = [];
  while (off < buf.length) {
    const len = buf.readUInt32BE(off);
    const type = buf.toString('ascii', off + 4, off + 8);
    const data = buf.slice(off + 8, off + 8 + len);
    if (type === 'IHDR') {
      w = data.readUInt32BE(0); h = data.readUInt32BE(4);
      bitDepth = data[8]; colorType = data[9];
      if (bitDepth !== 8) throw new Error('only 8-bit channels supported');
      if (data[12] !== 0) throw new Error('interlaced png not supported');
    } else if (type === 'IDAT') {
      idat.push(data);
    } else if (type === 'IEND') break;
    off += 12 + len;
  }
  const channels = { 0: 1, 2: 3, 4: 2, 6: 4 }[colorType];
  if (!channels) throw new Error('unsupported colour type ' + colorType);

  const raw = zlib.inflateSync(Buffer.concat(idat));
  const stride = w * channels;
  const out = Buffer.alloc(h * stride);
  let p = 0;
  for (let y = 0; y < h; ++y) {
    const filter = raw[p++];
    const line = raw.slice(p, p + stride); p += stride;
    const cur = out.slice(y * stride, (y + 1) * stride);
    const prev = y > 0 ? out.slice((y - 1) * stride, y * stride) : null;
    for (let x = 0; x < stride; ++x) {
      const a = x >= channels ? cur[x - channels] : 0;
      const b = prev ? prev[x] : 0;
      const c = (prev && x >= channels) ? prev[x - channels] : 0;
      let v = line[x];
      switch (filter) {
        case 0: break;
        case 1: v += a; break;
        case 2: v += b; break;
        case 3: v += (a + b) >> 1; break;
        case 4: {
          const pp = a + b - c, pa = Math.abs(pp - a), pb = Math.abs(pp - b), pc = Math.abs(pp - c);
          v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
          break;
        }
        default: throw new Error('bad filter ' + filter);
      }
      cur[x] = v & 0xff;
    }
  }
  return { w, h, channels, pixels: out };
}

function writePng(file, w, h, channels, pixels) {
  const stride = w * channels;
  const raw = Buffer.alloc(h * (stride + 1));
  for (let y = 0; y < h; ++y) {
    raw[y * (stride + 1)] = 0;                       // filter: none
    pixels.copy(raw, y * (stride + 1) + 1, y * stride, (y + 1) * stride);
  }
  const chunks = [Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a])];
  const chunk = (type, data) => {
    const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
    const body = Buffer.concat([Buffer.from(type, 'ascii'), data]);
    const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(body) >>> 0);
    return Buffer.concat([len, body, crc]);
  };
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4);
  ihdr[8] = 8; ihdr[9] = { 1: 0, 2: 4, 3: 2, 4: 6 }[channels]; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
  chunks.push(chunk('IHDR', ihdr));
  chunks.push(chunk('IDAT', zlib.deflateSync(raw)));
  chunks.push(chunk('IEND', Buffer.alloc(0)));
  fs.writeFileSync(file, Buffer.concat(chunks));
}

let crcTable = null;
function crc32(buf) {
  if (!crcTable) {
    crcTable = new Int32Array(256);
    for (let n = 0; n < 256; ++n) {
      let c = n;
      for (let k = 0; k < 8; ++k) c = (c & 1) ? (0xedb88320 ^ (c >>> 1)) : (c >>> 1);
      crcTable[n] = c;
    }
  }
  let c = 0xffffffff;
  for (let i = 0; i < buf.length; ++i) c = crcTable[(c ^ buf[i]) & 0xff] ^ (c >>> 8);
  return c ^ 0xffffffff;
}

const [inFile, outFile, xs, ys, ws, hs, zs] = process.argv.slice(2);
if (!inFile || !outFile) {
  console.log('usage: node pngcrop.js <in.png> <out.png> <x> <y> <w> <h> [zoom]');
  process.exit(1);
}
const src = readPng(inFile);
const x0 = Math.max(0, parseInt(xs, 10) || 0);
const y0 = Math.max(0, parseInt(ys, 10) || 0);
const cw = Math.min(parseInt(ws, 10) || src.w, src.w - x0);
const ch = Math.min(parseInt(hs, 10) || src.h, src.h - y0);
const zoom = Math.max(1, parseInt(zs, 10) || 1);

const ch3 = src.channels;
const dst = Buffer.alloc(cw * zoom * ch * zoom * ch3);
const dstStride = cw * zoom * ch3;
for (let y = 0; y < ch * zoom; ++y) {
  const sy = y0 + Math.floor(y / zoom);
  for (let x = 0; x < cw * zoom; ++x) {
    const sx = x0 + Math.floor(x / zoom);
    const s = (sy * src.w + sx) * ch3, d = y * dstStride + x * ch3;
    for (let c = 0; c < ch3; ++c) dst[d + c] = src.pixels[s + c];
  }
}
writePng(outFile, cw * zoom, ch * zoom, ch3, dst);
console.log(outFile + '  ' + (cw * zoom) + 'x' + (ch * zoom) + '  (from ' + src.w + 'x' + src.h + ')');
