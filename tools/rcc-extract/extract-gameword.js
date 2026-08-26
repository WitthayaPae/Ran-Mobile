'use strict';
//
// The client's UI string table — gameword.xml — flattened to JSON.
//
//   node extract-gameword.js --out MOBILE/assets/gameword.json
//
// gameword.xml is the file CGameTextMan loads for ID2GAMEWORD, the same
// lookups the UI code performs (LoginPage.cpp: "LOGIN_PAGE_OKCANCEL", etc.).
// Inside Gui.rcc it is already plaintext UTF-8 — the RCC container is the only
// layer of obfuscation, and rcc.js already removes it. So this is a straight
// XML parse, no AES needed (the loose-file path uses CRijndael; the shipped
// build ships it packed in the RCC instead).
//
// Output: { "WORD_ID": ["value0", "value1", ...], ... }, so the runtime looks
// a control's text up by the SAME id the PC passes to ID2GAMEWORD — the
// strings are the client's, not transcribed.
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
  if ((e.name || e).toLowerCase() === 'gameword.xml') { entry = e; break; }
}
if (!entry) { console.error('gameword.xml not found in Gui.rcc'); process.exit(1); }

const xml = arc.read(entry).toString('utf8');

// Each entry: <WORD Ver=".." Id="KEY"> <VALUE ... Index="n">text</VALUE> ... </WORD>
const words = {};
// Entries are <WORD> or <SENTENSE> (COPYRIGHT_COMPANY is a SENTENSE), each with
// one or more <VALUE Lang="..."> children. Lang="Common" holds the active
// (Thai) text — kr/en are other locales — so only Common is taken, indexed by
// Index when present else document order. Fall back to the first VALUE if a
// record has no Common entry.
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

console.log(`${count} words`);
// Spot-check the login page keys.
for (const k of ['LOGIN_PAGE_IDPW', 'LOGIN_PAGE_OKCANCEL', 'LOGIN_PAGE_IDSAVE_BACK',
                 'SELECT_SERVER', 'SELECT_SERVER_CONNECTQUIT']) {
  console.log(`  ${k} = ${JSON.stringify(words[k])}`);
}

if (outFile) {
  fs.writeFileSync(outFile, JSON.stringify(words));
  console.log(`-> ${outFile} (${fs.statSync(outFile).size} bytes)`);
}
