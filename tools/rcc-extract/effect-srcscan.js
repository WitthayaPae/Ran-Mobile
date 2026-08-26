'use strict';
// Which (property type, node version) branches read a DXAFFINEPARTS, and which
// read extra fields after the blitted PROPERTY. Derived from the 13
// `*_PROPERTY::LoadFile` bodies in SOURCE/Lib_Engine/DxEffect/Single/ rather
// than by eye, because the branch list is 60+ cases long.
const fs = require('fs');
const path = require('path');

const DIR = path.join(__dirname, '..', '..', '..', 'SOURCE', 'Lib_Engine', 'DxEffect', 'Single');

for (const file of fs.readdirSync(DIR).filter((f) => f.endsWith('.cpp'))) {
  const src = fs.readFileSync(path.join(DIR, file), 'latin1').replace(/\r\n/g, '\n');
  const m = src.match(/HRESULT (\w+_PROPERTY)::LoadFile \(([\s\S]*?)\n}\n/);
  if (!m) continue;
  const [, cls, body] = m;
  // split on the version dispatch
  const parts = body.split(/\n\t(?:else )?if\s*\(\s*dwVer\s*==\s*([^)]*?)\s*\)/);
  console.log(`\n== ${cls}  (${file})`);
  for (let i = 1; i < parts.length; i += 2) {
    const ver = parts[i].trim();
    const chunk = parts[i + 1];
    const affine = /DXAFFINEPARTS/.test(chunk);
    const reads = [...chunk.matchAll(/SFile\s*(?:>>\s*(\w+)|\.ReadBuffer\s*\(\s*&?([\w:.\[\]]+)\s*,\s*sizeof\s*\(\s*([\w:]+)\s*\)\s*(?:\*\s*(\w+))?)/g)]
      .map((r) => (r[1] ? `>>${r[1]}` : `blit ${r[2]}:${r[3]}${r[4] ? '*' + r[4] : ''}`));
    console.log(`  ${ver.padEnd(22)} affine=${affine ? 'Y' : '.'}  ${reads.join(' | ')}`);
  }
  const tail = parts[parts.length - 1] || '';
  if (/else\s*\{[\s\S]*?SetOffSet/.test(body)) console.log('  (unknown-version branch skips by dwSize)');
}
