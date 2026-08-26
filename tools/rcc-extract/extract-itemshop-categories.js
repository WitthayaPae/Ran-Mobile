'use strict';
//
// Item-shop (cash mall) category id -> display name, as one JSON the runtime
// loads from Resources ("itemshop-categories"), consumed by RanItemMallModule.
//
//   node extract-itemshop-categories.js            # write .../Resources/itemshop-categories.json
//   node extract-itemshop-categories.js --out PATH  # write elsewhere
//   node extract-itemshop-categories.js --stdout    # print the names, write nothing
//
// WHY THIS EXISTS
// --------------
// The item-mall window shows a category cycler; without a name table it renders
// "Cat N". The PC builds those labels in CItemShopWindow::InitShop
// (ItemShopWindow.cpp:774/778) as ID2GAMEWORD("ITEMSHOP_MENU_BUTTON", i) for
// i in 0..ITEM_SHOP_MAX_CATEGORY-1 (=17; ItemShopWindow.h:68). So the names are
// NOT server data and NOT hand-fixed client strings — they are one WORD group in
// the client's own gameword table. MEASURED: gameword.xml Id="ITEMSHOP_MENU_BUTTON"
// carries exactly 17 <VALUE Lang="Common"> entries (index 0..16), Thai text, in
// UTF-8 (the XML is UTF-8; there is no CP874 byte-decode here — that path is only
// for the item.isf name blobs).
//
// The category id on a catalog row (GET_ITEMSHOP_FROMDB category field) is the
// SAME index space: the PC bins items into m_cInvenItemShop[i] by that id and
// SelectType(i) filters on it, so row.category == the ITEMSHOP_MENU_BUTTON index.
// Index 0 ("ไอเทมทั้งหมด" / all items) is the PC's "show all" tab; the phone port's
// "All" pseudo-category (-1) maps to it.
//
// Output shape mirrors gameword.json — { "ITEMSHOP_MENU_BUTTON": [ ... ] } — so
// the runtime reads it with the existing RanGameWord parser and Get(id, index),
// i.e. literally ID2GAMEWORD, rather than a second bespoke reader.
//
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { RccArchive } = require('./rcc');

const base = path.resolve(__dirname, '../../..');
const GUI = path.join(base, 'Ran/data/gui/Gui.rcc');
const DEFAULT_OUT = 'MOBILE/unity/RanMobile/Assets/Ran/Resources/itemshop-categories.json';

const WORD_ID = 'ITEMSHOP_MENU_BUTTON';
const MAX_CATEGORY = 17;   // ItemShopWindow.h:68 ITEM_SHOP_MAX_CATEGORY

function decodeEntities(s) {
  return s.replace(/&lt;/g, '<').replace(/&gt;/g, '>')
          .replace(/&quot;/g, '"').replace(/&apos;/g, "'")
          .replace(/&amp;/g, '&');
}

/** Pull the ITEMSHOP_MENU_BUTTON WORD group's Common values, ordered by Index. */
function parseCategories(xml) {
  const entryRe = /<(?:WORD|SENTENSE)\b[^>]*\bId="([^"]+)"[^>]*>([\s\S]*?)<\/(?:WORD|SENTENSE)>/g;
  const valRe = /<VALUE\b([^>]*)>([\s\S]*?)<\/VALUE>/g;
  let m;
  while ((m = entryRe.exec(xml)) !== null) {
    if (m[1] !== WORD_ID) continue;
    const body = m[2];
    const vals = [];
    let next = 0, v;
    while ((v = valRe.exec(body)) !== null) {
      const attrs = v[1];
      if (!/\bLang="Common"/.test(attrs)) continue;
      const idxM = /\bIndex="(\d+)"/.exec(attrs);
      const idx = idxM ? Number(idxM[1]) : next;
      vals[idx] = decodeEntities(v[2].trim());
      next = Math.max(next, idx + 1);
    }
    return vals;
  }
  return null;
}

function main() {
  const arc = new RccArchive(GUI);
  let entry = null;
  for (const e of arc.entries) if ((e.name || '').toLowerCase() === 'gameword.xml') { entry = e; break; }
  if (!entry) throw new Error('gameword.xml not found in Gui.rcc');
  const xml = arc.read(entry).toString('utf8');

  const names = parseCategories(xml);
  if (!names) throw new Error(`WORD group ${WORD_ID} not found in gameword.xml`);
  // The engine indexes 0..ITEM_SHOP_MAX_CATEGORY-1; refuse to ship a table that
  // under/over-counts, which would silently mislabel or drop a category.
  if (names.length !== MAX_CATEGORY) {
    throw new Error(`${WORD_ID} has ${names.length} entries, expected ${MAX_CATEGORY}`);
  }
  for (let i = 0; i < names.length; i++) {
    if (!names[i]) throw new Error(`${WORD_ID} index ${i} is empty`);
  }

  names.forEach((n, i) => console.log(`  ${i}: ${n}`));

  const out = { [WORD_ID]: names };
  const j = JSON.stringify(out);

  if (process.argv.includes('--stdout')) { console.log(`(json ${j.length} bytes, not written)`); return; }
  const outArg = process.argv.indexOf('--out');
  const outRel = outArg > 0 ? process.argv[outArg + 1] : DEFAULT_OUT;
  const outPath = path.join(base, outRel);
  fs.mkdirSync(path.dirname(outPath), { recursive: true });
  fs.writeFileSync(outPath, j);
  // Stable TextAsset GUID across fresh checkouts (mirrors extract-crowmodels.js).
  const metaPath = outPath + '.meta';
  if (!fs.existsSync(metaPath)) {
    const guid = crypto.createHash('md5')
      .update('Assets/Ran/Resources/' + path.basename(outPath)).digest('hex').slice(0, 32);
    fs.writeFileSync(metaPath,
      `fileFormatVersion: 2\nguid: ${guid}\nTextScriptImporter:\n` +
      `  externalObjects: {}\n  userData: \n  assetBundleName: \n  assetBundleVariant: \n`);
  }
  console.log(`wrote ${outRel} (${j.length} bytes, ${names.length} categories)`);
}

module.exports = { parseCategories, WORD_ID, MAX_CATEGORY };

if (require.main === module) main();
