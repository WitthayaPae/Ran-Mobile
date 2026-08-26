'use strict';
//
// Emit the field NAMES for each (effect node type, version), in the same order
// `extract-effects.js` writes their values.
//
//   node gen-effectfields.js --out MOBILE/assets/effectfields.json
//
// The `.reff` payload is three flat streams — floats, ints, names — because the
// layouts come from a compiler probe over the real headers and re-declaring 23
// structs in C# would duplicate that with no way to keep the copy honest. This
// closes the loop: one sidecar, generated from the same probe output, that says
// which stream index is which field.
//
// Order here MUST match the exporter's, which walks `specsFor(struct)` and
// appends by measured KIND. Both read the same function, so the only way they
// can diverge is if one is edited without the other — which the round-trip check
// at the end catches by rebuilding the index arithmetic and comparing totals.
const fs = require('fs');
const path = require('path');
const props = require('./effect-props.js');

const base = path.resolve(__dirname, '../../..');

const out = { note: 'field order per (type, version); indices are into the .reff streams', types: {} };
let totalFields = 0;

for (const key of Object.keys(props.LAYOUTS)) {
  const spec = props.LAYOUTS[key];
  const specs = props.specsFor(spec.struct) || [];
  let fi = 0, ii = 0, ni = 0;
  const fields = [];
  for (const f of specs) {
    const n = f.n || 1;
    if (f.kind === 'string') { fields.push({ name: f.name, stream: 'name', index: ni, count: 1 }); ni += 1; }
    else if (f.kind === 'f32') { fields.push({ name: f.name, stream: 'float', index: fi, count: n }); fi += n; }
    else { fields.push({ name: f.name, stream: 'int', index: ii, count: n }); ii += n; }
    totalFields++;
  }
  out.types[key] = {
    struct: spec.struct,
    affine: !!spec.affine,
    floats: fi, ints: ii, names: ni,
    fields,
  };
}

console.log(`${Object.keys(out.types).length} (type, version) layouts, ${totalFields} fields`);

// Cross-check against the written effects: the per-node counts in a `.reff` must
// equal what this table predicts for that node's (type, version). If the two
// disagree the sidecar would silently mis-name every field.
const EFFECTS = path.join(base, 'MOBILE/assets/effects');
const TYPE_NAMES = require('./effect-egp.js').TYPE_NAMES;
if (fs.existsSync(EFFECTS)) {
  let checked = 0, mismatch = 0, noLayout = 0;
  const files = fs.readdirSync(EFFECTS).filter((f) => f.endsWith('.reff'));
  for (const f of files) {
    const b = fs.readFileSync(path.join(EFFECTS, f));
    if (b.toString('latin1', 0, 4) !== 'REFF') continue;
    const nc = b.readUInt32LE(8);
    const sb = b.readUInt32LE(16);
    const at = 32 + sb;
    for (let i = 0; i < nc; i++) {
      const o = at + i * 48;
      const typeId = b.readUInt32LE(o);
      const ver = b.readUInt32LE(o + 4);
      const fc = b.readUInt32LE(o + 32);
      const ic = b.readUInt32LE(o + 40);
      const name = TYPE_NAMES[typeId];
      const key = `${name} 0x${ver.toString(16).padStart(4, '0')}`;
      const t = out.types[key];
      // A node with no decoded fields writes zero of everything; those are the
      // 985 the exporter already reports and are not a mismatch.
      if (!t) { if (fc || ic) noLayout++; continue; }
      if (fc === 0 && ic === 0 && (t.floats || t.ints)) continue;
      checked++;
      if (fc !== t.floats || ic !== t.ints) mismatch++;
    }
  }
  console.log(`cross-check: ${checked.toLocaleString()} nodes, ${mismatch} disagree with the ` +
              `predicted counts, ${noLayout} carry fields with no layout entry`);
  if (mismatch || noLayout) process.exitCode = 1;
}

const outArg = process.argv.indexOf('--out');
if (outArg > 0) {
  const p = path.join(base, process.argv[outArg + 1]);
  fs.writeFileSync(p, JSON.stringify(out));
  console.log(`wrote ${process.argv[outArg + 1]} (${(fs.statSync(p).size / 1024).toFixed(0)} KB)`);
}
