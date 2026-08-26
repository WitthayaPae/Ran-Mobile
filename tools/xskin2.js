//  SkinWeights records, reported under the Frame that owns them.
//
//  One skin file holds every body piece for a class, and each piece names the
//  same bones, so a record found by bone name alone usually belongs to some
//  other mesh. This keeps the enclosing frame, so a piece's bind matrices can
//  be compared against what the client loaded for that piece.
//
//  usage: node xskin2.js <file.x> <frameName> [boneFilter]
const fs = require('fs');
const T = { NAME: 1, STRING: 2, INTEGER: 3, GUID: 5, INTEGER_LIST: 6, FLOAT_LIST: 7,
            OBRACE: 10, CBRACE: 11 };

const buf = fs.readFileSync(process.argv[2]);
const fb = (parseInt(buf.toString('ascii', 12, 16), 10) || 32) / 8;
let p = 16;
const tok = [];
while (p + 2 <= buf.length) {
  const k = buf.readUInt16LE(p); p += 2;
  if (k === T.NAME || k === T.STRING) {
    const n = buf.readUInt32LE(p); p += 4;
    const s = buf.toString('latin1', p, p + n).replace(/\0+$/, ''); p += n;
    if (k === T.STRING) p += 2;
    tok.push({ k, s });
  } else if (k === T.INTEGER) { tok.push({ k, v: buf.readUInt32LE(p) }); p += 4; }
  else if (k === T.INTEGER_LIST) {
    const n = buf.readUInt32LE(p); p += 4;
    const a = []; for (let i = 0; i < n; ++i) a.push(buf.readUInt32LE(p + i * 4));
    p += n * 4; tok.push({ k, a });
  } else if (k === T.FLOAT_LIST) {
    const n = buf.readUInt32LE(p); p += 4;
    const a = [];
    for (let i = 0; i < n; ++i) a.push(fb === 4 ? buf.readFloatLE(p + i * 4) : buf.readDoubleLE(p + i * 8));
    p += n * fb; tok.push({ k, a });
  } else if (k === T.GUID) { p += 16; tok.push({ k }); }
  else tok.push({ k });
}

const wantFrame = process.argv[3];
const boneFilter = process.argv[4];

//  One left-to-right pass. Every open brace pushes a label; the current frame
//  is the nearest enclosing label that came from a Frame.
const stack = [];
let i = 0;
function currentFrame() {
  //  Meshes hang in an unnamed child frame, so the piece is the nearest
  //  frame that actually carries a name.
  for (let k = stack.length - 1; k >= 0; --k)
    if (stack[k].frame && stack[k].name !== "(unnamed)") return stack[k].name;
  return '(root)';
}

while (i < tok.length) {
  const t = tok[i];
  if (t.k === T.CBRACE) { stack.pop(); ++i; continue; }
  if (t.k !== T.NAME) { ++i; continue; }

  let j = i + 1, inst = null;
  if (tok[j] && tok[j].k === T.NAME) { inst = tok[j].s; ++j; }
  if (!tok[j] || tok[j].k !== T.OBRACE) { ++i; continue; }

  if (t.s === 'SkinWeights' && currentFrame() === wantFrame) {
    let q = j + 1;
    const name = tok[q] && tok[q].k === T.STRING ? tok[q].s : '?';
    ++q;
    const ints = [], flo = [];
    for (let d = 0; q < tok.length; ++q) {
      if (tok[q].k === T.OBRACE) { ++d; continue; }
      if (tok[q].k === T.CBRACE) { if (d === 0) break; --d; continue; }
      if (tok[q].k === T.INTEGER_LIST) ints.push(...tok[q].a);
      else if (tok[q].k === T.INTEGER) ints.push(tok[q].v);
      else if (tok[q].k === T.FLOAT_LIST) flo.push(...tok[q].a);
    }
    if (!boneFilter || name.includes(boneFilter)) {
      const m = flo.slice(flo.length - 16);
      console.log(name.padEnd(22) + ' n=' + String(ints[0]).padStart(3) +
                  ' off=(' + m.slice(12, 15).map(v => v.toFixed(2)).join(', ') + ')');
    }
  }

  stack.push({ name: inst === null ? '(unnamed)' : inst, frame: t.s === 'Frame' });
  i = j + 1;
}
