'use strict';
//
//  One command from a source edit to an uploadable store.
//
//      node build-and-publish.js [--verify] [--fsck] [...]
//
//  MAKE-PATCH.bat calls this. Everything after it is a file copy.
//
//  It exists because publishing a code change by hand is four steps in a fixed
//  order, and getting one of them wrong is silent: forget to bump
//  android:versionCode and every client skips the new binary; forget to
//  repackage and the manifest publishes yesterday's. make-manifest.js refuses
//  to build in both cases - this stops them happening in the first place.
//
//  What it does, in order:
//
//    1. builds libran.so for both ABIs (ninja, so a no-op when nothing changed)
//    2. if anything the APK contains is newer than the APK - either .so, or
//       anything under android/ - bumps android:versionCode and repackages
//    3. runs make-manifest.js, which stores the APK as a blob and writes the
//       manifest
//
//  A data-only change therefore costs one ninja no-op and nothing else: the
//  APK is untouched, its versionCode does not move, and clients already on it
//  are not offered a reinstall.
//
const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');

/* --------------------------------------------------------------------- root */
function findRoot(dir) {
  for (let i = 0; i < 8; i++) {
    const has = n => fs.existsSync(path.join(dir, n));
    if (has('MOBILE') && (has('CLIENT') || has('Ran'))) return dir;
    const up = path.dirname(dir);
    if (up === dir) break;
    dir = up;
  }
  console.error('expected an ancestor directory containing MOBILE/ and CLIENT/ or Ran/');
  process.exit(1);
}
const ROOT   = findRoot(__dirname);
const NATIVE = path.join(ROOT, 'MOBILE/native');
const AMF    = path.join(NATIVE, 'android/AndroidManifest.xml');
const ABIS   = ['arm64-v8a', 'x86_64'];

/*  Two numbers, and they are not the same thing.
 *
 *  android:versionCode is Android's own: an integer that only ever goes up,
 *  and the only thing the platform and the launcher compare. Nobody sees it.
 *
 *  android:versionName is the release label - V001, V002 - and it is what the
 *  file is named after, because the APK is handed to people directly and
 *  "ran-phase3.apk" tells them nothing about what they have.                  */
function manifestXml() { return fs.readFileSync(AMF, 'utf8'); }
function versionName(src) {
  const m = /android:versionName="([^"]*)"/.exec(src || manifestXml());
  if (!m) throw new Error('no android:versionName in ' + AMF);
  return m[1];
}
function apkName(src) { return 'RanOnline' + versionName(src) + '.apk'; }
function apkPath(src) { return path.join(NATIVE, 'out', apkName(src)); }

/* --------------------------------------------------------------------- bash
   build.sh and build-apk.sh are shell scripts, and this runs from a .bat. Git
   for Windows is what everything else in this tree assumes, so look where it
   installs before giving up.                                                 */
function findBash() {
  if (process.env.RAN_BASH && fs.existsSync(process.env.RAN_BASH)) return process.env.RAN_BASH;
  const guesses = [
    'C:/Program Files/Git/bin/bash.exe',
    'C:/Program Files (x86)/Git/bin/bash.exe',
    process.env.ProgramW6432 ? process.env.ProgramW6432 + '/Git/bin/bash.exe' : null,
    '/bin/bash', '/usr/bin/bash',
  ].filter(Boolean);
  for (const g of guesses) if (fs.existsSync(g)) return g;
  const w = spawnSync(process.platform === 'win32' ? 'where' : 'which', ['bash'],
                      { encoding: 'utf8' });
  if (w.status === 0) {
    const first = String(w.stdout).split(/\r?\n/).find(Boolean);
    if (first && fs.existsSync(first)) return first;
  }
  return null;
}
const BASH = findBash();
if (!BASH) {
  console.error('no bash found. Install Git for Windows, or set RAN_BASH to bash.exe.');
  process.exit(1);
}

function sh(script, env) {
  const r = spawnSync(BASH, ['-lc', script], {
    cwd: NATIVE, stdio: ['ignore', 'pipe', 'pipe'], encoding: 'utf8',
    env: Object.assign({}, process.env, env || {}),
  });
  return { out: (r.stdout || '') + (r.stderr || ''), status: r.status };
}

/*  build.sh prints "--- errors: N  failed: M" as its last line whatever the
    pipeline's exit status ends up being, so that is what to believe.         */
function build(abi) {
  process.stdout.write('  ' + abi.padEnd(12));
  const r = sh('./build.sh', abi === 'arm64-v8a' ? {} : { ABI: abi });
  const m = /---\s*errors:\s*(\d+)\s+failed:\s*(\d+)/.exec(r.out);
  if (!m) {
    console.log('FAILED');
    console.error(r.out.split('\n').slice(-25).join('\n'));
    throw new Error('build.sh produced no result line for ' + abi);
  }
  const errors = +m[1], failed = +m[2];
  if (errors || failed) {
    console.log('FAILED  (' + errors + ' errors, ' + failed + ' targets)');
    const lines = r.out.split('\n').filter(l => /error:|FAILED/.test(l)).slice(0, 20);
    console.error(lines.join('\n'));
    throw new Error('compile errors in ' + abi);
  }
  console.log('ok');
}

/* ---------------------------------------------------------------- staleness
   Anything the APK carries: the two libraries, and the whole android/ tree -
   the Java, the manifest and the resources all end up inside it too, and a
   launcher-only change would otherwise never be packaged.                    */
function newestInput() {
  let newest = 0, who = '';
  const note = (p) => {
    let st; try { st = fs.statSync(p); } catch (e) { return; }
    if (st.mtimeMs > newest) { newest = st.mtimeMs; who = path.relative(NATIVE, p); }
  };
  for (const abi of ABIS) note(path.join(NATIVE, 'out', abi, 'libran.so'));
  (function walk(dir) {
    let entries; try { entries = fs.readdirSync(dir, { withFileTypes: true }); } catch (e) { return; }
    for (const e of entries) {
      const p = path.join(dir, e.name);
      if (e.isDirectory()) walk(p); else note(p);
    }
  })(path.join(NATIVE, 'android'));
  return { at: newest, who: who };
}

/*  Both numbers move together on a release: the integer Android compares, and
    the label the file is named after. A versionName that is not V<digits> is
    left alone - someone has named this release deliberately.                  */
function bumpVersion(bumpName) {
  let src = manifestXml();
  const c = /android:versionCode="(\d+)"/.exec(src);
  if (!c) throw new Error('no android:versionCode in ' + AMF);
  const codeWas = parseInt(c[1], 10), codeNow = codeWas + 1;
  src = src.replace(c[0], 'android:versionCode="' + codeNow + '"');

  const nameWas = versionName(src);
  let nameNow = nameWas;
  /*  The label only moves when there was already an APK carrying it. Renaming
      the scheme, or a first build, publishes V001 rather than skipping to V002 -
      while versionCode still goes up, because Android compares that one and it
      may never repeat.                                                        */
  const v = bumpName ? /^V(\d+)$/.exec(nameWas) : null;
  if (v) {
    const n = parseInt(v[1], 10) + 1;
    nameNow = 'V' + String(n).padStart(v[1].length, '0');
    src = src.replace('android:versionName="' + nameWas + '"',
                      'android:versionName="' + nameNow + '"');
  }
  fs.writeFileSync(AMF, src);
  return { codeWas: codeWas, codeNow: codeNow, nameWas: nameWas, nameNow: nameNow };
}

/*  Remove everything in a directory except a named keep-set, and say what went.
    Used on both out/ and the store: in each, anything not on the list is a
    leftover, and out/ is build output, so nothing there is precious.          */
function sweep(dir, keep, label) {
  if (!fs.existsSync(dir)) return 0;
  let n = 0;
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    if (keep.has(e.name)) continue;
    try {
      fs.rmSync(path.join(dir, e.name), { recursive: true, force: true });
      console.log('      removed ' + label + e.name);
      n++;
    } catch (err) { /* leave anything that will not go */ }
  }
  return n;
}

/*  Anything in the store that is not the manifest, its signature, or the blob
    directory. Nothing puts files there, so whatever turns up is left over - an
    old archive, a half-finished upload, a stray copy - and it would be
    published along with the rest.

    The manifest and its signature are deliberately spared: make-manifest.js
    reads the old manifest to work out the next version number, and deleting it
    here would silently reset the store to version 1.                          */
function sweepStore() {
  return sweep(path.join(NATIVE, 'out/launcher_mobile'),
               new Set(['blobs', 'manifest.json', 'manifest.sig']),
               'launcher_mobile/');
}

/*  out/ holds two things worth keeping and a lot that is not.
 *
 *  The two ABI directories stay, and that is deliberate rather than an
 *  oversight: they are ninja's build trees. Delete them and the next run
 *  recompiles the whole client from scratch, which also makes libran.so newer
 *  than the APK - so it would bump versionCode and hand every player a 320 MB
 *  reinstall of a binary that did not change.
 *
 *  Everything else goes. out/apk is build-apk.sh's staging area, which it wipes
 *  and recreates on every run anyway; the rest is screenshots, logs and pulled
 *  files from testing.                                                        */
function sweepOut() {
  const apk = apkName();
  return sweep(path.join(NATIVE, 'out'),
               new Set(['launcher_mobile', apk, apk + '.idsig',
                        'arm64-v8a', 'x86_64', 'ref']),
               'out/');
}

/* ------------------------------------------------------------------- run it */
let passthrough = process.argv.slice(2);

/*  Stale blobs go by default: the previous content of a file that has since
    changed, and every superseded APK, which is 320 MB apiece. They are only
    worth keeping to republish an older manifest, and that is not what this
    script is for. --keep-stale opts out.

    Pruning happens after the manifest is written, not before - "stale" means
    "not named by the manifest we just built", so the new manifest has to exist
    before anything can be judged against it.                                  */
const keepStale = passthrough.includes('--keep-stale');
passthrough = passthrough.filter(a => a !== '--keep-stale');
if (!keepStale && !passthrough.includes('--prune')) passthrough.push('--prune');

console.log('');
console.log('[1/3] building libran.so');
for (const abi of ABIS) build(abi);

const input = newestInput();
const APK   = apkPath();
const apkAt = fs.existsSync(APK) ? fs.statSync(APK).mtimeMs : 0;

console.log('');
if (input.at > apkAt) {
  console.log('[2/3] ' + (apkAt === 0 ? 'no APK for ' + versionName() + ' yet'
                                      : input.who + ' is newer than the APK'));
  const v = bumpVersion(apkAt !== 0);
  console.log('      versionCode ' + v.codeWas + ' -> ' + v.codeNow +
              (v.nameNow !== v.nameWas ? '   release ' + v.nameWas + ' -> ' + v.nameNow : ''));
  const base = path.basename(apkPath(), '.apk');
  process.stdout.write('      packaging   ');
  const r = sh('NAME=' + base + ' ABIS="arm64-v8a x86_64" ./build-apk.sh');
  const line = r.out.split('\n').find(l => l.indexOf(base + '.apk') >= 0 && /MB/.test(l));
  if (!line) {
    console.log('FAILED');
    console.error(r.out.split('\n').slice(-25).join('\n'));
    process.exit(1);
  }
  console.log(line.trim());
} else {
  console.log('[2/3] no code change - ' + apkName() + ' is current, version untouched');
}

console.log('');
console.log('[3/3] building the payload' + (keepStale ? '  (keeping stale blobs)' : ''));
const swept = sweepOut() + sweepStore();
if (swept) console.log('      swept ' + swept + ' leftover(s)');
console.log('');
const mk = spawnSync(process.execPath,
                     [path.join(ROOT, 'MOBILE/tools/patch/make-manifest.js')].concat(passthrough),
                     { cwd: ROOT, stdio: 'inherit' });
process.exit(mk.status === null ? 1 : mk.status);
