'use strict';
//
// Asset census over the deploy tree: what file types exist, how many, how big,
// and whether they ship loose or inside an .rcc.
//
// Phase 2 needs converters, and which converters is a question about the actual
// shipping data — not about what the engine can theoretically load. Same
// measure-first approach as client/census.js.
//
//   node census.js            top types by size
//   node census.js --all      every type
//   node census.js --where x  which directories hold a given extension
//
const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc');

const RAN = path.join(__dirname, '..', '..', '..', 'Ran');
const argv = process.argv.slice(2);
const showAll = argv.includes('--all');
const whereIdx = argv.indexOf('--where');
const whereExt = whereIdx === -1 ? null
  : ('.' + String(argv[whereIdx + 1]).replace(/^\./, '')).toLowerCase();

/** @type {Map<string,{count:number,bytes:number,loose:number,packed:number,dirs:Map<string,number>}>} */
const types = new Map();

function record(ext, bytes, packed, dir) {
  const key = (ext || '(none)').toLowerCase();
  let t = types.get(key);
  if (!t) {
    t = { count: 0, bytes: 0, loose: 0, packed: 0, dirs: new Map() };
    types.set(key, t);
  }
  t.count++;
  t.bytes += bytes;
  if (packed) t.packed++; else t.loose++;
  t.dirs.set(dir, (t.dirs.get(dir) || 0) + 1);
}

(function walk(dir) {
  let items;
  try { items = fs.readdirSync(dir, { withFileTypes: true }); } catch { return; }
  for (const it of items) {
    const p = path.join(dir, it.name);
    if (it.isDirectory()) { walk(p); continue; }

    const rel = path.relative(RAN, dir) || '.';
    if (it.name.toLowerCase().endsWith('.rcc')) {
      // Count what is INSIDE the archive, not the archive itself — the
      // archive is packaging, the entries are the assets.
      let ar;
      try { ar = new RccArchive(p); } catch { continue; }
      for (const e of ar.entries) record(path.extname(e.name), e.size, true, rel);
    } else {
      let size = 0;
      try { size = fs.statSync(p).size; } catch { /* ignore */ }
      record(path.extname(it.name), size, false, rel);
    }
  }
})(RAN);

if (whereExt) {
  const t = types.get(whereExt);
  if (!t) { console.error(`no files with extension ${whereExt}`); process.exit(1); }
  console.log(`${whereExt}: ${t.count} files, ${(t.bytes / 1048576).toFixed(1)}M\n`);
  for (const [d, n] of [...t.dirs.entries()].sort((a, b) => b[1] - a[1])) {
    console.log(`${String(n).padStart(7)}  ${d}`);
  }
  process.exit(0);
}

const rows = [...types.entries()].sort((a, b) => b[1].bytes - a[1].bytes);
const shown = showAll ? rows : rows.slice(0, 24);

console.log('   count         MB  packaging   ext');
console.log('--------  ---------  ----------  --------');
for (const [ext, t] of shown) {
  const pack = t.packed && t.loose ? 'both'
             : t.packed ? 'rcc' : 'loose';
  console.log(`${String(t.count).padStart(8)}  ` +
              `${(t.bytes / 1048576).toFixed(1).padStart(9)}  ` +
              `${pack.padEnd(10)}  ${ext}`);
}

const totalN = rows.reduce((s, [, t]) => s + t.count, 0);
const totalB = rows.reduce((s, [, t]) => s + t.bytes, 0);
if (!showAll && rows.length > shown.length) {
  console.log(`... and ${rows.length - shown.length} more types (--all)`);
}
console.log(`\n${totalN} files, ${(totalB / 1073741824).toFixed(2)} GB, ` +
            `${rows.length} distinct extensions`);
