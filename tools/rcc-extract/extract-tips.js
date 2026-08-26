'use strict';
//
// Build tips.json — the loading-tip strings the PC client's own loading screen
// is WIRED to show (NLOADINGTIP), from GLogic.rcc's tip.txt.
//
//   node extract-tips.js            write tips.json
//   node extract-tips.js --dry      parse + report, write nothing
//   node extract-tips.js --out DIR  write into DIR
//
// SOURCE, measured not guessed:
//   - The filename is data-driven, not hardcoded: CSimpleMessageMan::
//     CreateSubControl (SOURCE/Lib_ClientUI/Interface/SimpleMessageMan.cpp:48)
//     builds the path from ID2GAMEWORD("TIP_FILE") + SUBPATH::GLOGIC_FILE.
//     gameword.json's real TIP_FILE value is "tip.txt" (already staged;
//     extract-gameword.js). The archive entry is GLogic.rcc/tip.txt.
//   - FORMAT (CSimpleMessageMan::LoadMessage, SimpleMessageMan.cpp:122-171):
//     plain text, line-based. Lines accumulate with "\r\n" appended; a line
//     that is EXACTLY ";" flushes the accumulated text (with any trailing
//     "\r\n" stripped) as one tip and starts a new one. A non-empty
//     accumulator still open at EOF is flushed too. A tip's own text can
//     itself contain embedded newlines (the source wraps long tips across
//     2-3 physical lines before the ";").
//   - ENCODING: confirmed unencrypted on the real file (gamecrypt.isEncoded
//     is false; the bytes are directly readable ASCII English gameplay-hint
//     text, e.g. "Press the left mouse button to attack.").
//   - 23 tips measured in the real archive (re-counted 2026-08-22 against the
//     live parse output; an earlier pass here undercounted at 22), all plain
//     ASCII (no TIS-620/Thai transcoding needed here, unlike map/item names).
//
// A REAL, DOCUMENTED GAP — not fixed here, because it is a fact about the
// shipped client, not a bug in this extractor: CSimpleMessageMan::LoadMessage
// loads tip.txt into m_vecMESSAGE for its OWN in-game message ticker, but the
// three lines that would forward that vector into NLOADINGTIP (the loading
// screen's own tip store) are commented out (SimpleMessageMan.cpp:168-171,
// `/*NLOADINGTIP::Clear(); ... NLOADINGTIP::InsertTip(...); */`). Grepped the
// whole SOURCE tree for every other call to NLOADINGTIP::InsertTip: there is
// none. NLOADINGTHREAD::SetStep/GETSTEP (the "progress" counter) are equally
// dead — SetStep is never called and GetStep()/GETSTEP() are never read
// anywhere either, so the real PC loading screen has no working progress
// indicator, only the free-running spinner icon animation. On the shipped
// client the loading screen's own hint-icon box therefore draws with BLANK
// text — this data exists and is genuine, but is not actually reachable by
// the real, current wiring. The mobile port's RanLoadingScreen enables it
// anyway (documented there) since the data is real and the feature is
// evidently intended, not fabricated — but this is a deliberate enhancement
// choice, not a claim that a real PC screenshot shows tip text today.
//
const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc.js');
const gamecrypt = require('./gamecrypt.js');

const base = path.resolve(__dirname, '../../..');
const RAN = path.join(base, 'Ran');
const RES = path.join(base, 'MOBILE/unity/RanMobile/Assets/Ran/Resources');

/** Mirrors CSimpleMessageMan::LoadMessage's line-accumulate-until-";" parse. */
function parseTips(text) {
  // CBaseString::GetNextLine splits on \n (and/or \r\n); strip \r defensively.
  const lines = text.split('\n').map((l) => l.replace(/\r$/, ''));
  const tips = [];
  let cur = '';
  for (const line of lines) {
    if (line === ';') {
      // Strip a trailing \r\n the accumulator would have from the last
      // appended source line (we append '\n' below, not '\r\n', since \r was
      // already stripped per-line above — equivalent end state).
      const trimmed = cur.endsWith('\n') ? cur.slice(0, -1) : cur;
      if (trimmed.length > 0) tips.push(trimmed);
      cur = '';
    } else {
      cur += line + '\n';
    }
  }
  const trimmed = cur.endsWith('\n') ? cur.slice(0, -1) : cur;
  if (trimmed.length > 0) tips.push(trimmed);
  return tips;
}

function findRcc(re) {
  let found = null;
  (function scan(dir) {
    for (const it of fs.readdirSync(dir, { withFileTypes: true })) {
      if (found) return;
      const p = path.join(dir, it.name);
      if (it.isDirectory()) scan(p);
      else if (re.test(it.name)) found = p;
    }
  })(path.join(RAN, 'data'));
  return found;
}

function run() {
  const argv = process.argv.slice(2);
  const dry = argv.includes('--dry');
  const outIdx = argv.indexOf('--out');
  const outDir = outIdx !== -1 ? argv[outIdx + 1] : RES;

  const glogicRcc = findRcc(/^glogic\.rcc$/i);
  if (!glogicRcc) { console.log('! GLogic.rcc not found'); process.exit(1); }
  const arc = new RccArchive(glogicRcc);
  let raw = arc.read('tip.txt');
  if (gamecrypt.isEncoded(raw)) raw = gamecrypt.decode(raw);

  const text = raw.toString('latin1');
  const tips = parseTips(text);

  console.log(`tip.txt: ${raw.length} bytes, ${tips.length} tips parsed`);
  if (tips.length) console.log(`  first: ${JSON.stringify(tips[0])}`);

  if (dry) return;

  fs.mkdirSync(outDir, { recursive: true });
  const outPath = path.join(outDir, 'tips.json');
  const out = {
    note: 'Loading-screen hint strings (NLOADINGTIP) from GLogic.rcc/tip.txt ' +
      '(CSimpleMessageMan::LoadMessage format), unencrypted plain ASCII. ' +
      'MEASURED CAVEAT: on the real shipped client this data is loaded but ' +
      'never forwarded into NLOADINGTIP — the loading screen\'s tip box ' +
      'renders blank there (SimpleMessageMan.cpp:168-171 is commented out, ' +
      'and no other call site exists). See extract-tips.js header.',
    tips,
  };
  fs.writeFileSync(outPath, JSON.stringify(out));
  console.log(`wrote ${outPath} (${tips.length} tips)`);
}

if (require.main === module) run();
module.exports = { parseTips };
