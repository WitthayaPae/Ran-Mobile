'use strict';
//
//  Publish the iOS build as an AltStore source, so a phone updates itself.
//
//      node make-ios-source.js <RanLegacyM-unsigned.ipa> [--notes "what changed"]
//
//  iOS will not run code that was not signed into the bundle, so a code change
//  cannot arrive through the data patcher the way an Android one does - that
//  part is enforced by the kernel and there is no way around it. What CAN be
//  removed is the manual step: instead of a .ipa being handed over and pushed
//  with Sideloadly every time, the phone checks a source and offers the update
//  itself.
//
//  An AltStore/SideStore source is a JSON file listing apps and where their
//  .ipa files live. The store re-signs whatever it downloads with the user's
//  own Apple ID, so the unsigned bundle the CI job produces is exactly what it
//  wants. The user adds the source URL once; after that an update is a tap.
//
//  Both files go into the same out/launcher_mobile tree the data patch is
//  published from, so one upload serves both.
//
//  Schema per https://faq.altstore.io/developers/make-a-source - required at
//  the top level are `apps` and `news`; an app requires name, bundleIdentifier,
//  developerName, localizedDescription, iconURL, versions and appPermissions;
//  a version requires version, buildVersion, date, downloadURL and size.
//
const fs   = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

/* ------------------------------------------------------------------ inputs */
const args = process.argv.slice(2);
const ipaPath = args.find(a => !a.startsWith('--'));
if (!ipaPath || !fs.existsSync(ipaPath)) {
  console.error('usage: node make-ios-source.js <path to .ipa> [--notes "..."]');
  process.exit(1);
}
const notesAt = args.indexOf('--notes');
const notes = notesAt >= 0 ? (args[notesAt + 1] || '') : '';

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
const ROOT  = findRoot(__dirname);
const STORE = path.join(ROOT, 'MOBILE/native/out/launcher_mobile');

/*  The same base the client already patches from. One host, one upload.
 *  Overridable, because a test server is a different machine.               */
const BASE = process.env.RAN_PATCH_BASE ||
             'http://143.14.11.244:1521/launcher_mobile/';

/* ------------------------------------------------- what is inside the .ipa */
//
//  Read the version out of the bundle rather than being told it. Being told
//  is how a source ends up advertising a version the file does not contain,
//  and the phone then either refuses the update or installs it and never
//  offers the next one.
function plistFromIpa(ipa) {
  const out = execFileSync('unzip', ['-p', ipa, 'Payload/*.app/Info.plist'],
                           { maxBuffer: 1 << 24 });
  //  A built Info.plist is binary; parse both forms.
  const text = out.slice(0, 8).toString('binary').startsWith('bplist')
    ? execFileSync('python', ['-c',
        'import plistlib,sys;d=plistlib.loads(sys.stdin.buffer.read());' +
        'print("\\n".join("%s=%s"%(k,d[k]) for k in d))'],
        { input: out, maxBuffer: 1 << 24 }).toString()
    : out.toString('utf8');

  const get = key => {
    let m = new RegExp('^' + key + '=(.*)$', 'm').exec(text);
    if (m) return m[1].trim();
    m = new RegExp('<key>' + key + '</key>\\s*<string>([^<]*)</string>').exec(text);
    return m ? m[1].trim() : null;
  };
  return {
    bundleId: get('CFBundleIdentifier'),
    name:     get('CFBundleName'),
    version:  get('CFBundleShortVersionString'),
    build:    get('CFBundleVersion'),
    minOS:    get('MinimumOSVersion'),
  };
}

const info = plistFromIpa(ipaPath);
for (const k of ['bundleId', 'version', 'build']) {
  if (!info[k]) { console.error('could not read ' + k + ' from the .ipa'); process.exit(1); }
}
if (info.version === '1.0' && info.build === '1') {
  console.error('the .ipa still carries the placeholder version 1.0 (1).');
  console.error('Nothing will ever be offered as an update. Build with the');
  console.error('CMakeLists that stamps the product version.');
  process.exit(1);
}

/* ------------------------------------------------------------------ output */
const IOS_DIR = path.join(STORE, 'ios');
fs.mkdirSync(IOS_DIR, { recursive: true });

//  One name, always: the source points at it and the link never changes.
const ipaName = 'RanLegacyM.ipa';
fs.copyFileSync(ipaPath, path.join(IOS_DIR, ipaName));
const size = fs.statSync(path.join(IOS_DIR, ipaName)).size;

const source = {
  name: 'RAN Legacy M',
  subtitle: 'RAN Online EP9, on iOS',
  description: 'Updates for the RAN Legacy M client.',
  iconURL: BASE + 'ios/icon.png',
  website: '',
  tintColor: 'FFCB00',
  apps: [{
    name: info.name || 'Ran Legacy M',
    bundleIdentifier: info.bundleId,
    developerName: 'RAN Legacy M',
    subtitle: 'RAN Online EP9',
    localizedDescription:
      'The RAN Online EP9 client, compiled for iOS. Game data updates itself ' +
      'from the patch server on launch; this source carries the app itself, ' +
      'which iOS requires to be reinstalled rather than patched.',
    iconURL: BASE + 'ios/icon.png',
    tintColor: 'FFCB00',
    category: 'games',
    screenshotURLs: [],
    //  Declared because the schema requires the key. The app asks for nothing
    //  privacy-sensitive: no camera, no contacts, no location.
    appPermissions: { entitlements: [], privacy: {} },
    versions: [{
      version: info.version,
      buildVersion: info.build,
      date: new Date().toISOString(),
      localizedDescription: notes || ('Build ' + info.build + '.'),
      downloadURL: BASE + 'ios/' + ipaName,
      size: size,
      minOSVersion: info.minOS || '13.0',
    }],
  }],
  news: [],
};

/*  Keep older versions listed. AltStore shows a version history, and a source
 *  that forgets everything but the newest cannot offer a rollback when a build
 *  turns out to be bad.                                                      */
const OUT = path.join(IOS_DIR, 'source.json');
if (fs.existsSync(OUT)) {
  try {
    const prev = JSON.parse(fs.readFileSync(OUT, 'utf8'));
    const old = ((prev.apps || [])[0] || {}).versions || [];
    const keep = old.filter(v => v.buildVersion !== info.build);
    source.apps[0].versions = source.apps[0].versions.concat(keep).slice(0, 20);
  } catch (e) {
    console.warn('previous source.json unreadable, starting a new history: ' + e.message);
  }
}

fs.writeFileSync(OUT, JSON.stringify(source, null, 2));

console.log('iOS source written');
console.log('  app      ' + info.name + '  ' + info.version + ' (' + info.build + ')');
console.log('  bundle   ' + info.bundleId);
console.log('  ipa      ' + path.relative(ROOT, path.join(IOS_DIR, ipaName)) +
            '  ' + (size / 1048576).toFixed(1) + ' MB');
console.log('  versions ' + source.apps[0].versions.length + ' listed');
console.log('  source   ' + BASE + 'ios/source.json');
console.log('');
console.log('Upload out/launcher_mobile as usual - ios/ rides along with it.');
console.log('In AltStore or SideStore: Sources -> + -> ' + BASE + 'ios/source.json');
