'use strict';
//
// What the two dominant types actually do, so the rebuild can be sized against
// behaviour rather than against a type name.
//
// SEQUENCE::PROPERTY field offsets are computed from the struct in
// DxEffectSequence.h and CHECKED: m_szTexture lands at body+280, which is the
// offset effect-fields.js independently measured from the data. If the layout
// were wrong that check would fail, so the flag/grid/blend reads below sit on
// the same proven arithmetic.
//
//   body+76  = PROPERTY start
//   +76+0    m_dwFlag        +76+176 m_fAniTime   +76+180 m_iCol
//   +76+184  m_iRow          +76+200 m_nBlend     +76+204 m_szTexture
//
const path = require('path');
const { RccArchive } = require('./rcc');
const E = require('./effect-egp');

const RAN = path.join(__dirname, '..', '..', '..', 'Ran');
const ar = new RccArchive(path.join(RAN, 'data/effect/Effect.rcc'));

// DxEffectSequence.h
const SEQ_FLAGS = [
  [0x00000001, 'USEANI'], [0x00000004, 'USEROTATE'], [0x00000020, 'USECOLLISION'],
  [0x00000100, 'USEDIRECTION'], [0x00000400, 'USETEXROTATE'], [0x00004000, 'USEGOTOCENTER'],
  [0x00020000, 'USESEQUENCELOOP'], [0x00040000, 'USEBILLBOARD'], [0x00080000, 'USEBILLBOARDUP'],
  [0x00100000, 'USELIGHTING'], [0x00200000, 'USEDYNAMICSCALE'], [0x00400000, 'USEDEFAULTPOS'],
  [0x00800000, 'NOT_WORLD_RS'], [0x01000000, 'USERANDOMLIFE'],
];

// DxEffectMesh.h
const MESH_FLAGS = [
  [0x00000001, 'USESCALE'], [0x00000004, 'USEROTATE'], [0x00000100, 'USEDIRECTION'],
  [0x00000200, 'USETEXSCALE'], [0x00000400, 'USETEXROTATE'], [0x00000800, 'USETEXMOVE'],
  [0x00001000, 'USEBLUR'], [0x00002000, 'USEBLENDMESH'], [0x00004000, 'USEGOTOCENTER'],
  [0x00008000, 'USEOTHERTEX'], [0x00010000, 'USESEQUENCE'], [0x00020000, 'USESEQUENCELOOP'],
  [0x00200000, 'USECULLNONE'], [0x00400000, 'USESIZEXYZ'], [0x00800000, 'USEHEIGHT_MESH'],
  [0x04000000, 'USENORMAL2'], [0x08000000, 'USEGROUNDTEX'],
];

const seq = { n: 0, flags: new Map(), grid: new Map(), blend: new Map(), animated: 0 };
const mesh = { n: 0, slots: new Map(), flags: new Map(), morph: 0, meshes: new Set(), morphMeshes: new Set() };
const files = { n: 0, nodes: new Map(), depth: new Map(), life: [] };
const texAll = new Set(), meshAll = new Set();

for (const e of ar.entries) {
  if (!e.name.toLowerCase().endsWith('.egp')) continue;
  const r = E.parse(ar.read(e), e.name);
  if (!r.ok) continue;
  files.n++;
  files.nodes.set(r.nodes.length, (files.nodes.get(r.nodes.length) || 0) + 1);
  let d = 0;
  for (const n of r.nodes) if (n.depth > d) d = n.depth;
  files.depth.set(d, (files.depth.get(d) || 0) + 1);

  for (const n of r.nodes) {
    for (const res of E.resources(n) || []) {
      if (res.kind === 'texture') texAll.add(res.name.toLowerCase());
      if (res.kind === 'mesh' || res.kind === 'skinchar') meshAll.add(res.name.toLowerCase());
    }
    if (n.type === 'SEQUENCE' && n.ver === 0x0102 && n.body.length >= 76 + 460) {
      seq.n++;
      const P = 76;
      const flag = n.body.readUInt32LE(P);
      for (const [bit, name] of SEQ_FLAGS) if (flag & bit) seq.flags.set(name, (seq.flags.get(name) || 0) + 1);
      const col = n.body.readInt32LE(P + 180);
      const row = n.body.readInt32LE(P + 184);
      const t = n.body.readFloatLE(P + 176);
      const g = `${col}x${row}`;
      seq.grid.set(g, (seq.grid.get(g) || 0) + 1);
      if (col * row > 1 && t > 0) seq.animated++;
      const bl = n.body.readInt32LE(P + 200);
      seq.blend.set(bl, (seq.blend.get(bl) || 0) + 1);
    }
    if (n.type === 'MESH' && n.ver === 0x0105) {
      const res = E.resources(n) || [];
      mesh.n++;
      const slots = res.filter((x) => x.kind === 'mesh').map((x) => x.name);
      mesh.slots.set(slots.length, (mesh.slots.get(slots.length) || 0) + 1);
      const flag = n.body.readUInt32LE(76);
      for (const [bit, name] of MESH_FLAGS) if (flag & bit) mesh.flags.set(name, (mesh.flags.get(name) || 0) + 1);
      // Slots 1 and 2 are morph targets and only mean anything with USEBLENDMESH.
      if (slots[0]) mesh.meshes.add(slots[0].toLowerCase());
      if (flag & 0x00002000) {
        mesh.morph++;
        for (const s of slots.slice(1)) mesh.morphMeshes.add(s.toLowerCase());
      }
    }
  }
}

const show = (m, label) => {
  console.log(label);
  for (const [k, v] of [...m.entries()].sort((a, b) => b[1] - a[1])) console.log(`  ${String(k).padEnd(18)} ${v}`);
};

console.log(`SEQUENCE 0x0102 nodes examined: ${seq.n}`);
show(seq.flags, 'flags set:');
console.log('flipbook grid (m_iCol x m_iRow):');
for (const [k, v] of [...seq.grid.entries()].sort((a, b) => b[1] - a[1]).slice(0, 12)) {
  console.log(`  ${k.padEnd(18)} ${v}`);
}
console.log(`  animated (grid > 1 and m_fAniTime > 0): ${seq.animated}`);
show(seq.blend, 'm_nBlend:');
console.log(`\nMESH 0x0105 nodes: ${mesh.n}`);
show(mesh.slots, 'mesh slots filled per node:');
show(mesh.flags, 'flags set:');
console.log(`  with USEBLENDMESH (morph actually on): ${mesh.morph}`);
console.log(`  distinct slot-0 meshes: ${mesh.meshes.size}`);
console.log(`  distinct morph-target meshes (slots 1-2, USEBLENDMESH only): ${mesh.morphMeshes.size}`);

console.log(`\nwhole corpus, from declared fields only:`);
console.log(`  distinct textures named: ${texAll.size}`);
console.log(`  distinct meshes named:   ${meshAll.size}`);
