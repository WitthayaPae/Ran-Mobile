#!/usr/bin/env node
/*  Build the mobile patch payload: manifest.json plus a content-addressed
    blob store, ready to upload to /launcher_mobile/ on the patch host.

        node make-manifest.js --version 366 --min-apk 1
        node make-manifest.js --version 366 --verify --prune

    Output lands in MOBILE/native/out/launcher_mobile/ :

        manifest.json          version, minApk, one entry per shipped file
        blobs/<sha256>         the payload, immutable and append-only

    Why content-addressed: uploads only ever ADD files, so there is no CDN
    cache to invalidate and no window where a client can fetch a half-replaced
    file. Rolling back is republishing an older manifest - the blobs it points
    at are still there.

    Why an explicit allowlist and not a directory walk: push-data.sh copies
    whole directories, which is how 352 MB of .bak_pre_* files and 4 GB of
    loose copies of already-packed content ended up on the test device. A file
    ships because it is named here, not because it happened to be in the tree.  */

'use strict';
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const HERE = __dirname;
const CLIENT = path.resolve(HERE, '../../../CLIENT');
const OUT = path.resolve(HERE, '../../native/out/launcher_mobile');

/* ------------------------------------------------------------------ what ships
   Paths are relative to the device root, /sdcard/ran, so the patcher never has
   to translate: an entry's "path" is exactly where it lands.                  */
const SHIP = [
  /*  Root config. Small, and the only place server addresses live.            */
  { file: 'config.ini'  },
  { file: 'param.ini'   },
  { file: 'option.ini'  },
  { file: 'comment.ini' },

  /*  The packs. These ARE the game data - the client reads them, not the
      loose files beside them, because bGLOGIC_ZIPFILE is always on and
      bENGLIB_ZIPFILE follows the presence of Map.rcc.                         */
  { file: 'data/glogic/GLogic.rcc'         },
  { file: 'data/gui/Gui.rcc'               },
  { file: 'data/effect/Effect.rcc'         },
  { file: 'data/skinobject/SkinObject.rcc' },
  { file: 'data/animation/Animation.rcc'   },
  { file: 'data/map/Map.rcc'               },

  /*  Loose content that no pack covers. Verified two ways: nothing here is
      inside any .rcc, and every one of these directories is present in the
      shipped PC client under Ran/.                                            */
  { dir: 'data/skin'     },
  { dir: 'data/piece'    },
  { dir: 'data/object'   },
  { dir: 'data/skeleton' },
  { dir: 'data/help'     },

  /*  Subdirectories of packed directories. These are NOT in their pack, and
      missing them is silent: quests and NPC dialogue simply stop working.
      They were left out of the first version of this list because the check
      only looked at files directly inside data/glogic and never recursed -
      which is exactly the kind of gap --verify below exists to catch.         */
  { dir: 'data/glogic/quest'    },
  { dir: 'data/glogic/npctalk'  },
  { dir: 'data/glogic/level'    },
  { dir: 'data/glogic/activity' },
  { dir: 'data/effect/char'     },
];

/*  data/glogicserver is deliberately absent: the shipped PC client has no
    such directory. CLIENT/ is a development tree carrying both client and
    server data, so its presence there means nothing.                          */

const NEVER = [
  /\.bak(_|\.|$)/i,   /_bak$/i,      /_BACKUP/i,      /_RECOVERED/i,
  /_PRISTINE/i,       /_backup_/i,   /_removed_not_in_maplist/i,
  /ep9bak/i,          /Eo9Bak/i,     /ep1bak/i,       /_unreadable_/i,
  /_ep1import_bak/i,
  /\.tmp$/i,          /\.log$/i,     /^thumbs\.db$/i, /^\.ds_store$/i,
  /^RanMapZipTemp$/i, /^RccAniBinTemp$/i,   //  runtime scratch, never shipped
];

/*  The shipped PC client, used only to check this list - never as a source of
    files. It is a real working install, so anything under its data/ that the
    manifest does not carry is a gap.                                          */
const REFERENCE = path.resolve(HERE, '../../../Ran');
const REF_SKIP = [
  /^editor$/i, /^RanMapZipTemp$/i, /^RccAniBinTemp$/i,
];

/* --------------------------------------------------------------------- args */
const argv = process.argv.slice(2);
const arg = (name, fallback) => {
  const i = argv.indexOf('--' + name);
  return i >= 0 && argv[i + 1] ? argv[i + 1] : fallback;
};
const version = parseInt(arg('version', ''), 10);
const minApk = parseInt(arg('min-apk', '1'), 10);
if (!Number.isFinite(version)) {
  console.error('usage: node make-manifest.js --version <n> [--min-apk <n>]');
  process.exit(2);
}

/* ------------------------------------------------------------------- helpers */
const excluded = name => NEVER.some(re => re.test(name));

function walk(rel, acc) {
  const abs = path.join(CLIENT, rel);
  let entries;
  try { entries = fs.readdirSync(abs, { withFileTypes: true }); }
  catch (e) { console.error('  ! missing directory: ' + rel); return acc; }
  for (const e of entries) {
    if (excluded(e.name)) continue;
    const r = rel + '/' + e.name;
    if (e.isDirectory()) walk(r, acc);
    else if (e.isFile()) acc.push(r);
  }
  return acc;
}

function sha256(abs) {
  const h = crypto.createHash('sha256');
  const fd = fs.openSync(abs, 'r');
  const buf = Buffer.alloc(1 << 20);
  try {
    for (;;) {
      const n = fs.readSync(fd, buf, 0, buf.length, null);
      if (n <= 0) break;
      h.update(buf.subarray(0, n));
    }
  } finally { fs.closeSync(fd); }
  return h.digest('hex');
}

/*  Hardlink where the filesystem allows it, copy where it does not. The blob
    store is 1.7 GB; linking makes rebuilding it free.                         */
function place(abs, dest) {
  if (fs.existsSync(dest)) return 'kept';
  try { fs.linkSync(abs, dest); return 'linked'; }
  catch (e) { fs.copyFileSync(abs, dest); return 'copied'; }
}

/* ---------------------------------------------------------------------- run */
console.log('client : ' + CLIENT);
console.log('output : ' + OUT);
console.log('version: ' + version + '   minApk: ' + minApk);
console.log('');

const wanted = [];
for (const item of SHIP) {
  if (item.file) {
    if (excluded(path.basename(item.file))) continue;
    if (!fs.existsSync(path.join(CLIENT, item.file))) {
      console.error('  ! missing file: ' + item.file);
      continue;
    }
    wanted.push(item.file);
  } else {
    const before = wanted.length;
    walk(item.dir, wanted);
    console.log('  ' + item.dir + ': ' + (wanted.length - before) + ' files');
  }
}

fs.mkdirSync(path.join(OUT, 'blobs'), { recursive: true });

const files = [];
let bytes = 0, linked = 0, copied = 0, kept = 0;
let n = 0;
for (const rel of wanted) {
  const abs = path.join(CLIENT, rel);
  const st = fs.statSync(abs);
  const hash = sha256(abs);
  const how = place(abs, path.join(OUT, 'blobs', hash));
  if (how === 'linked') linked++; else if (how === 'copied') copied++; else kept++;
  files.push({ path: rel.replace(/\\/g, '/'), size: st.size, sha256: hash });
  bytes += st.size;
  if (++n % 500 === 0) process.stdout.write('  hashed ' + n + '/' + wanted.length + '\r');
}

files.sort((a, b) => a.path < b.path ? -1 : a.path > b.path ? 1 : 0);

const manifest = { version: version, minApk: minApk, files: files };
fs.writeFileSync(path.join(OUT, 'manifest.json'), JSON.stringify(manifest, null, 1));

const uniq = new Set(files.map(f => f.sha256)).size;
const mb = x => (x / 1048576).toFixed(1) + ' MB';
console.log('                                        ');
console.log('files    : ' + files.length + '  (' + uniq + ' unique blobs)');
console.log('payload  : ' + mb(bytes));
console.log('blobs    : ' + linked + ' linked, ' + copied + ' copied, ' + kept + ' already present');
console.log('manifest : ' + mb(fs.statSync(path.join(OUT, 'manifest.json')).size));
console.log('');
console.log('upload the contents of ' + OUT);
console.log('to http://<host>/launcher_mobile/');

/* -------------------------------------------------------------------- prune
   Blobs left behind by an earlier run: the previous content of a file that has
   since changed, or one dropped from the allowlist. No client asks for them -
   nothing in this manifest names them - but they are not junk either. They are
   what makes a rollback possible: republishing an older manifest works only for
   as long as the blobs it points at are still on the host.

   So this is opt-in, and it reports what it would remove before doing it. Prune
   when you are certain no manifest you might want to serve again refers to
   them; leave them alone otherwise, at 1.7 GB of store the space is rarely the
   binding constraint.                                                         */
{
  const blobDir = path.join(OUT, 'blobs');
  const need = new Set(files.map(f => f.sha256));
  let stale = [], staleBytes = 0;
  for (const name of fs.readdirSync(blobDir)) {
    if (need.has(name)) continue;
    stale.push(name);
    try { staleBytes += fs.statSync(path.join(blobDir, name)).size; } catch (e) {}
  }
  if (stale.length) {
    console.log('');
    if (argv.includes('--prune')) {
      for (const name of stale) fs.unlinkSync(path.join(blobDir, name));
      console.log('pruned   : ' + stale.length + ' blob(s), ' + mb(staleBytes) +
                  ' - rollback to any manifest naming them is no longer possible');
    } else {
      console.log('stale    : ' + stale.length + ' blob(s), ' + mb(staleBytes) +
                  ' not named by this manifest (kept for rollback; --prune removes them)');
    }
  }
}

/* ------------------------------------------------------------------- verify
   Walk the shipped PC client and report anything it has that this manifest
   does not. Structural only - it compares paths, not contents, because the
   two trees are patched independently and will differ by version.             */
if (argv.includes('--verify')) {
  console.log('');
  if (!fs.existsSync(REFERENCE)) {
    console.log('verify: no reference client at ' + REFERENCE + ' - skipped');
  } else {
    const shipped = new Set(files.map(f => f.path.toLowerCase()));
    const missing = [];
    (function refWalk(rel) {
      let entries;
      try { entries = fs.readdirSync(path.join(REFERENCE, rel), { withFileTypes: true }); }
      catch (e) { return; }
      for (const e of entries) {
        if (REF_SKIP.some(re => re.test(e.name))) continue;
        if (excluded(e.name)) continue;
        const r = rel ? rel + '/' + e.name : e.name;
        if (e.isDirectory()) refWalk(r);
        else if (e.isFile() && !shipped.has(r.toLowerCase())) missing.push(r);
      }
    })('data');

    if (!missing.length) {
      console.log('verify: every file under ' + REFERENCE + '/data is in the manifest.');
    } else {
      const byDir = {};
      for (const m of missing) {
        const d = m.split('/').slice(0, 3).join('/');
        byDir[d] = (byDir[d] || 0) + 1;
      }
      console.log('verify: ' + missing.length + ' file(s) in the shipped client are NOT shipped:');
      for (const [d, n] of Object.entries(byDir).sort((a, b) => b[1] - a[1]).slice(0, 12))
        console.log('   ' + String(n).padStart(6) + '  ' + d + '/');
      console.log('  (a loose file whose pack already contains it is fine; a whole');
      console.log('   directory here is a gap)');
    }
  }
}
