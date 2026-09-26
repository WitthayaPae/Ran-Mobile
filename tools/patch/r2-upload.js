'use strict';
//
//  Mirror the patch store's blobs to a Cloudflare R2 bucket.
//
//      node r2-upload.js [--threads 8] [--dry-run]
//
//  Why: a blob Cloudflare has dropped from its cache comes from the game
//  server's XAMPP at ~0.28 MB/s (measured 2026-09-26, against ~89 MB/s for a
//  cache hit), and on the Free plan it drops anything not asked for often - so
//  a fresh install took 30-60+ minutes. Served from R2 no request reaches the
//  game server at all.
//
//  Run after build-and-publish.js, before uploading the manifest. The manifest
//  names the bucket (blobBase, from R2_PUBLIC_BASE) and both launchers fall
//  back to the store for any blob the bucket lacks, so running this late costs
//  speed, never a failed install.
//
//  Settings: tools/patch/keys/r2.env (gitignored, like the signing key):
//
//      R2_ACCOUNT_ID=<cloudflare account id>
//      R2_ACCESS_KEY_ID=<R2 API token access key id>
//      R2_SECRET_ACCESS_KEY=<R2 API token secret>
//      R2_BUCKET=<bucket name>
//      R2_PUBLIC_BASE=https://cdn.example.com/blobs/
//
//  Objects are stored as blobs/<sha256>, content-addressed like the store, so
//  an object once uploaded is never changed and never needs uploading again.
//  S3 SigV4 is done here directly - no SDK to install.
//
const fs = require('fs');
const path = require('path');
const https = require('https');
const crypto = require('crypto');

const HERE = __dirname;
const STORE = path.join(HERE, '..', '..', 'native', 'out', 'launcher_mobile');
const args = process.argv.slice(2);
const THREADS = parseInt((args[args.indexOf('--threads') + 1] || '8'), 10) || 8;
const DRY = args.includes('--dry-run');

// ------------------------------------------------------------------ settings
const envPath = path.join(HERE, 'keys', 'r2.env');
if (!fs.existsSync(envPath)) { console.error('missing ' + envPath + ' - see the header of this file'); process.exit(1); }
const env = {};
for (const line of fs.readFileSync(envPath, 'utf8').split(/\r?\n/)) {
  const m = /^\s*([A-Z0-9_]+)\s*=\s*(.*?)\s*$/.exec(line);
  if (m) env[m[1]] = m[2];
}
for (const k of ['R2_ACCOUNT_ID', 'R2_ACCESS_KEY_ID', 'R2_SECRET_ACCESS_KEY', 'R2_BUCKET']) {
  if (!env[k]) { console.error(k + ' is not set in ' + envPath); process.exit(1); }
}
const HOST = env.R2_ACCOUNT_ID + '.r2.cloudflarestorage.com';
const BUCKET = env.R2_BUCKET;

// ------------------------------------------------------------------- SigV4
const sha256hex = (d) => crypto.createHash('sha256').update(d).digest('hex');
const hmac = (k, d) => crypto.createHmac('sha256', k).update(d).digest();
const enc = (s) => encodeURIComponent(s).replace(/[!'()*]/g, (c) => '%' + c.charCodeAt(0).toString(16).toUpperCase());

function sign(method, uriPath, query, payloadHash) {
  const now = new Date();
  const amzDate = now.toISOString().replace(/[:-]|\.\d{3}/g, '');   //  YYYYMMDDTHHMMSSZ
  const day = amzDate.slice(0, 8);
  const scope = day + '/auto/s3/aws4_request';

  const canonQuery = Object.keys(query).sort().map((k) => enc(k) + '=' + enc(query[k])).join('&');
  const canonHeaders = 'host:' + HOST + '\n' + 'x-amz-content-sha256:' + payloadHash + '\n' + 'x-amz-date:' + amzDate + '\n';
  const signed = 'host;x-amz-content-sha256;x-amz-date';
  const canon = [method, uriPath, canonQuery, canonHeaders, signed, payloadHash].join('\n');

  const toSign = ['AWS4-HMAC-SHA256', amzDate, scope, sha256hex(canon)].join('\n');
  let k = hmac('AWS4' + env.R2_SECRET_ACCESS_KEY, day);
  k = hmac(k, 'auto'); k = hmac(k, 's3'); k = hmac(k, 'aws4_request');
  const sig = crypto.createHmac('sha256', k).update(toSign).digest('hex');

  return {
    path: uriPath + (canonQuery ? '?' + canonQuery : ''),
    headers: {
      'host': HOST,
      'x-amz-date': amzDate,
      'x-amz-content-sha256': payloadHash,
      'authorization': 'AWS4-HMAC-SHA256 Credential=' + env.R2_ACCESS_KEY_ID + '/' + scope +
                       ', SignedHeaders=' + signed + ', Signature=' + sig,
    },
  };
}

const agent = new https.Agent({ keepAlive: true, maxSockets: THREADS });

function request(method, uriPath, query, extraHeaders, bodyFile, size) {
  return new Promise((resolve, reject) => {
    const s = sign(method, uriPath, query, bodyFile ? 'UNSIGNED-PAYLOAD' : sha256hex(''));
    const headers = Object.assign({}, s.headers, extraHeaders || {});
    if (bodyFile) headers['content-length'] = String(size);
    const req = https.request({ host: HOST, method, path: s.path, headers, agent }, (res) => {
      const chunks = [];
      res.on('data', (d) => chunks.push(d));
      res.on('end', () => resolve({ status: res.statusCode, body: Buffer.concat(chunks).toString('utf8') }));
      res.on('error', reject);
    });
    req.on('error', reject);
    req.setTimeout(900000, () => req.destroy(new Error('timeout')));
    if (bodyFile) fs.createReadStream(bodyFile).pipe(req);
    else req.end();
  });
}

//  every key already in the bucket under blobs/
async function listExisting() {
  const have = new Set();
  let token = null;
  for (;;) {
    const q = { 'list-type': '2', 'prefix': 'blobs/', 'max-keys': '1000' };
    if (token) q['continuation-token'] = token;
    const r = await request('GET', '/' + BUCKET, q);
    if (r.status !== 200) throw new Error('list failed: HTTP ' + r.status + ' ' + r.body.slice(0, 300));
    for (const m of r.body.matchAll(/<Key>blobs\/([0-9a-f]{64})<\/Key>/g)) have.add(m[1]);
    if (!/<IsTruncated>true<\/IsTruncated>/.test(r.body)) break;
    const t = /<NextContinuationToken>([^<]+)<\/NextContinuationToken>/.exec(r.body);
    if (!t) break;
    token = t[1].replace(/&amp;/g, '&');
  }
  return have;
}

// ---------------------------------------------------------------------- main
(async () => {
  const m = JSON.parse(fs.readFileSync(path.join(STORE, 'manifest.json'), 'utf8'));

  //  every blob a launcher fetches: parts for a split file, else the file; and the apk
  const want = new Map();
  for (const f of m.files) {
    if (Array.isArray(f.parts) && f.parts.length) for (const p of f.parts) want.set(p.sha256, p.size);
    else want.set(f.sha256, f.size);
  }
  if (m.apk && m.apk.sha256) want.set(m.apk.sha256, m.apk.size || 0);

  const have = await listExisting();
  const todo = [...want.entries()].filter(([sha]) => !have.has(sha));
  const missingLocal = todo.filter(([sha]) => !fs.existsSync(path.join(STORE, 'blobs', sha)));
  if (missingLocal.length) {
    console.error(missingLocal.length + ' blob(s) are not in the local store, e.g. ' + missingLocal[0][0]);
    process.exit(1);
  }
  const todoBytes = todo.reduce((a, [, s]) => a + s, 0);
  console.log(`manifest v${m.version}: ${want.size} blobs, ${have.size} already in ${BUCKET}, ` +
              `${todo.length} to upload (${(todoBytes / 1048576).toFixed(0)} MB)`);
  if (DRY || !todo.length) return;

  let next = 0, done = 0, bytes = 0, failed = 0;
  const t0 = Date.now();
  const tick = setInterval(() => {
    const s = (Date.now() - t0) / 1000;
    console.log(`${done}/${todo.length}  ${(bytes / 1048576).toFixed(0)} MB  ${(bytes / 1048576 / s).toFixed(1)} MB/s  failed ${failed}`);
  }, 30000);

  async function worker() {
    for (;;) {
      const i = next++;
      if (i >= todo.length) return;
      const [sha, size] = todo[i];
      const file = path.join(STORE, 'blobs', sha);
      const real = fs.statSync(file).size;
      let ok = false;
      for (let attempt = 0; attempt < 3 && !ok; ++attempt) {
        try {
          const r = await request('PUT', '/' + BUCKET + '/blobs/' + sha, {},
            { 'content-type': 'application/octet-stream', 'cache-control': 'public, max-age=31536000, immutable' },
            file, real);
          ok = (r.status === 200);
          if (!ok && attempt === 2) console.error('PUT ' + sha + ': HTTP ' + r.status + ' ' + r.body.slice(0, 200));
        } catch (e) {
          if (attempt === 2) console.error('PUT ' + sha + ': ' + e.message);
        }
      }
      if (ok) { done++; bytes += real; } else failed++;
    }
  }

  await Promise.all(Array.from({ length: THREADS }, worker));
  clearInterval(tick);
  console.log(`done: ${done} uploaded, ${failed} failed, ${(bytes / 1048576).toFixed(0)} MB in ${((Date.now() - t0) / 60000).toFixed(1)} min`);
  if (failed) process.exit(1);
})().catch((e) => { console.error(e.message); process.exit(1); });
