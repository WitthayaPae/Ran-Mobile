'use strict';
//
// `.effskin` / `.effskin_a` reader — the DxEffect/Char (character-attached
// effect) data format.
//
// Container, from SOURCE/Lib_Engine/DxEffect/Char/DxEffCharData.cpp:
//
//   0    128  CSerialFile type string   — "default" in every shipped file
//   128    4  u32 version               — 0 in every shipped file
//   132       body
//
// The type string and version are NOT this format's own: DxEffCharData never
// writes them, CSerialFile::OpenFile(FOT_WRITE) does. The real discriminator is
// the file extension, exactly as in the two call sites:
//
//   .effskin    DxEffCharData::LoadFile        one piece slot
//   .effskin_a  DxEffCharDataArray::LoadFile   an array of piece slots
//
// Array body (DxEffCharDataArray::LoadFile, line 332):
//   [u32 nSlots] then nSlots x [BOOL bExist][ DxEffCharData body if bExist ]
//
// DxEffCharData body (line 150):
//   [u32 EMPIECECHAR][u32 nEffects] then nEffects x [u32 nBytes][nBytes blob]
//
// Blob (DxEffCharMan::CreateEffInstance + every DxEffChar*::LoadFile):
//   [u32 TypeID][u32 version][u32 declaredSize][blitted EFFCHAR_PROPERTY_*]
//
// The blob length is given by the OUTER nBytes, so this walk needs no
// per-type knowledge at all — unlike `.egp`, where the node size had to be
// corrected by +4/-4. `declaredSize` is checked against it but never trusted.
//
// No encryption: DxEffCharData::LoadFile calls openfile_basestream WITHOUT an
// EMBYTECRYPT argument, so it defaults to EMBYTECRYPT_NONE (GLogic.h). There is
// no version gate here, and three types carry VERSION 0x0200 without triggering
// one — the gate lives in the loader, not in the version number.
//
const fs = require('fs');
const path = require('path');

const HEADER_SIZE = 132;

/** EFFCHAR_TYPES, DxEffChar.h:42 — id -> name. */
const TYPE_NAMES = {
  0: 'SINGLE', 1: 'BLUR', 2: 'CLONEBLUR', 3: 'DUST', 4: 'SHOCK',
  5: 'ATTRIBUTE', 6: 'ARROW', 7: 'SPECULAR', 8: 'LEVEL', 9: 'EMIT',
  10: 'ALPHA', 11: 'NEON', 12: 'MARK', 13: 'NOALPHA', 14: 'REFLECTION2',
  15: 'AMBIENT', 16: 'DOT3', 17: 'MULTITEX', 18: 'GHOSTING', 19: 'SPECULAR2',
  20: 'TOON', 21: 'TEXDIFF', 22: 'PARTICLE', 23: 'BONEPOSEFF',
  24: 'BONELISTEFF', 25: 'USERCOLOR', 26: 'NORMALMAP', 27: 'LINE2BONE',
  28: 'AROUNDEFFECT', 29: 'OVERLAY', 30: 'TOTALSHADER', 31: 'VISUALMATERIAL',
  32: 'SPLINESINGLEEFF', 33: 'WORLDBATTLEFLAG', 34: 'DISABLERENDER',
};

/** DxEffCharMan::CreateEffInstance, DxEffChar.cpp:132 — the ids it can build. */
const INSTANTIABLE = new Set([
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
  20, 21, 22, 23, 24, 25, 26, 27, 28,
]);

/** IsEffectNotSupported, DxEffChar.h:82. */
const NOT_SUPPORTED = new Set([29, 30, 31, 32, 33, 34]);

/** EMPIECECHAR, Meshs/DxPieceDefine.h:10. */
const PIECE_NAMES = {
  0: 'HEAD', 1: 'UPBODY', 2: 'LOBODY', 3: 'GLOVE', 4: 'RHAND', 5: 'LHAND',
  6: 'FOOT', 7: 'HAIR', 8: 'HEADGEAR', 9: 'VEHICLE', 10: 'WING', 11: 'NECK',
  12: 'WRIST', 13: 'RING', 14: 'BELT', 15: 'EAR_RING', 16: 'ACCESSORY',
  17: 'ENCHANT_UPBODY', 18: 'ENCHANT_LOBODY', 19: 'ENCHANT_GLOVE',
  20: 'ENCHANT_FOOT', 21: 'SHIFT_RWEAPON', 22: 'SHIFT_LWEAPON',
};

function readHeader(buf) {
  let end = 0;
  while (end < 128 && buf[end] !== 0) end++;
  return {
    type: buf.toString('latin1', 0, end),
    version: buf.readUInt32LE(128),
  };
}

/** Walk one DxEffCharData body starting at `off`. Returns {piece, effects, next}. */
function readCharData(buf, off) {
  const piece = buf.readUInt32LE(off);
  const count = buf.readUInt32LE(off + 4);
  off += 8;

  const effects = [];
  for (let i = 0; i < count; i++) {
    if (off + 4 > buf.length) throw new Error(`truncated at effect ${i} size`);
    const nBytes = buf.readUInt32LE(off);
    off += 4;
    if (nBytes < 12 || off + nBytes > buf.length) {
      throw new Error(`effect ${i}: implausible blob length ${nBytes}`);
    }
    const blob = buf.subarray(off, off + nBytes);
    effects.push({
      typeId: blob.readUInt32LE(0),
      version: blob.readUInt32LE(4),
      declaredSize: blob.readUInt32LE(8),
      bodyLength: nBytes - 12,
      blob,
    });
    off += nBytes;
  }
  return { piece, effects, next: off };
}

/**
 * Parse a whole `.effskin` / `.effskin_a`.
 * @param {Buffer} buf whole file, RCC-decrypted
 * @param {string} name used only to pick array vs single by extension
 */
function parse(buf, name) {
  const header = readHeader(buf);
  const isArray = /\.effskin_a(_before)?$/i.test(name);
  const slots = [];
  let off = HEADER_SIZE;
  let declaredSlots = null;

  if (isArray) {
    declaredSlots = buf.readUInt32LE(off);
    off += 4;
    for (let i = 0; i < declaredSlots; i++) {
      const exists = buf.readUInt32LE(off);
      off += 4;
      if (!exists) continue;
      const d = readCharData(buf, off);
      slots.push(d);
      off = d.next;
    }
  } else {
    const d = readCharData(buf, off);
    slots.push(d);
    off = d.next;
  }

  return {
    name,
    header,
    isArray,
    declaredSlots,
    slots,
    consumed: off,
    length: buf.length,
    exactEof: off === buf.length,
    trailing: buf.length - off,
  };
}

/** Every NUL-terminated printable run of >= 3 chars in a blob body. */
function strings(blob, min = 3) {
  const out = [];
  let cur = '';
  for (let i = 12; i < blob.length; i++) {
    const c = blob[i];
    if (c >= 0x20 && c < 0x7f) { cur += String.fromCharCode(c); continue; }
    if (c === 0 && cur.length >= min) out.push(cur);
    cur = '';
  }
  if (cur.length >= min) out.push(cur);
  return out;
}

module.exports = {
  parse, readCharData, readHeader, strings,
  TYPE_NAMES, INSTANTIABLE, NOT_SUPPORTED, PIECE_NAMES, HEADER_SIZE,
};
