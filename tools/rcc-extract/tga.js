'use strict';
//
// TGA decoder -> RGBA8.
//
// 355 shipped textures are TGA (most of them named `.dds`). Covers the types
// that actually occur: uncompressed and RLE, true-colour (24/32bpp),
// colour-mapped (8bpp) and greyscale.
//
// TGA has no magic number, so callers must sniff before handing a buffer here.
//

/**
 * @param {Buffer} buf
 * @returns {{width:number,height:number,data:Buffer,format:string}}
 */
function decode(buf) {
  if (buf.length < 18) throw new Error('TGA too short');

  const idLength = buf[0];
  const cmType = buf[1];
  const imgType = buf[2];
  const cmFirst = buf.readUInt16LE(3);
  const cmLength = buf.readUInt16LE(5);
  const cmDepth = buf[7];
  const width = buf.readUInt16LE(12);
  const height = buf.readUInt16LE(14);
  const bpp = buf[16];
  const descriptor = buf[17];

  // Bit 5 of the descriptor: 0 = origin bottom-left (needs a vertical flip),
  // 1 = top-left. Getting this wrong yields an upside-down texture, which is
  // easy to miss on symmetric art and obvious on UI.
  const topOrigin = !!(descriptor & 0x20);

  const rle = imgType >= 9;
  const baseType = imgType & 0x7;   // 1 = colour-mapped, 2 = true-colour, 3 = grey
  if (![1, 2, 3].includes(baseType)) throw new Error(`unsupported TGA type ${imgType}`);

  let off = 18 + idLength;

  let palette = null;
  if (cmType === 1) {
    const entryBytes = cmDepth / 8;
    palette = buf.subarray(off, off + cmLength * entryBytes);
    off += cmLength * entryBytes;
  }

  const pixelBytes = bpp / 8;
  if (![1, 2, 3, 4].includes(pixelBytes)) throw new Error(`unsupported TGA bpp ${bpp}`);

  const count = width * height;
  const raw = Buffer.alloc(count * pixelBytes);

  if (!rle) {
    buf.copy(raw, 0, off, off + Math.min(raw.length, buf.length - off));
  } else {
    let o = 0;
    while (o < raw.length && off < buf.length) {
      const packet = buf[off++];
      const n = (packet & 0x7f) + 1;
      if (packet & 0x80) { // run-length packet: one pixel repeated
        if (off + pixelBytes > buf.length) break;
        for (let i = 0; i < n && o < raw.length; i++, o += pixelBytes) {
          buf.copy(raw, o, off, off + pixelBytes);
        }
        off += pixelBytes;
      } else { // raw packet
        const bytes = n * pixelBytes;
        buf.copy(raw, o, off, Math.min(off + bytes, buf.length));
        o += bytes;
        off += bytes;
      }
    }
  }

  const out = Buffer.alloc(count * 4, 255);
  for (let i = 0; i < count; i++) {
    const s = i * pixelBytes;
    const d = i * 4;
    if (baseType === 1) { // colour-mapped
      const idx = (raw[s] - cmFirst) * (cmDepth / 8);
      const e = cmDepth / 8;
      if (palette && idx >= 0 && idx + e <= palette.length) {
        if (e === 2) {
          const v = palette.readUInt16LE(idx);
          out[d] = ((v >> 10) & 0x1f) * 255 / 31 | 0;
          out[d + 1] = ((v >> 5) & 0x1f) * 255 / 31 | 0;
          out[d + 2] = (v & 0x1f) * 255 / 31 | 0;
        } else {
          out[d] = palette[idx + 2];
          out[d + 1] = palette[idx + 1];
          out[d + 2] = palette[idx];
          if (e === 4) out[d + 3] = palette[idx + 3];
        }
      }
    } else if (baseType === 3) { // greyscale
      out[d] = out[d + 1] = out[d + 2] = raw[s];
      if (pixelBytes === 2) out[d + 3] = raw[s + 1];
    } else { // true-colour, stored BGR(A)
      if (pixelBytes === 2) {
        const v = raw.readUInt16LE(s);
        out[d] = ((v >> 10) & 0x1f) * 255 / 31 | 0;
        out[d + 1] = ((v >> 5) & 0x1f) * 255 / 31 | 0;
        out[d + 2] = (v & 0x1f) * 255 / 31 | 0;
        out[d + 3] = (v & 0x8000) ? 255 : 255;
      } else {
        out[d] = raw[s + 2];
        out[d + 1] = raw[s + 1];
        out[d + 2] = raw[s];
        if (pixelBytes === 4) out[d + 3] = raw[s + 3];
      }
    }
  }

  if (!topOrigin) {
    const row = width * 4;
    const tmp = Buffer.alloc(row);
    for (let y = 0; y < (height >> 1); y++) {
      const a = y * row;
      const b = (height - 1 - y) * row;
      out.copy(tmp, 0, a, a + row);
      out.copy(out, a, b, b + row);
      tmp.copy(out, b);
    }
  }

  return { width, height, data: out, format: `TGA${bpp}${rle ? '-RLE' : ''}` };
}

module.exports = { decode };
