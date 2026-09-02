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
const APK    = path.join(NATIVE, 'out/ran-phase3.apk');
const ABIS   = ['arm64-v8a', 'x86_64'];

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

function bumpVersionCode() {
  const src = fs.readFileSync(AMF, 'utf8');
  const m = /android:versionCode="(\d+)"/.exec(src);
  if (!m) throw new Error('no android:versionCode in ' + AMF);
  const was = parseInt(m[1], 10), now = was + 1;
  fs.writeFileSync(AMF, src.replace(m[0], 'android:versionCode="' + now + '"'));
  return { was: was, now: now };
}

/* ------------------------------------------------------------------- run it */
const passthrough = process.argv.slice(2);

console.log('');
console.log('[1/3] building libran.so');
for (const abi of ABIS) build(abi);

const input = newestInput();
const apkAt = fs.existsSync(APK) ? fs.statSync(APK).mtimeMs : 0;

console.log('');
if (input.at > apkAt) {
  console.log('[2/3] ' + (apkAt === 0 ? 'no APK yet' : input.who + ' is newer than the APK'));
  const v = bumpVersionCode();
  console.log('      versionCode ' + v.was + ' -> ' + v.now);
  process.stdout.write('      packaging   ');
  const r = sh('NAME=ran-phase3 ABIS="arm64-v8a x86_64" ./build-apk.sh');
  const line = r.out.split('\n').find(l => /ran-phase3\.apk/.test(l) && /MB/.test(l));
  if (!line) {
    console.log('FAILED');
    console.error(r.out.split('\n').slice(-25).join('\n'));
    process.exit(1);
  }
  console.log(line.trim());
} else {
  console.log('[2/3] no code change - the published APK is current, versionCode untouched');
}

console.log('');
console.log('[3/3] building the payload');
console.log('');
const mk = spawnSync(process.execPath,
                     [path.join(ROOT, 'MOBILE/tools/patch/make-manifest.js')].concat(passthrough),
                     { cwd: ROOT, stdio: 'inherit' });
process.exit(mk.status === null ? 1 : mk.status);
