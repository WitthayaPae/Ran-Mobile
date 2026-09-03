//  Minimal PNG read/resize helpers, shared by the icon and splash builders.
//  Non-interlaced, 8-bit, colour type 2 (RGB) or 6 (RGBA) - which is everything
//  this project produces.
const fs = require('fs'), zlib = require('zlib'), path = require('path');
const png = require(path.join(__dirname, 'png.js'));

function decode(file) {
  const buf = fs.readFileSync(file);
  let p = 8, w, h, ct, bd, il, idat = [];
  while (p < buf.length) {
    const len = buf.readUInt32BE(p), ty = buf.toString('ascii', p + 4, p + 8);
    if (ty === 'IHDR') {
      w = buf.readUInt32BE(p + 8); h = buf.readUInt32BE(p + 12);
      bd = buf[p + 16]; ct = buf[p + 17]; il = buf[p + 20];
    }
    if (ty === 'IDAT') idat.push(buf.slice(p + 8, p + 8 + len));
    p += 12 + len;
  }
  if (bd !== 8 || il !== 0 || (ct !== 2 && ct !== 6))
    throw new Error(file + ': need 8-bit non-interlaced RGB/RGBA, got bd=' + bd + ' ct=' + ct + ' il=' + il);
  const bpp = ct === 6 ? 4 : 3, stride = w * bpp + 1;
  const raw = zlib.inflateSync(Buffer.concat(idat));
  const px = Buffer.alloc(w * h * 4);
  let prev = Buffer.alloc(w * bpp);
  for (let y = 0; y < h; y++) {
    const f = raw[y * stride], line = raw.slice(y * stride + 1, y * stride + 1 + w * bpp);
    const cur = Buffer.alloc(w * bpp);
    for (let i = 0; i < w * bpp; i++) {
      const a = i >= bpp ? cur[i - bpp] : 0, b = prev[i], c = i >= bpp ? prev[i - bpp] : 0;
      let v = line[i];
      if (f === 1) v += a; else if (f === 2) v += b; else if (f === 3) v += (a + b) >> 1;
      else if (f === 4) {
        const pp = a + b - c, pa = Math.abs(pp - a), pb = Math.abs(pp - b), pc = Math.abs(pp - c);
        v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
      }
      cur[i] = v & 255;
    }
    for (let x = 0; x < w; x++) {
      const s = x * bpp, d = (y * w + x) * 4;
      px[d] = cur[s]; px[d + 1] = cur[s + 1]; px[d + 2] = cur[s + 2];
      px[d + 3] = bpp === 4 ? cur[s + 3] : 255;
    }
    prev = cur;
  }
  return { w: w, h: h, px: px };
}

function write(file, img) { fs.writeFileSync(file, png.encode(img.w, img.h, img.px)); }

//  Box-filter downscale with premultiplied alpha.
//
//  Averaging straight RGBA drags the colour of fully transparent pixels into
//  the edge - a logo keyed off black then gets a dark halo everywhere it meets
//  the transparency. Premultiplying first weights each sample by its own
//  coverage, which is what makes the edge stay gold.
function resize(img, W, H) {
  const out = Buffer.alloc(W * H * 4);
  const sx = img.w / W, sy = img.h / H;
  for (let y = 0; y < H; y++) {
    const y0 = Math.floor(y * sy), y1 = Math.max(y0 + 1, Math.floor((y + 1) * sy));
    for (let x = 0; x < W; x++) {
      const x0 = Math.floor(x * sx), x1 = Math.max(x0 + 1, Math.floor((x + 1) * sx));
      let r = 0, g = 0, b = 0, a = 0, n = 0;
      for (let yy = y0; yy < y1 && yy < img.h; yy++)
        for (let xx = x0; xx < x1 && xx < img.w; xx++) {
          const s = (yy * img.w + xx) * 4, al = img.px[s + 3] / 255;
          r += img.px[s] * al; g += img.px[s + 1] * al; b += img.px[s + 2] * al;
          a += img.px[s + 3]; n++;
        }
      const d = (y * W + x) * 4;
      if (!n) { out[d] = out[d+1] = out[d+2] = out[d+3] = 0; continue; }
      const am = a / n;
      const cov = am / 255;
      out[d]     = cov > 0 ? Math.min(255, Math.round(r / n / cov)) : 0;
      out[d + 1] = cov > 0 ? Math.min(255, Math.round(g / n / cov)) : 0;
      out[d + 2] = cov > 0 ? Math.min(255, Math.round(b / n / cov)) : 0;
      out[d + 3] = Math.round(am);
    }
  }
  return { w: W, h: H, px: out };
}

module.exports = { decode, write, resize };
