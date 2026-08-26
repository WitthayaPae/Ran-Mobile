'use strict';
//
// Where the name fields actually sit inside each property body.
//
// A free regex over the body invents names (`re.tga`, `ing.dds`, `1.dds`)
// because a `char[256]` that was overwritten by a shorter string keeps the tail
// of the previous one after the NUL. Fixed fields are at fixed offsets, so the
// honest way to read them is to find the offset by measurement and then read
// only from there.
//
// This histograms the start offset of every NUL-delimited printable run that
// looks like a filename, per (type, node version), and prints the offsets that
// dominate. Those are the real fields.
//
const path = require('path');
const { RccArchive } = require('./rcc');
const E = require('./effect-egp');

const RAN = path.join(__dirname, '..', '..', '..', 'Ran');
const NAME_RE = /^[A-Za-z0-9_\-. \\/()]{3,}\.(dds|tga|png|bmp|jpg|x|chf|egp|wav|ogg)$/i;

const ar = new RccArchive(path.join(RAN, 'data/effect/Effect.rcc'));
const hist = new Map();     // "TYPE 0xVVVV" -> Map(offset -> {n, exts:Map})
let nodes = 0;

for (const e of ar.entries) {
  if (!e.name.toLowerCase().endsWith('.egp')) continue;
  const r = E.parse(ar.read(e), e.name);
  for (const n of r.nodes || []) {
    nodes++;
    const key = `${n.type} 0x${n.ver.toString(16).padStart(4, '0')}`;
    if (!hist.has(key)) hist.set(key, new Map());
    const h = hist.get(key);
    const b = n.body;
    let start = 0;
    for (let i = 0; i <= b.length; i++) {
      if (i === b.length || b[i] === 0) {
        if (i - start >= 3) {
          const s = b.toString('latin1', start, i);
          if (NAME_RE.test(s)) {
            if (!h.has(start)) h.set(start, { n: 0, exts: new Map(), sample: s });
            const c = h.get(start);
            c.n++;
            const ext = s.split('.').pop().toLowerCase();
            c.exts.set(ext, (c.exts.get(ext) || 0) + 1);
          }
        }
        start = i + 1;
      }
    }
  }
}

console.log(`${nodes} property bodies scanned\n`);
const keys = [...hist.keys()].sort();
for (const key of keys) {
  const h = hist.get(key);
  const rows = [...h.entries()].sort((a, b) => b[1].n - a[1].n);
  const total = rows.reduce((a, [, c]) => a + c.n, 0);
  if (!total) continue;
  console.log(`${key}`);
  for (const [off, c] of rows.slice(0, 6)) {
    const exts = [...c.exts.entries()].sort((a, b) => b[1] - a[1]).map(([x, n]) => `${x}:${n}`).join(' ');
    const tag = c.n >= 20 ? '' : '   (rare — likely tail debris in an overwritten field)';
    console.log(`  offset ${String(off).padStart(5)}  ${String(c.n).padStart(6)}  ${exts.padEnd(28)} e.g. ${JSON.stringify(c.sample).slice(0, 48)}${tag}`);
  }
}
