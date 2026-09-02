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

/*  The development root - the "DEV EP9" directory - found by walking up from
    this script until a directory holds the trees that define it. Everything
    else hangs off that, so the checkout can live anywhere on any machine and
    the script can be moved without recounting ../ hops.                       */
function findRoot(from) {
  let dir = from;
  for (;;) {
    const has = n => fs.existsSync(path.join(dir, n));
    if (has('MOBILE') && (has('CLIENT') || has('Ran'))) return dir;
    const up = path.dirname(dir);
    if (up === dir) {
      console.error('cannot find the development root above ' + from);
      console.error('expected an ancestor directory containing MOBILE/ and CLIENT/ or Ran/');
      process.exit(2);
    }
    dir = up;
  }
}

const ROOT = findRoot(HERE);
const CLIENT = path.join(ROOT, 'CLIENT');
const OUT = path.join(ROOT, 'MOBILE/native/out/launcher_mobile');

/* ------------------------------------------------------------------ what ships
   Paths are relative to the device root, /sdcard/ran, so the patcher never has
   to translate: an entry's "path" is exactly where it lands.                  */
const SHIP = [
  /*  Root config. Small, and the only place server addresses live.            */
  { file: 'config.ini'  },
  { file: 'param.ini'   },
  /*  Seeded, not shipped. option.ini is the one file in this list the CLIENT
      writes: it is where a player's settings live, and the launcher replaces
      any file whose hash does not match the manifest. Shipping it normally
      reset everyone's graphics, sound and gameplay options on every patch.
      With seed:true it is installed when absent - so a fresh install still
      starts on sane defaults, the way the PC client ships one - and never
      touched again.                                                          */
  { file: 'option.ini', seed: true },
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

  /*  Everything above is under data/. These are not, and leaving them out meant
      a client provisioned only by the patcher had no item icons, no interface
      art, no sound and no version file - which nobody noticed because every
      device so far was seeded by push-data.sh instead.                        */
  { dir: 'textures' },
  { dir: 'sounds'   },

  /*  The version the login compares (g_szClientVerFile in s_NetClient.cpp).
      cFileList.bin sits beside it and is the PC launcher's own bookkeeping -
      nothing in the client reads it, so it is not shipped.                    */
  { file: 'cVer.bin' },
];

/*  cache/ is deliberately absent: it is the font cache, created by the client
    itself (DxResponseMan CreateDirectory, DxFontMan::SetPath) and written at
    runtime. Shipping one would be stale the moment a font changed.            */

/*  data/glogicserver is deliberately absent: the shipped PC client has no
    such directory. CLIENT/ is a development tree carrying both client and
    server data, so its presence there means nothing.                          */

const NEVER = [
  /\.bak(_|-|\.|$)/i, /_bak$/i,      /_BACKUP/i,      /_RECOVERED/i,
  /สำเนา/,  //  "สำเนา" - Explorer's Thai for "copy"
  / - copy(\.|$)/i, /^copy of /i,
  /^test\.effskin$/i,                //  a stray test asset in data/effect/char
  /_PRISTINE/i,       /_backup_/i,   /_removed_not_in_maplist/i,
  /ep9bak/i,          /Eo9Bak/i,     /ep1bak/i,       /_unreadable_/i,
  /_ep1import_bak/i,
  /\.tmp$/i,          /\.log$/i,     /^thumbs\.db$/i, /^\.ds_store$/i,
  /^RanMapZipTemp$/i, /^RccAniBinTemp$/i,   //  runtime scratch, never shipped
];

/*  The shipped PC client, used only to check this list - never as a source of
    files. It is a real working install, so anything under its data/ that the
    manifest does not carry is a gap.                                          */
const REFERENCE = path.join(ROOT, 'Ran');
/*  Walked over the WHOLE reference client, not just its data/ - which is how
    textures/ (2.8 GB) and sounds/ went unnoticed for the entire port. These are
    the parts of a PC install that have no business on a phone.               */
const REF_SKIP = [
  /^editor$/i, /^RanMapZipTemp$/i, /^RccAniBinTemp$/i,
  /^GMTool$/i, /^Hackshield$/i, /^Logs$/i, /^cache$/i,
  /\.exe$/i, /\.dll$/i, /\.url$/i, /\.dat$/i,
  /^cFileList\.bin$/i,       //  the PC launcher's own bookkeeping
  /^Launcher\.URS$/i,        //  likewise - nothing in the client reads it
  /^option\.ini$/i,          //  seeded, and the reference copy is somebody's settings
];

/* --------------------------------------------------------------------- args */
const argv = process.argv.slice(2);
const arg = (name, fallback) => {
  const i = argv.indexOf('--' + name);
  return i >= 0 && argv[i + 1] ? argv[i + 1] : fallback;
};
const versionArg = parseInt(arg('version', ''), 10);
const minApk = parseInt(arg('min-apk', '1'), 10);
/*  One name, every release: a download link to it never has to be reissued, and
    nobody has to work out which of several files is current. Which release it
    is lives inside, in versionCode and versionName.                           */
const apkArg = arg('apk', path.join(ROOT, 'MOBILE/native/out/RanMobile.apk'));
const noApk = argv.includes('--no-apk');

/*  The previous manifest, if this store has been built before. It is what the
    new version number is derived from, and what decides whether anything
    actually changed.                                                          */
const PREV = (() => {
  try { return JSON.parse(fs.readFileSync(path.join(OUT, 'manifest.json'), 'utf8')); }
  catch (e) { return null; }
})();

/* ------------------------------------------------------------------- helpers */
const excluded = name => NEVER.some(re => re.test(name));
const mb = x => (x / 1048576).toFixed(1) + ' MB';

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

/*  Copy, deliberately, even though hardlinking is free.
 *
 *  The store used to hardlink into CLIENT/ to save 1.7 GB. That makes a blob
 *  and its source the same inode, so editing a client file in place rewrites
 *  the blob holding its PREVIOUS content - the blob is still named for the old
 *  bytes and no longer hashes to its own name. Nothing reports it: the next
 *  build hashes the source, sees new content, and writes a new blob, while the
 *  old one sits there corrupt. A client that asks for it downloads it, fails
 *  the sha check, and retries forever.
 *
 *  Observed exactly that way: one appended byte to CLIENT/config.ini turned
 *  blob 56b39b16ce9f... from 1156 bytes into 1157.
 *
 *  A content-addressed store has to be immutable to be worth anything, so it
 *  gets its own copy of the bytes. --link restores the old behaviour for a
 *  throwaway store where the disk matters more than the guarantee.            */
function place(abs, dest) {
  if (fs.existsSync(dest)) return 'kept';
  if (argv.includes('--link')) {
    try { fs.linkSync(abs, dest); return 'linked'; } catch (e) {}
  }
  //  Write beside and rename, so an interrupted run cannot leave a short blob
  //  sitting under a name that says it is complete.
  const tmp = dest + '.tmp';
  fs.copyFileSync(abs, tmp);
  fs.renameSync(tmp, dest);
  return 'copied';
}

/* ---------------------------------------------------------------------- run */
console.log('root   : ' + ROOT);
console.log('client : ' + CLIENT);
console.log('output : ' + OUT);
console.log('');

/* ----------------------------------------------------- already in a pack
   A loose file whose bytes are already inside one of the shipped .rcc packs is
   dead weight: the client reads the pack, not the file beside it, because
   bGLOGIC_ZIPFILE is always on and bENGLIB_ZIPFILE follows Map.rcc - which is
   shipped. Sending both costs the player the download twice over.

   Matched on the bare filename, because that is how the reader resolves an
   entry (CUnzipper looks up zipPath + bareFilename), and then CONFIRMED by
   comparing the bytes. A name collision between two genuinely different files
   would otherwise silently drop one of them, and a missing asset is a much
   worse outcome than a duplicated one.                                        */
function packDuplicates(wanted) {
  let RccArchive;
  try { ({ RccArchive } = require('../rcc-extract/rcc.js')); }
  catch (e) {
    console.log('  (rcc reader unavailable - not checking for packed duplicates)');
    return new Set();
  }

  /*  Every .rcc this manifest ships, not just the ones named in SHIP.
      quest/, npctalk/, level/ and effect/char/ each carry their own archive -
      Quest.rcc, NpcTalk.rcc, Level.rcc, EffectChar.rcc - which arrive through
      the directory walk rather than by name. Indexing only the named packs
      meant the loose .qst, .ntk, .lev and .effskin_a sources beside them
      shipped too: 1,944 files of quest script, NPC dialogue and level data that
      the real client has never handed to a player, since Ran/ carries the
      archive alone.                                                           */
  const inPacks = new Map();
  for (const rel of wanted) {
    if (!/\.rcc$/i.test(rel)) continue;
    const item = { file: rel };
    const abs = path.join(CLIENT, item.file);
    if (!fs.existsSync(abs)) continue;
    let a;
    try { a = new RccArchive(abs); } catch (e) { continue; }
    for (const name of a.list()) {
      const bare = path.basename(name).toLowerCase();
      if (!inPacks.has(bare)) inPacks.set(bare, []);
      inPacks.get(bare).push({ pack: item.file, archive: a, entry: name });
    }
  }

  const drop = new Set();
  let bytes = 0, sameName = 0;
  for (const rel of wanted) {
    const hits = inPacks.get(path.basename(rel).toLowerCase());
    if (!hits) continue;
    sameName++;
    let loose;
    try { loose = fs.readFileSync(path.join(CLIENT, rel)); } catch (e) { continue; }
    for (const h of hits) {
      let packed;
      try { packed = h.archive.read(h.entry); } catch (e) { continue; }
      if (packed.length === loose.length && packed.equals(loose)) {
        drop.add(rel);
        bytes += loose.length;
        break;
      }
    }
  }
  if (sameName) {
    console.log('  packed already: ' + drop.size + ' of ' + sameName +
                ' name matches confirmed byte-identical, ' + mb(bytes) + ' not shipped');
    if (sameName !== drop.size)
      console.log('                  ' + (sameName - drop.size) +
                  ' kept - same name, different bytes');
  }
  return drop;
}

const wanted = [];
//  Paths the client owns once installed; see the seed note in SHIP. SHIP is
//  authored with forward slashes, which is also the form manifest paths take,
//  so these compare directly.
const seeded = new Set();
for (const item of SHIP) {
  if (item.file) {
    if (excluded(path.basename(item.file))) continue;
    if (!fs.existsSync(path.join(CLIENT, item.file))) {
      console.error('  ! missing file: ' + item.file);
      continue;
    }
    if (item.seed) seeded.add(item.file);
    wanted.push(item.file);
  } else {
    const before = wanted.length;
    walk(item.dir, wanted);
    console.log('  ' + item.dir + ': ' + (wanted.length - before) + ' files');
  }
}

let PACKED_DUP = new Set();
{
  const drop = packDuplicates(wanted);
  PACKED_DUP = drop;
  if (drop.size) {
    for (let i = wanted.length - 1; i >= 0; --i)
      if (drop.has(wanted[i])) wanted.splice(i, 1);
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
  if (seeded.has(files[files.length - 1].path)) files[files.length - 1].seed = true;
  bytes += st.size;
  if (++n % 500 === 0) process.stdout.write('  hashed ' + n + '/' + wanted.length + '\r');
}

files.sort((a, b) => a.path < b.path ? -1 : a.path > b.path ? 1 : 0);

/* ----------------------------------------------------------------- the apk
   Native code cannot travel in the payload: since Android 10 an app targeting
   API 29+ may not dlopen a library out of its own writable storage, and this
   one targets 34. So a code fix reaches a player only as a new APK, and the
   launcher installs it.

   It goes in as a blob like everything else - named by its own SHA-256 - so
   there is no path here for a manifest to choose, and nothing new to validate.
   The version numbers are read from the manifest that built it, which is the
   same file the APK's versionCode comes from, so they cannot drift.           */
const apk = (() => {
  if (noApk) return null;
  if (!fs.existsSync(apkArg)) {
    console.log('apk      : ' + apkArg + ' not found - no APK offered');
    return null;
  }
  const amf = fs.readFileSync(path.join(ROOT, 'MOBILE/native/android/AndroidManifest.xml'), 'utf8');
  const vc = /android:versionCode="(\d+)"/.exec(amf);
  const vn = /android:versionName="([^"]*)"/.exec(amf);
  if (!vc) throw new Error('no android:versionCode in AndroidManifest.xml');
  const st = fs.statSync(apkArg);

  /*  Two ways to publish an APK nobody will ever be offered, both silent and
      both easy to do. They are errors rather than warnings: a patch that looks
      published and changes nothing on the device is worse than one that
      refuses to build.

      The launcher only offers a strictly newer versionCode, so shipping a new
      binary under the old number means every client skips it, and you are left
      wondering why the fix never landed.                                      */
  const prevApk = PREV && PREV.apk ? PREV.apk : null;
  const vcNow = parseInt(vc[1], 10);
  const shaNow = sha256(apkArg);
  if (prevApk && prevApk.sha256 !== shaNow && vcNow <= prevApk.versionCode) {
    throw new Error(
      'this APK is a different build from the published one, but its versionCode is ' +
      vcNow + ', not newer than ' + prevApk.versionCode + '. No client would ever ' +
      'be offered it. Bump android:versionCode in ' +
      'MOBILE/native/android/AndroidManifest.xml and rebuild the APK.');
  }

  /*  And the other way: code rebuilt, APK not repackaged. The manifest would
      then publish yesterday's binary under today's version number.            */
  for (const abi of ['arm64-v8a', 'x86_64']) {
    const so = path.join(ROOT, 'MOBILE/native/out', abi, 'libran.so');
    if (fs.existsSync(so) && fs.statSync(so).mtimeMs > st.mtimeMs) {
      throw new Error(
        'out/' + abi + '/libran.so is newer than ' + path.basename(apkArg) +
        ', so the APK does not contain the current code. Run build-apk.sh again.');
    }
  }

  const hash = shaNow;
  place(apkArg, path.join(OUT, 'blobs', hash));
  return { versionCode: parseInt(vc[1], 10),
           versionName: vn ? vn[1] : '',
           size: st.size, sha256: hash };
})();


/* ------------------------------------------------------------------ version
   The version number is the switch that makes an already-patched client look
   at anything: it returns "up to date" the moment its local number matches,
   without inspecting a single file. Typing it by hand means one forgotten
   argument publishes an update nobody receives - so it is derived from the
   content instead.

   Same files and same hashes as the last build: keep the number, and say that
   nothing needs uploading. Anything different: one past the last. An explicit
   --version still wins, for republishing an old manifest or forcing a number. */
const changes = (() => {
  if (!PREV || !Array.isArray(PREV.files)) return null;   //  first ever build
  //  The seed flag is part of what a client is told to do with a file, so a
  //  change to it has to bump the version like a content change would -
  //  otherwise the new rule sits in a manifest nobody ever fetches.
  const key = f => f.sha256 + (f.seed ? ':seed' : '');
  const was = new Map(PREV.files.map(f => [f.path, key(f)]));
  const now = new Map(files.map(f => [f.path, key(f)]));
  //  A new APK is a reason to publish on its own: without this a build whose
  //  only change is the binary keeps the old version number, and no client
  //  ever looks at the manifest offering it.
  const apkWas = PREV.apk ? PREV.apk.sha256 : '';
  const apkNow = apk ? apk.sha256 : '';
  const added = [], changed = [], removed = [];
  for (const [p, sha] of now) {
    if (!was.has(p)) added.push(p);
    else if (was.get(p) !== sha) changed.push(p);
  }
  for (const p of was.keys()) if (!now.has(p)) removed.push(p);
  const apkChanged = apkWas !== apkNow;
  return { added, changed, removed, apkChanged,
           total: added.length + changed.length + removed.length + (apkChanged ? 1 : 0) };
})();

let version, versionWhy;
if (Number.isFinite(versionArg)) {
  version = versionArg;
  versionWhy = 'given on the command line';
} else if (!PREV) {
  version = 1;
  versionWhy = 'first build of this store';
} else if (changes && changes.total === 0) {
  version = PREV.version;
  versionWhy = 'unchanged - nothing to publish';
} else {
  version = (PREV.version || 0) + 1;
  versionWhy = 'bumped from ' + PREV.version;
}

let signed = 0;   //  signature length, 0 when the payload is unsigned
const manifest = { version: version, minApk: minApk, files: files };
if (apk) manifest.apk = apk;
fs.writeFileSync(path.join(OUT, 'manifest.json'), JSON.stringify(manifest, null, 1));

/* ------------------------------------------------------------------- sign
   The manifest is the only thing a client trusts. Every blob is checked
   against a hash that comes out of it, so whoever writes the manifest decides
   what lands on the device - and it travels over plain HTTP to a bare IP,
   which means anyone on the network path can write it. The SHA-256 check
   catches corruption and nothing else.

   So the manifest carries a signature, and the client refuses one it cannot
   verify against a key compiled into the APK. That holds even if the transport
   is plain HTTP, and even if the host itself is taken: an attacker who cannot
   sign cannot publish.

   The private key lives in tools/patch/keys/ and is gitignored. Lose it and
   you cannot publish another update without shipping a new APK - back it up
   the same way as the release keystore.                                      */
{
  const keyPath = path.join(HERE, 'keys', 'manifest-signing-key.pem');
  if (!fs.existsSync(keyPath)) {
    console.log('');
    console.log('  ******  NO SIGNING KEY  ******');
    console.log('  ' + keyPath);
    console.log('  is missing, so manifest.sig cannot be written. Every client will');
    console.log('  refuse this payload. Restore the key from your backup before');
    console.log('  uploading anything.');
    process.exitCode = 1;
  } else {
    const body = fs.readFileSync(path.join(OUT, 'manifest.json'));
    const sig = crypto.createSign('SHA256')
                      .update(body)
                      .sign(crypto.createPrivateKey(fs.readFileSync(keyPath)));
    fs.writeFileSync(path.join(OUT, 'manifest.sig'), sig.toString('base64') + '\n');
    signed = sig.length;
  }
}

const uniq = new Set(files.map(f => f.sha256)).size;
console.log('                                        ');
console.log('files    : ' + files.length + '  (' + uniq + ' unique blobs)');
console.log('payload  : ' + mb(bytes));
console.log('blobs    : ' + linked + ' linked, ' + copied + ' copied, ' + kept + ' already present');
console.log('manifest : ' + mb(fs.statSync(path.join(OUT, 'manifest.json')).size) +
            (signed ? '  + manifest.sig (' + signed + ' byte signature)' : '  UNSIGNED'));
console.log('version  : ' + version + '  (' + versionWhy + ')   minApk: ' + minApk);
console.log('apk      : ' + (apk
  ? 'versionCode ' + apk.versionCode + ' "' + apk.versionName + '", ' + mb(apk.size)
  : 'none offered'));
if (changes) {
  const show = (label, list) => {
    if (!list.length) return;
    console.log('  ' + label + ' ' + list.length);
    for (const p of list.slice(0, 8)) console.log('      ' + p);
    if (list.length > 8) console.log('      ... and ' + (list.length - 8) + ' more');
  };
  show('added  ', changes.added);
  show('changed', changes.changed);
  show('removed', changes.removed);
  if (changes.apkChanged) console.log('  apk      versionCode ' + (apk ? apk.versionCode : 'removed'));
  if (changes.total === 0)
    console.log('  nothing changed since version ' + PREV.version + ' - no upload needed');
}
console.log('');
console.log('upload the contents of ' + OUT);
console.log('to http://<host>/launcher_mobile/');

/* --------------------------------------------------------------------- fsck
   Every blob re-hashed and checked against its own name. Slow - it reads the
   whole 1.7 GB store - so it is opt-in, but it is the only thing that catches a
   blob that was corrupted after it was written. A blob that fails cannot be
   rebuilt from CLIENT/ (the source has moved on, which is how it broke), so it
   is deleted: an absent blob makes a client fail loudly on a manifest that
   names it, where a corrupt one makes it retry forever.                       */
if (argv.includes('--fsck')) {
  const blobDir = path.join(OUT, 'blobs');
  const names = fs.readdirSync(blobDir).filter(n => !n.endsWith('.tmp'));
  const live = new Set(files.map(f => f.sha256));
  let bad = [], seen = 0;
  console.log('');
  console.log('fsck     : verifying ' + names.length + ' blobs');
  for (const name of names) {
    if (sha256(path.join(blobDir, name)) !== name) bad.push(name);
    if (++seen % 500 === 0) process.stdout.write('  checked ' + seen + '/' + names.length + '\r');
  }
  process.stdout.write('                                        \r');
  if (!bad.length) {
    console.log('fsck     : all ' + names.length + ' blobs hash to their names');
  } else {
    for (const name of bad) {
      const inUse = live.has(name);
      fs.unlinkSync(path.join(blobDir, name));
      console.log('  CORRUPT ' + name + (inUse ? '  (named by THIS manifest - rebuild now)' : '  (old version, rollback point lost)'));
    }
    console.log('fsck     : ' + bad.length + ' corrupt blob(s) deleted');
  }
}

/* ----------------------------------------------------------------- unshare
   A store built by an earlier version of this script hardlinks into CLIENT/,
   so every blob is the same inode as the file it came from and an in-place
   edit rewrites it. Copying is the default now, but that only protects blobs
   written from here on - the ones already in the store stay shared until they
   are re-materialised, which is what this does. Reported free (the stat is one
   syscall), fixed only when asked, because it rewrites the whole 1.7 GB.      */
{
  const blobDir = path.join(OUT, 'blobs');
  const doIt = argv.includes('--unshare');
  let shared = [], sharedBytes = 0;
  for (const name of fs.readdirSync(blobDir)) {
    if (name.endsWith('.tmp')) continue;
    let st;
    try { st = fs.statSync(path.join(blobDir, name)); } catch (e) { continue; }
    if (st.nlink > 1) { shared.push(name); sharedBytes += st.size; }
  }
  if (shared.length) {
    console.log('');
    if (!doIt) {
      console.log('shared   : ' + shared.length + ' blob(s), ' + mb(sharedBytes) +
                  ' share an inode with CLIENT/ - an in-place edit there will');
      console.log('           corrupt them silently. --unshare rewrites them as copies.');
    } else {
      let n = 0;
      for (const name of shared) {
        const dest = path.join(blobDir, name), tmp = dest + '.tmp';
        fs.copyFileSync(dest, tmp);          //  a copy has its own inode
        fs.renameSync(tmp, dest);            //  and replaces the shared one
        if (++n % 500 === 0) process.stdout.write('  unshared ' + n + '/' + shared.length + '\r');
      }
      process.stdout.write('                                        \r');
      console.log('unshared : ' + shared.length + ' blob(s), ' + mb(sharedBytes) +
                  ' - the store no longer shares storage with CLIENT/');
    }
  }
}

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
  if (apk) need.add(apk.sha256);       //  the offered APK is referenced too
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
        else if (e.isFile() && !shipped.has(r.toLowerCase()) && !PACKED_DUP.has(r))
          missing.push(r);
      }
    })('');

    if (!missing.length) {
      console.log('verify: every file under ' + REFERENCE + ' is in the manifest.');
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
