'use strict';
//
// Build the item DATABASE the mobile inventory/shop/tooltip UI needs, and stage
// the icon atlases it draws from.
//
//   node extract-itemdb.js                 emit itemdb.json + stage icon atlases
//   node extract-itemdb.js --no-icons      json only
//   node extract-itemdb.js --out PATH      override the json path
//
// Pipeline (all measured; see itemdb.js for the source cites):
//   item.isf / item1.isf  --itemdb.parse-->  SBASIC (name/desc KEYS, icon, type,
//                                             grade, dwBuyPrice/dwSellPrice)
//                                             + SDRUG (wPileNum stack cap,
//                                             wCureVolume, emDrug)
//   ItemStrTable.txt      --loadStringTable-> KEY -> raw cp874 display string
//   sICONID               -->                icon (atlas file, grid col/row)
//
// `sp` (dwSellPrice) is the PER-UNIT base `SITEM::GETSELLPRICE` (GLItem.h:135-146)
// returns before the player's live sale rate is applied — that rate depends on
// PK state and the current map's commission (GLCharacter::GetSaleRate,
// GLCharacter.cpp:532-542), neither of which is DB data, so it is applied at
// display time in RanShop/RanItemDb, not baked in here. `cv` (wCureVolume) is
// GETAPPLYNUM's divisor (GLItem.cpp:717-746) for ammo-like item types when
// GETSELLPRICE prorates by held quantity; 0 means "not one of those types",
// i.e. divisor 1.
//
// The record's `strName`/`strComment` are LOOKUP KEYS ("IN_mmm_sss" /
// "ID_mmm_sss"), not display text — `SITEM::GetName`/`GetComment` resolve them
// through the item string table, falling back to the raw key when absent. The
// value bytes are kept as RAW cp874 (latin1 1:1) so the runtime decodes them
// with `RanText.DecodeCp874`, reproducing the PC's code-page-874 rendering
// byte-for-byte. Icons are addressed as `strInventoryFile` (the atlas) plus a
// grid cell: `sICONID.wMainID` is the column and `sICONID.wSubID` the row, each
// `ICON_PIXEL` (35) px in a `512x256` sheet (SkillFunc.cpp:6 GetIconTexurePos,
// SkillFunc.h:8-12). The (mainId,subId) key is the wire pair
// RanInventory.Slot / RanShopPackets.Build{Buy,Sell} use.
//
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { RccArchive } = require('./rcc.js');
const itemdb = require('./itemdb.js');
const dds = require('./dds.js');
const tga = require('./tga.js');
const png = require('./png.js');

const base = path.resolve(__dirname, '../../..');
const RAN = path.join(base, 'Ran');
const OUT_JSON = path.join(base,
  'MOBILE/unity/RanMobile/Assets/Ran/Resources/itemdb.json');
const UI_DIR = path.join(base,
  'MOBILE/unity/RanMobile/Assets/Ran/Resources/UI');

// Icon-cell geometry, from SkillFunc.h / GetIconTexurePos (SkillFunc.cpp:6).
const ICON_TEXTURE_SIZE_X = 512;
const ICON_TEXTURE_SIZE_Y = 256;
const ICON_PIXEL = 35;

const argv = process.argv.slice(2);
const has = (f) => argv.includes(f);
const val = (f, d) => { const i = argv.indexOf(f); return i === -1 ? d : argv[i + 1]; };

// ---- locate the shipped item tables and string table -----------------------

// item.isf/item1.isf and ItemStrTable.txt live in GLogic.rcc; scan the deploy
// tree for it exactly as gen-items.js does (loose copies would be picked up too).
function collectSources() {
  const tables = [];      // { name, buf }
  let stringTable = null; // Map
  (function scan(dir) {
    for (const it of fs.readdirSync(dir, { withFileTypes: true })) {
      const p = path.join(dir, it.name);
      if (it.isDirectory()) { scan(p); continue; }
      if (/^item1?\.isf$/i.test(it.name)) {
        tables.push({ name: it.name.toLowerCase(), buf: fs.readFileSync(p) });
      }
      if (/\.rcc$/i.test(it.name)) {
        let arc;
        try { arc = new RccArchive(p); } catch { continue; }
        for (const e of arc.entries) {
          const bn = path.basename(e.name).toLowerCase();
          if (/^item1?\.isf$/.test(bn)) tables.push({ name: bn, buf: arc.read(e) });
          else if (bn === 'itemstrtable.txt' && !stringTable) {
            try { stringTable = itemdb.loadStringTable(arc.read(e)); } catch { /* keep null */ }
          }
        }
      }
    }
  })(RAN);
  // item.isf before item1.isf, so item1 (the extension) wins on merge.
  tables.sort((a, b) => a.name.localeCompare(b.name));
  return { tables, stringTable };
}

// ---- build the flat records ------------------------------------------------

function build() {
  const { tables, stringTable } = collectSources();
  if (!stringTable) {
    console.log('! ItemStrTable.txt not found — names would be raw keys; aborting.');
    process.exit(1);
  }
  console.log(`string table: ${stringTable.size.toLocaleString()} entries`);

  const byKey = new Map();           // (main<<16|sub) -> record
  const sheets = new Map();          // atlas name -> use count
  const rep = { files: 0, records: 0, withBasic: 0, named: 0, keyOnly: 0,
                described: 0, stackable: 0, priced: 0, eofExact: 0, oldBasic: 0 };

  for (const t of tables) {
    let parsed;
    try { parsed = itemdb.parse(t.buf); }
    catch (e) { console.log(`  ! ${t.name}: ${e.message}`); continue; }
    rep.files++;
    if (parsed.bytesRead === parsed.fileSize) rep.eofExact++;
    for (const [, n] of parsed.stats.oldBasicVersions) rep.oldBasic += n;
    console.log(`  ${t.name}: ${parsed.items.length.toLocaleString()} items, ` +
      `${parsed.bytesRead === parsed.fileSize ? 'EOF-exact' : 'NOT eof-exact'} ` +
      `(${parsed.bytesRead}/${parsed.fileSize}), stats ` +
      `bad=${parsed.stats.badBasic} old=${[...parsed.stats.oldBasicVersions.values()].reduce((a, b) => a + b, 0)}`);

    for (const item of parsed.items) {
      rep.records++;
      const b = item.basic;
      if (!b) continue;
      rep.withBasic++;

      const main = (b.nativeId >>> 0) & 0xffff;
      const sub = (b.nativeId >>> 16) & 0xffff;

      // GetName: table lookup, else the raw key (GLItem.cpp:773-777).
      const nameVal = stringTable.get(b.name);
      const name = (nameVal !== undefined && nameVal !== '') ? nameVal : b.name;
      if (nameVal !== undefined && nameVal !== '') rep.named++; else rep.keyOnly++;
      // GetComment: table lookup, else nothing (GLItem.cpp:781-785).
      const descVal = stringTable.get(b.comment);
      const desc = (descVal !== undefined) ? descVal : '';
      if (desc) rep.described++;

      const maxStack = (item.pile && item.pile > 0) ? item.pile : 1;
      if (maxStack > 1) rep.stackable++;
      if (b.sellPrice > 0) rep.priced++;

      if (b.inventoryFile) {
        const k = b.inventoryFile.toLowerCase();
        sheets.set(k, (sheets.get(k) || 0) + 1);
      }

      byKey.set((main << 16) | sub, {
        m: main, s: sub,
        n: name,                                   // raw cp874, decoded at runtime
        d: desc,                                   // raw cp874 (usually empty)
        t: b.itemType | 0,                         // EMITEM_TYPE
        g: b.emLevel | 0,                          // EMITEMLEVEL grade/rarity
        f: b.inventoryFile || '',                  // icon atlas file
        x: (b.iconId >>> 0) & 0xffff,              // icon column (sICONID.wMainID)
        y: (b.iconId >>> 16) & 0xffff,             // icon row    (sICONID.wSubID)
        k: maxStack | 0,                           // wPileNum stack cap
        dr: item.drug | 0,                         // sDrugOp.emDrug (EMITEM_DRUG) —
                                                    // GLCHARLOGIC::GET_REVIVE_ITEM
                                                    // tests ==11 (CALL_REVIVE) on a
                                                    // worn neck/ornament item
                                                    // (GLogixExPC.cpp:4287-4297)
        bp: b.buyPrice >>> 0,                      // dwBuyPrice  (NPC buys this FROM
                                                    // the player at this unit price)
        sp: b.sellPrice >>> 0,                     // dwSellPrice (SITEM::GETSELLPRICE's
                                                    // per-unit base — GLItemBasic.h:1197)
        cv: (item.cureVolume && item.cureVolume > 0) ? item.cureVolume | 0 : 0,
                                                    // sDrugOp.wCureVolume — GETAPPLYNUM's
                                                    // divisor for ammo-like types when
                                                    // GETSELLPRICE prorates by held qty
                                                    // (GLItem.cpp:717-746); 0 = not ammo,
                                                    // proration divisor is 1
        bi: item.instance ? 1 : 0,                 // sDrugOp.bInstance — SITEM::ISPILE()
                                                    // is bInstance && wPileNum>1
                                                    // (GLItem.h:115/125); GETSELLPRICE
                                                    // only prorates by held quantity when
                                                    // ISPILE() is true, otherwise it is
                                                    // the flat per-item sp regardless of
                                                    // how many are held
      });
    }
  }

  return { records: [...byKey.values()], sheets, rep };
}

// ---- icon atlas staging ----------------------------------------------------

// Same dependency-free path as convert-textures.js / stage-ui-atlases.js: decode
// the top mip, re-encode as PNG under Resources/UI. Format is sniffed by content
// because ~1.6% of files named .dds are actually TGA/PNG (README "Textures").
const ICON_DIRS = ['textures/item', 'textures/gui', 'textures'].map((d) => path.join(RAN, d));

function findSource(name) {
  const bare = name.replace(/\.[^.]*$/, '').toLowerCase();
  for (const dir of ICON_DIRS) {
    if (!fs.existsSync(dir)) continue;
    let exact = null, sniffed = null;
    for (const f of fs.readdirSync(dir)) {
      const fl = f.toLowerCase();
      if (fl === name.toLowerCase()) { exact = path.join(dir, f); break; }
      if (f.replace(/\.[^.]*$/, '').toLowerCase() === bare) sniffed = path.join(dir, f);
    }
    if (exact) return exact;
    if (sniffed) return sniffed;
  }
  return null;
}

function sniff(buf) {
  if (buf.length >= 4 && buf.toString('ascii', 0, 4) === 'DDS ') return 'dds';
  if (buf.length >= 8 && buf[0] === 0x89 && buf[1] === 0x50) return 'png';
  return 'tga';   // the D3DX fallthrough for the mislabeled minority
}

function metaFor(guid) {
  // Mirrors the committed login/HUD atlas import (mipmapped sRGB 2D + alpha),
  // GUID derived from the asset path so re-runs and fresh checkouts agree.
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
  isReadable: 0
  streamingMipmaps: 0
  grayScaleToAlpha: 0
  generateCubemap: 6
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
  spritePixelsToUnits: 100
  alphaUsage: 1
  alphaIsTransparency: 1
  textureType: 0
  textureShape: 1
  maxTextureSizeSet: 0
  compressionQualitySet: 0
  textureFormatSet: 0
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
    androidETC2FallbackOverride: 0
  userData:
  assetBundleName:
  assetBundleVariant:
`;
}

function stageIcons(sheets) {
  fs.mkdirSync(UI_DIR, { recursive: true });
  const r = { present: 0, staged: 0, bytes: 0, missing: [], failed: [], notImage: [] };
  for (const name of [...sheets.keys()].sort()) {
    if (/\.x$/i.test(name)) { r.notImage.push(name); continue; }  // a stray model ref
    const outName = path.basename(name, path.extname(name)) + '.png';
    const outPng = path.join(UI_DIR, outName);
    if (fs.existsSync(outPng)) { r.present++; continue; }

    const src = findSource(name);
    if (!src) { r.missing.push(name); continue; }
    const buf = fs.readFileSync(src);
    const kind = sniff(buf);
    let out;
    try {
      if (kind === 'png') { out = buf; }              // already portable, pass through
      else {
        const img = kind === 'dds' ? dds.decode(buf) : tga.decode(buf);
        if (!img) { r.notImage.push(name); continue; }
        out = png.encode(img.width, img.height, img.data);
      }
    } catch (e) { r.failed.push(`${name}: ${e.message}`); continue; }

    fs.writeFileSync(outPng, out);
    const guid = crypto.createHash('md5')
      .update('Assets/Ran/Resources/UI/' + outName).digest('hex').slice(0, 32);
    fs.writeFileSync(outPng + '.meta', metaFor(guid));
    r.staged++; r.bytes += out.length;
  }
  return r;
}

// ---- run -------------------------------------------------------------------

const { records, sheets, rep } = build();

console.log(`\n${rep.files} table(s), ${rep.records.toLocaleString()} records, ` +
  `${rep.withBasic.toLocaleString()} with SBASIC, ${rep.eofExact}/${rep.files} EOF-exact`);
console.log(`merged unique (mainId,subId): ${records.length.toLocaleString()}`);
console.log(`names resolved: ${rep.named.toLocaleString()}  ` +
  `key-only (no table entry): ${rep.keyOnly.toLocaleString()}  ` +
  `old SBASIC skipped: ${rep.oldBasic}`);
console.log(`descriptions (non-empty): ${rep.described.toLocaleString()}  ` +
  `stackable (maxStack>1): ${rep.stackable.toLocaleString()}  ` +
  `priced (sellPrice>0): ${rep.priced.toLocaleString()}`);
console.log(`distinct icon atlases: ${sheets.size}`);

const outPath = val('--out', OUT_JSON);
fs.writeFileSync(outPath, JSON.stringify({ items: records }));
console.log(`\nwrote ${path.relative(base, outPath)} ` +
  `(${(fs.statSync(outPath).size / 1048576).toFixed(2)} MB, ${records.length.toLocaleString()} items)`);

// Commit a stable .meta so a fresh checkout keeps the same GUID (a scene or
// loader referencing itemdb.json would otherwise break). TextScriptImporter,
// matching uicfg.json.meta.
const metaPath = outPath + '.meta';
if (!fs.existsSync(metaPath)) {
  const guid = crypto.createHash('md5')
    .update('Assets/Ran/Resources/itemdb.json').digest('hex').slice(0, 32);
  fs.writeFileSync(metaPath, `fileFormatVersion: 2
guid: ${guid}
TextScriptImporter:
  externalObjects: {}
  userData:
  assetBundleName:
  assetBundleVariant:
`);
  console.log(`wrote itemdb.json.meta (guid ${guid})`);
}

if (!has('--no-icons')) {
  console.log('\nstaging icon atlases into Resources/UI ...');
  const r = stageIcons(sheets);
  console.log(`  staged ${r.staged} (${(r.bytes / 1048576).toFixed(1)} MB PNG), ` +
    `already present ${r.present}, missing ${r.missing.length}, ` +
    `non-image refs ${r.notImage.length}, failed ${r.failed.length}`);
  if (r.missing.length) console.log('  missing: ' + r.missing.slice(0, 12).join(', ') +
    (r.missing.length > 12 ? ` … +${r.missing.length - 12}` : ''));
  if (r.failed.length) console.log('  failed: ' + r.failed.slice(0, 6).join(' | '));
}

module.exports = { build, findSource, sniff };
