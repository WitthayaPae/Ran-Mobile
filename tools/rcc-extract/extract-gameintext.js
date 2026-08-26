'use strict';
//
// The client's "in-game text" table — gameintext.xml — flattened to JSON.
//
//   node extract-gameintext.js --out MOBILE/assets/gameintext.json
//
// gameintext.xml is a SEPARATE table from gameword.xml, both inside the same
// Gui.rcc archive. PC's CGameTextMan keeps GAME_WORD (ID2GAMEWORD — control
// labels, buttons, tooltips) and GAME_IN_TEXT (ID2GAMEINTEXT — longer runtime
// messages: dialogue prompts, quest/system notices) in two distinct maps
// (GameTextControl.cpp: EM_GAME_WORD vs EM_GAME_IN_TEXT, RANPARAM.cpp:181
// strGameInText = "gameintext.xml"). A control built with ID2GAMEINTEXT is NOT
// found by looking gameword.json up under the same id — this file exists so
// that lookup has somewhere real to land instead of falling back to English.
// Example: CRebirthDialogue's body text is
// ID2GAMEINTEXT("REBIRTH_DIALOGUE_TEXT") (RebirthDialogue.cpp:105), not
// ID2GAMEWORD — confirmed absent from gameword.xml, present here as
// "ต้องการเกิดใหม่หรือเปล่า ?".
//
// Same container/encoding situation as gameword.xml: plaintext UTF-8 once
// rcc.js peels the RCC wrapper, so this is a straight XML parse (the
// <WORD>/<SENTENSE> shape and Lang="Common" selection are identical — the
// parser here is the extract-gameword.js one, unchanged).
//
// Output: { "TEXT_ID": ["value0", "value1", ...], ... }, so the runtime looks
// a control's text up by the SAME id the PC passes to ID2GAMEINTEXT.
//
const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc');

const RAN = path.join(__dirname, '..', '..', '..', 'Ran');
const argv = process.argv.slice(2);
const val = (f, d) => { const i = argv.indexOf(f); return i === -1 ? d : argv[i + 1]; };
const outFile = val('--out', null);

const arc = new RccArchive(path.join(RAN, 'data', 'gui', 'Gui.rcc'));
let entry = null;
for (const e of arc.list()) {
  if ((e.name || e).toLowerCase() === 'gameintext.xml') { entry = e; break; }
}
if (!entry) { console.error('gameintext.xml not found in Gui.rcc'); process.exit(1); }

const xml = arc.read(entry).toString('utf8');

// Each entry: <WORD|SENTENSE Ver=".." Id="KEY"> <VALUE Lang=".." Index="n">text</VALUE> ... </>
const words = {};
const entryRe = /<(?:WORD|SENTENSE)\b[^>]*\bId="([^"]+)"[^>]*>([\s\S]*?)<\/(?:WORD|SENTENSE)>/g;
const valRe = /<VALUE\b([^>]*)>([\s\S]*?)<\/VALUE>/g;
let m;
let count = 0;
while ((m = entryRe.exec(xml)) !== null) {
  const id = m[1];
  const body = m[2];
  const vals = [];
  const fallback = [];
  let next = 0, v;
  while ((v = valRe.exec(body)) !== null) {
    const attrs = v[1];
    const idxM = /\bIndex="(\d+)"/.exec(attrs);
    const idx = idxM ? Number(idxM[1]) : next;
    const text = decodeEntities(v[2].trim());
    if (/\bLang="Common"/.test(attrs)) { vals[idx] = text; next = Math.max(next, idx + 1); }
    else fallback[idx] = text;
  }
  valRe.lastIndex = 0;
  words[id] = vals.length ? vals : fallback;
  count++;
}

function decodeEntities(s) {
  return s.replace(/&lt;/g, '<').replace(/&gt;/g, '>')
          .replace(/&quot;/g, '"').replace(/&apos;/g, "'")
          .replace(/&amp;/g, '&');
}

console.log(`${count} in-game texts`);
// Spot-check the string this extractor was written to unblock.
for (const k of ['REBIRTH_DIALOGUE_TEXT', 'REBIRTH_DIALOGUE_TEXT2', 'REBIRTH_DIALOGUE_TEXT3']) {
  console.log(`  ${k} = ${JSON.stringify(words[k])}`);
}

if (outFile) {
  fs.writeFileSync(outFile, JSON.stringify(words));
  console.log(`-> ${outFile} (${fs.statSync(outFile).size} bytes)`);
}
