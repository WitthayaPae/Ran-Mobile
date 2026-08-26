'use strict';
//
// Build help.json — the in-game manual CHelpWindow browses (SOURCE/Lib_ClientUI/
// Interface/HelpWindow.cpp + HelpDataMan.cpp), from GLogic.rcc's ran_help.hlp
// (CHelpDataMan::LoadFile, HelpDataMan.cpp:414-465).
//
//   node extract-help.js            write help.json
//   node extract-help.js --dry      parse + report, write nothing
//   node extract-help.js --out DIR  write into DIR
//
// FORMAT (measured). LoadFile passes EMBYTECRYPT_NONE (no byte-crypt table) and
// never calls SFile.GetFileType() itself, which reads as "no CSerialFile
// header" — but the real file DOES carry the standard 132-byte prelude (type
// string "default" + a version DWORD, both zero/unused here): byte-inspecting
// the raw archive entry shows the WORD version (1) and DWORD topCount sitting
// at absolute offsets 132/134, not 0/2. GLOGIC::openfile_basestream() (the
// stream opener LoadFile calls) evidently skips that header itself before
// handing back the stream, independent of whether the specific LoadFile body
// reads it back out via GetFileType(). Body layout from offset 132:
//   WORD version (SHELPNODE::VER = 1, confirmed byte-for-byte on the real file)
//   DWORD topCount
//   topCount x SHELPNODE (recursive, SHELPNODE::LoadFile, HelpDataMan.cpp:56-97):
//     DWORD titleLen (INCLUDING the NUL — this one raw-buffers a length-prefixed
//       string manually rather than going through CSerialFile's std::string
//       operator, but keeps the same "length includes NUL" convention)
//     titleLen bytes (NUL-terminated)
//     DWORD contentLen (including NUL)
//     contentLen bytes (NUL-terminated)
//     DWORD subCount
//     subCount x SHELPNODE (recurse)
//
// Confirmed unencrypted: gamecrypt.isEncoded() is false on the raw archive
// entry and the bytes are directly readable text once past the counts (e.g.
// the real file's first node title is "Movements").

const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc.js');
const gamecrypt = require('./gamecrypt.js');

const base = path.resolve(__dirname, '../../..');
const RAN = path.join(base, 'Ran');
const RES = path.join(base, 'MOBILE/unity/RanMobile/Assets/Ran/Resources');

const BODY_OFFSET = 132;

class Cursor {
  constructor(buf, at) { this.b = buf; this.p = at; }
  get left() { return this.b.length - this.p; }
  _need(n) { if (this.p + n > this.b.length) throw new Error(`help: read past end (need ${n}, have ${this.left})`); }
  u16() { this._need(2); const v = this.b.readUInt16LE(this.p); this.p += 2; return v; }
  u32() { this._need(4); const v = this.b.readUInt32LE(this.p); this.p += 4; return v; }
  // length-prefixed (INCLUDING NUL), latin1 (raw cp874 bytes, decode client-side)
  str() {
    const n = this.u32();
    if (n === 0 || n > 1 << 22) throw new Error(`help: implausible string length ${n}`);
    this._need(n);
    const raw = this.b.slice(this.p, this.p + n); this.p += n;
    const z = raw.indexOf(0);
    return (z < 0 ? raw : raw.slice(0, z)).toString('latin1');
  }
}

function readNode(c, depth, stats) {
  if (depth > 32) throw new Error('help: runaway recursion');
  const title = c.str();
  const content = c.str();
  const subCount = c.u32();
  if (subCount > 100000) throw new Error(`help: implausible subCount ${subCount}`);
  stats.nodes++;
  const sub = [];
  for (let i = 0; i < subCount; i++) sub.push(readNode(c, depth + 1, stats));
  return { title, content, sub };
}

function parse(raw) {
  let buf = raw;
  if (gamecrypt.isEncoded(buf)) buf = gamecrypt.decode(buf);

  const c = new Cursor(buf, BODY_OFFSET);
  const ver = c.u16();
  if (ver !== 1) throw new Error(`help: unknown SHELPNODE version ${ver}`);
  const topCount = c.u32();
  if (topCount > 100000) throw new Error(`help: implausible topCount ${topCount}`);

  const stats = { nodes: 0 };
  const roots = [];
  for (let i = 0; i < topCount; i++) roots.push(readNode(c, 0, stats));

  return { ver, topCount, roots, nodes: stats.nodes, consumed: c.p, eofExact: c.p === buf.length, trailing: buf.length - c.p };
}

function findRcc(re) {
  let found = null;
  (function scan(dir) {
    for (const it of fs.readdirSync(dir, { withFileTypes: true })) {
      if (found) return;
      const p = path.join(dir, it.name);
      if (it.isDirectory()) scan(p);
      else if (re.test(it.name)) found = p;
    }
  })(path.join(RAN, 'data'));
  return found;
}

function run() {
  const argv = process.argv.slice(2);
  const dry = argv.includes('--dry');
  const outIdx = argv.indexOf('--out');
  const outDir = outIdx !== -1 ? argv[outIdx + 1] : RES;

  const glogicRcc = findRcc(/^glogic\.rcc$/i);
  if (!glogicRcc) { console.log('! GLogic.rcc not found'); process.exit(1); }
  const arc = new RccArchive(glogicRcc);
  const raw = arc.read('ran_help.hlp');
  const parsed = parse(raw);

  console.log(`ran_help.hlp: ver=${parsed.ver} topCount=${parsed.topCount} totalNodes=${parsed.nodes} ` +
    `EOF-exact=${parsed.eofExact} (trailing ${parsed.trailing})`);

  if (dry) return;

  fs.mkdirSync(outDir, { recursive: true });
  const outPath = path.join(outDir, 'help.json');
  const out = {
    note: 'In-game manual (HelpWindow) from GLogic.rcc/ran_help.hlp, ' +
      'CHelpDataMan::LoadFile — a title/content tree, unencrypted. ' +
      'Text is raw cp874, decode with RanText.DecodeCp874.',
    roots: parsed.roots,
  };
  fs.writeFileSync(outPath, JSON.stringify(out));
  console.log(`wrote ${outPath} (${parsed.nodes} nodes)`);
}

if (require.main === module) run();
module.exports = { parse };
