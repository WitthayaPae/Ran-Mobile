'use strict';
//
// Per-CHARACTER clip lists, from animtypes.json's characters section.
//
// PC mechanism this feeds (the reason this file exists): each .chf skin
// declares which anim containers IT loads, and SELECTANI only ever picks among
// that character's OWN clips (GLCharacter.cpp GLAT_* cases -> DxSkinChar's own
// container). Baking one AnimatorController per SKELETON instead unions every
// character sharing that skeleton — which put mob_ky_01_cube's AN_ATTACK clip
// (mob_ky_sangongdock, subtype 0) into the PLAYER body o_w's attack pool,
// where the longest-clip tiebreak picked it over the player's own
// a_w_00_att01. Found 2026-08-23 during the attack-animation deep-read.
//
//   node gen-animchars.js    writes MOBILE/assets/animchars.json
//
const fs = require('fs');
const path = require('path');

const base = path.resolve(__dirname, '../..');
const src = path.join(base, 'assets/animtypes.json');
const out = path.join(base, 'assets/animchars.json');

const data = JSON.parse(fs.readFileSync(src, 'utf8'));
const chars = data.characters || {};
const result = { chars: [] };
let withClips = 0, without = 0;
for (const [key, rec] of Object.entries(chars)) {
  if (!key.toLowerCase().endsWith('.chf')) continue;
  const stem = key.slice(0, -4);
  const clips = rec.clips;
  if (!Array.isArray(clips) || clips.length === 0) { without++; continue; }
  withClips++;
  result.chars.push({ n: stem, k: (rec.skeleton || '').replace(/\.x$/i, ''), c: clips });
}
fs.writeFileSync(out, JSON.stringify(result));
console.log(`animchars: ${withClips} chars with clip lists, ${without} without, -> ${out}`);
