'use strict';
//
// Generate DUMP_STRUCT/DUMP_MEMBER lines for the `.egp` effect PROPERTY structs.
//
//   node gen-effectprops.js > effectprops.gen.inc
//   node build-structs.js                       # compiles the probe
//
// Why this is separate from gen-structs.js: every one of these structs is a
// NESTED type called `PROPERTY` (or `PROPERTY_100`, ...) declared inside
// `<TYPE>_PROPERTY`. gen-structs.js emits unqualified names, so six headers
// each declaring a `PROPERTY` would collide into one entry and then fail to
// compile at namespace scope. Here the names are emitted qualified —
// `SEQUENCE_PROPERTY::PROPERTY` — which is both unambiguous and legal for
// offsetof, since the nested structs have no virtuals and stay standard-layout.
//
// Same principle as everything else in this directory: this script extracts
// NAMES only. Every size and offset is computed by MSVC from the real headers.
// A `char[256]` at the wrong offset invents filenames out of the tail of the
// previous string, which is exactly the failure mode EFFECTS-SCOPE.md warns
// about, so no offset here is ever hand-computed.
//
// MESH's and PARTICLESYS's legacy PROPERTY_10x structs are declared at FILE
// SCOPE in a .cpp, not in any header, so nothing can #include them. They are
// 4,120 shipped nodes (12.5% of the six types' total), so they are worth
// having. This script copies those struct declarations out VERBATIM into
// effectprops.legacy.gen.h, wrapped in a namespace to keep the six identically
// named PROPERTY_100s apart, and the probe measures them like anything else.
// Verbatim is the point: the text is moved by a script, byte for byte, and
// MSVC still computes every size and offset. Nothing is retyped.
//

const fs = require('fs');
const path = require('path');

const SRC = path.join(__dirname, '..', '..', '..', 'SOURCE');
const SINGLE = path.join(SRC, 'Lib_Engine', 'DxEffect', 'Single');

// The six types that carry 95.5% of all shipped property nodes
// (EFFECTS-SCOPE.md §3), in frequency order.
const HEADERS = [
  'DxEffectSequence.h',     // SEQUENCE     25.6%
  'DxEffectMesh.h',         // MESH         24.8%
  'DxEffectParticleSys.h',  // PARTICLESYS  14.1%
  'DxEffectMoveRotate.h',   // MOVEROTATE   13.1%
  'DxEffectGround.h',       // GROUND       11.6%
  'DxEffectBlurSys.h',      // BLURSYS       6.2%
  // Not one of the six. Measured opportunistically because sk_dfly.egp — one of
  // the five files EFFECTS-SCOPE.md §8 could not explain — stops 4 bytes short
  // with a single POINTLIGHT node in it.
  'DxEffectPointLight.h',   // POINTLIGHT    1.7%
];

const SKIP_MEMBER = /^(operator|~|enum|struct|class|union|typedef|public|private|protected|friend|static|const|inline|virtual|template)\b/;

function stripComments(src) {
  let out = '';
  let i = 0;
  while (i < src.length) {
    if (src[i] === '/' && src[i + 1] === '*') {
      const end = src.indexOf('*/', i + 2);
      out += src.slice(i, end === -1 ? src.length : end + 2).replace(/[^\n]/g, ' ');
      i = end === -1 ? src.length : end + 2;
    } else if (src[i] === '/' && src[i + 1] === '/') {
      const end = src.indexOf('\n', i);
      i = end === -1 ? src.length : end;
    } else {
      out += src[i++];
    }
  }
  return out;
}

// Length-PRESERVING comment blanking. The legacy extraction below slices the
// original text by offsets found in the stripped text, so the two must stay in
// lockstep; stripComments() drops line comments and shifts every offset after
// them, which silently lifts struct bodies starting four lines too early.
function blankComments(src) {
  let out = '';
  let i = 0;
  while (i < src.length) {
    if (src[i] === '/' && src[i + 1] === '*') {
      const end = src.indexOf('*/', i + 2);
      const stop = end === -1 ? src.length : end + 2;
      out += src.slice(i, stop).replace(/[^\n]/g, ' ');
      i = stop;
    } else if (src[i] === '/' && src[i + 1] === '/') {
      let end = src.indexOf('\n', i);
      if (end === -1) end = src.length;
      out += ' '.repeat(end - i);
      i = end;
    } else {
      out += src[i++];
    }
  }
  return out;
}

/** Body text of `struct NAME {...}` / `class NAME {...}`, with its span. */
function findStructs(src) {
  const found = [];
  const re = /\b(?:struct|class)\s+([A-Za-z_]\w*)\s*(?::[^{]*)?\{/g;
  let m;
  while ((m = re.exec(src)) !== null) {
    let depth = 1;
    let i = re.lastIndex;
    const start = i;
    while (i < src.length && depth > 0) {
      if (src[i] === '{') depth++;
      else if (src[i] === '}') depth--;
      i++;
    }
    found.push({ name: m[1], body: src.slice(start, i - 1), from: m.index, to: i });
  }
  return found;
}

// Data members declared directly in this struct body. Same rules as
// gen-structs.js, including the comma-declarator split that a silent bug once
// made necessary (`DWORD a, b;` used to yield only `b`).
//
// The declared TYPE SPELLING is captured alongside the name and written to
// effectprops.types.json. That is still a name, not a value: nothing here
// computes a size. The consumer pairs the spelling with the compiler's measured
// size and refuses to decode if the two disagree (float that is not 4 bytes,
// D3DXVECTOR3 that is not 12, ...), so a mis-parsed declaration cannot quietly
// turn into a wrong reader — it turns into an error.
function findMembers(body) {
  let flat = '';
  let depth = 0;
  for (const ch of body) {
    if (ch === '{') depth++;
    else if (ch === '}') depth--;
    else if (depth === 0) flat += ch;
  }
  const members = [];
  const seen = new Set();
  for (const raw of flat.split(';')) {
    const decl = raw.trim().replace(/\s+/g, ' ');
    if (!decl || decl.includes('(') || decl.includes(':')) continue;
    if (SKIP_MEMBER.test(decl)) continue;
    const parts = decl.includes('<') || decl.includes('>')
      ? [decl]
      : decl.split(',').map((p) => p.trim()).filter(Boolean);
    // A comma continuation carries no type of its own: `float a, b;` gives b
    // the type of the whole declaration.
    const baseType = (parts[0].match(/^([A-Za-z_][\w:]*)\s/) || [])[1] || null;
    parts.forEach((part, i) => {
      const m = part.match(/([A-Za-z_]\w*)\s*((?:\[[^\]]*\])*)\s*$/);
      if (!m) return;
      if (i === 0 && part.split(' ').length < 2) return;
      if (seen.has(m[1])) return;
      seen.add(m[1]);
      members.push({ name: m[1], type: baseType, array: m[2] || '' });
    });
  }
  return members;
}

const out = [];
const types = {};
let nStruct = 0;
let nField = 0;

for (const h of HEADERS) {
  const src = stripComments(fs.readFileSync(path.join(SINGLE, h), 'utf8'));
  const all = findStructs(src);
  const outers = all.filter((s) => /_PROPERTY$/.test(s.name));
  for (const outer of outers) {
    // Nested = declared strictly inside the outer struct's span.
    const nested = all.filter(
      (s) => s !== outer && s.from > outer.from && s.to <= outer.to && /^PROPERTY/.test(s.name));
    for (const n of nested) {
      const members = findMembers(n.body);
      if (!members.length) continue;
      const q = `${outer.name}::${n.name}`;
      out.push(`DUMP_STRUCT(${q});`);
      types[q] = {};
      for (const f of members) {
        out.push(`DUMP_MEMBER(${q}, ${f.name});`);
        types[q][f.name] = f.type + f.array;
      }
      out.push('END_STRUCT();');
      nStruct++;
      nField += members.length;
    }
  }
}

// ---- legacy PROPERTY_10x structs, declared at file scope in a .cpp ----------
//
// Copied verbatim into a generated header. `struct NAME ... };` is lifted with
// its braces balanced, so the constructors come along and the declarations are
// exactly what the compiler in the real build saw. Comments are NOT stripped
// from the copied text — only from the text this script scans — so nothing that
// is commented out in the original can become live in the copy.
const LEGACY = [
  { file: 'DxEffectMeshPROP.cpp', ns: 'EFFLEGACY_MESH', needs: 'DxEffectMesh.h' },
  { file: 'DxEffectParticleSysPROP.cpp', ns: 'EFFLEGACY_PARTICLESYS', needs: 'DxEffectParticleSys.h' },
];

const legacyOut = [
  '// GENERATED by gen-effectprops.js — do not edit.',
  '// Verbatim copies of the file-scope PROPERTY_10x structs declared in',
  '// DxEffect/Single/*PROP.cpp, which no header exposes. Wrapped in a namespace',
  '// each, because every one of these files names its oldest struct PROPERTY_100.',
  '#pragma once',
];

for (const lg of LEGACY) {
  const raw = fs.readFileSync(path.join(SINGLE, lg.file), 'latin1');
  const scan = blankComments(raw);   // same length as raw — offsets are shared
  legacyOut.push(`namespace ${lg.ns} {`);
  // File scope only: `struct NAME` starting in column 0.
  const re = /^struct\s+([A-Za-z_]\w*)/gm;
  let m;
  while ((m = re.exec(scan)) !== null) {
    let i = scan.indexOf('{', m.index);
    if (i === -1) continue;
    let depth = 1;
    i++;
    while (i < scan.length && depth > 0) {
      if (scan[i] === '{') depth++;
      else if (scan[i] === '}') depth--;
      i++;
    }
    const end = scan.indexOf(';', i - 1);
    if (end === -1) continue;
    legacyOut.push(raw.slice(m.index, end + 1));   // verbatim, comments intact
    types[`${lg.ns}::${m[1]}`] = {};
    const members = findMembers(scan.slice(scan.indexOf('{', m.index) + 1, i - 1));
    out.push(`DUMP_STRUCT(${lg.ns}::${m[1]});`);
    for (const f of members) {
      out.push(`DUMP_MEMBER(${lg.ns}::${m[1]}, ${f.name});`);
      types[`${lg.ns}::${m[1]}`][f.name] = f.type + f.array;
    }
    out.push('END_STRUCT();');
    nStruct++;
    nField += members.length;
  }
  legacyOut.push(`} // namespace ${lg.ns}`);
}

fs.writeFileSync(path.join(__dirname, 'effectprops.legacy.gen.h'),
                 legacyOut.join('\n') + '\n');

fs.writeFileSync(path.join(__dirname, 'effectprops.types.json'),
                 JSON.stringify({
                   note: 'GENERATED by gen-effectprops.js — declared type SPELLINGS only. '
                       + 'All sizes and offsets live in layout.json and come from MSVC.',
                   types,
                 }, null, 1) + '\n');

console.log('// GENERATED by gen-effectprops.js — do not edit.');
console.log(`// ${nStruct} nested effect PROPERTY structs, ${nField} members.`);
console.log('// Sizes and offsets are computed by the compiler, never by a script.');
console.log(out.join('\n'));
