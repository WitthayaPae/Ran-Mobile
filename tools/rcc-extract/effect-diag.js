'use strict';
// Trace one .egp walk, byte by byte, plus a hex dump around wherever it stops.
// `node effect-diag.js AAA101_A.egp`
const path = require('path');
const { RccArchive } = require('./rcc');
const B = require('./bytecrypt');
const E = require('./effect-egp');

const RAN = path.join(__dirname, '..', '..', '..', 'Ran');
const ar = new RccArchive(path.join(RAN, 'data/effect/Effect.rcc'));
const name = process.argv[2];
const buf = ar.read(name);

const head = B.readHeader(buf);
const ver = buf.readUInt32LE(132);
console.log(`${name}: ${buf.length} bytes, type "${head.type}" fileVer ${head.version} ver 0x${ver.toString(16)}`);
const res = E.parse(buf, name, { trace: (s) => console.log(s) });
console.log(`ok=${res.ok} trailing=${res.trailing} error=${res.error} nodes=${(res.nodes || []).length}`);

let body = buf;
if (ver >= 0x0200) { body = Buffer.from(buf); B.decode(body, 'EMBYTECRYPT_EGP', 136); }
const at = res.at != null ? res.at : body.length - (res.trailing || 0);
const from = Math.max(0, at - 64);
console.log(`\nhex around ${at}:`);
for (let i = from; i < Math.min(body.length, at + 128); i += 16) {
  const row = body.subarray(i, i + 16);
  const mark = i <= at && at < i + 16 ? ' <<<' : '';
  console.log(`${String(i).padStart(6)}  ${row.toString('hex').replace(/(..)/g, '$1 ')} ${row.toString('latin1').replace(/[^\x20-\x7e]/g, '.')}${mark}`);
}
