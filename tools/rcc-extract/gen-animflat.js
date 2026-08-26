'use strict';
// Flatten animtypes.json into the one shape Unity's JsonUtility can actually
// read: a serialisable array, not a dictionary keyed by arbitrary strings.
//
// The C# side previously scraped this with a regex and came out one entry short
// of the truth (8 vs 9 on a 20-clip folder), which is exactly the kind of quiet
// undercount that would have silently dropped states from a controller. Node
// owns the parse; Unity just reads a list.
const fs = require('fs');
const path = require('path');

const base = path.resolve(__dirname, '../../..');
const src = path.join(base, 'MOBILE/assets/animtypes.json');
const outArg = process.argv.indexOf('--out');
const out = outArg > 0 ? process.argv[outArg + 1]
                       : path.join(base, 'MOBILE/assets/animflat.json');

const doc = JSON.parse(fs.readFileSync(src, 'utf8'));
const clips = doc.clips;

const entries = [];
for (const name of Object.keys(clips)) {
  const c = clips[name];
  entries.push({
    n: name,
    t: c.mainType,
    s: c.subType,
    // ACF_LOOP off the .cfg — the authority for whether a state may loop.
    l: !!c.loop,
    // Upper/lower body split: the engine plays these on separate layers, so
    // they must not be chosen as a full-body state.
    u: !!c.upperBody,
    d: !!c.lowerBody,
    // First strike frame, SECONDS from clip start (0 = no strike frame).
    // PC fires the attack/skill HIT effect exactly when its attack timer
    // crosses m_sStrikeEff[n].m_dwFrame (GLCharacter::AttackProc,
    // GLCharacter.cpp:5255-5284: dwThisKey = timer*UNITANIKEY_PERSEC vs
    // m_dwFrame) — NOT at cast time. ticksPerSecond comes from the header
    // (4800); the first strike is the one the mobile hit-effect delay uses.
    st: Array.isArray(c.strikeFrames) && c.strikeFrames.length > 0
      ? +(c.strikeFrames[0] / (doc.ticksPerSecond || 4800)).toFixed(4)
      : 0,
  });
}
entries.sort((a, b) => (a.n < b.n ? -1 : a.n > b.n ? 1 : 0));

fs.writeFileSync(out, JSON.stringify({ entries }));

// Cross-check: the flat file must carry every clip the source had, and the
// mainType histogram must match. A silent drop here is the whole point of the
// rewrite, so it is checked rather than assumed.
const rt = JSON.parse(fs.readFileSync(out, 'utf8'));
if (rt.entries.length !== Object.keys(clips).length) {
  console.error(`FAIL: ${rt.entries.length} entries vs ${Object.keys(clips).length} clips`);
  process.exit(1);
}
const hist = {};
for (const e of rt.entries) hist[e.t] = (hist[e.t] || 0) + 1;
let bad = 0;
for (const name of Object.keys(clips)) {
  const e = rt.entries.find(x => x.n === name);
  if (!e || e.t !== clips[name].mainType || e.l !== !!clips[name].loop) bad++;
}
if (bad) { console.error(`FAIL: ${bad} entries disagree with source`); process.exit(1); }

const looping = rt.entries.filter(e => e.l).length;
console.log(`${rt.entries.length} clips -> ${path.relative(base, out)}`);
console.log(`  looping ${looping} (${(looping / rt.entries.length * 100).toFixed(1)}%), ` +
            `${Object.keys(hist).length} distinct mainTypes, ` +
            `${(fs.statSync(out).size / 1024).toFixed(0)} KB`);
