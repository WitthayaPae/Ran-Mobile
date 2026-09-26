'use strict';
//
//  Warm the Cloudflare cache for the patch store: GET every blob a fresh install
//  would fetch, once, and throw the bytes away.
//
//      node warm.js [--threads 8] [--base https://ran-legacy-m.com/launcher_mobile/]
//
//  Why: on the Free plan Cloudflare drops rarely-requested objects whatever the
//  cache rule's TTL says. Measured 2026-09-26: 73% of 240 random blobs were
//  MISS, and a MISS comes from the XAMPP origin at ~0.28 MB/s per file against
//  ~89 MB/s for a HIT. A fresh install then takes 30-60+ minutes.
//
//  It downloads the whole store (~4.7 GB) from the origin through Cloudflare, so
//  it uses the server's upload for as long as it runs: start it at a quiet hour.
//  A file split into parts is warmed part by part - the whole-file blob is never
//  requested by a patcher and may not be on the server at all.
//
const fs = require('fs');
const path = require('path');
const https = require('https');

const args = process.argv.slice(2);
const opt = (k, d) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : d; };
const THREADS = parseInt(opt('--threads', '8'), 10);
const BASE = opt('--base', 'https://ran-legacy-m.com/launcher_mobile/');

const MANIFEST = path.join(__dirname, '..', '..', 'native', 'out', 'launcher_mobile', 'manifest.json');
const m = JSON.parse(fs.readFileSync(MANIFEST, 'utf8'));

//  every blob a patcher asks for, once
const seen = new Map();
for (const f of m.files) {
  if (Array.isArray(f.parts) && f.parts.length) {
    for (const p of f.parts) seen.set(p.sha256, p.size);
  } else {
    seen.set(f.sha256, f.size);
  }
}
const list = [...seen.entries()];
const total = list.reduce((a, [, s]) => a + s, 0);
console.log(`manifest v${m.version}: ${list.length} blobs, ${(total / 1048576).toFixed(0)} MB, ${THREADS} at a time`);

const agent = new https.Agent({ keepAlive: true, maxSockets: THREADS });
const count = {};
let next = 0, done = 0, bytes = 0, failed = 0;
const t0 = Date.now();

function one([sha, size]) {
  return new Promise((resolve) => {
    const req = https.get(BASE + 'blobs/' + sha, { agent }, (res) => {
      const st = res.headers['cf-cache-status'] || 'none';
      count[st] = (count[st] || 0) + 1;
      if (res.statusCode !== 200) failed++;
      res.on('data', (d) => { bytes += d.length; });
      res.on('end', resolve);
      res.on('error', () => { failed++; resolve(); });
    });
    req.on('error', () => { failed++; resolve(); });
    req.setTimeout(600000, () => { req.destroy(); });
  });
}

async function worker() {
  for (;;) {
    const i = next++;
    if (i >= list.length) return;
    await one(list[i]);
    done++;
  }
}

const tick = setInterval(() => {
  const s = (Date.now() - t0) / 1000;
  console.log(`${done}/${list.length}  ${(bytes / 1048576).toFixed(0)}/${(total / 1048576).toFixed(0)} MB  ` +
    `${(bytes / 1048576 / s).toFixed(1)} MB/s  ${JSON.stringify(count)}  failed ${failed}`);
}, 30000);

Promise.all(Array.from({ length: THREADS }, worker)).then(() => {
  clearInterval(tick);
  const s = (Date.now() - t0) / 1000;
  console.log(`done: ${done} blobs, ${(bytes / 1048576).toFixed(0)} MB in ${(s / 60).toFixed(1)} min  ${JSON.stringify(count)}  failed ${failed}`);
});
