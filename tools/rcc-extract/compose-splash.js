/*  Builds the window-background PNG the two activities share.
 *
 *  android:windowBackground is drawn before either activity's own views are,
 *  and a BitmapDrawable there can only be stretched to fill - there is no
 *  CENTER_CROP for a drawable. The old splash.png was the bare 2:1 art, so the
 *  window background stretched it to the screen and left the RAN mark out
 *  entirely, while RanLauncher's page and the native splash both draw it
 *  cover-fit *with* the mark. The player saw the page, then the same photo at a
 *  different crop with the mark gone, then the page again: the patch screen
 *  looked like it disappeared and a separate loading screen took over.
 *
 *  Composing the cover-fit result plus the mark at 16:9 makes the stretch a
 *  no-op on a 16:9 panel and a couple of percent on a 16:10 one, so all three
 *  frames are the same picture.
 */
const fs = require('fs'), zlib = require('zlib'), path = require('path');
const png = require(path.join(__dirname, 'png.js'));

function decode(file) {
  const buf = fs.readFileSync(file);
  let p = 8, w, h, ct, idat = [];
  while (p < buf.length) {
    const len = buf.readUInt32BE(p), ty = buf.toString('ascii', p + 4, p + 8);
    if (ty === 'IHDR') { w = buf.readUInt32BE(p + 8); h = buf.readUInt32BE(p + 12); ct = buf[p + 17]; }
    if (ty === 'IDAT') idat.push(buf.slice(p + 8, p + 8 + len));
    p += 12 + len;
  }
  if (ct !== 2 && ct !== 6) throw new Error(file + ': colour type ' + ct + ' not handled');
  const bpp = ct === 6 ? 4 : 3, stride = w * bpp + 1;
  const raw = zlib.inflateSync(Buffer.concat(idat));
  const out = Buffer.alloc(w * h * 4);
  let prev = Buffer.alloc(w * bpp);
  for (let y = 0; y < h; y++) {
    const f = raw[y * stride], line = raw.slice(y * stride + 1, y * stride + 1 + w * bpp);
    const cur = Buffer.alloc(w * bpp);
    for (let i = 0; i < w * bpp; i++) {
      const a = i >= bpp ? cur[i - bpp] : 0, b = prev[i], c = i >= bpp ? prev[i - bpp] : 0;
      let v = line[i];
      if (f === 1) v += a; else if (f === 2) v += b; else if (f === 3) v += (a + b) >> 1;
      else if (f === 4) { const pp = a + b - c, pa = Math.abs(pp - a), pb = Math.abs(pp - b), pc = Math.abs(pp - c);
                          v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c); }
      cur[i] = v & 255;
    }
    for (let x = 0; x < w; x++) {
      const s = x * bpp, d = (y * w + x) * 4;
      out[d] = cur[s]; out[d + 1] = cur[s + 1]; out[d + 2] = cur[s + 2];
      out[d + 3] = bpp === 4 ? cur[s + 3] : 255;
    }
    prev = cur;
  }
  return { w: w, h: h, px: out };
}

//  Bilinear, because the art is upscaled ~2.8x and a nearest-neighbour window
//  background next to a filtered ImageView of the same art is a visible seam.
function sample(img, fx, fy, o) {
  const x0 = Math.max(0, Math.min(img.w - 1, Math.floor(fx))), y0 = Math.max(0, Math.min(img.h - 1, Math.floor(fy)));
  const x1 = Math.min(img.w - 1, x0 + 1), y1 = Math.min(img.h - 1, y0 + 1);
  const tx = fx - x0, ty = fy - y0;
  for (let c = 0; c < 4; c++) {
    const a = img.px[(y0 * img.w + x0) * 4 + c], b = img.px[(y0 * img.w + x1) * 4 + c];
    const d = img.px[(y1 * img.w + x0) * 4 + c], e = img.px[(y1 * img.w + x1) * 4 + c];
    o[c] = Math.round((a * (1 - tx) + b * tx) * (1 - ty) + (d * (1 - tx) + e * tx) * ty);
  }
}

const dir  = path.join(__dirname, '..', '..', 'native', 'android', 'res', 'drawable-nodpi');
const art  = decode(path.join(dir, 'ran_loading.png'));
const mark = decode(path.join(dir, 'ran_mark.png'));

//  Both activities upscale the same 1024x512 source, so a 720p composite is
//  exactly as sharp on screen as a 1440p one and a quarter of the APK weight.
const W = 1280, H = 720;
const out = Buffer.alloc(W * H * 4);

//  CENTER_CROP: scale by whichever axis needs the larger factor, centre the rest.
const s = Math.max(W / art.w, H / art.h);
const dw = art.w * s, dh = art.h * s, ox = (W - dw) / 2, oy = (H - dh) / 2;
const t = [0, 0, 0, 0];
for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
  sample(art, (x - ox) / s, (y - oy) / s, t);
  const d = (y * W + x) * 4;
  out[d] = t[0]; out[d + 1] = t[1]; out[d + 2] = t[2]; out[d + 3] = 255;
}

//  The mark: 230dp wide, 30dp from the top, centred. Both test devices land
//  between 18.0% and 19.1% of the screen width, so take the middle - the window
//  background is on screen for a fraction of a second and a 1% difference in
//  where it sits is not visible, while its absence very much was.
//  Matches the launcher's 170dp against a ~1240dp-wide tablet.
const mw = Math.round(W * 0.137), mh = Math.round(mw * mark.h / mark.w);
const mx = Math.round((W - mw) / 2), my = Math.round(H * 0.041);
for (let y = 0; y < mh; y++) for (let x = 0; x < mw; x++) {
  sample(mark, x * mark.w / mw, y * mark.h / mh, t);
  const a = t[3] / 255;
  if (a <= 0) continue;
  const d = ((my + y) * W + (mx + x)) * 4;
  for (let c = 0; c < 3; c++) out[d + c] = Math.round(out[d + c] * (1 - a) + t[c] * a);
}

const dst = path.join(dir, 'splash.png');
fs.writeFileSync(dst, png.encode(W, H, out));
console.log('wrote ' + dst + '  ' + W + 'x' + H + '  ' + fs.statSync(dst).size + ' bytes');
