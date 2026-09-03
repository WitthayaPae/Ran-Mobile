//  Builds the app icons and the in-app marks from one square logo.
//
//  Usage: node make-icons.js <logo.png>
//
//  The source is a JPEG-flattened square: gold artwork on a black field, no
//  alpha. Two different things are wanted from it.
//
//    * The launcher icon keeps the black. It is part of the artwork, the logo
//      reads as a coin on it, and a legacy icon is drawn as a square anyway.
//    * The adaptive foreground and the in-app marks need the black gone, so
//      the mark can sit over the loading art and the launcher can mask the
//      icon to whatever shape it likes.
//
//  The black is removed by flood-filling inwards from the border rather than by
//  thresholding the whole image: the logo's own metal is nearly black in places
//  and a plain threshold eats holes right through the middle of it. Only black
//  that is connected to the edge is background.
const fs = require('fs'), path = require('path');
const u = require(path.join(__dirname, 'imgutil.js'));

const src = u.decode(process.argv[2] || 'MOBILE/native/out/logo_src.png');
const W = src.w, H = src.h;

const lum = (i) => (src.px[i] * 77 + src.px[i + 1] * 151 + src.px[i + 2] * 28) >> 8;
const kBlack = 26;                       //  corners measure 0; the glow starts well above this

const bg = new Uint8Array(W * H);
const stack = [];
for (let x = 0; x < W; x++) { stack.push(x, 0); stack.push(x, H - 1); }
for (let y = 0; y < H; y++) { stack.push(0, y); stack.push(W - 1, y); }
while (stack.length) {
  const y = stack.pop(), x = stack.pop();
  if (x < 0 || y < 0 || x >= W || y >= H) continue;
  const k = y * W + x;
  if (bg[k]) continue;
  if (lum(k * 4) > kBlack) continue;
  bg[k] = 1;
  stack.push(x + 1, y); stack.push(x - 1, y); stack.push(x, y + 1); stack.push(x, y - 1);
}

//  Keyed copy, and the logo's bounding box within it.
const keyed = { w: W, h: H, px: Buffer.from(src.px) };
let minX = W, minY = H, maxX = -1, maxY = -1;
for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
  const k = y * W + x;
  if (bg[k]) { keyed.px[k * 4 + 3] = 0; continue; }
  if (x < minX) minX = x; if (x > maxX) maxX = x;
  if (y < minY) minY = y; if (y > maxY) maxY = y;
}
console.log('logo bounds ' + minX + ',' + minY + ' .. ' + maxX + ',' + maxY +
            '  (' + (maxX - minX + 1) + 'x' + (maxY - minY + 1) + ')');

//  Trim to the artwork, then pad back to a square so nothing is distorted.
function cropSquare(img) {
  const cw = maxX - minX + 1, ch = maxY - minY + 1, s = Math.max(cw, ch);
  const out = Buffer.alloc(s * s * 4);
  const ox = ((s - cw) / 2) | 0, oy = ((s - ch) / 2) | 0;
  for (let y = 0; y < ch; y++) for (let x = 0; x < cw; x++) {
    const sI = ((minY + y) * img.w + (minX + x)) * 4, d = ((oy + y) * s + (ox + x)) * 4;
    img.px.copy(out, d, sI, sI + 4);
  }
  return { w: s, h: s, px: out };
}
const art = cropSquare(keyed);

function put(file, img) {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  u.write(file, img);
  console.log('  ' + file + '  ' + img.w + 'x' + img.h);
}

//  Logo on its own black square, for the legacy icon.
function onBlack(size) {
  const s = u.resize(art, size, size);
  const out = Buffer.alloc(size * size * 4);
  for (let i = 0; i < size * size; i++) {
    const a = s.px[i * 4 + 3] / 255;
    out[i * 4]     = Math.round(s.px[i * 4] * a);
    out[i * 4 + 1] = Math.round(s.px[i * 4 + 1] * a);
    out[i * 4 + 2] = Math.round(s.px[i * 4 + 2] * a);
    out[i * 4 + 3] = 255;
  }
  return { w: size, h: size, px: out };
}

//  Adaptive foreground: a 108-unit canvas with the art inside the 66-unit safe
//  zone, so no launcher's mask can clip it.
function foreground(size) {
  const inner = Math.round(size * 66 / 108);
  const s = u.resize(art, inner, inner);
  const out = Buffer.alloc(size * size * 4);
  const off = ((size - inner) / 2) | 0;
  for (let y = 0; y < inner; y++)
    s.px.copy(out, ((off + y) * size + off) * 4, y * inner * 4, (y + 1) * inner * 4);
  return { w: size, h: size, px: out };
}

const RES = 'MOBILE/native/android/res';
const dpi = [['mdpi', 1], ['hdpi', 1.5], ['xhdpi', 2], ['xxhdpi', 3], ['xxxhdpi', 4]];
for (const [name, k] of dpi) {
  put(RES + '/mipmap-' + name + '/ic_launcher.png', onBlack(Math.round(48 * k)));
  put(RES + '/mipmap-' + name + '/ic_launcher_foreground.png', foreground(Math.round(108 * k)));
}

//  The mark drawn over the loading art, by the launcher and by the boot screen.
put(RES + '/drawable-nodpi/ran_mark.png', u.resize(art, 512, 512));
//  And a full-size transparent master for anything else that needs it.
put('MOBILE/native/out/logo_rgba.png', art);
