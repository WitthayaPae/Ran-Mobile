//  Print the SkinWeights records of a binary .x file.
//
//  A bone's offset matrix - the inverse bind pose - is the last sixteen floats
//  of its SkinWeights record, after two variable-length arrays. If a reader
//  loses its place in those arrays it reads the matrix from the wrong offset,
//  and the bone drags its vertices somewhere impossible. This prints what the
//  file says, so a loader's answer can be checked against it.
//
//  usage: node xskin.js <file.x> [nameFilter]
const fs = require('fs');

const T = { NAME: 1, STRING: 2, INTEGER: 3, GUID: 5, INTEGER_LIST: 6, FLOAT_LIST: 7,
            OBRACE: 10, CBRACE: 11 };

const buf = fs.readFileSync(process.argv[2]);
const header = buf.toString('ascii', 0, 16);
if (!/^xof 0(3|2)\d\dbin/.test(header)) { console.log('not a binary .x: ' + header); process.exit(1); }
const fbytes = (parseInt(header.slice(12, 16), 10) || 32) / 8;

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
    for (let i = 0; i < n; ++i) a.push(fbytes === 4 ? buf.readFloatLE(p + i * 4) : buf.readDoubleLE(p + i * 8));
    p += n * fbytes; tok.push({ k, a });
  } else if (k === T.GUID) { p += 16; tok.push({ k }); }
  else tok.push({ k });
}

const filter = process.argv[3];
for (let i = 0; i < tok.length; ++i) {
  if (tok[i].k !== T.NAME || tok[i].s !== 'SkinWeights') continue;
  let j = i + 1;
  if (tok[j] && tok[j].k === T.NAME) ++j;            // optional instance name
  if (!tok[j] || tok[j].k !== T.OBRACE) continue;
  ++j;

  //  The record: a string (bone name), an integer count, then the index and
  //  weight arrays, then sixteen floats. Binary .x packs the numbers into as
  //  many list tokens as it likes, so they are concatenated before slicing.
  const name = tok[j] && tok[j].k === T.STRING ? tok[j].s : '?';
  ++j;
  const ints = [], floats = [];
  for (; j < tok.length && tok[j].k !== T.CBRACE; ++j) {
    if (tok[j].k === T.INTEGER_LIST) ints.push(...tok[j].a);
    else if (tok[j].k === T.INTEGER) ints.push(tok[j].v);
    else if (tok[j].k === T.FLOAT_LIST) floats.push(...tok[j].a);
  }
  const nWeights = ints.length ? ints[0] : 0;
  const m = floats.slice(floats.length - 16);
  if (filter && !name.includes(filter)) continue;
  console.log(name.padEnd(22) + ' n=' + String(nWeights).padStart(4) +
              '  ints=' + ints.length + ' floats=' + floats.length +
              '  offsetT=(' + m.slice(12, 15).map(v => v.toFixed(2)).join(', ') + ')');
}
