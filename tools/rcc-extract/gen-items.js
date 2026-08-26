'use strict';
//
// Flatten the parsed item tables into something the Unity UI can read.
//
//   node gen-items.js --out MOBILE/assets/itemflat.json
//
// `itemdata.js` decodes 37,090 records byte-exact to EOF; this keeps only the
// fields an inventory screen needs and drops the rest. The full parse is 49 MB
// of source and most of it is combat maths the UI never touches.
//
// **Icons are addressed as a SHEET plus an index**, never as a filename:
// `strInventoryFile` names an image and `sICONID` selects a cell inside it. That
// is why no filename scan can recover item art, and why the flat file has to
// carry both halves.
const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc.js');
const itemdata = require('./itemdata.js');

const base = path.resolve(__dirname, '../../..');
const RAN = path.join(base, 'Ran');

function findIsf() {
  const out = [];
  (function scan(dir) {
    for (const it of fs.readdirSync(dir, { withFileTypes: true })) {
      const p = path.join(dir, it.name);
      if (it.isDirectory()) { scan(p); continue; }
      if (/^item1?\.isf$/i.test(it.name)) out.push({ loose: p, name: it.name });
      if (/\.rcc$/i.test(it.name)) {
        try {
          const arc = new RccArchive(p);
          for (const e of arc.entries)
            if (/^item1?\.isf$/i.test(path.basename(e.name)))
              out.push({ arc, entry: e, name: path.basename(e.name) });
        } catch { /* unreadable archive, counted by absence */ }
      }
    }
  })(RAN);
  return out;
}

const sources = findIsf();
console.log(`${sources.length} item table(s) found`);

const entries = [];
const stats = { records: 0, named: 0, withIcon: 0, files: 0 };
const sheets = new Map();

for (const src of sources) {
  let buf;
  try { buf = src.arc ? src.arc.read(src.entry) : fs.readFileSync(src.loose); }
  catch { continue; }

  let parsed;
  try { parsed = itemdata.parse(buf); }
  catch (e) { console.log(`  ! ${src.name}: ${e.message}`); continue; }
  stats.files++;

  for (const item of parsed.items) {
    stats.records++;
    const b = item.basic;
    if (!b) continue;
    if (b.name) stats.named++;
    if (b.inventoryFile) {
      stats.withIcon++;
      sheets.set(b.inventoryFile.toLowerCase(),
                 (sheets.get(b.inventoryFile.toLowerCase()) || 0) + 1);
    }
    entries.push({
      // nativeId PACKS the pair: sub << 16 | main. Confirmed against the record
      // name, which spells the same pair out — nativeId 196608 is 3 << 16 and
      // the name is IN_000_003. Treating nativeId as a scalar id works until two
      // items share a main and differ only in sub.
      m: (b.nativeId >>> 0) & 0xffff,
      s: (b.nativeId >>> 16) & 0xffff,
      // NOT a display name. "IN_000_003" is a lookup key into the item string
      // table; the human-readable name lives there. Carried so the UI can do the
      // lookup, and so a missing string table is visible rather than silent.
      n: b.name || '',
      t: b.itemType | 0,
      // Icon sheet plus cell, and iconId PACKS them the same way:
      // sheet << 16 | cell. Raw 262145 is sheet 4, cell 1. Using the raw value
      // as a cell index would address past the end of every sheet.
      f: b.inventoryFile || '',
      i: (b.iconId >>> 0) & 0xffff,
      p: (b.iconId >>> 16) & 0xffff,
      // Inventory footprint in grid cells. Measured: 37,087 of 37,090 items are
      // 1x1 and the other 3 are 0x0, so RAN's inventory is NOT a spatial
      // Diablo-style grid — it is uniform cells. Carried anyway because it is
      // in the record, but a UI can safely lay out a plain grid.
      w: b.invenX | 0,
      h: b.invenY | 0,
    });
  }
}

console.log(`${stats.files} table(s) parsed, ${stats.records.toLocaleString()} records`);
console.log(`${stats.named.toLocaleString()} named, ${stats.withIcon.toLocaleString()} carry an icon sheet`);
console.log(`${sheets.size} distinct icon sheets`);

const sizes = {};
for (const e of entries) sizes[`${e.w}x${e.h}`] = (sizes[`${e.w}x${e.h}`] || 0) + 1;
console.log('inventory footprints: ' +
  Object.entries(sizes).sort((a, b) => b[1] - a[1]).slice(0, 6)
    .map(([k, v]) => `${k}:${v}`).join('  '));

const outArg = process.argv.indexOf('--out');
if (outArg > 0) {
  const p = path.join(base, process.argv[outArg + 1]);
  fs.writeFileSync(p, JSON.stringify({ entries }));
  console.log(`wrote ${process.argv[outArg + 1]} ` +
              `(${(fs.statSync(p).size / 1048576).toFixed(1)} MB)`);
}
