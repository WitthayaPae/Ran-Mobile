'use strict';
//
// Which characters are actually complete enough to test with.
//
//   node pick-character.js                 top candidates
//   node pick-character.js --all           every scored character
//   node pick-character.js --check NAME    score one by name
//
// This exists because choosing a test character by eye went wrong twice, and
// both times the failure looked like a renderer bug rather than a content gap:
//
//   * `boa`     — built almost entirely from RIGID pieces, so it rendered as a
//                 head and a few floating limbs.
//   * `9lvl_m`  — every one of its 8 textures is absent from the shipped data,
//                 so it rendered as a white blob.
//
// Neither is a defect in the port. 209 of 216 multi-part characters are both
// fully textured and mostly skinned; the two picked happened to be in the
// unlucky remainder. A character is scored here on what the data can actually
// deliver, so a visual check tests the renderer rather than the content.
const fs = require('fs');
const path = require('path');

const base = path.resolve(__dirname, '../../..');
const ASSETS = path.join(base, 'MOBILE/assets');
const PROJECT = path.join(base, 'MOBILE/unity/RanMobile/Assets/Ran');

/**
 * Texture names referenced by a `.rmesh`.
 *
 * The header is 44 bytes. Reading the string pool at 40 does NOT fail — it
 * returns plausible one-character names like "s", which is worse than an error
 * because it looks like sparse data rather than a wrong offset.
 */
function meshTextures(file) {
  let fd;
  try { fd = fs.openSync(file, 'r'); } catch { return []; }
  try {
    const head = Buffer.alloc(44);
    if (fs.readSync(fd, head, 0, 44, 0) < 44) return [];
    if (head.toString('latin1', 0, 4) !== 'RMSH') return [];

    const boneCount = head.readUInt32LE(12);
    const meshCount = head.readUInt32LE(16);
    const submeshCount = head.readUInt32LE(20);
    const stringBytes = head.readUInt32LE(36);
    if (!submeshCount || !stringBytes) return [];

    const strings = Buffer.alloc(stringBytes);
    fs.readSync(fd, strings, 0, stringBytes, 44);

    const at = 44 + stringBytes + boneCount * 72 + meshCount * 40;
    const sub = Buffer.alloc(submeshCount * 12);
    if (fs.readSync(fd, sub, 0, sub.length, at) < sub.length) return [];

    const names = new Set();
    for (let i = 0; i < submeshCount; i++) {
      const off = sub.readUInt32LE(i * 12);
      if (off >= stringBytes) continue;
      let end = off;
      while (end < stringBytes && strings[end] !== 0) end++;
      const n = strings.toString('latin1', off, end).trim();
      if (n) names.add(n);
    }
    return [...names];
  } catch {
    return [];
  } finally {
    fs.closeSync(fd);
  }
}

/**
 * Whether a mesh carries skinning (header flags bit0).
 *
 * Texture completeness alone is NOT enough to judge a test character, and
 * `boa` is the proof: it scores 19/19 textures and still renders as a head and
 * a few floating limbs, because its body is RIGID pieces whose placement comes
 * from their frame transform rather than from bones. A character built mostly
 * of rigid parts exercises a different path and makes a poor check of skinning.
 */
function isSkinned(mesh) {
  const file = path.join(meshDir, mesh + '.rmesh');
  let fd;
  try { fd = fs.openSync(file, 'r'); } catch { return null; }
  try {
    const head = Buffer.alloc(12);
    if (fs.readSync(fd, head, 0, 12, 0) < 12) return null;
    return (head.readUInt32LE(8) & 1) !== 0;
  } catch { return null; } finally { fs.closeSync(fd); }
}

const chars = JSON.parse(
  fs.readFileSync(path.join(ASSETS, 'characters.json'), 'utf8')).characters;

const texDir = path.join(PROJECT, 'Textures');
const staged = new Set(fs.existsSync(texDir)
  ? fs.readdirSync(texDir).filter((f) => f.endsWith('.png')).map((f) => f.toLowerCase())
  : []);

const prefabDir = path.join(PROJECT, 'Characters');
const prefabs = new Set(fs.existsSync(prefabDir)
  ? fs.readdirSync(prefabDir).filter((f) => f.endsWith('.prefab'))
      .map((f) => f.slice(0, -7).toLowerCase())
  : []);

const meshDir = path.join(ASSETS, 'meshes');
const cache = new Map();
function texturesOf(mesh) {
  if (!cache.has(mesh)) {
    const p = path.join(meshDir, mesh + '.rmesh');
    cache.set(mesh, fs.existsSync(p) ? meshTextures(p) : null);
  }
  return cache.get(mesh);
}

const only = process.argv.indexOf('--check') > 0
  ? process.argv[process.argv.indexOf('--check') + 1] : null;

const scored = [];
let noPrefab = 0, tooFewParts = 0, missingMesh = 0;

for (const [name, c] of Object.entries(chars)) {
  const id = name.replace(/\.chf$/, '');
  if (only && id.toLowerCase() !== only.toLowerCase()) continue;

  if (!prefabs.has(id.toLowerCase())) { noPrefab++; continue; }
  const parts = c.parts || [];
  // A one-or-two-part character is usually a prop or an attachment, not
  // something that tells you whether a body assembled correctly.
  if (parts.length < 3 && !only) { tooFewParts++; continue; }

  let want = 0, got = 0, resolved = 0, rigid = 0;
  for (const p of parts) {
    const t = texturesOf(p.mesh);
    if (!t) continue;
    resolved++;
    if (isSkinned(p.mesh) === false) rigid++;
    for (const n of t) {
      want++;
      if (staged.has(n.replace(/\.[^.]+$/, '').toLowerCase() + '.png')) got++;
    }
  }
  // A character whose meshes are not all extracted cannot be judged on
  // textures alone — the missing mesh might be the whole body.
  if (resolved < parts.length) { missingMesh++; if (!only) continue; }
  if (want < 4 && !only) continue;

  scored.push({
    id, parts: parts.length, want, got,
    pct: want ? got / want : 0,
    rigid, rigidPct: parts.length ? rigid / parts.length : 0,
    skeleton: c.skeleton, clips: (c.clips || []).length,
  });
}

// Rank on texture completeness FIRST, then on how little of the character is
// rigid — a mostly-rigid character can be perfectly textured and still be a
// misleading thing to look at.
scored.sort((a, b) => b.pct - a.pct || a.rigidPct - b.rigidPct ||
                      b.parts - a.parts || b.clips - a.clips);

const complete = scored.filter((s) => s.pct === 1 && s.rigidPct < 0.5);
console.log(`${scored.length} scorable characters, ` +
            `${complete.length} fully textured and mostly skinned ` +
            `(${(100 * complete.length / Math.max(1, scored.length)).toFixed(0)}%)`);
console.log(`skipped: ${noPrefab} without a prefab, ${tooFewParts} with under 3 ` +
            `parts, ${missingMesh} with an unextracted mesh`);

const show = process.argv.includes('--all') ? scored : scored.slice(0, 10);
console.log('\n  character              parts  textures  rigid  skeleton      clips');
for (const s of show) {
  console.log(`  ${s.id.padEnd(22)} ${String(s.parts).padStart(5)}  ` +
              `${(s.got + '/' + s.want).padStart(8)}  ${String(s.rigid).padStart(5)}  ` +
              `${(s.skeleton || '?').padEnd(12)}  ` +
              `${String(s.clips).padStart(5)}${s.pct < 1 ? '   NO TEXTURES' : (s.rigidPct >= 0.5 ? '   MOSTLY RIGID' : '')}`);
}

if (complete.length && !only) {
  console.log(`\nUse: -ranChar ${complete[0].id}`);
}
