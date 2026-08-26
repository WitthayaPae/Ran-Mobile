// Extract the 116 login-scene tree placements from log_in.wld.
//
// The wld object section stores each placed piece as:
//   [u32 nameLen][name bytes][u32 leading][D3DXMATRIX 16f][3f trailing]
// The matrix is row-major with translation in row 3 (_41,_42,_43,_44=1). Trees
// only ever carry a uniform scale + a Y rotation, so we decompose to
// {piece, pos, scale, rotYDeg} — everything the Unity builder needs.
//
//   node tools/rcc-extract/extract-login-trees.js
//
// Writes MOBILE/unity/RanMobile/Assets/Ran/Resources/login-trees.json.

const fs = require('fs');
const path = require('path');
const wld = require('./wld.js');

const WLD = 'C:/Users/tapnu/Downloads/RAN/DEV EP9/CLIENT/data/map/log_in.wld';
const OUT = path.join(__dirname, '..', '..', 'unity', 'RanMobile',
  'Assets', 'Ran', 'Resources', 'login-trees.json');

const raw = fs.readFileSync(WLD);
const o = wld.open(raw);
const buf = o.buf;
const s = buf.toString('latin1');

const trees = [];
let i = 0;
while ((i = s.indexOf('srp_tree', i)) >= 0) {
  const nameStart = i;
  let end = nameStart;
  while (buf[end] !== 0 && end < nameStart + 40) end++;
  const name = buf.toString('latin1', nameStart, end);   // e.g. srp_tree_a.pis
  i = nameStart + 1;

  // name is nul-terminated; nameLen bytes = name.length + 1. Then a 4-byte
  // leading value, then the 16-float matrix.
  const matAt = nameStart + (name.length + 1) + 4;
  const m = [];
  for (let k = 0; k < 16; k++) m.push(buf.readFloatLE(matAt + k * 4));

  // Sanity: _44 must be 1 for a well-formed transform.
  if (Math.abs(m[15] - 1) > 0.01) continue;

  // Row-major: translation is _41,_42,_43 = m[12],m[13],m[14].
  const pos = [m[12], m[13], m[14]];
  // Uniform scale = |row0|.
  const scale = Math.hypot(m[0], m[1], m[2]);
  // Y rotation from the rotation submatrix: row0=(c,_,-s)*scale, row2=(s,_,c)*scale.
  const rotYDeg = Math.atan2(m[8], m[0]) * 180 / Math.PI;

  const piece = name.replace(/\.pis$/i, '');   // srp_tree_a / _b / _c
  trees.push({
    piece,
    pos: pos.map((v) => +v.toFixed(3)),
    scale: +scale.toFixed(4),
    rotYDeg: +rotYDeg.toFixed(2),
  });
}

const byPiece = {};
trees.forEach((t) => { byPiece[t.piece] = (byPiece[t.piece] || 0) + 1; });

fs.writeFileSync(OUT, JSON.stringify({ count: trees.length, byPiece, trees }, null, 1));
console.log(`login trees: ${trees.length} placements`, byPiece);
console.log('sample:', JSON.stringify(trees.slice(0, 3)));
console.log('wrote', OUT);
