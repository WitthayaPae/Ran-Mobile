'use strict';
//
// Touch-target audit for the ported PC UI.
//
//   node audit-touch.js                          # Galaxy Tab S9 (the test device)
//   node audit-touch.js --w 1080 --h 2400 --dpi 395
//
// The mobile UI places controls at the PC client's own coordinates
// (`RanUiCfg.Place`, fed by `uicfg.json`) against an 800x600 reference canvas,
// match-on-height. Every control keeps exactly the rect the original gave it —
// but those rects were sized for a mouse cursor, and nothing in the pipeline
// re-checks them against a finger.
//
//   physical px = declared units * (deviceHeight / 600)     [match-height]
//   dp          = physical px * 160 / dpi
//
// Minimum is 48dp on Android, 44pt on iOS; 48dp is used here.
//
// **Which controls count is taken from the C# source, not from the id.** A first
// version guessed tap targets by name suffix and reported 96.7% failing —
// nonsense, because it swept up 1x3 window-border slices like
// `BASIC_LINE_BOX_LEFT_BUTTON`. The ids the runtime actually binds are the only
// honest population, so they are scraped from `Runtime/*.cs`.
//
const fs = require('fs');
const path = require('path');

const ASSETS = path.join(__dirname, '..', '..', 'assets');
const RUNTIME = path.join(__dirname, '..', '..', 'unity', 'com.ran.mobile.assets', 'Runtime');
const argv = process.argv.slice(2);
const val = (f, d) => { const i = argv.indexOf(f); return i === -1 ? d : Number(argv[i + 1]); };

// Galaxy Tab S9 (SM-X710), the device the perf pass was measured on.
const DEV_W = val('--w', 2560);
const DEV_H = val('--h', 1600);
const DPI = val('--dpi', 340);

const REF_W = 800;   // RanBootScreen.PcScreenW
const REF_H = 600;   // RanBootScreen.PcScreenH
const MIN_DP = 48;

// Match-on-height (matchWidthOrHeight = 1), so height drives both axes.
const SCALE = DEV_H / REF_H;
const toDp = (units) => units * SCALE * 160 / DPI;
const toMm = (units) => units * SCALE / DPI * 25.4;

// Chrome, not targets: window-frame slices, bar fills, backing plates, captions.
const CHROME = /(WINDOW_BODY|LINE_BOX|_OVERIMAGE|_BACK$|_MARK$|_PAGE$|_BOTTOM$|_TITLE$|_TEXT$|_LABEL$|_ICON$|_BG$|_FRAME$)/;

/** Control ids the runtime actually references. */
function boundIds() {
  const ids = new Set();
  for (const f of fs.readdirSync(RUNTIME)) {
    if (!f.endsWith('.cs')) continue;
    const src = fs.readFileSync(path.join(RUNTIME, f), 'utf8');
    for (const m of src.matchAll(/"([A-Z][A-Z0-9_]{4,})"/g)) ids.add(m[1]);
  }
  return ids;
}

const raw = fs.readFileSync(path.join(ASSETS, 'uicfg.json'), 'utf8');
const byId = new Map();
for (const rec of raw.match(/\{[^{}]*"id"[^{}]*\}/g) || []) {
  let c;
  try { c = JSON.parse(rec); } catch { continue; }
  if (c && c.id && c.w > 0 && c.h > 0 && !byId.has(c.id)) byId.set(c.id, c);
}

const bound = boundIds();
const targets = [];
for (const id of bound) {
  const c = byId.get(id);
  if (!c || CHROME.test(id)) continue;
  targets.push({
    id, w: c.w, h: c.h,
    wDp: toDp(c.w), hDp: toDp(c.h),
    minDp: Math.min(toDp(c.w), toDp(c.h)),
    mm: Math.min(toMm(c.w), toMm(c.h)),
  });
}
targets.sort((a, b) => a.minDp - b.minDp);
const failing = targets.filter((t) => t.minDp < MIN_DP);

console.log(`device ${DEV_W}x${DEV_H} @ ${DPI}dpi | reference ${REF_W}x${REF_H}, ` +
            `match-height, scale x${SCALE.toFixed(3)}`);
console.log(`control ids bound by the runtime: ${bound.size}`);
console.log(`  of those, real tap targets with a rect: ${targets.length}`);
console.log(`  BELOW the ${MIN_DP}dp minimum: ${failing.length}` +
            (targets.length ? ` (${(100 * failing.length / targets.length).toFixed(0)}%)` : ''));

if (targets.length) {
  console.log('\n   dp WxH        mm  units      control');
  console.log('  ----------  ----  ---------  -----------------------------------');
  for (const t of targets) {
    const flag = t.minDp < MIN_DP ? '!' : ' ';
    console.log(` ${flag}${(t.wDp.toFixed(0) + 'x' + t.hDp.toFixed(0)).padStart(10)}  ` +
                `${t.mm.toFixed(1).padStart(4)}  ${(t.w + 'x' + t.h).padEnd(9)}  ${t.id}`);
  }
}

if (failing.length) {
  const worst = failing[0];
  const shortSide = Math.min(worst.w, worst.h);
  const neededScale = MIN_DP * DPI / 160 / shortSide;
  console.log(`\nSmallest target ${worst.id} is ${worst.mm.toFixed(1)}mm on its short side; ` +
              `the ergonomics figure behind 48dp is ~9mm.`);
  console.log(`Clearing ${MIN_DP}dp on it by canvas scale alone would need ` +
              `${(neededScale / SCALE).toFixed(1)}x the current scale, which magnifies every`);
  console.log('panel with it. The fix is a per-control hit area larger than the sprite —');
  console.log('the visual can stay PC-sized while the collider meets the minimum.');
}
process.exit(0);
