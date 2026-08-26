//  Print the Frame tree of a binary .x file.
//
//  The client builds its skeleton by recursing through nested Frame objects, so
//  when a bone ends up transformed as though it hung off the root, the first
//  thing to establish is where the file actually puts it. This walks the token
//  stream and reports each Frame with its depth and its parent, which is ground
//  truth no parser sits in front of.
//
//  usage: node xtree.js <file.x> [nameFilter]
const fs = require('fs');

const T = { NAME: 1, STRING: 2, INTEGER: 3, GUID: 5, INTEGER_LIST: 6, FLOAT_LIST: 7,
            OBRACE: 10, CBRACE: 11 };

const buf = fs.readFileSync(process.argv[2]);
const header = buf.toString('ascii', 0, 16);
if (!/^xof 0(3|2)\d\dbin/.test(header)) { console.log('not a binary .x: ' + header); process.exit(1); }
const floatBits = parseInt(header.slice(12, 16), 10) || 32;

let p = 16;
const tokens = [];
while (p + 2 <= buf.length) {
  const tok = buf.readUInt16LE(p); p += 2;
  if (tok === T.NAME || tok === T.STRING) {
    const n = buf.readUInt32LE(p); p += 4;
    const s = buf.toString('latin1', p, p + n).replace(/\0+$/, '');
    p += n;
    if (tok === T.STRING) p += 2;
    tokens.push({ tok, s });
  } else if (tok === T.INTEGER) { tokens.push({ tok, v: buf.readUInt32LE(p) }); p += 4; }
  else if (tok === T.INTEGER_LIST) { const n = buf.readUInt32LE(p); p += 4 + n * 4; tokens.push({ tok }); }
  else if (tok === T.FLOAT_LIST) { const n = buf.readUInt32LE(p); p += 4 + n * (floatBits / 8); tokens.push({ tok }); }
  else if (tok === T.GUID) { p += 16; tokens.push({ tok }); }
  else tokens.push({ tok });
}

//  Walk braces, tracking the enclosing Frame at each level.
const filter = process.argv[3];
const stack = [];                 // {name, isFrame}
const seen = [];
let inTemplate = false;
for (let i = 0; i < tokens.length; ++i) {
  const t = tokens[i];
  if (t.tok === T.OBRACE) { if (!stack.length || stack[stack.length - 1].pending === undefined) stack.push({ name: '?', isFrame: false }); continue; }
  if (t.tok === T.CBRACE) { stack.pop(); continue; }
  if (t.tok !== T.NAME) continue;

  const templ = t.s;
  let j = i + 1, instance = null;
  if (j < tokens.length && tokens[j].tok === T.NAME) { instance = tokens[j].s; ++j; }
  if (j >= tokens.length || tokens[j].tok !== T.OBRACE) continue;

  const isFrame = templ === 'Frame';
  //  Parent is the nearest enclosing Frame.
  let parent = '(root)';
  for (let k = stack.length - 1; k >= 0; --k) if (stack[k].isFrame) { parent = stack[k].name; break; }
  const name = instance === null ? '(unnamed)' : (instance === '' ? '(empty)' : instance);
  if (isFrame) seen.push({ depth: stack.filter(s => s.isFrame).length, name, parent });
  stack.push({ name, isFrame });
  i = j;                          // consume through the brace
}

let n = 0;
for (const f of seen) {
  if (filter && !f.name.includes(filter) && !f.parent.includes(filter)) continue;
  console.log('  '.repeat(f.depth) + f.name + '   <- ' + f.parent);
  ++n;
}
console.log('frames: ' + seen.length + (filter ? ', shown: ' + n : ''));
