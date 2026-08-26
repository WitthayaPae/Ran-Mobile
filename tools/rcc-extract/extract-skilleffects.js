'use strict';
//
// Per-skill effect (.egp) names, from `skill.csv` in GLogic.rcc.
//
//   node extract-skilleffects.js
//   node extract-skilleffects.js --out MOBILE/unity/RanMobile/Assets/Ran/Resources/skilleffects.json
//
// The skill visual is NOT invented and needs no binary decode: the .egp effect
// filenames are DECLARED as plain columns in the same skill.csv the skill book
// reads (see extract-skills.js for the four-line-block shape). SEXT_DATA in the
// C++ (GLSkillExData.h) mirrors these exact fields:
//
//   strSELFZONE01..03  effect played AT THE CASTER when the skill is cast
//                      (the "ready"/charge burst, e.g. meongryoung_ready.egp)
//   strTARGZONE01..02  effect played AT THE TARGET on impact. ZONE01 is the
//                      generic shared shock (skill_shock.egp), ZONE02 is the
//                      skill-specific hit (miragekick_fire.egp) — prefer 02.
//   strSELFBODY        an effskin_a body aura (a different pipeline; skipped).
//
// Each group is 9 sub-columns ("strTARGZONE02 1".."9") — one variant per
// caster race/weapon. Slot 1 (index 0) is the canonical variant; race-specific
// variant selection is not modelled (over-engineering for the visual).
//
// Output: { entries: [ {m,s,c,h}, ... ] } where
//   c = cast egp basename (lowercased, no extension) or "" if none
//   h = hit  egp basename (lowercased, no extension) or "" if none
// keyed at runtime by (main<<16 | sub). Only skills with at least one of the
// two are emitted. `.egp`-only: SELFBODY effskin entries are dropped because
// RanEffect plays .egp effects, not effskins.
const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc.js');

const base = path.resolve(__dirname, '../../..');
const ARCHIVE = path.join(base, 'CLIENT/data/glogic/GLogic.rcc');

const HDR_A = 'sNATIVEID wMainID';
const HDR_B = 'emBASIC_TYPE';

function parse(text) {
  const lines = text.split(/\r?\n/);
  let header = null;
  const rows = [];
  for (let i = 0; i < lines.length; i++) {
    if (!lines[i].startsWith(HDR_A)) continue;
    if (i + 3 >= lines.length) break;
    if (!lines[i + 1].startsWith(HDR_B)) continue;
    const h = lines[i].split(',');
    if (header === null) header = h.map((s) => s.trim());
    rows.push(lines[i + 2].split(','));
    i += 3;
  }
  return { header, rows };
}

// First non-empty ".egp" value across a group's variant columns (slot 1 first).
function pickEgp(r, col, groups) {
  for (const g of groups) {
    for (let v = 1; v <= 9; v++) {
      const idx = col[`${g} ${v}`];
      if (idx === undefined) continue;
      const raw = (r[idx] || '').trim();
      if (!raw) continue;
      if (!/\.egp$/i.test(raw)) continue;         // drop effskin_a etc.
      return raw.replace(/\.egp$/i, '').toLowerCase();
    }
  }
  return '';
}

function main() {
  const arc = new RccArchive(ARCHIVE);
  const entry = arc.entries.find((e) => /skill\.csv$/i.test(e.name) || /skill\.csv/i.test(e.name));
  if (!entry) throw new Error('skill.csv not found in GLogic.rcc');

  const { header, rows } = parse(arc.read(entry).toString('latin1'));
  if (!header) throw new Error('no header block found');

  const col = {};
  header.forEach((h, i) => { col[h] = i; });
  const mainI = col[HDR_A], subI = col['sNATIVEID wSubID'];
  if (mainI === undefined || subI === undefined) throw new Error('id columns not found');

  const CAST = ['strSELFZONE01', 'strSELFZONE02', 'strSELFZONE03'];
  const HIT  = ['strTARGZONE02', 'strTARGZONE01', 'strTARG'];

  const out = [];
  let withCast = 0, withHit = 0;
  for (const r of rows) {
    const m = parseInt(r[mainI], 10), s = parseInt(r[subI], 10);
    if (!Number.isFinite(m) || !Number.isFinite(s)) continue;
    const c = pickEgp(r, col, CAST);
    const h = pickEgp(r, col, HIT);
    if (!c && !h) continue;
    if (c) withCast++;
    if (h) withHit++;
    out.push({ m, s, c, h });
  }

  console.log(`${rows.length} skills -> ${out.length} with an effect ` +
              `(${withCast} cast, ${withHit} hit)`);

  const outArg = process.argv.indexOf('--out');
  if (outArg > 0) {
    const p = process.argv[outArg + 1];
    const abs = path.isAbsolute(p) ? p : path.join(base, p);
    fs.mkdirSync(path.dirname(abs), { recursive: true });
    fs.writeFileSync(abs, JSON.stringify({ entries: out }));
    console.log(`wrote ${p} (${out.length} entries)`);
  }
  return out;
}

if (require.main === module) main();
module.exports = { parse, pickEgp };
