'use strict';
//  The ride/dismount button beside the chat.
//
//  The first cut used the vehicle *equip slot* out of GUI_Inven_Slots - correct
//  in meaning, but it is a socket outline, not a picture of anything, and it
//  looked like an empty inventory square sitting on the HUD.
//
//  This composes the game's own motorcycle icon onto a round face in the same
//  idiom as the touch buttons: dark chrome, a lit rim, a brighter rim when
//  pressed. Source is sc_bike_gui.dds, the bike icon sheet - 32x32 cells on a
//  blue plate, the row at y=213. The red sportbike at x=422 has
//  the clearest silhouette at button size.
const fs = require('fs'), path = require('path');
const dds = require(path.join(__dirname, 'dds.js'));
const enc = require(path.join(__dirname, 'dds-encode.js'));
const u   = require(path.join(__dirname, 'imgutil.js'));

const ROOT = path.join(__dirname, '..', '..', '..');
const GUI  = path.join(ROOT, 'CLIENT/textures/gui');

const sheet = dds.decode(fs.readFileSync(path.join(GUI, 'sc_bike_gui.dds')));
const src = { w: sheet.width, h: sheet.height, px: sheet.rgba || sheet.data };

//  The cell, inside its border.
//  Measured, not eyeballed: border columns in the row band sit at 418/421 and
//  453/456, so this cell content is x 422..452; the plate rows are 213..241.
const CX = 422, CY = 213, CW = 31, CH = 29;
const cell = { w: CW, h: CH, px: Buffer.alloc(CW * CH * 4) };
for (let y = 0; y < CH; y++) for (let x = 0; x < CW; x++) {
  const s = ((CY + y) * src.w + (CX + x)) * 4, d = (y * CW + x) * 4;
  cell.px[d] = src.px[s]; cell.px[d+1] = src.px[s+1];
  cell.px[d+2] = src.px[s+2]; cell.px[d+3] = 255;
}

//  Key the blue plate, flooding in from the border so blue *on the bike* stays.
{
  const isPlate = (i) => {
    const r = cell.px[i], g = cell.px[i+1], b = cell.px[i+2];
    return b > 90 && b > r + 35 && b > g + 15;
  };
  const seen = new Uint8Array(CW * CH), stack = [];
  for (let x = 0; x < CW; x++) { stack.push(x, 0); stack.push(x, CH - 1); }
  for (let y = 0; y < CH; y++) { stack.push(0, y); stack.push(CW - 1, y); }
  while (stack.length) {
    const y = stack.pop(), x = stack.pop();
    if (x < 0 || y < 0 || x >= CW || y >= CH) continue;
    const k = y * CW + x;
    if (seen[k] || !isPlate(k * 4)) continue;
    seen[k] = 1; cell.px[k * 4 + 3] = 0;
    stack.push(x+1, y); stack.push(x-1, y); stack.push(x, y+1); stack.push(x, y-1);
  }
}

const N = 64;
const bike = u.resize(cell, 44, 44);

function face(pressed) {
  const px = Buffer.alloc(N * N * 4);
  const c = (N - 1) / 2, rOuter = N / 2 - 0.5, rRim = rOuter - 2.5;
  for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) {
    const dx = x - c, dy = y - c, r = Math.sqrt(dx*dx + dy*dy), d = (y * N + x) * 4;
    if (r > rOuter) { px[d+3] = 0; continue; }
    //  Face: a little lighter at the top, as a dome catches light.
    const t = Math.max(0, Math.min(1, (dy / N) + 0.5));
    let R = Math.round(46 - 22 * t), G = Math.round(51 - 24 * t), B = Math.round(57 - 26 * t);
    if (r > rRim) {
      //  Rim. Amber when pressed, so a press reads at a glance.
      R = pressed ? 216 : 110; G = pressed ? 162 : 122; B = pressed ? 74 : 133;
    }
    px[d] = R; px[d+1] = G; px[d+2] = B;
    //  Feather the last half pixel so the disc has no staircase edge.
    px[d+3] = r > rOuter - 1 ? Math.round(255 * (rOuter - r)) : 255;
  }
  //  The bike, centred.
  const ox = ((N - bike.w) / 2) | 0, oy = ((N - bike.h) / 2) | 0;
  for (let y = 0; y < bike.h; y++) for (let x = 0; x < bike.w; x++) {
    const s = (y * bike.w + x) * 4, a = bike.px[s+3] / 255;
    if (a <= 0) continue;
    const d = ((oy + y) * N + (ox + x)) * 4;
    px[d]   = Math.round(px[d]   * (1 - a) + bike.px[s]   * a);
    px[d+1] = Math.round(px[d+1] * (1 - a) + bike.px[s+1] * a);
    px[d+2] = Math.round(px[d+2] * (1 - a) + bike.px[s+2] * a);
    px[d+3] = Math.max(px[d+3], bike.px[s+3]);
  }
  return { w: N, h: N, px: px };
}

for (const [name, pressed] of [['mobile_vehicle.dds', false], ['mobile_vehicle_f.dds', true]]) {
  const img = face(pressed);
  fs.writeFileSync(path.join(GUI, name), enc.encodeDXT5(img));
  u.write(path.join(ROOT, 'MOBILE/native/out', name.replace('.dds', '.png')), img);
  console.log('  ' + name + '  ' + N + 'x' + N);
}
