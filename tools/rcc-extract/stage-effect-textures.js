'use strict';
//
// Stage the textures the simplified effect player needs, as PNGs under
// Resources/UI/effect/, loaded at runtime by
//   Resources.Load<Texture2D>("UI/effect/<stem>").
//
//   node extract-effects-json.js       # first: writes effect-textures.list
//   node stage-effect-textures.js      # then: stage those textures
//   node stage-effect-textures.js --dry
//
// The list is the distinct textures named by the SEQUENCE and (sprite)
// PARTICLESYS layers that effects.json actually contains — see
// extract-effects-json.js. Each name is a STEM (no extension): the engine loads
// through D3DX, which sniffs the file header and ignores the extension, so a
// layer naming `eff08.tga` resolves against `eff08.dds` on disk. Resolution and
// decode both go by CONTENT here, the same rule convert-textures.js follows.
//
// DDS and TGA are decoded (top mip) and re-encoded to PNG; an already-PNG source
// is copied. BMP/PSD/other are reported as unstaged rather than written through
// an untested decoder — honest gap over silent corruption.
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const dds = require('./dds.js');
const tga = require('./tga.js');
const png = require('./png.js');

const base = path.resolve(__dirname, '../../..');
const RAN = path.join(base, 'Ran');
const TEX_ROOT = path.join(RAN, 'textures');
const OUT_DIR = path.join(base,
  'MOBILE/unity/RanMobile/Assets/Ran/Resources/UI/effect');
const LIST = path.join(__dirname, 'effect-textures.list');

function sniff(buf) {
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

const TEXTURE_EXT = new Set(['.dds', '.tga', '.png', '.bmp', '.jpg', '.jpeg', '.psd']);

// Index every shipped loose texture by stem. `textures/effect` wins ties: an
// effect's `flare.tga` should resolve to the effect folder's copy, not a
// same-named map texture.
function buildIndex() {
  const byStem = new Map();
  (function walk(dir, inEffect) {
    let items;
    try { items = fs.readdirSync(dir, { withFileTypes: true }); } catch { return; }
    for (const it of items) {
      const p = path.join(dir, it.name);
      if (it.isDirectory()) { walk(p, inEffect || it.name.toLowerCase() === 'effect'); continue; }
      const ext = path.extname(it.name).toLowerCase();
      if (!TEXTURE_EXT.has(ext)) continue;
      const stem = it.name.toLowerCase().replace(/\.[^.]*$/, '');
      const prev = byStem.get(stem);
      // Prefer the effect subtree; otherwise first-seen wins.
      if (!prev || (inEffect && !prev.inEffect)) byStem.set(stem, { path: p, inEffect });
    }
  })(TEX_ROOT, false);
  return byStem;
}

function metaFor(guid) {
  // Mipmapped sRGB 2D texture with alphaIsTransparency, matching how the HUD
  // atlases are imported (stage-ui-atlases.js). Stable GUID keeps prefab/scene
  // references intact across a fresh checkout.
  return `fileFormatVersion: 2
guid: ${guid}
TextureImporter:
  internalIDToNameTable: []
  externalObjects: {}
  serializedVersion: 13
  mipmaps:
    mipMapMode: 0
    enableMipMap: 1
    sRGBTexture: 1
    linearTexture: 0
    fadeOut: 0
    borderMipMap: 0
    mipMapsPreserveCoverage: 0
    alphaTestReferenceValue: 0.5
    mipMapFadeDistanceStart: 1
    mipMapFadeDistanceEnd: 3
  bumpmap:
    convertToNormalMap: 0
    externalNormalMap: 0
    heightScale: 0.25
    normalMapFilter: 0
    flipGreenChannel: 0
  isReadable: 0
  streamingMipmaps: 0
  streamingMipmapsPriority: 0
  vTOnly: 0
  ignoreMipmapLimit: 0
  grayScaleToAlpha: 0
  generateCubemap: 6
  cubemapConvolution: 0
  seamlessCubemap: 0
  textureFormat: 1
  maxTextureSize: 2048
  textureSettings:
    serializedVersion: 2
    filterMode: 1
    aniso: 1
    mipBias: 0
    wrapU: 1
    wrapV: 1
    wrapW: 0
  nPOTScale: 1
  lightmap: 0
  compressionQuality: 50
  spriteMode: 0
  spriteExtrude: 1
  spriteMeshType: 1
  alignment: 0
  spritePivot: {x: 0.5, y: 0.5}
  spritePixelsToUnits: 100
  spriteBorder: {x: 0, y: 0, z: 0, w: 0}
  spriteGenerateFallbackPhysicsShape: 1
  alphaUsage: 1
  alphaIsTransparency: 1
  spriteTessellationDetail: -1
  textureType: 0
  textureShape: 1
  singleChannelComponent: 0
  flipbookRows: 1
  flipbookColumns: 1
  maxTextureSizeSet: 0
  compressionQualitySet: 0
  textureFormatSet: 0
  ignorePngGamma: 0
  applyGammaDecoding: 0
  swizzle: 50462976
  cookieLightType: 0
  platformSettings:
  - serializedVersion: 4
    buildTarget: DefaultTexturePlatform
    maxTextureSize: 2048
    resizeAlgorithm: 0
    textureFormat: -1
    textureCompression: 1
    compressionQuality: 50
    crunchedCompression: 0
    allowsAlphaSplitting: 0
    overridden: 0
    ignorePlatformSupport: 0
    androidETC2FallbackOverride: 0
    forceMaximumCompressionQuality_BC6H_BC7: 0
  spriteSheet:
    serializedVersion: 2
    sprites: []
    outline: []
    customData:
    physicsShape: []
    bones: []
    spriteID:
    internalID: 0
    vertices: []
    indices:
    edges: []
    weights: []
    secondaryTextures: []
    spriteCustomMetadata:
      entries: []
    nameFileIdTable: {}
  mipmapLimitGroupName:
  pSDRemoveMatte: 0
  userData:
  assetBundleName:
  assetBundleVariant:
`;
}

// Minimal uncompressed BMP (BI_RGB, 24/32-bit) -> RGBA8. RAN effect meshes name
// a handful of .bmp diffuse textures — `1d_lighting.bmp` alone is the diffuse of
// 4,642 MESH layers — and without this they stage as nothing and the mesh quad
// draws untextured. 24-bit BMP has no alpha channel; alpha is DERIVED FROM
// LUMINANCE so the black backgrounds these additive glow textures are authored
// on stay transparent (the engine draws them with SrcAlpha/ONE — DxEffectMesh.cpp
// blend cases 0-4 — where black adds nothing). Stated approximation, not invention.
function decodeBmp(buf) {
  if (buf.length < 54 || buf.toString('latin1', 0, 2) !== 'BM') throw new Error('not BMP');
  const dataOff = buf.readUInt32LE(10);
  const w = buf.readInt32LE(18);
  let h = buf.readInt32LE(22);
  const bpp = buf.readUInt16LE(28);
  const compression = buf.readUInt32LE(30);
  const hdrSize = buf.readUInt32LE(14);
  if (compression !== 0) throw new Error(`BMP compression ${compression} unsupported`);
  if (bpp !== 8 && bpp !== 24 && bpp !== 32) throw new Error(`BMP ${bpp}bpp unsupported`);
  const topDown = h < 0;
  h = Math.abs(h);
  if (w <= 0 || h <= 0 || w > 8192 || h > 8192) throw new Error(`BMP size ${w}x${h}`);
  // 8-bit: read the BGRX palette that follows the info header.
  let pal = null;
  if (bpp === 8) {
    const palOff = 14 + hdrSize;
    let clrUsed = buf.readUInt32LE(46);
    if (clrUsed === 0) clrUsed = 256;
    pal = [];
    for (let i = 0; i < clrUsed; i++) {
      const o = palOff + i * 4;
      pal.push([buf[o + 2], buf[o + 1], buf[o]]);   // stored BGRX -> RGB
    }
  }
  const bytesPP = bpp / 8;
  const stride = ((w * bytesPP + 3) & ~3);
  const out = Buffer.alloc(w * h * 4);
  for (let y = 0; y < h; y++) {
    const srcRow = dataOff + (topDown ? y : (h - 1 - y)) * stride;
    for (let x = 0; x < w; x++) {
      const si = srcRow + x * bytesPP;
      if (si + bytesPP > buf.length) continue;
      let r, g, b;
      if (bpp === 8) { const c = pal[buf[si]] || [0, 0, 0]; r = c[0]; g = c[1]; b = c[2]; }
      else { b = buf[si]; g = buf[si + 1]; r = buf[si + 2]; }
      const di = (y * w + x) * 4;
      out[di] = r; out[di + 1] = g; out[di + 2] = b;
      // 32-bit BMP carries alpha; 8/24-bit derive it from luminance (see note).
      out[di + 3] = bpp === 32 ? buf[si + 3]
        : Math.round((r * 0.299 + g * 0.587 + b * 0.114));
    }
  }
  return { width: w, height: h, data: out };
}

function toPng(buf, kind) {
  if (kind === 'DDS') { const i = dds.decode(buf); return png.encode(i.width, i.height, i.data); }
  if (kind === 'TGA') { const i = tga.decode(buf); return png.encode(i.width, i.height, i.data); }
  if (kind === 'BMP') { const i = decodeBmp(buf); return png.encode(i.width, i.height, i.data); }
  if (kind === 'PNG') return buf;
  return null;   // PSD/JPEG/unknown: not handled, reported as unstaged
}

function run(dry) {
  if (!fs.existsSync(LIST)) {
    throw new Error('effect-textures.list missing — run `node extract-effects-json.js` first');
  }
  const names = fs.readFileSync(LIST, 'latin1').split('\n').map((s) => s.trim()).filter(Boolean);
  const index = buildIndex();
  if (!dry) fs.mkdirSync(OUT_DIR, { recursive: true });

  const stat = { want: names.length, staged: 0, present: 0, missing: 0,
                 unhandled: 0, failed: 0, inBytes: 0, outBytes: 0 };
  const byKind = new Map();
  const missing = [];
  const unhandled = [];

  for (const stem of names) {
    const hit = index.get(stem);
    if (!hit) { stat.missing++; missing.push(stem); continue; }
    const outPng = path.join(OUT_DIR, stem + '.png');
    if (fs.existsSync(outPng)) { stat.present++; continue; }

    let buf;
    try { buf = fs.readFileSync(hit.path); } catch { stat.failed++; continue; }
    stat.inBytes += buf.length;
    const kind = sniff(buf);
    byKind.set(kind, (byKind.get(kind) || 0) + 1);
    let out;
    try { out = toPng(buf, kind); }
    catch (e) { stat.failed++; continue; }
    if (!out) { stat.unhandled++; unhandled.push(`${stem}(${kind})`); continue; }
    stat.outBytes += out.length;
    if (!dry) {
      fs.writeFileSync(outPng, out);
      const guid = crypto.createHash('md5')
        .update('Assets/Ran/Resources/UI/effect/' + stem + '.png').digest('hex').slice(0, 32);
      fs.writeFileSync(outPng + '.meta', metaFor(guid));
    }
    stat.staged++;
  }
  return { stat, byKind, missing, unhandled };
}

if (require.main === module) {
  const dry = process.argv.includes('--dry');
  const { stat, byKind, missing, unhandled } = run(dry);
  const fmt = (n) => n.toLocaleString('en-US');
  console.log('== stage-effect-textures ==');
  console.log(`referenced textures     ${fmt(stat.want)}`);
  console.log(`  staged (new PNG)      ${fmt(stat.staged)}`);
  console.log(`  already present       ${fmt(stat.present)}`);
  console.log(`  not shipped           ${fmt(stat.missing)}`);
  console.log(`  unhandled format      ${fmt(stat.unhandled)}${unhandled.length ? '  ' + unhandled.slice(0, 8).join(' ') : ''}`);
  console.log(`  decode failed         ${fmt(stat.failed)}`);
  console.log(`source bytes            ${(stat.inBytes / 1048576).toFixed(2)} MB (DXT/TGA)`);
  console.log(`PNG bytes written       ${(stat.outBytes / 1048576).toFixed(2)} MB`);
  console.log(`by source kind          ${[...byKind].map(([k, n]) => `${k}:${n}`).join('  ')}`);
  if (missing.length) console.log(`missing (first 15)      ${missing.slice(0, 15).join(' ')}`);
  if (dry) console.log('\n--dry: nothing written');
}

module.exports = { run, buildIndex, sniff, toPng };
