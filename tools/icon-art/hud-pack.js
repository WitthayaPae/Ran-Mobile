'use strict';
//  Build mobile_hud.dds - the painted art for the on-screen controls.
//
//  Separate sheet from mobile_icons.dds because these are drawn by the overlay
//  rather than by the interface: the overlay is handed one texture and samples
//  a cell out of it, so everything it needs has to live together and at one
//  cell size. 256 a cell, four across, sixteen controls.
const fs = require('fs'), path = require('path');
const { decode } = require('./png2.js');
const { resize } = require('./resize.js');
const png = require(path.join(__dirname, '..', 'rcc-extract', 'png.js'));
const SRC = process.argv[2] || 'C:/Users/tapnu/Downloads/RanIcon/';

//  Cell order. The overlay indexes this by name, so it must not be shuffled
//  without changing kCell* in touch_ui.cpp to match.
const CELLS = [
  'atk.png',      'atk_ring.png',  'skillframe.png', 'auto.png',
  'auto_on.png',  'pk.png',        'pk_on.png',      'camlock.png',
  'camlock_on.png','pickup.png',   'vehicle.png',    'menu.png',
  'page_up.png',  'page_down.png', 'stick_base.png', 'stick_knob.png',
];

const N = 256, COLS = 4, AW = 1024, AH = 1024;

//  A round mask taken from the controls that came back with alpha.
//
//  One of these arrived as flat RGB on a light background, and a flood fill
//  handles that badly - the background is a gradient, and a tolerance loose
//  enough to walk it eats into the art. Every control is the same disc filling
//  the same canvas, so the silhouette is already known from its neighbours.
let DISC = null;
function discMask(size) {
  if (DISC) return DISC;
  const donors = ['pickup.png', 'menu.png', 'auto.png', 'page_up.png', 'vehicle.png'];
  const got = [];
  for (const d of donors) {
    if (!fs.existsSync(SRC + d)) continue;
    const im = decode(SRC + d);
    if (!im.hasAlpha) continue;
    got.push(resize(im, size, size));
  }
  const out = Buffer.alloc(size * size);
  for (let k = 0; k < size * size; k++) {
    const v = got.map(g => g[k * 4 + 3]).sort((a, b) => a - b);
    out[k] = v.length ? v[v.length >> 1] : 255;
  }
  DISC = out;
  console.log('   disc mask built from ' + got.length + ' controls');
  return DISC;
}

//  Carry the edge colour outward, so scaling cannot mix the background in.
function bleed(px, size, passes) {
  for (let p = 0; p < passes; p++) {
    const src = Buffer.from(px);
    for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) {
      const k = (y * size + x) * 4;
      if (src[k + 3] > 0) continue;
      let r = 0, g = 0, b = 0, n = 0;
      for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) {
        const nx = x + dx, ny = y + dy;
        if (nx < 0 || ny < 0 || nx >= size || ny >= size) continue;
        const m = (ny * size + nx) * 4;
        if (src[m + 3] === 0) continue;
        r += src[m]; g += src[m + 1]; b += src[m + 2]; n++;
      }
      if (!n) continue;
      px[k] = Math.round(r / n); px[k + 1] = Math.round(g / n); px[k + 2] = Math.round(b / n);
    }
  }
}

const atlas = Buffer.alloc(AW * AH * 4);
let filled = 0;

CELLS.forEach((name, n) => {
  const file = SRC + name;
  if (!fs.existsSync(file)) { console.log(String(n).padStart(2) + '  MISSING ' + name); return; }
  const img = decode(file);
  const px = resize(img, N, N);
  let masked = '';
  if (!img.hasAlpha) {
    const m = discMask(N);
    for (let k = 0; k < N * N; k++) px[k * 4 + 3] = m[k];
    masked = '  [disc mask applied]';
  }
  bleed(px, N, 5);
  const ox = (n % COLS) * N, oy = Math.floor(n / COLS) * N;
  for (let y = 0; y < N; y++)
    px.copy(atlas, ((oy + y) * AW + ox) * 4, y * N * 4, (y + 1) * N * 4);
  filled++;
  console.log(String(n).padStart(2) + '  ' + name.padEnd(17) + ' -> ' + ox + ',' + oy + masked);
});

fs.writeFileSync(path.join(__dirname, 'hud.png'), png.encode(AW, AH, atlas));
console.log('\n' + filled + ' of ' + CELLS.length + ' cells -> hud.png  (' + AW + 'x' + AH + ')');
