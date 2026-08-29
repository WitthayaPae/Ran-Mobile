//  Where did two screenshots differ?
//
//  A tap that does nothing and a tap that works can look identical in a crop of
//  the wrong corner. This reports the bounding box of the changed pixels and how
//  many there are, so "did the client react at all" is a measurement rather than
//  a squint.
//
//  usage: node pngdiff.js <a.png> <b.png> [threshold]
const fs = require('fs');
const path = require('path');
const src = fs.readFileSync(path.join(__dirname, 'pngcrop.js'), 'utf8');
const readPng = new Function('fs', 'zlib', src.slice(src.indexOf('function readPng'),
                                      src.indexOf('function writePng')) +
                             '; return readPng;')(fs, require('zlib'));

const [, , fa, fb, thrArg] = process.argv;
const thr = thrArg ? parseInt(thrArg, 10) : 24;
const A = readPng(fa), B = readPng(fb);
if (A.w !== B.w || A.h !== B.h) { console.error('size mismatch'); process.exit(1); }

let n = 0, x0 = 1e9, y0 = 1e9, x1 = -1, y1 = -1;
//  Coarse grid of changed-pixel counts, so a diff spread over the HUD reads
//  differently from one concentrated on the character.
const GX = 8, GY = 8, grid = new Array(GX * GY).fill(0);

for (let y = 0; y < A.h; ++y) {
  for (let x = 0; x < A.w; ++x) {
    const i = (y * A.w + x) * A.channels;
    const d = Math.abs(A.pixels[i] - B.pixels[i]) +
              Math.abs(A.pixels[i + 1] - B.pixels[i + 1]) +
              Math.abs(A.pixels[i + 2] - B.pixels[i + 2]);
    if (d <= thr) continue;
    ++n;
    if (x < x0) x0 = x; if (x > x1) x1 = x;
    if (y < y0) y0 = y; if (y > y1) y1 = y;
    grid[((y * GY / A.h) | 0) * GX + ((x * GX / A.w) | 0)]++;
  }
}

const total = A.w * A.h;
console.log('changed ' + n + ' px (' + (100 * n / total).toFixed(2) + '%)');
if (!n) process.exit(0);
console.log('bbox ' + x0 + ',' + y0 + ' ' + (x1 - x0 + 1) + 'x' + (y1 - y0 + 1));
console.log('grid (% of each cell):');
for (let gy = 0; gy < GY; ++gy) {
  let row = '  ';
  for (let gx = 0; gx < GX; ++gx) {
    const cell = grid[gy * GX + gx] * GX * GY / total * 100;
    row += (cell < 0.05 ? '   .' : cell.toFixed(0).padStart(4));
  }
  console.log(row);
}
