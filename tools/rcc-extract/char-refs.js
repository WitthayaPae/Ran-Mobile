'use strict';
//
// Which `.effskin*` files shipped content actually names, and where from.
// Same method (and same caveats) as effect-refs.js: this is a STRING SCAN over
// decoded bytes, so it is a floor on usage, not a measurement of it.
//
//   node char-refs.js
//
const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc.js');
const bytecrypt = require('./bytecrypt.js');
const gamecrypt = require('./gamecrypt.js');

const RAN = path.resolve(__dirname, '../../../Ran');

const TOKEN = /[A-Za-z0-9_\-.]{1,64}\.effskin(_a(_before)?)?/g;

// Byte-crypt tables to try on each source, per CRYPT-MAP.md. A per-byte
// substitution is stateless, so trying a table costs nothing but a pass.
const TRY_TABLES = [null, 'EMBYTECRYPT_ITEM', 'EMBYTECRYPT_PIECE',
                    'EMBYTECRYPT_EGP', 'EMBYTECRYPT_CONTAINER'];

function scan(buf, into, src) {
  // Layer 2: most of data/glogic is AES-256-ECB under a version prefix.
  // `default.charclass` — which holds strHALFALPHA_EFFECT and strCLASS_EFFECT —
  // yields nothing raw, so skipping this loses the class/status effect names
  // entirely.
  try { if (gamecrypt.isEncoded(buf)) buf = gamecrypt.decode(buf); } catch (e) { /* not encoded */ }
  for (const table of TRY_TABLES) {
    let b = buf;
    if (table) {
      try { b = bytecrypt.decode(Buffer.from(buf), table, 0); }
      catch (e) { continue; }
    }
    const s = b.toString('latin1');
    let m;
    TOKEN.lastIndex = 0;
    while ((m = TOKEN.exec(s))) {
      const name = m[0].toLowerCase();
      if (!into.has(name)) into.set(name, new Set());
      into.get(name).add(src);
    }
  }
}

const found = new Map();
const perSource = {};

function scanArchive(rel, label) {
  const p = path.join(RAN, rel);
  if (!fs.existsSync(p)) { console.log(`  (missing) ${rel}`); return; }
  const arc = new RccArchive(p);
  const before = found.size;
  for (const e of arc.entries) {
    let buf;
    try { buf = arc.read(e); } catch (err) { continue; }
    scan(buf, found, label);
  }
  perSource[label] = { entries: arc.entries.length, newNames: found.size - before };
}

function scanLoose(rel, label) {
  const dir = path.join(RAN, rel);
  if (!fs.existsSync(dir)) { console.log(`  (missing) ${rel}`); return; }
  const before = found.size;
  let n = 0;
  const walk = (d) => {
    for (const ent of fs.readdirSync(d, { withFileTypes: true })) {
      const p = path.join(d, ent.name);
      if (ent.isDirectory()) walk(p);
      else { n++; scan(fs.readFileSync(p), found, label); }
    }
  };
  walk(dir);
  perSource[label] = { entries: n, newNames: found.size - before };
}

scanArchive('data/glogic/GLogic.rcc', 'GLogic.rcc');
scanArchive('data/skinobject/SkinObject.rcc', 'SkinObject.rcc');
scanArchive('data/map/Map.rcc', 'Map.rcc');
scanArchive('data/gui/Gui.rcc', 'Gui.rcc');
scanArchive('data/effect/Effect.rcc', 'Effect.rcc');
scanArchive('data/effect/char/EffectChar.rcc', 'EffectChar.rcc');
scanLoose('data/piece', 'data/piece');
scanLoose('data/skin', 'data/skin');

// What actually ships
const shipped = new Set(
  new RccArchive(path.join(RAN, 'data/effect/char/EffectChar.rcc'))
    .entries.map((e) => e.name.toLowerCase()));

const names = [...found.keys()].sort();
const resolved = names.filter((n) => shipped.has(n));
const dangling = names.filter((n) => !shipped.has(n));
const unref = [...shipped].filter((n) => !found.has(n)).sort();

console.log('\n=== .effskin* references in shipped data ===');
console.log('  source                 entries   new names');
for (const k of Object.keys(perSource)) {
  console.log(`  ${k.padEnd(22)} ${String(perSource[k].entries).padStart(7)}   ` +
              `${String(perSource[k].newNames).padStart(9)}`);
}
console.log(`\n  distinct names referenced : ${names.length}`);
console.log(`  ...that ship              : ${resolved.length} of ${shipped.size}`);
console.log(`  ...named but NOT shipped  : ${dangling.length}`);
console.log(`  shipped but never named   : ${unref.length}`);

console.log('\n  named but not shipped:');
console.log('    ' + dangling.join(' '));
console.log('\n  shipped but never named (review list, not a deletion list):');
for (let i = 0; i < unref.length; i += 6) {
  console.log('    ' + unref.slice(i, i + 6).join(' '));
}
