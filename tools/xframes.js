//  Walk a binary .x file's token stream and report its Frame objects.
//
//  Ground truth for the skeleton: an object instance is a template name token,
//  optionally an instance name token, then an open brace. Whether a Frame
//  carries a name decides whether the client makes a bone of it, so this says
//  what the file actually contains rather than what a parser made of it.
//
//  usage: node xframes.js <file.x> [--list]
const fs = require('fs');

const T = { NAME: 1, STRING: 2, INTEGER: 3, GUID: 5, INTEGER_LIST: 6, FLOAT_LIST: 7,
            OBRACE: 10, CBRACE: 11, OPAREN: 12, CPAREN: 13, OBRACKET: 14, CBRACKET: 15,
            OANGLE: 16, CANGLE: 17, DOT: 18, COMMA: 19, SEMICOLON: 20, TEMPLATE: 31,
            WORD: 40, DWORD: 41, FLOAT: 42, DOUBLE: 43, CHAR: 44, UCHAR: 45, SWORD: 46,
            SDWORD: 47, VOID: 48, LPSTR: 49, UNICODE: 50, CSTRING: 51, ARRAY: 52 };

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
    if (tok === T.STRING) p += 2;                 // trailing separator token
    tokens.push({ tok, s });
  } else if (tok === T.INTEGER) {
    tokens.push({ tok, v: buf.readUInt32LE(p) }); p += 4;
  } else if (tok === T.INTEGER_LIST) {
    const n = buf.readUInt32LE(p); p += 4 + n * 4;
    tokens.push({ tok, count: n });
  } else if (tok === T.FLOAT_LIST) {
    const n = buf.readUInt32LE(p); p += 4 + n * (floatBits / 8);
    tokens.push({ tok, count: n });
  } else if (tok === T.GUID) {
    p += 16;
    tokens.push({ tok });
  } else {
    tokens.push({ tok });
  }
}

//  Objects: a NAME (the template), maybe a NAME (the instance), then OBRACE.
let frames = 0, named = 0, depth = 0;
const list = [];
for (let i = 0; i < tokens.length; ++i) {
  if (tokens[i].tok !== T.NAME) continue;
  const templ = tokens[i].s;
  let j = i + 1, instance = null;
  if (j < tokens.length && tokens[j].tok === T.NAME) { instance = tokens[j].s; ++j; }
  if (j >= tokens.length || tokens[j].tok !== T.OBRACE) continue;
  if (templ === 'Frame') {
    ++frames;
    if (instance && instance.length) ++named; else list.push('(unnamed) near token ' + i);
  }
  i = j;
}

console.log('frames: ' + frames + ', named: ' + named + ', unnamed: ' + (frames - named));
if (process.argv[3] === '--list') list.slice(0, 20).forEach(l => console.log('  ' + l));
