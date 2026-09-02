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
//      loading_002.dds                     the lobby art
//      outgui_character.dds @ 335,416      LOGIN_MARK, the RAN mark on the
//                                          login page (from the ui config, not
//                                          eyeballed)
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
function loadDds(name) {
  const p = path.join(GUI, name);
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
  { out: 'ran_loading.png', src: 'loading_002.dds' },
  { out: 'ran_mark.png',    src: 'outgui_character.dds', x: 335, y: 416, w: 177, h: 96 },
];

for (const j of jobs) {
  const img = loadDds(j.src);
  const w = j.w || img.width, h = j.h || img.height;
  const rgba = (j.w ? crop(img, j.x, j.y, j.w, j.h) : img.data);
  const png = encodePNG(w, h, rgba);
  fs.writeFileSync(path.join(RES, j.out), png);
  console.log('  ' + j.out.padEnd(18) + w + 'x' + h + '  ' +
              (png.length / 1024).toFixed(0) + ' KB   <- ' + j.src +
              (j.w ? '  @' + j.x + ',' + j.y : ''));
}
console.log('written to ' + RES);
