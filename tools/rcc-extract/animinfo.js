'use strict';
//
// Animation clip METADATA — the clip -> action classification an Animator needs.
//
// Two formats live here, because they answer two halves of the same question:
//
//   .cfg  SANIMCONINFO   (Animation.rcc, SkinObject.rcc)
//         WHAT a clip is: EMANI_MAINTYPE / EMANI_SUBTYPE, start/end time,
//         loop flag, the skeleton it was authored against, hit frames, sounds.
//         One `.cfg` per animation `.bin`, sharing the file stem.
//
//   .chf  DxSkinCharData      (SkinObject.rcc)
//   .abf  DxAttBoneData       (SkinObject.rcc)
//         WHICH clips a character owns: skeleton name, body-piece list, and the
//         list of animation files to load. No types — see the note below.
//
// `.chf` and `.abf` are NOT the same format. They share the `EMBYTECRYPT_CONTAINER`
// gate, the "version is the first body dword" quirk and most field order, which
// makes one parser look like it works for both — it does for 0x0200 minus a
// trailing BOOL, and it does not for the older ones: `DxSkinCharData` has nine
// body versions with three different field orders, `DxAttBoneData` has exactly
// two loaders (0x0100-0x0102 and 0x0200). Running every `.abf` through the
// `.chf` reader fails outright on 315 of 509 files, which is the good outcome;
// the bad one would have been a version where it silently half-parses.
//
// ---------------------------------------------------------------------------
// Where the main type is NOT
// ---------------------------------------------------------------------------
//
// It is not in the animation `.bin` (`SAnimContainer::Load_*` reads only bone
// tracks), and it is NOT in `.chf`/`.abf` either. `DxSkinCharData::LOAD_0200`
// and every older LOAD_* read the animation list as bare file NAMES — the
// engine then calls `DxSkinAniControl::LoadAnimation(name)`, which lands in
// `DxSkinAniMan::LoadAnimContainer`, which swaps the extension `.x` -> `.cfg`
// and calls `SANIMCONINFO::LoadFile` on THAT (DxSkinAniMan.cpp:562-565).
// `ClassifyAnimation` then buckets each clip by `m_MainType`/`m_SubType`, which
// only exist on the SANIMCONINFO the `.cfg` supplied.
//
// So `.chf` -> clip names, `.cfg` -> clip types. Both are needed; neither is
// sufficient. Decoding a `.chf` with EMBYTECRYPT_CONTAINER does yield real clip
// names (that is the animation list), which is easy to mistake for the mapping
// itself.
//
// ---------------------------------------------------------------------------
// Struct sizes
// ---------------------------------------------------------------------------
//
// Every blitted size below comes from `../layout-probe/layout.json`, i.e. from
// MSVC compiling the real headers. None is computed here. The ones that matter:
//
//   SANIMCONINFO_101   304    (302 of payload + 2 bytes tail padding)
//   SANIMCONINFO_102   564
//   SANIMCONINFO_103   732
//   SANIMCONINFO_104   840
//   SANIMSTRIKE         12  x9 = 108
//   SChaSoundData_102  260
//   SChaSoundData_103  428
//   SChaSoundData      388
//
// SANIMCONINFO_101 is the cautionary one: its last member ends at 302, but the
// struct is 304 because of tail padding, and `LoadFile_0101` blits
// `sizeof(OldAnimInfo)` — so the record on disk is 304 bytes, not 302.
//
// ---------------------------------------------------------------------------
// Encryption
// ---------------------------------------------------------------------------
//
// Both formats follow the version gate CRYPT-MAP.md documents as the norm:
//
//   .cfg  `dwVersion >= 0x0200` -> EMBYTECRYPT_CFG        from byte 132
//         version is the CSerialFile header version at byte 128
//   .chf  `dwVer >= 0x0200`     -> EMBYTECRYPT_CONTAINER  from byte 136
//         version is the first BODY dword (byte 132), like `.cps` — the
//         CSerialFile header of a `.chf` says type "default", version 0.
//
const LAYOUT = require('../layout-probe/layout.json');
const BC = require('./bytecrypt');

const S = LAYOUT.allStructs;
const E = LAYOUT.allEnums;

function structSize(name) {
  const s = S[name];
  if (!s) throw new Error(`layout.json has no struct "${name}" — rerun MOBILE/tools/layout-probe/build-structs.js`);
  return s.size;
}
function fieldOff(struct, field) {
  const f = S[struct] && S[struct].fields[field];
  if (!f) throw new Error(`layout.json has no ${struct}.${field}`);
  return f.off;
}
function enumVal(name) {
  const v = E[name];
  if (v === undefined) throw new Error(`layout.json has no enum "${name}"`);
  return v;
}

// ACF_SZNAME / ACF_DIV / ACF_STRIKE — from the compiler, like everything else.
const ACF_SZNAME = enumVal('ACF_SZNAME');
const ACF_DIV = enumVal('ACF_DIV');
const ACF_STRIKE = enumVal('ACF_STRIKE');
const ACF_LOOP = enumVal('ACF_LOOP');
const ACF_UPBODY = enumVal('ACF_UPBODY');
const ACF_DOWNBODY = enumVal('ACF_DOWNBODY');
const ACF_NEWINFO = enumVal('ACF_NEWINFO');

const SZ_STRIKE = structSize('SANIMSTRIKE') * ACF_STRIKE;
const SZ_SOUND = structSize('SChaSoundData');
const SZ_SOUND_102 = structSize('SChaSoundData_102');
const SZ_SOUND_103 = structSize('SChaSoundData_103');

const VERSION_ENCODE = 0x0200; // SANIMCONINFO::VERSION_ENCODE (SAnimationInfo.h)

// EMANI_MAINTYPE overlays four independent numbering schemes on one C++ enum:
// character (AN_GUARD_N..AN_REARCAR_A), ABL (AN_ABL_*), vehicle (AN_VEHICLE_*)
// and the generic AN_SUB_nn ladder. A value -> name map is therefore ambiguous
// unless the scheme is chosen first. These lists name the CHARACTER scheme; the
// VALUES still come from the compiler. Naming by hand is safe, valuing is not.
const MAIN_TYPE_NAMES = [
  'AN_GUARD_N', 'AN_PLACID', 'AN_WALK', 'AN_RUN', 'AN_ATTACK', 'AN_SHOCK',
  'AN_DIE', 'AN_CREATE', 'AN_TALK', 'AN_SKILL_A', 'AN_GESTURE', 'AN_SHOCK_MIX',
  'AN_GUARD_L', 'AN_CONFT_WIN', 'AN_CONFT_LOSS', 'AN_SPEC', 'AN_SKILL_B',
  'AN_SKILL_C', 'AN_SKILL_D', 'AN_SKILL_E', 'AN_GATHERING', 'AN_BIKE_A',
  'AN_BIKE_B', 'AN_BIKE_C', 'AN_BIKE_EV1', 'AN_FLIGHT', 'AN_SKILL_F',
  'AN_SKILL_G', 'AN_SKILL_H', 'AN_CAR_A', 'AN_CAR_B', 'AN_CAR_C', 'AN_CAR_D',
  'AN_REARCAR_A', 'AN_NONE',
];

const SUB_TYPE_NAMES = [
  'AN_SUB_NONE', 'AN_SUB_ONEHSWORD', 'AN_SUB_TWOHSWORD', 'AN_SUB_EITHERSWORD',
  'AN_SUB_DAGGER', 'AN_SUB_SPEAR', 'AN_SUB_BOW', 'AN_SUB_THROW', 'AN_SUB_MANA',
  'AN_SUB_BIGHSWORD', 'AN_SUB_STICK', 'AN_SUB_ONEBLADE', 'AN_SUB_TWOBLADE',
  'AN_SUB_BIGBLADE', 'AN_SUB_EITHERBLADE', 'AN_SUB_GAUNT', 'AN_SUB_BROOM',
  'AN_SUB_HOVERBOARD', 'AN_SUB_DUALGUN', 'AN_SUB_GUN', 'AN_SUB_SCYTHE',
  'AN_SUB_DUALSPEAR', 'AN_SUB_SHURIKEN', 'AN_SUB_EXTREME_FIST',
  'AN_SUB_TRICKER_WAND', 'AN_SUB_TRICKER_CUBE', 'AN_SUB_TRICKER_ROPE',
  'AN_SUB_SHAPER_HAMMER', 'AN_SUB_SHAPER_SHIELD', 'AN_SUB_SHAPER_UMBRELLA',
];

function buildTypeTable(names) {
  const byValue = new Map();
  for (const n of names) {
    const v = enumVal(n);
    if (!byValue.has(v)) byValue.set(v, n);
  }
  return byValue;
}
const MAIN_BY_VALUE = buildTypeTable(MAIN_TYPE_NAMES);
const SUB_BY_VALUE = buildTypeTable(SUB_TYPE_NAMES);

const AN_TYPE_SIZE = enumVal('AN_TYPE_SIZE');
const AN_SUB_00_SIZE = enumVal('AN_SUB_00_SIZE');

/** Character-scheme name for an EMANI_MAINTYPE value, or null if unnamed. */
const mainTypeName = (v) => MAIN_BY_VALUE.get(v) || null;
/**
 * EMANI_SUBTYPE name. Beyond the weapon block the enum degenerates to the
 * generic AN_SUB_nn ladder (0..99), which is what skill variants use, so
 * anything past the named weapons is reported as AN_SUB_nn rather than as a
 * weapon it is not.
 */
function subTypeName(v) {
  if (SUB_BY_VALUE.has(v)) return SUB_BY_VALUE.get(v);
  if (v >= 0 && v < AN_SUB_00_SIZE) return `AN_SUB_${String(v).padStart(2, '0')}`;
  return null;
}

// ---------------------------------------------------------------------------
// Cursor
// ---------------------------------------------------------------------------

class Cursor {
  constructor(buf, off = 0) { this.buf = buf; this.off = off; }
  get left() { return this.buf.length - this.off; }
  need(n) {
    if (n < 0 || this.off + n > this.buf.length) {
      throw new Error(`truncated: need ${n} at ${this.off}, have ${this.left}`);
    }
  }
  u32() { this.need(4); const v = this.buf.readUInt32LE(this.off); this.off += 4; return v; }
  i32() { this.need(4); const v = this.buf.readInt32LE(this.off); this.off += 4; return v; }
  u16() { this.need(2); const v = this.buf.readUInt16LE(this.off); this.off += 2; return v; }
  f32() { this.need(4); const v = this.buf.readFloatLE(this.off); this.off += 4; return v; }
  bytes(n) { this.need(n); const b = this.buf.subarray(this.off, this.off + n); this.off += n; return b; }
  skip(n) { this.need(n); this.off += n; }
  /** Fixed-width NUL-padded char array. */
  fixedStr(n) { return cstr(this.bytes(n)); }
  /**
   * CSerialFile `operator >> (std::string&)`: `[u32 len][len bytes]`, where len
   * INCLUDES the NUL — the reader assigns straight from the buffer and relies
   * on the stored terminator. Getting this off by one shifts every later field.
   */
  str() {
    const n = this.u32();
    if (n > 0x10000) throw new Error(`implausible string length ${n} at ${this.off - 4}`);
    if (n === 0) return '';
    return cstr(this.bytes(n));
  }
  vec3() { return { x: this.f32(), y: this.f32(), z: this.f32() }; }
}

function cstr(buf) {
  const nul = buf.indexOf(0);
  return buf.toString('latin1', 0, nul === -1 ? buf.length : nul);
}

// ---------------------------------------------------------------------------
// Shared sub-readers
// ---------------------------------------------------------------------------

/**
 * The animation-effect list.
 *
 * `SANIMCONINFO::SaveFile` writes the type id, then calls the effect's own
 * `SaveFile`, which writes the type id AGAIN followed by version and size. So
 * the on-disk record is `[typeID][typeID][version][size][size bytes]` — the
 * doubled id is real, not a misread.
 *
 * All four effect kinds (SINGLE/GHOSTING/TRACE/FACEOFF) share that framing and
 * `size` is `sizeof(m_Property)` in every writer, so the payload can be skipped
 * by size on every version, known or not. That is what the engine itself does
 * on an unknown version (`SetOffSet(GetfTell()+dwSize)`).
 *
 * The one way this desyncs is an unregistered type id: `CreateEffInstance`
 * returns NULL, the engine reads nothing and the stream is lost. Treated as an
 * error here rather than silently continuing.
 */
const EFF_TYPE_NAMES = ['SINGLE', 'GHOSTING', 'TRACE', 'FACEOFF'];

function readEffectList(c) {
  const count = c.u32();
  if (count > 4096) throw new Error(`implausible effect count ${count}`);
  const out = [];
  for (let i = 0; i < count; i++) {
    const outerType = c.u32();
    if (outerType >= EFF_TYPE_NAMES.length) {
      // The engine's CreateEffInstance would return NULL here and desync too.
      throw new Error(`unknown effect type id ${outerType}`);
    }
    const innerType = c.u32();
    const version = c.u32();
    const size = c.u32();
    if (innerType !== outerType) {
      throw new Error(`effect type id mismatch ${outerType} vs ${innerType}`);
    }
    const body = c.bytes(size);
    out.push({ type: outerType, typeName: EFF_TYPE_NAMES[outerType], version, size, body });
  }
  return out;
}

/** `DxAniScale::Load` — `[u32 n]` of `[string bone][u32 m]` of `[u32 key][f32 scale]`. */
function readAniScale(c) {
  const count = c.u32();
  if (count > 4096) throw new Error(`implausible ani-scale count ${count}`);
  const out = [];
  for (let i = 0; i < count; i++) {
    const bone = c.str();
    const n = c.u32();
    if (n > 65536) throw new Error(`implausible ani-scale key count ${n}`);
    const keys = [];
    for (let k = 0; k < n; k++) keys.push({ key: c.u32(), scale: c.f32() });
    out.push({ bone, keys });
  }
  return out;
}

function readStrikes(c, count) {
  const raw = c.bytes(SZ_STRIKE);
  const stride = structSize('SANIMSTRIKE');
  const pieceOff = fieldOff('SANIMSTRIKE', 'm_emPiece');
  const effOff = fieldOff('SANIMSTRIKE', 'm_emEffect');
  const frameOff = fieldOff('SANIMSTRIKE', 'm_dwFrame');
  const out = [];
  for (let i = 0; i < Math.min(count, ACF_STRIKE); i++) {
    const b = i * stride;
    out.push({
      piece: raw.readUInt32LE(b + pieceOff),
      effect: raw.readUInt32LE(b + effOff),
      frame: raw.readUInt32LE(b + frameOff),
    });
  }
  return out;
}

function readDivFrames(c, count) {
  const raw = c.bytes(2 * ACF_DIV);
  const out = [];
  for (let i = 0; i < Math.min(count, ACF_DIV); i++) out.push(raw.readUInt16LE(i * 2));
  return out;
}

/** `SChaSoundData*` blit — the file names are what a port can actually use. */
function readSound(c, size) {
  const raw = c.bytes(size);
  const struct = size === SZ_SOUND ? 'SChaSoundData'
    : size === SZ_SOUND_103 ? 'SChaSoundData_103' : 'SChaSoundData_102';
  const nameOff = fieldOff(struct, 'm_szFileName');
  const nameSize = S[struct].fields.m_szFileName.size;
  const SLOT = 32; // STRING_NUM_32 — the row width of m_szFileName[N][32]
  const slots = nameSize / SLOT;
  const files = [];
  for (let i = 0; i < slots; i++) {
    const s = cstr(raw.subarray(nameOff + i * SLOT, nameOff + (i + 1) * SLOT));
    if (s) files.push(s);
  }
  const frames = [];
  const frameOff = fieldOff(struct, 'm_PlayFrame');
  const frameCount = S[struct].fields.m_PlayFrame.size / 2;
  for (let i = 0; i < frameCount; i++) frames.push(raw.readUInt16LE(frameOff + i * 2));
  return { files, frames };
}

// ---------------------------------------------------------------------------
// SANIMCONINFO (.cfg)
// ---------------------------------------------------------------------------

/**
 * Blitted versions 0x0101..0x0104: `SFile.ReadBuffer(&s, sizeof(s))`. Field
 * offsets AND the record stride are read out of layout.json.
 */
function readBlitted(buf, structName, hasStrike, soundSize) {
  const size = structSize(structName);
  if (buf.length < size) {
    throw new Error(`${structName}: body is ${buf.length} bytes, struct is ${size}`);
  }
  const F = (f) => fieldOff(structName, f);
  const rec = {
    name: cstr(buf.subarray(F('m_szName'), F('m_szName') + ACF_SZNAME)),
    skeleton: cstr(buf.subarray(F('m_szSkeletion'), F('m_szSkeletion') + ACF_SZNAME)),
    flag: buf.readUInt32LE(F('m_dwFlag')),
    startTime: buf.readUInt32LE(F('m_dwSTime')),
    endTime: buf.readUInt32LE(F('m_dwETime')),
    endTimeOrig: buf.readUInt32LE(F('m_dwETimeOrig')),
    unitTime: buf.readUInt32LE(F('m_UNITTIME')),
    mainType: buf.readInt32LE(F('m_MainType')),
    subType: buf.readInt32LE(F('m_SubType')),
    divCount: buf.readUInt16LE(F('m_wDivCount')),
    divFrames: [],
    strikeCount: 0,
    strikes: [],
    sound: null,
    effects: [],
    aniScale: [],
  };
  const divOff = F('m_wDivFrame');
  for (let i = 0; i < Math.min(rec.divCount, ACF_DIV); i++) {
    rec.divFrames.push(buf.readUInt16LE(divOff + i * 2));
  }
  if (hasStrike) {
    rec.strikeCount = buf.readUInt16LE(F('m_wStrikeCount'));
    const stride = structSize('SANIMSTRIKE');
    const base = F('m_sStrikeEff');
    for (let i = 0; i < Math.min(rec.strikeCount, ACF_STRIKE); i++) {
      rec.strikes.push({
        piece: buf.readUInt32LE(base + i * stride + fieldOff('SANIMSTRIKE', 'm_emPiece')),
        effect: buf.readUInt32LE(base + i * stride + fieldOff('SANIMSTRIKE', 'm_emEffect')),
        frame: buf.readUInt32LE(base + i * stride + fieldOff('SANIMSTRIKE', 'm_dwFrame')),
      });
    }
  }
  if (soundSize) {
    const c = new Cursor(buf, F('m_ChaSoundData'));
    rec.sound = readSound(c, soundSize);
  }
  return { rec, consumed: size };
}

/** Fields shared by every field-by-field loader, in the given order. */
function readCore(c, rec, order) {
  for (const f of order) {
    switch (f) {
      case 'flag': rec.flag = c.u32(); break;
      case 'sTime': rec.startTime = c.u32(); break;
      case 'eTime': rec.endTime = c.u32(); break;
      case 'eTimeOrig': rec.endTimeOrig = c.u32(); break;
      case 'unit': rec.unitTime = c.u32(); break;
      case 'types':
        rec.mainType = c.i32();
        rec.subType = c.i32();
        break;
      case 'div':
        rec.divCount = c.u16();
        rec.divFrames = readDivFrames(c, rec.divCount);
        break;
      case 'strike':
        rec.strikeCount = c.u16();
        rec.strikes = readStrikes(c, rec.strikeCount);
        break;
      default: throw new Error(`bad core field "${f}"`);
    }
  }
}

/**
 * Parse one `.cfg` (SANIMCONINFO).
 *
 * @param {Buffer} buf     raw entry bytes, RCC-decrypted, header included
 * @param {object} [opt]   {strict:true} rejects a body not consumed to EOF
 */
function parseAnimInfo(buf, opt = {}) {
  const hdr = BC.readHeader(buf);
  if (!hdr) throw new Error('too short for a CSerialFile header');
  if (hdr.type !== 'SANIMCONINFO') throw new Error(`unexpected file type "${hdr.type}"`);

  const version = hdr.version;
  let body = buf.subarray(hdr.bodyOffset);
  if (version >= VERSION_ENCODE) {
    body = BC.decode(Buffer.from(body), 'EMBYTECRYPT_CFG');
  }

  const rec = {
    version,
    name: '',
    skeleton: '',
    flag: 0,
    startTime: 0,
    endTime: 0,
    endTimeOrig: 0,
    unitTime: 0,
    mainType: -1,
    subType: -1,
    divCount: 0,
    divFrames: [],
    strikeCount: 0,
    strikes: [],
    sound: null,
    effects: [],
    aniScale: [],
  };

  // Blitted generations. The whole versioned struct is one ReadBuffer, so the
  // record stride IS sizeof() — including tail padding.
  const BLIT = {
    0x0101: ['SANIMCONINFO_101', false, 0],
    0x0102: ['SANIMCONINFO_102', false, SZ_SOUND_102],
    0x0103: ['SANIMCONINFO_103', false, SZ_SOUND_103],
    0x0104: ['SANIMCONINFO_104', true, SZ_SOUND_103],
  };
  if (BLIT[version]) {
    const [struct, hasStrike, soundSize] = BLIT[version];
    const { rec: r, consumed } = readBlitted(body, struct, hasStrike, soundSize);
    Object.assign(rec, r, { version });
    return finish(rec, consumed, body.length, opt);
  }

  const c = new Cursor(body);

  // 0x0111+ replaced the two fixed-width name arrays with four length-prefixed
  // strings (cfg, x, bin, skeleton) — read with ReadBuffer of exactly the
  // declared length, so the stored NUL is part of the count. m_szName takes the
  // .x name and m_szSkeletion the skeleton; the cfg and bin names are dropped
  // by the engine, and are kept here because they name the sibling files.
  const NEWSTR = [0x0111, 0x0112, 0x0114, 0x0115];

  if (NEWSTR.includes(version)) {
    rec.cfgFile = c.str();
    rec.name = c.str();
    rec.binFile = c.str();
    rec.skeleton = c.str();
    readCore(c, rec, ['flag', 'sTime', 'eTime', 'eTimeOrig', 'unit', 'types', 'div', 'strike']);
    rec.sound = readSound(c, SZ_SOUND);
    rec.effects = readEffectList(c);
    rec.aniScale = readAniScale(c);
    return finish(rec, c.off, body.length, opt);
  }

  rec.name = c.fixedStr(ACF_SZNAME);
  rec.skeleton = c.fixedStr(ACF_SZNAME);

  switch (version) {
    case 0x0105:
      readCore(c, rec, ['flag', 'sTime', 'eTime', 'eTimeOrig', 'unit', 'types', 'div', 'strike']);
      rec.sound = readSound(c, SZ_SOUND_103);
      rec.effects = readEffectList(c);
      break;

    case 0x0106:
      readCore(c, rec, ['flag', 'sTime', 'eTime', 'eTimeOrig', 'unit', 'types', 'div', 'strike']);
      rec.sound = readSound(c, SZ_SOUND);
      rec.effects = readEffectList(c);
      break;

    case 0x0107:
      readCore(c, rec, ['flag', 'sTime', 'eTime', 'eTimeOrig', 'unit', 'types', 'div', 'strike']);
      rec.sound = readSound(c, SZ_SOUND);
      rec.effects = readEffectList(c);
      rec.aniScale = readAniScale(c);
      break;

    // 0x0108+ REORDER the core: m_UNITTIME moves ahead of the three times, the
    // strike block moves ahead of the div block, and the sound blob moves to
    // the very end after the effects and the ani-scale table. Every one of
    // those still "parses" in the old order and yields wrong numbers.
    case 0x0108:
    case 0x0109:
    case 0x0110:
    case 0x0200:
      readCore(c, rec, ['flag', 'unit', 'sTime', 'eTime', 'eTimeOrig', 'types', 'strike', 'div']);
      rec.effects = readEffectList(c);
      rec.aniScale = readAniScale(c);
      rec.sound = readSound(c, SZ_SOUND);
      break;

    default:
      throw new Error(`unsupported SANIMCONINFO version 0x${version.toString(16).padStart(4, '0')}`);
  }

  return finish(rec, c.off, body.length, opt);
}

function finish(rec, consumed, bodyLength, opt) {
  rec.consumed = consumed;
  rec.bytesLeft = bodyLength - consumed;
  rec.loop = !!(rec.flag & ACF_LOOP);
  rec.upperBody = !!(rec.flag & ACF_UPBODY);
  rec.lowerBody = !!(rec.flag & ACF_DOWNBODY);
  rec.newInfo = !!(rec.flag & ACF_NEWINFO);
  rec.mainTypeName = mainTypeName(rec.mainType);
  rec.subTypeName = subTypeName(rec.subType);
  if (opt.strict && rec.bytesLeft !== 0) {
    throw new Error(`body not consumed: ${rec.bytesLeft} bytes left of ${bodyLength}`);
  }
  return rec;
}

// ---------------------------------------------------------------------------
// DxSkinCharData (.chf) and DxAttBoneData (.abf)
// ---------------------------------------------------------------------------
//
// Nine shipped body versions dispatched by `DxSkinCharData::LoadFile`. The field
// ORDER is genuinely different per version — 0x0100 blits a MAX_PATH skeleton
// name and fixed-width animation names, 0x0101+ use length-prefixed strings,
// 0x0104 moves the scale after the animation list, and 0x0200 leads with the
// skeleton. Only the animation NAME LIST is common to all of them, and that is
// what this is for.
//
// Several versions end with a chain of optional `BOOL bExist` blocks that
// `return TRUE` early when clear, so a short body is legal, not truncated.

const PIECE_SIZE = 26; // DxPieceDefine.h PIECE_SIZE — only used to clamp
const MAX_PATH = 260;
const CHF_VERSION_ENCRYPT = 0x0200;

function readPieceList(c) {
  const n = c.u32();
  if (n > 256) throw new Error(`implausible piece count ${n}`);
  const pieces = [];
  for (let i = 0; i < n; i++) {
    const use = c.u32();
    const file = use ? c.str() : '';
    if (i < PIECE_SIZE) pieces.push(file);
  }
  return pieces;
}

function readBoneScaleBlitted(c) {
  // LOAD_0103 / LOAD_0104: `ReadBuffer(&s, sizeof(SBONESCALE_100))`.
  const n = c.u32();
  if (n > 4096) throw new Error(`implausible bone-scale count ${n}`);
  c.skip(n * structSize('SBONESCALE_100'));
  return n;
}

function readBoneScaleStreamed(c) {
  // LOAD_0106 / LOAD_0107_0108: `>> vScale` then `>> strBONE`.
  const n = c.u32();
  if (n > 4096) throw new Error(`implausible bone-scale count ${n}`);
  for (let i = 0; i < n; i++) { c.vec3(); c.str(); }
  return n;
}

function readBounds(c, rec) {
  if (c.u32()) { rec.vMax = c.vec3(); rec.vMin = c.vec3(); }
}

/**
 * Strip the container header and the encryption layer shared by `.chf`/`.abf`.
 *
 * The version is the first BODY dword, not the header version — the CSerialFile
 * header of both reads type "default", version 0. Same trap as `.cps`.
 * `SetEncodeType` is called AFTER reading that dword, so the version itself is
 * plaintext and the encoded region starts 4 bytes later, at absolute 136.
 */
function openContainer(buf) {
  const hdr = BC.readHeader(buf);
  if (!hdr) throw new Error('too short for a CSerialFile header');
  if (buf.length < hdr.bodyOffset + 4) throw new Error('too short for a body version');
  const version = buf.readUInt32LE(hdr.bodyOffset);
  let body = buf.subarray(hdr.bodyOffset + 4);
  if (version >= CHF_VERSION_ENCRYPT) {
    body = BC.decode(Buffer.from(body), 'EMBYTECRYPT_CONTAINER');
  }
  return { version, body };
}

/**
 * Parse one `.chf` (DxSkinCharData).
 *
 * @param {Buffer} buf raw entry bytes, RCC-decrypted, header included
 */
function parseSkinChar(buf, opt = {}) {
  const { version, body } = openContainer(buf);

  const c = new Cursor(body);
  const rec = {
    version,
    skeleton: '',
    pieces: [],
    anims: [],
    scale: null,
    height: null,
    radius: null,
    vMax: null,
    vMin: null,
    worldObj: null,
  };

  const readAnimNames = () => {
    const n = c.u32();
    if (n > 65536) throw new Error(`implausible animation count ${n}`);
    const out = [];
    for (let i = 0; i < n; i++) out.push(c.str());
    return out;
  };

  try {
    switch (version) {
      case 0x0100:
        rec.pieces = readPieceList(c);
        rec.skeleton = c.fixedStr(MAX_PATH);
        {
          const n = c.u32();
          if (n > 65536) throw new Error(`implausible animation count ${n}`);
          // 0x0100 stores the names as fixed ACF_SZNAME arrays, not strings.
          for (let i = 0; i < n; i++) rec.anims.push(c.fixedStr(ACF_SZNAME));
        }
        readBounds(c, rec);
        break;

      case 0x0101:
      case 0x0102:
        rec.scale = c.f32();
        rec.pieces = readPieceList(c);
        rec.skeleton = c.str();
        rec.anims = readAnimNames();
        readBounds(c, rec);
        if (!c.u32()) break;
        rec.worldObj = c.u32();
        if (!c.u32()) break;
        rec.height = c.f32();
        rec.radius = c.f32();
        c.u32();
        break;

      case 0x0103:
        rec.scale = c.f32();
        rec.pieces = readPieceList(c);
        rec.skeleton = c.str();
        rec.anims = readAnimNames();
        readBounds(c, rec);
        if (!c.u32()) break;
        rec.worldObj = c.u32();
        if (!c.u32()) break;
        rec.height = c.f32();
        rec.radius = c.f32();
        readBoneScaleBlitted(c);
        c.u32();
        break;

      case 0x0104:
        rec.pieces = readPieceList(c);
        rec.skeleton = c.str();
        rec.anims = readAnimNames();
        rec.scale = c.f32();
        if (!c.u32()) break;
        rec.height = c.f32();
        rec.radius = c.f32();
        readBoneScaleBlitted(c);
        readBounds(c, rec);
        if (!c.u32()) break;
        rec.worldObj = c.u32();
        c.u32();
        break;

      case 0x0106:
      case 0x0107:
      case 0x0108:
        rec.pieces = readPieceList(c);
        rec.skeleton = c.str();
        rec.anims = readAnimNames();
        rec.scale = c.f32();
        c.f32(); // unnamed float the engine reads and discards
        if (!c.u32()) break;
        rec.height = c.f32();
        rec.radius = c.f32();
        readBoneScaleStreamed(c);
        readBounds(c, rec);
        if (!c.u32()) break;
        rec.worldObj = c.u32();
        c.u32();
        // LOAD_0107_0108 comments that data still follows; the engine stops
        // here, so bytesLeft > 0 is expected on these and is not an error.
        break;

      case 0x0200:
        rec.skeleton = c.str();
        rec.pieces = readPieceList(c);
        rec.anims = readAnimNames();
        rec.scale = c.f32();
        rec.height = c.f32();
        rec.radius = c.f32();
        rec.vMax = c.vec3();
        rec.vMin = c.vec3();
        rec.worldObj = c.u32();
        break;

      default:
        throw new Error(`unsupported DxSkinCharData version 0x${version.toString(16).padStart(4, '0')}`);
    }
  } catch (err) {
    if (!opt.tolerant) throw err;
    rec.error = err.message;
  }

  // 0x0106+ store the animation name with a `.cfg`/`.bin` extension that the
  // engine rewrites to `.x` before use (LOAD_0106: ReverseFind('.') + ".x").
  // The stem is the identity that matters, so it is exposed directly.
  rec.animStems = rec.anims.map((a) => a.replace(/\.[^.\\/]*$/, '').toLowerCase());

  rec.consumed = c.off;
  rec.bytesLeft = body.length - c.off;
  return rec;
}

/**
 * Parse one `.abf` (DxAttBoneData — the ABL / attach-bone system, 2017).
 *
 * Only two loaders exist: `LOAD_Ver_0100_0102` for 0x0100/0x0101/0x0102 and
 * `LOAD_Ver_0200`. The 0x0200 body is the `.chf` 0x0200 body plus a trailing
 * `BOOL`; the older one is close to `.chf` 0x0101 but has no leading scale and
 * puts the bone-scale table between the height/radius pair and the bounds.
 */
function parseAttBoneData(buf, opt = {}) {
  const { version, body } = openContainer(buf);

  const c = new Cursor(body);
  const rec = {
    version,
    skeleton: '',
    pieces: [],
    anims: [],
    scale: null,
    height: null,
    radius: null,
    vMax: null,
    vMin: null,
    worldObj: null,
  };

  const readAnimNames = () => {
    const n = c.u32();
    if (n > 65536) throw new Error(`implausible animation count ${n}`);
    const out = [];
    for (let i = 0; i < n; i++) out.push(c.str());
    return out;
  };

  try {
    switch (version) {
      case 0x0100:
      case 0x0101:
      case 0x0102:
        rec.pieces = readPieceList(c);
        rec.skeleton = c.str();
        rec.anims = readAnimNames();
        rec.scale = c.f32();
        if (!c.u32()) break;
        rec.height = c.f32();
        rec.radius = c.f32();
        readBoneScaleStreamed(c);
        readBounds(c, rec);
        if (!c.u32()) break;
        rec.worldObj = c.u32();
        c.u32();
        break;

      case 0x0200:
        rec.skeleton = c.str();
        rec.pieces = readPieceList(c);
        rec.anims = readAnimNames();
        rec.scale = c.f32();
        rec.height = c.f32();
        rec.radius = c.f32();
        rec.vMax = c.vec3();
        rec.vMin = c.vec3();
        rec.worldObj = c.u32();
        c.u32(); // trailing BOOL that DxSkinCharData 0x0200 does NOT write
        break;

      default:
        throw new Error(`unsupported DxAttBoneData version 0x${version.toString(16).padStart(4, '0')}`);
    }
  } catch (err) {
    if (!opt.tolerant) throw err;
    rec.error = err.message;
  }

  rec.animStems = rec.anims.map((a) => a.replace(/\.[^.\\/]*$/, '').toLowerCase());
  rec.consumed = c.off;
  rec.bytesLeft = body.length - c.off;
  return rec;
}

/** Dispatch on extension: `.chf` -> DxSkinCharData, `.abf` -> DxAttBoneData. */
function parseCharContainer(buf, fileName, opt = {}) {
  return /\.abf$/i.test(fileName) ? parseAttBoneData(buf, opt) : parseSkinChar(buf, opt);
}

module.exports = {
  parseAnimInfo,
  parseSkinChar,
  parseAttBoneData,
  parseCharContainer,
  mainTypeName,
  subTypeName,
  MAIN_TYPE_NAMES,
  SUB_TYPE_NAMES,
  AN_TYPE_SIZE,
  AN_SUB_00_SIZE,
  ACF_SZNAME,
  ACF_DIV,
  ACF_STRIKE,
  ACF_LOOP,
  VERSION_ENCODE,
  Cursor,
};
