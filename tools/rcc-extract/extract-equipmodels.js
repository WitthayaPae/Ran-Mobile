'use strict';
//
// extract-equipmodels.js — the item -> worn attach-part map for RanEquipView.
//
//   node extract-equipmodels.js            # write assets + Resources JSON
//   node extract-equipmodels.js --dry      # measure, write nothing
//
// WHAT THIS DECODES (all cited to SOURCE)
// ---------------------------------------
// The PC composes a worn character in GLCharClient::UpdateSuit
// (GLCharClient.cpp:370-614). For each equip slot it takes the item's
// per-character-index wearing file:
//
//   strFileName = pItem->GetWearingFileR( emIndex );           // GLItem.h:158
//   if ( SLOT_LHAND* ) strFileName = pItem->GetWearingFileL( emIndex );
//   pBoneLink = DxAttBoneLinkContainer::LoadData( strFileName );// tries .abl
//   if ( pBoneLink ) m_pSkinChar->SetAttBone( pBoneLink, ... ); // ATTACH-BONE part
//   else             m_pSkinChar->SetPiece  ( strFileName, ... );// skinned .cps piece
//
// So a wearing file ending ".abl" is a bone-attached part (weapon / wings /
// belt / hat / accessory); anything else (".cps") is a mesh skinned onto the
// body and is NOT handled here — those deform with the body skeleton and are
// the RanChfBuilder body-piece job, not a bone parent.
//
// The ".abl" (DxAttBoneLink, DxAttBoneLinkSaveLoad.cpp) names:
//   m_strAttBoneData  -> the ".abf" mesh (DxAttBoneData) == the staged prefab
//   m_strBoneTrans    -> the bone on the CHARACTER skeleton it hangs off
//                        (DxAttBoneLink::LoadFile:270 FindBone(m_strBoneTrans))
//   m_affBoneTrans    -> DXAFFINEPARTS (vTrans, vRotate-euler, vScale), 36 bytes,
//                        composed Scale*Rotate*Translate (DxMethods.cpp:214),
//                        YawPitchRoll(x->Y, y->X, z->Z) (DxMethods.cpp:206)
//   m_emWeaponWhereBack / m_emPieceType (EMPIECECHAR / EMPEACEZONEWEAPON)
//
// The bone names are REAL bones in the body skeleton `.x` (e.g. b_m/b_w):
// verified present in the extracted b_m.rmesh — Bip01_Spine2 (wings/belts),
// gum / r_gun / l_gun (guns), whal / stick / sickle (melee dummies),
// Scene_Root, Bip01_Footsteps. So RanEquipView finds them by name on the body.
//
// OUTPUT (equipmodels.json)
//   links: { <ablStem>: { abf, bone, t:[3], r:[3], s:[3], pt, wb } }
//   items: { "<main>/<sub>": { r:[16], l:[16] } }  // per EMCHARINDEX (0..15),
//          each entry an <ablStem> that keys into links, or "" — only items with
//          >=1 resolving .abl wearing file are emitted, which keeps this small.
//
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const BC = require('./bytecrypt.js');
const itemdata = require('./itemdata.js');
const animinfo = require('./animinfo.js');                   // .chf -> per-EMPIECECHAR base pieces
const { RccArchive } = require('./rcc.js');                  // SkinObject.rcc reader

const base = path.resolve(__dirname, '../../..');
const ABL_DIR = path.join(base, 'CLIENT/data/skinobject');   // .abl/.abf source (frozen, read-only)
// SkinObject.rcc holds every shipped .chf (DxSkinCharData). Its piece list is
// indexed by EMPIECECHAR (DxPieceDefine.h) — readPieceList's loop index IS the
// slot — so it is the authority for "which base body renderer is which piece".
const SKINOBJECT_RCC = path.join(base, 'Ran/data/skinobject/SkinObject.rcc');
// Extracted .rmesh, read to recover each piece submesh's ORDERED skin-bone names
// (the runtime rebind needs them; see buildPieceBones + RanEquipView.RebindToBody).
const MESH_DIR = path.join(base, 'MOBILE/assets/meshes');
const OUT_ASSETS = path.join(base, 'MOBILE/assets/equipmodels.json');
const OUT_RES = path.join(base, 'MOBILE/unity/RanMobile/Assets/Ran/Resources/equipmodels.json');
// charflat.json is the manifest RanChfBuilder consumes; its dedup order is what
// decides which of several geometrically-identical .abf attach parts actually
// gets a prefab (built under the FIRST name for a given skeleton+part-list key).
// Reading it here lets us alias the collapsed .abf names onto the built one — so
// all 505 item->.abl->.abf references resolve to a prefab that exists, with no
// extra builds. See the abfAlias section in main().
const CHARFLAT = path.join(base, 'MOBILE/unity/RanMobile/Assets/Ran/charflat.json');
// pieces.json (cps-refs.js) resolves every .cps/.aps character piece to the
// extracted .rmesh (+ sub-mesh) it draws — the link needed for SKINNED equipment
// (armour/suits) that the PC mounts with SetPiece rather than SetAttBone.
const PIECES = path.join(base, 'MOBILE/assets/pieces.json');
// The skinned-piece appearance table is emitted to its OWN file, not merged into
// equipmodels.json, so the always-loaded attach-part table stays small and the
// (much larger, and still asset-gated) suit table is loaded only where used.
const OUT_PIECES = path.join(base, 'MOBILE/assets/equippieces.json');
const OUT_PIECES_RES = path.join(base, 'MOBILE/unity/RanMobile/Assets/Ran/Resources/equippieces.json');
const DRY = process.argv.includes('--dry');

const GLCI_NUM_8CLASS = 16;                                  // GLCharDefine.h:276
const CHF_VERSION_ENCRYPT = 0x0200;

// ---------------------------------------------------------------------------
// Minimal CSerialFile cursor + container opener (mirrors animinfo.js openContainer)
// ---------------------------------------------------------------------------
function cstr(b) { const n = b.indexOf(0); return b.toString('latin1', 0, n === -1 ? b.length : n); }
class Cursor {
  constructor(buf) { this.buf = buf; this.off = 0; }
  get left() { return this.buf.length - this.off; }
  need(n) { if (n < 0 || this.off + n > this.buf.length) throw new Error(`truncated need ${n} at ${this.off}`); }
  u32() { this.need(4); const v = this.buf.readUInt32LE(this.off); this.off += 4; return v; }
  f32() { this.need(4); const v = this.buf.readFloatLE(this.off); this.off += 4; return v; }
  skip(n) { this.need(n); this.off += n; }
  str() { const n = this.u32(); if (n > 0x10000) throw new Error(`implausible str len ${n}`); if (n === 0) return ''; this.need(n); const s = cstr(this.buf.subarray(this.off, this.off + n)); this.off += n; return s; }
  aff() { return { t: [this.f32(), this.f32(), this.f32()], r: [this.f32(), this.f32(), this.f32()], s: [this.f32(), this.f32(), this.f32()] }; }
}

function openContainer(buf) {
  const hdr = BC.readHeader(buf);
  if (!hdr) throw new Error('short header');
  const version = buf.readUInt32LE(hdr.bodyOffset);
  let body = buf.subarray(hdr.bodyOffset + 4);
  if (version >= CHF_VERSION_ENCRYPT) body = BC.decode(Buffer.from(body), 'EMBYTECRYPT_CONTAINER');
  return { version, body };
}

// DxAttBoneLink::LoadFile dispatch (DxAttBoneLinkSaveLoad.cpp:206-288).
function parseAbl(buf) {
  const { version, body } = openContainer(buf);
  const c = new Cursor(body);
  const r = { version };
  switch (version) {
    case 0x0201:                                            // LOAD_Ver_0201
    case 0x0200:                                            // LOAD_Ver_0200 (same field order)
      r.pt = c.u32();            // m_emPieceStrike (0201) / mapped (0200) — we keep pieceType below
      r.pieceType = c.u32();     // m_emPieceType
      r.wb = c.u32();            // m_emWeaponWhereBack
      c.str();                   // m_strMaskPiece
      r.abf = c.str();           // m_strAttBoneData
      c.str();                   // m_strSkeleton
      r.bone = c.str();          // m_strBoneTrans
      r.aff = c.aff();           // m_affBoneTrans
      c.aff();                   // m_affPeaceZone
      break;
    case 0x0101:                                            // LOAD_Ver_0101
      c.u32(); r.abf = c.str(); c.str(); c.str(); r.bone = c.str(); c.u32();
      r.pt = c.u32(); r.pieceType = c.u32(); r.wb = c.u32();
      r.aff = c.aff(); c.aff();
      break;
    case 0x0100:                                            // LOAD_Ver_0100
      c.u32(); r.abf = c.str(); c.str(); c.str(); r.bone = c.str(); c.u32();
      r.pieceType = c.u32(); r.wb = c.u32();
      r.aff = c.aff(); c.aff();
      break;
    case 0x0102: {                                          // LOAD_Ver_0102 (nested [ver][size])
      c.u32(); r.abf = c.str(); c.str(); c.str(); c.u32();
      r.pt = c.u32(); r.pieceType = c.u32();
      const dwVER = c.u32(); c.u32();                       // dwVER, dwSIZE
      if (dwVER === 0x0100) {
        r.bone = c.str(); r.wb = c.u32();
        r.aff = c.aff(); c.aff(); c.u32(); c.skip(64);      // trailing D3DXMATRIX
      } else { throw new Error(`abl 0102 inner ver 0x${dwVER.toString(16)}`); }
      break;
    }
    default:
      throw new Error(`abl version 0x${version.toString(16)}`);
  }
  if (!r.abf || !r.bone) throw new Error('abl missing abf/bone');
  return r;
}

const stem = (s) => path.basename(String(s || '').toLowerCase()).replace(/\.[a-z0-9]+$/, '');

function loadJson(p) { try { return JSON.parse(fs.readFileSync(p, 'utf8')); } catch { return null; } }

// ---------------------------------------------------------------------------
// abfAlias — map an .abf attach part onto the prefab that actually gets built.
//
// RanChfBuilder.BuildAll de-duplicates characters (attach parts included) by
// `skeleton | part(mesh:submesh)…`: geometrically identical parts collapse and
// only the FIRST (in charflat order) is written, as `<firstName>.prefab`. So a
// belt whose .abf shares a skeleton+piece with an earlier one never gets a
// prefab of its own name, and RanEquipView's Resources.Load("<abf>.abf") misses.
//
// The alias replicates that exact key over charflat.json and records, for each
// collapsed .abf, the representative that WAS built. RanEquipView tries the .abf
// name first, then the alias — so all geometrically-distinct parts still resolve
// with zero extra builds. Identical geometry is exactly why they collapsed, so
// the shared prefab is the correct mesh; only per-.aps material/texture overrides
// (e.g. colour variants) are not reproduced — a pre-existing pipeline limit that
// affects a separately-built prefab identically (see report).
function computeAbfAlias(abfStems) {
  const cf = loadJson(CHARFLAT);
  if (!cf || !Array.isArray(cf.chars)) return { alias: {}, ok: false };
  const rep = new Map();                                   // dedup key -> first char name
  const repFor = new Map();                                // char name (lc) -> representative name (lc)
  for (const c of cf.chars) {
    const key = `${c.k}|${(c.p || []).map((p) => `${p.m}:${p.s}`).join(',')}`;
    if (!rep.has(key)) rep.set(key, c.n);
    repFor.set(String(c.n).toLowerCase(), String(rep.get(key)).toLowerCase());
  }
  const alias = {};
  for (const a of abfStems) {
    const self = `${a}.abf`;
    const r = repFor.get(self);
    if (r && r !== self) alias[a] = r.replace(/\.abf$/, '');  // -> representative .abf stem
  }
  return { alias, ok: true };
}

// ---------------------------------------------------------------------------
// .rmesh skin-bone reader — the ordered skin-bone NAMES per sub-mesh.
//
// WHY: RanMeshImporter leaves a SkinnedMeshRenderer's `bones` slot NULL for any
// skin bone that lives in a DIFFERENT file — which is exactly the character
// skeleton an equipment piece skins onto (README "bone indices point at the mesh,
// not the file"). At runtime RanEquipView.RebindToBody cannot read a name back
// off a null Transform, so 20.1% of equipment skin-bone slots (600 of 3,490
// mounted sub-meshes) would rebind to nothing and collapse. RanChfBuilder avoids
// this at BUILD time by reading the names from the .rmesh (SkinBoneNames); the
// runtime has no file access, so the names are emitted here instead.
//
// Layout mirrors RanMeshFile.Parse (Runtime/RanMeshFile.cs): 44-byte header,
// string pool, bone table (72), mesh table (40), sub-mesh table (12), then the
// GLOBAL skin-bone table (72) that each mesh indexes with kOff..kOff+kCnt.
const RMESH_HEAD = 44, RMESH_BONE = 72, RMESH_MESH = 40, RMESH_SUB = 12, RMESH_SKIN = 72;
function readRmeshSkinBones(buf) {
  if (!buf || buf.length < RMESH_HEAD || buf.toString('latin1', 0, 4) !== 'RMSH') return null;
  const boneCount = buf.readUInt32LE(12), meshCount = buf.readUInt32LE(16);
  const subCount = buf.readUInt32LE(20), stringBytes = buf.readUInt32LE(36);
  const strAt = RMESH_HEAD;
  const boneAt = strAt + stringBytes;
  const meshAt = boneAt + boneCount * RMESH_BONE;
  const subAt = meshAt + meshCount * RMESH_MESH;
  const skinAt = subAt + subCount * RMESH_SUB;
  const pool = buf.subarray(strAt, strAt + stringBytes);
  const rd = (o) => {
    if (o < 0 || o >= pool.length) return '';
    let e = pool.indexOf(0, o); if (e < 0) e = pool.length;
    return pool.toString('latin1', o, e);
  };
  const out = {};                                           // submeshLower -> [boneName,...]
  for (let m = 0; m < meshCount; m++) {
    const o = meshAt + m * RMESH_MESH;
    const name = rd(buf.readUInt32LE(o)).toLowerCase();
    const kOff = buf.readUInt32LE(o + 28), kCnt = buf.readUInt32LE(o + 32);
    const skinned = (buf.readUInt32LE(o + 36) & 1) !== 0;
    if (!skinned || kCnt === 0) continue;
    const bones = [];
    for (let k = 0; k < kCnt; k++) bones.push(rd(buf.readUInt32LE(skinAt + (kOff + k) * RMESH_SKIN)));
    out[name] = bones;
  }
  return out;
}

const _rmeshCache = new Map();
function rmeshSkinBonesOf(meshStem) {
  if (_rmeshCache.has(meshStem)) return _rmeshCache.get(meshStem);
  let r = null;
  try { r = readRmeshSkinBones(fs.readFileSync(path.join(MESH_DIR, `${meshStem}.rmesh`))); }
  catch { r = null; }
  _rmeshCache.set(meshStem, r);
  return r;
}

// pieceBones — meshStem -> { submeshLower: [ordered skin-bone names] } for every
// mesh a mounted piece draws. RanEquipView rebinds each renderer with THESE names
// instead of the null imported Transforms, so external (character-skeleton) bones
// resolve. Only skinned sub-meshes carry an entry.
function buildPieceBones(pieceLinks) {
  const out = {};
  for (const l of Object.values(pieceLinks)) {
    if (!l || !l.m || out[l.m]) continue;
    const bones = rmeshSkinBonesOf(l.m);
    if (bones && Object.keys(bones).length) out[l.m] = bones;
  }
  return out;
}

// basePieces — the map that lets RanEquipView HIDE (not overlay) the body piece a
// worn .cps replaces. GLCharClient::UpdateSuit calls SetPiece(emPiece), which
// OVERWRITES slot emPiece in DxSkinChar's per-EMPIECECHAR array; the mobile port
// has no such array, so it must DISABLE the base renderer occupying that slot.
//
// A .chf's piece list is indexed by EMPIECECHAR (animinfo.readPieceList: the loop
// index i == the slot, DxPieceDefine.h), and RanChfBuilder names each base
// renderer after its resolved SUB-MESH. So EMPIECECHAR -> submesh (per character)
// is exactly "which renderer to hide". It MUST be per-character: the same submesh
// name maps to different slots across characters — e.g. `w1_bs_hand` is GLOVE for
// 48 characters but LHAND for one, so a global submesh->slot map would mis-hide.
//
// Keyed by the .chf stem, which is the charflat character name AND the built
// prefab name (RanChfBuilder saves `<c.n>.prefab`); the runtime identifies the
// body by matching its renderer names against these sets (RanEquipModels.IdentifyBody).
const PIECE_SIZE = 26;                                       // DxPieceDefine.h PIECE_SIZE
function buildBasePieces(resolveSubmesh) {
  const out = {};
  let chfOk = 0, chfFail = 0, entries = 0;
  let arc;
  try { arc = new RccArchive(SKINOBJECT_RCC); }
  catch { return { basePieces: out, chfOk, chfFail, entries }; }
  const chfs = arc.entries.filter((e) => /\.chf$/i.test(e.name));
  for (const e of chfs) {
    let rec;
    try { rec = animinfo.parseSkinChar(arc.read(e), { tolerant: true }); chfOk++; }
    catch { chfFail++; continue; }
    const plist = rec.pieces || [];
    const map = {};
    for (let emPiece = 0; emPiece < plist.length && emPiece < PIECE_SIZE; emPiece++) {
      const cps = plist[emPiece];
      if (!cps) continue;
      const sub = resolveSubmesh(cps);                      // submeshLower or null
      if (!sub) continue;
      map[emPiece] = sub;
      entries++;
    }
    if (Object.keys(map).length) out[stem(e.name)] = map;
  }
  return { basePieces: out, chfOk, chfFail, entries };
}

// ---------------------------------------------------------------------------
// Skinned equipment (armour/suits) — the SetPiece path.
//
// GLCharClient::UpdateSuit (GLCharClient.cpp:581-583) tries the wearing file as
// an .abl first (SetAttBone, handled above); if that fails it is a .cps skinned
// piece (SetPiece) that REPLACES the body piece for SLOT_2_PIECE(slot)
// (GLItemDef.h:290 / DxPieceDefine.h EMPIECECHAR). cps-refs.js already resolves
// every .cps to the .rmesh (+ sub-mesh) it draws, so this just joins item ->
// per-class .cps -> mesh. Emitted to equippieces.json, separate from the small
// always-loaded attach table. Runtime mounting is asset-gated — see the report.
function buildPieces(parsedItemsList) {
  const pj = loadJson(PIECES);
  if (!pj || !pj.pieces) return { ok: false, pieceLinks: {}, pieceItems: {} };
  const meshDir = path.join(base, 'MOBILE/assets/meshes');
  const haveMesh = new Set(fs.existsSync(meshDir)
    ? fs.readdirSync(meshDir).filter((f) => f.endsWith('.rmesh')).map((f) => f.slice(0, -6).toLowerCase())
    : []);
  const pieces = pj.pieces;
  // Resolve one .cps name -> { mesh, sub } or null. A piece resolves only when
  // its mesh is actually extracted, mirroring what the runtime can load.
  const cache = new Map();
  function resolveCps(name) {
    const key = stem(name);
    if (cache.has(key)) return cache.get(key);
    const ref = pieces[`${key}.cps`] || pieces[`${key}.aps`] || pieces[name.toLowerCase()];
    let out = null;
    if (ref && ref.mesh && haveMesh.has(ref.mesh)) {
      out = { m: ref.mesh, s: ref.submeshFound ? (ref.submesh || '') : '' };
    }
    cache.set(key, out);
    return out;
  }
  // For basePieces: a base .cps -> its lowercased sub-mesh renderer name, but only
  // when the mesh is extracted AND the sub-mesh was found (so a single named
  // renderer exists on the built prefab to hide). Whole-file base pieces (no named
  // sub-mesh) are skipped — there is no one renderer to target.
  const resolveSubmesh = (name) => {
    const r = resolveCps(name);
    return r && r.s ? r.s.toLowerCase() : null;
  };
  const pieceLinks = {};                                   // cps stem -> { m, s }
  const pieceItems = {};                                   // main/sub -> { r:[16], l:[16] }
  const isCps = (name) => /\.cps$/i.test(name || '');
  let items = 0, resolved = 0;
  for (const parsed of parsedItemsList) {
    for (const it of parsed.items) {
      const b = it.basic;
      if (!b || !b.wearingRight) continue;
      const rArr = new Array(GLCI_NUM_8CLASS).fill('');
      const lArr = new Array(GLCI_NUM_8CLASS).fill('');
      let any = false, anyRes = false;
      for (let i = 0; i < GLCI_NUM_8CLASS; i++) {
        for (const [src, arr] of [[b.wearingRight[i], rArr], [b.wearingLeft[i], lArr]]) {
          if (!isCps(src)) continue;
          const st = stem(src);
          const r = resolveCps(src);
          if (!r) continue;                                // unresolvable -> skip (honest hole)
          arr[i] = st; any = true; anyRes = true;
          if (!pieceLinks[st]) pieceLinks[st] = r;
        }
      }
      if (!any) continue;
      items++;
      if (anyRes) resolved++;
      const mainId = (b.nativeId >>> 0) & 0xffff;
      const subId = (b.nativeId >>> 16) & 0xffff;
      pieceItems[`${mainId}/${subId}`] = { r: rArr, l: lArr };
    }
  }
  // FACE/HAIR pieces (2026-08-23, PC UpdateSuit parity): the per-class
  // strHEAD_CPS/strHAIR_CPS lists (charclasses.json headCps/hairCps) are the
  // pieces GLCharClient mounts by the character's own wFace/wHair index — they
  // are NOT item-referenced, so the item loop above never adds them. Join them
  // into pieceLinks from the same pieces.json resolver so the runtime can key
  // them by stem exactly like an item piece.
  try {
    const cc = loadJson(path.join(base, 'MOBILE/assets/charclasses.json'));
    if (cc && cc.classes) {
      let fhAdded = 0;
      for (const cls of cc.classes) {
        for (const lst of [cls.headCps, cls.hairCps]) {
          if (!Array.isArray(lst)) continue;
          for (const st of lst) {
            if (pieceLinks[st]) continue;
            const r = resolveCps(st + '.cps');
            if (r) { pieceLinks[st] = r; fhAdded++; }
          }
        }
      }
      console.log(`  face/hair pieces added to pieceLinks: ${fhAdded}`);
    }
  } catch (e) { console.log('  face/hair join skipped: ' + e.message); }

  // Base-piece hide table (per character) and the ordered skin-bone names the
  // runtime rebind needs. Both are derived from the SAME resolveCps/mesh set, so
  // a base or piece whose mesh is not extracted is consistently absent here too.
  const { basePieces, chfOk, chfFail, entries: baseEntries } = buildBasePieces(resolveSubmesh);
  const pieceBones = buildPieceBones(pieceLinks);
  return {
    ok: true, pieceLinks, pieceItems, items, resolved,
    basePieces, pieceBones, chfOk, chfFail, baseEntries,
  };
}

function writeText(dst, resPath, json, importerGuidPath) {
  fs.writeFileSync(dst, json);
  console.log(`wrote ${path.relative(base, dst)}`);
  const resDir = path.dirname(resPath);
  if (fs.existsSync(resDir)) {
    fs.writeFileSync(resPath, json);
    const metaPath = resPath + '.meta';
    if (!fs.existsSync(metaPath)) {
      const guid = crypto.createHash('md5').update(importerGuidPath).digest('hex').slice(0, 32);
      fs.writeFileSync(metaPath,
        `fileFormatVersion: 2\nguid: ${guid}\nTextScriptImporter:\n` +
        `  externalObjects: {}\n  userData: \n  assetBundleName: \n  assetBundleVariant: \n`);
    }
    console.log(`wrote ${path.relative(base, resPath)}`);
  } else {
    console.log(`(skipped Resources copy — ${path.relative(base, resDir)} absent)`);
  }
}

// ---------------------------------------------------------------------------
function main() {
  // 1. Every .abl -> link record, keyed by its own stem.
  const links = {};
  let ablOk = 0, ablFail = 0;
  const ablFiles = fs.readdirSync(ABL_DIR).filter((f) => f.toLowerCase().endsWith('.abl'));
  for (const f of ablFiles) {
    try {
      const r = parseAbl(fs.readFileSync(path.join(ABL_DIR, f)));
      links[stem(f)] = {
        abf: stem(r.abf), bone: r.bone,
        t: r.aff.t.map((x) => +x.toFixed(5)),
        r: r.aff.r.map((x) => +x.toFixed(5)),
        s: r.aff.s.map((x) => +x.toFixed(5)),
        pt: r.pieceType | 0, wb: r.wb | 0,
      };
      ablOk++;
    } catch (e) { ablFail++; }
  }

  // 2. Every item -> its 16 wearing files (R/L). Keep only those whose stem
  //    resolves to a known link (i.e. is an .abl attach part).
  const items = {};
  let itemCount = 0, withAbl = 0;
  const parsedItemsList = [];                              // reused by buildPieces
  const isAbl = (name) => /\.abl$/i.test(name) && links[stem(name)];
  for (const t of ['item.isf', 'item1.isf']) {
    const p = findIsf(t);
    if (!p) continue;
    const parsed = itemdata.parse(fs.readFileSync(p));
    parsedItemsList.push(parsed);
    for (const it of parsed.items) {
      const b = it.basic;
      if (!b || !b.wearingRight) continue;
      itemCount++;
      const rArr = new Array(GLCI_NUM_8CLASS).fill('');
      const lArr = new Array(GLCI_NUM_8CLASS).fill('');
      let any = false;
      for (let i = 0; i < GLCI_NUM_8CLASS; i++) {
        const wr = b.wearingRight[i] || '';
        const wl = b.wearingLeft[i] || '';
        if (isAbl(wr)) { rArr[i] = stem(wr); any = true; }
        if (isAbl(wl)) { lArr[i] = stem(wl); any = true; }
      }
      if (!any) continue;
      withAbl++;
      const main = (b.nativeId >>> 0) & 0xffff;
      const sub = (b.nativeId >>> 16) & 0xffff;
      items[`${main}/${sub}`] = { r: rArr, l: lArr };
    }
  }

  // 3. abfAlias — collapsed .abf -> the prefab that was actually built.
  const abfStems = [...new Set(Object.values(links).map((l) => l.abf))];
  const { alias: abfAlias, ok: aliasOk } = computeAbfAlias(abfStems);

  const out = {
    note: 'item -> worn attach-part (.abl->.abf) map for RanEquipView; see extract-equipmodels.js',
    links, items, abfAlias,
  };
  const json = JSON.stringify(out);

  // 4. Skinned equipment (.cps SetPiece path) -> its own file.
  const pcs = buildPieces(parsedItemsList);
  const piecesOut = {
    note: 'item -> skinned .cps piece (SetPiece) -> .rmesh mesh/submesh for RanEquipView; ' +
      'runtime mounting is asset-gated (stage piece meshes into Resources). See extract-equipmodels.js.',
    pieceLinks: pcs.pieceLinks, pieceItems: pcs.pieceItems,
    // basePieces: charName -> { EMPIECECHAR: submesh } — the base body renderer to
    // HIDE when a worn .cps replaces that slot (SetPiece overwrites, so the port
    // disables the base renderer it replaces). pieceBones: mesh -> { submesh:
    // [skin-bone names] } — the ordered names RanEquipView rebinds with, since the
    // imported prefab leaves external (character-skeleton) bones null.
    basePieces: pcs.basePieces || {}, pieceBones: pcs.pieceBones || {},
  };
  const piecesJson = JSON.stringify(piecesOut);

  console.log(`.abl files: ${ablFiles.length}  parsed ${ablOk}  failed ${ablFail}`);
  console.log(`links: ${Object.keys(links).length}`);
  console.log(`items scanned: ${itemCount}  with >=1 .abl wearing file: ${withAbl}`);
  console.log(`abfAlias: ${Object.keys(abfAlias).length} collapsed .abf -> built prefab ` +
    `${aliasOk ? '' : '(charflat.json absent — empty)'}`);
  const builtish = abfStems.filter((a) => !abfAlias[a]).length;
  console.log(`  coverage: ${abfStems.length} distinct .abf; ${builtish} own-name + ` +
    `${Object.keys(abfAlias).length} aliased = ${builtish + Object.keys(abfAlias).length} resolvable`);
  console.log(`pieces: ${pcs.ok ? `${pcs.items} items with a resolvable .cps, ` +
    `${Object.keys(pcs.pieceLinks).length} distinct pieces, ` +
    `${Object.keys(pcs.pieceItems).length} item entries` : 'pieces.json absent — skipped'}`);
  if (pcs.ok) {
    const bp = pcs.basePieces || {}, pb = pcs.pieceBones || {};
    let boneLists = 0; for (const m of Object.values(pb)) boneLists += Object.keys(m).length;
    console.log(`basePieces: ${Object.keys(bp).length} characters (${pcs.baseEntries} EMPIECECHAR->submesh ` +
      `entries) from ${pcs.chfOk} .chf parsed (${pcs.chfFail} failed) — the per-char base renderer to hide`);
    console.log(`pieceBones: ${Object.keys(pb).length} meshes, ${boneLists} skinned sub-mesh bone lists ` +
      `— the ordered skin-bone names RanEquipView rebinds with`);
  }
  console.log(`json: equipmodels ${(json.length / 1024).toFixed(1)} KB, ` +
    `equippieces ${(piecesJson.length / 1024).toFixed(1)} KB`);
  // distinct bones actually referenced
  const bones = {};
  for (const k of Object.keys(links)) bones[links[k].bone] = (bones[links[k].bone] || 0) + 1;
  console.log('bones:', Object.entries(bones).sort((a, b) => b[1] - a[1]).slice(0, 12)
    .map(([k, v]) => `${k}:${v}`).join('  '));

  if (DRY) { console.log('(dry run — nothing written)'); return; }
  writeText(OUT_ASSETS, OUT_RES, json, 'Assets/Ran/Resources/equipmodels.json');
  // equippieces.json is ~3 MB and inert until the piece meshes are staged into
  // Resources (see report — the SetPiece feature is asset-gated), so it is NOT
  // copied into the build by default; --stage-pieces opts in once meshes exist.
  if (pcs.ok) {
    fs.writeFileSync(OUT_PIECES, piecesJson);
    console.log(`wrote ${path.relative(base, OUT_PIECES)}`);
    if (process.argv.includes('--stage-pieces')) {
      writeText(OUT_PIECES, OUT_PIECES_RES, piecesJson, 'Assets/Ran/Resources/equippieces.json');
    } else {
      console.log('(equippieces.json NOT staged to Resources — pass --stage-pieces after ' +
        'the piece meshes are staged; it is inert without them)');
    }
  }
}

// item.isf/item1.isf live loose or in GLogic.rcc; the loose copies under
// Ran/data/glogic are simplest for a read of the tables (itemdata.parse takes
// the RCC-decrypted bytes; the loose files are already plaintext-after-XOR here).
function findIsf(name) {
  const cands = [
    path.join(base, 'Ran/data/glogic', name),
    path.join(base, 'CLIENT/data/glogic', name),
  ];
  for (const c of cands) if (fs.existsSync(c)) return c;
  return null;
}

module.exports = {
  parseAbl, openContainer, stem, Cursor, ABL_DIR, computeAbfAlias, buildPieces,
  buildBasePieces, buildPieceBones, readRmeshSkinBones, rmeshSkinBonesOf,
};

if (require.main === module) main();
