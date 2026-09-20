'use strict';
//  Write the icon atlas as an uncompressed A8R8G8B8 .dds, which is the format
//  every other GUI sheet in this client already uses (interface_main.dds,
//  q_icon.dds, sc_battle_ui_set.dds). DXT would halve the file and quantise
//  the plate's gradient into bands, on the one surface the player looks at
//  closest.
const fs = require('fs');
const path = require('path');
const png = require(path.join('C:/Users/tapnu/Downloads/RAN/DEV EP9/MOBILE/tools/rcc-extract', 'png.js'));

//  Re-run the renderer's own output rather than re-deriving it: draw.js wrote
//  atlas.png straight from the rasteriser.
const zlib = require('zlib');
function readPng(file) {
  const buf = fs.readFileSync(file);
  let p = 8, w = 0, h = 0;
  const idat = [];
  while (p < buf.length) {
    const len = buf.readUInt32BE(p), type = buf.toString('latin1', p + 4, p + 8);
    if (type === 'IHDR') { w = buf.readUInt32BE(p + 8); h = buf.readUInt32BE(p + 12); }
    else if (type === 'IDAT') idat.push(buf.slice(p + 8, p + 8 + len));
    p += 12 + len;
  }
  const raw = zlib.inflateSync(Buffer.concat(idat));
  const out = Buffer.alloc(w * h * 4);
  const stride = w * 4;
  let prev = Buffer.alloc(stride);
  for (let y = 0; y < h; y++) {
    const ft = raw[y * (stride + 1)];
    const row = Buffer.from(raw.slice(y * (stride + 1) + 1, (y + 1) * (stride + 1)));
    for (let i = 0; i < stride; i++) {
      const a = i >= 4 ? row[i - 4] : 0, b = prev[i], c = i >= 4 ? prev[i - 4] : 0;
      let v = row[i];
      if (ft === 1) v += a;
      else if (ft === 2) v += b;
      else if (ft === 3) v += (a + b) >> 1;
      else if (ft === 4) {
        const pp = a + b - c, pa = Math.abs(pp - a), pb = Math.abs(pp - b), pc = Math.abs(pp - c);
        v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
      }
      row[i] = v & 0xff;
    }
    row.copy(out, y * stride);
    prev = row;
  }
  return { width: w, height: h, data: out };
}

//  Which sheet to write. The menu icons come from atlas.png and the on-screen
//  controls from hud.png, and both take the same header - so the source is an
//  argument rather than a second copy of this file.
const src = process.argv[3] || path.join(__dirname, 'atlas.png');
const img = readPng(src);

const H = Buffer.alloc(128);
H.write('DDS ', 0, 'latin1');
H.writeUInt32LE(124, 4);                      //  dwSize
H.writeUInt32LE(0x1 | 0x2 | 0x4 | 0x1000 | 0x8, 8);   //  CAPS|HEIGHT|WIDTH|PIXELFORMAT|PITCH
H.writeUInt32LE(img.height, 12);
H.writeUInt32LE(img.width, 16);
H.writeUInt32LE(img.width * 4, 20);           //  pitch
H.writeUInt32LE(0, 24);                       //  depth
H.writeUInt32LE(0, 28);                       //  mipmaps
H.writeUInt32LE(32, 76);                      //  pf size
H.writeUInt32LE(0x41, 80);                    //  ALPHAPIXELS|RGB
H.writeUInt32LE(0, 84);                       //  fourCC
H.writeUInt32LE(32, 88);                      //  bit count
H.writeUInt32LE(0x00FF0000, 92);              //  R
H.writeUInt32LE(0x0000FF00, 96);              //  G
H.writeUInt32LE(0x000000FF, 100);             //  B
H.writeUInt32LE(0xFF000000, 104);             //  A
H.writeUInt32LE(0x1000, 108);                 //  caps TEXTURE

const px = Buffer.alloc(img.width * img.height * 4);
for (let i = 0; i < img.width * img.height; i++) {
  px[i * 4]     = img.data[i * 4 + 2];        //  B
  px[i * 4 + 1] = img.data[i * 4 + 1];        //  G
  px[i * 4 + 2] = img.data[i * 4];            //  R
  px[i * 4 + 3] = img.data[i * 4 + 3];        //  A
}

const dest = process.argv[2];
fs.writeFileSync(dest, Buffer.concat([H, px]));
console.log(`${dest}  ${img.width}x${img.height}  ${(128 + px.length) / 1048576} MB`);
