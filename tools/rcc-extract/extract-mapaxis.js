'use strict';
//
// Emit the per-map minimap AXIS table + stage the minimap images the PC large map
// blits. This is the data behind CLargeMapWindowImage::SetMapAxisInfo — the real
// rendered map picture (GLMapAxisInfo::GetMinMapTex) plus the authoritative world
// bounds (GetMapStartX/Y + GetMapSizeX/Y) — which this port had extracted NEITHER
// of, so the mobile large map drew a schematic grid instead of the map.
//
//   node extract-mapaxis.js                 # -> Resources/mapaxis.json + stage PNGs
//   node extract-mapaxis.js --list          # print id -> tex/bounds, write nothing
//   node extract-mapaxis.js --no-stage      # emit json only, skip the image staging
//   node extract-mapaxis.js --out PATH      # custom json path (repo-relative)
//
// TWO SOURCES, both from the SHIPPED Ran/ deploy tree (not CLIENT/):
//   1. Ran/data/map/Map.rcc          -> <lev-stem>.mmp   (axis text config)
//   2. Ran/textures/gui/mini/*.dds   -> the minimap picture named by MINIMAPNAME
//
// The map id/name/lev come from the already-emitted mapcatalog.json, so this keys
// exactly the way the runtime resolves a server spawn: SNATIVEID -> catalog id.
// The `.mmp` itself is keyed by the map's `.lev` basename — see mapaxis.js for why
// that (not the scene/.wld name) is the engine's key.
//
// Byte-exact where a struct is parsed: the `.mmp` is decoded field-for-field by
// mapaxis.readAxis (mirroring GLMapAxisInfo::LoadFile's getflag calls); the `.dds`
// top mip is decoded by the same dds.js/png.js the rest of the pipeline uses.
//
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { RccArchive } = require('./rcc');
const MA = require('./mapaxis');
const dds = require('./dds.js');
const tga = require('./tga.js');
const png = require('./png.js');

const base = path.resolve(__dirname, '..', '..', '..');
const RAN = path.join(base, 'Ran');
const MAP_RCC = path.join(RAN, 'data', 'map', 'Map.rcc');
const MINI_DIR = path.join(RAN, 'textures', 'gui', 'mini');
const CATALOG = path.join(base, 'MOBILE', 'unity', 'RanMobile', 'Assets', 'Ran',
                          'Resources', 'mapcatalog.json');
const DEFAULT_OUT = path.join('MOBILE', 'unity', 'RanMobile', 'Assets', 'Ran',
                              'Resources', 'mapaxis.json');
const PNG_DIR = path.join(base, 'MOBILE', 'unity', 'RanMobile', 'Assets', 'Ran',
                          'Resources', 'UI', 'minimap');

// ---------------------------------------------------------------------------
//  Minimap image source resolution — the engine sniffs headers through D3DX and
//  resolves case-insensitively, so mirror both. The one texture directory the
//  shipped minimaps live in is textures/gui/mini (measured: all 97 are there).
// ---------------------------------------------------------------------------
let _miniIndex = null;
function miniIndex() {
  if (_miniIndex) return _miniIndex;
  _miniIndex = { byFull: new Map(), byStem: new Map() };
  if (fs.existsSync(MINI_DIR)) {
    for (const f of fs.readdirSync(MINI_DIR)) {
      _miniIndex.byFull.set(f.toLowerCase(), f);
      _miniIndex.byStem.set(MA.stem(f), f);   // ext-agnostic, as D3DX does
    }
  }
  return _miniIndex;
}

/** Resolve a MINIMAPNAME to the actual file on disk, or null. */
function findMini(texName) {
  if (!texName) return null;
  const idx = miniIndex();
  const bn = texName.replace(/\\/g, '/').split('/').pop();
  return idx.byFull.get(bn.toLowerCase()) || idx.byStem.get(MA.stem(bn)) || null;
}

// A .meta for a staged PNG, identical import settings to stage-ui-atlases.js so a
// minimap sprite imports the same way (mipmapped sRGB 2D texture, alpha kept). The
// GUID is derived from the asset path so re-runs and fresh checkouts agree.
function pngMeta(guid) {
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
    wrapW: 1
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

/** Decode a texture buffer (DDS / TGA / PNG passthrough) to {width,height,data}. */
function decodeTexture(buf) {
  if (buf.toString('latin1', 0, 4) === 'DDS ') return dds.decode(buf);
  if (buf[0] === 0x89 && buf.toString('latin1', 1, 4) === 'PNG') return { png: buf };
  // TGA has no magic; try it last.
  try { return tga.decode(buf); } catch { return null; }
}

/**
 * Stage one minimap texture -> Resources/UI/minimap/<name>.png (+ .meta).
 * @returns {{status:string, png:string, bytes:number}}
 */
function stageImage(srcFile) {
  const outName = path.basename(srcFile, path.extname(srcFile)) + '.png';
  const outPng = path.join(PNG_DIR, outName);
  if (fs.existsSync(outPng)) {
    return { status: 'present', png: path.basename(outName, '.png'),
             bytes: fs.statSync(outPng).size };
  }
  const buf = fs.readFileSync(path.join(MINI_DIR, srcFile));
  let img;
  try { img = decodeTexture(buf); }
  catch (e) { return { status: 'decode-failed: ' + e.message, png: '', bytes: 0 }; }
  if (!img) return { status: 'not-an-image', png: '', bytes: 0 };

  fs.mkdirSync(PNG_DIR, { recursive: true });
  const bytes = img.png ? img.png : png.encode(img.width, img.height, img.data);
  fs.writeFileSync(outPng, bytes);
  const guid = crypto.createHash('md5')
    .update('Assets/Ran/Resources/UI/minimap/' + outName).digest('hex').slice(0, 32);
  fs.writeFileSync(outPng + '.meta', pngMeta(guid));
  return { status: img.png ? 'copied(png)' : `staged ${img.width}x${img.height}`,
           png: path.basename(outName, '.png'), bytes: bytes.length };
}

// ---------------------------------------------------------------------------
//  Build the table
// ---------------------------------------------------------------------------
function build({ stage = true } = {}) {
  const catalog = JSON.parse(fs.readFileSync(CATALOG, 'utf8'));
  const arc = new RccArchive(MAP_RCC);
  const mmpByName = new Map();
  for (const e of arc.entries)
    if (/\.mmp$/i.test(e.name)) mmpByName.set(e.name.toLowerCase(), e);

  const maps = [];
  const noMmp = [];           // catalog maps with no .mmp at all
  const noImage = [];         // .mmp present, but its MINIMAPNAME is not shipped
  const stagedByFile = new Map();   // srcFile -> stage result (dedupe distinct textures)

  for (const m of catalog.maps) {
    // The engine keys the .mmp on the map's .lev basename; fall back to the
    // scene/.wld name only if a lev-named one is absent (never observed, kept safe).
    const levStem = MA.stem(m.lev || m.scene);
    const entry = mmpByName.get(levStem + '.mmp') ||
                  mmpByName.get(MA.stem(m.scene) + '.mmp');
    if (!entry) { noMmp.push({ id: m.id, scene: m.scene, lev: m.lev }); continue; }

    const axis = MA.readAxis(arc.read(entry));
    const srcFile = findMini(axis.minMapTex);   // actual on-disk file, or null

    let pngName = '';
    if (srcFile) {
      if (stage) {
        if (!stagedByFile.has(srcFile)) stagedByFile.set(srcFile, stageImage(srcFile));
        pngName = stagedByFile.get(srcFile).png;
      } else {
        // Not staging: still resolve the resource name we WOULD produce.
        pngName = path.basename(srcFile, path.extname(srcFile));
      }
    } else {
      noImage.push({ id: m.id, scene: m.scene, tex: axis.minMapTex });
    }

    maps.push({
      id: m.id,
      scene: m.scene,
      name: m.name || '',
      mmp: entry.name,
      tex: axis.minMapTex,     // MINIMAPNAME verbatim
      png: pngName,            // Resources/UI/minimap/<png>, "" when image absent
      // world bounds — the authoritative PC values
      startX: axis.startX, startY: axis.startY,
      sizeX: axis.sizeX, sizeY: axis.sizeY,
      // atlas geometry (whole-texture for every shipped minimap)
      texW: axis.texW, texH: axis.texH,
      texPos: axis.texPos,
    });
  }

  return { catalog, maps, noMmp, noImage, stagedByFile };
}

// ---------------------------------------------------------------------------
//  CLI
// ---------------------------------------------------------------------------
function main() {
  const noStage = process.argv.includes('--no-stage') || process.argv.includes('--list');
  const { maps, noMmp, noImage, stagedByFile } = build({ stage: !noStage });

  const withImage = maps.filter((m) => m.png).length;
  console.log(`map-axis: ${maps.length} of ${maps.length + noMmp.length} catalog maps have a .mmp`);
  console.log(`  with minimap image: ${withImage}   without image: ${maps.length - withImage}`);

  if (!noStage) {
    let staged = 0, present = 0, failed = 0, totalBytes = 0;
    for (const r of stagedByFile.values()) {
      if (r.status.startsWith('staged') || r.status.startsWith('copied')) staged++;
      else if (r.status === 'present') present++;
      else failed++;
      totalBytes += r.bytes;
    }
    console.log(`  images: ${stagedByFile.size} distinct  (${staged} newly staged, ` +
                `${present} already present, ${failed} failed), ` +
                `${(totalBytes / 1048576).toFixed(2)} MB PNG`);
    for (const [f, r] of stagedByFile)
      if (r.png === '') console.log(`    FAILED ${f}: ${r.status}`);
  }

  if (noMmp.length)
    console.log(`\n  ${noMmp.length} catalog maps have NO .mmp (no axis/image):`);
  for (const x of noMmp) console.log(`    ${String(x.id).padStart(4)}  ${x.scene}  (lev=${x.lev})`);

  if (noImage.length)
    console.log(`\n  ${noImage.length} maps have a .mmp but its MINIMAPNAME is NOT shipped:`);
  for (const x of noImage) console.log(`    ${String(x.id).padStart(4)}  ${x.scene}  -> ${x.tex}`);

  if (process.argv.includes('--list')) {
    console.log('');
    for (const m of maps)
      console.log(`  ${String(m.id).padStart(4)}  ${m.scene.padEnd(22)} ` +
                  `${(m.png || '(no image)').padEnd(30)} ` +
                  `start(${m.startX},${m.startY}) size(${m.sizeX},${m.sizeY})`);
    return;
  }

  // Emit the runtime table.
  const out = {
    count: maps.length,
    withImage,
    maps: maps.map((m) => ({
      id: m.id, scene: m.scene, name: m.name,
      tex: m.tex, png: m.png,
      startX: m.startX, startY: m.startY, sizeX: m.sizeX, sizeY: m.sizeY,
      texW: m.texW, texH: m.texH,
    })),
  };
  const at = process.argv.indexOf('--out');
  const outRel = at > 0 ? process.argv[at + 1] : DEFAULT_OUT;
  const outAbs = path.isAbsolute(outRel) ? outRel : path.join(base, outRel);
  fs.mkdirSync(path.dirname(outAbs), { recursive: true });
  const j = JSON.stringify(out);
  fs.writeFileSync(outAbs, j);

  // Commit a stable .meta so the TextAsset GUID survives a fresh checkout.
  const metaPath = outAbs + '.meta';
  if (!fs.existsSync(metaPath)) {
    const guid = crypto.createHash('md5')
      .update('Assets/Ran/Resources/' + path.basename(outAbs)).digest('hex').slice(0, 32);
    fs.writeFileSync(metaPath,
      `fileFormatVersion: 2\nguid: ${guid}\nTextScriptImporter:\n` +
      `  externalObjects: {}\n  userData: \n  assetBundleName: \n  assetBundleVariant: \n`);
  }

  // Folder .meta for Resources/UI/minimap so the AssetDatabase has a stable GUID.
  if (!noStage) {
    const folderMeta = PNG_DIR + '.meta';
    if (!fs.existsSync(folderMeta)) {
      const guid = crypto.createHash('md5')
        .update('Assets/Ran/Resources/UI/minimap').digest('hex').slice(0, 32);
      fs.writeFileSync(folderMeta,
        `fileFormatVersion: 2\nguid: ${guid}\nfolderAsset: yes\nDefaultImporter:\n` +
        `  externalObjects: {}\n  userData: \n  assetBundleName: \n  assetBundleVariant: \n`);
    }
  }

  console.log(`\nwrote ${outRel} (${(j.length / 1024).toFixed(1)} KB, ${maps.length} maps)`);
}

module.exports = { build, findMini, stageImage };

if (require.main === module) main();
