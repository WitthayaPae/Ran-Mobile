'use strict';
//
// DDS decoder -> RGBA8.
//
// Covers everything the census found in Ran/: DXT1/2/3/5 and the uncompressed
// RGB16/24/32(A) variants. No dependencies — the block formats are simple and
// a native module would be one more thing to build per platform.
//
// Only the top mip is decoded. Mobile pipelines regenerate mip chains anyway,
// and the shipped chains are DXT-compressed at each level, so re-deriving them
// after transcode is both easier and higher quality than carrying them across.
//

const DDPF_FOURCC = 0x4;
const DDPF_RGB = 0x40;
const DDPF_ALPHAPIXELS = 0x1;
const DDSCAPS2_CUBEMAP = 0x200;
const DDSCAPS2_VOLUME = 0x200000;

/** Parse the 128-byte header. Returns null if this is not a DDS. */
function parseHeader(buf) {
  if (buf.length < 128 || buf.readUInt32LE(0) !== 0x20534444) return null;
  const h = 4;
  const pf = h + 72;
  const pfFlags = buf.readUInt32LE(pf + 4);
  const fourCC = buf.toString('latin1', pf + 8, pf + 12).replace(/\0/g, '');
  const caps2 = buf.readUInt32LE(h + 108);

  return {
    height: buf.readUInt32LE(h + 8),
    width: buf.readUInt32LE(h + 12),
    pitchOrLinear: buf.readUInt32LE(h + 16),
    mipCount: Math.max(1, buf.readUInt32LE(h + 24)),
    fourCC: (pfFlags & DDPF_FOURCC) ? fourCC : null,
    rgb: !!(pfFlags & DDPF_RGB),
    hasAlpha: !!(pfFlags & DDPF_ALPHAPIXELS),
    rgbBits: buf.readUInt32LE(pf + 12),
    masks: {
      r: buf.readUInt32LE(pf + 16),
      g: buf.readUInt32LE(pf + 20),
      b: buf.readUInt32LE(pf + 24),
      a: buf.readUInt32LE(pf + 28),
    },
    cubemap: !!(caps2 & DDSCAPS2_CUBEMAP),
    volume: !!(caps2 & DDSCAPS2_VOLUME),
    dataOffset: 128,
  };
}

const rgb565 = (v) => [
  ((v >> 11) & 0x1f) * 255 / 31 | 0,
  ((v >> 5) & 0x3f) * 255 / 63 | 0,
  (v & 0x1f) * 255 / 31 | 0,
];

/**
 * Decode one 4x4 colour block (the DXT1 colour scheme, shared by 2/3/5).
 * `opaque` selects the DXT1 1-bit-alpha variant, where c0 <= c1 means the
 * fourth entry is transparent black rather than an interpolated colour.
 */
function decodeColorBlock(src, off, out, ox, oy, width, height, opaque) {
  const c0 = src.readUInt16LE(off);
  const c1 = src.readUInt16LE(off + 2);
  const bits = src.readUInt32LE(off + 4);
  const p = [rgb565(c0), rgb565(c1), null, null];

  if (c0 > c1 || opaque) {
    p[2] = [(2 * p[0][0] + p[1][0]) / 3 | 0,
            (2 * p[0][1] + p[1][1]) / 3 | 0,
            (2 * p[0][2] + p[1][2]) / 3 | 0];
    p[3] = [(p[0][0] + 2 * p[1][0]) / 3 | 0,
            (p[0][1] + 2 * p[1][1]) / 3 | 0,
            (p[0][2] + 2 * p[1][2]) / 3 | 0];
  } else {
    p[2] = [(p[0][0] + p[1][0]) >> 1,
            (p[0][1] + p[1][1]) >> 1,
            (p[0][2] + p[1][2]) >> 1];
    p[3] = [0, 0, 0]; // transparent in DXT1a
  }

  for (let y = 0; y < 4; y++) {
    for (let x = 0; x < 4; x++) {
      const px = ox + x;
      const py = oy + y;
      if (px >= width || py >= height) continue;
      const idx = (bits >> (2 * (4 * y + x))) & 3;
      const o = (py * width + px) * 4;
      out[o] = p[idx][0];
      out[o + 1] = p[idx][1];
      out[o + 2] = p[idx][2];
      // Alpha belongs to the colour block ONLY in DXT1 (opaque=false):
      // 1-bit transparency via index 3 with c0 <= c1. For DXT2/3/5
      // (opaque=true) the alpha block decoded BEFORE this call owns the
      // channel — writing 255 here silently flattened every DXT3/DXT5
      // texture's alpha, which is how the cloud sails became opaque photos
      // and the sky turned into "multiple pictures moving".
      if (!opaque) out[o + 3] = (c0 <= c1 && idx === 3) ? 0 : 255;
    }
  }
}

/** DXT2/3: 4 bits of explicit alpha per texel. */
function decodeAlpha4(src, off, out, ox, oy, width, height) {
  for (let y = 0; y < 4; y++) {
    const row = src.readUInt16LE(off + y * 2);
    for (let x = 0; x < 4; x++) {
      const px = ox + x;
      const py = oy + y;
      if (px >= width || py >= height) continue;
      const a = (row >> (4 * x)) & 0xf;
      out[(py * width + px) * 4 + 3] = a * 17; // 0..15 -> 0..255
    }
  }
}

/** DXT4/5: two endpoints plus 3-bit interpolation indices. */
function decodeAlpha8(src, off, out, ox, oy, width, height) {
  const a0 = src[off];
  const a1 = src[off + 1];
  const a = [a0, a1];
  if (a0 > a1) {
    for (let i = 1; i < 7; i++) a.push(((7 - i) * a0 + i * a1) / 7 | 0);
  } else {
    for (let i = 1; i < 5; i++) a.push(((5 - i) * a0 + i * a1) / 5 | 0);
    a.push(0, 255);
  }
  // 16 x 3-bit indices packed into 6 bytes, little-endian.
  let lo = src.readUIntLE(off + 2, 3);
  let hi = src.readUIntLE(off + 5, 3);
  for (let i = 0; i < 16; i++) {
    const bitsSrc = i < 8 ? lo : hi;
    const shift = (i % 8) * 3;
    const idx = (bitsSrc >> shift) & 7;
    const px = ox + (i % 4);
    const py = oy + ((i / 4) | 0);
    if (px >= width || py >= height) continue;
    out[(py * width + px) * 4 + 3] = a[idx];
  }
}

function decodeBlockCompressed(buf, h, fourCC) {
  const { width, height } = h;
  const out = Buffer.alloc(width * height * 4, 255);
  const bw = Math.max(1, Math.ceil(width / 4));
  const bh = Math.max(1, Math.ceil(height / 4));
  const dxt1 = fourCC === 'DXT1';
  const blockBytes = dxt1 ? 8 : 16;

  let off = h.dataOffset;
  for (let by = 0; by < bh; by++) {
    for (let bx = 0; bx < bw; bx++) {
      if (off + blockBytes > buf.length) return out; // truncated; keep what we have
      const ox = bx * 4;
      const oy = by * 4;
      if (dxt1) {
        decodeColorBlock(buf, off, out, ox, oy, width, height, false);
      } else if (fourCC === 'DXT2' || fourCC === 'DXT3') {
        decodeAlpha4(buf, off, out, ox, oy, width, height);
        decodeColorBlock(buf, off + 8, out, ox, oy, width, height, true);
      } else { // DXT4 / DXT5
        decodeAlpha8(buf, off, out, ox, oy, width, height);
        decodeColorBlock(buf, off + 8, out, ox, oy, width, height, true);
      }
      off += blockBytes;
    }
  }
  return out;
}

const shiftOf = (mask) => { if (!mask) return 0; let s = 0; while (!((mask >>> s) & 1)) s++; return s; };
const scaleOf = (mask, shift) => { const m = mask >>> shift; return m ? 255 / m : 0; };

function decodeUncompressed(buf, h) {
  const { width, height, rgbBits, masks } = h;
  const bytes = rgbBits / 8;
  const out = Buffer.alloc(width * height * 4, 255);
  const sh = { r: shiftOf(masks.r), g: shiftOf(masks.g), b: shiftOf(masks.b), a: shiftOf(masks.a) };
  const sc = {
    r: scaleOf(masks.r, sh.r), g: scaleOf(masks.g, sh.g),
    b: scaleOf(masks.b, sh.b), a: scaleOf(masks.a, sh.a),
  };

  let off = h.dataOffset;
  for (let i = 0; i < width * height; i++, off += bytes) {
    if (off + bytes > buf.length) break;
    const v = bytes === 4 ? buf.readUInt32LE(off)
            : bytes === 3 ? buf.readUIntLE(off, 3)
            : buf.readUInt16LE(off);
    const o = i * 4;
    out[o] = ((v & masks.r) >>> sh.r) * sc.r | 0;
    out[o + 1] = ((v & masks.g) >>> sh.g) * sc.g | 0;
    out[o + 2] = ((v & masks.b) >>> sh.b) * sc.b | 0;
    out[o + 3] = masks.a ? (((v & masks.a) >>> sh.a) * sc.a | 0) : 255;
  }
  return out;
}

/**
 * Decode the top mip to RGBA8.
 * @returns {{width:number,height:number,data:Buffer,format:string}|null}
 */
function decode(buf) {
  const h = parseHeader(buf);
  if (!h) return null;
  if (h.fourCC) {
    if (!['DXT1', 'DXT2', 'DXT3', 'DXT4', 'DXT5'].includes(h.fourCC)) {
      throw new Error(`unsupported DDS fourCC "${h.fourCC}"`);
    }
    return { width: h.width, height: h.height, format: h.fourCC,
             data: decodeBlockCompressed(buf, h, h.fourCC) };
  }
  if (h.rgb) {
    if (![16, 24, 32].includes(h.rgbBits)) {
      throw new Error(`unsupported DDS bit depth ${h.rgbBits}`);
    }
    return { width: h.width, height: h.height,
             format: `RGB${h.rgbBits}${h.hasAlpha ? 'A' : ''}`,
             data: decodeUncompressed(buf, h) };
  }
  throw new Error('unsupported DDS pixel format');
}

module.exports = { decode, parseHeader };
