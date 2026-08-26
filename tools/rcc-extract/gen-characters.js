'use strict';
//
// Join the character-side extractions into one manifest Unity can build from.
//
//   node gen-characters.js --out MOBILE/assets/characters.json
//
// A shipped character is spread across four files and none of them names all
// the others:
//   .chf   -> skeleton + a list of piece names               (animtypes.json)
//   .cps   -> which .x holds the geometry, and which mesh    (pieces.json)
//   .x     -> the geometry and the bones                     (meshes/*.rmesh)
//   .cfg   -> which clips belong to that skeleton            (meshclips.json)
//
// Emitting the join once, here, keeps the Unity side from re-deriving it — and
// keeps every drop reportable, because a character missing a piece should be
// visible as a number rather than as a hole in the model.
const fs = require('fs');
const path = require('path');

const base = path.resolve(__dirname, '../../..');
const read = (p) => JSON.parse(fs.readFileSync(path.join(base, p), 'utf8'));

const types = read('MOBILE/assets/animtypes.json');
const pieces = read('MOBILE/assets/pieces.json').pieces;
const meshclips = read('MOBILE/assets/meshclips.json').meshes;
const haveMesh = new Set(fs.readdirSync(path.join(base, 'MOBILE/assets/meshes'))
  .filter(f => f.endsWith('.rmesh')).map(f => f.slice(0, -6).toLowerCase()));

const stem = (s) => s.toLowerCase().replace(/\.[a-z0-9]+$/, '');

const out = {};
const stats = {
  characters: 0, noSkeleton: 0, skeletonMissing: 0,
  pieceRefs: 0, pieceResolved: 0, pieceUnknown: 0, pieceMeshMissing: 0,
  withClips: 0, noClips: 0,
};
const pieceDrops = new Map();

for (const name of Object.keys(types.characters)) {
  const c = types.characters[name];
  if (!c.skeleton) { stats.noSkeleton++; continue; }
  const sk = stem(c.skeleton);
  if (!haveMesh.has(sk)) { stats.skeletonMissing++; continue; }

  const parts = [];
  for (const raw of c.pieces || []) {
    stats.pieceRefs++;
    // Pieces are keyed by full filename because `.aps` and `.cps` collide on
    // stem for 10 of them; fall back to the stem for anything a .chf names
    // without an extension.
    const p = raw.toLowerCase();
    const ref = pieces[p] || pieces[`${stem(raw)}.cps`] || pieces[`${stem(raw)}.aps`];
    if (!ref) {
      stats.pieceUnknown++;
      pieceDrops.set(p, 'unresolved');
      continue;
    }
    if (!ref.mesh || !haveMesh.has(ref.mesh)) {
      stats.pieceMeshMissing++;
      pieceDrops.set(p, `mesh ${ref.mesh} absent`);
      continue;
    }
    stats.pieceResolved++;
    parts.push({
      piece: p,
      mesh: ref.mesh,
      // null when the name did not match any mesh inside the .x; the Unity side
      // then takes the whole file rather than one sub-mesh, which over-draws
      // but never silently draws nothing.
      submesh: ref.submeshFound ? ref.submesh : null,
    });
  }

  // Clips are attached to the SKELETON, not to a piece. Any of the character's
  // meshes would give the same answer, so the skeleton's own entry is used and
  // the pieces are not consulted at all.
  const clips = meshclips[sk] ? meshclips[sk].clips : [];
  if (clips.length) stats.withClips++; else stats.noClips++;

  out[name] = {
    skeleton: sk,
    scale: c.scale, height: c.height, radius: c.radius,
    parts,
    clips,
  };
  stats.characters++;
}

console.log(`${stats.characters} characters written`);
console.log(`  dropped: ${stats.noSkeleton} declare no skeleton, ` +
            `${stats.skeletonMissing} skeleton not extracted`);
console.log(`  pieces: ${stats.pieceRefs} referenced, ${stats.pieceResolved} resolved ` +
            `(${(stats.pieceResolved / stats.pieceRefs * 100).toFixed(1)}%), ` +
            `${stats.pieceUnknown} unresolved, ${stats.pieceMeshMissing} mesh absent`);
console.log(`  clips: ${stats.withClips} characters have clips, ${stats.noClips} have none`);

const partCounts = Object.values(out).map(c => c.parts.length).sort((a, b) => a - b);
const empty = partCounts.filter(n => n === 0).length;
console.log(`  parts per character: min ${partCounts[0]}, ` +
            `median ${partCounts[partCounts.length >> 1]}, ` +
            `max ${partCounts[partCounts.length - 1]}, ${empty} with none`);

// Named control: the first documented character in this project. A player
// character is a face, an upper, a lower, hands, feet and hair — six pieces on
// b_w — so anything much smaller means the indirection silently dropped parts.
const sample = Object.entries(out).find(([, c]) => c.skeleton === 'b_w' && c.parts.length >= 5);
if (sample) {
  console.log(`\ncontrol ${sample[0]}: skeleton ${sample[1].skeleton}, ` +
              `${sample[1].parts.length} parts, ${sample[1].clips.length} clips`);
  for (const p of sample[1].parts.slice(0, 8))
    console.log(`  ${p.piece} -> ${p.mesh}${p.submesh ? ` [${p.submesh}]` : ' (whole file)'}`);
} else {
  console.log('\ncontrol: FAIL — no b_w character assembled with 5+ parts');
}

const outArg = process.argv.indexOf('--out');
if (outArg > 0) {
  fs.writeFileSync(path.join(base, process.argv[outArg + 1]), JSON.stringify({ characters: out }));
  console.log(`\nwrote ${process.argv[outArg + 1]}`);
}

// Unity's JsonUtility cannot deserialise a dictionary keyed by arbitrary
// strings, so it gets an array-shaped copy. Clip lists are emitted once per
// SKELETON rather than per character: 1,565 characters share 722 skeletons and
// b_w alone carries 861 clips, so inlining them per character would repeat the
// same few hundred thousand strings for no gain.
const flatArg = process.argv.indexOf('--flat');
if (flatArg > 0) {
  const skelSeen = new Map();
  const chars = [];
  for (const [name, c] of Object.entries(out)) {
    if (!skelSeen.has(c.skeleton)) skelSeen.set(c.skeleton, c.clips);
    chars.push({
      n: name.replace(/\.chf$/i, ''),
      k: c.skeleton,
      s: c.scale || 1,
      p: c.parts.map(p => ({ m: p.mesh, s: p.submesh || '' })),
    });
  }
  const skeletons = [...skelSeen].map(([n, clips]) => ({ n, c: clips }));
  const flat = { skeletons, chars };
  const p = path.join(base, process.argv[flatArg + 1]);
  fs.writeFileSync(p, JSON.stringify(flat));
  console.log(`wrote ${process.argv[flatArg + 1]} — ${chars.length} characters, ` +
              `${skeletons.length} skeletons, ${(fs.statSync(p).size / 1024).toFixed(0)} KB`);
}
