'use strict';
//
//  Pull the loading-screen art out of the client and write it into the APK's
//  resources, as PNG.
//
//      node extract-launcher-art.js
//
//  Why it has to be baked into the APK rather than read from the data root:
//  the launcher draws this screen *while provisioning that root*. On a fresh
//  install there is no textures/gui/ yet - that is the 2.8 GB the patch is in
//  the middle of fetching - so anything the launcher paints has to already be
//  inside the APK.
//
//  Source pieces, the same ones splash.cpp composites for the in-game loading
//  screen, so the two look like one screen rather than two:
//
//  The pieces, and where the numbers come from - LoadingThread.cpp lays the
//  in-game loading screen out in a 1024x768 virtual space:
//
//      ld_top.dds    @ 0,0   1024x140   drawn 1024x128 at (0,0)
//      the art                          drawn 1024x512 at (0,128)
//      ld_under.dds  @ 0,7   1024x140   drawn 1024x128 at (0,640)
//
//  so each band is 128/768 of the height. The patch page is built to the same
//  proportions, which is the point: it is the same screen the player sees a
//  moment later when the client loads a map.
//
//  ran_mark.png is NOT written here any more - make-icons.js owns it, because
//  the mark is now the Ran Legacy logo rather than a crop of a client sheet.
//  Leaving the old job in would silently put the RAN ONLINE wordmark back the
//  next time anyone ran this.
//
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');
const dds = require('./dds.js');

function findRoot(dir) {
  for (let i = 0; i < 8; i++) {
    const has = n => fs.existsSync(path.join(dir, n));
    if (has('MOBILE') && (has('CLIENT') || has('Ran'))) return dir;
    const up = path.dirname(dir);
    if (up === dir) break;
    dir = up;
  }
  throw new Error('cannot find the development root');
}
const ROOT = findRoot(__dirname);
const GUI  = path.join(ROOT, 'CLIENT/textures/gui');
const ART  = path.join(ROOT, 'MOBILE/art');
const u    = require('./imgutil.js');
const RES  = path.join(ROOT, 'MOBILE/native/android/res/drawable-nodpi');

/* ------------------------------------------------------------------ png out */
function crc32(buf) {
  let c, table = crc32.t;
  if (!table) {
    table = crc32.t = new Int32Array(256);
    for (let n = 0; n < 256; n++) {
      c = n;
      for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
      table[n] = c;
    }
  }
  let crc = -1;
  for (let i = 0; i < buf.length; i++) crc = (crc >>> 8) ^ table[(crc ^ buf[i]) & 0xFF];
  return (crc ^ -1) >>> 0;
}
function chunk(type, data) {
  const len = Buffer.alloc(4); len.writeUInt32BE(data.length, 0);
  const td = Buffer.concat([Buffer.from(type, 'latin1'), data]);
  const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td), 0);
  return Buffer.concat([len, td, crc]);
}
function encodePNG(w, h, rgba) {
  const stride = w * 4;
  const raw = Buffer.alloc(h * (stride + 1));
  for (let y = 0; y < h; y++) {
    raw[y * (stride + 1)] = 0;
    rgba.copy(raw, y * (stride + 1) + 1, y * stride, (y + 1) * stride);
  }
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4);
  ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]),
    chunk('IHDR', ihdr),
    chunk('IDAT', zlib.deflateSync(raw, { level: 9 })),
    chunk('IEND', Buffer.alloc(0)),
  ]);
}

/* ------------------------------------------------------------------- source */
function loadDds(name, dir) {
  const p = path.join(dir || GUI, name);
  if (!fs.existsSync(p)) throw new Error('missing ' + p);
  const img = dds.decode(fs.readFileSync(p));
  if (!img || !img.data) throw new Error('cannot decode ' + name);
  return img;                                  // { width, height, data (RGBA) }
}

function crop(img, x0, y0, w, h) {
  const out = Buffer.alloc(w * h * 4);
  for (let y = 0; y < h; y++) {
    const sy = Math.min(img.height - 1, y0 + y);
    for (let x = 0; x < w; x++) {
      const sx = Math.min(img.width - 1, x0 + x);
      img.data.copy(out, (y * w + x) * 4, (sy * img.width + sx) * 4,
                    (sy * img.width + sx) * 4 + 4);
    }
  }
  return out;
}

/* ---------------------------------------------------------------------- run */
fs.mkdirSync(RES, { recursive: true });

const jobs = [
  //  The page art. 4096x2828 is far more than the panel can show and would put
  //  11 MB of PNG in the APK, so it is taken down to something the largest
  //  panel still cannot out-resolve.
  { out: 'ran_loading.png', src: 'ran_old_film.dds', dir: ART, max: 1600 },
  //  The two bands, cropped to the rect LoadingThread actually samples.
  { out: 'ld_top.png',      src: 'ld_top.dds',   x: 0, y: 0, w: 1024, h: 140 },
  { out: 'ld_under.png',    src: 'ld_under.dds', x: 0, y: 7, w: 1024, h: 140 },
];

for (const j of jobs) {
  const img = loadDds(j.src, j.dir);
  let w = j.w || img.width, h = j.h || img.height;
  let rgba = (j.w ? crop(img, j.x, j.y, j.w, j.h) : img.data);
  if (j.max && w > j.max) {
    const sc = j.max / w, nw = j.max, nh = Math.round(h * sc);
    rgba = u.resize({ w: w, h: h, px: rgba }, nw, nh).px;
    w = nw; h = nh;
  }
  const png = encodePNG(w, h, rgba);
  fs.writeFileSync(path.join(RES, j.out), png);
  console.log('  ' + j.out.padEnd(18) + w + 'x' + h + '  ' +
              (png.length / 1024).toFixed(0) + ' KB   <- ' + j.src +
              (j.w ? '  @' + j.x + ',' + j.y : ''));
}
console.log('written to ' + RES);
