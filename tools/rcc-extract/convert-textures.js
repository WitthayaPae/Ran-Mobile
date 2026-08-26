'use strict';
//
// Texture conversion: everything in Ran/ -> PNG (RGBA8).
//
//   node convert-textures.js --out DIR [--limit N] [--dry]
//
// PNG is the intermediate, not the shipping format. The engine importer will
// re-encode to ETC2/ASTC per platform; what matters here is getting every
// texture out of D3D-specific containers losslessly and in one place.
//
// Files are identified by CONTENT, never by extension — 247 files named .dds
// are actually PNG/TGA/PSD. Anything already in a portable format is copied
// through rather than re-encoded.
//
const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc');
const gamecrypt = require('./gamecrypt');
const dds = require('./dds');
const png = require('./png');
const tga = require('./tga');

const RAN = path.join(__dirname, '..', '..', '..', 'Ran');
const argv = process.argv.slice(2);
const val = (f, d) => { const i = argv.indexOf(f); return i === -1 ? d : argv[i + 1]; };
const outDir = val('--out', null);
const limit = Number(val('--limit', Infinity));
const dry = argv.includes('--dry');

if (!outDir && !dry) {
  console.error('usage: node convert-textures.js --out DIR [--limit N] [--dry]');
  process.exit(2);
}

function sniff(buf) {
  if (buf.length === 0) return 'empty';
  if (buf.length >= 4 && buf.readUInt32LE(0) === 0x20534444) return 'DDS';
  if (buf.length >= 8 && buf.toString('hex', 0, 8) === '89504e470d0a1a0a') return 'PNG';
  if (buf.length >= 4 && buf.toString('latin1', 0, 4) === '8BPS') return 'PSD';
  if (buf.length >= 2 && buf.toString('latin1', 0, 2) === 'BM') return 'BMP';
  if (buf.length >= 3 && buf[0] === 0xff && buf[1] === 0xd8 && buf[2] === 0xff) return 'JPEG';
  if (buf.length >= 18) {
    const cm = buf[1], t = buf[2];
    if ((cm === 0 || cm === 1) && [1, 2, 3, 9, 10, 11].includes(t)) return 'TGA';
  }
  return 'unknown';
}

const TEXTURE_EXT = new Set(['.dds', '.tga', '.png', '.bmp', '.jpg', '.jpeg']);

const stats = { seen: 0, converted: 0, copied: 0, skipped: 0, failed: 0,
                inBytes: 0, outBytes: 0 };
const byKind = new Map();
const failures = [];

function handle(name, data, relDir) {
  if (stats.seen >= limit) return;
  if (!TEXTURE_EXT.has(path.extname(name).toLowerCase())) return;
  stats.seen++;
  stats.inBytes += data.length;

  const kind = sniff(data);
  byKind.set(kind, (byKind.get(kind) || 0) + 1);

  let outName = path.basename(name);
  let outData = null;

  try {
    if (kind === 'DDS') {
      const img = dds.decode(data);
      dehalo(img.width, img.height, img.data);
      outData = png.encode(img.width, img.height, img.data);
      outName = outName.replace(/\.[^.]+$/, '') + '.png';
      stats.converted++;
    } else if (kind === 'TGA') {
      const img = tga.decode(data);
      outData = png.encode(img.width, img.height, img.data);
      outName = outName.replace(/\.[^.]+$/, '') + '.png';
      stats.converted++;
    } else if (kind === 'PNG') {
      outData = data; // already portable
      outName = outName.replace(/\.[^.]+$/, '') + '.png';
      stats.copied++;
    } else {
      // TGA/PSD/BMP/JPEG/unknown: pass through untouched, preserving the real
      // type in the name so a later pass can pick them up. Re-encoding these
      // without a tested decoder would risk silent corruption.
      outData = data;
      outName = `${outName}.${kind.toLowerCase()}`;
      stats.skipped++;
    }
  } catch (err) {
    stats.failed++;
    if (failures.length < 12) failures.push(`${name}: ${err.message}`);
    return;
  }

  stats.outBytes += outData.length;
  if (!dry) {
    const dest = path.join(outDir, relDir);
    fs.mkdirSync(dest, { recursive: true });
    fs.writeFileSync(path.join(dest, outName), outData);
  }
}

/**
 * Repair the DXT dark-edge halo.
 *
 * Block compression averages edge texels with the black the artists left
 * under the transparent region, so the one-or-two texel band where alpha
 * fades carries near-black RGB. The PC renders these atlases at 1:1 where
 * that band stays sub-pixel; a phone canvas magnifies ~2.7x with bilinear
 * filtering and the band becomes a visible dark outline (the login logo was
 * the complaint that found this).
 *
 * Fix: for texels whose alpha is below 250 and which sit within 2 texels of
 * solid coverage, replace RGB with the nearest solid texel's colour — alpha
 * untouched. The 2-texel bound matters: it covers the DXT block-edge noise
 * but leaves broad AUTHORED semi-transparent areas (the cloud sails are
 * mostly alpha 20..120 by design) exactly as painted.
 */
function dehalo(width, height, px) {
  const solid = new Uint8Array(width * height);
  let any = 0, hasAlpha = false;
  for (let i = 0; i < width * height; i++) {
    const a = px[i * 4 + 3];
    if (a < 255) hasAlpha = true;
    if (a >= 250) { solid[i] = 1; any++; }
  }
  if (!hasAlpha || any === 0) return;

  // Two dilation passes: each pass colours texels adjacent to the current
  // solid set and adds them to it. ONLY texels below half alpha are
  // candidates — the halo band always is, while AUTHORED semi-transparent
  // fills (window bodies sit around 0.5-0.8 alpha) must keep their painted
  // colour. The first version of this pass recoloured those fills from
  // whatever opaque sprite happened to neighbour them in the atlas, which
  // visibly changed the tone of every translucent UI panel.
  for (let pass = 0; pass < 2; pass++) {
    const next = Uint8Array.from(solid);
    for (let y = 0; y < height; y++) {
      for (let x = 0; x < width; x++) {
        const i = y * width + x;
        if (solid[i]) continue;
        if (px[i * 4 + 3] >= 128) continue;
        // nearest of the 4-neighbourhood that is already solid
        let src = -1;
        if (x > 0 && solid[i - 1]) src = i - 1;
        else if (x + 1 < width && solid[i + 1]) src = i + 1;
        else if (y > 0 && solid[i - width]) src = i - width;
        else if (y + 1 < height && solid[i + width]) src = i + width;
        if (src < 0) continue;
        px[i * 4] = px[src * 4];
        px[i * 4 + 1] = px[src * 4 + 1];
        px[i * 4 + 2] = px[src * 4 + 2];
        next[i] = 1;
      }
    }
    solid.set(next);
  }
}

const t0 = Date.now();
(function walk(dir) {
  let items;
  try { items = fs.readdirSync(dir, { withFileTypes: true }); } catch { return; }
  for (const it of items) {
    if (stats.seen >= limit) return;
    const p = path.join(dir, it.name);
    if (it.isDirectory()) { walk(p); continue; }
    const rel = path.relative(RAN, dir) || '.';
    if (it.name.toLowerCase().endsWith('.rcc')) {
      let ar;
      try { ar = new RccArchive(p); } catch { continue; }
      for (const e of ar.entries) {
        if (stats.seen >= limit) break;
        if (!TEXTURE_EXT.has(path.extname(e.name).toLowerCase())) continue;
        try { handle(e.name, gamecrypt.decode(ar.read(e)), rel); }
        catch (err) { stats.failed++; }
      }
    } else {
      try { handle(it.name, fs.readFileSync(p), rel); }
      catch (err) { stats.failed++; }
    }
  }
})(RAN);

console.log('by detected type:');
for (const [k, n] of [...byKind.entries()].sort((a, b) => b[1] - a[1])) {
  console.log(`${String(n).padStart(8)}  ${k}`);
}
if (failures.length) {
  console.log('\nfailures:');
  for (const f of failures) console.log(`  ! ${f}`);
}
console.log(`\n${stats.seen} textures: ${stats.converted} converted, ` +
            `${stats.copied} already portable, ${stats.skipped} passed through, ` +
            `${stats.failed} failed`);
console.log(`${(stats.inBytes / 1048576).toFixed(1)}M in -> ` +
            `${(stats.outBytes / 1048576).toFixed(1)}M out (RGBA8 PNG, ` +
            `no mips) in ${((Date.now() - t0) / 1000).toFixed(1)}s`);
if (dry) console.log('(dry run — nothing written)');
