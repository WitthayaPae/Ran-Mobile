'use strict';
//
// nativeID -> character model, and emClass -> player body, as one JSON the
// runtime loads from Resources ("crowmodels").
//
//   node extract-crowmodels.js                 # write Assets/Ran/Resources/crowmodels.json
//   node extract-crowmodels.js --out PATH       # write elsewhere
//   node extract-crowmodels.js --stdout         # print stats only, write nothing
//
// WHY THIS EXISTS
// --------------
// In the live world every mob and NPC arrives as SDROP_CROW carrying only its
// `sNativeID` (a main/sub pair). The PC turns that id into a rendered body via
// SCROWDATA::GetSkinObjFile() -> m_sAction.m_strSkinObj -> a `.chf`
// DxSkinCharData file (GLCrowClient.cpp:118, GLCrowData.h:249). This tool reads
// the same crow table the PC loads (`Crow.mnsf`, GLogicData.cpp:1174) and emits
// nativeID -> `.chf` STEM, which is exactly the name the character prefabs under
// Assets/Ran/Characters are built under (s_m_dw_wlb.prefab, archer_mob.prefab,
// mob_*.prefab, npc_*.prefab, ...). So the runtime does
// Resources.Load("Characters/<stem>").
//
// CONTAINER  (GLCrowDataMan::LoadFile, GLCrowData.cpp:947)
//   132-byte CSerialFile header      type "GLCROW", u32 fileVersion
//   version-gated byte substitution, later gate wins (:980):
//       fileVersion >= 0x0102 -> EMBYTECRYPT_OLD
//       fileVersion >= 0x0200 -> EMBYTECRYPT_CROW      (the shipped file is 0x0200)
//   u32 bodyVersion
//   u32 crowCount
//   crowCount x SCROWDATA::LoadFile
//
// RECORD  (SCROWDATA::LoadFile, GLCrowData.cpp:178; SaveFile at :690)
//   u32 recordVersion (<= 0x0101)
//   repeat until FILE_END_DATA (0xEDEDEDED):
//     u32 blockType   u32 blockVersion   u32 blockSize   [payload]
//   FILE_SBASIC (1): payload is a blitted SCROWBASIC_xxx; sNativeID is its FIRST
//     dword in EVERY version, and blockSize == sizeof(struct) (GASSERT), so the
//     id is read and the block skipped by size — reliable.
//   FILE_SACTION (2): version 0x0201 goes through SCROWACTION::LOAD, whose field
//     order (mirrors SAVE at GLCrowDataAction.cpp:531) is
//       dwActFlag, emMoveType, fDriftHeight, fWalkVelo, bRun, fRunVelo  (6 dwords)
//       wBodyRadius (WORD)      then    m_strSkinObj (std::string)
//     so skinObj sits at payload offset 26. blockSize here is the true block size
//     (CSerialFile BeginBlock/EndBlock patches it), so the block is skippable.
//
// After SBASIC+SACTION both fields wanted are known; the rest of the record
// (SGEN, SATTACK) is skipped by resynchronising on the FILE_END_DATA sentinel —
// the same technique itemdata.js uses, because SGEN writes sizeof(struct) as its
// size while streaming variable strings, so its size UNDERCOUNTS and is not a
// safe skip (GLCrowData.cpp:719 vs the streamed SCROWGEN::SAVE). Measured: this
// walks all 1853 records to EOF with zero tail bytes.
//
// std::string on this stream: CSerialFile writes u32(len+1) then len+1 bytes,
// the count INCLUDING the NUL (SerialFile.cpp:444/593) — the same convention as
// item.isf and .wld0, the OPPOSITE of the DxFrame strings in map objects.
//
// The player body is NOT decoded from a file: GLOGIC::szCharSkin
// (GLogicData.cpp:787) is a 16-entry compile-time table indexed by EMCHARINDEX
// (the bit position of the EMCHARCLASS flag), and every entry is either
// "o_m.chf" or "o_w.chf" — i.e. purely by sex. It is reproduced below with its
// source cite; nothing is hand-computed beyond copying that literal table.
//
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { RccArchive } = require('./rcc.js');
const B = require('./bytecrypt.js');

const base = path.resolve(__dirname, '../../..');
const GLOGIC = path.join(base, 'Ran/data/glogic/GLogic.rcc');
const DEFAULT_OUT = 'MOBILE/unity/RanMobile/Assets/Ran/Resources/crowmodels.json';

const FILE_SBASIC = 1, FILE_SACTION = 2, FILE_END = 0xededed_ed;
const SCROWDATA_VERSION = 0x0101;   // GLCrowData.h:126
const SCROWACTION_VERSION = 0x0201; // GLCrowDataAction.h:387
const ACF_SZNAME = 128;             // SAnimationInfo.h:16
// SCROWACTION::SAVE field order (GLCrowDataAction.cpp:531-536):
//   m_dwActFlag, m_emMoveType, m_fDriftHeight, m_fWalkVelo, m_bRun, m_fRunVelo
//   (six 4-byte slots), m_wBodyRadius (WORD), then m_strSkinObj (std::string).
// So fWalkVelo/fRunVelo sit inside the same fixed prefix SKINOBJ_OFFSET already
// walks past — this is the per-monster world-units/sec speed
// GLCrow.cpp:337/345 feeds straight into Actor::SetMaxSpeed (actor.h/.cpp),
// which the mobile port has no other source for (SDROP_CROW/SNETCROW_MOVETO
// carry no speed field of their own — only a run/walk STATE bit, dwActState).
const WALKVELO_OFFSET = 3 * 4;      // = 12
const RUNVELO_OFFSET = 5 * 4;       // = 20
// m_wBodyRadius (WORD) directly after fRunVelo — the radius GLCOPY::
// GetBodyRadius returns, which PC's melee range formula is built on
// (GLCharacter.cpp:1804: wAttackRange = targetBodyRadius + myBodyRadius(4)
// + weaponRange + 2). Needed 2026-08-23 for the mobile attack-range fix.
const BODYRADIUS_OFFSET = 6 * 4;    // = 24
const SKINOBJ_OFFSET = 6 * 4 + 2;   // = 26

// --- NPC-vs-monster discriminator -----------------------------------------
// The FILE_SBASIC block is a blitted SCROWBASIC_xxx (GLCrowData.cpp:195). Its
// SECOND field is EMCROW m_emCrow — CROW_NPC(1) / CROW_MOB(2) / CROW_PET(5) /
// CROW_ZONE_NAME(8) ... (GLDefine.h:573) — the flag the PC uses to decide an
// entity is a talk/shop/gate NPC (e.g. GLCharacter.cpp:6208
// `m_pCrowData->m_emCrow != CROW_NPC`, ReqReGenGate's CROW_NPC gate).
//
// The offset of m_emCrow is NOT hand-summed: every shipped record (measured:
// 1853/1853) uses the current SCROWBASIC layout whose block version is
// SCROWBASIC::VERSION = 0x0205, and MSVC placed m_emCrow at offset 4 in it
// (layout.json SCROWBASIC.fields.m_emCrow.off, via tools/layout-probe with
// GLCrowDataBasic.h added). sNativeID (SNATIVEID = two WORDs) occupies 0..3, so
// the enum sits at 4 with no padding. Older SBASIC versions place m_emCrow
// elsewhere (field 4 in v100..v110) — none of those ship here, so kind is read
// only for 0x0205 and left UNKNOWN otherwise rather than guessing.
const SCROWBASIC_VERSION = 0x0205;  // GLCrowDataBasic.h:613
const EMCROW_OFFSET_V205 = 4;       // layout.json: SCROWBASIC.m_emCrow.off
const CROW_NPC = 1, CROW_MOB = 2;   // GLDefine.h:576-577

// GLOGIC::szCharSkin[GLCI_NUM_8CLASS] (GLogicData.cpp:789-790), indexed by
// EMCHARINDEX = bit position of the EMCHARCLASS flag (GLCharDefine.h:252).
const SZCHARSKIN = [
  'o_m', 'o_m', 'o_w', 'o_w', 'o_m', 'o_w', 'o_w', 'o_w',
  'o_m', 'o_m', 'o_m', 'o_w', 'o_m', 'o_w', 'o_m', 'o_w',
];

function stemLower(s) { return s.toLowerCase().replace(/\.[a-z0-9]+$/, ''); }
function round2(x) { return Math.round(x * 100) / 100; }

// A prefab existing is NOT proof it renders. Two ways a "built" skin is still a
// capsule, both measured on this tree and neither caught by a file-exists check:
//   (1) geometry-empty — the skeleton .rmesh has 0 vertices and no part supplies
//       any (mob_yoyoman: its 4 .cps pieces ship but cps-refs.js dropped them).
//   (2) serialized-version mismatch — the prefab was written by a newer Unity
//       (format v23 / Unity 6) than the project's target editor (v22 / Unity
//       2021.3.45f2), so Resources.Load returns null in the older editor. This
//       was the 158-capsule cohort: every dedup-collapsed clone, and nothing
//       else, was v23.
// Best-effort and log-only: guarded by existsSync, never touches the emitted JSON.
function modelHealth(distinct, base) {
  const flatPath = path.join(base, 'MOBILE/unity/RanMobile/Assets/Ran/charflat.json');
  const meshDir = path.join(base, 'MOBILE/unity/RanMobile/Assets/Ran/meshes');
  const chDir = path.join(base, 'MOBILE/unity/RanMobile/Assets/Ran/Characters');
  if (!fs.existsSync(flatPath)) return null;

  const flat = JSON.parse(fs.readFileSync(flatPath, 'utf8'));
  const byName = new Map();
  for (const c of flat.chars) if (!byName.has(c.n.toLowerCase())) byName.set(c.n.toLowerCase(), c);

  const rverts = (stem) => {
    const p = path.join(meshDir, stem + '.rmesh');
    if (!fs.existsSync(p)) return null;
    const fd = fs.openSync(p, 'r'); const buf = Buffer.alloc(44);
    fs.readSync(fd, buf, 0, 44, 0); fs.closeSync(fd);
    return buf.toString('latin1', 0, 4) === 'RMSH' ? buf.readUInt32LE(28) : -1;
  };
  const prefabVer = (stem) => {
    const p = path.join(chDir, stem + '.prefab');
    if (!fs.existsSync(p)) return null;
    const fd = fs.openSync(p, 'r'); const buf = Buffer.alloc(12);
    fs.readSync(fd, buf, 0, 12, 0); fs.closeSync(fd);
    return buf.readUInt32BE(8);   // SerializedFile header: version is BE u32 at 8
  };

  const empty = [];
  const verHist = new Map();
  for (const stem of distinct.keys()) {
    const c = byName.get(stem);
    if (c) {
      let v = 0;
      if (c.p && c.p.length) { for (const pt of c.p) { const pv = rverts(pt.m); if (pv > 0) v += pv; } }
      else { const pv = rverts(c.k); v = pv > 0 ? pv : 0; }
      if (v <= 0) empty.push(stem);
    }
    const ver = prefabVer(stem);
    if (ver != null) verHist.set(ver, (verHist.get(ver) || 0) + 1);
  }
  return { empty, verHist, hasFlat: true };
}

function readStr(body, p) {
  const n = body.readUInt32LE(p.v); p.v += 4;
  if (n === 0 || n > 4096) throw new Error(`bad string length ${n}`);
  const raw = body.subarray(p.v, p.v + n); p.v += n;
  const nul = raw.indexOf(0);
  return raw.toString('latin1', 0, nul === -1 ? n : nul);
}

function cstr(body, off, max) {
  let end = off; const stop = Math.min(off + max, body.length);
  while (end < stop && body[end] !== 0) end++;
  return body.toString('latin1', off, end);
}

/** Offset just past this record's FILE_END_DATA, verified against what follows. */
function findRecordEnd(body, from) {
  for (let q = from; q + 4 <= body.length; q++) {
    if (body.readUInt32LE(q) !== FILE_END) continue;
    const after = q + 4;
    if (after + 8 > body.length) return after;           // last record
    const nv = body.readUInt32LE(after), nt = body.readUInt32LE(after + 4);
    // A real next record opens with recordVersion (<= 0x0101) then FILE_SBASIC.
    if (nv > 0 && nv <= SCROWDATA_VERSION && nt === FILE_SBASIC) return after;
  }
  throw new Error(`no record terminator from ${from}`);
}

function parseCrowTable(raw) {
  const head = B.readHeader(raw);
  if (!head || head.type !== 'GLCROW') {
    throw new Error(`not a GLCROW file (type "${head && head.type}")`);
  }
  let body = raw, table = null;
  if (head.version >= 0x0200) table = 'EMBYTECRYPT_CROW';
  else if (head.version >= 0x0102) table = 'EMBYTECRYPT_OLD';
  if (table) body = B.decode(Buffer.from(raw), table, head.bodyOffset);

  const p = { v: head.bodyOffset };
  const u32 = () => { const x = body.readUInt32LE(p.v); p.v += 4; return x; };

  const bodyVersion = u32();
  const count = u32();
  if (count > 200000) throw new Error(`implausible crow count ${count}`);

  const stats = { count, withSkin: 0, emptySkin: 0, actionVersions: new Map(),
                  basicVersions: new Map(), kinds: new Map(), npcCount: 0,
                  sentinelFalsePositives: 0 };
  const rows = [];
  for (let i = 0; i < count; i++) {
    const recVer = u32();
    if (recVer > SCROWDATA_VERSION) throw new Error(`record ${i} version 0x${recVer.toString(16)}`);

    const t1 = u32();
    if (t1 !== FILE_SBASIC) throw new Error(`record ${i}: first block ${t1} not SBASIC`);
    const bVer = u32();           // basic block version (SCROWBASIC::VERSION on save)
    const bSize = u32();
    stats.basicVersions.set(bVer, (stats.basicVersions.get(bVer) || 0) + 1);
    const nativeId = body.readUInt32LE(p.v);   // sNativeID is field 0 in all versions
    // m_emCrow only where the layout is the one MSVC measured (0x0205). -1 = unknown.
    const kind = (bVer === SCROWBASIC_VERSION)
      ? body.readUInt32LE(p.v + EMCROW_OFFSET_V205) : -1;
    if (kind >= 0) stats.kinds.set(kind, (stats.kinds.get(kind) || 0) + 1);
    if (kind === CROW_NPC) stats.npcCount++;
    p.v += bSize;                 // blitted, size == sizeof — authoritative skip

    let skin = '', walkVelo = 0, runVelo = 0, bodyRadius = 0;
    const t2 = u32();
    if (t2 === FILE_SACTION) {
      const aVer = u32(), aSize = u32();
      stats.actionVersions.set(aVer, (stats.actionVersions.get(aVer) || 0) + 1);
      const start = p.v;
      if (aVer === SCROWACTION_VERSION) {
        walkVelo = body.readFloatLE(start + WALKVELO_OFFSET);
        runVelo = body.readFloatLE(start + RUNVELO_OFFSET);
        bodyRadius = body.readUInt16LE(start + BODYRADIUS_OFFSET);
        p.v = start + SKINOBJ_OFFSET;
        skin = readStr(body, p);
      } else if (aVer <= 0x0102) {
        // Blitted SCROWACTION_100/101/102: WORD bodyRadius then char[ACF_SZNAME].
        skin = cstr(body, start + 2, ACF_SZNAME);
        // Field order differs pre-0x0201 and none of these ship (verified via
        // stats.actionVersions: every one of 1854 shipped records is 0x201), so
        // walk/run velocity is only extracted for the version that actually ships.
      } // other streamed versions: skinObj still first-ish, but none ship here.
      p.v = start + aSize;        // action size is authoritative (BeginBlock/EndBlock)
      p.v = findRecordEnd(body, p.v);
    } else {
      p.v = findRecordEnd(body, p.v - 4);
    }

    if (skin) stats.withSkin++; else stats.emptySkin++;
    rows.push({ main: nativeId & 0xffff, sub: (nativeId >>> 16) & 0xffff,
                nativeId: nativeId >>> 0, skin, kind, walkVelo, runVelo, bodyRadius });
  }
  stats.bytesRead = p.v;
  stats.fileSize = body.length;
  return { fileVersion: head.version, bodyVersion, rows, stats };
}

function main() {
  const arc = new RccArchive(GLOGIC);
  const entry = arc.entries.find(e => /(^|[\\/])crow\.mnsf$/i.test(e.name) || /crow\.mnsf$/i.test(e.name));
  if (!entry) throw new Error('Crow.mnsf not found in GLogic.rcc');
  const raw = arc.read(entry);
  const parsed = parseCrowTable(raw);
  const { rows, stats } = parsed;

  // Distinct skins and how they map to a built prefab (for the log only).
  const chDir = path.join(base, 'MOBILE/unity/RanMobile/Assets/Ran/Characters');
  const prefabs = fs.existsSync(chDir)
    ? new Set(fs.readdirSync(chDir).filter(f => f.endsWith('.prefab')).map(f => f.slice(0, -7).toLowerCase()))
    : new Set();

  // crow entries: nativeId -> prefab stem. Only rows with a skin get an entry;
  // a crow with no skinObj (barrier / trigger / material) has no model by design.
  const crows = [];
  const distinct = new Map();
  for (const r of rows) {
    if (!r.skin) continue;
    const stem = stemLower(r.skin);
    crows.push([r.nativeId, stem]);
    distinct.set(stem, (distinct.get(stem) || 0) + 1);
  }
  let haveP = 0; for (const s of distinct.keys()) if (prefabs.has(s)) haveP++;

  // player entries: classBit -> body stem, from szCharSkin.
  const players = [];
  for (let i = 0; i < SZCHARSKIN.length; i++) players.push([(1 << i) >>> 0, SZCHARSKIN[i]]);

  // npc entries: every nativeID whose crow record is CROW_NPC — the talk/shop/
  // gate NPCs. Emitted for ALL such crows, WITH or WITHOUT a skinObj, so the
  // runtime can tag a model-less (capsule) NPC as tappable too. A flat id list.
  const npcs = rows.filter((r) => r.kind === CROW_NPC).map((r) => r.nativeId);

  // speed entries: nativeId -> (walkVelo, runVelo), world-units/sec, straight
  // from SCROWACTION (see WALKVELO_OFFSET/RUNVELO_OFFSET above). This is what
  // the real client feeds Actor::SetMaxSpeed (GLCrow.cpp:337/345) — the ONLY
  // source of a per-monster move speed anywhere in this pipeline, since neither
  // SDROP_CROW nor SNETCROW_MOVETO carry a speed field on the wire (only a
  // run/walk STATE bit). Emitted for every row that reached the 0x201 action
  // block (all 1854 shipped records do), skin or not — a skinless/capsule
  // crow still moves, and should still move at its real speed.
  const speeds = [];
  for (const r of rows) {
    if (!r.walkVelo && !r.runVelo) continue;
    speeds.push(r.nativeId, round2(r.walkVelo), round2(r.runVelo));
  }

  // body-radius entries: nativeId -> m_wBodyRadius (WORD, world units). PC's
  // melee attack range is targetBodyRadius + myBodyRadius(GLCONST_CHAR::
  // wBODYRADIUS=4) + weaponRange(bare hands wMAXATRANGE_SHORT=2) + 2
  // (GLCharacter.cpp:1804/1894) — without this the mobile port had no per-mob
  // radius at all and used a flat 90-unit reach, ~5x PC's real melee range.
  const radii = [];
  for (const r of rows) {
    if (!r.bodyRadius) continue;
    radii.push(r.nativeId, r.bodyRadius);
  }

  const kindName = { [CROW_NPC]: 'NPC', [CROW_MOB]: 'MOB' };
  console.log(`Crow.mnsf: fileVer 0x${parsed.fileVersion.toString(16)} bodyVer 0x${parsed.bodyVersion.toString(16)} crows ${stats.count}`);
  console.log(`  action versions: ${[...stats.actionVersions].map(([k, v]) => `0x${k.toString(16)}:${v}`).join(' ')}`);
  console.log(`  basic versions: ${[...stats.basicVersions].map(([k, v]) => `0x${k.toString(16)}:${v}`).join(' ')}`);
  console.log(`  kinds (m_emCrow): ${[...stats.kinds].sort((a, b) => a[0] - b[0]).map(([k, v]) => `${kindName[k] || k}:${v}`).join(' ')}`);
  console.log(`  with skinObj ${stats.withSkin}, empty ${stats.emptySkin}, tail ${stats.fileSize - stats.bytesRead} bytes`);
  console.log(`  NPCs (CROW_NPC): ${npcs.length}`);
  console.log(`  distinct skins ${distinct.size}; ${haveP} have a built prefab, ${distinct.size - haveP} do not (capsule fallback)`);
  const health = modelHealth(distinct, base);
  if (health) {
    console.log(`  geometry-empty skins (built prefab but 0 vertices): ${health.empty.length}` +
                (health.empty.length ? ` [${health.empty.join(', ')}]` : ''));
    const vh = [...health.verHist].sort((a, b) => b[1] - a[1]);
    if (vh.length) {
      const majority = vh[0][0];
      const mismatch = vh.filter(([v]) => v !== majority).reduce((s, [, n]) => s + n, 0);
      console.log(`  prefab serialized versions: ${vh.map(([v, n]) => `v${v}:${n}`).join(' ')}` +
                  (mismatch ? ` — ${mismatch} NOT v${majority} (would load null / capsule in the v${majority} editor)` : ' — all consistent'));
    }
  }
  console.log(`  players: ${players.length} class bits -> ${new Set(SZCHARSKIN).size} bodies (${[...new Set(SZCHARSKIN)].join(', ')})`);
  const walkVals = rows.filter(r => r.walkVelo > 0).map(r => r.walkVelo).sort((a, b) => a - b);
  const runVals = rows.filter(r => r.runVelo > 0).map(r => r.runVelo).sort((a, b) => a - b);
  const med = (a) => a.length ? a[Math.floor(a.length / 2)] : 0;
  console.log(`  speeds: ${speeds.length / 3} nativeIds; walkVelo range ${walkVals[0]}..${walkVals[walkVals.length - 1]} median ${med(walkVals)}, ` +
              `runVelo range ${runVals[0]}..${runVals[runVals.length - 1]} median ${med(runVals)} (world units/sec, Actor::SetMaxSpeed)`);

  // Flat alternating [id, name, id, name, ...] arrays: trivial to hand-parse in
  // the runtime, which cannot use JsonUtility for a dictionary. `npcs` is a flat
  // [id, id, ...] list (RanCharacterResolver reads it into a set).
  // Geometry-empty skins: a built prefab that renders nothing (RanCharacterResolver
  // routes these to a labelled capsule instead of an invisible ghost). Derived
  // from charflat + .rmesh when those exist; [] otherwise, so the field is purely
  // additive and never invents a gap on a tree without imported meshes.
  const emptySkins = health ? health.empty : [];
  const out = {
    note: 'nativeID->character prefab (from Crow.mnsf), classBit->player body (szCharSkin), NPC nativeIDs (m_emCrow==CROW_NPC), geometry-empty skin stems, and nativeID->(walkVelo,runVelo) world-units/sec speeds. Generated by extract-crowmodels.js.',
    crows: crows.flat(),
    players: players.flat(),
    npcs,
    emptySkins,
    speeds,
    radii,
  };
  const j = JSON.stringify(out);

  if (process.argv.includes('--stdout')) { console.log(`(json ${(j.length / 1024).toFixed(0)} KB, not written)`); return; }
  const outArg = process.argv.indexOf('--out');
  const outRel = outArg > 0 ? process.argv[outArg + 1] : DEFAULT_OUT;
  const outPath = path.join(base, outRel);
  fs.mkdirSync(path.dirname(outPath), { recursive: true });
  fs.writeFileSync(outPath, j);
  // Commit a .meta so the TextAsset GUID is stable across fresh checkouts (a
  // scene or component referencing "crowmodels" then does not break). Only
  // written for the default in-project location.
  const metaPath = outPath + '.meta';
  if (!fs.existsSync(metaPath)) {
    const guid = crypto.createHash('md5')
      .update('Assets/Ran/Resources/' + path.basename(outPath)).digest('hex').slice(0, 32);
    fs.writeFileSync(metaPath,
      `fileFormatVersion: 2\nguid: ${guid}\nTextScriptImporter:\n` +
      `  externalObjects: {}\n  userData: \n  assetBundleName: \n  assetBundleVariant: \n`);
  }
  console.log(`wrote ${outRel} (${(j.length / 1024).toFixed(0)} KB, ${crows.length} crow ids, ${players.length} class bits, ${npcs.length} npc ids)`);
}

module.exports = { parseCrowTable, SZCHARSKIN, stemLower, CROW_NPC, CROW_MOB };

if (require.main === module) main();
