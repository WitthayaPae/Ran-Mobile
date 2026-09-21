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
//
//  Five across since the page arrows became four F-key buttons with a lit
//  state each: that is 22 controls, and sixteen cells no longer hold them.
//  The sheet stays square because the overlay derives a cell's height from its
//  width.
const CELLS = [
  'atk.png',      'atk_ring.png',  'skillframe.png', 'auto.png',    'auto_on.png',
  'pk.png',       'pk_on.png',     'camlock.png',    'camlock_on.png','pickup.png',
  'vehicle.png',  'menu.png',      'f1.png',         'f2.png',      'f3.png',
  'f4.png',       'f1_on.png',     'f2_on.png',      'f3_on.png',   'f4_on.png',
  'stick_base.png','stick_knob.png', '@steel:stick_base.png',
];

const N = 256, COLS = 5, AW = 1280, AH = 1280;

//  The silhouette of a control that arrived as flat RGB, cut from its own
//  background rather than borrowed.
//
//  It is tried BEFORE the donor mask below, because the donor mask is the
//  shape of the round BUTTONS - a full disc with four studs - and not every
//  control is that shape. stick_knob.png is a sphere on flat white inside that
//  canvas, so the donor silhouette kept a ring of the white background around
//  the sphere: the white outline that showed round the joystick knob on the
//  device.
//
//  Only taken when it is believable: the fill has to start from every corner,
//  agree on one background colour, and end up covering between a tenth and
//  four fifths of the canvas. Anything else falls through to the donor mask,
//  which is what the gradient-background case needs.
function bgMask(px, size) {
  const at = (x, y) => (y * size + x) * 4;

  //  The ground is whatever most of the border is. A corner is not enough -
  //  the knob's canvas has one corner sitting at 211 where the sphere's shadow
  //  reaches it, and testing corners for agreement rejected the whole image.
  const bx = [], by = [], bz = [];
  for (let x = 0; x < size; x++) {
    for (const y of [0, size - 1]) { const k = at(x, y); bx.push(px[k]); by.push(px[k+1]); bz.push(px[k+2]); }
  }
  const med = a => { a.sort((p, q) => p - q); return a[a.length >> 1]; };
  const c0 = [med(bx), med(by), med(bz)];

  //  Generous, because what it must not swallow is the ART, and the art here
  //  is a long way from the ground: a dark sphere on white. A specular
  //  highlight inside the sphere is near-white but the fill cannot reach it -
  //  it only walks in from the border.
  const TOL = 150;
  const seen = Buffer.alloc(size * size);
  const stack = [];
  for (let x = 0; x < size; x++) { stack.push(x, 0); stack.push(x, size - 1); }
  for (let y = 0; y < size; y++) { stack.push(0, y); stack.push(size - 1, y); }
  let n = 0;
  while (stack.length) {
    const y = stack.pop(), x = stack.pop();
    if (x < 0 || y < 0 || x >= size || y >= size) continue;
    const i = y * size + x;
    if (seen[i]) continue;
    const k = i * 4;
    if (Math.abs(px[k]-c0[0]) + Math.abs(px[k+1]-c0[1]) + Math.abs(px[k+2]-c0[2]) > TOL) continue;
    seen[i] = 1; n++;
    stack.push(x+1, y); stack.push(x-1, y); stack.push(x, y+1); stack.push(x, y-1);
  }
  const frac = n / (size * size);
  if (frac < 0.10 || frac > 0.80) return null;

  //  Opaque where the fill did not reach, and a one-pixel ramp at the border
  //  so the edge is not a staircase.
  const out = Buffer.alloc(size * size);
  for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) {
    const i = y * size + x;
    if (seen[i]) { out[i] = 0; continue; }
    let edge = false;
    for (let dy = -1; dy <= 1 && !edge; dy++) for (let dx = -1; dx <= 1; dx++) {
      const nx = x + dx, ny = y + dy;
      if (nx < 0 || ny < 0 || nx >= size || ny >= size) continue;
      if (seen[ny * size + nx]) { edge = true; break; }
    }
    out[i] = edge ? 140 : 255;
  }
  return out;
}

//  A round mask taken from the controls that came back with alpha.
//
//  The fallback, for a control whose background bgMask cannot read: a gradient
//  ground defeats a flood fill, and a tolerance loose enough to walk it eats
//  into the art. The round buttons are all the same disc filling the same
//  canvas, so that silhouette is known from its neighbours.
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

//  A cell asked for as '@steel:<file>' is that file with the gold taken out.
//
//  The joystick's seat is the one bezel in the sheet, and the skill slots and
//  the potion row borrow it. Desaturating the shared cell would have taken the
//  gold off those too, so the stick gets a copy of its own and they keep the
//  art as painted.
function steelise(px, size) {
  for (let k = 0; k < size * size; k++) {
    const i = k * 4;
    //  Rec.601 luma, then a cool cast, so it reads as steel rather than as a
    //  grey photograph of something gold.
    const y = 0.299 * px[i] + 0.587 * px[i + 1] + 0.114 * px[i + 2];
    px[i]     = Math.min(255, Math.round(y * 0.95));
    px[i + 1] = Math.min(255, Math.round(y * 0.98));
    px[i + 2] = Math.min(255, Math.round(y * 1.05));
  }
}

CELLS.forEach((name, n) => {
  const steel = name.startsWith('@steel:');
  if (steel) name = name.slice(7);
  const file = SRC + name;
  if (!fs.existsSync(file)) { console.log(String(n).padStart(2) + '  MISSING ' + name); return; }
  const img = decode(file);
  const px = resize(img, N, N);
  let masked = '';
  if (!img.hasAlpha) {
    const own = bgMask(px, N);
    const m = own || discMask(N);
    for (let k = 0; k < N * N; k++) px[k * 4 + 3] = m[k];
    masked = own ? '  [own background cut]' : '  [disc mask applied]';
  }
  if (steel) masked += '  [gold removed]';
  if (steel) steelise(px, N);
  bleed(px, N, 5);
  const ox = (n % COLS) * N, oy = Math.floor(n / COLS) * N;
  for (let y = 0; y < N; y++)
    px.copy(atlas, ((oy + y) * AW + ox) * 4, y * N * 4, (y + 1) * N * 4);
  filled++;
  console.log(String(n).padStart(2) + '  ' + name.padEnd(17) + ' -> ' + ox + ',' + oy + masked);
});

fs.writeFileSync(path.join(__dirname, 'hud.png'), png.encode(AW, AH, atlas));
console.log('\n' + filled + ' of ' + CELLS.length + ' cells -> hud.png  (' + AW + 'x' + AH + ')');
