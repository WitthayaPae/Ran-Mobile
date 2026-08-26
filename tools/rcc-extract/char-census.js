'use strict';
//
// Census of EffectChar.rcc — the `.effskin*` corpus that DxEffect/Char consumes.
// Mirrors what effect-census.js does for the `.egp` corpus.
//
//   node char-census.js
//   node char-census.js --strings     also histogram the strings per type
//
const path = require('path');
const { RccArchive } = require('./rcc.js');
const eff = require('./char-effskin.js');

const RAN = path.resolve(__dirname, '../../../Ran');
const ARCHIVE = path.join(RAN, 'data/effect/char/EffectChar.rcc');

const wantStrings = process.argv.includes('--strings');

const arc = new RccArchive(ARCHIVE);
const files = arc.entries.slice().sort((a, b) => a.name.localeCompare(b.name));

const byExt = {};
const headerTypes = {};
const headerVers = {};
const typeCount = {};          // typeId -> blobs
const typeFiles = {};          // typeId -> Set(file)
const typeVers = {};           // "typeId/ver" -> count
const typeBody = {};           // typeId -> [body lengths]
const pieceCount = {};         // pieceId -> slots
const pieceTypes = {};         // pieceId -> Set(typeId)
const perFileBlobs = [];
const failures = [];
const sizeMismatch = [];
const notExact = [];
let totalBytes = 0, totalBlobs = 0, totalSlots = 0;
const typeStrings = {};

for (const e of files) {
  const ext = (e.name.match(/\.[^.]+$/) || ['(none)'])[0].toLowerCase();
  byExt[ext] = (byExt[ext] || 0) + 1;
  totalBytes += e.size;

  const buf = arc.read(e);
  let p;
  try {
    p = eff.parse(buf, e.name);
  } catch (err) {
    failures.push(`${e.name}: ${err.message}`);
    continue;
  }
  const h = p.header;
  headerTypes[h.type] = (headerTypes[h.type] || 0) + 1;
  headerVers['0x' + h.version.toString(16).padStart(4, '0')] =
    (headerVers['0x' + h.version.toString(16).padStart(4, '0')] || 0) + 1;
  if (!p.exactEof) notExact.push(`${e.name}: consumed ${p.consumed} of ${p.length}`);

  let n = 0;
  for (const s of p.slots) {
    totalSlots++;
    pieceCount[s.piece] = (pieceCount[s.piece] || 0) + 1;
    (pieceTypes[s.piece] || (pieceTypes[s.piece] = new Set()));
    for (const fx of s.effects) {
      n++; totalBlobs++;
      typeCount[fx.typeId] = (typeCount[fx.typeId] || 0) + 1;
      (typeFiles[fx.typeId] || (typeFiles[fx.typeId] = new Set())).add(e.name);
      const k = `${fx.typeId}/0x${fx.version.toString(16).padStart(4, '0')}`;
      typeVers[k] = (typeVers[k] || 0) + 1;
      (typeBody[fx.typeId] || (typeBody[fx.typeId] = [])).push(fx.bodyLength);
      pieceTypes[s.piece].add(fx.typeId);
      // declaredSize is the writer's sizeof(); the outer length is authority.
      if (fx.declaredSize !== fx.bodyLength) {
        sizeMismatch.push(
          `${e.name} type ${fx.typeId} v0x${fx.version.toString(16)}: ` +
          `declared ${fx.declaredSize}, actual ${fx.bodyLength}`);
      }
      if (wantStrings) {
        const set = typeStrings[fx.typeId] || (typeStrings[fx.typeId] = new Map());
        for (const s2 of eff.strings(fx.blob)) set.set(s2, (set.get(s2) || 0) + 1);
      }
    }
  }
  perFileBlobs.push({ name: e.name, n, bytes: e.size });
}

const pad = (s, w) => String(s).padStart(w);

console.log(`\n=== EffectChar.rcc — ${files.length} entries, ${totalBytes} bytes uncompressed`);
console.log('extensions:', JSON.stringify(byExt));
console.log('CSerialFile type strings:', JSON.stringify(headerTypes));
console.log('CSerialFile versions:', JSON.stringify(headerVers));
console.log(`parsed ${files.length - failures.length}/${files.length}; ` +
            `${notExact.length} not consumed to exact EOF; ${failures.length} failed`);
console.log(`${totalSlots} piece slots, ${totalBlobs} effect blobs`);

if (failures.length) { console.log('\nFAILURES:'); failures.forEach(f => console.log('  ' + f)); }
if (notExact.length) { console.log('\nNOT EXACT EOF:'); notExact.forEach(f => console.log('  ' + f)); }

console.log('\n--- effect types (by blob count) ---');
console.log('  id  name              blobs   files   body bytes (min/med/max)  versions');
const ids = Object.keys(typeCount).map(Number).sort((a, b) => typeCount[b] - typeCount[a]);
for (const id of ids) {
  const b = typeBody[id].slice().sort((x, y) => x - y);
  const med = b[Math.floor(b.length / 2)];
  const vers = Object.keys(typeVers).filter(k => k.startsWith(id + '/'))
    .map(k => `${k.split('/')[1]}:${typeVers[k]}`).join(' ');
  const flag = eff.NOT_SUPPORTED.has(id) ? '!' : (eff.INSTANTIABLE.has(id) ? ' ' : '?');
  console.log(`${flag} ${pad(id, 3)}  ${(eff.TYPE_NAMES[id] || '???').padEnd(16)} ` +
              `${pad(typeCount[id], 5)}  ${pad(typeFiles[id].size, 5)}   ` +
              `${pad(b[0], 6)}/${pad(med, 6)}/${pad(b[b.length - 1], 6)}      ${vers}`);
}
const unseen = Object.keys(eff.TYPE_NAMES).map(Number).filter(i => !typeCount[i]);
console.log('types never present in shipped data:',
            unseen.map(i => `${i} ${eff.TYPE_NAMES[i]}`).join(', '));

console.log('\n--- piece slots ---');
for (const p of Object.keys(pieceCount).map(Number).sort((a, b) => a - b)) {
  const ts = [...pieceTypes[p]].sort((a, b) => a - b)
    .map(t => eff.TYPE_NAMES[t] || t).join(',');
  console.log(`  ${pad(p, 2)} ${(eff.PIECE_NAMES[p] || '?').padEnd(16)} ` +
              `${pad(pieceCount[p], 4)} slots   ${ts}`);
}

console.log('\n--- declared-size vs actual ---');
console.log(`  ${sizeMismatch.length} of ${totalBlobs} blobs disagree`);
sizeMismatch.slice(0, 12).forEach(s => console.log('    ' + s));

console.log('\n--- blobs per file ---');
const bins = { 1: 0, 2: 0, '3-5': 0, '6-10': 0, '11-20': 0, '21+': 0, 0: 0 };
for (const f of perFileBlobs) {
  const n = f.n;
  if (n === 0) bins[0]++;
  else if (n === 1) bins[1]++;
  else if (n === 2) bins[2]++;
  else if (n <= 5) bins['3-5']++;
  else if (n <= 10) bins['6-10']++;
  else if (n <= 20) bins['11-20']++;
  else bins['21+']++;
}
console.log(' ', JSON.stringify(bins));
const top = perFileBlobs.slice().sort((a, b) => b.n - a.n).slice(0, 8);
console.log('  most blobs:', top.map(f => `${f.name}=${f.n}`).join('  '));

if (wantStrings) {
  console.log('\n--- strings per type ---');
  for (const id of ids) {
    const m = typeStrings[id];
    if (!m || !m.size) { console.log(`  ${eff.TYPE_NAMES[id]}: (none)`); continue; }
    const list = [...m.entries()].sort((a, b) => b[1] - a[1]);
    console.log(`  ${eff.TYPE_NAMES[id]} (${m.size} distinct):`);
    list.slice(0, 14).forEach(([s, c]) => console.log(`      ${pad(c, 4)}  ${s}`));
  }
}
