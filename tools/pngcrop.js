//  Crop and magnify a PNG, with no dependencies.
//
//  Screenshots off the tablet are 2560x1600, and the things that need looking
//  at - a hairstyle, a name plate against a character's head, the edge of an
//  icon - are a hundred pixels across. This decodes a PNG, cuts a rectangle out
//  of it, scales it up with nearest-neighbour so nothing is invented, and
//  writes it back out.
//
//    node pngcrop.js in.png out.png x y w h [zoom]
//
const fs = require('fs');
const zlib = require('zlib');

function readChunks(buf) {
    if (buf.readUInt32BE(0) !== 0x89504e47) throw new Error('not a PNG');
    const chunks = [];
    let at = 8;
    while (at < buf.length) {
        const len = buf.readUInt32BE(at);
        const type = buf.toString('latin1', at + 4, at + 8);
        chunks.push({ type, data: buf.slice(at + 8, at + 8 + len) });
        at += 12 + len;
    }
    return chunks;
}

function decode(buf) {
    const chunks = readChunks(buf);
    const ihdr = chunks.find(c => c.type === 'IHDR').data;
    const width = ihdr.readUInt32BE(0), height = ihdr.readUInt32BE(4);
    const depth = ihdr[8], colour = ihdr[9], interlace = ihdr[12];
    if (depth !== 8) throw new Error('only 8-bit channels: depth ' + depth);
    if (interlace !== 0) throw new Error('interlaced PNGs are not handled');
    //  2 = RGB, 6 = RGBA, 0 = grey.
    const channels = colour === 6 ? 4 : colour === 2 ? 3 : colour === 0 ? 1 : 0;
    if (!channels) throw new Error('colour type ' + colour + ' is not handled');

    const idat = Buffer.concat(chunks.filter(c => c.type === 'IDAT').map(c => c.data));
    const raw = zlib.inflateSync(idat);

    const stride = width * channels;
    const out = Buffer.alloc(height * stride);
    let pos = 0;
    for (let y = 0; y < height; ++y) {
        const filter = raw[pos++];
        const line = raw.slice(pos, pos + stride); pos += stride;
        const cur = out.slice(y * stride, (y + 1) * stride);
        const prev = y ? out.slice((y - 1) * stride, y * stride) : null;
        for (let i = 0; i < stride; ++i) {
            const a = i >= channels ? cur[i - channels] : 0;
            const b = prev ? prev[i] : 0;
            const c = (prev && i >= channels) ? prev[i - channels] : 0;
            let v = line[i];
            switch (filter) {
                case 0: break;
                case 1: v += a; break;
                case 2: v += b; break;
                case 3: v += (a + b) >> 1; break;
                case 4: {
                    const p = a + b - c;
                    const pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
                    v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
                    break;
                }
                default: throw new Error('filter ' + filter);
            }
            cur[i] = v & 0xff;
        }
    }
    return { width, height, channels, data: out };
}

function encode(width, height, rgba) {
    const stride = width * 4;
    const raw = Buffer.alloc(height * (stride + 1));
    for (let y = 0; y < height; ++y) {
        raw[y * (stride + 1)] = 0;                       // no filter
        rgba.copy(raw, y * (stride + 1) + 1, y * stride, (y + 1) * stride);
    }
    const idat = zlib.deflateSync(raw, { level: 6 });

    const crcTable = [];
    for (let n = 0; n < 256; ++n) {
        let c = n;
        for (let k = 0; k < 8; ++k) c = (c & 1) ? (0xedb88320 ^ (c >>> 1)) : (c >>> 1);
        crcTable[n] = c >>> 0;
    }
    const crc = b => {
        let c = 0xffffffff;
        for (let i = 0; i < b.length; ++i) c = crcTable[(c ^ b[i]) & 0xff] ^ (c >>> 8);
        return (c ^ 0xffffffff) >>> 0;
    };
    const chunk = (type, data) => {
        const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
        const body = Buffer.concat([Buffer.from(type, 'latin1'), data]);
        const c = Buffer.alloc(4); c.writeUInt32BE(crc(body));
        return Buffer.concat([len, body, c]);
    };

    const ihdr = Buffer.alloc(13);
    ihdr.writeUInt32BE(width, 0); ihdr.writeUInt32BE(height, 4);
    ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    return Buffer.concat([
        Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
        chunk('IHDR', ihdr), chunk('IDAT', idat), chunk('IEND', Buffer.alloc(0)),
    ]);
}

const [, , inPath, outPath, xs, ys, ws, hs, zs] = process.argv;
if (!outPath) { console.error('usage: pngcrop.js in.png out.png x y w h [zoom]'); process.exit(2); }
const img = decode(fs.readFileSync(inPath));
const x0 = parseInt(xs, 10), y0 = parseInt(ys, 10);
const cw = parseInt(ws, 10), ch = parseInt(hs, 10);
const zoom = zs ? parseInt(zs, 10) : 1;

const out = Buffer.alloc(cw * zoom * ch * zoom * 4);
for (let y = 0; y < ch * zoom; ++y) {
    const sy = Math.min(img.height - 1, Math.max(0, y0 + Math.floor(y / zoom)));
    for (let x = 0; x < cw * zoom; ++x) {
        const sx = Math.min(img.width - 1, Math.max(0, x0 + Math.floor(x / zoom)));
        const s = (sy * img.width + sx) * img.channels;
        const d = (y * cw * zoom + x) * 4;
        out[d]     = img.data[s];
        out[d + 1] = img.channels >= 3 ? img.data[s + 1] : img.data[s];
        out[d + 2] = img.channels >= 3 ? img.data[s + 2] : img.data[s];
        out[d + 3] = img.channels === 4 ? img.data[s + 3] : 255;
    }
}
fs.writeFileSync(outPath, encode(cw * zoom, ch * zoom, out));
console.log(inPath + ' ' + img.width + 'x' + img.height + ' -> ' + outPath +
            ' (' + cw * zoom + 'x' + ch * zoom + ')');
