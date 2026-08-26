'use strict';
//
// Cross-check every texture the extracted assets REFERENCE against the textures
// that actually SHIP.
//
//   node audit-assets.js              report
//   node audit-assets.js --write DIR  also write referenced.json / missing.json
//
// The question this answers: **is anything referenced but absent?** Those
// surfaces render untextured, and the failure is silent — a missing texture is
// not an error anywhere in the pipeline, just a grey wall in the game.
//
// References come from two kinds of source, and they do NOT deserve equal
// confidence. The report keeps them apart for that reason:
//
//   * **Parsed** — terrain, map objects and meshes, read out of structured
//     fields in formats this pipeline fully decodes. A name here is real.
//   * **Scanned** — item tables, GUI XML and effect definitions, recovered by a
//     regex over (optionally byte-crypt decoded) bytes. Good enough to find
//     absences, but it both misses names and invents them, so its findings are
//     reported as candidates.
//
// The unreferenced figure is likewise **not a deletion list**. Item icon sheets
// are addressed by convention, quest and NPC data name textures this does not
// read, and some are built at runtime. Treating it as an install-size saving
// would strip working art.
//
// Matching is deliberately forgiving in the ways the engine itself is forgiving:
//
//   * **Case-insensitive.** Windows filesystems are, and the shipped data mixes
//     `.DDS` and `.dds` for the same file constantly.
//   * **Extension-agnostic on the fallback pass.** 247 files named `.dds` are
//     really TGA/PNG/PSD; the engine sniffs headers via D3DX rather than
//     trusting the name, and `convert-textures.js` does too. So a reference to
//     `x.dds` is satisfied by `x.tga` on disk.
//   * **Basename only.** References carry no directory and the archives are
//     flat.
//
const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc');

const RAN = path.join(__dirname, '..', '..', '..', 'Ran');
const ASSETS = path.join(__dirname, '..', '..', 'assets');
const argv = process.argv.slice(2);
const writeDir = (() => { const i = argv.indexOf('--write'); return i === -1 ? null : argv[i + 1]; })();

const TEXTURE_EXT = new Set(['.dds', '.tga', '.png', '.bmp', '.jpg', '.jpeg', '.psd']);
const key = (name) => path.basename(String(name)).toLowerCase();
const stripExt = (k) => k.replace(/\.[^.]*$/, '');

// ---- what ships -------------------------------------------------------------
const present = new Map();        // basename+ext -> where
const presentByStem = new Map();  // basename without ext -> [full keys]

function note(name, where) {
  const k = key(name);
  if (!TEXTURE_EXT.has(path.extname(k))) return;
  if (!present.has(k)) present.set(k, where);
  const stem = stripExt(k);
  if (!presentByStem.has(stem)) presentByStem.set(stem, []);
  if (!presentByStem.get(stem).includes(k)) presentByStem.get(stem).push(k);
}

const presentAll = new Set();   // every shipped basename, any extension

(function walk(dir) {
  let items;
  try { items = fs.readdirSync(dir, { withFileTypes: true }); } catch { return; }
  for (const it of items) {
    const p = path.join(dir, it.name);
    if (it.isDirectory()) {
      // Stale per-pid map extractions; counting them inflates everything.
      if (it.name.toLowerCase() === 'ranmapziptemp') continue;
      walk(p);
      continue;
    }
    if (it.name.toLowerCase().endsWith('.rcc')) {
      let ar;
      try { ar = new RccArchive(p); } catch { continue; }
      for (const e of ar.entries) {
        note(e.name, path.basename(p));
        presentAll.add(key(e.name));
      }
    } else {
      note(it.name, 'loose');
      presentAll.add(key(it.name));
    }
  }
})(RAN);

// ---- what is referenced -----------------------------------------------------
const referenced = new Map();     // key -> Set of sources

// Non-texture references, kept by extension. Item records name a lot more than
// images and lumping them together is actively misleading.
const otherRefs = new Map();      // ext -> Map(name -> Set(source))

/**
 * Record a reference, routed by extension.
 *
 * The routing matters. Item records name **models and character pieces** as
 * well as textures: `strFieldFile` is a `.x` or `.egp`, and the 32
 * `strWearingFile*` entries are `.cps` character pieces. Feeding those into the
 * texture set makes every one look like a missing texture — and worse, the
 * extension-agnostic fallback then "resolves" `sword.x` against `sword.dds`,
 * hiding a real answer behind a wrong one. That mistake inflated the missing
 * count from 433 to 7,938 before it was caught.
 */
function refer(name, source) {
  if (!name) return;
  const k = key(name);
  if (!k || k === '.') return;
  const ext = path.extname(k);
  if (TEXTURE_EXT.has(ext)) {
    if (!referenced.has(k)) referenced.set(k, new Set());
    referenced.get(k).add(source);
    return;
  }
  if (!ext) return;
  if (!otherRefs.has(ext)) otherRefs.set(ext, new Map());
  const bucket = otherRefs.get(ext);
  if (!bucket.has(k)) bucket.set(k, new Set());
  bucket.get(k).add(source);
}

// Map manifests already merge terrain + object textures per map.
const mapDir = path.join(ASSETS, 'maps');
if (fs.existsSync(mapDir)) {
  for (const f of fs.readdirSync(mapDir)) {
    if (!f.endsWith('.map.json')) continue;
    const m = JSON.parse(fs.readFileSync(path.join(mapDir, f), 'utf8'));
    for (const t of m.textures || []) refer(t, 'map:' + m.map);
  }
}

// Mesh textures live in each .rmesh submesh table, addressed through the string
// blob — read them straight out rather than re-parsing the source .x files.
const meshDir = path.join(ASSETS, 'meshes');
if (fs.existsSync(meshDir)) {
  for (const f of fs.readdirSync(meshDir)) {
    if (!f.endsWith('.rmesh')) continue;
    const b = fs.readFileSync(path.join(meshDir, f));
    if (b.length < 44 || b.toString('latin1', 0, 4) !== 'RMSH') continue;
    const boneCount = b.readUInt32LE(12);
    const meshCount = b.readUInt32LE(16);
    const subCount = b.readUInt32LE(20);
    const strBytes = b.readUInt32LE(36);
    const strAt = 44;
    const subAt = strAt + strBytes + boneCount * 72 + meshCount * 40;
    for (let s = 0; s < subCount; s++) {
      const off = b.readUInt32LE(subAt + s * 12);
      if (off >= strBytes) continue;
      let end = strAt + off;
      const limit = strAt + strBytes;
      while (end < limit && b[end] !== 0) end++;
      refer(b.toString('latin1', strAt + off, end), 'mesh:' + path.basename(f, '.rmesh'));
    }
  }
}

// Item tables are a real parse, not a scan — `itemdata.js` walks every record
// to EOF — so their references count as parsed. This is what makes item icons
// visible at all: they are addressed as `strInventoryFile` + `sICONID`, never as
// a literal filename in any mesh or map.
const ITEM_FILES = ['item.isf', 'item1.isf'];
const glogicPath = path.join(RAN, 'data/glogic/GLogic.rcc');
if (fs.existsSync(glogicPath)) {
  const I = require('./itemdata');
  let ar = null;
  try { ar = new RccArchive(glogicPath); } catch { /* leave item refs empty */ }
  for (const name of ar ? ITEM_FILES : []) {
    try {
      const table = I.parse(ar.read(name));
      for (const item of table.items) {
        for (const t of I.textures(item)) refer(t, 'item:' + name);
      }
    } catch (err) {
      console.error(`  (item table ${name} not read: ${err.message})`);
    }
  }
}

const parsedRefs = referenced.size;

// ---- data-driven references -------------------------------------------------
//
// Geometry is only one of the ways a texture gets used. Item icons, GUI art and
// effect textures are named in DATA files, and most of those files are
// byte-crypt encoded on a version gate (>= 0x0200 encoded, older plaintext —
// the pattern every format in this tree follows).
//
// This is a length-agnostic STRING SCAN, not a parse. The same technique already
// recovered 2,901 terrain texture names before `.wld0` was properly decoded. It
// is deliberately chosen over implementing `GLItemBasic::LoadFile` and its nine
// versions, because for an audit the names are the whole payload. The trade-off
// is real and stated: a scan can miss a name split across a boundary, and can
// invent one from arbitrary bytes that happen to spell `x.dds`. Treat the counts
// as close, not exact.
//
const B = require('./bytecrypt');

// Table per extension, from CRYPT-MAP.md. Anything unlisted is scanned raw,
// which is correct for the plaintext formats and harmless for the rest.
const TABLE_FOR_EXT = {
  '.isf': 'EMBYTECRYPT_ITEM',
  '.egp': 'EMBYTECRYPT_EGP',
  '.crowsale': 'EMBYTECRYPT_CROWSALE',
  '.charset': 'EMBYTECRYPT_CHARSET',
  '.classconst': 'EMBYTECRYPT_CLASSCONST',
  '.qst': 'EMBYTECRYPT_QUEST',
  '.ims': 'EMBYTECRYPT_ITEMIX',
  '.pis': 'EMBYTECRYPT_PIECE',
  '.cps': 'EMBYTECRYPT_PIECE',
  '.aps': 'EMBYTECRYPT_PIECE',
  '.ntk': 'EMBYTECRYPT_NPCTALK',
  '.npcex': 'EMBYTECRYPT_NPCTALK',
};
// `.egp` encodes from 136, everything else from the 132-byte header end.
const OFFSET_FOR_EXT = { '.egp': 136 };
// Where each format keeps the version that drives its encryption gate. Most sit
// in the CSerialFile header; `.cps` reads its version as the FIRST BODY DWORD
// (`DxSkinPiece::LoadPiece` does `SFile >> dwVer` before anything else), so
// asking the header for it returns 0 and every file looks unencrypted.
const VERSION_AT_BODY = new Set(['.cps', '.aps', '.abl', '.chf', '.abf']);
const ENCODE_GATE = 0x0200;
const TEX_RE = /[A-Za-z0-9_\-]{1,60}\.(dds|tga|png|bmp|jpg|jpeg)/gi;

function scanForTextures(buf, ext, source) {
  const candidates = [buf];
  const table = TABLE_FOR_EXT[ext];
  if (table) {
    let head = null;
    try { head = B.readHeader(buf); } catch { /* not a CSerialFile container */ }
    const bodyAt = head ? head.bodyOffset : 132;
    const version = VERSION_AT_BODY.has(ext)
      ? (buf.length > bodyAt + 4 ? buf.readUInt32LE(bodyAt) : 0)
      : (head ? head.version : 0);
    const from = OFFSET_FOR_EXT[ext] || bodyAt;
    // Scanning both forms cannot lose a name, so when the gate is ambiguous
    // (no header) try the decoded form anyway.
    if (!head || version >= ENCODE_GATE) {
      try { candidates.push(B.decode(Buffer.from(buf), table, from)); } catch { /* skip */ }
    }
  }
  for (const c of candidates) {
    for (const m of c.toString('latin1').matchAll(TEX_RE)) refer(m[0], source);
  }
}

const DATA_ARCHIVES = [
  ['data/glogic/GLogic.rcc', 'glogic'],
  ['data/gui/Gui.rcc', 'gui'],
  ['data/effect/Effect.rcc', 'effect'],
  // Character pieces: the link between an item and the art it wears. 12,879
  // .cps across 17 versions, of which only 960 are current — implementing all
  // fourteen loaders to reach names a scan already finds is not a good trade.
  ['data/skinobject/SkinObject.rcc', 'skinobject'],
  // Quest, NPC dialogue and level data. `.lev` is plaintext (every
  // SetEncodeType for it is commented out — see CRYPT-MAP.md), the other two
  // are version-gated like everything else here.
  ['data/glogic/quest/Quest.rcc', 'quest'],
  ['data/glogic/npctalk/NpcTalk.rcc', 'npctalk'],
  ['data/glogic/level/Level.rcc', 'level'],
];
for (const [rel, label] of DATA_ARCHIVES) {
  const p = path.join(RAN, rel);
  if (!fs.existsSync(p)) continue;
  let ar;
  try { ar = new RccArchive(p); } catch { continue; }
  for (const e of ar.entries) {
    let buf;
    try { buf = ar.read(e); } catch { continue; }
    scanForTextures(buf, path.extname(e.name).toLowerCase(), label);
  }
}

// Loose data directories. `data/piece` alone is 1,105 `.pis` map-piece files
// that live on disk rather than in any archive, so an archive-only sweep misses
// them entirely.
const DATA_DIRS = [
  ['data/piece', 'piece'],
  ['data/skin', 'skin'],
  ['data/skeleton', 'skeleton'],
];
const SCANNABLE_LOOSE = new Set(['.pis', '.cps', '.aps', '.egp', '.mxf']);
for (const [rel, label] of DATA_DIRS) {
  const dir = path.join(RAN, rel);
  let entries;
  try { entries = fs.readdirSync(dir); } catch { continue; }
  for (const name of entries) {
    const ext = path.extname(name).toLowerCase();
    if (!SCANNABLE_LOOSE.has(ext)) continue;
    try {
      scanForTextures(fs.readFileSync(path.join(dir, name)), ext, label);
    } catch { /* unreadable file is not a reason to abandon the sweep */ }
  }
}

// ---- resolve ----------------------------------------------------------------
const missing = [];
const resolvedByStem = [];
let exact = 0;

for (const [k, sources] of referenced) {
  if (present.has(k)) { exact++; continue; }
  // The engine sniffs headers, so a name/extension mismatch still resolves.
  const alt = presentByStem.get(stripExt(k));
  if (alt && alt.length) {
    resolvedByStem.push({ wanted: k, found: alt[0] });
    continue;
  }
  missing.push({ name: k, sources: [...sources].slice(0, 3), count: sources.size });
}

const referencedKeys = new Set(referenced.keys());
const referencedStems = new Set([...referencedKeys].map(stripExt));
const unreferenced = [...present.keys()].filter((k) => !referencedKeys.has(k) &&
                                                       !referencedStems.has(stripExt(k)));

// Split the misses by how much the source can be trusted. A regex over decoded
// bytes produces both real names and debris; `outgui_character_lgaacter_lga.dds`
// is two overlapping matches, and `e.tga` is two bytes that happened to line up.
// Anything with a stem under three characters or not starting with a letter is
// treated as debris — crude, but it keeps the headline number honest.
const SCAN_SOURCES = new Set(['glogic', 'gui', 'effect', 'skinobject',
                              'quest', 'npctalk', 'level',
                              'piece', 'skin', 'skeleton']);  // item:* is parsed
const isScanOnly = (m) => m.sources.every((s) => SCAN_SOURCES.has(s));
const looksReal = (name) => /^[a-z]/.test(name) && stripExt(name).length >= 3;

const missingParsed = missing.filter((m) => !isScanOnly(m));
const missingScanned = missing.filter((m) => isScanOnly(m) && looksReal(m.name));
const missingDebris = missing.filter((m) => isScanOnly(m) && !looksReal(m.name));

const fmt = (n) => n.toLocaleString('en-US');
console.log(`textures shipped:    ${fmt(present.size)}`);
console.log(`textures referenced: ${fmt(referenced.size)}`);
console.log(`  parsed  (terrain, objects, meshes, items) ${fmt(parsedRefs)}`);
console.log(`  scanned (GUI, effects, other glogic)     ${fmt(referenced.size - parsedRefs)} more`);
console.log(`resolved exactly              ${fmt(exact)}`);
console.log(`resolved by name, not ext     ${fmt(resolvedByStem.length)}`);
console.log(`MISSING, from PARSED refs     ${fmt(missingParsed.length)}   <- reliable`);
console.log(`MISSING, from SCANNED refs    ${fmt(missingScanned.length)}   <- candidates`);
console.log(`  (plus ${fmt(missingDebris.length)} discarded as scan debris)`);
// Non-texture references, so the item -> character-piece -> art chain is visible
// rather than silently dropped.
console.log('\nnon-texture references (parsed from item records):');
for (const [ext, bucket] of [...otherRefs].sort((a, b) => b[1].size - a[1].size).slice(0, 6)) {
  const miss = [...bucket.keys()].filter((k) => !presentAll.has(k)).length;
  console.log(`  ${ext.padEnd(6)} ${String(bucket.size).padStart(6)} referenced, ${miss} missing`);
}
console.log(`referenced by nothing scanned: ${fmt(unreferenced.length)}`);
console.log('  Still not a deletion list. Quest, NPC and level data ARE scanned');
console.log('  now and reference no textures at all, so the remainder is mostly');
console.log('  legacy art variants — but a scan can miss a name, and some');
console.log('  character textures are substituted at runtime per school/class.');

if (resolvedByStem.length) {
  console.log('\nname/extension mismatches (engine sniffs headers, so these are fine):');
  for (const r of resolvedByStem.slice(0, 8)) console.log(`  ~ ${r.wanted} -> ${r.found}`);
  if (resolvedByStem.length > 8) console.log(`  ... and ${resolvedByStem.length - 8} more`);
}

if (missingParsed.length) {
  console.log('\nreferenced by geometry but NOT SHIPPED — these render untextured:');
  for (const m of missingParsed.slice(0, 12)) {
    console.log(`  ! ${m.name}  (${m.count} ref${m.count === 1 ? '' : 's'}, e.g. ${m.sources[0]})`);
  }
  if (missingParsed.length > 12) console.log(`  ... and ${missingParsed.length - 12} more`);
}

if (missingScanned.length) {
  console.log('\ncandidates from the string scan — real-looking, unverified:');
  for (const m of missingScanned.slice(0, 8)) {
    console.log(`  ? ${m.name}  (${m.sources.join(', ')})`);
  }
  if (missingScanned.length > 8) console.log(`  ... and ${missingScanned.length - 8} more`);
}

if (writeDir) {
  fs.mkdirSync(writeDir, { recursive: true });
  fs.writeFileSync(path.join(writeDir, 'referenced.json'),
                   JSON.stringify([...referenced.keys()].sort(), null, 2));
  fs.writeFileSync(path.join(writeDir, 'missing.json'),
                   JSON.stringify({ parsed: missingParsed, scanned: missingScanned,
                                    debris: missingDebris }, null, 2));
  fs.writeFileSync(path.join(writeDir, 'unreferenced.json'),
                   JSON.stringify(unreferenced.sort(), null, 2));
  console.log(`\nwrote referenced.json / missing.json / unreferenced.json to ${writeDir}`);
}
process.exit(0);
