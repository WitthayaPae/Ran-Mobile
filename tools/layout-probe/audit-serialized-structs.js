'use strict';
//
// Find every struct that is read or written to a file as one block and whose
// layout differs between the Win32 build that wrote the data and a 64-bit build.
//
// Two things change size: a pointer member (4 -> 8, plus alignment padding) and
// anything spelled size_t / long / size_type. A record containing either can
// never be moved with ReadBuffer(&s, sizeof(s)) on mobile — the reads that did
// cost this port a full day of chasing symptoms (a bogus allocation of
// 8389754676365913933 bytes, a map whose effect list walked off the end).
//
// Usage: node audit-serialized-structs.js [SOURCE-root]
//
const fs = require('fs');
const path = require('path');

const root = process.argv[2] || 'C:/Users/tapnu/Downloads/RAN/DEV EP9/SOURCE';

const files = [];
(function walk(dir) {
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    if (e.name === '_Build' || e.name === 'Debug' || e.name === 'Release') continue;
    const p = path.join(dir, e.name);
    if (e.isDirectory()) walk(p);
    else if (/\.(cpp|h)$/i.test(e.name)) files.push(p);
  }
})(root);

// ---------------------------------------------------------------- structs
// name -> { file, members: [raw declaration lines] }
const structs = new Map();
const structRe = /(?:^|\n)\s*(?:typedef\s+)?(?:struct|class)\s+([A-Za-z_]\w*)[^\n;{]*\{/g;

for (const f of files) {
  const src = fs.readFileSync(f, 'utf8');
  let m;
  structRe.lastIndex = 0;
  while ((m = structRe.exec(src)) !== null) {
    const name = m[1];
    // Take the body up to the matching close brace, tracking nesting.
    let depth = 1, i = m.index + m[0].length;
    for (; i < src.length && depth > 0; ++i) {
      if (src[i] === '{') ++depth;
      else if (src[i] === '}') --depth;
    }
    const body = src.slice(m.index + m[0].length, i - 1);
    if (!structs.has(name)) structs.set(name, { file: path.relative(root, f), body });
  }
}

// A member line that changes size on LP64.
function widthRisks(body) {
  const risks = [];
  for (let line of body.split('\n')) {
    line = line.replace(/\/\/.*$/, '').trim();
    if (!line || line.startsWith('#')) continue;
    if (/\)\s*(const)?\s*[;{]/.test(line)) continue;      // a function declaration
    if (/^(public|protected|private)\s*:/.test(line)) continue;
    if (/^(typedef|using|enum|friend|static\s+const)\b/.test(line)) continue;

    if (/^[A-Za-z_][\w:<>,\s]*\*+\s*\w+\s*(\[[^\]]*\])?\s*;/.test(line)) risks.push(['pointer', line]);
    else if (/\b(size_t|ptrdiff_t|size_type|intptr_t|uintptr_t|DWORD_PTR)\b/.test(line)) risks.push(['word-sized', line]);
    else if (/^\s*(unsigned\s+)?long\s+\w+\s*(\[[^\]]*\])?\s*;/.test(line)) risks.push(['long', line]);
  }
  return risks;
}

// ------------------------------------------------------- bulk read/write
const callRe = /\b(ReadBuffer|WriteBuffer)\s*\(\s*([^;]*?)sizeof\s*\(\s*([A-Za-z_]\w*)\s*\)/g;
const hits = [];

for (const f of files) {
  const src = fs.readFileSync(f, 'utf8');
  const lines = src.split('\n');
  let m;
  callRe.lastIndex = 0;
  while ((m = callRe.exec(src)) !== null) {
    const type = m[3];
    const s = structs.get(type);
    if (!s) continue;
    const risks = widthRisks(s.body);
    if (!risks.length) continue;
    const line = src.slice(0, m.index).split('\n').length;
    hits.push({ file: path.relative(root, f), line, call: m[1], type, risks, def: s.file });
  }
}

// ------------------------------------------------------------------ report
if (!hits.length) {
  console.log('no struct read or written whole changes width on a 64-bit build');
} else {
  const byType = new Map();
  for (const h of hits) {
    if (!byType.has(h.type)) byType.set(h.type, []);
    byType.get(h.type).push(h);
  }
  console.log(`${hits.length} bulk reads/writes across ${byType.size} risky structs\n`);
  for (const [type, list] of [...byType].sort((a, b) => b[1].length - a[1].length)) {
    const risks = list[0].risks;
    console.log(`${type}  (${list[0].def})`);
    for (const [kind, line] of risks) console.log(`    ${kind.padEnd(11)} ${line}`);
    for (const h of list) console.log(`    -> ${h.call} at ${h.file}:${h.line}`);
    console.log('');
  }
}
