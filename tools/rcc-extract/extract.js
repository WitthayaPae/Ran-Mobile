'use strict';
//
// Extract RCC archives from the deploy tree.
//
//   node extract.js --list                     inventory every archive
//   node extract.js --list Gui                 entries in one archive
//   node extract.js --out ../../build/assets   extract everything
//   node extract.js --out DIR --only Gui,Map   extract selected archives
//   node extract.js --cat Gui gameextext.xml   dump one entry to stdout
//
// Sources from `Ran/` — the deploy tree — not `CLIENT/`. CLIENT/ is the dev
// tree and roughly half of its data never ships; extracting from it would
// convert backup directories.
//
const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc');
const gamecrypt = require('./gamecrypt');

const RAN = path.join(__dirname, '..', '..', '..', 'Ran');

function findArchives(root) {
  const out = [];
  (function walk(dir) {
    let items;
    try { items = fs.readdirSync(dir, { withFileTypes: true }); } catch { return; }
    for (const it of items) {
      const p = path.join(dir, it.name);
      if (it.isDirectory()) walk(p);
      else if (it.name.toLowerCase().endsWith('.rcc')) {
        out.push({ name: path.basename(it.name, path.extname(it.name)), path: p });
      }
    }
  })(root);
  return out.sort((a, b) => a.name.localeCompare(b.name));
}

const argv = process.argv.slice(2);
const has = (f) => argv.includes(f);
const val = (f, d) => { const i = argv.indexOf(f); return i === -1 ? d : argv[i + 1]; };

const archives = findArchives(RAN);
if (!archives.length) {
  console.error(`no .rcc archives under ${RAN}`);
  process.exit(1);
}

// --cat ARCHIVE ENTRY
if (has('--cat')) {
  const i = argv.indexOf('--cat');
  const a = archives.find((x) => x.name.toLowerCase() === String(argv[i + 1]).toLowerCase());
  if (!a) { console.error(`no archive named ${argv[i + 1]}`); process.exit(1); }
  const one = new RccArchive(a.path).read(argv[i + 2]);
  process.stdout.write(has('--raw') ? one : gamecrypt.decode(one));
  process.exit(0);
}

if (has('--list')) {
  const i = argv.indexOf('--list');
  const which = argv[i + 1] && !argv[i + 1].startsWith('--') ? argv[i + 1] : null;
  if (which) {
    const a = archives.find((x) => x.name.toLowerCase() === which.toLowerCase());
    if (!a) { console.error(`no archive named ${which}`); process.exit(1); }
    for (const n of new RccArchive(a.path).list()) console.log(n);
    process.exit(0);
  }
  let total = 0;
  let bytes = 0;
  console.log('entries       size  archive');
  console.log('-------  ---------  ------------------------------------');
  for (const a of archives) {
    const ar = new RccArchive(a.path);
    const raw = ar.entries.reduce((s, e) => s + e.size, 0);
    total += ar.length; bytes += raw;
    console.log(`${String(ar.length).padStart(7)}  ` +
                `${(raw / 1048576).toFixed(1).padStart(8)}M  ` +
                `${path.relative(RAN, a.path)}`);
  }
  console.log(`\n${archives.length} archives, ${total} entries, ` +
              `${(bytes / 1048576).toFixed(1)}M uncompressed`);
  process.exit(0);
}

const outDir = val('--out', null);
if (!outDir) {
  console.error('usage: node extract.js --list | --out DIR [--only A,B] | --cat A ENTRY');
  process.exit(2);
}
const only = val('--only', null);
const wanted = only ? new Set(only.split(',').map((s) => s.trim().toLowerCase())) : null;

const keepRaw = has('--raw');
let decoded = 0;
let files = 0;
let bytes = 0;
let failed = 0;
const t0 = Date.now();

for (const a of archives) {
  if (wanted && !wanted.has(a.name.toLowerCase())) continue;
  const ar = new RccArchive(a.path);
  // Mirror the archive's own directory so extracted trees stay navigable;
  // entries are flat bare filenames, so the archive location IS the grouping.
  const dest = path.join(outDir, path.relative(RAN, path.dirname(a.path)));
  fs.mkdirSync(dest, { recursive: true });

  for (const e of ar.entries) {
    try {
      // Second layer: most data/glogic/ files carry a 4-byte version prefix
      // and an AES-256-ECB payload underneath the RCC XOR. decode() is a
      // no-op on everything else, so this is safe to apply everywhere.
      const rccBytes = ar.read(e);
      let data = rccBytes;
      if (!keepRaw && gamecrypt.isEncoded(rccBytes)) {
        data = gamecrypt.decode(rccBytes);
        decoded++;
      }
      // Entry names are bare by construction, but never trust an archive to
      // stay inside the output directory.
      const safe = path.basename(e.name);
      fs.writeFileSync(path.join(dest, safe), data);
      files++; bytes += data.length;
    } catch (err) {
      failed++;
      console.error(`  ! ${a.name}/${e.name}: ${err.message}`);
    }
  }
  console.log(`${a.name.padEnd(14)} ${String(ar.length).padStart(6)} entries -> ` +
              path.relative(process.cwd(), dest));
}

console.log(`\n${files} files, ${(bytes / 1048576).toFixed(1)}M in ` +
            `${((Date.now() - t0) / 1000).toFixed(1)}s` +
            (decoded ? `, ${decoded} AES-decoded` : '') +
            (failed ? `, ${failed} FAILED` : ''));
process.exit(failed ? 1 : 0);
