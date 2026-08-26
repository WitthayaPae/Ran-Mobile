'use strict';
//
// Traffic census: enter the world, sit still, and report what the server
// actually sends. Used to decide which broadcasts the world model must handle,
// from measured traffic rather than from guessing at the 1615 message IDs.
//
//   node census.js [seconds]
//
const fs = require('fs');
const path = require('path');
const { Session } = require('./session');
const P = require('../spike/protocol');

const cfg = JSON.parse(
  fs.readFileSync(path.join(__dirname, '..', 'spike', 'local.json'), 'utf8'));
const seconds = Number(process.argv[2] || 30);

(async () => {
  const s = new Session({
    host: cfg.host, port: cfg.port || 12004,
    user: cfg.user, pass: cfg.pass,
    allowRemote: cfg.allowRemote, pwMode: cfg.pwMode || 'plain',
  });

  const counts = new Map();   // "role type" -> {n, bytes}
  s.on('message', (m) => {
    const k = `${m.role}\t${m.type}`;
    const e = counts.get(k) || { n: 0, bytes: 0 };
    e.n++; e.bytes += m.size;
    counts.set(k, e);
  });
  s.on('error', (e) => console.log('ERROR', e.message));

  await s.run();
  if (s.state !== 'playing') {
    console.log('not in world; aborting census');
    s.close();
    process.exit(1);
  }
  console.log(`in world, observing ${seconds}s while standing still...\n`);
  counts.clear();
  await new Promise((r) => setTimeout(r, seconds * 1000));

  const rows = [...counts.entries()]
    .map(([k, v]) => {
      const [role, type] = k.split('\t');
      return { role, type: Number(type), ...v };
    })
    .sort((a, b) => b.n - a.n);

  console.log('count   bytes  socket  message');
  console.log('-----  ------  ------  -------------------------------------');
  for (const r of rows) {
    console.log(
      `${String(r.n).padStart(5)}  ${String(r.bytes).padStart(6)}  ` +
      `${r.role.padEnd(6)}  ${P.nameOfType(r.type)} (${r.type})`);
  }
  const total = rows.reduce((a, r) => a + r.n, 0);
  const bytes = rows.reduce((a, r) => a + r.bytes, 0);
  console.log(`\n${total} messages, ${bytes} bytes in ${seconds}s ` +
              `(${(bytes / seconds).toFixed(0)} B/s idle)`);

  s.close();
  process.exit(0);
})();
