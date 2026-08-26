'use strict';
//
// Stage the in-game HUD's texture atlases into the Unity project as PNGs.
//
//   node stage-ui-atlases.js            stage the persistent-HUD atlas set
//   node stage-ui-atlases.js A.dds B.dds   stage a specific list
//   node stage-ui-atlases.js --all      stage EVERY atlas uicfg.json references
//
// --all is what makes the windows draw. uicfg.json names 95 distinct atlases
// across its 1,746 textured controls, and for a long time only the three the
// login screen happened to need were staged, so ~700 controls resolved their
// rect and then found no pixels. Everything else was already extracted and
// converted -- it had simply never been copied to where Resources.Load looks.
//
// The persistent HUD's controls (BASIC_INFO_VIEW_*, BASIC_MINIMAP/MINIMAP_BACK,
// BASIC_QUICK_SKILL_SLOT, GAME_MENU, MENU_*, BASIC_CHAT_BOX and their overimages)
// are already in `uicfg.json` with their real rects and TEXTURE names — see
// extract-uicfg.js. What was missing is the *pixels*: the atlas each control
// samples must be present under Resources/UI as a PNG, loaded by
// `RanUiCfg.Sheet("<name>.dds")` -> `Resources.Load<Texture2D>("UI/<name>")`.
//
// Interface_Main.dds and CharInven.dds were already staged (the login screen
// needed them). The CP gauge (BASIC_INFO_VIEW_CP / _CP_OVERIMAGE) samples
// `charinven02.dds`, which lives loose at Ran/textures/gui and had never been
// staged, so the CP bar had no frame. This stages it (and any atlas named on the
// command line) the same dependency-free way convert-textures.js does: decode
// the top mip with dds.js, re-encode with png.js.
//
// Idempotent: an atlas already present under Resources/UI is left untouched, so
// the three login atlases keep their committed .meta / import settings.
const fs = require('fs');
const path = require('path');
const dds = require('./dds.js');
const png = require('./png.js');
const crypto = require('crypto');

const base = path.resolve(__dirname, '../../..');
const SRC_DIR = path.join(base, 'Ran/textures/gui');
const OUT_DIR = path.join(base,
  'MOBILE/unity/RanMobile/Assets/Ran/Resources/UI');

// Already-converted PNGs from convert-textures.js. Preferred over the loose DDS
// because it is the same decode this tool would redo, and because the UI atlases
// are scattered across textures/gui, textures/effect and textures/map -- SRC_DIR
// alone only ever covered the handful under gui/.
const CONV_ROOT = path.join(base, 'MOBILE/assets/textures');
const UICFG = path.join(base, 'MOBILE/assets/uicfg.json');

// The distinct atlases the persistent in-game HUD samples. Interface_Main and
// CharInven are usually already present (login); charinven02 is the CP gauge.
const HUD_ATLASES = ['Interface_Main.dds', 'CharInven.dds', 'charinven02.dds'];

// A .meta whose only per-file part is the guid, matching how the login atlases
// are imported (mipmapped sRGB 2D texture, alphaIsTransparency handled by the
// RanTexturePostprocessor at import). Without a committed .meta Unity would
// still generate one on import; committing it keeps the GUID stable so a scene
// or prefab referencing the sheet does not break on a fresh checkout.
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
    wrapU: 0
    wrapV: 0
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

// Find the loose source case-insensitively: the config names "charinven02.dds"
// but the file on disk is lowercased.
function findSource(name) {
  const want = name.toLowerCase();
  for (const f of fs.readdirSync(SRC_DIR)) {
    if (f.toLowerCase() === want) return path.join(SRC_DIR, f);
  }
  // Also accept an extension-agnostic match, the way D3DX sniffs headers.
  const bare = path.basename(want, path.extname(want));
  for (const f of fs.readdirSync(SRC_DIR)) {
    if (path.basename(f, path.extname(f)).toLowerCase() === bare) {
      return path.join(SRC_DIR, f);
    }
  }
  return null;
}

// Texture names the shipped GUI xml asks for that name no file which has ever
// existed. These are typos in the DATA — present in the stock EP9 xml and in
// every backup of it — so the PC client fails to load them too. Kept in step
// with RanUiCfg._aliases, which does the same correction at runtime; both are
// needed, because this one decides what gets STAGED and that one decides what
// gets LOADED.
const ALIASES = {
  'outgui_char': 'outgui_character',
  'outgui_character_lgaacter_lga': 'outgui_character_lga',
};

// Index every already-converted texture by bare name, once. The converted tree
// is large, so this is built lazily and only when --all or a gui/ miss needs it.
let _convIndex = null;
function convIndex() {
  if (_convIndex) return _convIndex;
  _convIndex = new Map();
  const walk = (p) => {
    let entries;
    try { entries = fs.readdirSync(p, { withFileTypes: true }); }
    catch (e) { return; }
    for (const e of entries) {
      const f = path.join(p, e.name);
      if (e.isDirectory()) { walk(f); continue; }
      if (!/\.png$/i.test(e.name)) continue;
      const bare = path.basename(e.name, path.extname(e.name)).toLowerCase();
      // First hit wins: convert-textures preserves the source tree, and a name
      // colliding across gui/ and effect/ is the same art in every case seen.
      if (!_convIndex.has(bare)) _convIndex.set(bare, f);
    }
  };
  walk(CONV_ROOT);
  return _convIndex;
}

function stage(atlas) {
  const bare = path.basename(atlas, path.extname(atlas)).toLowerCase();

  // A mistyped name is satisfied by staging its real target; the runtime alias
  // in RanUiCfg.Sheet redirects the lookup to the same file.
  const alias = ALIASES[bare];
  if (alias) {
    const r = stage(alias + path.extname(atlas));
    return { atlas, status: `alias -> ${alias} (${r.status})` };
  }

  const outName = path.basename(atlas, path.extname(atlas)) + '.png';
  const outPng = path.join(OUT_DIR, outName);
  if (fs.existsSync(outPng)) return { atlas, status: 'present' };

  // A converted PNG is a straight copy; no decode, no re-encode, no chance of
  // this tool's DDS reader disagreeing with the one that produced the rest.
  const conv = convIndex().get(bare);
  if (conv) {
    const img = fs.readFileSync(conv);
    fs.mkdirSync(OUT_DIR, { recursive: true });
    fs.writeFileSync(outPng, img);
    writeMeta(outPng, outName);
    return { atlas, status: `copied ${img.readUInt32BE(16)}x${img.readUInt32BE(20)}` };
  }

  const src = findSource(atlas);
  if (!src) return { atlas, status: 'source-missing' };

  const buf = fs.readFileSync(src);
  // Only DDS is handled here — every persistent-HUD atlas is DDS. A mislabeled
  // TGA/PNG would throw; that is reported rather than silently written wrong.
  let img;
  try { img = dds.decode(buf); }
  catch (e) { return { atlas, status: 'decode-failed: ' + e.message }; }
  if (!img) return { atlas, status: 'not-dds' };

  fs.mkdirSync(OUT_DIR, { recursive: true });
  fs.writeFileSync(outPng, png.encode(img.width, img.height, img.data));
  writeMeta(outPng, outName);
  return { atlas, status: `staged ${img.width}x${img.height} (${img.format})` };
}

// Stable GUID from the asset name so re-runs and fresh checkouts agree.
function writeMeta(outPng, outName) {
  if (fs.existsSync(outPng + '.meta')) return;
  const guid = crypto.createHash('md5')
    .update('Assets/Ran/Resources/UI/' + outName).digest('hex').slice(0, 32);
  fs.writeFileSync(outPng + '.meta', metaFor(guid));
}

/** Every distinct atlas named by a textured control in uicfg.json. */
function atlasesFromUiCfg() {
  const cfg = JSON.parse(fs.readFileSync(UICFG, 'utf8')).controls;
  const seen = new Map();               // lowercased name -> control count
  for (const id of Object.keys(cfg)) {
    const c = cfg[id];
    if (!c.tex || !(c.tw > 0)) continue;
    const k = c.tex.toLowerCase();
    seen.set(k, (seen.get(k) || 0) + 1);
  }
  // Busiest first, so a truncated run still staged the atlases that matter.
  return [...seen.entries()].sort((a, b) => b[1] - a[1]);
}

if (require.main === module) {
  const args = process.argv.slice(2);
  const all = args.includes('--all');
  const list = args.filter((a) => !a.startsWith('--'));

  if (all) {
    const wanted = atlasesFromUiCfg();
    let ok = 0, already = 0, missing = [], ctlOk = 0, ctlMissing = 0;
    for (const [atlas, uses] of wanted) {
      const r = stage(atlas);
      if (/source-missing|decode-failed|not-dds/.test(r.status)) {
        missing.push([atlas, uses, r.status]); ctlMissing += uses;
      } else if (r.status === 'present') { already++; ctlOk += uses; }
      else { ok++; ctlOk += uses; }
    }
    console.log(`atlases: ${ok} newly staged, ${already} already present, ` +
                `${missing.length} unresolved (of ${wanted.length})`);
    console.log(`controls: ${ctlOk} resolvable, ${ctlMissing} still without art`);
    if (missing.length) {
      console.log('unresolved:');
      for (const [a, uses, why] of missing) {
        console.log(`  ${a.padEnd(32)} ${String(uses).padStart(4)} controls  ${why}`);
      }
    }
  } else {
    for (const a of (list.length ? list : HUD_ATLASES)) {
      const r = stage(a);
      console.log(`  ${r.atlas.padEnd(24)} ${r.status}`);
    }
  }
}

module.exports = { stage, HUD_ATLASES, atlasesFromUiCfg };
