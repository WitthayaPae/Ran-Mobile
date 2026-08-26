'use strict';
//
// Every UI control id the C# asks for, checked against uicfg.json.
//
//   node audit-uicfg-ids.js            report
//   node audit-uicfg-ids.js --strict   exit 1 if any id is unknown
//
// Why this exists: a control id that does not exist fails SILENTLY. RanUiCfg
// .TryGet returns false, the caller takes its hardcoded fallback rect, and the
// window still builds — so a typo costs one control's PC placement and nothing
// anywhere reports it. That is indistinguishable from "the art is missing",
// which is the failure this project has already spent time chasing twice.
//
// Also reports ids that resolve but whose atlas is not staged, because that is
// the other half of the same question: a control can have a real rect and still
// draw nothing.
const fs = require('fs');
const path = require('path');

const base = path.resolve(__dirname, '../../..');
const RUNTIME = path.join(base, 'MOBILE/unity/com.ran.mobile.assets/Runtime');
const UICFG = path.join(base, 'MOBILE/assets/uicfg.json');
const UIDIR = path.join(base, 'MOBILE/unity/RanMobile/Assets/Ran/Resources/UI');

const ALIASES = {
  'outgui_char': 'outgui_character',
  'outgui_character_lgaacter_lga': 'outgui_character_lga',
};

const cfg = JSON.parse(fs.readFileSync(UICFG, 'utf8')).controls;
const known = new Set(Object.keys(cfg).map((k) => k.toUpperCase()));

const staged = new Set(
  fs.readdirSync(UIDIR)
    .filter((f) => /\.(png|jpg)$/i.test(f))
    .map((f) => path.basename(f, path.extname(f)).toLowerCase()));

// A control id looks like SCREAMING_SNAKE and is at least two words long; that
// shape is what separates it from ordinary string literals in the same files.
const IDLIKE = /"([A-Z][A-Z0-9]*(?:_[A-Z0-9]+){1,})"/g;

// Ids built by concatenation or a format string cannot be checked statically;
// they are counted rather than reported as unknown.
const DYNAMIC = /"[A-Z][A-Z0-9_]*_"\s*\+|\+\s*"[A-Z0-9_]*"|\{0\}/;

const unknown = new Map();   // id -> [files]
let checked = 0, dynamic = 0;
const unstaged = new Map();  // id -> atlas

for (const f of fs.readdirSync(RUNTIME).filter((n) => n.endsWith('.cs'))) {
  const src = fs.readFileSync(path.join(RUNTIME, f), 'utf8');
  if (DYNAMIC.test(src)) dynamic++;
  let m;
  IDLIKE.lastIndex = 0;
  while ((m = IDLIKE.exec(src)) !== null) {
    const id = m[1];
    // Only judge ids this project actually looks up; the same shape shows up in
    // packet-name constants and log tags, which are not controls.
    const at = Math.max(0, m.index - 60);
    const ctx = src.slice(at, m.index);
    // Only the call sites that resolve against uicfg. Deliberately NOT a loose
    // "Get(" — RanGameWord.Text and the packet-name constants share the same
    // SCREAMING_SNAKE shape and would otherwise be reported as missing controls.
    if (!/(TryGet|Ctl|GetRect|SpriteFor|MakeSprite|MakeSliced|PlaceTopLevel|Place|SkinSliced|Skin)\(\s*$/.test(ctx)
        && !/_ui\.(TryGet|Get)\(\s*$/.test(ctx)
        && !/_cfg\.(TryGet|Get)\(\s*$/.test(ctx)) continue;

    checked++;
    if (!known.has(id.toUpperCase())) {
      if (!unknown.has(id)) unknown.set(id, []);
      if (!unknown.get(id).includes(f)) unknown.get(id).push(f);
      continue;
    }
    const c = cfg[Object.keys(cfg).find((k) => k.toUpperCase() === id.toUpperCase())];
    if (c && c.tex && c.tw > 0) {
      let bare = path.basename(c.tex, path.extname(c.tex)).toLowerCase();
      bare = ALIASES[bare] || bare;
      if (!staged.has(bare)) unstaged.set(id, c.tex);
    }
  }
}

console.log(`control ids looked up in C#: ${checked}`);
console.log(`unknown to uicfg.json      : ${unknown.size}`);
console.log(`known but atlas not staged : ${unstaged.size}`);

if (unknown.size) {
  console.log('\nunknown ids (each costs that control its PC rect, silently):');
  for (const [id, files] of [...unknown].sort()) {
    console.log(`  ${id.padEnd(44)} ${files.join(', ')}`);
  }
}
if (unstaged.size) {
  console.log('\nrect resolves but the atlas is absent:');
  for (const [id, tex] of [...unstaged].sort()) {
    console.log(`  ${id.padEnd(44)} ${tex}`);
  }
}

if (process.argv.includes('--strict') && unknown.size) process.exit(1);
