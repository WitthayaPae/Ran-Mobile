'use strict';
//
// Stage the PC client's audio — per-map background music + the sound-effect set
// the game logic addresses by keyword — into the Unity project, plus a manifest
// (audio.json) that binds each SFX event keyword to its file.
//
//   node stage-audio.js            stage the BGM + SFX set and (re)write audio.json
//   node stage-audio.js --dry      report what WOULD be staged, write nothing
//
// WHERE THE AUDIO LIVES, AND HOW IT IS ADDRESSED ON PC
// ----------------------------------------------------
// Two source trees under CLIENT/sounds (the dev tree — the deploy Ran/ tree
// packs sounds loose the same way, but CLIENT/sounds is the canonical set the
// editors read):
//   • CLIENT/sounds/bgm/*.ogg   — streaming background music, one per zone.
//   • CLIENT/sounds/sfx/*.wav   — short effects. Named either by an 8-digit id
//                                 %02d%06d = [ESoundType][id] (SoundSourceMan.h
//                                 enum: 05=eItem, 06=eEffect, 07=eReserved …),
//                                 or by a literal name (godlike.wav, cd01.wav).
//
// BGM -> MAP is DATA, not guessed: RanMapCatalog (mapcatalog.json, decoded by
// extract-mapcatalog.js from each map's strBGM) carries a per-map bgm filename,
// e.g. tradezone -> m7a.ogg. This script stages exactly the BGM the catalog
// references and nothing else, so every loadable map finds its music and no
// unreferenced track is shipped. Measured: 18 distinct .ogg are referenced by a
// map and present on disk (scarysong.ogg is referenced by one unused map and is
// absent from the source tree — reported, not staged).
//
// SFX -> EVENT is also DATA. The engine plays effects by KEYWORD through
// DxSoundLib (DxSoundLib::CreateSound/PlaySound), and the keyword's filename is
// loaded from config, NOT hard-coded:
//   • GLCONST_CHAR::* — default.charclass in GLogic.rcc, read by
//     GLogicDataLoad.cpp:403-435 (strPICKUP_ITEM, strGRINDING_*, strITEMDROP_*,
//     strGAMBLING_*, strPKCOMBO_*, strQITEM_FACT). Values decoded straight from
//     the shipped default.charclass in this pass (see EVENTS below, each cited).
//   • GLGaeaClient.cpp:1760-1768 — nine PKCOMBO announcer wavs given as literal
//     filenames in code.
//   • Gameword table (gameword.json, ID2GAMEWORD) — QUEST_ALARM and the
//     confrontation/BR countdown+result wavs (CONFT_WAV_BEGIN/END), read by
//     InnerInterfaceSimple.cpp:5721 and ConftDisplayMan.cpp:151/186.
//   • level_up.egp / skill_learn.egp / skill_up.egp — the level-up and skill
//     effects embed their sound in a SEQUENCE node (SSound::LoadSet). Pulled with
//     effect-egp.js; see LEVELUP/SKILL_* below.
//
// WHAT IS DELIBERATELY NOT MAPPED (reported, not faked):
//   • "UI click": the PC client has NO generic button-click sound. The only UI
//     sound hooks in Lib_ClientUI are QUEST_ALARM (a notification ding) and the
//     confrontation set. QUEST_ALARM is staged as the nearest UI-feedback sound
//     but is not claimed to be a click.
//   • "attack/hit" and "skill CAST": these are per-weapon / per-animation frame
//     triggers stored in each character's SChaSound data (CharacterSound.h —
//     m_szFileName[frame]) and in per-skill effect files, referencing numeric
//     wavs. There is no single fixed id for "attack" or "skill"; a faithful port
//     would drive PlaySfx from the animation/effect graph. Not resolvable to one
//     filename here, so left to the effect system rather than invented.
//
// STAGING TARGET: Assets/Ran/Resources/Audio/{bgm,sfx}. Unity IMPORTS each file
// there as an AudioClip, so RanAudio loads it with Resources.Load<AudioClip> — a
// CORE-module call with no UnityWebRequestAudioModule dependency. (The earlier
// StreamingAssets + UnityWebRequestMultimedia path needed DownloadHandlerAudioClip,
// a module the Runtime asmdef does not reference, so the real Android build failed
// CS1069 even though the typecheck stub compiled.) Committed .meta files use
// AudioImporter with a path-derived GUID: BGM loadType Streaming (large ogg stays
// streamed off disk), SFX DecompressOnLoad (short one-shots). The manifest goes to
// Resources/audio.json (a TextAsset), read by both RanAudio and RunCheck.
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const base = path.resolve(__dirname, '../../..');
const BGM_SRC = path.join(base, 'CLIENT/sounds/bgm');
const SFX_SRC = path.join(base, 'CLIENT/sounds/sfx');
const CATALOG = path.join(base,
  'MOBILE/unity/RanMobile/Assets/Ran/Resources/mapcatalog.json');

const UNITY_ASSETS = path.join(base, 'MOBILE/unity/RanMobile/Assets');
const AUDIO_DIR = path.join(UNITY_ASSETS, 'Ran/Resources/Audio');
const BGM_DST  = path.join(AUDIO_DIR, 'bgm');
const SFX_DST  = path.join(AUDIO_DIR, 'sfx');
const MANIFEST = path.join(UNITY_ASSETS, 'Ran/Resources/audio.json');

// AudioImporter loadType (Unity): 0 DecompressOnLoad, 1 CompressedInMemory,
// 2 Streaming. BGM streams; SFX decompress on load for zero-latency one-shots.
const LOAD_STREAMING = 2;
const LOAD_DECOMPRESS = 0;

// ---------------------------------------------------------------------------
// The SFX event -> filename table, every entry cited to its SOURCE origin.
// Keys are the DxSoundLib keyword (GLGaeaClient.cpp CreateSound) where one
// exists, else a synthesised UPPER_SNAKE name for the gameword/effect sounds.
// Filenames are as the SOURCE data gives them; on-disk casing is resolved
// case-insensitively when copying (the deploy tree lowercases everything).
// ---------------------------------------------------------------------------
const EVENTS = {
  // -- default.charclass / GLCONST_CHAR (GLogicDataLoad.cpp:403-423) ---------
  GRINDING_SUCCEED: '06000199.wav',
  GRINDING_FAIL:    '06000195.wav',
  GRINDING_RESET:   '02000207.wav',
  GRINDING_BROKEN:  '06000211.wav',
  GAMBLING_SHUFFLE: 'gambleshuffle.wav',
  GAMBLING_WIN:     'gamblewin.wav',
  GAMBLING_LOSE:    'gamblelose.wav',
  ITEMDROP_SUIT:    '07000567.wav',
  ITEMDROP_WAPON:   '07000555.wav',
  ITEMDROP_SHOES:   '07000565.wav',
  ITEMDROP_RING:    '07000562.wav',
  ITEMDROP_QBOX:    '05000202.wav',
  ITEMDROP_SCROLL:  '07000571.wav',
  ITEMDROP_COIN:    '07000560.wav',
  ITEMDROP_DRUGS:   '07000568.wav',
  PICKUP_ITEM:      '07000570.wav',   // <- "item pickup" event
  QITEM_FACT:       '05000203.wav',
  PKCOMBO_DOUBLE:   '2doublekill.wav',
  PKCOMBO_TRIPLE:   '3triplekill.wav',
  PKCOMBO_ULTRA:    '4ultrakill.wav',
  PKCOMBO_RAMPAGE:  '5rampage.wav',
  // -- literal names (GLGaeaClient.cpp:1760-1768) ---------------------------
  PKCOMBO_KILLING_SPREE: 'killing_spree.wav',
  PKCOMBO_DOMINATING:    'dominating.wav',
  PKCOMBO_MEGA_KILL:     'megakill.wav',
  PKCOMBO_UNSTOPPABLE:   'unstoppable.wav',
  PKCOMBO_WICKED_SICK:   'whickedsick.wav',
  PKCOMBO_MONSTER_KILL:  'monster_kill.wav',
  PKCOMBO_GODLIKE:       'godlike.wav',
  PKCOMBO_HOLY_SHIT:     'holyshit.wav',
  PKCOMBO_OWNAGE:        'ownage.wav',
  // -- gameword table (gameword.json / ID2GAMEWORD) -------------------------
  QUEST_ALARM: '06000196.wav',        // InnerInterfaceSimple.cpp:5721
  CONFT_BEGIN_FIGHT: 'fight.wav',     // ConftDisplayMan.cpp:151 (CONFT_WAV_BEGIN)
  CONFT_BEGIN_CD1:   'cd01.wav',
  CONFT_BEGIN_CD2:   'cd02.wav',
  CONFT_BEGIN_CD3:   'cd03.wav',
  CONFT_BEGIN_CD4:   'cd04.wav',
  CONFT_BEGIN_CD5:   'cd05.wav',
  CONFT_END_WIN:  'win.wav',          // ConftDisplayMan.cpp:186 (CONFT_WAV_END)
  CONFT_END_LOST: 'lost.wav',
  CONFT_END_DRAW: 'draw.wav',
  // -- effect-embedded (effect-egp.js SSound::LoadSet) ----------------------
  LEVELUP:      '06000494.wav',       // level_up.egp  SEQUENCE #1 (also #2 06000492)
  LEVELUP_RING: '06000492.wav',       //   level_up.egp SEQUENCE #2
  SKILL_LEARN:  '06000212.wav',       // skill_learn.egp SEQUENCE #1
  SKILL_UP:     '02000316.wav',       // skill_up.egp   SEQUENCE #2 (#1 shares 06000212)
};

// ---- meta writers ----------------------------------------------------------
// Under Resources/, Unity imports .ogg/.wav as AudioClips via AudioImporter — so
// each file gets a real AudioImporter meta (NOT DefaultImporter), and committing
// it with a path-derived GUID pins both the import settings and stable ids across
// checkouts. Folders keep the DefaultImporter folderAsset meta Unity assigns.
function guidFor(assetRelPath) {
  return crypto.createHash('md5').update(assetRelPath).digest('hex').slice(0, 32);
}
function defaultMeta(guid, folder) {
  return `fileFormatVersion: 2
guid: ${guid}
${folder ? 'folderAsset: yes\n' : ''}DefaultImporter:
  externalObjects: {}
  userData:
  assetBundleName:
  assetBundleVariant:
`;
}
// The AudioImporter meta Unity 2021.3 writes. loadType selects Streaming (BGM) or
// DecompressOnLoad (SFX); preloadAudioData is off for streamed BGM, on for SFX so
// the first hit has no load hitch. compressionFormat 1 = Vorbis; these are 2D
// sounds (3D: 0), matching RanAudio's spatialBlend 0.
function audioMeta(guid, loadType) {
  const preload = loadType === LOAD_STREAMING ? 0 : 1;
  return `fileFormatVersion: 2
guid: ${guid}
AudioImporter:
  externalObjects: {}
  serializedVersion: 6
  defaultSettings:
    serializedVersion: 2
    loadType: ${loadType}
    sampleRateSetting: 0
    sampleRateOverride: 44100
    compressionFormat: 1
    quality: 1
    conversionMode: 0
    preloadAudioData: ${preload}
  platformSettingOverrides: {}
  forceToMono: 0
  normalize: 1
  preloadAudioData: ${preload}
  loadInBackground: ${loadType === LOAD_STREAMING ? 1 : 0}
  ambisonic: 0
  3D: 0
  userData:
  assetBundleName:
  assetBundleVariant:
`;
}
function textMeta(guid) {
  return `fileFormatVersion: 2
guid: ${guid}
TextScriptImporter:
  externalObjects: {}
  userData:
  assetBundleName:
  assetBundleVariant:
`;
}
// Unity asset path (forward slashes, "Assets/..."), used for the GUID and metas.
function assetPath(absPath) {
  return 'Assets/' + path.relative(UNITY_ASSETS, absPath).split(path.sep).join('/');
}

// ---- source lookup (case-insensitive; deploy lowercases names) -------------
function indexDir(dir) {
  const m = new Map();
  if (fs.existsSync(dir)) for (const f of fs.readdirSync(dir)) m.set(f.toLowerCase(), f);
  return m;
}

// ---- BGM set: every distinct bgm a map references AND that exists -----------
function bgmSet(bgmIndex) {
  const json = JSON.parse(fs.readFileSync(CATALOG, 'utf8'));
  const want = new Set();
  const missing = new Set();
  for (const m of json.maps) {
    const b = (m.bgm || '').trim();
    if (!b || b.toUpperCase() === 'NULL') continue;
    if (bgmIndex.has(b.toLowerCase())) want.add(bgmIndex.get(b.toLowerCase()).toLowerCase());
    else missing.add(b);
  }
  return { want: [...want].sort(), missing: [...missing].sort() };
}

function ensureDir(d, folder = true) {
  if (!fs.existsSync(d)) fs.mkdirSync(d, { recursive: true });
  const meta = d + '.meta';
  if (folder && !fs.existsSync(meta)) {
    fs.writeFileSync(meta, defaultMeta(guidFor(assetPath(d)), true));
  }
}

function copyOne(srcDir, srcIndex, name, dstDir, results, loadType) {
  const real = srcIndex.get(name.toLowerCase());
  if (!real) { results.missing.push(name); return; }
  const dstName = real.toLowerCase();           // normalise to lowercase in-project
  const dst = path.join(dstDir, dstName);
  const bytes = fs.statSync(path.join(srcDir, real)).size;
  if (!DRY) {
    fs.copyFileSync(path.join(srcDir, real), dst);
    fs.writeFileSync(dst + '.meta', audioMeta(guidFor(assetPath(dst)), loadType));
  }
  results.staged.push(dstName);
  results.bytes += bytes;
  return dstName;
}

const DRY = process.argv.includes('--dry');

function main() {
  const bgmIndex = indexDir(BGM_SRC);
  const sfxIndex = indexDir(SFX_SRC);

  const { want: bgmFiles, missing: bgmMissing } = bgmSet(bgmIndex);

  // SFX files = the distinct filenames the EVENTS table names, deduped.
  const sfxWanted = [...new Set(Object.values(EVENTS).map((s) => s.toLowerCase()))].sort();

  if (!DRY) {
    ensureDir(AUDIO_DIR);
    ensureDir(BGM_DST);
    ensureDir(SFX_DST);
  }

  const bgm = { staged: [], missing: [], bytes: 0 };
  for (const f of bgmFiles) copyOne(BGM_SRC, bgmIndex, f, BGM_DST, bgm, LOAD_STREAMING);

  const sfx = { staged: [], missing: [], bytes: 0 };
  for (const f of sfxWanted) copyOne(SFX_SRC, sfxIndex, f, SFX_DST, sfx, LOAD_DECOMPRESS);

  // Rebuild the EVENTS map with lowercased filenames actually staged, dropping
  // any whose source was missing so RanAudio never points at an absent file.
  const events = {};
  for (const [k, v] of Object.entries(EVENTS)) {
    const low = v.toLowerCase();
    if (sfx.staged.includes(low)) events[k] = low;
  }

  const manifest = {
    note: 'Generated by tools/rcc-extract/stage-audio.js. BGM per map comes '
        + 'from RanMapCatalog.bgm; SFX events are cited in stage-audio.js.',
    bgm: bgm.staged.sort(),
    sfx: sfx.staged.sort(),
    events,
  };
  if (!DRY) {
    fs.writeFileSync(MANIFEST, JSON.stringify(manifest, null, 1));
    if (!fs.existsSync(MANIFEST + '.meta'))
      fs.writeFileSync(MANIFEST + '.meta', textMeta(guidFor(assetPath(MANIFEST))));
  }

  // ---- report -------------------------------------------------------------
  const MB = (n) => (n / 1048576).toFixed(2) + ' MB';
  console.log(`BGM staged: ${bgm.staged.length} files, ${MB(bgm.bytes)}`);
  if (bgmMissing.length)
    console.log(`  referenced but absent from source: ${bgmMissing.join(', ')}`);
  console.log(`SFX staged: ${sfx.staged.length} files, ${MB(sfx.bytes)}  `
            + `(${Object.keys(events).length} event keywords)`);
  if (sfx.missing.length)
    console.log(`  SFX source missing: ${sfx.missing.join(', ')}`);
  console.log(`TOTAL added: ${MB(bgm.bytes + sfx.bytes)}`);
  console.log(`manifest -> ${DRY ? '(dry run, not written)' : assetPath(MANIFEST)}`);
}

// Only stage when run directly; `require`-ing this (test.js) must not write.
if (require.main === module) main();
module.exports = { EVENTS, bgmSet, indexDir };
