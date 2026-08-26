'use strict';
//
// Stage the PK visual textures the mobile PK HUD needs, as PNGs under
// Resources/UI/, loaded at runtime by
//   Resources.Load<Texture2D>("UI/<stem>").
//
//   node stage-pk-textures.js            stage the PK set
//   node stage-pk-textures.js --dry      measure without writing
//
// The set:
//   * pk_combo_02..14.dds  — the 13 kill-streak banner tiers. The engine's
//     PK_COMBO_* clusters (uiinnercfg03.xml) map DOUBLE_KILL->pk_combo_02 ...
//     OWNAGE->pk_combo_14, and CPKComboDisplay::START picks the tier from the
//     combo count. See comboTierStem() below, which reproduces
//     CInnerInterface::SET_PK_COMBO (InnerInterfaceSimple.cpp:5988) exactly —
//     including its skip of MASTER_KILL (pk_combo_05).
//   * number.dds       — the DAMAGE_DISPLAY digit sheet (floating combat numbers).
//   * charinven03.dds   — a PK-window / rank chrome atlas named by the PK clusters.
//
// Every source is DDS (verified: header 'DDS '). Decoded top mip via dds.js,
// re-encoded to PNG via png.js — the same dependency-free path stage-ui-atlases.js
// and convert-textures.js follow. Resolution is case-insensitive and
// extension-agnostic (D3DX sniffs headers; the on-disk names are lowercased).
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const dds = require('./dds.js');
const png = require('./png.js');

const base = path.resolve(__dirname, '../../..');
const SRC_DIR = path.join(base, 'Ran/textures/gui');
const OUT_DIR = path.join(base,
  'MOBILE/unity/RanMobile/Assets/Ran/Resources/UI');

// The PK textures to stage. pk_combo_02..14 = the 13 banner tiers; the two
// atlases carry the damage digits and the PK-window chrome.
const PK_TEXTURES = [];
for (let n = 2; n <= 14; n++) PK_TEXTURES.push(`pk_combo_${String(n).padStart(2, '0')}.dds`);
PK_TEXTURES.push('number.dds', 'charinven03.dds');

// ---------------------------------------------------------------------------
// The combo-count -> banner-texture mapping, a faithful transcription of
// CInnerInterface::SET_PK_COMBO (InnerInterfaceSimple.cpp:5988) composed with
// the PK_COMBO_* cluster textures (uiinnercfg03.xml:5381+). Exported so the
// C# RanPkCombo can be cross-checked against it and test.js can pin it.
//
// SET_PK_COMBO maps the combo COUNT to a CPKComboDisplay tier id; the tier id
// (minus DOUBLE_KILL) indexes the keyword/texture list. The result:
//   count 2  -> DOUBLE_KILL   -> pk_combo_02
//   count 3  -> TRIPLE_KILL   -> pk_combo_03
//   count 4  -> QUARD_KILL    -> pk_combo_04
//   count 5  -> KILLING_SPREE -> pk_combo_06   (MASTER_KILL / pk_combo_05 SKIPPED)
//   count 6  -> DOMINATING    -> pk_combo_07
//   count 7  -> MEGA_KILL     -> pk_combo_08
//   count 8  -> UNSTOPPABLE   -> pk_combo_09
//   count 9  -> WICKED_SICK   -> pk_combo_10
//   count 10 -> MONSTER_KILL  -> pk_combo_11
//   count 11 -> GODLIKE       -> pk_combo_12
//   count 12 -> HOLY_SHIT     -> pk_combo_13
//   count>=13-> OWNAGE        -> pk_combo_14
// The MASTER_KILL skip is the PC's actual behaviour, reproduced verbatim; a
// count of 5 never shows pk_combo_05.
function comboTierStem(count) {
  let tex;
  if (count === 2)       tex = 2;
  else if (count === 3)  tex = 3;
  else if (count === 4)  tex = 4;
  else if (count === 5)  tex = 6;   // KILLING_SPREE (SET_PK_COMBO skips MASTER_KILL)
  else if (count === 6)  tex = 7;
  else if (count === 7)  tex = 8;
  else if (count === 8)  tex = 9;
  else if (count === 9)  tex = 10;
  else if (count === 10) tex = 11;
  else if (count === 11) tex = 12;
  else if (count === 12) tex = 13;
  else if (count >= 13)  tex = 14;
  else return null;                 // count < 2: no banner
  return `pk_combo_${String(tex).padStart(2, '0')}`;
}

// A .meta matching how the other Resources/UI atlases are imported (mipmapped
// sRGB 2D texture). Stable GUID from the asset path keeps prefab/scene refs
// intact across a fresh checkout.
function metaFor(guid) {
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

// Case-insensitive + extension-agnostic source lookup in Ran/textures/gui.
function findSource(name) {
  const want = name.toLowerCase();
  let files;
  try { files = fs.readdirSync(SRC_DIR); } catch { return null; }
  for (const f of files) if (f.toLowerCase() === want) return path.join(SRC_DIR, f);
  const bare = path.basename(want, path.extname(want));
  for (const f of files) {
    if (path.basename(f, path.extname(f)).toLowerCase() === bare) return path.join(SRC_DIR, f);
  }
  return null;
}

function stage(atlas, dry) {
  const outName = path.basename(atlas, path.extname(atlas)) + '.png';
  const outPng = path.join(OUT_DIR, outName);

  const src = findSource(atlas);
  if (!src) return { atlas, status: 'source-missing', ok: false };

  let img;
  try { img = dds.decode(fs.readFileSync(src)); }
  catch (e) { return { atlas, status: 'decode-failed: ' + e.message, ok: false }; }
  if (!img) return { atlas, status: 'not-dds', ok: false };

  if (!dry) {
    fs.mkdirSync(OUT_DIR, { recursive: true });
    fs.writeFileSync(outPng, png.encode(img.width, img.height, img.data));
    const guid = crypto.createHash('md5')
      .update('Assets/Ran/Resources/UI/' + outName).digest('hex').slice(0, 32);
    fs.writeFileSync(outPng + '.meta', metaFor(guid));
  }
  return { atlas, status: `${dry ? 'ready' : 'staged'} ${img.width}x${img.height} (${img.format})`,
           ok: true, width: img.width, height: img.height, format: img.format };
}

function run(dry) {
  const results = PK_TEXTURES.map((a) => stage(a, dry));
  return results;
}

if (require.main === module) {
  const dry = process.argv.includes('--dry');
  console.log('== stage-pk-textures ==');
  const results = run(dry);
  for (const r of results) console.log(`  ${r.atlas.padEnd(20)} ${r.status}`);
  const ok = results.filter((r) => r.ok).length;
  const miss = results.filter((r) => !r.ok);
  console.log(`resolved ${ok}/${results.length}`);
  if (miss.length) console.log(`ABSENT/failed: ${miss.map((r) => r.atlas).join(' ')}`);
  if (dry) console.log('--dry: nothing written');
}

module.exports = { run, stage, comboTierStem, PK_TEXTURES, findSource };
