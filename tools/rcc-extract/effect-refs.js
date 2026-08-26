'use strict';
//
// Two questions this answers, kept strictly apart because they have different
// confidence:
//
//  (a) What does each effect TYPE reference at runtime?  Read out of the
//      blitted PROPERTY body of each node — but by SCANNING it for NUL-
//      terminated printable runs with a known extension, not by decoding the
//      struct field by field. The struct differs per type and per version and
//      the names are the whole payload here, the same trade already made for
//      `.cps` in audit-assets.js. So: a scan, and reported as one.
//
//  (b) Which of the 4,294 shipped `.egp` are actually NAMED by shipped
//      content?  Effect names never appear in geometry — they are addressed
//      from skill/item/NPC/pet tables in `data/glogic`, from map pieces, and
//      from a handful of hardcoded literals in the client. Those are scanned
//      for `*.egp` tokens, decoded per CRYPT-MAP.md where the format is
//      version-gated.
//
const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc');
const B = require('./bytecrypt');
const E = require('./effect-egp');

const RAN = path.join(__dirname, '..', '..', '..', 'Ran');
const EFFECT_RCC = path.join(RAN, 'data/effect/Effect.rcc');

// ---- (a) what each type references ------------------------------------------
const RES_RE = /[A-Za-z0-9_\-.]{1,64}\.(dds|tga|png|bmp|jpg|x|chf|egp|wav|ogg|cps|aps|pis)/gi;
const KIND = {
  dds: 'texture', tga: 'texture', png: 'texture', bmp: 'texture', jpg: 'texture',
  x: 'mesh', chf: 'skinchar', egp: 'effect', wav: 'sound', ogg: 'sound',
  cps: 'piece', aps: 'piece', pis: 'piece',
};

// Strict: read the declared fields only. `null` means this node version has no
// field map, which is reported rather than papered over with a scan.
function nodeResources(n) {
  return E.resources(n);
}

const ar = new RccArchive(EFFECT_RCC);
const egpNames = new Set();
const parsed = [];
for (const e of ar.entries) {
  if (!e.name.toLowerCase().endsWith('.egp')) continue;
  egpNames.add(e.name.toLowerCase());
  const r = E.parse(ar.read(e), e.name);
  parsed.push(r);
}

const perType = new Map();   // type -> {nodes, kinds:Map, names:Map(kind->Set), sounds, unmapped}
for (const r of parsed) {
  for (const n of r.nodes || []) {
    if (!perType.has(n.type)) {
      perType.set(n.type, { nodes: 0, kinds: new Map(), names: new Map(), sounds: 0, noRes: 0, unmapped: 0 });
    }
    const t = perType.get(n.type);
    t.nodes++;
    if (n.sound) t.sounds++;
    const hits = nodeResources(n);
    if (hits === null) { t.unmapped++; continue; }
    if (!hits.length) t.noRes++;
    const kinds = new Set();
    for (const h of hits) {
      kinds.add(h.kind);
      if (!t.names.has(h.kind)) t.names.set(h.kind, new Set());
      t.names.get(h.kind).add(h.name.toLowerCase());
    }
    for (const k of kinds) t.kinds.set(k, (t.kinds.get(k) || 0) + 1);
  }
}

console.log('== (a) what each effect type carries, read from its declared name fields ==');
console.log(`${'type'.padEnd(13)} ${'nodes'.padStart(6)} ${'texture'.padStart(8)} ${'mesh'.padStart(6)} ${'skinchar'.padStart(9)} ${'effect'.padStart(7)} ${'sound'.padStart(6)} ${'none'.padStart(6)} ${'nofield'.padStart(8)}`);
const order = [...perType.entries()].sort((a, b) => b[1].nodes - a[1].nodes);
for (const [type, t] of order) {
  const g = (k, w) => String(t.kinds.get(k) || 0).padStart(w);
  console.log(`${type.padEnd(13)} ${String(t.nodes).padStart(6)} ${g('texture', 8)} ${g('mesh', 6)} ${g('skinchar', 9)} ${g('effect', 7)} ${String(t.sounds).padStart(6)} ${String(t.noRes).padStart(6)} ${String(t.unmapped).padStart(8)}`);
}
console.log('\ndistinct names referenced, by type and kind:');
for (const [type, t] of order) {
  const parts = [...t.names.entries()].map(([k, s]) => `${k}:${s.size}`);
  console.log(`  ${type.padEnd(13)} ${parts.join('  ') || '(none)'}`);
}

// union of texture names across all effects, vs what ships
const allTex = new Set();
const allMesh = new Set();
for (const [, t] of perType) {
  for (const n of t.names.get('texture') || []) allTex.add(n);
  for (const n of t.names.get('mesh') || []) allMesh.add(n);
  for (const n of t.names.get('skinchar') || []) allMesh.add(n);
}
console.log(`\ndistinct textures named by effects: ${allTex.size}`);
console.log(`distinct meshes/skinchars named by effects: ${allMesh.size}`);

// how many ship?
const shipped = new Map();  // lowercase basename -> true
function indexDir(dir) {
  let st;
  try { st = fs.statSync(dir); } catch { return; }
  if (!st.isDirectory()) return;
  for (const n of fs.readdirSync(dir)) {
    const p = path.join(dir, n);
    let s;
    try { s = fs.statSync(p); } catch { continue; }
    if (s.isDirectory()) indexDir(p);
    else shipped.set(n.toLowerCase(), true);
  }
}
indexDir(path.join(RAN, 'textures'));
for (const rccRel of ['data/animation/Animation.rcc', 'data/map/Map.rcc', 'data/skinobject/SkinObject.rcc']) {
  const p = path.join(RAN, rccRel);
  if (!fs.existsSync(p)) continue;
  try { for (const e of new RccArchive(p).entries) shipped.set(e.name.toLowerCase(), true); } catch { /* skip */ }
}
// Effect meshes are loose `.x` in data/object (941 files), not in any archive.
for (const rel of ['data/object', 'data/skin', 'data/skeleton', 'data/piece', 'sounds']) {
  indexDir(path.join(RAN, rel));
}
const stem = (n) => n.replace(/\.[^.]+$/, '');
const shippedStems = new Set([...shipped.keys()].map(stem));
const missTex = [...allTex].filter((n) => !shipped.has(n) && !shippedStems.has(stem(n)));
const missMesh = [...allMesh].filter((n) => !shipped.has(n) && !shippedStems.has(stem(n)));
console.log(`  textures present: ${allTex.size - missTex.length}, absent: ${missTex.length}`);
console.log(`  meshes present:   ${allMesh.size - missMesh.length}, absent: ${missMesh.length}`);
console.log(`  absent texture examples: ${missTex.slice(0, 8).join(', ')}`);

// ---- (b) which .egp are named by shipped content ------------------------------
const EGP_RE = /[A-Za-z0-9_\-]{1,60}\.egp/gi;
const TABLE_FOR_EXT = {
  '.isf': 'EMBYTECRYPT_ITEM', '.crowsale': 'EMBYTECRYPT_CROWSALE',
  '.charset': 'EMBYTECRYPT_CHARSET', '.classconst': 'EMBYTECRYPT_CLASSCONST',
  '.qst': 'EMBYTECRYPT_QUEST', '.ims': 'EMBYTECRYPT_ITEMIX',
  '.pis': 'EMBYTECRYPT_PIECE', '.cps': 'EMBYTECRYPT_PIECE', '.aps': 'EMBYTECRYPT_PIECE',
  '.ntk': 'EMBYTECRYPT_NPCTALK', '.npcex': 'EMBYTECRYPT_NPCTALK',
  '.egp': 'EMBYTECRYPT_EGP',
};
const refs = new Map();   // lowercase egp name -> Set(source)
function note(name, src) {
  const k = name.toLowerCase();
  if (!refs.has(k)) refs.set(k, new Set());
  refs.get(k).add(src);
}
function scanEgpRefs(buf, ext, src) {
  const cands = [buf];
  const table = TABLE_FOR_EXT[ext];
  if (table) {
    try {
      const from = ext === '.egp' ? 136 : 132;
      const cp = Buffer.from(buf);
      B.decode(cp, table, from);
      cands.push(cp);
    } catch { /* skip */ }
  }
  for (const c of cands) for (const m of c.toString('latin1').matchAll(EGP_RE)) note(m[0], src);
}

const ARCHIVES = [
  ['data/glogic/GLogic.rcc', 'glogic'],
  ['data/gui/Gui.rcc', 'gui'],
  ['data/glogic/quest/Quest.rcc', 'quest'],
  ['data/glogic/npctalk/NpcTalk.rcc', 'npctalk'],
  ['data/glogic/level/Level.rcc', 'level'],
  ['data/skinobject/SkinObject.rcc', 'skinobject'],
  ['data/map/Map.rcc', 'map'],
];
for (const [rel, label] of ARCHIVES) {
  const p = path.join(RAN, rel);
  if (!fs.existsSync(p)) continue;
  let a;
  try { a = new RccArchive(p); } catch { continue; }
  for (const e of a.entries) {
    let b;
    try { b = a.read(e); } catch { continue; }
    scanEgpRefs(b, path.extname(e.name).toLowerCase(), label);
  }
}
for (const rel of ['data/piece', 'data/skin', 'data/skeleton', 'data/object']) {
  const dir = path.join(RAN, rel);
  let names;
  try { names = fs.readdirSync(dir); } catch { continue; }
  for (const n of names) {
    const ext = path.extname(n).toLowerCase();
    try { scanEgpRefs(fs.readFileSync(path.join(dir, n)), ext, rel.split('/').pop()); } catch { /* skip */ }
  }
}
// .egp naming .egp (PARTICLESYS carries m_szEffFile, MOVETARGET m_szFileName)
for (const r of parsed) {
  for (const n of r.nodes || []) {
    for (const h of nodeResources(n) || []) if (h.kind === 'effect') note(h.name, 'effect');
  }
}

console.log('\n== (b) which shipped .egp are named by shipped content ==');
const bySrc = new Map();
for (const [, s] of refs) for (const x of s) bySrc.set(x, (bySrc.get(x) || 0) + 1);
console.log('references found per source:');
for (const [s, n] of [...bySrc.entries()].sort((a, b) => b[1] - a[1])) console.log(`  ${s.padEnd(12)} ${n}`);
const referenced = [...refs.keys()].filter((n) => egpNames.has(n));
const dangling = [...refs.keys()].filter((n) => !egpNames.has(n));
console.log(`distinct .egp names referenced: ${refs.size}`);
console.log(`  resolve to a shipped file: ${referenced.length}`);
console.log(`  named but not shipped:     ${dangling.length}`);
console.log(`shipped .egp never named:    ${[...egpNames].filter((n) => !refs.has(n)).length} of ${egpNames.size}`);

// type mix restricted to the referenced subset
const refSet = new Set(referenced);
const mixAll = new Map();
const mixRef = new Map();
let refFiles = 0;
for (const r of parsed) {
  const isRef = refSet.has(r.name.toLowerCase());
  if (isRef) refFiles++;
  const seen = new Set((r.nodes || []).map((n) => n.type));
  for (const t of seen) {
    mixAll.set(t, (mixAll.get(t) || 0) + 1);
    if (isRef) mixRef.set(t, (mixRef.get(t) || 0) + 1);
  }
}
console.log(`\ntype presence: all ${parsed.length} files vs the ${refFiles} named ones`);
console.log(`  ${'type'.padEnd(13)} ${'all'.padStart(6)} ${'%'.padStart(6)}  ${'named'.padStart(6)} ${'%'.padStart(6)}`);
for (const [t, n] of [...mixAll.entries()].sort((a, b) => b[1] - a[1])) {
  const rn = mixRef.get(t) || 0;
  console.log(`  ${t.padEnd(13)} ${String(n).padStart(6)} ${((n / parsed.length) * 100).toFixed(1).padStart(6)}  ${String(rn).padStart(6)} ${(refFiles ? (rn / refFiles) * 100 : 0).toFixed(1).padStart(6)}`);
}

if (process.argv.includes('--write')) {
  const out = process.argv[process.argv.indexOf('--write') + 1];
  fs.mkdirSync(out, { recursive: true });
  fs.writeFileSync(path.join(out, 'effect-refs.json'), JSON.stringify({
    referenced: referenced.sort(),
    dangling: dangling.sort(),
    unreferenced: [...egpNames].filter((n) => !refs.has(n)).sort(),
  }, null, 1));
  console.log(`\nwrote ${out}/effect-refs.json`);
}
