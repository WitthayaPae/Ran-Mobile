'use strict';
//
// Minimal PNG encoder (RGBA8, no interlacing).
//
// Node ships zlib, which is the only hard part of PNG, so this avoids a
// dependency for what amounts to three chunks and a CRC.
//
const zlib = require('zlib');

const CRC_TABLE = new Int32Array(256);
for (let n = 0; n < 256; n++) {
  let c = n;
  for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  CRC_TABLE[n] = c;
}
function crc32(buf) {
  let c = -1;
  for (let i = 0; i < buf.length; i++) c = CRC_TABLE[(c ^ buf[i]) & 0xff] ^ (c >>> 8);
  return (c ^ -1) >>> 0;
}

function chunk(type, data) {
  const out = Buffer.alloc(12 + data.length);
  out.writeUInt32BE(data.length, 0);
  out.write(type, 4, 'latin1');
  data.copy(out, 8);
  const forCrc = Buffer.concat([Buffer.from(type, 'latin1'), data]);
  out.writeUInt32BE(crc32(forCrc), 8 + data.length);
  return out;
}

/**
 * @param {number} width
 * @param {number} height
 * @param {Buffer} rgba  width*height*4 bytes
 * @returns {Buffer} PNG file
 */
function encode(width, height, rgba) {
  if (rgba.length < width * height * 4) {
    throw new Error(`rgba buffer too small: ${rgba.length} < ${width * height * 4}`);
  }
  // Filter type 0 (None) per scanline. Better filters would shrink the output,
  // but these are an intermediate artefact — the engine importer recompresses.
  const raw = Buffer.alloc(height * (1 + width * 4));
  for (let y = 0; y < height; y++) {
    const o = y * (1 + width * 4);
    raw[o] = 0;
    rgba.copy(raw, o + 1, y * width * 4, (y + 1) * width * 4);
  }

  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0);
  ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8;  // bit depth
  ihdr[9] = 6;  // colour type: RGBA
  ihdr[10] = 0; // deflate
  ihdr[11] = 0; // adaptive filtering
  ihdr[12] = 0; // no interlace

  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', ihdr),
    chunk('IDAT', zlib.deflateSync(raw, { level: 6 })),
    chunk('IEND', Buffer.alloc(0)),
  ]);
}

module.exports = { encode, crc32 };
