'use strict';
//  DXT5 DDS writer.
//
//  There was no encoder in the tree, only decoders, and the format matters:
//  TextureManager picks how a texture is drawn from the format it comes back
//  as. D3DFMT_A8R8G8B8 lands in the EMTT_ALPHA_HARD case - alpha test, no
//  blending - which turns a logo's glow into a jagged cut-out. DXT5 is the
//  EMTT_ALPHA_SOFT case, which is what every other UI mark in the game uses.
const path = require('path');
const u = require(path.join(__dirname, 'imgutil.js'));

function to565(r, g, b) {
  return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}
function from565(c) {
  return [((c >> 11) & 31) * 255 / 31, ((c >> 5) & 63) * 255 / 63, (c & 31) * 255 / 31];
}

function encodeBlock(px, w, h, bx, by, out, o) {
  //  Gather the 4x4, clamping at the edges.
  const R = [], G = [], B = [], A = [];
  for (let y = 0; y < 4; y++) for (let x = 0; x < 4; x++) {
    const sx = Math.min(w - 1, bx + x), sy = Math.min(h - 1, by + y);
    const s = (sy * w + sx) * 4;
    R.push(px[s]); G.push(px[s + 1]); B.push(px[s + 2]); A.push(px[s + 3]);
  }

  //  --- alpha: two endpoints and eight interpolated steps
  let a0 = Math.max.apply(null, A), a1 = Math.min.apply(null, A);
  out[o] = a0; out[o + 1] = a1;
  const pal = new Array(8);
  if (a0 > a1) {
    pal[0] = a0; pal[1] = a1;
    for (let i = 2; i < 8; i++) pal[i] = ((8 - i) * a0 + (i - 1) * a1) / 7;
  } else {
    pal[0] = a0; pal[1] = a1;
    for (let i = 2; i < 6; i++) pal[i] = ((6 - i) * a0 + (i - 1) * a1) / 5;
    pal[6] = 0; pal[7] = 255;
  }
  let bits = 0n;
  for (let i = 15; i >= 0; i--) {
    let best = 0, bd = 1e9;
    for (let k = 0; k < 8; k++) { const d = Math.abs(pal[k] - A[i]); if (d < bd) { bd = d; best = k; } }
    bits = (bits << 3n) | BigInt(best);
  }
  for (let i = 0; i < 6; i++) out[o + 2 + i] = Number((bits >> BigInt(8 * i)) & 0xFFn);

  //  --- colour: bounding-box endpoints, four-colour mode
  //  Fully transparent texels carry no colour worth fitting, so they are left
  //  out of the box - otherwise the black they were keyed from drags both
  //  endpoints dark and the visible edge goes muddy.
  let rl = 255, gl = 255, bl = 255, rh = 0, gh = 0, bh = 0, any = false;
  for (let i = 0; i < 16; i++) {
    if (A[i] < 8) continue;
    any = true;
    if (R[i] < rl) rl = R[i]; if (R[i] > rh) rh = R[i];
    if (G[i] < gl) gl = G[i]; if (G[i] > gh) gh = G[i];
    if (B[i] < bl) bl = B[i]; if (B[i] > bh) bh = B[i];
  }
  if (!any) { rl = gl = bl = rh = gh = bh = 0; }

  let c0 = to565(rh, gh, bh), c1 = to565(rl, gl, bl);
  if (c0 === c1) { out[o+8]=c0&255; out[o+9]=c0>>8; out[o+10]=c1&255; out[o+11]=c1>>8;
                   out[o+12]=out[o+13]=out[o+14]=out[o+15]=0; return; }
  let swap = false;
  if (c0 < c1) { const t = c0; c0 = c1; c1 = t; swap = true; }
  const e0 = from565(c0), e1 = from565(c1);
  const cp = [e0, e1,
              [(2*e0[0]+e1[0])/3, (2*e0[1]+e1[1])/3, (2*e0[2]+e1[2])/3],
              [(e0[0]+2*e1[0])/3, (e0[1]+2*e1[1])/3, (e0[2]+2*e1[2])/3]];
  out[o+8]=c0&255; out[o+9]=c0>>8; out[o+10]=c1&255; out[o+11]=c1>>8;
  for (let row = 0; row < 4; row++) {
    let byte = 0;
    for (let col = 3; col >= 0; col--) {
      const i = row * 4 + col;
      let best = 0, bd = 1e18;
      for (let k = 0; k < 4; k++) {
        const dr = cp[k][0]-R[i], dg = cp[k][1]-G[i], db = cp[k][2]-B[i];
        const d = dr*dr + dg*dg + db*db;
        if (d < bd) { bd = d; best = k; }
      }
      byte = (byte << 2) | best;
    }
    out[o + 12 + row] = byte;
  }
  if (swap) { /* endpoints already ordered; indices computed against them */ }
}

function encodeLevel(img) {
  const bw = Math.ceil(img.w / 4), bh = Math.ceil(img.h / 4);
  const out = Buffer.alloc(bw * bh * 16);
  let o = 0;
  for (let by = 0; by < bh; by++) for (let bx = 0; bx < bw; bx++) {
    encodeBlock(img.px, img.w, img.h, bx * 4, by * 4, out, o); o += 16;
  }
  return out;
}

//  A DDS with a full mip chain, which is what the rest of the GUI textures have.
function encodeDXT5(img) {
  const levels = [];
  let cur = img;
  while (true) {
    levels.push(encodeLevel(cur));
    if (cur.w <= 1 && cur.h <= 1) break;
    cur = u.resize(cur, Math.max(1, cur.w >> 1), Math.max(1, cur.h >> 1));
  }
  const head = Buffer.alloc(128);
  head.write('DDS ', 0, 'ascii');
  head.writeUInt32LE(124, 4);
  //  CAPS|HEIGHT|WIDTH|PIXELFORMAT|MIPMAPCOUNT|LINEARSIZE
  head.writeUInt32LE(0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000, 8);
  head.writeUInt32LE(img.h, 12);
  head.writeUInt32LE(img.w, 16);
  head.writeUInt32LE(levels[0].length, 20);
  head.writeUInt32LE(levels.length, 28);
  head.writeUInt32LE(32, 76);            // pixel format size
  head.writeUInt32LE(0x4, 80);           // DDPF_FOURCC
  head.write('DXT5', 84, 'ascii');
  head.writeUInt32LE(0x1000 | 0x400000 | 0x8, 108);   // TEXTURE|MIPMAP|COMPLEX
  return Buffer.concat([head].concat(levels));
}

module.exports = { encodeDXT5 };
