'use strict';
//
// The PC client's UI layout, from the `ui*cfg*.xml` files in Gui.rcc.
//
//   node extract-uicfg.js                        summary
//   node extract-uicfg.js --prefix LOGIN_        just the login page
//   node extract-uicfg.js --out MOBILE/assets/uicfg.json
//
// Every control the C++ creates is looked up BY NAME:
//
//     CInterfaceCfg::GetInstance().ArrangeInfo( szControlKeyword, uiCfg )
//
// and that lookup resolves against these XML files (`DxGlobalStage.cpp:391`
// loads every one listed by the GUI list XML). So the layout is data, not code
// — which means the mobile client can render the SAME panel rather than an
// approximation of it, and a control that moves in a future client patch moves
// here too.
//
//     <CONTROL Local="Common" Id="LOGIN_PAGE_OK">
//       <WINDOW_POS  X="10" Y="125" W="86" H="18" />
//       <TEXTURE SizeX="512" SizeY="512">Interface_Main.dds</TEXTURE>
//       <TEXTURE_POS X="238" Y="125" W="11" H="11" />
//     </CONTROL>
//
// WINDOW_POS is the control's rect in PARENT-relative pixels; TEXTURE_POS is
// the source rect inside the named sheet. Both are needed: the sheet is an
// atlas and the same file backs dozens of unrelated controls.
const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc.js');
const G = require('./gamecrypt.js');

const base = path.resolve(__dirname, '../../..');
const ARCHIVE = path.join(base, 'Ran/data/gui/Gui.rcc');

// The LOOSE configs are read first and win, for two reasons.
//
// One: they are newer. The operator edits `CLIENT/data/gui/*.xml` and repacks
// Gui.rcc only later, so the archive lags every window added or moved since the
// last repack — auction, auto-pot and item-transfer all existed as loose xml
// while being absent from uicfg.json entirely.
//
// Two: this used to read only `ui*cfg*.xml`, which silently skipped the whole
// `_inner_*.xml` family — 51 files and 3,744 CONTROL blocks, i.e. most modern
// window layouts. A window with no entry here falls back to a hardcoded rect
// and nothing reports it, so the loss was invisible.
const LOOSE_DIR = path.join(base, 'CLIENT/data/gui');

/** Config xml, either family. Excludes .bak siblings — they are previous
 *  revisions and would overwrite current geometry by iteration order. */
// Standalone underscore-prefixed window configs that are neither `_inner_*`
// nor `_outer_*` — named individually (not by a wildcard) because several
// OTHER underscore-less and underscore-prefixed files in the same directory
// (gameextext/gameintext/gameword/launcher/npctalkstrtable/queststrtable/
// servertext are plain string tables; uiinnertexturelist.xml has <CONTROL>
// blocks too but they are unpositioned texture-atlas lookups with no
// WINDOW_POS/Local, a different namespace of small generic-looking ids like
// "Button_Add_Down" that would collide across contexts) are NOT window
// layouts and must not be swept in by a broad `^_.*\.xml$` pattern.
const MISC_WINDOW_FILES = new Set(['_pandorawindow.xml', '_petstyle.xml']);

function isConfigXml(name) {
  if (/\.bak/i.test(name)) return false;
  return /^ui[a-z0-9]*cfg[0-9]*\.xml$/i.test(name) || /^_inner_.*\.xml$/i.test(name) ||
         /^_outer_.*\.xml$/i.test(name) || MISC_WINDOW_FILES.has(name.toLowerCase());
}

function attrs(tag) {
  const out = {};
  const re = /([A-Za-z_]+)\s*=\s*"([^"]*)"/g;
  let m;
  while ((m = re.exec(tag))) out[m[1]] = m[2];
  return out;
}

function num(v) { const n = parseFloat(v); return Number.isFinite(n) ? n : 0; }

/** Parse one `<CONTROL>` block into a flat record. */
function parseControl(id, body, local) {
  const rec = { id, local };

  const win = /<WINDOW_POS([^>]*)\/>/.exec(body);
  if (win) {
    const a = attrs(win[1]);
    rec.x = num(a.X); rec.y = num(a.Y); rec.w = num(a.W); rec.h = num(a.H);
  }

  const tex = /<TEXTURE([^>]*)>([^<]*)<\/TEXTURE>/.exec(body);
  if (tex) {
    const a = attrs(tex[1]);
    rec.tex = (tex[2] || '').trim();
    rec.texSizeX = num(a.SizeX);
    rec.texSizeY = num(a.SizeY);
  }

  const tp = /<TEXTURE_POS([^>]*)\/>/.exec(body);
  if (tp) {
    const a = attrs(tp[1]);
    rec.tx = num(a.X); rec.ty = num(a.Y); rec.tw = num(a.W); rec.th = num(a.H);
  }

  const flip = /<FLIP_TEXTURE_POS([^>]*)\/>/.exec(body);
  if (flip) {
    const a = attrs(flip[1]);
    rec.fx = num(a.X); rec.fy = num(a.Y); rec.fw = num(a.W); rec.fh = num(a.H);
  }
  return rec;
}

const controls = {};
const perFile = {};
let files = 0;

/** Harvest every <CONTROL> in one config's text. First definition wins,
 *  matching the load order the client uses. */
function harvest(label, buf) {
  files++;
  if (G.isEncoded(buf)) { try { buf = G.decode(buf); } catch { /* plain */ } }
  // Strip XML comments BEFORE the CONTROL/WINDOW_POS regexes ever see the
  // text. Several loose configs leave a previous revision's WINDOW_POS (and
  // occasionally the previous rect for several sibling tags) inside a
  // `<!-- ... -->` block immediately ahead of the live one, as a note for
  // "restore when this feature is re-enabled" — e.g.
  // `_inner_competitionui.xml`'s COMPETITION_NOTIFY_BUTTON has the OLD
  // (542,514) rect in a comment directly before the LIVE (605,514) tag.
  // `parseControl`'s WINDOW_POS/TEXTURE regexes take the FIRST match in a
  // control's body, so without stripping comments first they silently locked
  // onto the stale, commented-out rect instead of the real one. Confirmed by
  // diffing extraction with/without this strip across every loose file that
  // has a WINDOW_POS-shaped string inside a comment (8 files): real,
  // measurable rect changes for COMPETITION_NOTIFY_BUTTON, RAN_PRODUCT_BUTTON,
  // RAN_ITEMMALL_NOTIFY_BUTTON, RAN_PLAYERRANKING_NOTIFY_BUTTON,
  // RAN_SHOP_BUTTON, and three PANDORA_WINDOW_* panes (whose OLD rect was the
  // ENTIRE line commented out, not just a note in prose).
  const text = buf.toString('latin1').replace(/[^\x20-\x7e\r\n]/g, '')
    .replace(/<!--[\s\S]*?-->/g, '');

  let n = 0;
  const re = /<CONTROL([^>]*)>([\s\S]*?)<\/CONTROL>/g;
  let m;
  while ((m = re.exec(text))) {
    const a = attrs(m[1]);
    if (!a.Id) continue;
    if (!controls[a.Id]) { controls[a.Id] = parseControl(a.Id, m[2], a.Local || ''); n++; }
  }
  perFile[label] = n;
}

const isUiCfg  = (n) => /^ui[a-z0-9]*cfg[0-9]*\.xml$/i.test(n);
const isInner  = (n) => /^_inner_.*\.xml$/i.test(n);
const isOuter  = (n) => /^_outer_.*\.xml$/i.test(n);
const isMisc   = (n) => MISC_WINDOW_FILES.has(n.toLowerCase());

// Order is deliberate and STRICTLY ADDITIVE. 191 ids are defined in both
// families, and the client's real load order comes from its gui list rather
// than from anything visible here — so the ui*cfg* family keeps the precedence
// it has always had, and the _inner_ family only fills ids nobody else defined.
// That way adding 3,491 new controls cannot move a control that already works.
// Within each family, loose beats the archive because loose is newer.
//
// _outer_*.xml (create-character, select-character, register) is a THIRD,
// lowest-precedence pass added for the same reason _inner_ was: these ids
// were simply never scanned (the regex only matched `ui*cfg*` / `_inner_`),
// so CREATE_CHAR_* etc. were silently absent from every consumer of this
// file. Lowest precedence keeps it from ever overriding an id something else
// already defines.
//
// _pandorawindow.xml / _petstyle.xml are a FOURTH, still lower-precedence
// pass — same bug class again (filename regex missed them entirely; found by
// RanUiCheck reporting PANDORA_BUTTON absent from uicfg.json). Measured:
// they are genuine positioned window layouts (WINDOW_POS + Local on every
// CONTROL, same shape as _inner_/_outer_), not string tables — see the
// MISC_WINDOW_FILES comment above for the other 8 underscore/non-underscore
// files in the same directory that were checked and are NOT layouts.
let loose = 0;
function pass(accept) {
  try {
    for (const name of fs.readdirSync(LOOSE_DIR).sort()) {
      if (!isConfigXml(name) || !accept(name)) continue;
      try { harvest(name, fs.readFileSync(path.join(LOOSE_DIR, name))); loose++; }
      catch { /* an unreadable file costs that file, not the run */ }
    }
  } catch { /* no loose dir: archive only */ }

  for (const e of arc.entries) {
    if (!isConfigXml(e.name) || !accept(e.name)) continue;
    if (perFile[e.name] !== undefined) continue;   // already taken from loose
    let buf;
    try { buf = arc.read(e); } catch { continue; }
    harvest(e.name, buf);
  }
}

const arc = new RccArchive(ARCHIVE);
pass(isUiCfg);
pass(isInner);
pass(isOuter);
pass(isMisc);

const all = Object.keys(controls);
const withTex = all.filter((k) => controls[k].tex);
const sheets = new Set(withTex.map((k) => controls[k].tex.toLowerCase()));

console.log(`${files} config files (${loose} loose, ${files - loose} from the archive) ` +
            `-> ${all.length} controls ` +
            `(${withTex.length} textured, ${sheets.size} distinct sheets)`);

const pi = process.argv.indexOf('--prefix');
if (pi > 0) {
  const p = process.argv[pi + 1];
  const hits = all.filter((k) => k.startsWith(p)).sort();
  console.log(`\n${hits.length} controls matching "${p}":`);
  for (const k of hits) {
    const c = controls[k];
    console.log(`  ${k.padEnd(30)} ${String(c.x).padStart(4)},${String(c.y).padStart(4)} ` +
                `${String(c.w).padStart(4)}x${String(c.h).padStart(3)}` +
                (c.tex ? `   ${c.tex} @ ${c.tx},${c.ty} ${c.tw}x${c.th}` : ''));
  }
}

const oi = process.argv.indexOf('--out');
if (oi > 0) {
  const out = process.argv[oi + 1];
  fs.writeFileSync(path.join(base, out), JSON.stringify({ controls }));
  console.log(`\nwrote ${out}`);
}

module.exports = { parseControl, attrs };
