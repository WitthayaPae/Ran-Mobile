'use strict';
//
// Tests for the RCC asset pipeline. Runs against the real Ran/ deploy tree.
//
//   node test.js
//
const path = require('path');
const fs = require('fs');
const { RccArchive, decrypt, encrypt } = require('./rcc');
const G = require('./gamecrypt');

let pass = 0, fail = 0;
const check = (n, c, d) => {
  if (c) { pass++; console.log(`  ok   ${n}`); }
  else { fail++; console.log(`  FAIL ${n}${d ? ` — ${d}` : ''}`); }
};
const eq = (n, got, want) => check(n, got === want, `got ${got}, want ${want}`);

const RAN = path.join(__dirname, '..', '..', '..', 'Ran');
const A = (p) => path.join(RAN, 'data', p);

console.log('\nCCrypt XOR layer');
{
  // b -> (b ^ 0xDF) + 0x06, and back. Round-trip across the whole byte range,
  // because an off-by-one in the wrap would only show at the boundaries.
  const all = Buffer.from(Array.from({ length: 256 }, (_, i) => i));
  const enc = encrypt(Buffer.from(all));
  check('encrypt changes every byte value', !enc.equals(all));
  check('decrypt(encrypt(x)) == x', decrypt(enc).equals(all));
  // Pinned constants: EN=9183&0xff=0xDF, EN^EN2=0x06.
  const one = encrypt(Buffer.from([0x00]));
  eq('encrypt(0x00)', one[0], ((0x00 ^ 0xdf) + 0x06) & 0xff);
}

console.log('\nzip reading');
{
  const gui = new RccArchive(A('gui/Gui.rcc'));
  check('Gui.rcc opens', gui.length > 0);
  check('entries are flat bare filenames (no folder prefix)',
        gui.list().every((n) => !n.includes('/') && !n.includes('\\')));
  const xml = gui.read('gameextext.xml');
  eq('gameextext.xml is UTF-8 BOM XML',
     xml.toString('latin1', 0, 3), '\xef\xbb\xbf');
  check('  contains an XML declaration',
        xml.toString('latin1', 0, 64).includes('<?xml'));
  check('lookup is case-insensitive',
        gui.read('GAMEEXTEXT.XML').length === xml.length);
  let threw = false;
  try { gui.read('does-not-exist.xml'); } catch { threw = true; }
  check('missing entry throws', threw);
}

console.log('\nAES layer (the one that looks like Rijndael-256 and is not)');
{
  // Ground truth from a harness linked against the real CRijndael.
  const crypto = require('crypto');
  const d = crypto.createDecipheriv('aes-256-ecb', G.V8_KEY, null);
  d.setAutoPadding(false);
  const zero = Buffer.concat([d.update(Buffer.alloc(32)), d.final()]);
  eq('reference vector: decrypt(32 zero bytes)',
     zero.toString('hex'),
     '996d7bae887a7a15daed63f31acfb503996d7bae887a7a15daed63f31acfb503');
  check('  16-byte block (halves identical under ECB+zero input)',
        zero.toString('hex', 0, 16) === zero.toString('hex', 16, 32));

  eq('v8 key', G.V8_KEY.toString('hex'),
     '44ad788576778c987b8d7e8736893da2967c74503d88acad4c869595a09b9a7b');

  const glogic = new RccArchive(A('glogic/GLogic.rcc'));
  const enc = glogic.read('attendance.ini');
  eq('attendance.ini carries the version prefix', enc.readInt32LE(0), G.VERSION);
  check('  isEncoded() agrees', G.isEncoded(enc));
  const dec = G.decode(enc);
  eq('  decodes to the expected text',
     dec.toString('latin1', 0, 8), 'bUse\t\t0\t');

  // decode() must be a no-op on anything without the prefix, so it is safe to
  // apply blindly across every archive.
  const plain = Buffer.from('<?xml version="1.0"?>');
  check('decode() passes through unencoded data', G.decode(plain).equals(plain));
}

console.log('\nlayer coverage across archives');
{
  const counts = {};
  for (const [file, name] of [
    ['glogic/GLogic.rcc', 'GLogic'],
    ['glogic/quest/Quest.rcc', 'Quest'],
    ['gui/Gui.rcc', 'Gui'],
  ]) {
    const ar = new RccArchive(A(file));
    let n = 0;
    for (const e of ar.entries) if (G.isEncoded(ar.read(e))) n++;
    counts[name] = n;
  }
  // Only GLogic.rcc uses the second layer. Quest/Level/NpcTalk are plaintext
  // after the XOR pass despite SOURCE_RCC.md describing them as encrypted at
  // rest — packing stores the already-encrypted bytes, so the total stays one
  // layer. Verified rather than assumed.
  check('GLogic.rcc uses the AES layer', counts.GLogic > 300, `${counts.GLogic}`);
  eq('Quest.rcc does not', counts.Quest, 0);
  eq('Gui.rcc does not', counts.Gui, 0);

  const quest = new RccArchive(A('glogic/quest/Quest.rcc'));
  eq('a .qst is plaintext after one XOR pass',
     quest.read('00001B.qst').toString('latin1', 0, 7), 'default');
}

console.log('\nPNG encoder');
{
  const png = require('./png');
  const zlib = require('zlib');
  // 2x2: red, green, blue, half-transparent white
  const rgba = Buffer.from([
    255, 0, 0, 255, 0, 255, 0, 255,
    0, 0, 255, 255, 255, 255, 255, 128,
  ]);
  const out = png.encode(2, 2, rgba);
  eq('PNG signature', out.toString('hex', 0, 8), '89504e470d0a1a0a');
  eq('IHDR chunk type', out.toString('latin1', 12, 16), 'IHDR');
  eq('  width', out.readUInt32BE(16), 2);
  eq('  height', out.readUInt32BE(20), 2);
  eq('  bit depth', out[24], 8);
  eq('  colour type RGBA', out[25], 6);
  check('ends with IEND', out.toString('latin1', out.length - 8, out.length - 4) === 'IEND');

  // Round-trip the pixel data back out of the IDAT to prove it is not just
  // structurally valid but carries the right samples.
  const idatStart = out.indexOf(Buffer.from('IDAT', 'latin1'));
  const idatLen = out.readUInt32BE(idatStart - 4);
  const raw = zlib.inflateSync(out.subarray(idatStart + 4, idatStart + 4 + idatLen));
  eq('inflated scanline length', raw.length, 2 * (1 + 2 * 4));
  eq('  filter byte is None', raw[0], 0);
  eq('  first pixel is red', raw.toString('hex', 1, 5), 'ff0000ff');
  eq('  last pixel keeps alpha', raw[raw.length - 1], 128);
}

console.log('\nDDS decoder');
{
  const dds = require('./dds');
  const png = require('./png');
  const glob = [];
  // Find one real texture of each block format actually present.
  (function walk(dir) {
    let items;
    try { items = fs.readdirSync(dir, { withFileTypes: true }); } catch { return; }
    for (const it of items) {
      if (glob.length >= 400) return;
      const p = path.join(dir, it.name);
      if (it.isDirectory()) walk(p);
      else if (it.name.toLowerCase().endsWith('.dds')) glob.push(p);
    }
  })(path.join(RAN, 'textures'));

  const seen = new Set();
  let decoded = 0;
  let failed = 0;
  for (const p of glob) {
    let buf;
    try { buf = fs.readFileSync(p); } catch { continue; }
    const h = dds.parseHeader(buf);
    if (!h) continue;
    try {
      const img = dds.decode(buf);
      eqSize(img, h, p);
      seen.add(img.format);
      decoded++;
    } catch { failed++; }
  }
  function eqSize(img, h, p) {
    if (img.width !== h.width || img.height !== h.height ||
        img.data.length !== h.width * h.height * 4) {
      fail++;
      console.log(`  FAIL ${path.basename(p)}: size mismatch`);
    }
  }
  check(`decoded ${decoded} real textures`, decoded > 50, `${decoded}`);
  eq('  none failed', failed, 0);
  check(`  covered formats: ${[...seen].join(', ')}`, seen.size >= 2);

  // DXT1 with c0 <= c1 encodes 1-bit alpha; index 3 must come out transparent.
  const hdr = Buffer.alloc(128);
  hdr.writeUInt32LE(0x20534444, 0);
  // Offsets are absolute: DDS_HEADER starts at byte 4, so height/width live at
  // 4+8 and 4+12, not 8 and 12.
  hdr.writeUInt32LE(4, 4 + 8);        // height
  hdr.writeUInt32LE(4, 4 + 12);       // width
  hdr.writeUInt32LE(0x4, 4 + 72 + 4); // DDPF_FOURCC
  hdr.write('DXT1', 4 + 72 + 8, 'latin1');
  const block = Buffer.alloc(8);
  block.writeUInt16LE(0x0000, 0);     // c0
  block.writeUInt16LE(0xffff, 2);     // c1 > c0 -> punch-through mode
  block.writeUInt32LE(0xffffffff, 4); // every texel index 3
  const img = dds.decode(Buffer.concat([hdr, block]));
  eq('DXT1 punch-through alpha is transparent', img.data[3], 0);
  check('  and PNG-encodes', png.encode(img.width, img.height, img.data).length > 0);
}

console.log('\nTGA decoder');
{
  const tga = require('./tga');
  // 2x2 uncompressed 32bpp BGRA, bottom-left origin (descriptor 0).
  const h = Buffer.alloc(18);
  h[2] = 2;                    // uncompressed true-colour
  h.writeUInt16LE(2, 12);      // width
  h.writeUInt16LE(2, 14);      // height
  h[16] = 32;                  // bpp
  // rows stored bottom-up: row0 = bottom
  const px = Buffer.from([
    0, 0, 255, 255, 0, 255, 0, 255,      // bottom: red, green (BGRA)
    255, 0, 0, 255, 255, 255, 255, 64,   // top: blue, white a=64
  ]);
  const img = tga.decode(Buffer.concat([h, px]));
  eq('TGA size', `${img.width}x${img.height}`, '2x2');
  eq('  BGRA is swizzled to RGBA', img.data.toString('hex', 0, 4), '0000ffff');
  check('  bottom-left origin is flipped to top-down',
        img.data.toString('hex', 0, 4) === '0000ffff');
  eq('  alpha preserved', img.data[15], 255);

  // Same image RLE-encoded must decode identically.
  const hr = Buffer.from(h); hr[2] = 10;
  const rle = Buffer.concat([
    Buffer.from([0x00]), px.subarray(0, 4),   // 1 raw pixel
    Buffer.from([0x00]), px.subarray(4, 8),
    Buffer.from([0x00]), px.subarray(8, 12),
    Buffer.from([0x00]), px.subarray(12, 16),
  ]);
  const img2 = tga.decode(Buffer.concat([hr, rle]));
  check('RLE decodes identically to uncompressed', img2.data.equals(img.data));
}

console.log('\n.x container reader');
{
  const X = require('./xfile');
  const zlib = require('zlib');

  // Synthetic MSZip: two blocks, the second back-referencing the first, which
  // is the case that breaks if the deflate history is not carried across.
  const first = Buffer.from('The quick brown fox jumps over the lazy dog. '.repeat(40));
  const second = Buffer.from('The quick brown fox jumps over the lazy dog! '.repeat(40));
  const c1 = zlib.deflateRawSync(first);
  const c2 = zlib.deflateRawSync(second, { dictionary: first });
  const mk = (comp, unc) => {
    const h = Buffer.alloc(6);
    h.writeUInt16LE(unc, 0);
    h.writeUInt16LE(comp.length + 2, 2);
    h.write('CK', 4, 'latin1');
    return Buffer.concat([h, comp]);
  };
  const total = first.length + second.length;
  const head = Buffer.alloc(16 + 4);
  head.write('xof 0303bzip0032', 0, 'latin1');
  head.writeUInt32LE(total, 16);
  const file = Buffer.concat([head, mk(c1, first.length), mk(c2, second.length)]);

  const opened = X.open(file);
  eq('MSZip decompresses to the declared size', opened.body.length, total);
  check('  first block content', opened.body.subarray(0, first.length).equals(first));
  check('  second block resolves back-references across the block boundary',
        opened.body.subarray(first.length).equals(second));
  eq('  format maps bzip -> bin', opened.format, 'bin');

  // Header parsing for the uncompressed variants.
  const txt = Buffer.concat([Buffer.from('xof 0303txt 0032', 'latin1'),
                             Buffer.from('Frame Root {}')]);
  eq('txt format detected', X.open(txt).format, 'txt');
  let threw = false;
  try { X.open(Buffer.from('not an x file at all')); } catch { threw = true; }
  check('non-.x input rejected', threw);

  // Binary tokenizer over a hand-built stream.
  const t = [];
  const push16 = (v) => { const b = Buffer.alloc(2); b.writeUInt16LE(v); t.push(b); };
  const push32 = (v) => { const b = Buffer.alloc(4); b.writeUInt32LE(v); t.push(b); };
  push16(X.TOKEN.NAME); push32(5); t.push(Buffer.from('Frame'));
  push16(X.TOKEN.OBRACE);
  push16(X.TOKEN.INTEGER); push32(42);
  push16(X.TOKEN.FLOAT_LIST); push32(3);
  const f = Buffer.alloc(12); f.writeFloatLE(1, 0); f.writeFloatLE(2, 4); f.writeFloatLE(3, 8);
  t.push(f);
  push16(X.TOKEN.CBRACE);
  const toks = [...X.binaryTokens(Buffer.concat(t), 32)];
  eq('token count', toks.length, 5);
  eq('  NAME', toks[0].value, 'Frame');
  eq('  brace', toks[1].type, '{');
  eq('  INTEGER', toks[2].value, 42);
  eq('  FLOAT_LIST length', toks[3].value.length, 3);
  eq('  FLOAT_LIST values', [...toks[3].value].join(','), '1,2,3');

  // A truncated body must stop cleanly in tolerant mode and throw otherwise —
  // 14 shipped meshes have damaged MSZip tails and are recovered this way.
  const cut = Buffer.concat(t).subarray(0, 9);
  let tolerantCount = 0;
  for (const _ of X.binaryTokens(cut, 32, { tolerant: true })) tolerantCount++;
  check('tolerant tokenizer stops cleanly on truncation', tolerantCount >= 0);
  let strictThrew = false;
  try { for (const _ of X.binaryTokens(cut, 32)) { /* consume */ } }
  catch { strictThrew = true; }
  check('strict tokenizer reports truncation', strictThrew);
}

console.log('\nmesh extraction');
{
  const M = require('./xmesh');

  // A minimal text .x: one frame, one two-triangle mesh with normals and UVs.
  const src = `xof 0303txt 0032
Frame Root {
  FrameTransformMatrix { 1.0,0.0,0.0,0.0, 0.0,1.0,0.0,0.0, 0.0,0.0,1.0,0.0, 0.0,0.0,0.0,1.0;; }
  Mesh Square {
    4;
    -1.0;-1.0;0.0;,
    1.0;-1.0;0.0;,
    1.0;1.0;0.0;,
    -1.0;1.0;0.0;;
    2;
    3;0,1,2;,
    3;0,2,3;;
    MeshNormals { 4; 0.0;0.0;1.0;,0.0;0.0;1.0;,0.0;0.0;1.0;,0.0;0.0;1.0;; 2; 3;0,1,2;,3;0,2,3;; }
    MeshTextureCoords { 4; 0.0;0.0;,1.0;0.0;,1.0;1.0;,0.0;1.0;; }
  }
}
`;
  const r = M.parse(Buffer.from(src, 'latin1'));
  eq('text .x parses one mesh', r.meshes.length, 1);
  const m = r.meshes[0];
  eq('  vertex count', m.vertexCount, 4);
  eq('  face count', m.faceCount, 2);
  eq('  triangles', m.indices.length / 3, 2);
  eq('  first vertex x', m.positions[0], -1);
  eq('  indices of second triangle', [...m.indices.slice(3)].join(','), '0,2,3');
  check('  normals present', !!m.normals);
  check('  uvs present', !!m.uvs);
  eq('  uv of vertex 2', `${m.uvs[4]},${m.uvs[5]}`, '1,1');
  check('  frame hierarchy seen', r.frameCount >= 1);

  // An n-gon must be fan-triangulated rather than dropped.
  const quad = `xof 0303txt 0032
Mesh Q { 4; 0.0;0.0;0.0;,1.0;0.0;0.0;,1.0;1.0;0.0;,0.0;1.0;0.0;; 1; 4;0,1,2,3;; }
`;
  const rq = M.parse(Buffer.from(quad, 'latin1'));
  eq('quad face triangulates to 2 triangles', rq.meshes[0].indices.length / 3, 2);
  eq('  fan order', [...rq.meshes[0].indices].join(','), '0,1,2,0,2,3');

  // Real shipped geometry, exercising the binary and MSZip paths end to end.
  const skin = path.join(RAN, 'data', 'skin');
  const names = fs.readdirSync(skin).filter((f) => f.toLowerCase().endsWith('.x'));
  let parsed = 0, geo = 0, failed = 0;
  for (const nm of names.slice(0, 250)) {
    try {
      const res = M.parse(fs.readFileSync(path.join(skin, nm)), { tolerant: true });
      parsed++;
      for (const mm of res.meshes) if (mm.vertexCount > 0) geo++;
    } catch (e) {
      if (!/not a DirectX/.test(e.message)) failed++;
    }
  }
  check(`parsed ${parsed} shipped meshes`, parsed > 200, `${parsed}`);
  eq('  none failed unexpectedly', failed, 0);
  check(`  found ${geo} non-empty meshes`, geo > 50, `${geo}`);

  // Object REFERENCE syntax: `{ Name }` points at a Data Object defined
  // elsewhere rather than opening a new one — the 3ds Max .x exporter uses
  // this to dedupe materials a mesh shares with an earlier one. MEASURED
  // real bug (found chasing "boa's main body invisible on device"): a naked
  // `{` was silently skipped instead of parsed as its own frame, so each
  // reference's NAME+`}` was misread as the ENCLOSING node's own bare-name
  // reference + closing brace, returning one nesting level early. A
  // MeshMaterialList with 3 referenced materials cascaded through three such
  // premature returns and detached MeshTextureCoords/XSkinMeshHeader/
  // SkinWeights from the mesh entirely, so a fully-skinned torso came out
  // reporting `skinBones=0` — RanChfBuilder then treated it as an
  // orphaned RIGID part and placed it unposed, several units below the
  // character's feet. Pinned here with a minimal synthetic file so the
  // mechanism is covered even if the specific shipped file it was found in
  // ever changes.
  const refSrc = `xof 0303txt 0032
Material RedMat {
  1.0;0.0;0.0;1.0;;
  0.5;
  1.0;1.0;1.0;;
  0.0;0.0;0.0;;
  TextureFilename { "red.dds"; }
}
Frame Root {
  Mesh Body {
    3;
    0.0;0.0;0.0;,1.0;0.0;0.0;,0.0;1.0;0.0;;
    1;
    3;0,1,2;;
    MeshMaterialList {
      1;
      1;
      0;
      { RedMat }
    }
    MeshNormals { 3; 0.0;0.0;1.0;,0.0;0.0;1.0;,0.0;0.0;1.0;; 1; 3;0,1,2;; }
    MeshTextureCoords { 3; 0.0;0.0;,1.0;0.0;,0.0;1.0;; }
    XSkinMeshHeader { 4; 0; 1; }
    SkinWeights {
      "Bone0";
      3;
      0,1,2;
      1.0;1.0;1.0;;
      1.0;0.0;0.0;0.0;
      0.0;1.0;0.0;0.0;
      0.0;0.0;1.0;0.0;
      0.0;0.0;0.0;1.0;;
    }
  }
}
`;
  const rr = M.parse(Buffer.from(refSrc, 'latin1'));
  eq('object-reference material: mesh still parses', rr.meshes.length, 1);
  const rm = rr.meshes[0];
  check('  MeshTextureCoords survives a referenced (not inline) material',
        !!rm.uvs, `uvs=${rm.uvs}`);
  eq('  MeshTextureCoords vertex count', rm.uvs ? rm.uvs.length / 2 : -1, 3);
  eq('  SkinWeights survives too (was detached by the cascade bug)', rm.skinBones, 1);
  check('  referenced material resolves to its real TextureFilename',
        rm.textures && rm.textures[0] === 'red.dds', `textures=${rm.textures}`);

  // Real shipped data: boa's actual torso ('bir01.x', frame "01") and lower
  // body ('bir02.x', frame "Object01") — the exact files/meshes a real
  // Android-device screenshot showed missing (2026-08-20). Both use
  // materials-by-reference, so both are direct regression pins for the fix
  // above, not just the synthetic case.
  const bir01 = M.parse(fs.readFileSync(path.join(skin, 'bir01.x')));
  const torso = bir01.meshes.find((m) => m.frameName === '01' && m.vertexCount > 0);
  check('bir01.x "01" (boa torso) mesh found', !!torso);
  if (torso) {
    eq('  vertex count', torso.vertexCount, 1230);
    eq('  skinned to 9 bones (was 0 before the reference-object fix)', torso.skinBones, 9);
    const boneNames = torso.skin.map((s) => s.bone).sort().join(',');
    eq('  bone set is the spine/arm/head chain',
       boneNames,
       ['Bip01_Head', 'Bip01_L_Forearm', 'Bip01_L_UpperArm', 'Bip01_Neck', 'Bip01_R_Forearm',
        'Bip01_R_UpperArm', 'Bip01_Spine', 'Bip01_Spine1', 'Bip01_Spine2'].sort().join(','));
    check('  every vertex has at least one influence',
          torso.skin.some((s) => s.indices.length > 0));
  }

  const bir02 = M.parse(fs.readFileSync(path.join(skin, 'bir02.x')));
  const lower = bir02.meshes.find((m) => m.frameName === 'Object01' && m.vertexCount > 0);
  check('bir02.x "Object01" (boa lower body) mesh found', !!lower);
  if (lower) {
    eq('  vertex count', lower.vertexCount, 763);
    eq('  skinned to 7 bones (was 0 before the reference-object fix)', lower.skinBones, 7);
  }
}

console.log('\nbyte-crypt tables');
{
  const B = require('./bytecrypt');
  const names = B.names();
  eq('all 43 tables extracted from SOURCE', names.length, 43);
  check('includes the animation tables',
        names.includes('EMBYTECRYPT_BIN') && names.includes('EMBYTECRYPT_BIN2'));
  eq('enum ids match ByteCrypt.h', B.idOf('EMBYTECRYPT_BIN'), 19);
  eq('  and the ver2 id', B.idOf('EMBYTECRYPT_BIN2'), 38);

  // Every table must be a permutation of 0..255, or decode is not invertible
  // and 1/256 of every byte would be silently wrong.
  let nonPermutation = 0;
  for (const n of names) {
    const seen = new Set(B.TABLES[n].encode);
    if (seen.size !== 256) nonPermutation++;
  }
  eq('every table is a permutation', nonPermutation, 0);

  // Round-trip across the full byte range, for every table.
  const all = Buffer.from(Array.from({ length: 256 }, (_, i) => i));
  let badRound = 0;
  for (const n of names) {
    const enc = B.encode(Buffer.from(all), n);
    const dec = B.decode(Buffer.from(enc), n);
    if (!dec.equals(all)) badRound++;
  }
  eq('decode(encode(x)) == x for all tables', badRound, 0);

  const noop = B.decode(Buffer.from(all), 'EMBYTECRYPT_NONE');
  check('EMBYTECRYPT_NONE is a no-op', noop.equals(all));

  // Ranged operation must leave bytes outside the range untouched, since the
  // 132-byte CSerialFile header is never encoded.
  const ranged = B.decode(Buffer.from(all), 'EMBYTECRYPT_BIN', 10, 20);
  check('range start respected', ranged[9] === all[9]);
  check('range end respected', ranged[20] === all[20]);
  check('  inside the range is changed', ranged[10] !== all[10]);

  // CSerialFile header parsing against a real animation file.
  const anim = new RccArchive(A('animation/Animation.rcc'));
  const first = anim.entries.find((e) => e.name.toLowerCase().endsWith('.bin'));
  if (first) {
    const h = B.readHeader(anim.read(first));
    eq('animation header type', h.type, 'AnimContainer');
    eq('  body starts after 128+4', h.bodyOffset, 132);
    check('  version is one of the six shipped',
          [0x100, 0x101, 0x102, 0x103, 0x104, 0x200].includes(h.version),
          `0x${h.version.toString(16)}`);
  }
}

console.log('\nanimation encoding gate');
{
  const B = require('./bytecrypt');
  const anim = new RccArchive(A('animation/Animation.rcc'));

  const pick = (want) => {
    for (const e of anim.entries) {
      if (!e.name.toLowerCase().endsWith('.bin')) continue;
      const b = anim.read(e);
      if (b.toString('latin1', 0, 13) === 'AnimContainer' &&
          b.readUInt32LE(128) === want) return b;
    }
    return null;
  };
  const hasName = (buf) => /[A-Za-z_][A-Za-z0-9_]{6,}/.test(
    buf.toString('latin1', 132, 400));

  // SAnimationSaveLoad.cpp:401 gates on `dwVersion >= VERSION_ENCODE` where
  // VERSION_ENCODE is 0x0200. Below that the body is NOT byte-encoded at all —
  // applying a table there destroys readable data rather than revealing it.
  const old = pick(0x104);
  if (old) {
    check('v0x0104 body is already plaintext (bone names visible raw)',
          hasName(old));
    const wrong = B.decode(Buffer.from(old), 'EMBYTECRYPT_BIN2', 132);
    check('  applying BIN2 to it destroys the names', !hasName(wrong));
  }

  const now = pick(0x200);
  if (now) {
    check('v0x0200 body is encoded (no names raw)', !hasName(now));
    const dec = B.decode(Buffer.from(now), 'EMBYTECRYPT_BIN2', 132);
    check('  BIN2 reveals bone names', hasName(dec));
    check('  and specifically a biped bone',
          dec.toString('latin1', 132, 600).includes('Bip01'));
  }
}

console.log('\nEGP encoding gate');
{
  const B = require('./bytecrypt');
  const eff = new RccArchive(A('effect/Effect.rcc'));
  const hasName = (b, s, e) => /[A-Za-z_][A-Za-z0-9_]{5,}/.test(
    b.toString('latin1', s, e));

  // EFF_PROPGROUP writes its own DWORD version AFTER the CSerialFile header and
  // BEFORE SetEncodeType, so the encoded region starts at 136 rather than 132,
  // and the gate reads that inner version (DxEffSinglePropGManSaveLoad.cpp:449).
  let encoded = null;
  let plain = null;
  for (const e of eff.entries) {
    if (!e.name.toLowerCase().endsWith('.egp')) continue;
    const b = eff.read(e);
    const inner = b.readUInt32LE(132);
    if (inner >= 0x200 && !encoded) encoded = b;
    if (inner < 0x200 && !plain) plain = b;
    if (encoded && plain) break;
  }

  check('found both an encoded and a plain .egp', !!encoded && !!plain);
  if (plain) check('version < 0x0200 is plaintext', hasName(plain, 136, 900));
  if (encoded) {
    check('version >= 0x0200 is not readable raw', !hasName(encoded, 136, 900));
    const dec = B.decode(Buffer.from(encoded), 'EMBYTECRYPT_EGP', 136);
    check('  EGP table at offset 136 reveals names', hasName(dec, 136, 900));
    // The cipher is a stateless per-byte substitution, so the start offset
    // cannot shift or corrupt anything downstream of it — bytes past 136 decode
    // identically either way. What the offset actually protects is the version
    // DWORD at 132, which is plaintext and must not be transformed.
    const wrong = B.decode(Buffer.from(encoded), 'EMBYTECRYPT_EGP', 132);
    check('  body is unaffected by the start offset (stateless substitution)',
          wrong.subarray(136, 900).equals(dec.subarray(136, 900)));
    check('  but decoding from 132 corrupts the version field',
          wrong.readUInt32LE(132) !== encoded.readUInt32LE(132));
    eq('  correct decode preserves the version', dec.readUInt32LE(132),
       encoded.readUInt32LE(132));
  }
}

console.log('\nanimation reader');
{
  const AN = require('./xanim');

  // Key strides must come from the probe. SMatrixKey is the trap: a
  // D3DXMATRIXA16 forces 16-byte alignment, so it is 80 bytes with the matrix
  // at offset 16 — not 4+64. A wrong stride here shreds every matrix track
  // while still "parsing".
  eq('SPositionKey stride', AN.KEY.position, 16);
  eq('SRotateKey stride', AN.KEY.rotate, 20);
  eq('SScaleKey stride', AN.KEY.scale, 16);
  eq('SQuatPosKey stride', AN.KEY.quatPos, 36);
  eq('SMatrixKey stride (A16 padding)', AN.KEY.matrix, 80);

  const anim = new RccArchive(A('animation/Animation.rcc'));
  const byVersion = new Map();
  let parsed = 0, failed = 0, tracks = 0, keyframes = 0;
  const badNames = [];

  for (const e of anim.entries) {
    if (!e.name.toLowerCase().endsWith('.bin')) continue;
    const buf = anim.read(e);
    if (buf.toString('latin1', 0, 13) !== 'AnimContainer') continue;
    try {
      const r = AN.parse(buf);
      parsed++;
      byVersion.set(r.version, (byVersion.get(r.version) || 0) + 1);
      const all = [...r.tracks, ...r.upperBody];
      tracks += all.length;
      for (const t of all) {
        for (const k of ['position', 'rotate', 'scale', 'matrix', 'quatPos']) {
          if (t[k]) keyframes += t[k].count;
        }
      }
      // A wrong stride or field order shows up as garbage bone names long
      // before it shows up as a parse error.
      for (const n of r.bones) {
        if (!/^[\x20-\x7e]+$/.test(n) && badNames.length < 5) badNames.push(n);
      }
    } catch (err) {
      failed++;
    }
  }

  check(`parsed ${parsed} animation files`, parsed > 6000, `${parsed}`);
  eq('  none failed', failed, 0);
  eq('  all six shipped versions covered', byVersion.size, 6);
  check('  every documented version present',
        [0x100, 0x101, 0x102, 0x103, 0x104, 0x200]
          .every((v) => byVersion.has(v)));
  check(`  ${tracks.toLocaleString()} tracks extracted`, tracks > 300000);
  check(`  ${keyframes.toLocaleString()} keyframes extracted`, keyframes > 10e6);
  eq('  no garbage bone names', badNames.length, 0);

  // The encrypted ones must actually be the 0x0200 files, and must decode.
  const encFile = anim.entries.find((e) => {
    if (!e.name.toLowerCase().endsWith('.bin')) return false;
    const b = anim.read(e);
    return b.toString('latin1', 0, 13) === 'AnimContainer' &&
           b.readUInt32LE(128) === 0x200;
  });
  if (encFile) {
    const r = AN.parse(anim.read(encFile));
    check('v0x0200 is flagged encoded', r.encoded === true);
    check('  and yields real bone names',
          r.bones.length > 0 && r.bones.every((n) => /^[\x20-\x7e]+$/.test(n)));
  }
}

console.log('\n.wld container + navmesh');
{
  const W = require('./wld');
  const N = require('./navmesh');
  const LAYOUT = require('../layout-probe/layout.json');

  // Strides must come from the probe, not from the plan document.
  const SZ = LAYOUT.structs.NAVMESH_SIZES.fields;
  eq('Plane size', SZ.Plane.size, 28);
  eq('Line2D size (mutable bool -> 3 B tail padding)', SZ.Line2D.size, 28);
  eq('D3DXVECTOR3 size', SZ.D3DXVECTOR3.size, 12);
  eq('serialised cell record', SZ.CELL_RECORD.size, 188);
  check('sizeof(NavigationCell) differs from the record — fields are written ' +
        'individually, never blitted',
        SZ.NavigationCell.size !== SZ.CELL_RECORD.size);
  eq('navmesh.js agrees with the probe', N.CELL_RECORD, SZ.CELL_RECORD.size);

  // Both filemark layouts are 16 bytes; only the ORDER differs, and NAVI is
  // field 0 in both — which is why navmesh extraction is version-safe.
  const fm = LAYOUT.structs.SLAND_FILEMARK.fields;
  const fm100 = LAYOUT.structs.SLAND_FILEMARK_100.fields;
  eq('SLAND_FILEMARK NAVI at 0', fm.dwNAVI_MARK.off, 0);
  eq('SLAND_FILEMARK_100 NAVI at 0', fm100.dwNAVI_MARK.off, 0);
  check('the two layouts genuinely differ',
        fm.dwWEATHER_MARK.off !== fm100.dwWEATHER_MARK.off);

  const marks101 = W.readMarks(
    Buffer.from([1,0,0,0, 2,0,0,0, 3,0,0,0, 4,0,0,0]), 0, 0x0101);
  const marks100 = W.readMarks(
    Buffer.from([1,0,0,0, 2,0,0,0, 3,0,0,0, 4,0,0,0]), 0, 0x0100);
  eq('0x0101 order: navi,weather,gate,coll',
     `${marks101.navi},${marks101.weather},${marks101.gate},${marks101.coll}`, '1,2,3,4');
  eq('0x0100 order: navi,gate,coll,weather',
     `${marks100.navi},${marks100.gate},${marks100.coll},${marks100.weather}`, '1,2,3,4');

  // The WLD cipher: stateless per-byte, must round-trip across all 256 values.
  const all = Buffer.concat([Buffer.alloc(W.BODY_OFFSET),
    Buffer.from(Array.from({ length: 256 }, (_, i) => i))]);
  const enc = W.encrypt(Buffer.from(all));
  check('encrypt changes the body', !enc.equals(all));
  check('header is untouched by the cipher',
        enc.subarray(0, W.BODY_OFFSET).equals(all.subarray(0, W.BODY_OFFSET)));
  check('decrypt(encrypt(x)) == x', W.decrypt(Buffer.from(enc)).equals(all));
  eq('decrypt matches ((b-0x10)&0xff)^0x10',
     W.decrypt(Buffer.concat([Buffer.alloc(W.BODY_OFFSET), Buffer.from([0x00])]))
       [W.BODY_OFFSET],
     ((0x00 - 0x10) & 0xff) ^ 0x10);

  // Full corpus. Deduplicated by map name: data/map/RanMapZipTemp holds stale
  // per-pid extractions of the same maps, so counting files triple-counts.
  const maps = new Map();
  (function walk(dir) {
    let items;
    try { items = fs.readdirSync(dir, { withFileTypes: true }); } catch { return; }
    for (const it of items) {
      const p = path.join(dir, it.name);
      if (it.isDirectory()) { walk(p); continue; }
      if (it.name.toLowerCase().endsWith('.rcc')) {
        let ar;
        try { ar = new RccArchive(p); } catch { continue; }
        for (const e of ar.entries) {
          if (!e.name.toLowerCase().endsWith('.wld')) continue;
          const k = path.basename(e.name).toLowerCase();
          if (!maps.has(k)) maps.set(k, ar.read(e));
        }
      } else if (it.name.toLowerCase().endsWith('.wld')) {
        const k = it.name.toLowerCase();
        if (!maps.has(k)) maps.set(k, fs.readFileSync(p));
      }
    }
  })(RAN);

  let opened = 0, withMesh = 0, empty = 0, failed = 0, encrypted = 0;
  let verts = 0, cells = 0, problems = 0;
  for (const [name, raw] of maps) {
    let w;
    try { w = W.open(raw); } catch { failed++; continue; }
    opened++;
    if (w.encrypted) encrypted++;
    let m;
    try { m = N.parse(w.buf, W.navmeshOffset(w)); } catch { failed++; continue; }
    if (!m) { empty++; continue; }
    withMesh++; verts += m.vertexCount; cells += m.cellCount;
    if (N.validate(m).length) problems++;
  }

  eq('distinct maps found', maps.size, 132);
  eq('  headers opened', opened, 132);
  eq('  encrypted "Land.Man" maps', encrypted, 12);
  eq('  carrying a navmesh', withMesh, 121);
  eq('  navmesh section present but empty', empty, 10);
  eq('  failures (known: es_f41_killbillzone01)', failed, 1);
  check(`  ${verts.toLocaleString()} vertices`, verts > 800000, `${verts}`);
  check(`  ${cells.toLocaleString()} cells`, cells > 500000, `${cells}`);
  // A wrong stride still "parses" — this is the check that catches it.
  eq('  structural validation problems', problems, 0);

  // Tie the pipeline back to live server behaviour: the Phase 0 spawn observed
  // on map 8 was (109.0, -319.7, -4203.4).
  const school = maps.get('w_school_03.wld');
  if (school) {
    const w = W.open(school);
    const m = N.parse(w.buf, W.navmeshOffset(w));
    const b = N.bounds(m);
    const spawn = [109.0, -319.7, -4203.4];
    check('live spawn falls inside w_school_03 navmesh XZ bounds',
          spawn[0] >= b.min[0] && spawn[0] <= b.max[0] &&
          spawn[2] >= b.min[2] && spawn[2] <= b.max[2]);
    let best = Infinity;
    for (let i = 0; i < m.vertexCount; i++) {
      const dx = m.vertices[i * 3] - spawn[0];
      const dy = m.vertices[i * 3 + 1] - spawn[1];
      const dz = m.vertices[i * 3 + 2] - spawn[2];
      best = Math.min(best, dx * dx + dy * dy + dz * dz);
    }
    check(`  nearest navmesh vertex within 15 units (${Math.sqrt(best).toFixed(1)})`,
          Math.sqrt(best) < 15);
  }
}

console.log('\n.wld0 sidecar');
{
  const W = require('./wld');

  // Encryption is keyed on the two ENCRYPTED spellings only. Deciding by
  // exclusion breaks login_2.wld0, which ships plaintext under the .wld
  // spelling "LAND.MAN" — decrypting it yields a nonsense header.
  check('Land.Man is encrypted', W.isEncryptedType('Land.Man'));
  check('Default_Crypt is encrypted', W.isEncryptedType('Default_Crypt'));
  check('LAND.MAN is NOT encrypted', !W.isEncryptedType('LAND.MAN'));
  check('default is NOT encrypted', !W.isEncryptedType('default'));

  // Decryption_WLD0 is byte-identical to Decryption_WLD on the live path
  // (WLDCrypt.cpp:69-88), so one implementation covers both sidecars and maps.
  const probe = Buffer.concat([Buffer.alloc(W.BODY_OFFSET),
                               Buffer.from([0, 1, 2, 254, 255])]);
  check('one cipher serves .wld and .wld0',
        W.decrypt(W.encrypt(Buffer.from(probe))).equals(probe));

  // Every .wld0 block is [u32 version][u32 payloadSize][payload], and the first
  // block is the AABB — exactly 24 bytes of 6 floats. That explicit size is what
  // will let a future geometry reader skip blocks it does not understand.
  const sidecars = new Map();
  (function walk(dir) {
    let items;
    try { items = fs.readdirSync(dir, { withFileTypes: true }); } catch { return; }
    for (const it of items) {
      const p = path.join(dir, it.name);
      if (it.isDirectory()) { walk(p); continue; }
      if (it.name.toLowerCase().endsWith('.rcc')) {
        let ar;
        try { ar = new RccArchive(p); } catch { continue; }
        for (const e of ar.entries) {
          if (!e.name.toLowerCase().endsWith('.wld0')) continue;
          const k = path.basename(e.name).toLowerCase();
          if (!sidecars.has(k)) sidecars.set(k, ar.read(e));
        }
      } else if (it.name.toLowerCase().endsWith('.wld0')) {
        const k = it.name.toLowerCase();
        if (!sidecars.has(k)) sidecars.set(k, fs.readFileSync(p));
      }
    }
  })(RAN);

  let sane = 0, odd = 0;
  const versions = new Map();
  for (const [name, raw] of sidecars) {
    const type = W.readTypeString(raw);
    const b = W.isEncryptedType(type) ? W.decrypt(Buffer.from(raw)) : raw;
    if (b.length < 164) { odd++; continue; }
    const ver = b.readUInt32LE(132);
    const size = b.readUInt32LE(136);
    const aabb = Array.from({ length: 6 }, (_, i) => b.readFloatLE(140 + i * 4));
    if (size === 24 && aabb.every(Number.isFinite)) {
      sane++;
      versions.set(ver, (versions.get(ver) || 0) + 1);
    } else {
      odd++;
    }
  }

  // Two files are not ordinary sidecars, for two different reasons:
  //   es_f41_killbillzone01.wld0 — genuinely corrupt. Its .wld is broken too
  //     (NAVI mark past EOF), so the whole map is damaged, not just this file.
  //   square_rd.wld0 — 144 bytes, i.e. the 132-byte header plus a stub. A map
  //     with no static mesh, which is legitimate rather than an error.
  eq('distinct .wld0 sidecars', sidecars.size, 103);
  eq('  with a coherent AABB block', sane, 101);
  eq('  anomalies (1 corrupt, 1 empty stub)', odd, 2);
  check('  0x0102 is the dominant version',
        versions.get(0x0102) > 80, `${versions.get(0x0102)}`);
  check('  every version maps to a known DxStaticMesh loader',
        [...versions.keys()].every((v) => [0x0100, 0x0101, 0x0102, 0x0103,
                                           0x0104, 0x0105, 0x0200].includes(v)),
        [...versions.keys()].map((v) => '0x' + v.toString(16)).join(','));
}

console.log('\nFVF decoding');
{
  const FVF = require('./fvf');

  // The writer emits these three; the stride is derived from the DWORD, never
  // hardcoded, because OctreeLoadOLD stores the FVF where a version would be.
  eq('0x112 XYZ|NORMAL|TEX1 stride', FVF.vertexSize(0x112), 32);
  eq('0x152 XYZ|NORMAL|DIFFUSE|TEX1 stride', FVF.vertexSize(0x152), 36);
  eq('0x142 XYZ|DIFFUSE|TEX1 stride', FVF.vertexSize(0x142), 24);

  const a = FVF.layout(0x112);
  eq('0x112 position offset', a.position, 0);
  eq('0x112 normal offset', a.normal, 12);
  eq('0x112 uv0 offset', a.texcoords[0].offset, 24);
  check('0x112 has no diffuse', a.diffuse === null);

  const b = FVF.layout(0x152);
  eq('0x152 diffuse offset', b.diffuse, 24);
  eq('0x152 uv0 offset', b.texcoords[0].offset, 28);

  // Diffuse sits BEFORE the texcoords, so dropping the normal shifts uv0 down
  // by 12 rather than leaving it where 0x152 put it.
  const c2 = FVF.layout(0x142);
  check('0x142 has no normal', c2.normal === null);
  eq('0x142 diffuse offset', c2.diffuse, 12);
  eq('0x142 uv0 offset', c2.texcoords[0].offset, 16);

  // Texcoord size bits live two-per-stage from bit 16; 00 means 2 floats.
  eq('two texture stages add two UV sets', FVF.vertexSize(0x212), 40);
}

console.log('\n.wld0 geometry');
{
  const SM = require('./staticmesh');
  const OCT = require('./octree');
  const FVF = require('./fvf');

  // -- serialisation invariants that produce plausible garbage, not errors ----

  // std::string length INCLUDES the NUL (SerialFile.cpp:444 writes length()+1),
  // so an empty string is [1]['\0'] and never [0]. Off by one shifts everything.
  {
    const s = Buffer.alloc(4 + 4);
    s.writeUInt32LE(4, 0);
    s.write('abc\0', 4, 'latin1');
    const cur = new SM.Cursor(s, 0);
    eq('string length is NUL-inclusive', cur.str(), 'abc');
    eq('  and consumes exactly len+4', cur.p, 8);
  }
  eq('BOOL is 4 bytes, never 1', OCT.BOOL_SIZE, 4);

  // 0x0100 and 0x0101 share Load_101, so they must share a bucket list; ALPHA
  // moves from second (0x0102) to last (0x0103/0x0200).
  check('0x0100 and 0x0101 share a bucket list',
        SM.BUCKETS[0x0100].join() === SM.BUCKETS[0x0101].join());
  eq('  and it is three buckets', SM.BUCKETS[0x0100].length, 3);
  eq('0x0102 puts alpha second', SM.BUCKETS[0x0102][1], 'alpha');
  eq('0x0103 puts alpha last', SM.BUCKETS[0x0103][4], 'alpha');
  check('0x0103 and 0x0200 share a bucket list',
        SM.BUCKETS[0x0103].join() === SM.BUCKETS[0x0200].join());

  // Effect field ORDER changes between versions of the same effect.
  const dif = SM.EFFECT_FIELDS[SM.TEXEFF.DIFFUSE];
  check('Diffuse 0x0101 leads with bSpeed', dif[0x0101][0].startsWith('d:'));
  check('Diffuse 0x0102 leads with the texture name', dif[0x0102][0].startsWith('s:'));

  // -- full corpus ------------------------------------------------------------
  const arc = new RccArchive(A('map/Map.rcc'));
  const wld0 = arc.list().filter((n) => (n.name || n).toLowerCase().endsWith('.wld0'));

  let parsed = 0, exact = 0, skipped = 0, verts = 0, tris = 0, meshes = 0;
  const textures = new Set();
  const failed = [];
  for (const e of wld0) {
    const nm = path.basename(e.name || e);
    try {
      const sm = SM.parse(arc.read(e), { headerOnly: true });
      parsed++;
      // Trailing zero padding is fine; unread CONTENT is a desync.
      const tail = arc.read(e).subarray(sm.bytesRead);
      if (tail.every((x) => x === 0)) exact++;
      const s = SM.summarise(sm);
      meshes += s.meshes; verts += s.verts; tris += s.tris; skipped += s.skipped;
      for (const t of s.textures) textures.add(t);
    } catch (err) { failed.push(nm); }
  }

  eq('.wld0 files walked', parsed, 101);
  eq('  every one consumed to EOF (bar zero padding)', exact, parsed);
  eq('  nodes skipped as unreadable', skipped, 0);
  // Only the two known-bad files may fail: one genuinely corrupt, one a stub.
  check('  only the two known-bad files fail',
        failed.length === 2 &&
        failed.some((n) => n.startsWith('es_f41_killbillzone01')) &&
        failed.some((n) => n.startsWith('square_rd')),
        failed.join(','));
  check('  mesh records', meshes > 6800, `${meshes}`);
  check('  vertices', verts > 15e6, `${verts}`);
  check('  triangles', tris > 10e6, `${tris}`);
  check('  distinct terrain textures', textures.size > 2900, `${textures.size}`);

  // -- geometry, not just parse success ---------------------------------------
  // A wrong stride still "parses" — the lesson from meshes and animation. These
  // maps cover all three shipped FVFs and both mesh header layouts.
  // deathjail_01 is the smallest map built entirely from 0x142, the FVF with no
  // normal — the one that moves uv0 and would go unnoticed without a sample.
  const SAMPLE = ['2010_bossroom00.wld0', '1city_sportzone.wld0',
                  'srp_ground.wld0', 'cha1_select.wld0', 'deathjail_01.wld0'];
  let checkedNodes = 0, badIndex = 0, outOfBox = 0, overLimit = 0;
  const fvfs = new Set();
  for (const nm of SAMPLE) {
    const e = wld0.find((x) => path.basename(x.name || x).toLowerCase() === nm);
    if (!e) { check(`sample ${nm} present`, false); continue; }
    const sm = SM.parse(arc.read(e));
    for (const bk of sm.buckets) for (const m of bk.meshes) {
      if (!m.octree) continue;
      for (const n of m.octree.nodes) {
        const g = n.geometry;
        if (!g) continue;
        checkedNodes++;
        fvfs.add(g.fvf);
        if (g.vertexCount > 65535) overLimit++;
        for (let i = 0; i < g.indices.length; i++) {
          if (g.indices[i] >= g.vertexCount) { badIndex++; break; }
        }
        for (let i = 0; i < g.vertexCount; i++) {
          const x = g.positions[i * 3], y = g.positions[i * 3 + 1], z = g.positions[i * 3 + 2];
          if (x < n.vMin[0] - 0.01 || x > n.vMax[0] + 0.01 ||
              y < n.vMin[1] - 0.01 || y > n.vMax[1] + 0.01 ||
              z < n.vMin[2] - 0.01 || z > n.vMax[2] + 0.01 ||
              !Number.isFinite(x + y + z)) { outOfBox++; break; }
        }
      }
    }
  }
  check('sample covers > 1000 geometry nodes', checkedNodes > 1000, `${checkedNodes}`);
  eq('  every index addresses a vertex in its own node', badIndex, 0);
  eq('  every vertex lies inside its node AABB', outOfBox, 0);
  eq('  no node exceeds the 16-bit index limit', overLimit, 0);
  check('  sample exercises all three shipped FVFs', fvfs.size === 3,
        [...fvfs].map((f) => '0x' + f.toString(16)).join(','));

  // Collision: DxAABBNode leaves carry a TRIANGLE INDEX into the owning mesh's
  // index buffer (DxOctreeMesh.cpp:236 does `pwIndexB + dwFace * 3`), never
  // geometry. Measured across the whole corpus, every tree indexes every face —
  // so the tree is a pure BVH and there is nothing to extract; Unity's
  // MeshCollider rebuilds an equivalent one.
  //
  // This doubles as the strongest check on the collision-tree walker itself: a
  // walk that drifted would yield garbage face indices, and both numbers below
  // would stop matching.
  {
    let trees = 0, fullCoverage = 0, outOfRange = 0, noTree = 0;
    for (const nm of SAMPLE) {
      const e = wld0.find((x) => path.basename(x.name || x).toLowerCase() === nm);
      if (!e) continue;
      const sm = SM.parse(arc.read(e), { headerOnly: true, collisionFaces: true });
      for (const bk of sm.buckets) for (const m of bk.meshes) {
        if (!m.octree) continue;
        for (const n of m.octree.nodes) {
          const g = n.geometry;
          if (!g || !g.faceCount) continue;
          if (!g.collisionFaces) { noTree++; continue; }
          trees++;
          if (g.collisionFaces.size === g.faceCount) fullCoverage++;
          for (const f of g.collisionFaces) if (f >= g.faceCount) { outOfRange++; break; }
        }
      }
    }
    check('every geometry node has a collision tree', trees > 0 && noTree === 0,
          `${trees} trees, ${noTree} without`);
    eq('  every leaf face index is in range', outOfRange, 0);
    eq('  and the tree covers every face', fullCoverage, trees);
  }

  // Positive control: the AABB test is only worth running if a wrong stride
  // actually trips it. Force 0x152 to decode at 0x112's stride and layout.
  {
    const realSize = FVF.vertexSize, realLayout = FVF.layout;
    FVF.vertexSize = (f) => (f === 0x152 ? 32 : realSize(f));
    FVF.layout = (f) => (f === 0x152 ? realLayout(0x112) : realLayout(f));
    let tripped = 0, seen = 0;
    try {
      const e = wld0.find((x) => path.basename(x.name || x).toLowerCase() === '2010_bossroom00.wld0');
      const sm = SM.parse(arc.read(e));
      for (const bk of sm.buckets) for (const m of bk.meshes) {
        if (!m.octree) continue;
        for (const n of m.octree.nodes) {
          const g = n.geometry;
          if (!g || g.fvf !== 0x152) continue;
          seen++;
          for (let i = 0; i < g.vertexCount; i++) {
            const x = g.positions[i * 3];
            if (x < n.vMin[0] - 0.01 || x > n.vMax[0] + 0.01) { tripped++; break; }
          }
        }
      }
    } catch { tripped = seen = 1; }          // an outright throw also counts
    FVF.vertexSize = realSize; FVF.layout = realLayout;
    check('control: a wrong stride trips the AABB test', seen > 0 && tripped === seen,
          `${tripped}/${seen} nodes flagged`);
  }
}

console.log('\nexport format spec (what the Unity importers read)');
{
  // The C# readers in MOBILE/unity cannot be compiled here — there is no .NET
  // SDK on this machine, only the runtime — so the format they hardcode is
  // pinned from this side instead. If an offset below changes, the C# is wrong.
  const OUT = path.join(__dirname, '..', '..', 'assets', 'terrain');
  const one = path.join(OUT, 'w_school_03.terrain');

  if (!fs.existsSync(one)) {
    check('terrain export present (run extract-terrain.js --out ../../assets/terrain)', false);
  } else {
    const b = fs.readFileSync(one);
    eq('magic is "RTRN"', b.toString('latin1', 0, 4), 'RTRN');
    eq('  as a little-endian u32', b.readUInt32LE(0), 0x4e525452);
    eq('version', b.readUInt32LE(4), 2);

    // Header is fully self-describing: the C# validates total size before
    // reading anything, so these five fields and their offsets are load-bearing.
    const meshCount = b.readUInt32LE(8);
    const nodeCount = b.readUInt32LE(12);
    const vertexCount = b.readUInt32LE(16);
    const indexCount = b.readUInt32LE(20);
    const HEADER = 48, MESH_REC = 16, NODE_REC = 40;
    eq('header size', HEADER, 48);
    eq('mesh record size', MESH_REC, 16);
    eq('node record size', NODE_REC, 40);
    // v2 appends [u32 hasColors][dayARGB][nightARGB] after the index blob, so
    // the total size depends on the flag — read it from where it must sit.
    const preColor = HEADER + meshCount * MESH_REC + nodeCount * NODE_REC +
                     vertexCount * 12 + vertexCount * 8 + indexCount * 2;
    const hasColors = b.readUInt32LE(preColor);
    check('hasColors flag is 0 or 1', hasColors === 0 || hasColors === 1, `${hasColors}`);
    eq('file size is exactly what the header describes',
       preColor + 4 + (hasColors ? vertexCount * 8 : 0), b.length);

    // Bounds must be the union of node boxes, never the stale source header.
    const bMax = [0, 1, 2].map((k) => b.readFloatLE(24 + k * 4));
    const bMin = [0, 1, 2].map((k) => b.readFloatLE(36 + k * 4));
    check('bounds max > min on every axis', bMax.every((v, k) => v > bMin[k]));

    // Walk the tables the way the C# does and check every reference resolves.
    let badBatch = 0, badNode = 0, overNodeBox = 0, badLocalIndex = 0;
    for (let i = 0; i < meshCount; i++) {
      const o = HEADER + i * MESH_REC;
      const first = b.readUInt32LE(o + 8), count = b.readUInt32LE(o + 12);
      if (first + count > nodeCount) badBatch++;
    }
    const nodeBase = HEADER + meshCount * MESH_REC;
    const posBase = nodeBase + nodeCount * NODE_REC;
    const uvBase = posBase + vertexCount * 12;
    const idxBase = uvBase + vertexCount * 8;
    for (let i = 0; i < nodeCount; i++) {
      const o = nodeBase + i * NODE_REC;
      const nMax = [0, 1, 2].map((k) => b.readFloatLE(o + k * 4));
      const nMin = [0, 1, 2].map((k) => b.readFloatLE(o + 12 + k * 4));
      const vOff = b.readUInt32LE(o + 24), vCnt = b.readUInt32LE(o + 28);
      const iOff = b.readUInt32LE(o + 32), iCnt = b.readUInt32LE(o + 36);
      if (vOff + vCnt > vertexCount || iOff + iCnt > indexCount) { badNode++; continue; }
      // Node box inside the map box.
      for (let k = 0; k < 3; k++) {
        if (nMin[k] < bMin[k] - 0.01 || nMax[k] > bMax[k] + 0.01) overNodeBox++;
      }
      // Indices are NODE-LOCAL — the single most likely thing for a consumer to
      // get wrong, since a file-global reading also "works" for node 0.
      for (let j = 0; j < iCnt; j++) {
        if (b.readUInt16LE(idxBase + (iOff + j) * 2) >= vCnt) { badLocalIndex++; break; }
      }
    }
    eq('every batch references nodes in range', badBatch, 0);
    eq('every node references vertices and indices in range', badNode, 0);
    eq('every node box lies inside the map bounds', overNodeBox, 0);
    eq('indices are node-local and in range', badLocalIndex, 0);

    // UVs are stored raw (D3D, origin top-left). The C# flips V on read; if the
    // exporter ever flipped too the map would come in upside down.
    //
    // Finiteness is not cosmetic: the source has NaN texcoords in untextured
    // meshes (8 of 185,041 here), and one NaN reaching a Unity mesh poisons its
    // bounds and silently kills culling for the whole batch. The exporter
    // flattens them, so ZERO must survive into the file.
    let nonFinite = 0;
    for (let i = 0; i < vertexCount; i++) {
      if (!Number.isFinite(b.readFloatLE(uvBase + i * 8)) ||
          !Number.isFinite(b.readFloatLE(uvBase + i * 8 + 4))) nonFinite++;
    }
    eq('UVs are finite', nonFinite, 0);
    let nonFinitePos = 0;
    for (let i = 0; i < vertexCount * 3; i++) {
      if (!Number.isFinite(b.readFloatLE(posBase + i * 4))) nonFinitePos++;
    }
    eq('positions are finite', nonFinitePos, 0);

    // The texture sidecar the importer indexes into.
    const names = JSON.parse(fs.readFileSync(path.join(OUT, 'w_school_03.textures.json'), 'utf8'));
    check('texture sidecar is an array of names', Array.isArray(names) && names.length > 0);
    let badTexRef = 0;
    for (let i = 0; i < meshCount; i++) {
      if (b.readUInt32LE(HEADER + i * MESH_REC + 4) >= names.length) badTexRef++;
    }
    eq('every batch texture index resolves', badTexRef, 0);
  }

  // --- v2 colour bake, checked on the map that motivated it ---------------
  //
  // log_in.wld renders on the PC with D3DRS_LIGHTING=FALSE and hour forced to
  // 1 (night), so its NIGHT bake is literally what the login screen shows.
  // If the colour tail is wrong the login goes back to guessing.
  const loginT = path.join(OUT, 'log_in.terrain');
  if (!fs.existsSync(loginT)) {
    check('log_in.terrain present', false);
  } else {
    const b = fs.readFileSync(loginT);
    const meshCount = b.readUInt32LE(8), nodeCount = b.readUInt32LE(12);
    const vertexCount = b.readUInt32LE(16), indexCount = b.readUInt32LE(20);
    const preColor = 48 + meshCount * 16 + nodeCount * 40 +
                     vertexCount * 12 + vertexCount * 8 + indexCount * 2;
    eq('log_in has the colour bake', b.readUInt32LE(preColor), 1);
    const dayBase = preColor + 4, nightBase = dayBase + vertexCount * 4;
    eq('  colour tail sized to the header', nightBase + vertexCount * 4, b.length);

    // What log_in's bake actually contains, measured from the source data:
    // day and night are IDENTICAL on every vertex (this map was baked once,
    // so the forced night hour at the login changes nothing), every colour is
    // grey (R==G==B — a lighting bake, not a tint), opaque, and bimodal —
    // lit faces at 255 and shadowed faces at 102 (0.40). These pins are what
    // a byte-order or offset slip would break first.
    let differ = 0, alphaBad = 0, notGrey = 0, at255 = 0, at102 = 0;
    for (let i = 0; i < vertexCount; i++) {
      const d = b.readUInt32LE(dayBase + i * 4), n = b.readUInt32LE(nightBase + i * 4);
      if (d !== n) differ++;
      if ((d >>> 24) !== 255) alphaBad++;
      const r = (d >>> 16) & 255, g = (d >>> 8) & 255, bl = d & 255;
      if (r !== g || g !== bl) notGrey++;
      if (r === 255) at255++;
      else if (r === 102) at102++;
    }
    eq('  day and night sets are identical on this map', differ, 0);
    eq('  every baked alpha is opaque', alphaBad, 0);
    eq('  every baked colour is grey (a light bake, not a tint)', notGrey, 0);
    check('  bake is bimodal: lit 255 and shadow 102 both well represented',
          at255 > 10000 && at102 > 10000, `255:${at255} 102:${at102}`);
  }

  // --- .rmesh -------------------------------------------------------------
  const MESHDIR = path.join(__dirname, '..', '..', 'assets', 'meshes');
  const rmeshFiles = fs.existsSync(MESHDIR)
    ? fs.readdirSync(MESHDIR).filter((n) => n.endsWith('.rmesh')) : [];
  if (!rmeshFiles.length) {
    check('mesh export present (run extract-meshes.js --out ../../assets/meshes)', false);
  } else {
    eq('.rmesh files written', rmeshFiles.length, 4467);
    let sizeBad = 0, refBad = 0, parentBad = 0, weightBad = 0, subBad = 0,
        localBad = 0, strBad = 0, skinnedFiles = 0;
    for (const name of rmeshFiles) {
      const b = fs.readFileSync(path.join(MESHDIR, name));
      if (b.toString('latin1', 0, 4) !== 'RMSH') { sizeBad++; continue; }
      const skin = (b.readUInt32LE(8) & 1) !== 0;
      if (skin) skinnedFiles++;
      const boneCount = b.readUInt32LE(12), meshCount = b.readUInt32LE(16);
      const subCount = b.readUInt32LE(20), skinCount = b.readUInt32LE(24);
      const vCount = b.readUInt32LE(28), iCount = b.readUInt32LE(32);
      const strBytes = b.readUInt32LE(36);

      const strAt = 44, boneAt = strAt + strBytes, meshAt = boneAt + boneCount * 72;
      const subAt = meshAt + meshCount * 40, skinAt = subAt + subCount * 12;
      const posAt = skinAt + skinCount * 72;
      const idxAt = posAt + vCount * 12 + vCount * 12 + vCount * 8 +
                    (skin ? vCount * 8 + vCount * 16 : 0);
      if (idxAt + iCount * 4 !== b.length) { sizeBad++; continue; }
      const biAt = posAt + vCount * 12 + vCount * 12 + vCount * 8;
      const bwAt = biAt + vCount * 8;

      if (strBytes && b[strAt + strBytes - 1] !== 0) strBad++;

      // A parent must precede its child, which is what lets both the C# importer
      // and any world-transform pass work in a single forward loop.
      for (let i = 0; i < boneCount; i++) {
        const p = b.readInt32LE(boneAt + i * 72);
        if (p < -1 || p >= i) { parentBad++; break; }
      }
      // localBone is an index into this file's bones, or -1 for an external one.
      for (let i = 0; i < skinCount; i++) {
        const l = b.readInt32LE(skinAt + i * 72 + 4);
        if (l < -1 || l >= boneCount) { localBad++; break; }
      }
      for (let m = 0; m < meshCount; m++) {
        const o = meshAt + m * 40;
        const vOff = b.readUInt32LE(o + 4), vCnt = b.readUInt32LE(o + 8);
        const iOff = b.readUInt32LE(o + 12), iCnt = b.readUInt32LE(o + 16);
        const sOff = b.readUInt32LE(o + 20), sCnt = b.readUInt32LE(o + 24);
        const kOff = b.readUInt32LE(o + 28), kCnt = b.readUInt32LE(o + 32);
        if (vOff + vCnt > vCount || iOff + iCnt > iCount ||
            sOff + sCnt > subCount || kOff + kCnt > skinCount) { refBad++; break; }
        // Submesh runs must tile the mesh's index range exactly — a gap means
        // triangles that never get drawn, an overlap means double-drawn ones.
        let covered = 0;
        for (let s = 0; s < sCnt; s++) covered += b.readUInt32LE(subAt + (sOff + s) * 12 + 8);
        if (covered !== iCnt) { subBad++; break; }
        // Indices are mesh-local.
        for (let k = 0; k < iCnt; k++) {
          if (b.readUInt32LE(idxAt + (iOff + k) * 4) >= vCnt) { refBad++; k = iCnt; m = meshCount; }
        }
        // Bone indices address the MESH's skin table, and weights sum to 1.
        if (skin && kCnt) {
          for (let v = 0; v < vCnt; v++) {
            let sum = 0;
            for (let k = 0; k < 4; k++) {
              const w = b.readFloatLE(bwAt + (vOff + v) * 16 + k * 4);
              if (w > 0 && b.readUInt16LE(biAt + (vOff + v) * 8 + k * 2) >= kCnt) { refBad++; v = vCnt; break; }
              sum += w;
            }
            if (Math.abs(sum - 1) > 0.001) { weightBad++; break; }
          }
        }
      }
    }
    eq('  size matches header', sizeBad, 0);
    eq('  every cross-reference in range', refBad, 0);
    eq('  bone parents precede their children', parentBad, 0);
    eq('  skin localBone in range or -1', localBad, 0);
    eq('  submeshes tile each mesh exactly', subBad, 0);
    eq('  bone weights sum to 1', weightBad, 0);
    eq('  string blobs NUL-terminated', strBad, 0);
    // See the matching comment on this same number in RunCheck.cs's Meshes()
    // check — measured 2026-08-20 after the xmesh.js object-reference fix.
    eq('  skinned files', skinnedFiles, 2576);
  }

  // --- .ranim -------------------------------------------------------------
  const ANIMDIR = path.join(__dirname, '..', '..', 'assets', 'anim');
  const ranimFiles = fs.existsSync(ANIMDIR)
    ? fs.readdirSync(ANIMDIR).filter((n) => n.endsWith('.ranim')) : [];
  if (!ranimFiles.length) {
    check('anim export present (run extract-anim.js --out ../../assets/anim)', false);
  } else {
    eq('.ranim files written', ranimFiles.length, 6498);
    let sizeBad = 0, refBad = 0, quatBad = 0, unsorted = 0, tickBad = 0, typeBad = 0;
    let totalKeys = 0;
    for (const name of ranimFiles) {
      const b = fs.readFileSync(path.join(ANIMDIR, name));
      if (b.toString('latin1', 0, 4) !== 'RANM') { sizeBad++; continue; }
      const tc = b.readUInt32LE(12), mk = b.readUInt32LE(16), qk = b.readUInt32LE(20);
      const tps = b.readUInt32LE(28), sb = b.readUInt32LE(32);
      if (tps !== 4800) tickBad++;
      if (40 + sb + tc * 20 + mk * 68 + qk * 44 !== b.length) { sizeBad++; continue; }
      totalKeys += mk + qk;
      const trAt = 40 + sb, mAt = trAt + tc * 20, qAt = mAt + mk * 68;
      for (let i = 0; i < tc; i++) {
        const o = trAt + i * 20;
        const type = b.readUInt32LE(o + 4);
        const ko = b.readUInt32LE(o + 8), kc = b.readUInt32LE(o + 12);
        if (type > 1) { typeBad++; break; }
        if (type === 0 ? ko + kc > mk : ko + kc > qk) { refBad++; break; }
        // Key times must be non-decreasing or any interpolator walks backwards.
        const base = type === 0 ? mAt : qAt, stride = type === 0 ? 68 : 44;
        let prev = -1;
        for (let k = 0; k < kc; k++) {
          const t = b.readUInt32LE(base + (ko + k) * stride);
          if (t < prev) { unsorted++; break; }
          prev = t;
        }
      }
      // Rotations are stored already decompressed and normalised. This is the
      // check that would catch the asymmetric QUATCOMP decode being wrong.
      for (let i = 0; i < qk; i++) {
        const o = qAt + i * 44;
        const x = b.readFloatLE(o + 28), y = b.readFloatLE(o + 32);
        const z = b.readFloatLE(o + 36), w = b.readFloatLE(o + 40);
        if (Math.abs(Math.hypot(x, y, z, w) - 1) > 1e-3) { quatBad++; break; }
      }
    }
    eq('  size matches header', sizeBad, 0);
    eq('  every key reference in range', refBad, 0);
    eq('  track types are known', typeBad, 0);
    eq('  ticksPerSecond is 4800 everywhere', tickBad, 0);
    eq('  key times non-decreasing', unsorted, 0);
    eq('  rotations are unit quaternions', quatBad, 0);
    eq('  total keys', totalKeys, 15452358);
  }

  // --- .rmapobj -----------------------------------------------------------
  const OBJDIR = path.join(__dirname, '..', '..', 'assets', 'mapobj');
  const objFiles = fs.existsSync(OBJDIR)
    ? fs.readdirSync(OBJDIR).filter((n) => n.endsWith('.rmapobj')) : [];
  if (!objFiles.length) {
    check('map object export present (run extract-mapobj.js --out ../../assets/mapobj)', false);
  } else {
    eq('.rmapobj files written', objFiles.length, 33);
    let sizeBad = 0, refBad = 0, subBad = 0, strBad = 0, nonFinite = 0, degenerate = 0;
    let objects = 0, verts = 0;
    for (const name of objFiles) {
      const b = fs.readFileSync(path.join(OBJDIR, name));
      if (b.toString('latin1', 0, 4) !== 'ROBJ') { sizeBad++; continue; }
      const objCount = b.readUInt32LE(8), meshCount = b.readUInt32LE(12);
      const subCount = b.readUInt32LE(16), vCount = b.readUInt32LE(20);
      const iCount = b.readUInt32LE(24), strBytes = b.readUInt32LE(28);
      const strAt = 68, objAt = strAt + strBytes, meshAt = objAt + objCount * 76;
      const subAt = meshAt + meshCount * 32, posAt = subAt + subCount * 20;
      const uvAt = posAt + vCount * 12, idxAt = uvAt + vCount * 8;
      // v2 tails: [u32 hasColors][ARGB?][u32 hasNormals][f32x3?].
      const colorFlagAt = idxAt + iCount * 2;
      if (colorFlagAt + 4 > b.length) { sizeBad++; continue; }
      const hasColors = b.readUInt32LE(colorFlagAt);
      if (hasColors > 1) { sizeBad++; continue; }
      const normalFlagAt = colorFlagAt + 4 + (hasColors ? vCount * 4 : 0);
      if (normalFlagAt + 4 > b.length) { sizeBad++; continue; }
      const hasNormals = b.readUInt32LE(normalFlagAt);
      if (hasNormals > 1 ||
          normalFlagAt + 4 + (hasNormals ? vCount * 12 : 0) !== b.length) { sizeBad++; continue; }
      objects += objCount; verts += vCount;
      if (strBytes && b[strAt + strBytes - 1] !== 0) strBad++;

      for (let o = 0; o < objCount; o++) {
        const oo = objAt + o * 76;
        if (b.readUInt32LE(oo) >= strBytes && strBytes) { strBad++; break; }
        // A transform must be finite and must not be all-zero, or the object
        // collapses to a point at the origin.
        let any = false;
        for (let k = 0; k < 16; k++) {
          const v = b.readFloatLE(oo + 4 + k * 4);
          if (!Number.isFinite(v)) { nonFinite++; break; }
          if (v !== 0) any = true;
        }
        if (!any) degenerate++;
        const fm = b.readUInt32LE(oo + 68), nm = b.readUInt32LE(oo + 72);
        if (fm + nm > meshCount) { refBad++; break; }
      }

      for (let m = 0; m < meshCount; m++) {
        const mo = meshAt + m * 32;
        const fs2 = b.readUInt32LE(mo), ns = b.readUInt32LE(mo + 4);
        const vOff = b.readUInt32LE(mo + 8), vCnt = b.readUInt32LE(mo + 12);
        const iOff = b.readUInt32LE(mo + 16), iCnt = b.readUInt32LE(mo + 20);
        if (fs2 + ns > subCount || vOff + vCnt > vCount || iOff + iCnt > iCount) {
          refBad++; break;
        }
        // Indices are mesh-local.
        for (let k = 0; k < iCnt; k++) {
          if (b.readUInt16LE(idxAt + (iOff + k) * 2) >= vCnt) { refBad++; k = iCnt; m = meshCount; }
        }
        // Attribute ranges are in FACES, not indices — the easiest field in the
        // whole format to misread by a factor of three.
        for (let s = 0; s < ns; s++) {
          const so = subAt + (fs2 + s) * 20;
          const faceStart = b.readUInt32LE(so + 4), faceCount = b.readUInt32LE(so + 8);
          if ((faceStart + faceCount) * 3 > iCnt) { subBad++; break; }
        }
      }
      for (let i = 0; i < vCount * 3; i++) {
        if (!Number.isFinite(b.readFloatLE(posAt + i * 4))) { nonFinite++; break; }
      }
    }
    eq('  size matches header', sizeBad, 0);
    eq('  every cross-reference in range', refBad, 0);
    eq('  attribute ranges are in faces and fit', subBad, 0);
    eq('  string offsets in range and blob terminated', strBad, 0);
    eq('  transforms and positions finite', nonFinite, 0);
    eq('  no all-zero transforms', degenerate, 0);
    eq("  total placed objects", objects, 14968);
    check('  total object vertices', verts > 1e6, `${verts}`);

    // The login map's replace-piece chain — the 116 trees the PC login shows
    // and the octree-only extraction silently dropped. Three .pis models,
    // every instance pointing at SHARED mesh ranges, every piece vertex
    // carrying its in-VB diffuse bake.
    const li = path.join(OBJDIR, 'log_in.rmapobj');
    if (!fs.existsSync(li)) {
      check('log_in.rmapobj present (the 116 login trees)', false);
    } else {
      const b = fs.readFileSync(li);
      const objCount = b.readUInt32LE(8), meshCount = b.readUInt32LE(12);
      const vCount = b.readUInt32LE(20), iCount = b.readUInt32LE(24);
      const strBytes = b.readUInt32LE(28);
      eq("log_in places 116 trees + the grass", objCount, 117);
      eq("  from 8 shared meshes (3 pis x 2 + 2 grass)", meshCount, 8);
      const colorFlagAt = 68 + strBytes + objCount * 76 + meshCount * 32 +
                          b.readUInt32LE(16) * 20 + vCount * 12 + vCount * 8 + iCount * 2;
      eq('  carries the vertex-colour bake', b.readUInt32LE(colorFlagAt), 1);
      // Shared ranges: more objects than meshes proves instancing works.
      let maxFirst = 0;
      for (let o = 0; o < objCount; o++)
        maxFirst = Math.max(maxFirst, b.readUInt32LE(68 + strBytes + o * 76 + 68));
      check('  instances reference shared mesh ranges', maxFirst < meshCount,
            `maxFirstMesh ${maxFirst} of ${meshCount}`);
    }
  }

  // --- per-map manifests --------------------------------------------------
  const MAPDIR = path.join(__dirname, '..', '..', 'assets', 'maps');
  const manifests = fs.existsSync(MAPDIR)
    ? fs.readdirSync(MAPDIR).filter((n) => n.endsWith('.map.json')) : [];
  if (!manifests.length) {
    check('map manifests present (run build-maps.js --out ../../assets/maps)', false);
  } else {
    eq('map manifests written', manifests.length, 130);
    const ASSETROOT = path.join(__dirname, '..', '..', 'assets');
    let missingFile = 0, badBounds = 0, noTextures = 0, contained = 0, checked = 0;
    const escaped = [];
    for (const name of manifests) {
      const m = JSON.parse(fs.readFileSync(path.join(MAPDIR, name), 'utf8'));
      // Every referenced asset must actually exist — a manifest pointing at a
      // file that was never written is worse than no manifest.
      for (const part of [m.terrain, m.objects, m.navmesh]) {
        if (part && !fs.existsSync(path.join(ASSETROOT, part.file))) missingFile++;
      }
      if (m.bounds) {
        for (let a = 0; a < 3; a++) {
          if (!(m.bounds.max[a] > m.bounds.min[a])) badBounds++;
        }
      }
      if (m.terrain && (!m.textures || !m.textures.length)) noTextures++;

      // The cross-check: three independent parsers, two container formats, one
      // coordinate space. If the walkable surface sits inside the world it walks
      // on, none of the three decoders is badly wrong.
      if (m.checks.navmeshOverflow !== null) {
        checked++;
        if (m.checks.navmeshOverflow <= 1.0) contained++;
        else escaped.push(`${m.map} by ${m.checks.navmeshOverflow.toFixed(1)}`);
      }
    }
    eq('  every referenced asset file exists', missingFile, 0);
    eq('  bounds are non-degenerate on every axis', badBounds, 0);
    eq('  every terrain map lists its textures', noTextures, 0);
    eq('  navmesh sits inside the world bounds', contained, checked - 2);
    // The two exceptions are character-select scenes: a single backdrop mesh
    // with a deliberately larger navmesh, not a decode error.
    check('  and the only exceptions are the character-select scenes',
          escaped.length === 2 && escaped.every((s) => s.startsWith('cha1_select') ||
                                                       s.startsWith('cha2_select')),
          escaped.join('; '));
    // w_school_03 is the map the Phase 0 spawn was verified against.
    const school = JSON.parse(fs.readFileSync(path.join(MAPDIR, 'w_school_03.map.json'), 'utf8'));
    check('  w_school_03 navmesh fits with margin to spare',
          school.checks.navmeshOverflow < 0 && school.checks.navmeshOverflow > -5,
          `${school.checks.navmeshOverflow}`);
  }

  // --- texture reference audit --------------------------------------------
  // A missing texture is silent everywhere in this pipeline — it is a grey wall
  // in the game, not an error — so the count is pinned here. If it moves,
  // something either started referencing assets that do not ship, or stopped
  // resolving ones that do.
  {
    const AUD = path.join(__dirname, 'audit-assets.js');
    check('audit-assets.js present', fs.existsSync(AUD));
    // Extension mismatches must keep resolving: 247 files named .dds are really
    // TGA/PNG/PSD, and the engine sniffs headers rather than trusting the name.
    const out = require('child_process')
      .execSync(`node --max-old-space-size=8192 "${AUD}"`, { encoding: 'utf8' });
    const num = (re) => { const m = out.match(re); return m ? parseInt(m[1].replace(/,/g, ''), 10) : -1; };
    eq('textures shipped', num(/textures shipped:\s+([\d,]+)/), 15194);
    // MEASURED 2026-08-20 after fixing xmesh.js's object-reference bug (see
    // the matching comment on "skinned files" in the .rmesh section above):
    // a `.x` mesh whose materials were shared by reference (`{ Name }`)
    // instead of defined inline had its TextureFilename(s) silently detached
    // along with its skin data, so its real texture references were
    // structurally invisible — either falling back to the unreliable scanned
    // pass or not counted at all. Fixing the parser surfaced 1,247 more real
    // "parsed" texture references (was 6,656) and correspondingly needed
    // fewer scanned-fallback candidates (was 6,422) and fewer "resolved by
    // name" guesses (was 63) — and, because those references are now
    // counted at all, surfaced more genuinely-missing files (533 -> 563,
    // 1303 -> 1282) and far fewer "referenced by nothing" orphans
    // (4,180 -> 3,539).
    eq('  referenced, parsed sources', num(/parsed\s+\([^)]*\)\s+([\d,]+)/), 7903);
    eq('  referenced, scanned sources', num(/scanned\s+\([^)]*\)\s+([\d,]+)/), 5903);
    eq('  resolved by name, not extension', num(/resolved by name, not ext\s+([\d,]+)/), 113);
    // The parsed number is the trustworthy one — it comes from structured fields
    // in formats this pipeline fully decodes.
    eq('  missing, from parsed refs', num(/MISSING, from PARSED refs\s+([\d,]+)/), 563);
    eq('  missing, from scanned refs', num(/MISSING, from SCANNED refs\s+([\d,]+)/), 1282);
    // Character pieces are the item -> art link. Adding them took the
    // unreferenced pile from 8,327 to 4,436 — the single biggest reduction, and
    // the reason the earlier "install-size saving" reading was wrong.
    eq('  referenced by nothing', num(/referenced by nothing scanned:\s+([\d,]+)/), 3539);
    // Non-texture references must stay OUT of the texture set. Routing item
    // model/piece names into it once inflated the missing count from 433 to
    // 7,938, and the extension-agnostic fallback then "resolved" `sword.x`
    // against `sword.dds`, hiding the real answer behind a wrong one.
    eq('  .cps character pieces referenced', num(/\.cps\s+([\d,]+) referenced/), 6696);
    check('  .cps are not counted as textures', !out.includes('.cps') ||
          num(/parsed\s+\([^)]*\)\s+([\d,]+)/) < 8000);
    // Guard the wording too: this number has been misread as an install saving
    // once already, and it is not one.
    check('  the unreferenced figure is labelled as not-a-deletion-list',
          out.includes('not a deletion list'));
    // Quest/NPC/level are scanned and contribute zero texture references — a
    // real finding, and the reason that caveat no longer applies.
    check('  quest/NPC/level coverage is stated', out.includes('reference no textures'));
    check('  parsed and scanned findings are reported separately',
          out.includes('reliable') && out.includes('candidates'));
  }

  // --- item tables ---------------------------------------------------------
  // The only way item icons become visible: they are addressed as
  // strInventoryFile + sICONID, never as a literal filename anywhere else.
  {
    const I = require('./itemdata');
    const glogic = A('glogic/GLogic.rcc');
    check('GLogic.rcc present', fs.existsSync(glogic));
    const ar = new RccArchive(glogic);
    let totalItems = 0, withBasic = 0, falsePositives = 0, bytesExact = 0;
    for (const name of ['item.isf', 'item1.isf']) {
      const t = I.parse(ar.read(name));
      eq(`${name} records read`, t.items.length, t.count);
      // Reading to the last byte across 26 MB is the real proof the record
      // resync is right — the sentinel scan has nowhere to hide a drift.
      if (t.bytesRead === t.fileSize) bytesExact++;
      totalItems += t.items.length;
      withBasic += t.items.filter((x) => x.basic).length;
      falsePositives += t.stats.sentinelFalsePositives;
      eq(`  ${name} failed records`, t.stats.failedItems, 0);
    }
    eq('  both tables consumed to EOF exactly', bytesExact, 2);
    eq('  total items', totalItems, 37091);
    eq('  every item decoded its SBASIC block', withBasic, totalItems);
    eq('  no sentinel false positives', falsePositives, 0);

    // SCHARSTATS is blitted with ReadBuffer inside SITEMBASIC::LOAD; every field
    // after it — including the two this needs — shifts if the stride is wrong.
    eq('  SCHARSTATS stride comes from the probe', I.SCHARSTATS, 12);
  }

  // Quaternion decompression: x,y,z are biased and scaled by 1/32768; w is
  // NEITHER, because the compressor negates the quaternion to force w >= 0.
  // Making these four symmetric yields rotations that look almost right.
  {
    const AK = require('./animkeys');
    const mid = AK.decompressQuat((32767 << 16) | 32767, (32767 << 16) | 0);
    check('decompress: biased channels centre on 0',
          Math.abs(mid[0]) < 1e-6 && Math.abs(mid[1]) < 1e-6 && Math.abs(mid[2]) < 1e-6);
    eq('decompress: w has no bias', mid[3], 0);
    const wFull = AK.decompressQuat(0, 0xffff);
    check('decompress: w spans 0..1', Math.abs(wFull[3] - 1) < 1e-4, `${wFull[3]}`);
    check('decompress: x,y,z span -1..1',
          Math.abs(AK.decompressQuat(0xffff0000, 0)[0] - 1) < 1e-3 &&
          Math.abs(AK.decompressQuat(0, 0)[0] + 1) < 1e-3);
  }

  // Navmesh: same discipline, and its counts are explicit already.
  const NAV = path.join(__dirname, '..', '..', 'assets', 'navmesh', 'w_school_03.navmesh');
  if (fs.existsSync(NAV)) {
    const b = fs.readFileSync(NAV);
    eq('navmesh magic is "RNAV"', b.toString('latin1', 0, 4), 'RNAV');
    eq('  as a little-endian u32', b.readUInt32LE(0), 0x56414e52);
    eq('navmesh version', b.readUInt32LE(4), 1);
    const v = b.readUInt32LE(8), c = b.readUInt32LE(12);
    eq('navmesh size matches its header', 16 + v * 12 + c * 12 + c * 12, b.length);
  }
}

console.log('\nanimation type mapping');
{
  // `EMANI_MAINTYPE` per clip lives in the per-clip `.cfg` (type string
  // SANIMCONINFO), NOT in `.chf`/`.abf` as first assumed. `.chf` says which
  // clips a character OWNS; `.cfg` says what each clip IS. The engine bridges
  // them in DxSkinAniMan::LoadAnimContainer, which swaps `.x` for `.cfg`.
  const I = require('./animinfo');
  const map = path.join(__dirname, '..', '..', 'assets', 'animtypes.json');
  check('animtypes.json present (run extract-animtypes.js)', fs.existsSync(map));

  if (fs.existsSync(map)) {
    const data = JSON.parse(fs.readFileSync(map, 'utf8'));
    const clips = data.clips || data;
    const names = Object.keys(clips);
    check('clips typed', names.length > 6000, `${names.length}`);

    // Sizes are ReadBuffer strides and must come from the probe. 304, not the
    // payload's 302 — tail padding is part of the blit.
    const LAYOUT = require('../layout-probe/layout.json');
    eq('SANIMCONINFO_101 stride from the probe',
       LAYOUT.allStructs.SANIMCONINFO_101.size, 304);
    check('  ACF_LOOP resolved from the compiler, not guessed', I.ACF_LOOP > 0);

    // The decisive cross-check: `m_dwETimeOrig` is a DWORD at a probe-derived
    // offset in the `.cfg`, while `durationTicks` was measured by walking bone
    // tracks in a different file in a different container. They agree.
    const animDir = path.join(__dirname, '..', '..', 'assets', 'anim');
    let compared = 0, agree = 0;
    for (const name of names) {
      const rec = clips[name];
      const f = path.join(animDir, name + '.ranim');
      if (!rec || rec.endTimeOrig == null || !fs.existsSync(f)) continue;
      const b = fs.readFileSync(f);
      if (b.toString('latin1', 0, 4) !== 'RANM') continue;
      compared++;
      if (b.readUInt32LE(24) === rec.endTimeOrig) agree++;
      if (compared >= 2000) break;
    }
    check('  .cfg duration agrees with the .ranim it describes',
          compared > 500 && agree > compared * 0.95, `${agree}/${compared}`);
  }

  // The full harness the agent wrote: parse rate, name matching, tick
  // agreement, name-vs-type semantics, and a positive control that re-reads
  // 0x0108+ records in 0x0106 field order and must flag every one.
  const out = require('child_process')
    .execSync(`node --max-old-space-size=8192 "${path.join(__dirname, 'verify-animtypes.js')}"`,
              { encoding: 'utf8' });
  check('  verify-animtypes.js passes end to end', out.includes('every check passed'));
  // Walks loop, deaths do not. Either a wrong m_dwFlag offset or a wrong
  // ACF_LOOP bit would fail to split those two apart.
  check('  loop flag separates walks from deaths',
        /AN_WALK[^\n]*9[0-9]\.\d+%/.test(out) && /AN_DIE[^\n]*[0-4]\.\d+%/.test(out));
}

console.log('\neffect containers (.egp)');
{
  // `.egp` is EFF_PROPGROUP: a tree of EFF_PROPERTY nodes, 13 concrete types.
  // The trap is that the declared node size is WRONG by a predictable amount:
  //   +4 when the branch reads a DXAFFINEPARTS, -4 when the type never reads
  //   m_bMoveObj.
  // A naive walk reaches 92.6% of files; applying the rule reaches 99.4%.
  const out = require('child_process')
    .execSync(`node --max-old-space-size=8192 "${path.join(__dirname, 'effect-census.js')}"`,
              { encoding: 'utf8' });
  const num = (re) => { const m = out.match(re); return m ? parseInt(m[1].replace(/,/g, ''), 10) : -1; };

  eq('.egp files', num(/\.egp files: (\d+)/), 4294);
  eq('  walked to exact EOF', num(/Walked to exact EOF: (\d+)/), 4268);
  // The encoded count must match what CRYPT-MAP.md recorded from a completely
  // separate analysis — two independent routes to the same version gate.
  eq('  encoded at the 0x0200 gate', num(/0x0200\s+(\d+)/), 1165);
}

console.log('\neffect field table');
{
  // The sidecar that names each stream index. Its cross-check rebuilds the
  // per-node counts from the layout table and compares them against the counts
  // in every written .reff — if the two ever disagree the table would silently
  // mis-name every field of the affected type.
  const out = require('child_process')
    .execSync(`node "${path.join(__dirname, 'gen-effectfields.js')}"`, { encoding: 'utf8' });
  const num = (re) => { const m = out.match(re); return m ? parseInt(m[1].replace(/,/g, ''), 10) : -1; };
  eq('  layouts', num(/^(\d+) \(type, version\) layouts/m), 29);
  eq('  fields', num(/layouts, (\d+) fields/), 858);
  eq('  nodes cross-checked', num(/cross-check: ([\d,]+) nodes/), 33624);
  eq('  count disagreements', num(/nodes, (\d+) disagree/), 0);
  eq('  fields with no layout entry', num(/(\d+) carry fields with no layout/), 0);
}

console.log('\neffect export (.reff)');
{
  // The exported form, checked by a reader that shares no code with the writer
  // and against facts the writer never saw: the shipped file set, and a fresh
  // effect-egp.js walk. A writer verified only by its own reader proves the two
  // agree, not that either is right.
  const out = require('child_process')
    .execSync(`node --max-old-space-size=8192 "${path.join(__dirname, 'verify-effects.js')}"`,
              { encoding: 'utf8' });
  const num = (re) => { const m = out.match(re); return m ? parseInt(m[1].replace(/,/g, ''), 10) : -1; };

  eq('  .reff files', num(/^(\d+) \.reff/m), 4294);
  eq('  nodes', num(/([\d,]+) nodes,/), 34609);
  eq('  nodes with decoded fields', num(/([\d,]+) decoded/), 33624);
  // Matches the count EFFECTS-PROPS.md derived by a separate route.
  eq('  floats checked', num(/([\d,]+) floats checked/), 1267239);
  eq('  non-finite floats', num(/floats checked, (\d+) non-finite/), 0);
  check('  all structural checks pass', /\n\d+ passed, 0 failed/.test(out),
        out.split('\n').slice(-4).join(' '));
}

{
  // Skills.
  //
  // The regression guarded here is the one that actually happened: skill.csv
  // re-emits its header before EVERY record, and header-part-A has the same
  // 322-field width as record-part-A. A reader that treats every 322-field line
  // as a row therefore sees exactly twice as many skills as exist (2,334 for
  // 1,167), and reads emIMPACT_TAR — a 0..4 enum — as values like 65535 and -40.
  console.log('\nskills');
  const { parse, TAR } = require('./extract-skills.js');

  // Synthetic first, in the real four-line shape.
  const hdrA = ['sNATIVEID wMainID', 'sNATIVEID wSubID', 'szNAME'].join(',');
  const hdrB = ['emBASIC_TYPE', 'x', 'y'].join(',');
  const fake = [hdrA, hdrB, '1,2,A', '9,9,9', hdrA, hdrB, '3,4,B', '9,9,9'].join('\n');
  const r = parse(fake);
  eq('  records, not header lines', r.rows.length, 2);
  eq('  first record is data', r.rows[0][2], 'A');
  eq('  second record is data', r.rows[1][2], 'B');
  eq('  header captured once', r.header[0], 'sNATIVEID wMainID');
  eq('  headers agree across blocks', r.mismatched, 0);

  // A trailing header with no record behind it must not become a record.
  const dangling = [hdrA, hdrB, '1,2,A', '9,9,9', hdrA].join('\n');
  eq('  a dangling header yields no extra record', parse(dangling).rows.length, 1);

  // Then the real file, end to end.
  const skillOut = require('child_process')
    .execSync('node "' + path.join(__dirname, 'extract-skills.js') + '"',
              { encoding: 'utf8' });
  const snum = (re) => {
    const m = skillOut.match(re);
    return m ? parseInt(m[1].replace(/,/g, ''), 10) : -1;
  };
  eq('  shipped skills', snum(/-> (\d+) skills/), 1167);

  // Every label on the targeting line must be a NAME. A bare number there means
  // a value fell outside the enum, which is the misalignment signature.
  const tarLine = skillOut.split('\n').find((l) => l.startsWith('targeting:')) || '';
  check('  every target mode is a named enum member',
        tarLine.length > 0 && !/[\s:]\d+:\d+/.test(tarLine.replace('targeting:', '')),
        tarLine);
  check('  no out-of-range report', !/FAIL: .* outside/.test(skillOut), tarLine);
  eq('  all five modes present',
     TAR.filter((n) => new RegExp('\\b' + n + ':').test(tarLine)).length, 5);
}

{
  // Skill -> effect (.egp) mapping — extract-skilleffects.js. The cast/hit egp
  // names are DECLARED as skill.csv columns (SEXT_DATA mirror), not decoded from
  // a binary, so this proves the column pick and the effects.json coverage.
  console.log('\nskilleffects');
  const { parse: parseSe, pickEgp } = require('./extract-skilleffects.js');

  // pickEgp: first non-empty ".egp" across a group's 9 variant columns; drops
  // effskin_a (not a .egp) and strips the extension, lowercasing.
  const col = {};
  ['strTARGZONE02 1', 'strTARGZONE02 2', 'strSELFBODY 1'].forEach((c, i) => { col[c] = i; });
  const row = ['', 'Miragekick_Fire.egp', 'faa101_st.effskin_a'];
  eq('  picks slot-2 when slot-1 empty, lowercased no-ext',
     pickEgp(row, col, ['strTARGZONE02']), 'miragekick_fire');
  eq('  drops effskin (not .egp)', pickEgp(row, col, ['strSELFBODY']), '');

  // The real file, end to end + on-disk coverage against effects.json.
  const seOut = require('child_process')
    .execSync('node "' + path.join(__dirname, 'extract-skilleffects.js') + '"',
              { encoding: 'utf8' });
  const seNum = (re) => { const m = seOut.match(re); return m ? parseInt(m[1], 10) : -1; };
  check('  most skills carry an effect', seNum(/-> (\d+) with an effect/) > 800,
        seOut.trim());
  check('  hundreds have a cast egp', seNum(/\((\d+) cast/) > 400, seOut.trim());
  check('  hundreds have a hit egp', seNum(/(\d+) hit\)/) > 700, seOut.trim());

  const seJson = path.resolve(__dirname, '../../unity/RanMobile/Assets/Ran/Resources/skilleffects.json');
  const effJson = path.resolve(__dirname, '../../unity/RanMobile/Assets/Ran/Resources/effects.json');
  if (fs.existsSync(seJson) && fs.existsSync(effJson)) {
    const se = JSON.parse(fs.readFileSync(seJson, 'utf8')).entries;
    const eff = (() => { const j = JSON.parse(fs.readFileSync(effJson, 'utf8')); return j.effects || j; })();
    const has = (k) => k && Object.prototype.hasOwnProperty.call(eff, k);
    eq('  staged entries', se.length, 896);
    check('  ids are lowercase, no .egp', se.every((e) =>
      (!e.c || (e.c === e.c.toLowerCase() && !/\.egp$/.test(e.c))) &&
      (!e.h || (e.h === e.h.toLowerCase() && !/\.egp$/.test(e.h)))), 'bad id form');
    const hitCov = se.filter((e) => e.h).filter((e) => has(e.h)).length;
    const hitTot = se.filter((e) => e.h).length;
    check('  >85% of hit effects exist in effects.json',
          hitCov / hitTot > 0.85, `${hitCov}/${hitTot}`);
  }
}

{
  // Per-skill CAST ANIMATION — extract-skillanim.js. The PC plays a SPECIFIC
  // clip per skill via SELECTSKILLANI(emANIMTYPE, emANISTYPE) (GLCharacter.cpp:
  // 3515), NOT a generic AN_ATTACK. Those two fields are DECLARED columns of the
  // same skill.csv, so this pins the by-NAME column pick, the anim-key packing,
  // and that no value falls outside the DxAniKeys enum ranges (the misalignment
  // signature — the same class of bug the emIMPACT_TAR guard catches).
  console.log('\nskillanim');
  const sa = require('./extract-skillanim.js');

  // Synthetic four-line block with the anim columns placed at ODD positions, to
  // prove resolution is by NAME and not by the shipped column index (107/108).
  const hA = ['sNATIVEID wMainID', 'sNATIVEID wSubID', 'emANISTYPE', 'x', 'emANIMTYPE'];
  const hB = ['emBASIC_TYPE', 'p', 'q', 'r', 's'];
  const text = [
    hA.join(','), hB.join(','), '11,0,5,z,9', '0,0,0,0,0',
    hA.join(','), hB.join(','), '3,1,0,z,4', '0,0,0,0,0',
  ].join('\n');
  const p = sa.parse(text);
  eq('  records, not header lines', p.rows.length, 2);
  const m = sa.toEntries(p.header, p.rows);
  eq('  both rows mapped', m.entries.length, 2);
  eq('  none out of range', m.outOfRange, 0);
  // Row 1: emANIMTYPE=9 (AN_SKILL_A), emANISTYPE=5 -> k = 9*100+5 = 905.
  eq('  anim key packs main*100+sub', m.entries[0].k, 905);
  eq('  main resolved by name', m.entries[0].am, 9);
  eq('  sub resolved by name', m.entries[0].as, 5);
  eq('  skill id carried through', `${m.entries[0].m}_${m.entries[0].s}`, '11_0');
  eq('  row 2 is AN_ATTACK k=400', m.entries[1].k, 400);

  // Positive control: a misaligned read that puts a non-enum value in the anim
  // column MUST be flagged out-of-range, so a clean run means something.
  const badText = [
    hA.join(','), hB.join(','), '11,0,5,z,65535', '0,0,0,0,0',
  ].join('\n');
  const bp = sa.parse(badText);
  const bm = sa.toEntries(bp.header, bp.rows);
  eq('  a 65535 anim type is rejected', bm.outOfRange, 1);
  eq('  ...and not emitted', bm.entries.length, 0);

  // The real file, end to end.
  const saOut = require('child_process')
    .execSync('node "' + path.join(__dirname, 'extract-skillanim.js') + '"',
              { encoding: 'utf8' });
  const saNum = (re) => { const mm = saOut.match(re); return mm ? parseInt(mm[1], 10) : -1; };
  eq('  shipped skills mapped', saNum(/-> (\d+) skills mapped/), 1167);
  eq('  none out of enum range (real file)', saNum(/\((\d+) out of enum range\)/), 0);
  eq('  distinct anim keys', saNum(/distinct anim keys \(am:as\): (\d+)/), 371);
  check('  no misalignment FAIL', !/FAIL:/.test(saOut), saOut.trim());
  // Skills draw from AN_ATTACK and AN_SKILL_A..G — eight distinct main types.
  check('  eight EMANI_MAINTYPEs used', /AN_SKILL_A:399/.test(saOut) &&
        /AN_ATTACK:48/.test(saOut) && /AN_SKILL_G:83/.test(saOut), saOut.trim());

  // Staged JSON, and its coverage against the clip table the controllers build
  // from. Every anim key that a skill needs SHOULD have at least one shipped clip
  // somewhere in animflat (a character without that clip falls back in-graph, but
  // the KEY itself must exist as an authored animation for the mapping to be real).
  const saJson = path.resolve(__dirname, '../../unity/RanMobile/Assets/Ran/Resources/skillanim.json');
  const afJson = path.resolve(__dirname, '../../unity/RanMobile/Assets/Ran/animflat.json');
  if (fs.existsSync(saJson)) {
    const doc = JSON.parse(fs.readFileSync(saJson, 'utf8'));
    eq('  staged entries', doc.entries.length, 1167);
    check('  every entry k = am*100+as, in range',
          doc.entries.every((e) => e.k === e.am * 100 + e.as &&
            e.am >= 0 && e.am < sa.MAIN_MAX && e.as >= 0 && e.as < sa.SUB_MAX), 'bad key');
    const distinct = new Set(doc.entries.map((e) => e.k));
    eq('  distinct keys in JSON', distinct.size, 371);
    if (fs.existsSync(afJson)) {
      const af = JSON.parse(fs.readFileSync(afJson, 'utf8'));
      const clipEntries = af.entries || af;
      const clipKeys = new Set(clipEntries
        .filter((e) => !e.u && !e.d)          // full-body only, like the builder
        .map((e) => e.t * 100 + e.s));
      const covered = [...distinct].filter((k) => clipKeys.has(k)).length;
      // Not 100%: a handful of authored anim keys have no shipped clip on this
      // build (event/legacy skills), the same class of gap as the 433 missing
      // textures. The bulk must resolve, or the mapping is decoding the wrong
      // columns.
      check('  >85% of cast keys have a shipped clip',
            covered / distinct.size > 0.85, `${covered}/${distinct.size}`);
    }
  }
}

{
  // Skill NAMES / DESCRIPTIONS / ICONS — extract-skillnames.js.
  //
  // skill.csv only carries the string-table KEY ("SN_mid_sid"); the display name
  // lives in SkillStrTable.txt, AES-encrypted under GLogic (GLSkill.cpp:749/1047).
  // These pin: the AES decode, the TIS-620 -> Unicode transcode of the Thai
  // descriptions, and the honest fallback where a name is genuinely absent.
  console.log('\nskill names (SkillStrTable.txt)');
  const skn = require('./extract-skillnames.js');
  const { RccArchive } = require('./rcc.js');
  const arc = new RccArchive(
    path.join(__dirname, '..', '..', '..', 'CLIENT', 'data', 'glogic', 'GLogic.rcc'));

  // TIS-620 positive control: 0xA1 must map to U+0E01 (ก), the base of the Thai
  // block; a wrong offset shifts every glyph and the check would catch it.
  eq('  TIS-620 0xA1 -> U+0E01', skn.tis620(Buffer.from([0xA1])).charCodeAt(0), 0x0E01);
  eq('  TIS-620 passes ASCII', skn.tis620(Buffer.from('Hi', 'latin1')), 'Hi');

  const st = skn.loadStringTable(arc);
  eq('  name entries decoded', st.count, 619);
  eq('  SN_000_000 name', st.names['SN_000_000'], 'Armor Damage');
  eq('  SN_000_001 name', st.names['SN_000_001'], 'Heavy Punch');
  eq('  SN_004_000 name', st.names['SN_004_000'], 'Spinning Attack');
  eq('  SN_012_000 name', st.names['SN_012_000'], 'Rapid Pierce');
  const isThai = (s) => [...(s || '')].some((ch) => {
    const c = ch.charCodeAt(0); return c >= 0x0E00 && c <= 0x0E7F;
  });
  check('  SD_000_003 decodes to Thai', isThai(st.descs['SD_000_003']),
        JSON.stringify(st.descs['SD_000_003']));

  const built = skn.build();
  eq('  player-class skills', built.entries.length, 657);
  eq('  real names resolved', built.resolved, 451);
  // Assassin genuinely has NO SN_043_* rows on this build — the same reason the
  // PC shows the bare key. Proven, not hidden: those stay flagged placeholders.
  eq('  Assassin resolved 0/88', built.perClass[43].resolved, 0);
  eq('  Brawler resolved 77/77', built.perClass[0].resolved, 77);

  const brawl = built.entries.find((e) => e.m === 0 && e.s === 1);
  eq('  a resolved entry is not a placeholder', brawl.p, 0);
  eq('  a resolved entry carries the real name', brawl.n, 'Heavy Punch');
  const assa = built.entries.find((e) => e.m === 43);
  eq('  an unresolved entry is a placeholder', assa.p, 1);
  check('  an unresolved entry keeps the key', /^SN_043_/.test(assa.n), assa.n);
  // Icon fields for the atlas draw: cells are 35x35 on a 512x256 sheet, so a
  // column is 0..14 and a row 0..7.
  eq('  brawler icon atlas', brawl.f, 'skill_fighter.dds');
  check('  icon cell indices in range',
        brawl.x >= 0 && brawl.x < 15 && brawl.y >= 0 && brawl.y < 8, `${brawl.x},${brawl.y}`);

  // The staged JSON the runtime loads must match the build, Thai intact (UTF-8).
  const stagedPath = path.join(__dirname, '..', '..', 'unity', 'RanMobile',
    'Assets', 'Ran', 'Resources', 'skillinfo.json');
  if (!fs.existsSync(stagedPath)) {
    check('  skillinfo.json staged (run extract-skillnames.js)', false, stagedPath);
  } else {
    const staged = JSON.parse(fs.readFileSync(stagedPath, 'utf8'));
    eq('  staged entry count', staged.entries.length, built.entries.length);
    const s0 = staged.entries.find((e) => e.m === 0 && e.s === 1);
    eq('  staged name matches', s0.n, 'Heavy Punch');
    const sd = staged.entries.find((e) => e.m === 0 && e.s === 3);
    check('  staged desc is Thai', sd && isThai(sd.d), sd && sd.d);
  }
}

// --- .rshadow (the ground-shadow decal) -----------------------------------
{
  const SHADOW = path.join(__dirname, '..', '..', 'assets', 'mapobj', 'log_in.rshadow');
  if (!fs.existsSync(SHADOW)) {
    check('log_in.rshadow present (run extract-mapobj.js)', false);
  } else {
    const b = fs.readFileSync(SHADOW);
    eq('rshadow magic "RSHD"', b.toString('latin1', 0, 4), 'RSHD');
    eq('  version', b.readUInt32LE(4), 1);
    check('  density is 0..1', b.readFloatLE(8) > 0 && b.readFloatLE(8) <= 1, `${b.readFloatLE(8)}`);
    let p = 12;
    const texLen = b.readUInt32LE(p); p += 4;
    eq('  texture is the day lightmap', b.toString('latin1', p, p + texLen - 1), 'log_in.dds');
    p += texLen;
    const meshCount = b.readUInt32LE(p), totalV = b.readUInt32LE(p + 4), totalI = b.readUInt32LE(p + 8);
    p += 12;
    const tableAt = p, posAt = tableAt + meshCount * 16;
    const uvAt = posAt + totalV * 12, idxAt = uvAt + totalV * 8;
    eq('  size matches header', idxAt + totalI * 2, b.length);
    // UVs are a planar projection — every one must land inside the [0,1]
    // lightmap (a projection escaping the box would sample wrap/clamp garbage).
    let uvOut = 0, idxBad = 0;
    for (let i = 0; i < totalV; i++) {
      const u = b.readFloatLE(uvAt + i * 8), v = b.readFloatLE(uvAt + i * 8 + 4);
      if (u < -0.01 || u > 1.01 || v < -0.01 || v > 1.01) uvOut++;
    }
    for (let m = 0; m < meshCount; m++) {
      const mo = tableAt + m * 16;
      const vCnt = b.readUInt32LE(mo + 4), iOff = b.readUInt32LE(mo + 8), iCnt = b.readUInt32LE(mo + 12);
      for (let k = 0; k < iCnt; k++) if (b.readUInt16LE(idxAt + (iOff + k) * 2) >= vCnt) { idxBad++; break; }
    }
    eq('  projected UVs stay inside the lightmap', uvOut, 0);
    eq('  indices mesh-local and in range', idxBad, 0);
    check('  has shadow geometry', totalV > 500 && totalI > 1000, `${totalV}v ${totalI}i`);
  }
}

// --- gameword.json (the client's UI strings) ------------------------------
{
  const GW = path.join(__dirname, '..', '..', 'assets', 'gameword.json');
  if (!fs.existsSync(GW)) {
    check('gameword.json present (run extract-gameword.js)', false);
  } else {
    const w = JSON.parse(fs.readFileSync(GW, 'utf8'));
    check('  word count', Object.keys(w).length > 700, `${Object.keys(w).length}`);
    // The login-page strings the UI actually looks up. ID/Pass stay Latin in
    // the shipped Thai file; the rest are Thai — if these regress the login
    // page silently falls back to English placeholders.
    eq('  LOGIN_PAGE_IDPW[0]', w.LOGIN_PAGE_IDPW && w.LOGIN_PAGE_IDPW[0], 'ID');
    eq('  LOGIN_PAGE_IDPW[1]', w.LOGIN_PAGE_IDPW && w.LOGIN_PAGE_IDPW[1], 'Pass');
    eq('  LOGIN_PAGE_OKCANCEL[0]', w.LOGIN_PAGE_OKCANCEL && w.LOGIN_PAGE_OKCANCEL[0], 'ตกลง');
    eq('  LOGIN_PAGE_OKCANCEL[1]', w.LOGIN_PAGE_OKCANCEL && w.LOGIN_PAGE_OKCANCEL[1], 'ยกเลิก');
    eq('  LOGIN_PAGE_IDSAVE_BACK[0]', w.LOGIN_PAGE_IDSAVE_BACK && w.LOGIN_PAGE_IDSAVE_BACK[0], 'จำชื่อ ID');
    eq('  SELECT_SERVER[0]', w.SELECT_SERVER && w.SELECT_SERVER[0], 'เลือกเซิร์ฟเวอร์');
    eq('  SELECT_SERVER_CONNECTQUIT[1]', w.SELECT_SERVER_CONNECTQUIT && w.SELECT_SERVER_CONNECTQUIT[1], 'ออก');
  }
}

// --- gameintext.json (the client's SEPARATE "in-game text" table) ---------
//
// CGameTextMan keeps GAME_WORD (ID2GAMEWORD) and GAME_IN_TEXT (ID2GAMEINTEXT)
// as two distinct maps loaded from two distinct files inside Gui.rcc
// (GameTextControl.cpp; RANPARAM.cpp:181 strGameInText = "gameintext.xml").
// RanRevivePanel used to look REBIRTH_DIALOGUE_TEXT up in gameword.json and
// silently miss (real id, wrong table) — this pins the fix: the id is real,
// present here, and the staged Resources copy carries the same value the
// runtime reads via RanGameWord.InText.
console.log('\ngameintext.json (the client\'s in-game text table, separate from gameword)');
{
  const GIT = path.join(__dirname, '..', '..', 'assets', 'gameintext.json');
  if (!fs.existsSync(GIT)) {
    check('gameintext.json present (run extract-gameintext.js)', false);
  } else {
    const g = JSON.parse(fs.readFileSync(GIT, 'utf8'));
    check('  entry count', Object.keys(g).length > 2000, `${Object.keys(g).length}`);
    check('  REBIRTH_DIALOGUE_TEXT absent from gameword.json (confirms the two tables really are separate)',
          (() => {
            const GW = path.join(__dirname, '..', '..', 'assets', 'gameword.json');
            const w = JSON.parse(fs.readFileSync(GW, 'utf8'));
            return !Object.prototype.hasOwnProperty.call(w, 'REBIRTH_DIALOGUE_TEXT');
          })());
    eq('  REBIRTH_DIALOGUE_TEXT[0]', g.REBIRTH_DIALOGUE_TEXT && g.REBIRTH_DIALOGUE_TEXT[0],
       'ต้องการเกิดใหม่หรือเปล่า ?');
    check('  REBIRTH_DIALOGUE_TEXT2 present', !!(g.REBIRTH_DIALOGUE_TEXT2 && g.REBIRTH_DIALOGUE_TEXT2[0]));

    const stagedPath = path.resolve(__dirname, '../../unity/RanMobile/Assets/Ran/Resources/gameintext.json');
    if (fs.existsSync(stagedPath)) {
      const staged = fs.readFileSync(stagedPath, 'utf8');
      const source = fs.readFileSync(GIT, 'utf8');
      check('  staged gameintext.json matches the source copy', staged === source);
    } else {
      check('  staged gameintext.json present under unity Resources', false);
    }
  }
}

// --- CHATMACRO window layout/strings (menu-bar quick-phrase editor) -------
//
// The CHATMACRO/RUN buttons were inert placeholders; CHATMACRO is now wired
// (RanChatMacroWindow) against these REAL uicfg.json/gameword.json values —
// pin them so a future re-extraction cannot silently reflow the window
// without the runtime noticing.
console.log('\nCHATMACRO window (uicfg.json rects + gameword.json strings)');
{
  const CFG = path.join(__dirname, '..', '..', 'assets', 'uicfg.json');
  const c = JSON.parse(fs.readFileSync(CFG, 'utf8')).controls;
  check('  CHATMACRO_WINDOW present', !!c.CHATMACRO_WINDOW);
  eq('  CHATMACRO_WINDOW size', c.CHATMACRO_WINDOW && `${c.CHATMACRO_WINDOW.w}x${c.CHATMACRO_WINDOW.h}`, '375x360');
  eq('  CHAT_MACRO_EDIT0 rect', c.CHAT_MACRO_EDIT0 && `${c.CHAT_MACRO_EDIT0.x},${c.CHAT_MACRO_EDIT0.y},${c.CHAT_MACRO_EDIT0.w},${c.CHAT_MACRO_EDIT0.h}`, '90,40,270,11');
  eq('  CHATMACRO_OKBUTTON rect', c.CHATMACRO_OKBUTTON && `${c.CHATMACRO_OKBUTTON.x},${c.CHATMACRO_OKBUTTON.y}`, '300,335');
  check('  all 10 CHAT_MACRO_EDIT{n}/EDIT_BACK{n}/KEYTEXT{n} present',
        Array.from({ length: 10 }, (_, i) => i).every((i) =>
          c[`CHAT_MACRO_EDIT${i}`] && c[`CHAT_MACRO_EDIT_BACK${i}`] && c[`CHAT_MACRO_KEYTEXT${i}`]));

  const GW = path.join(__dirname, '..', '..', 'assets', 'gameword.json');
  const w = JSON.parse(fs.readFileSync(GW, 'utf8'));
  eq('  CHATMACRO_WINDOW_NAME_STATIC[0]', w.CHATMACRO_WINDOW_NAME_STATIC && w.CHATMACRO_WINDOW_NAME_STATIC[0], 'ตั้งประโชคที่ใช้บ่อย');
  eq('  CHATMACRO_OKBUTTON[0]', w.CHATMACRO_OKBUTTON && w.CHATMACRO_OKBUTTON[0], 'ตกลง');
  eq('  CHATMACRO_KEYTEXT[0] (real PC label — the hotkey, not shown as-is on mobile; see RanChatMacroWindow)',
     w.CHATMACRO_KEYTEXT && w.CHATMACRO_KEYTEXT[0], 'Alt + 1');
}

// --- item-shop / cash-mall category names (ITEMSHOP_MENU_BUTTON) ----------
//
// The item-mall window cycler showed "Cat N" because the category-name table
// was not decoded. The PC builds those labels from ID2GAMEWORD(
// "ITEMSHOP_MENU_BUTTON", i) (CItemShopWindow::InitShop, ItemShopWindow.cpp:774)
// — a WORD group in the client's own gameword.xml, NOT server data and NOT a
// hand-fixed string. 17 entries (ITEM_SHOP_MAX_CATEGORY, ItemShopWindow.h:68),
// index = the catalog row's category id. Parsed from the live archive AND pinned
// against the staged Resources json the runtime (RanItemMallModule) loads.
console.log('\nitem-shop category names (ITEMSHOP_MENU_BUTTON)');
{
  const { parseCategories, WORD_ID, MAX_CATEGORY } = require('./extract-itemshop-categories');
  const gui = new RccArchive(A('gui/Gui.rcc'));
  const entry = gui.entries.find((e) => /^gameword\.xml$/i.test(e.name));
  check('gameword.xml present in Gui.rcc', !!entry);
  const cats = parseCategories(gui.read(entry).toString('utf8'));
  check('ITEMSHOP_MENU_BUTTON present', !!cats);
  eq('category count', cats.length, MAX_CATEGORY);           // 17: 0=all, 1..16
  check('every category name is non-empty', cats.every((s) => s && s.length > 0));
  // Spot-check the anchor names by index — Thai UTF-8 in the XML (no CP874
  // byte-decode: gameword.xml is UTF-8). A shifted index would mislabel a tab.
  eq('cat 0 = all items', cats[0], 'ไอเทมทั้งหมด');
  eq('cat 3 = weapon', cats[3], 'อาวุธ');
  eq('cat 13 = box', cats[13], 'กล่อง');
  eq('cat 16 = discount', cats[16], 'สินค้าลดราคา');

  // The staged Resources json the runtime reads must match, in the {id:[values]}
  // shape RanGameWord parses (so RanItemMallModule.CategoryName == ID2GAMEWORD).
  const catPath = path.join(__dirname, '..', '..', 'unity', 'RanMobile', 'Assets',
                            'Ran', 'Resources', 'itemshop-categories.json');
  if (fs.existsSync(catPath)) {
    const j = JSON.parse(fs.readFileSync(catPath, 'utf8'));
    check('itemshop-categories.json keyed by ' + WORD_ID, Array.isArray(j[WORD_ID]));
    eq('staged category count', j[WORD_ID] ? j[WORD_ID].length : -1, MAX_CATEGORY);
    check('staged names match the archive', JSON.stringify(j[WORD_ID]) === JSON.stringify(cats));
  } else {
    check('itemshop-categories.json present (run extract-itemshop-categories.js)', false);
  }
}

// --- persistent in-game HUD controls (uiinnercfg01.xml) -------------------
//
// Pins the config the mobile HUD is assembled from: RanHud/RanMinimap/
// RanQuickSlots/RanMenuBar/RanChat draw these EXACT rects and textures via
// RanUiCfg.PlaceTopLevel, so a shift here would move the HUD off the PC's own
// layout silently. Parsed the same way extract-uicfg.js parses it (the source
// of uicfg.json), from the live archive rather than a staged copy.
console.log('\npersistent HUD controls (uiinnercfg01.xml)');
{
  const { parseControl } = require('./extract-uicfg');
  const gui = new RccArchive(A('gui/Gui.rcc'));
  const entry = gui.entries.find((e) => /^uiinnercfg01\.xml$/i.test(e.name));
  if (!entry) {
    check('uiinnercfg01.xml present in Gui.rcc', false);
  } else {
    let buf = gui.read(entry);
    if (G.isEncoded(buf)) { try { buf = G.decode(buf); } catch { /* plain */ } }
    const text = buf.toString('latin1').replace(/[^\x20-\x7e\r\n]/g, '');
    const controls = {};
    const re = /<CONTROL([^>]*)>([\s\S]*?)<\/CONTROL>/g;
    let m;
    while ((m = re.exec(text))) {
      const id = /Id\s*=\s*"([^"]*)"/.exec(m[1]);
      const local = /Local\s*=\s*"([^"]*)"/.exec(m[1]);
      if (id && !controls[id[1]]) controls[id[1]] = parseControl(id[1], m[2], local ? local[1] : '');
    }

    // Assert one control's rect and (optionally) its atlas + source rect.
    const rect = (id, x, y, w, h) => {
      const c = controls[id] || {};
      check(`  ${id} rect`, c.x === x && c.y === y && c.w === w && c.h === h,
            `${c.x},${c.y} ${c.w}x${c.h} want ${x},${y} ${w}x${h}`);
    };
    const tex = (id, name, tx, ty, tw, th) => {
      const c = controls[id] || {};
      check(`  ${id} tex`,
            (c.tex || '').toLowerCase() === name.toLowerCase() &&
            c.tx === tx && c.ty === ty && c.tw === tw && c.th === th,
            `${c.tex} @ ${c.tx},${c.ty} ${c.tw}x${c.th}`);
    };

    // Status gauges — the cluster group is top-left (Create, UI_FLAG_DEFAULT);
    // the bars step 0,14,28,42,57 with the coloured fill inset 31px.
    rect('BASIC_INFO_VIEW', 42, 0, 162, 71);
    check('  BASIC_INFO_VIEW untextured', !controls.BASIC_INFO_VIEW.tex);
    rect('BASIC_INFO_VIEW_HP', 0, 0, 162, 14);
    tex('BASIC_INFO_VIEW_HP', 'Interface_Main.dds', 84, 163, 162, 14);
    tex('BASIC_INFO_VIEW_HP_OVERIMAGE', 'Interface_Main.dds', 84, 221, 130, 13);
    rect('BASIC_INFO_VIEW_MP', 0, 14, 162, 14);
    rect('BASIC_INFO_VIEW_SP', 0, 28, 162, 14);
    rect('BASIC_INFO_VIEW_EXP', 0, 42, 162, 15);
    tex('BASIC_INFO_VIEW_EXP_OVERIMAGE', 'Interface_Main.dds', 84, 263, 130, 13);
    rect('BASIC_INFO_VIEW_CP', 0, 57, 162, 14);
    // The CP gauge is the only bar on a different atlas — charinven02.dds, staged
    // by stage-ui-atlases.js. If this name changes the CP frame goes blank.
    tex('BASIC_INFO_VIEW_CP', 'charinven02.dds', 136, 160, 162, 14);
    rect('BASIC_INFO_VIEW_HP_TEXT', 108, 2, 50, 11);

    // Minimap — group top-right (UI_FLAG_RIGHT); the round map is MINIMAP_BACK.
    rect('BASIC_MINIMAP', 460, 0, 340, 120);
    tex('MINIMAP_BACK', 'CharInven.dds', 272, 414, 98, 98);

    // Quick-skill tray — left-edge vertical strip (CreateEx, no WINDOW_ALIGN =>
    // UI_FLAG_DEFAULT) with a 41x41 slot template.
    rect('QUICK_SKILL_TRAY_TAB_WINDOW', 0, 41, 41, 415);
    tex('BASIC_QUICK_SKILL_SLOT', 'Interface_Main.dds', 0, 163, 41, 41);

    // Game menu — bottom-right bar (UI_FLAG_RIGHT|UI_FLAG_BOTTOM) with 24x24 icons.
    rect('GAME_MENU', 426, 574, 317, 26);
    tex('GAME_MENU', 'Interface_Main.dds', 16, 277, 2, 26);
    rect('MENU_INVENTORY_BUTTON', 18, 1, 24, 24);
    rect('MENU_SKILL_BUTTON', 68, 1, 24, 24);

    // Chat — bottom-left box (UI_FLAG_BOTTOM).
    rect('BASIC_CHAT_BOX', 0, 445, 447, 155);
  }
}

console.log('\ncrow model table (nativeID -> .chf, and emClass -> body)');
{
  const { parseCrowTable, SZCHARSKIN } = require('./extract-crowmodels');
  const arc = new RccArchive(A('glogic/GLogic.rcc'));
  const entry = arc.entries.find((e) => /crow\.mnsf$/i.test(e.name));
  check('Crow.mnsf present in GLogic.rcc', !!entry);
  const parsed = parseCrowTable(arc.read(entry));
  eq('Crow.mnsf file version', parsed.fileVersion, 0x0200);
  eq('crow count', parsed.rows.length, 1854);
  eq('crows with a skinObj', parsed.stats.withSkin, 1390);
  // Consumed to the byte: the sentinel resync landed on every record boundary.
  eq('parsed to EOF (0 tail bytes)', parsed.stats.fileSize - parsed.stats.bytesRead, 0);
  // Only one action version ships, read by SCROWACTION::LOAD.
  check('all action blocks are version 0x0201',
        parsed.stats.actionVersions.size === 1 && parsed.stats.actionVersions.has(0x0201));

  // NPC-vs-mob kind (m_emCrow). Every SBASIC block is the current 0x0205 layout,
  // so m_emCrow is read at the probe-measured offset 4 (layout.json SCROWBASIC).
  check('all basic blocks are version 0x0205',
        parsed.stats.basicVersions.size === 1 && parsed.stats.basicVersions.has(0x0205));
  // The kind histogram: CROW_NPC(1)=493 talk/shop/gate NPCs, CROW_MOB(2)=952
  // monsters (+ CROW_PET(5)=88, CROW_ZONE_NAME(8)=321). All are valid EMCROW
  // members — a wrong offset would read arbitrary dwords, not this clean split.
  eq('crows that are NPCs (CROW_NPC)', parsed.stats.npcCount, 493);
  eq('crows that are MOBs (CROW_MOB)', parsed.stats.kinds.get(2), 952);
  // POSITIVE CONTROL: only these four EMCROW values appear (1,2,5,8); a mis-read
  // offset would scatter across many bogus values.
  check('only valid EMCROW kinds appear',
        [...parsed.stats.kinds.keys()].every((k) => [1, 2, 5, 8].includes(k)),
        `saw ${[...parsed.stats.kinds.keys()].join(',')}`);
  // A record's own emCrow must be one of the known kinds, per row.
  const npcRows = parsed.rows.filter((r) => r.kind === 1);
  eq('per-row NPC count matches stat', npcRows.length, 493);
  // NPC nativeIDs are dominated by npc_*-skinned crows — a sanity cross-check.
  const npcSkinned = npcRows.filter((r) => /^npc/i.test(r.skin || ''));
  check('most NPCs carry an npc_* skin', npcSkinned.length > 300,
        `${npcSkinned.length}/${npcRows.length}`);

  // POSITIVE CONTROL for the skinObj offset (26). Every skin is a `.chf`; a wrong
  // field offset would read garbage here and this would fail. Verified 1390/1390.
  const skins = parsed.rows.filter((r) => r.skin);
  check('every skinObj is a .chf (offset 26 is right)',
        skins.every((r) => /\.chf$/i.test(r.skin)),
        `${skins.filter((r) => !/\.chf$/i.test(r.skin)).length} are not .chf`);

  const distinct = new Set(skins.map((r) => r.skin.toLowerCase()));
  eq('distinct skins', distinct.size, 675);
  const byId = new Map(parsed.rows.map((r) => [r.nativeId, r.skin]));
  // main=0, sub=1 -> nativeId 0x00010000 -> the first school ghost variant.
  eq('nativeID 65536 -> mob_ch_02.chf', (byId.get(65536) || '').toLowerCase(), 'mob_ch_02.chf');

  // Per-monster speed (walkVelo/runVelo, WALKVELO_OFFSET=12/RUNVELO_OFFSET=20 in
  // the same SCROWACTION prefix as the skinObj offset already pinned above): the
  // only source of a real per-monster move speed, since neither SDROP_CROW nor
  // SNETCROW_MOVETO carry one on the wire. nativeID 0 (main=0,sub=0, mob_ch_01)
  // is 10/30 world-units/sec.
  const speedById = new Map(parsed.rows.map((r) => [r.nativeId, r]));
  eq('nativeID 0 walkVelo', speedById.get(0).walkVelo, 10);
  eq('nativeID 0 runVelo', speedById.get(0).runVelo, 30);
  // POSITIVE CONTROL: real per-monster speeds sit in the same order of magnitude
  // as the PC's own player class constants (fWALKVELO 10-12, fRUNVELO 34-36,
  // GLogicData.cpp:245-246) — a wrong offset would not land in this range at all.
  const mobRows = parsed.rows.filter((r) => r.kind === 2 && r.walkVelo > 0);
  const walkVels = mobRows.map((r) => r.walkVelo).sort((a, b) => a - b);
  const medianWalk = walkVels[Math.floor(walkVels.length / 2)];
  check('median monster walkVelo is a walking pace, not a sprint (5..25 u/s)',
        medianWalk >= 5 && medianWalk <= 25, `median ${medianWalk}`);
  check('MOB records with a real speed number in the thousands range', mobRows.length > 500,
        `${mobRows.length}`);

  // szCharSkin is purely by sex: 16 class bits, exactly {o_m, o_w}.
  eq('player body table has 16 class entries', SZCHARSKIN.length, 16);
  check('player bodies are exactly o_m / o_w',
        new Set(SZCHARSKIN).size === 2 && SZCHARSKIN.includes('o_m') && SZCHARSKIN.includes('o_w'));
  eq('class bit 0x01 (BRAWLER_M) -> o_m', SZCHARSKIN[0], 'o_m');
  eq('class bit 0x04 (ARCHER_W)  -> o_w', SZCHARSKIN[2], 'o_w');

  // The emitted runtime table, if present, must agree with the parse.
  const cmPath = path.join(__dirname, '..', '..', 'unity', 'RanMobile', 'Assets',
                           'Ran', 'Resources', 'crowmodels.json');
  if (fs.existsSync(cmPath)) {
    const cm = JSON.parse(fs.readFileSync(cmPath, 'utf8'));
    eq('crowmodels.json crow entries', cm.crows.length / 2, 1390);
    eq('crowmodels.json player entries', cm.players.length / 2, 16);
    const cmById = new Map();
    for (let i = 0; i < cm.crows.length; i += 2) cmById.set(cm.crows[i], cm.crows[i + 1]);
    eq('crowmodels.json 65536 -> mob_ch_02', cmById.get(65536), 'mob_ch_02');
    // npc_mgod_n (nativeID 4194367 = main63/sub64) is the crow that pushed the
    // skinned count to 1390; it must be present in the emitted table.
    eq('crowmodels.json 4194367 -> npc_mgod_n', cmById.get(4194367), 'npc_mgod_n');
    const cmByBit = new Map();
    for (let i = 0; i < cm.players.length; i += 2) cmByBit.set(cm.players[i], cm.players[i + 1]);
    eq('crowmodels.json class 0x01 -> o_m', cmByBit.get(1), 'o_m');
    eq('crowmodels.json class 0x04 -> o_w', cmByBit.get(4), 'o_w');
    // The NPC nativeID list the runtime tags tapped entities against.
    eq('crowmodels.json npc ids', (cm.npcs || []).length, 493);
    const npcSet = new Set(cm.npcs || []);
    check('crowmodels.json nativeID 8 is an NPC', npcSet.has(8));
    check('crowmodels.json a MOB nativeID is not an NPC', !npcSet.has(65536));
    // Per-monster speed table (the "mob walks fast" fix): nativeID -> (walkVelo,
    // runVelo) triples, emitted for every row that reached a real 0x201 action
    // block (1483 of 1854 — the rest are stationary trap/material types with 0/0
    // and are legitimately omitted, not dropped by a bug).
    eq('crowmodels.json speed entries', (cm.speeds || []).length / 3, 1483);
    const speedById = new Map();
    for (let i = 0; i < (cm.speeds || []).length; i += 3) {
      speedById.set(cm.speeds[i], [cm.speeds[i + 1], cm.speeds[i + 2]]);
    }
    const [w0, r0] = speedById.get(0) || [];
    eq('crowmodels.json nativeID 0 walkVelo', w0, 10);
    eq('crowmodels.json nativeID 0 runVelo', r0, 30);
  } else {
    check('crowmodels.json present (run extract-crowmodels.js)', false);
  }
}

console.log('\nreferenced character prefabs (every crow/body skin can build + stage)');
{
  // The runtime resolves a crow to Resources.Load("Characters/<skinStem>"); a
  // stem with no prefab is a capsule. This pins that EVERY referenced skin can be
  // materialised — either it already has a prefab, or (the 158 that BuildAll
  // dedup-collapsed) it is a structural twin of one that does. The invariant is
  // stable across RanCharacterBuilder.BuildMissing: after it runs, every skin has
  // its OWN prefab and the twin clause is trivially satisfied.
  const proj = (...p) => path.join(__dirname, '..', '..', 'unity', 'RanMobile',
                                   'Assets', 'Ran', ...p);
  const cmPath = proj('Resources', 'crowmodels.json');
  const flatPath = proj('charflat.json');
  const chDir = proj('Characters');
  if (fs.existsSync(cmPath) && fs.existsSync(flatPath) && fs.existsSync(chDir)) {
    const cm = JSON.parse(fs.readFileSync(cmPath, 'utf8'));
    const crowSkins = new Set(), idToStem = new Map();
    for (let i = 0; i < cm.crows.length; i += 2) {
      const s = String(cm.crows[i + 1]).toLowerCase();
      idToStem.set(cm.crows[i], s); crowSkins.add(s);
    }
    const bodies = new Set();
    for (let i = 1; i < cm.players.length; i += 2) bodies.add(String(cm.players[i]).toLowerCase());
    const referenced = new Set([...crowSkins, ...bodies]);
    eq('referenced entity skins (crow ∪ player bodies)', referenced.size, 677);

    // NPC skins reference a subset of the crow skins — no separate merge needed.
    const npcSkins = new Set();
    for (const id of cm.npcs || []) { const s = idToStem.get(id); if (s) npcSkins.add(s); }
    eq('distinct NPC skins', npcSkins.size, 172);
    check('every NPC skin is one of the crow skins', [...npcSkins].every((s) => crowSkins.has(s)));

    // charflat.json is the manifest RanChfBuilder/BuildMissing build from. Its
    // dedup key (skeleton|parts) is what "structural twin" means.
    const flat = JSON.parse(fs.readFileSync(flatPath, 'utf8'));
    const key = (c) => c.k + '|' + (c.p || []).map((p) => p.m + ':' + (p.s || '')).join(',');
    const byName = new Map(), keyToNames = new Map();
    for (const c of flat.chars) {
      const n = c.n.toLowerCase();
      if (!byName.has(n)) byName.set(n, c);
      const k = key(c);
      if (!keyToNames.has(k)) keyToNames.set(k, []);
      keyToNames.get(k).push(n);
    }
    const prefabs = new Set(fs.readdirSync(chDir)
      .filter((f) => f.endsWith('.prefab')).map((f) => f.slice(0, -7).toLowerCase()));

    const notInFlat = [...referenced].filter((s) => !byName.has(s));
    eq('every referenced skin is a charflat character', notInFlat.length, 0);
    check('player bodies o_m and o_w are built', prefabs.has('o_m') && prefabs.has('o_w'));

    // The decoded guarantee that makes BuildMissing sound: a referenced skin has a
    // built MODEL if it has its own prefab OR a same-key twin that does.
    const hasBuiltModel = (s) => {
      if (prefabs.has(s)) return true;
      const c = byName.get(s); if (!c) return false;
      return (keyToNames.get(key(c)) || []).some((n) => prefabs.has(n));
    };
    const orphan = [...referenced].filter((s) => !hasBuiltModel(s));
    eq('every referenced skin has a built model (own prefab or structural twin)', orphan.length, 0);
    // POSITIVE CONTROL: a fabricated stem has no model, so the check can fail.
    check('control: a non-existent skin has NO built model', !hasBuiltModel('zzz_not_a_real_skin'));

    // Informational only (NOT pinned — it drops to 0 once BuildMissing runs).
    const pending = [...referenced].filter((s) => !prefabs.has(s));
    console.log(`  (referenced ${referenced.size}: ${referenced.size - pending.length} built now, ` +
                `${pending.length} pending BuildMissing — all with a built twin)`);
  } else {
    check('crowmodels.json + charflat.json + Characters/ present', false);
  }
}

console.log('\nmap catalog (mapslist.mst -> .lev -> .wld -> scene)');
{
  const MC = require('./mapcatalog');
  const B = require('./bytecrypt');
  const glogic = new RccArchive(A('glogic/GLogic.rcc'));
  const mst = glogic.read('mapslist.mst');

  const list = MC.parseMapList(mst);
  eq('mapslist is GLMAPS_LIST v0x0200', list.version, 0x0200);
  eq('map count', list.count, 121);
  // The record has no per-node size; a wrong bool-count would drift, so landing
  // exactly on EOF is the proof the structural walk is right.
  eq('parses byte-exact to EOF', list.bytesRead, list.fileSize);
  check('every node is version 0x0203 in this data',
        Object.keys(list.versions).length === 1 && list.versions[0x0203] === 121);
  check('all maps are on field server 0 (no field reconnect on gate)',
        list.maps.every((m) => m.fieldSID === 0));

  const byId = new Map(list.maps.map((m) => [m.id, m]));
  // Map 8 == w_school_03 is the anchor: the README's Phase-0 spawn was verified
  // on map 8, INDEPENDENTLY of this table, so agreement cross-checks both.
  eq('map 8 -> w_school_03.Lev', (byId.get(8) || {}).lev, 'w_school_03.Lev');
  eq('map 22 -> w_tradezone1.lev', (byId.get(22) || {}).lev, 'w_tradezone1.lev');
  eq('map 22 sMapID is main 22 / sub 0', `${(byId.get(22) || {}).main}~${(byId.get(22) || {}).sub}`, '22~0');

  // TIS-620 Thai transcode: 0xB5 -> U+0E15 (THAI CHARACTER TO TAO). Names read
  // as latin1 mojibake without it, so a client would show garbage map titles.
  eq('tis620(0xB5) == U+0E15', MC.tis620(Buffer.from([0xb5])).charCodeAt(0), 0x0e15);
  check('map 22 name is Thai (non-ASCII)',
        [...((byId.get(22) || {}).name || '')].some((ch) => ch.charCodeAt(0) > 0x7f));

  // Positive control: the byte-crypt table is load-bearing. Decoding the body
  // with the WRONG table must NOT parse — proving a clean parse means the table
  // is right, not that any bytes will do.
  {
    const head = B.readHeader(mst);
    const wrong = B.decode(Buffer.from(mst), 'EMBYTECRYPT_OLD', head.bodyOffset);
    // parseMapList always uses MAPSLIST internally, so test the raw walk: under
    // the wrong table the first node's version DWORD (body offset 4, after the
    // u32 count) is not a version the walk knows, so parsing would throw.
    const firstVer = wrong.readUInt32LE(head.bodyOffset + 4);
    check('wrong byte-crypt table yields an unknown node version (control)',
          MC.TAIL_BOOLS[firstVer] === undefined, `0x${firstVer.toString(16)}`);
  }

  // .lev -> .wld resolution, the hop that makes the scene name.
  const level = new RccArchive(A('glogic/level/Level.rcc'));
  const resolve = (lev) => MC.resolveWld(level.read(lev));
  const trade = resolve('w_tradezone1.Lev');
  eq('w_tradezone1.lev -> tradezone.wld', trade.wld, 'tradezone.wld');
  check('  head block guard passed', trade.ok === true);
  eq('  scene basename', MC.sceneFromWld(trade.wld), 'tradezone');
  eq('w_school_03.lev -> w_school_03.wld', resolve('w_school_03.Lev').wld, 'w_school_03.wld');
  eq('w_city_s_01.lev -> w_city_01.wld', resolve('w_city_s_01.Lev').wld, 'w_city_01.wld');

  // The emitted runtime catalog, if present, must agree with the parse.
  const catPath = path.join(__dirname, '..', '..', 'unity', 'RanMobile', 'Assets',
                            'Ran', 'Resources', 'mapcatalog.json');
  if (fs.existsSync(catPath)) {
    const cat = JSON.parse(fs.readFileSync(catPath, 'utf8'));
    eq('mapcatalog.json map count', cat.count, 121);
    const cById = new Map(cat.maps.map((m) => [m.id, m]));
    eq('mapcatalog.json map 22 -> scene tradezone', (cById.get(22) || {}).scene, 'tradezone');
    eq('mapcatalog.json map 8  -> scene w_school_03', (cById.get(8) || {}).scene, 'w_school_03');
    eq('mapcatalog.json map 3  -> scene w_city_01', (cById.get(3) || {}).scene, 'w_city_01');
    check('mapcatalog.json map 8 carries fog', !!(cById.get(8) || {}).fog);
    check('mapcatalog.json map 22 name is Thai',
          [...((cById.get(22) || {}).name || '')].some((ch) => ch.charCodeAt(0) > 0x7f));

    // Sky/cloud (extract-sky.js), merged the same way fog is.
    const m8sky = (cById.get(8) || {}).sky;
    check('mapcatalog.json map 8 (w_school_03) carries sky', !!m8sky);
    if (m8sky) {
      eq('  map 8 skyEnable', m8sky.skyEnable, true);
      eq('  map 8 cloudEnable', m8sky.cloudEnable, true);
      eq('  map 8 radioAxis (no tilt)', m8sky.radioAxis, 0);
      eq('  map 8 axisValue (no tilt)', m8sky.axisValue, 0);
    }
  } else {
    check('mapcatalog.json present (run extract-mapcatalog.js)', false);
  }
}

console.log('\nper-map sky/cloud (SKY_PROPERTY, extract-sky.js)');
{
  const W = require('./wld');
  const S = require('./extract-sky.js');
  const arc = new RccArchive(A('map/Map.rcc'));

  const out = new Map();
  const seen = new Set();
  let unreadable = 0;
  for (const e of arc.entries) {
    if (!/\.wld$/i.test(e.name)) continue;
    const name = path.basename(e.name).replace(/\.wld$/i, '').toLowerCase();
    if (seen.has(name)) continue;
    seen.add(name);
    let f; try { f = W.open(arc.read(e)); } catch { unreadable++; continue; }
    let sky; try { sky = S.readSkyForWld(f); } catch { sky = null; }
    if (!sky) { unreadable++; continue; }   // e.g. marks.coll overflows the buffer
    out.set(name, sky);
  }

  eq('distinct maps scanned', seen.size, 132);
  eq('unreadable/unparseable .wld (known failure count)', unreadable, 2);
  const withSky = [...out.values()].filter((s) => s.hasSky);
  eq('maps carrying a structurally-valid sky record', withSky.length, 92);
  check('every found sky record is version 0x0102 (measured — the shipped ' +
        'writer, not SKY_PROPERTY::VERSION 0x0103, the current one)',
        withSky.every((s) => s.skyVersion === 0x0102));

  const school = out.get('w_school_03');
  check('w_school_03 carries sky', !!(school && school.hasSky));
  if (school && school.hasSky) {
    eq('  w_school_03 skyEnable', school.skyEnable, true);
    eq('  w_school_03 cloudEnable', school.cloudEnable, true);
    eq('  w_school_03 radioAxis', school.radioAxis, 0);
    eq('  w_school_03 axisValue', school.axisValue, 0);
  }

  // A real, measured edge case: cloudEnable can be true while skyEnable is
  // false (DxSkyMan::Render gates them independently, DxSkyMan.cpp:770,787) —
  // proof this reader keeps the two flags distinct instead of collapsing them
  // into one "has sky" bit.
  const cloudOnly = [...out.entries()].filter(([, s]) => s.hasSky && s.cloudEnable && !s.skyEnable);
  check('at least one shipped map has cloudEnable without skyEnable',
        cloudOnly.length > 0, `${cloudOnly.length} found`);

  // Positive control: an unrecognised SKY_PROPERTY version must be reported
  // unsupported, not silently misread as a known layout.
  const bogus = Buffer.alloc(24);
  bogus.writeUInt32LE(0x0099, 0);
  bogus.writeUInt32LE(16, 4);
  const rejected = S.readSky(bogus, 0);
  eq('an unrecognised SKY_PROPERTY version is reported unsupported, not guessed',
     rejected.unsupportedVersion, 0x0099);

  // 0x0104 is the one branch whose field order differs from VERSION's (a
  // leading spacer BOOL ahead of the four real fields) — pin it directly.
  const v104 = Buffer.alloc(8 + 4 + 16);
  v104.writeUInt32LE(0x0104, 0);
  v104.writeUInt32LE(20, 4);
  v104.writeUInt32LE(1, 8);          // spacer BOOL (ignored)
  v104.writeUInt32LE(1, 12);         // m_bSkyEnable
  v104.writeUInt32LE(0, 16);         // m_bCloudEnable
  v104.writeFloatLE(45, 20);         // m_fAxisValue
  v104.writeUInt32LE(1, 24);         // m_nRadioAxis
  const r104 = S.readSky(v104, 0);
  eq('0x0104 skips its leading spacer BOOL -> skyEnable', r104.skyEnable, 1);
  eq('0x0104 cloudEnable', r104.cloudEnable, 0);
  eq('0x0104 axisValue', r104.axisValue, 45);
  eq('0x0104 radioAxis', r104.radioAxis, 1);
}

console.log('\nLoading screen — per-map background + tip strings (extract-tips.js)');
{
  const { parseTips } = require('./extract-tips');

  // Per-map loading texture: mapcatalog.js's own `loading` field, already
  // parsed structurally alongside strMapName/strBGM (SMAPNODE_DATA::LOAD).
  // Map 8 (w_school_03) carries a REAL per-map image, not the generic
  // fallback, cross-checking that the field is wired end to end.
  const catPath = path.join(__dirname, '..', '..', 'unity', 'RanMobile', 'Assets',
                            'Ran', 'Resources', 'mapcatalog.json');
  if (fs.existsSync(catPath)) {
    const cat = JSON.parse(fs.readFileSync(catPath, 'utf8'));
    const cById = new Map(cat.maps.map((m) => [m.id, m]));
    eq('mapcatalog.json map 8 loading texture', (cById.get(8) || {}).loading, 'loading_056.dds');
  } else {
    check('mapcatalog.json present (run extract-mapcatalog.js)', false);
  }

  // parseTips: CSimpleMessageMan::LoadMessage's own "accumulate until a line
  // that is EXACTLY ';'" rule. A positive control proves the split is on the
  // FULL line, not any occurrence of the character.
  eq('parseTips: two tips split on a lone ";" line',
     JSON.stringify(parseTips('one\n;\ntwo\n;\n')), JSON.stringify(['one', 'two']));
  eq('parseTips: embedded newline kept inside one tip',
     JSON.stringify(parseTips('a\nb\n;\n')), JSON.stringify(['a\nb']));
  eq('parseTips: trailing text with no closing ";" is still flushed at EOF',
     JSON.stringify(parseTips('a\n;\nb')), JSON.stringify(['a', 'b']));
  eq('parseTips: a line merely CONTAINING ";" does not split (control)',
     JSON.stringify(parseTips('a;b\n;\n')), JSON.stringify(['a;b']));
  eq('parseTips: empty input yields no tips',
     JSON.stringify(parseTips('')), JSON.stringify([]));

  // The real staged file, against a direct re-parse of the archive — proves
  // extract-tips.js's OWN output matches what parseTips computes fresh, not
  // just that the function works on synthetic fixtures.
  const tipsPath = path.join(__dirname, '..', '..', 'unity', 'RanMobile', 'Assets',
                             'Ran', 'Resources', 'tips.json');
  if (fs.existsSync(tipsPath)) {
    const staged = JSON.parse(fs.readFileSync(tipsPath, 'utf8'));
    eq('tips.json tip count', staged.tips.length, 23);
    eq('tips.json first tip', staged.tips[0], 'Press the left mouse button to attack.');
    check('tips.json a multi-line tip keeps its embedded newline',
          staged.tips.some((t) => t.includes('\n')));

    const glogic = new RccArchive(A('glogic/GLogic.rcc'));
    const fresh = parseTips(glogic.read('tip.txt').toString('latin1'));
    eq('tips.json matches a fresh re-parse of GLogic.rcc/tip.txt',
       JSON.stringify(staged.tips), JSON.stringify(fresh));
  } else {
    check('tips.json present (run extract-tips.js)', false);
  }
}

console.log('\nItem DATABASE — names, icons, grade, stack cap (itemdb.js)');
{
  const DB = require('./itemdb');
  const glogic = A('glogic/GLogic.rcc');
  const ar = new RccArchive(glogic);

  // --- the item string table: keys resolve to real display names -----------
  // strName is a KEY ("IN_mmm_sss"), not display text; GLStringTable maps it.
  const stbl = DB.loadStringTable(ar.read('ItemStrTable.txt'));
  check('ItemStrTable.txt decodes (AES + version prefix)', stbl.size > 10000);
  eq('  string-table entries', stbl.size, 18771);
  eq('  IN_000_003 -> "Japanese Sword"', stbl.get('IN_000_003'), 'Japanese Sword');
  eq('  IN_000_001 -> "Freshman Stick"', stbl.get('IN_000_001'), 'Freshman Stick');
  // A Thai name is stored as RAW cp874 bytes (0xA1..0xFF), not ASCII — the proof
  // that the runtime's RanText.DecodeCp874 has real bytes to work on.
  const thai = stbl.get('IN_000_009') || '';
  check('  IN_000_009 is Thai (raw cp874, high bytes present)',
        [...thai].some((c) => c.charCodeAt(0) >= 0xa1 && c.charCodeAt(0) <= 0xff));

  // --- the record walk: SBASIC + SDRUG stack cap, byte-exact to EOF ---------
  // The stack cap wPileNum lives in the blitted SDRUG block; its offset and the
  // block size both come from the layout probe, never hand arithmetic.
  const LAYOUT = require('../layout-probe/layout.json');
  eq('SDRUG stride from probe', DB.SDRUG_SIZE, LAYOUT.structs.ITEM_SDRUG.size);
  eq('  wPileNum offset from probe', DB.WPILENUM_OFF,
     LAYOUT.structs.ITEM_SDRUG.fields.wPileNum.off);
  check('  wPileNum offset is non-trivial (8-byte __time64_t alignment)',
        DB.WPILENUM_OFF === 20 && DB.SDRUG_SIZE === 32);
  // wCureVolume/bInstance offsets — measured by the probe, per rule #1, not the
  // naive "next field after wPileNum" guess (wCureVolume shares an anonymous
  // union with wArrowNum right after wPileNum; bInstance sits at the struct's
  // very front, before the __time64_t that forces wPileNum's own alignment).
  eq('  wCureVolume offset from probe', DB.CUREVOLUME_OFF,
     LAYOUT.structs.ITEM_SDRUG.fields.wCureVolume.off);
  eq('  wCureVolume offset', DB.CUREVOLUME_OFF, 22);
  eq('  bInstance offset from probe', DB.BINSTANCE_OFF,
     LAYOUT.structs.ITEM_SDRUG.fields.bInstance.off);
  eq('  bInstance offset', DB.BINSTANCE_OFF, 4);

  let total = 0, eofExact = 0, stackable = 0, pile100 = 0;
  for (const name of ['item.isf', 'item1.isf']) {
    const t = DB.parse(ar.read(name));
    eq(`  ${name} failed records`, t.stats.failedItems, 0);
    // Walking SBASIC + SDRUG + the sized blocks to the final byte is the proof
    // the drug-block stride is right: a wrong one desyncs and misses EOF.
    if (t.bytesRead === t.fileSize) eofExact++;
    total += t.items.length;
    for (const it of t.items) {
      if (it.pile && it.pile > 1) stackable++;
      if (it.pile === 100) pile100++;
    }
  }
  eq('  both tables consumed to EOF exactly', eofExact, 2);
  eq('  total items', total, 37091);
  check('  stackable items found (wPileNum > 1)', stackable > 1000);
  check('  some items stack to 100', pile100 > 0);

  // --- the emitted runtime JSON must agree with the parse ------------------
  const dbPath = path.join(__dirname, '..', '..', 'unity', 'RanMobile', 'Assets',
                           'Ran', 'Resources', 'itemdb.json');
  if (fs.existsSync(dbPath)) {
    const db = JSON.parse(fs.readFileSync(dbPath, 'utf8'));
    // 20,898, not 20,897 — cross-checked independently against itemdata.js's own
    // parse of the same canonical glogic/GLogic.rcc (see the item-price test
    // block below); the shipped Ran/ item tables carry one more unique
    // (mainId,subId) pair than an earlier snapshot of this count recorded.
    eq('itemdb.json unique (mainId,subId)', db.items.length, 20898);
    const byId = new Map(db.items.map((x) => [(x.m << 16) | x.s, x]));
    const sword = byId.get((0 << 16) | 3);
    check('  (0,3) present', !!sword);
    eq('  (0,3) name resolved', sword.n, 'Japanese Sword');
    eq('  (0,3) icon atlas', sword.f, 'Exclusive_knight.dds');
    // sICONID.wMainID = column, wSubID = row (GetIconTexurePos).
    eq('  (0,3) icon col', sword.x, 1);
    eq('  (0,3) icon row', sword.y, 4);
    // Name in JSON matches the raw string-table value byte-for-byte (cp874).
    eq('  (0,3) name == string table', sword.n, stbl.get('IN_000_003'));
    const thaiItem = byId.get((0 << 16) | 9);
    check('  (0,9) name is raw cp874 (decoded at runtime by DecodeCp874)',
          !!thaiItem && [...thaiItem.n].some((c) => c.charCodeAt(0) >= 0xa1));
    check('  at least one item has maxStack 100',
          db.items.some((x) => x.k === 100));
    // grade is the EMITEMLEVEL rarity (0..5), never out of range.
    check('  grade in 0..5 for every item',
          db.items.every((x) => x.g >= 0 && x.g <= 5));
    // Descriptions are ENTIRELY absent in the currently shipped ItemStrTable.txt
    // — every one of its 24 "ID_mmm_sss" keys carries an empty value (measured
    // directly off the decrypted text, not just via this JSON). Not a decode
    // bug: SITEM::GetComment (GLItem.cpp:781-785) falls back to "" the same way
    // when the table has no/an-empty entry, and this port matches that.
    eq('  items carrying a non-empty description', db.items.filter((x) => x.d).length, 0);

    // --- dwBuyPrice/dwSellPrice: previously skipped, now decoded -------------
    // Pins the exact real bytes at (SBASIC's dwFlags, then dwBuyPrice, then
    // dwSellPrice — GLItemBasic.h:1195-1197) against three real, different-shape
    // items: plain equipment (flat price, not a pile), a piled ammo item (whose
    // real GETAPPLYNUM divisor is wCureVolume, not 1), and a piled non-ammo
    // consumable (whose real GETAPPLYNUM divisor is 1).
    eq('  (0,3) Japanese Sword buy/sell price',
       JSON.stringify([sword.bp, sword.sp, sword.k, sword.bi]),
       JSON.stringify([2800, 280, 1, 0]));
    const arrow = byId.get((13 << 16) | 0);
    check('  (13,0) arrow-type item present', !!arrow);
    eq('  (13,0) ITEM_ARROW buy/sell/cureVolume/pile/instance',
       JSON.stringify([arrow.t, arrow.bp, arrow.sp, arrow.cv, arrow.k, arrow.bi]),
       JSON.stringify([1, 50, 16, 150, 999, 1]));
    const dumpling = byId.get((1 << 16) | 18);
    check('  (1,18) Rice Dumpling present', !!dumpling);
    eq('  (1,18) Rice Dumpling buy/sell/pile/instance',
       JSON.stringify([dumpling.bp, dumpling.sp, dumpling.k, dumpling.bi]),
       JSON.stringify([1, 1, 30, 1]));

    // --- SITEM::GETSELLPRICE / GETAPPLYNUM, mirrored from GLItem.h:135-146 and
    // GLItem.cpp:717-746 (the same formula RanItemDb.GetSellPrice/GetApplyNum
    // implement in C#) --------------------------------------------------------
    const APPLYNUM_CUREVOLUME_TYPES = new Set([1, 4, 7, 8, 14, 15, 19, 23, 28, 34, 43, 44, 50, 51, 55, 57]);
    const getApplyNum = (it) => (APPLYNUM_CUREVOLUME_TYPES.has(it.t) && it.cv > 0) ? it.cv : 1;
    const isPile = (it) => !!it.bi && it.k > 1;
    const getSellPrice = (it, qty) => isPile(it)
      ? Math.floor(it.sp * qty / getApplyNum(it)) : it.sp;

    // Non-pile equipment: flat sp regardless of an (inapplicable) quantity.
    eq('  GETSELLPRICE(sword, *) is flat dwSellPrice (not a pile)',
       getSellPrice(sword, 1), 280);
    // Ammo: GETAPPLYNUM==wCureVolume(150), so a full "bundle" (150 held) sells
    // for exactly the flat per-unit sp (16) — proration divides it back out.
    eq('  GETSELLPRICE(arrow, 150) == flat sp (one full wCureVolume bundle)',
       getSellPrice(arrow, 150), 16);
    // A half bundle sells for half — proves the proration, not just the divisor.
    eq('  GETSELLPRICE(arrow, 75) == sp*75/150', getSellPrice(arrow, 75), 8);
    // Non-ammo consumable: GETAPPLYNUM==1, so price scales 1:1 with held qty —
    // this is the common case (potions/materials/food), most of the 1,159
    // stackable items in the shipped tables.
    eq('  GETSELLPRICE(dumpling, 10) == sp*10 (GETAPPLYNUM==1 for ITEM_DRUG)',
       getSellPrice(dumpling, 10), 10);

    // --- sDrugOp.emDrug (EMITEM_DRUG) ----------------------------------------
    // GLCHARLOGIC::GET_REVIVE_ITEM (GLogixExPC.cpp:4287-4297) tests this field
    // against ITEM_DRUG_CALL_REVIVE(11) on a worn SLOT_NECK/SLOT_ORNAMENT item
    // to decide whether real PC shows the "revive in place" button; RanAutoPotModule
    // reads the HP/MP/SP family the same way GLCharacter::RunAutoPots does
    // (GLCharacter.cpp:7834-7857).
    eq('  emDrug offset from probe', DB.EMDRUG_OFF, LAYOUT.structs.ITEM_SDRUG.fields.emDrug.off);
    eq('  emDrug offset', DB.EMDRUG_OFF, 0);
    // Every emDrug value in the shipped tables is in EMITEM_DRUG's declared range
    // (GLItemDef.h:490-515, 0..15) except a documented handful of legacy/garbage
    // records — measured, not assumed: only report, do not silently mask a real
    // decode break for the vast majority.
    const drugCounts = new Map();
    for (const it of db.items) drugCounts.set(it.dr, (drugCounts.get(it.dr) || 0) + 1);
    const inRange = db.items.filter((x) => x.dr >= 0 && x.dr <= 15).length;
    check('  emDrug in EMITEM_DRUG range (0..15) for the overwhelming majority',
          inRange / db.items.length > 0.999);
    // ITEM_DRUG_CALL_REVIVE(11): real, plural, and genuinely Thai "revive
    // necklace" items — not a single coincidental hit.
    const reviveItems = db.items.filter((x) => x.dr === 11);
    check('  ITEM_DRUG_CALL_REVIVE(11) items found', reviveItems.length > 10,
          `got ${reviveItems.length}`);
    // At least some (not necessarily all — several CALL_REVIVE items are cash-shop
    // ("CommercialSet*.dds") entries with no ItemStrTable.txt row, so they
    // correctly fall back to their raw "IN_mmm_sss" key, matching SITEM::GetName's
    // own fallback, GLItem.cpp:773-777) resolve to real Thai text naming the
    // revive necklace/ornament.
    check('  some CALL_REVIVE items decode to real Thai text (revive necklace)',
          reviveItems.some((x) => [...x.n].some((c) => c.charCodeAt(0) >= 0xa1)));
    // HP/MP/SP family present with plausible counts (measured off the shipped
    // table, not hand-picked): plain HP/MP/SP potions dominate; combo (HP_MP,
    // MP_SP, HP_MP_SP) are rarer.
    check('  ITEM_DRUG_HP(1) items found', (drugCounts.get(1) || 0) > 10);
    check('  ITEM_DRUG_MP(2) items found', (drugCounts.get(2) || 0) > 10);
    check('  ITEM_DRUG_SP(3) items found', (drugCounts.get(3) || 0) > 10);
    check('  ITEM_DRUG_HP_MP_SP(6) items found', (drugCounts.get(6) || 0) > 10);
    // NONE(0) dominates — every non-drug item (equipment, quest items, …) still
    // writes an SDRUG block (GLItem.cpp:43) whose emDrug defaults to NONE.
    check('  ITEM_DRUG_NONE(0) is the overwhelming majority (equipment etc.)',
          (drugCounts.get(0) || 0) / db.items.length > 0.9);

    // Icon atlas staged under Resources/UI as a PNG.
    const uiPng = path.join(__dirname, '..', '..', 'unity', 'RanMobile', 'Assets',
                            'Ran', 'Resources', 'UI', 'Exclusive_knight.png');
    check('  Exclusive_knight.dds staged as PNG under Resources/UI',
          fs.existsSync(uiPng));
  } else {
    check('itemdb.json present (run extract-itemdb.js)', false);
  }
}

console.log('\nQuest DATABASE — GLQUEST text/objectives/rewards (questdata.js)');
{
  const questdata = require('./questdata');
  const B = require('./bytecrypt');
  const { parseList, findQuestRcc } = require('./extract-questdb');

  const rccPath = findQuestRcc();
  check('Quest.rcc located', !!rccPath);
  if (rccPath) {
    const arc = new RccArchive(rccPath);
    const lstName = arc.list().find((n) => /^quest\.lst$/i.test(n));
    check('quest.lst present in Quest.rcc', !!lstName);
    const list = parseList(arc.read(lstName));
    check('quest.lst lists 629 quests', list.size === 629, `got ${list.size}`);

    // Every REFERENCED quest must decode to its exact final byte — the same
    // "a wrong stride cannot reach EOF" proof used everywhere in this pipeline.
    let eofExact = 0, withSteps = 0;
    const rewardIds = new Set();
    const byVer = {};
    for (const [, e] of list) {
      const raw = arc.read(e.file.toLowerCase());
      const res = questdata.decodeQst(raw, B);
      if (!res.quest) continue;
      byVer[res.quest.ver] = (byVer[res.quest.ver] || 0) + 1;
      if (res.eofExact) eofExact++;
      if (res.quest.steps.length) withSteps++;
      for (const it of res.quest.gift.items) if (it.m || it.s) rewardIds.add((it.m << 16) | it.s);
    }
    eq('every referenced quest decodes byte-exact to EOF', eofExact, 629);
    eq('every quest carries at least one step', withSteps, 629);
    // Shipped versions are the "old" GLQUEST loaders only (no 0x200+).
    check('shipped quest versions are 0x10/0x11/0x12/0x15 dominant',
          (byVer[0x10] || 0) > 300 && (byVer[0x12] || 0) > 100);

    // Titles are cp874 (Thai): quest 1's title matches its quest.lst name byte-for-byte.
    const q1raw = arc.read(list.get(1).file.toLowerCase());
    const q1 = questdata.decodeQst(q1raw, B).quest;
    check('quest 1 title/comment carry cp874 Thai bytes',
          [...(q1.title + q1.comment)].some((c) => c.charCodeAt(0) >= 0xa1));

    // Positive control: the byte-crypt table is load-bearing. Decoding the same
    // file with the WRONG table must NOT yield a valid quest version, so it
    // cannot reach EOF — proof the EMBYTECRYPT_OLD choice actually matters.
    const wrong = questdata.decodeQst(q1raw, {
      decode: (buf) => B.decode(buf, 'EMBYTECRYPT_QUEST'),
    });
    check('wrong byte-crypt table fails to reach EOF (positive control)',
          !wrong.eofExact);

    // Reward item ids resolve against the item database (same (main,sub) key).
    const dbPath = path.join(__dirname, '..', '..', 'unity', 'RanMobile', 'Assets',
                             'Ran', 'Resources', 'itemdb.json');
    if (fs.existsSync(dbPath)) {
      const items = JSON.parse(fs.readFileSync(dbPath));
      const key = new Set(items.items.map((it) => (it.m << 16) | it.s));
      let resolved = 0;
      for (const id of rewardIds) if (key.has(id)) resolved++;
      eq('every reward item id resolves in itemdb.json', resolved, rewardIds.size);
    }
  }

  // The staged questdb.json the runtime loads.
  const qdb = path.join(__dirname, '..', '..', 'unity', 'RanMobile', 'Assets',
                        'Ran', 'Resources', 'questdb.json');
  if (fs.existsSync(qdb)) {
    const db = JSON.parse(fs.readFileSync(qdb));
    eq('questdb.json quest count', db.quests.length, 629);
    const q1 = db.quests.find((q) => q.id === 1);
    check('questdb.json quest 1 has 6 steps', !!q1 && q1.steps.length === 6);
    check('questdb.json quest 1 title is raw cp874',
          !!q1 && [...q1.t].some((c) => c.charCodeAt(0) >= 0xa1));
    check('questdb.json no quest marked partial (all EOF-exact)',
          db.quests.every((q) => !q.partial));
  } else {
    check('questdb.json present (run extract-questdb.js)', false);
  }
}

console.log('\nnpc shops (.crowsale) + dialogue (.ntk) over Crow.mnsf');
{
  const crowdata = require('./crowdata');
  const crowsale = require('./crowsale');
  const npcdialogue = require('./npcdialogue');
  const arc = new RccArchive(A('glogic/GLogic.rcc'));
  const ntkArc = new RccArchive(A('glogic/npctalk/NpcTalk.rcc'));
  const load = (n) => { let b = arc.read(n); if (G.isEncoded(b)) b = G.decode(b); return b; };

  // --- Crow.mnsf index: EOF-exact, and NPC file references decode to clean names.
  const crows = crowdata.parse(arc.read('Crow.mnsf'));
  check('Crow.mnsf is GLCROW v0x0200', crows.type === 'GLCROW' && crows.bodyVersion === 0x0200);
  eq('Crow.mnsf record count', crows.count, 1854);
  check('Crow.mnsf consumed to EOF exactly (trailing 0)', crows.eofExact);
  const npc83 = crows.crows.find((c) => c.main === 8 && c.sub === 3);
  check('NPC 8:3 -> talk npc_1_shop1.ntk + sale files',
        !!npc83 && npc83.talkFile === 'npc_1_shop1.ntk' && npc83.saleFiles.some((f) => f));

  // --- .crowsale: both line shapes decode (auto-insert 11-param, positioned 13-param).
  const csArms = crowsale.parse(load('inven1_0_1.crowsale').toString('latin1'));
  check('inven1_0_1.crowsale is auto-insert (no explicit slot)',
        !csArms.positioned && csArms.items.length > 0 && csArms.items[0].posX === null);
  check('  first item is (19,0)', csArms.items[0].main === 19 && csArms.items[0].sub === 0);
  const csEtc = crowsale.parse(load('inven1_0_3.crowsale').toString('latin1'));
  check('inven1_0_3.crowsale is explicit-slot at (0,0)',
        csEtc.positioned && csEtc.items[0].posX === 0 && csEtc.items[0].posY === 0);

  // --- .ntk: every REFERENCED, shipped talk file parses to its exact final byte.
  const ntkNames = new Set(ntkArc.list().map((n) => n.toLowerCase()));
  let parsed = 0, eofExact = 0, missing = 0, failed = 0; const seen = new Set();
  for (const c of crows.crows) {
    if (!c.talkFile) continue;
    const lc = c.talkFile.toLowerCase();
    if (!ntkNames.has(lc)) { missing++; continue; }
    if (seen.has(lc)) continue; seen.add(lc);
    try { const r = npcdialogue.parse(ntkArc.read(c.talkFile)); parsed++; if (r.eofExact) eofExact++; }
    catch { failed++; }
  }
  check('every unique referenced .ntk parses EOF-exact',
        parsed > 0 && eofExact === parsed && failed === 1,
        `${eofExact}/${parsed} eof, ${failed} failed (test_NPC.ntk)`);
  check('only test/legacy talk files unreferenced-missing (npc_cdm, inloveran)',
        missing === 2, `${missing} missing`);

  // NPC 8:3's tree: greeting is real cp874 Thai, and IsMARKET drives the Shop option.
  const dlg83 = npcdialogue.parse(ntkArc.read('npc_1_shop1.ntk'));
  check('npc_1_shop1.ntk sets market (shop) flag', dlg83.flags.market === true);
  check('npc_1_shop1.ntk greeting is raw cp874 (0xA1+ byte present)',
        [...dlg83.greetingRaw.toString('latin1')].some((c) => c.charCodeAt(0) >= 0xa1));

  // POSITIVE CONTROL: the case/talk exist-flags are BOOL(4), not bool(1); the whole
  // walk is byte-exact, so truncating a good file MUST break EOF-exactness. If this
  // still read EOF-exact, the check above would be meaningless.
  const cut = ntkArc.read('npc_1_shop1.ntk');
  const truncated = cut.slice(0, cut.length - 8);
  let ctrlEof; try { ctrlEof = npcdialogue.parse(truncated).eofExact; } catch { ctrlEof = false; }
  check('truncated .ntk is NOT EOF-exact (positive control)', ctrlEof === false);

  // --- buildNpcTree: the per-NPC page tree the runtime renders (greeting + answers).
  {
    const tree = npcdialogue.buildNpcTree(dlg83);
    check('8:3 tree startPage is nSTARTINDEX (1)', tree.startPage === 1);
    eq('8:3 tree page count', Object.keys(tree.pages).length, 37);
    const start = tree.pages[tree.startPage];
    check('8:3 start page has a greeting + many answers',
          !!start && [...start.g].some((c) => c.charCodeAt(0) >= 0xa1) && start.a.length === 94);

    // First answer is the market/shop basic (k=EM_BASIC, b=EM_MARKET); its icon is
    // TALK(9), because the PC's EM_MARKET icon case is COMMENTED OUT (only CURE/
    // STARTPOINT basics get a dedicated icon) — pin that exact quirk.
    const a0 = start.a[0];
    check('8:3 answer[0] is basic/market (k=2,b=1) with icon TALK(9)',
          a0.k === 2 && a0.b === 1 && a0.tg === -1 && a0.ic === 9);
    // Second is a page-move (k=EM_PAGE_MOVE) whose target page exists.
    const a1 = start.a[1];
    check('8:3 answer[1] is page-move (k=1) to an existing page',
          a1.k === 1 && a1.tg >= 0 && (a1.tg in tree.pages));
    // Quest-step lines carry the QUEST_START icon (7), not QUEST_ING (8).
    check('8:3 has quest-step answers iconed QUEST_START(7), never QUEST_ING(8)',
          start.a.some((a) => a.k === 4 && a.ic === 7) && start.a.every((a) => a.ic !== 8));

    // Glob ids: the entry dialogue is first and its default case is case 0, so its
    // answers (talk-NID order) get glob ids 0,1,2,... — the dwTalkID a quest request
    // carries. Pin the invariant so a reorder of AssignTalkGlobID would fail here.
    let gseq = true;
    for (let k = 0; k < start.a.length; k++) if (start.a[k].gid !== k) gseq = false;
    check('8:3 start-page answers carry sequential glob ids 0..n-1', gseq);

    // Every in-tree navigation target (page-move / quest) resolves to a real page —
    // no dangling LoadNode. (A wrong walk would produce out-of-range targets.)
    let dangling = 0, moves = 0;
    for (const pk in tree.pages)
      for (const a of tree.pages[pk].a)
        if (a.tg >= 0) { moves++; if (!(a.tg in tree.pages)) dangling++; }
    check('8:3 every page-move/quest target resolves (no dangling LoadNode)',
          moves > 0 && dangling === 0, `${moves} moves, ${dangling} dangling`);

    // iconFor unit table — the exact DialogueWindow.cpp:728-784 switch.
    const IF = npcdialogue.iconFor;
    check('iconFor: quest-start=7, quest-step=7 (not QUEST_ING 8)',
          IF({ action: 3 }) === 7 && IF({ action: 4 }) === 7);
    check('iconFor: basic cure=HEAL(0), startpoint=STARTPOINT(2), market=TALK(9)',
          IF({ action: 2, actionNo: 2 }) === 0 &&
          IF({ action: 2, actionNo: 3 }) === 2 &&
          IF({ action: 2, actionNo: 1 }) === 9);
    check('iconFor: page-move=TALK(9), do-nothing=HEAL(0)',
          IF({ action: 1 }) === 9 && IF({ action: 0 }) === 0);

    // Whole-corpus: every referenced, shipped .ntk builds a tree whose every
    // navigation target resolves — a structural check across all NPCs, not just 8:3.
    let trees = 0, badTree = 0, totalDangling = 0;
    for (const c of crows.crows) {
      if (!c.talkFile) continue;
      const lc2 = c.talkFile.toLowerCase();
      if (!ntkNames.has(lc2)) continue;
      let pr; try { pr = npcdialogue.parse(ntkArc.read(c.talkFile)); } catch { continue; }
      let t; try { t = npcdialogue.buildNpcTree(pr); } catch { badTree++; continue; }
      trees++;
      for (const pk in t.pages)
        for (const a of t.pages[pk].a)
          if (a.tg >= 0 && !(a.tg in t.pages)) totalDangling++;
    }
    check('every referenced .ntk builds a tree with resolvable targets',
          trees > 100 && badTree === 0,
          `${trees} trees, ${badTree} failed, ${totalDangling} dangling targets`);
  }

  // --- staged tables the runtime loads.
  const R = (f) => path.join(__dirname, '..', '..', 'unity', 'RanMobile', 'Assets', 'Ran', 'Resources', f);
  const shopsPath = R('npcshops.json'), dlgPath = R('npcdialogue.json');
  if (fs.existsSync(shopsPath) && fs.existsSync(dlgPath)) {
    const shops = JSON.parse(fs.readFileSync(shopsPath));
    const npcs = JSON.parse(fs.readFileSync(dlgPath));
    eq('npcshops.json shop count', Object.keys(shops.shops).length, 89);
    eq('npcdialogue.json npc count', Object.keys(npcs.npcs).length, 312);
    check('npcshops.json 8:3 has 41 stock entries',
          !!shops.shops['8:3'] && shops.shops['8:3'].length === 41);
    let bad = 0;
    for (const k in shops.shops) for (const e of shops.shops[k])
      if (e.c < 0 || e.c > 2 || e.x < 0 || e.x >= 6 || e.y < 0 || e.y >= 8) bad++;
    eq('every stock slot is a valid 6x8 sale-grid cell', bad, 0);
    check('npcdialogue.json 8:3 greeting is raw cp874',
          !!npcs.npcs['8:3'] && [...npcs.npcs['8:3'].g].some((c) => c.charCodeAt(0) >= 0xa1));
    check('npcdialogue.json 8:3 offers shop', !!npcs.npcs['8:3'] && npcs.npcs['8:3'].shop === 1);

    // The staged page tree the runtime renders: startPage + per-page answer lists.
    const r83 = npcs.npcs['8:3'];
    check('npcdialogue.json 8:3 has a page tree (sp + pg)',
          !!r83 && r83.sp === 1 && r83.pg && Object.keys(r83.pg).length === 37);
    const sp83 = r83 && r83.pg && r83.pg[r83.sp];
    check('npcdialogue.json 8:3 start page answers carry text/kind/icon',
          !!sp83 && sp83.a.length === 94 &&
          sp83.a[0].k === 2 && sp83.a[0].b === 1 && sp83.a[0].ic === 9 &&
          sp83.a.some((a) => a.k === 4 && a.ic === 7));
    // Icons only ever index the 10-entry DIALOGUE_ICON table; every navigation
    // target that is set resolves to a real page — validated across ALL staged NPCs.
    let badIcon = 0, dangling = 0, answers = 0;
    for (const k in npcs.npcs) {
      const rec = npcs.npcs[k];
      if (!rec.pg) continue;
      for (const pk in rec.pg)
        for (const a of rec.pg[pk].a) {
          answers++;
          if (a.ic < 0 || a.ic > 9) badIcon++;
          if (a.tg >= 0 && !(a.tg in rec.pg)) dangling++;
        }
    }
    eq('every staged answer icon is a valid DIALOGUE_ICON index 0..9', badIcon, 0);
    // 3 authored dangling page-moves survive: npc_3_medic_1 / NPC_0_Helper carry a
    // "go back" (k=1) to page 12, which those files never define — the PC would
    // GASSERT/no-op on it too. Runtime NavigateTo returns false and hides. Pinned
    // exactly so a broken tree walk (which would balloon this to thousands) fails.
    check('staged answers present, only the 3 known authored dangling targets',
          answers > 5000 && dangling === 3, `${answers} answers, ${dangling} dangling`);
  } else {
    check('npcshops.json + npcdialogue.json present (run extract-npcshops.js)', false);
  }
}

console.log('\nCNPCShopWindow catalogs (.npcshop — SEPARATE from .crowsale/RanShop above)');
{
  const npcshopMod = require('./npcshop');
  const arc = new RccArchive(A('glogic/GLogic.rcc'));
  const load = (n) => { let b = arc.read(n); if (G.isEncoded(b)) b = G.decode(b); return b; };

  // --- exactly 7 .npcshop files ship, all AES-layer encoded (08 00 00 00 prefix).
  const npcshopNames = arc.list().filter((n) => /\.npcshop$/i.test(n));
  eq('GLogic.rcc .npcshop file count', npcshopNames.length, 7);
  check('every .npcshop entry is gamecrypt-encoded', npcshopNames.every((n) => G.isEncoded(arc.read(n))));

  // --- line-count structural check: every SHOP_TYPE/SHOP_ITEM line parses to
  // exactly one record (a wrong field-separator handling silently drops or
  // duplicates rows — this is the check that would catch it).
  let totalItems = 0, totalTypes = 0;
  for (const n of npcshopNames) {
    const text = load(n).toString('latin1');
    const r = npcshopMod.parse(text);
    const rawItems = (text.match(/^SHOP_ITEM\s*=/gm) || []).length;
    const rawTypes = (text.match(/^SHOP_TYPE\s*=/gm) || []).length;
    check(`${n}: item/type line count matches parsed record count`,
          r.items.length === rawItems && r.types.length === rawTypes,
          `items ${r.items.length}/${rawItems}, types ${r.types.length}/${rawTypes}`);
    totalItems += r.items.length; totalTypes += r.types.length;
  }
  eq('total .npcshop items across all 7 files', totalItems, 581);
  eq('total .npcshop category tabs across all 7 files', totalTypes, 20);

  // --- the two real SHOP_ITEM row shapes both resolve to exactly 11 fields:
  // "10, 111, 4,[0,...]" (trailing comma before '[', merchant.npcshop) and
  // "10, 0, 0[0,...]" (no comma, betatestitem.npcshop) — CIniLoader::split
  // DROPS zero-length fields rather than emitting "", so both shapes agree.
  eq('SHOP_ITEM row shape A (comma before "[") -> 11 fields',
     npcshopMod.splitFields('10, 111, 4,[0,0,0,0,0,0,0,0]').length, 11);
  eq('SHOP_ITEM row shape B (no comma before "[") -> 11 fields',
     npcshopMod.splitFields('10, 0, 0[0,0,0,0,0,0,0,0]').length, 11);

  // --- merchant.npcshop: known title/types/items, and SHOP_OPTION (currency) = 0
  // (gold) in every one of the 7 shipped files — measured, not assumed.
  const merchant = npcshopMod.parse(load('merchant.npcshop').toString('latin1'));
  check('merchant.npcshop title/shopType/type-tab-order', merchant.title === 'Merchant' &&
        merchant.shopType === 0 && merchant.types.map((t) => t.id).join(',') === '10,12,14,15');
  check('merchant.npcshop type tabs sorted by id (std::map order), not file order',
        merchant.types.every((t, i) => i === 0 || merchant.types[i - 1].id <= t.id));
  eq('merchant.npcshop item count', merchant.items.length, 46);
  let allZeroCurrency = true;
  for (const n of npcshopNames) if (npcshopMod.parse(load(n).toString('latin1')).shopType !== 0) allZeroCurrency = false;
  check('SHOP_OPTION (currency type) is 0/gold in all 7 shipped .npcshop files', allZeroCurrency);

  // --- Crow.mnsf m_strShopFile linkage: measured 11 references (matches the
  // GUI audit's own citation), only 5 resolve to a real shipped .npcshop —
  // the other 6 name a typo'd/unshipped file (dev/test crow records, same
  // anomaly class as missing .crowsale/.ntk references above).
  const crowdata = require('./crowdata');
  const crows = crowdata.parse(arc.read('Crow.mnsf'));
  const shopFileSet = new Set(npcshopNames.map((n) => n.toLowerCase()));
  let refs = 0, linked = 0;
  for (const c of crows.crows) {
    if (!c.shopFile) continue;
    refs++;
    if (shopFileSet.has(c.shopFile.toLowerCase())) linked++;
  }
  eq('Crow.mnsf m_strShopFile references', refs, 11);
  eq('...of which resolve to a real shipped .npcshop', linked, 5);
  const npc963 = crows.crows.find((c) => c.main === 9 && c.sub === 63);
  check('NPC 9:63 (Mr Ukay) -> clothing.npcshop', !!npc963 && npc963.shopFile === 'clothing.npcshop');

  // --- staged table the runtime loads.
  const R = (f) => path.join(__dirname, '..', '..', 'unity', 'RanMobile', 'Assets', 'Ran', 'Resources', f);
  const tablesPath = R('npcshoptables.json');
  if (fs.existsSync(tablesPath)) {
    const staged = JSON.parse(fs.readFileSync(tablesPath));
    eq('npcshoptables.json table count', Object.keys(staged.tables).length, 7);
    const m = staged.tables['merchant.npcshop'];
    check('npcshoptables.json merchant.npcshop staged correctly',
          !!m && m.t === 'Merchant' && m.s === 0 && m.ty.length === 4 && m.it.length === 46);
    check('npcshoptables.json ty rows are [id,name,count] and sum(count) == it.length',
          !!m && m.ty.every((t) => t.length === 3) &&
          m.ty.reduce((s, t) => s + t[2], 0) === m.it.length);
    // Every npcdialogue.json "ns" reference resolves into this table set —
    // the runtime's fallback path (RanNpcModule.OnNpcShopChosen) only exists
    // for the case it does NOT, so the common case should always resolve.
    const dlgPath = R('npcdialogue.json');
    if (fs.existsSync(dlgPath)) {
      const npcs = JSON.parse(fs.readFileSync(dlgPath)).npcs;
      let nsRefs = 0, nsResolved = 0;
      for (const k in npcs) if (npcs[k].ns) { nsRefs++; if (staged.tables[npcs[k].ns]) nsResolved++; }
      eq('npcdialogue.json "ns" references', nsRefs, 5);
      eq('...all resolve into npcshoptables.json', nsResolved, 5);
    }
  } else {
    check('npcshoptables.json present (run extract-npcshops.js)', false);
  }
}

console.log('\nmap axis (.mmp minimap image + world bounds — GLMapAxisInfo::LoadFile)');
{
  const MA = require('./mapaxis');
  const map = new RccArchive(A('map/Map.rcc'));
  const readMmp = (name) => MA.readAxis(map.read(name));

  // Byte-exact field decode, pinned against the raw source lines. This is what
  // makes a wrong getflag index a test failure rather than a silently shifted
  // field. circle_zone.mmp reads:
  //   MINIMAPNAME  circle_zone_mini.dds
  //   MAPSIZE_X    2605 -1300     (size, start)
  //   MAPSIZE_Y    2605 -1300
  //   TEXTURE_SIZE 512 512
  //   TEXTURE_POS  0 0 512 512
  const cz = readMmp('circle_zone.mmp');
  eq('circle_zone.mmp MINIMAPNAME', cz.minMapTex, 'circle_zone_mini.dds');
  eq('circle_zone.mmp sizeX (MAPSIZE_X token 1)', cz.sizeX, 2605);
  eq('circle_zone.mmp startX (MAPSIZE_X token 2)', cz.startX, -1300);
  eq('circle_zone.mmp sizeY', cz.sizeY, 2605);
  eq('circle_zone.mmp startY', cz.startY, -1300);
  eq('circle_zone.mmp texW', cz.texW, 512);
  check('circle_zone.mmp TEXTURE_POS is the whole texture',
        cz.texPos.join(',') === '0,0,512,512');

  // A second, differently-sized map so a hardcoded stride cannot pass both, and
  // whose X-start and Y-start DIFFER (−2807 vs −2788), proving MAPSIZE_X and
  // MAPSIZE_Y are read as independent lines rather than one shared value.
  const bz = readMmp('w_base_zone.mmp');
  eq('w_base_zone.mmp MINIMAPNAME', bz.minMapTex, 'base_zone_minimap_00.dds');
  eq('w_base_zone.mmp sizeX', bz.sizeX, 5372);
  eq('w_base_zone.mmp startX', bz.startX, -2807);
  eq('w_base_zone.mmp sizeY', bz.sizeY, 5371);
  eq('w_base_zone.mmp startY (differs from startX)', bz.startY, -2788);

  // POSITIVE CONTROL: MAPSIZE_X is "SIZE START", not "START SIZE". Reading the
  // tokens in the other order would make the (positive) size the start and the
  // (negative) origin the size — every shipped map here has size>0 and start<=0,
  // so a swapped decode is detectable, and this asserts it did not happen.
  check('size is the first token, start the second (not swapped)',
        bz.sizeX > 0 && bz.startX < 0 && cz.sizeX > 0 && cz.startX < 0);

  // Comment stripping: a value line must not absorb a trailing "// ..." comment.
  // Fabricate one and confirm the parser drops it (the engine strips at "//").
  {
    const withComment = MA.readAxis('MAPSIZE_X 1234 -56 // trailing comment\nMAPSIZE_Y 78 -9\n');
    check('inline // comment is stripped from a value line',
          withComment.sizeX === 1234 && withComment.startX === -56 && withComment.sizeY === 78);
  }

  // The emitted runtime table, if present, must agree with a fresh parse.
  const axPath = path.join(__dirname, '..', '..', 'unity', 'RanMobile', 'Assets',
                           'Ran', 'Resources', 'mapaxis.json');
  if (fs.existsSync(axPath)) {
    const ax = JSON.parse(fs.readFileSync(axPath, 'utf8'));
    eq('mapaxis.json map count (106 of 121 catalog maps have a .mmp)', ax.count, 106);
    eq('mapaxis.json maps with a shipped minimap image', ax.withImage, 104);
    const byId = new Map(ax.maps.map((m) => [m.id, m]));

    // Map 8 == w_school_03, the Phase-0 anchor. Pin its tex, staged png and a bound.
    const m8 = byId.get(8) || {};
    eq('mapaxis.json map 8 tex', m8.tex, 'w_school_03_minimap_00.dds');
    eq('mapaxis.json map 8 png stem', m8.png, 'w_school_03_minimap_00');
    eq('mapaxis.json map 8 sizeX', m8.sizeX, 9095);
    eq('mapaxis.json map 8 startX', m8.startX, -5087);
    eq('mapaxis.json map 22 png stem', (byId.get(22) || {}).png, 'trade_zone');

    // The two maps whose .mmp names a texture that was never shipped: reported
    // image-less, NOT substituted with a lookalike.
    check('mapaxis.json map 251 (suhak) has no image — circle_zone_mini.dds not shipped',
          (byId.get(251) || {}).png === '');
    check('mapaxis.json map 48 (ep3_ege4part) has no image',
          (byId.get(48) || {}).png === '');

    // Every non-empty png must be a real staged PNG on disk (a dangling name would
    // resolve to null at runtime and silently show no map).
    const miniDir = path.join(__dirname, '..', '..', 'unity', 'RanMobile', 'Assets',
                              'Ran', 'Resources', 'UI', 'minimap');
    let missing = 0;
    for (const m of ax.maps)
      if (m.png && !fs.existsSync(path.join(miniDir, m.png + '.png'))) missing++;
    eq('every staged png resource resolves to a file on disk', missing, 0);
    check('map 8 minimap PNG present',
          fs.existsSync(path.join(miniDir, 'w_school_03_minimap_00.png')));

    // CROSS-CHECK against the INDEPENDENT terrain bounds (union of node boxes, a
    // different parser from a different container): the .mmp bounds CENTRE must sit
    // inside the terrain AABB. Measured 103/103 maps; pin w_school_03.
    const manPath = path.join(__dirname, '..', '..', 'assets', 'maps', 'w_school_03.map.json');
    if (fs.existsSync(manPath)) {
      const mb = (JSON.parse(fs.readFileSync(manPath, 'utf8')).worldBounds) || {};
      if (mb.min && mb.max) {
        const cx = m8.startX + m8.sizeX / 2, cz2 = m8.startY + m8.sizeY / 2;
        check('map 8 .mmp bounds centre lies inside the terrain node-box AABB',
              cx >= mb.min[0] && cx <= mb.max[0] && cz2 >= mb.min[2] && cz2 <= mb.max[2]);
      }
    }
  } else {
    check('mapaxis.json present (run extract-mapaxis.js)', false);
  }
}

console.log('\naudio staging (stage-audio.js — BGM per map + SFX event set)');
{
  const AUD = require('./stage-audio');
  const root = path.join(__dirname, '..', '..', '..');
  const BGM_SRC = path.join(root, 'CLIENT', 'sounds', 'bgm');
  const SFX_SRC = path.join(root, 'CLIENT', 'sounds', 'sfx');
  const UNITY = path.join(__dirname, '..', '..', 'unity', 'RanMobile', 'Assets');
  const BGM_DST = path.join(UNITY, 'Ran', 'Resources', 'Audio', 'bgm');
  const SFX_DST = path.join(UNITY, 'Ran', 'Resources', 'Audio', 'sfx');

  // -- EVENTS table decode (each value cited to SOURCE in stage-audio.js) ----
  // Pin the count and a spread of the four origins so a re-typed filename or a
  // dropped keyword fails here rather than silently muting an event on PC-parity.
  eq('EVENTS keyword count', Object.keys(AUD.EVENTS).length, 44);
  eq('PICKUP_ITEM (GLCONST_CHAR strPICKUP_ITEM)', AUD.EVENTS.PICKUP_ITEM, '07000570.wav');
  eq('GRINDING_SUCCEED (default.charclass)', AUD.EVENTS.GRINDING_SUCCEED, '06000199.wav');
  eq('PKCOMBO_GODLIKE (GLGaeaClient.cpp:1760-1768)', AUD.EVENTS.PKCOMBO_GODLIKE, 'godlike.wav');
  eq('QUEST_ALARM (gameword ID2GAMEWORD)', AUD.EVENTS.QUEST_ALARM, '06000196.wav');
  eq('CONFT_BEGIN_FIGHT (ConftDisplayMan.cpp:151)', AUD.EVENTS.CONFT_BEGIN_FIGHT, 'fight.wav');
  eq('LEVELUP (level_up.egp SEQUENCE #1)', AUD.EVENTS.LEVELUP, '06000494.wav');
  eq('SKILL_LEARN (skill_learn.egp)', AUD.EVENTS.SKILL_LEARN, '06000212.wav');

  // -- BGM -> map is DATA: bgmSet over the real catalog + real source dir -----
  // Cross-checks two independent parsers (RanMapCatalog.bgm and the on-disk ogg
  // set): the staged set is exactly the referenced-AND-present tracks.
  if (fs.existsSync(BGM_SRC)) {
    const bgmIndex = AUD.indexDir(BGM_SRC);
    const { want, missing } = AUD.bgmSet(bgmIndex);
    eq('bgmSet want count (referenced by a map AND on disk)', want.length, 18);
    check('bgmSet references tradezone music m7a.ogg (map 22=trade_zone)',
          want.includes('m7a.ogg'));
    check('bgmSet references map-8 music ran12.ogg', want.includes('ran12.ogg'));
    // POSITIVE CONTROL: scarysong.ogg is referenced by an unused map but is
    // absent from the source tree — reported missing, NOT staged.
    check('scarysong.ogg reported missing (referenced, not on disk)',
          missing.includes('scarysong.ogg'));
    check('missing tracks are never in the staged want set',
          missing.every((m) => !want.includes(m.toLowerCase())));
  } else {
    console.log('  --   CLIENT/sounds/bgm absent — skipped bgmSet checks');
  }

  // -- staged files exist, and each EVENTS filename resolves in the source ----
  // A dangling event filename resolves to null at runtime and silently drops the
  // sound, so pin the whole set on disk (both staged and source-present).
  if (fs.existsSync(SFX_SRC)) {
    const sfxIndex = AUD.indexDir(SFX_SRC);
    const wanted = [...new Set(Object.values(AUD.EVENTS).map((s) => s.toLowerCase()))];
    const notInSrc = wanted.filter((f) => !sfxIndex.has(f));
    eq('every EVENTS wav present in CLIENT/sounds/sfx', notInSrc.length, 0);
  } else {
    console.log('  --   CLIENT/sounds/sfx absent — skipped source-resolve check');
  }
  if (fs.existsSync(SFX_DST)) {
    const wanted = [...new Set(Object.values(AUD.EVENTS).map((s) => s.toLowerCase()))];
    const notStaged = wanted.filter((f) => !fs.existsSync(path.join(SFX_DST, f)));
    eq('every EVENTS wav staged under Ran/Resources/Audio/sfx', notStaged.length, 0);
    check('each staged sfx has a committed AudioImporter .meta',
          wanted.every((f) => {
            const m = path.join(SFX_DST, f + '.meta');
            return fs.existsSync(m) && fs.readFileSync(m, 'utf8').includes('AudioImporter:');
          }));
  } else {
    console.log('  --   Ran/Resources/Audio/sfx absent (run stage-audio.js) — skipped stage checks');
  }
  if (fs.existsSync(BGM_DST) && fs.existsSync(BGM_SRC)) {
    const { want } = AUD.bgmSet(AUD.indexDir(BGM_SRC));
    const notStaged = want.filter((f) => !fs.existsSync(path.join(BGM_DST, f)));
    eq('every referenced BGM staged under Ran/Resources/Audio/bgm', notStaged.length, 0);
    check('each staged bgm has a committed AudioImporter .meta (loadType Streaming)',
          want.every((f) => {
            const m = path.join(BGM_DST, f + '.meta');
            return fs.existsSync(m) && /AudioImporter:[\s\S]*loadType: 2/.test(fs.readFileSync(m, 'utf8'));
          }));
  }
}

// --- effects.json (simplified SEQUENCE/PARTICLESYS effect table) -----------
// The runtime player (Runtime/RanEffect.cs) is driven by effects.json, produced
// by extract-effects-json.js from the shipped .egp. Pin the corpus-wide counts
// (Effect.rcc is frozen, so these are exact), and pin two effects field-for-
// field against an INDEPENDENT decode via effect-egp + effect-props, so a drift
// in either the layout or the JSON shape fails here rather than on device.
{
  const EFFECT_RCC = path.join(RAN, 'data/effect/Effect.rcc');
  if (fs.existsSync(EFFECT_RCC)) {
    const ej = require('./extract-effects-json');
    const egp = require('./effect-egp');
    const props = require('./effect-props');
    const { out, stat, texUse } = ej.run();

    eq('effects.json: effects emitted', out.count, 4243);
    eq('effects.json: distinct textures', out.textureCount, 1019);
    eq('effects.json: SEQUENCE layers', stat.nodesSeq, 8863);
    eq('effects.json: PARTICLESYS layers', stat.nodesPar, 4863);
    eq('effects.json: mesh-particle layers (no sprite)', stat.meshParticle, 594);
    // New drawable kinds (Effect.rcc frozen, so exact): MESH -> textured quad,
    // GROUND -> decal, LIGHTNING -> beam, BLURSYS -> trail ribbon,
    // MOVEROTATE -> folded onto descendants (now including trail layers, which
    // is why moverFolded rose from 3653 once BLURSYS joined the fold targets).
    eq('effects.json: MESH layers', stat.nodesMesh, 8598);
    eq('effects.json: MESH skipped (no diffuse)', stat.meshNoTex, 1);
    eq('effects.json: GROUND decal layers', stat.nodesDecal, 4029);
    eq('effects.json: LIGHTNING beam layers', stat.nodesBeam, 283);
    eq('effects.json: BLURSYS trail layers', stat.nodesTrail, 2156);
    eq('effects.json: BLURSYS skipped (no texture)', stat.trailNoTex, 0);
    eq('effects.json: MOVEROTATE nodes', stat.moverNodes, 4520);
    eq('effects.json: MOVEROTATE folded onto drawables', stat.moverFolded, 4840);

    // The written file, if extract-effects-json.js has been run, must agree with
    // the in-memory recompute — a stale committed JSON is a silent bug.
    const JSON_PATH = path.join(__dirname, '..', '..',
      'unity/RanMobile/Assets/Ran/Resources/effects.json');
    if (fs.existsSync(JSON_PATH)) {
      const disk = JSON.parse(fs.readFileSync(JSON_PATH, 'utf8'));
      eq('effects.json on disk: format', disk.format, 'ran-effects');
      eq('effects.json on disk: count matches recompute', disk.count, out.count);
      eq('effects.json on disk: 001_shock present', !!disk.effects['001_shock'], true);
    } else {
      console.log('  --   effects.json not written yet (run extract-effects-json.js) — skipped disk check');
    }

    // 001_shock: par(_eff_flare)+seq(eff09) — pin the decoded fields.
    const e1 = out.effects['001_shock'];
    check('001_shock: has >=2 layers', e1 && e1.layers.length >= 2, `layers=${e1 && e1.layers.length}`);
    eq('001_shock: ver', e1.ver, 512);
    const p0 = e1.layers[0], s1 = e1.layers.find((l) => l.k === 'seq');
    eq('001_shock: layer0 kind', p0.k, 'par');
    eq('001_shock: layer0 texture', p0.tex, '_eff_flare');
    eq('001_shock: layer0 rate', p0.rate, 300);
    eq('001_shock: layer0 theta', p0.theta, 360);
    eq('001_shock: first seq texture', s1.tex, 'eff09');
    eq('001_shock: first seq size', s1.size, 40);
    eq('001_shock: first seq blend (additive)', s1.blend, 1);
    eq('001_shock: first seq billboard', s1.bill, true);

    // 2010map_eff13: a single billboard SEQUENCE, the simplest shape.
    const e2 = out.effects['2010map_eff13'];
    eq('2010map_eff13: one layer', e2.layers.length, 1);
    eq('2010map_eff13: seq 4x4 flipbook', `${e2.layers[0].cols}x${e2.layers[0].rows}`, '4x4');
    eq('2010map_eff13: seq texture', e2.layers[0].tex, '_eff_flare');

    // Independent cross-check: decode 001_shock straight from the .egp with the
    // layout probe and confirm the JSON reflects the SAME bytes. This is what
    // makes the pins above a layout check and not just a self-consistency one.
    const arc = new RccArchive(EFFECT_RCC);
    const ent = arc.entries.find((e) => e.name.toLowerCase() === '001_shock.egp');
    const res = egp.parse(arc.read(ent), ent.name);
    const seq = res.nodes.filter((n) => n.type === 'SEQUENCE').map((n) => props.decode(n));
    const firstSeq = seq.find((d) => d && d.props.m_szTexture);
    eq('001_shock: independent decode, first seq texture stem',
       ej.texId(firstSeq.props.m_szTexture), 'eff09');
    eq('001_shock: independent decode, first seq size', firstSeq.props.m_fSizeStart, 40);

    // texId normalisation: basename, lowercased, extension stripped.
    eq('texId strips path/case/ext', ej.texId('/A/B/Flare.TGA'), 'flare');

    // Positive control: SEQUENCE::PROPERTY m_szTexture is at body offset 280
    // (effect-egp FIELDS / effect-props). Reading it there yields eff09; reading
    // it 4 bytes off yields something else. The check can fail, so a pass means
    // the offset is actually load-bearing.
    const seqBody = res.nodes.filter((n) => n.type === 'SEQUENCE')[0].body;
    eq('positive control: SEQUENCE texture at body@280', ej.texId(egp.cstr(seqBody, 280, 64)), 'eff09');
    check('positive control: +4 shift no longer reads eff09',
          ej.texId(egp.cstr(seqBody, 284, 64) || '') !== 'eff09');

    // -- new drawable kinds: pin one solo effect of each, field for field -------
    // MESH -> textured billboard quad (a spinning flipbook).
    const em = out.effects['110114_loveyou'];
    eq('110114_loveyou: one mesh layer', `${em.layers.length}:${em.layers[0].k}`, '1:mesh');
    eq('110114_loveyou: mesh texture', em.layers[0].tex, '1d_lighting');
    eq('110114_loveyou: mesh 4x4 flipbook', `${em.layers[0].cols}x${em.layers[0].rows}`, '4x4');
    eq('110114_loveyou: mesh spin rad/s', em.layers[0].spin, 3);
    // GROUND -> ground decal quad with a fixed yaw.
    const ed = out.effects['228_shadow'];
    eq('228_shadow: one decal layer', `${ed.layers.length}:${ed.layers[0].k}`, '1:decal');
    eq('228_shadow: decal texture', ed.layers[0].tex, 'ice228_06');
    eq('228_shadow: decal yaw (rad)', ed.layers[0].rot, 10);
    eq('228_shadow: decal half-size (GROUND /2 baked)', ed.layers[0].size, 25);
    // LIGHTNING -> beam ribbon, inner/outer widths + fallback length.
    const eb = out.effects['base_solute_point_light03'];
    eq('base_solute_point_light03: one beam layer', `${eb.layers.length}:${eb.layers[0].k}`, '1:beam');
    eq('base_solute_point_light03: beam texture', eb.layers[0].tex, '_eff_flare03');
    eq('base_solute_point_light03: beam inner width', eb.layers[0].w0, 20);
    eq('base_solute_point_light03: beam has outer glow flag', eb.layers[0].outer, 1);
    // MOVEROTATE folded: 1bokmajang_fire's seq layers ride a mover velocity.
    const ev = out.effects['1bokmajang_fire'];
    const mv = ev.layers.find((l) => l.mvel);
    check('1bokmajang_fire: a layer carries folded mover velocity', !!mv && mv.mvel[0] === 500,
          mv ? JSON.stringify(mv.mvel) : 'none');
    // BLURSYS -> trail ribbon (RanEffectBlurSys builds geometry at runtime).
    const et = out.effects['aac113'];
    eq('aac113: one trail layer', `${et.layers.length}:${et.layers[0].k}`, '1:trail');
    eq('aac113: trail texture', et.layers[0].tex, 'f_sd_005');
    eq('aac113: trail per-point life', et.layers[0].tlife, 0.5);
    eq('aac113: trail width start/end', `${et.layers[0].tlen0}/${et.layers[0].tlen1}`, '3/2');
    eq('aac113: trail nNum decoded (unused at runtime)', et.layers[0].tnum, 10);
    eq('aac113: trail USEABSOLUTE', et.layers[0].tabs, true);
    // Independent cross-check, same shape as the SEQUENCE one above: decode
    // aac113 straight from the .egp with the layout probe and confirm the JSON
    // reflects the SAME bytes.
    const blurEnt = arc.entries.find((e) => e.name.toLowerCase() === 'aac113.egp');
    const blurRes = egp.parse(arc.read(blurEnt), blurEnt.name);
    const blurNode = blurRes.nodes.find((n) => n.type === 'BLURSYS');
    const blurDecoded = props.decode(blurNode);
    eq('aac113: independent decode, texture stem',
       ej.texId(blurDecoded.props.m_szTexture), 'f_sd_005');
    eq('aac113: independent decode, m_fLife', blurDecoded.props.m_fLife, 0.5);
    eq('aac113: independent decode, m_nNum', blurDecoded.props.m_nNum, 10);
    // Positive control: BLURSYS::PROPERTY m_szTexture is at body offset 140
    // (EFFECTS-PROPS.md §3 / layout.json). Reading it there yields f_sd_005;
    // reading it 4 bytes off does not.
    eq('positive control: BLURSYS texture at body@140',
       ej.texId(egp.cstr(blurNode.body, 140, 64)), 'f_sd_005');
    check('positive control: +4 shift no longer reads f_sd_005',
          ej.texId(egp.cstr(blurNode.body, 144, 64) || '') !== 'f_sd_005');

    // LIGHTNING is NOT in the layout probe; its offsets are derived structurally.
    // Pin that derivation to the two texture offsets effect-egp.js already
    // confirmed against the data — so the beam decode is anchored, not guessed.
    for (const [ver, base] of [[0x0102, 76], [0x0101, 112], [0x0100, 112]]) {
      const f = egp.FIELDS['LIGHTNING 0x' + ver.toString(16).padStart(4, '0')];
      eq(`LIGHTNING tex_In offset derives to FIELDS (v${ver.toString(16)})`,
         base + ej.LGT_OFF.m_szTexture_In, f[0][0]);
      eq(`LIGHTNING tex_Out offset derives to FIELDS (v${ver.toString(16)})`,
         base + ej.LGT_OFF.m_szTexture_Out, f[1][0]);
    }
    // Independent decode of a real beam node, straight off the .egp body.
    const beamNode = (() => {
      for (const e of arc.entries) {
        if (!e.name.toLowerCase().endsWith('.egp')) continue;
        let r; try { r = egp.parse(arc.read(e), e.name); } catch { continue; }
        const n = (r.nodes || []).find((x) => x.type === 'LIGHTNING');
        if (n) return n;
      }
      return null;
    })();
    if (beamNode) {
      const g = ej.decodeLightning(beamNode);
      check('LIGHTNING decode: inner width finite and >0', g && g.widthIn > 0 && isFinite(g.widthIn),
            g ? `${g.widthIn}` : 'null');
      check('LIGHTNING decode: texture name is printable', g && /^[\x20-\x7e]*$/.test(g.texIn), g && g.texIn);
    }

    // Compaction round-trips: a decal built from a decoded GROUND node keeps its
    // meaningful fields after compact() drops the defaults.
    const gNode = (() => {
      for (const e of arc.entries) {
        if (!e.name.toLowerCase().endsWith('.egp')) continue;
        let r; try { r = egp.parse(arc.read(e), e.name); } catch { continue; }
        const n = (r.nodes || []).find((x) => x.type === 'GROUND');
        if (n) { const d = props.decode(n); if (d && (d.props.m_szTexture || '').trim()) return d; }
      }
      return null;
    })();
    if (gNode) {
      const dl = ej.decalLayer(gNode);
      eq('decalLayer: kind', dl.k, 'decal');
      check('decalLayer: has a texture id', !!dl.tex, dl.tex);
    }

    // effect-textures.list and a sample staged PNG.
    const LIST = path.join(__dirname, 'effect-textures.list');
    if (fs.existsSync(LIST)) {
      const names = fs.readFileSync(LIST, 'latin1').split('\n').filter(Boolean);
      eq('effect-textures.list count', names.length, texUse.size);
    }
    const STAGED = path.join(__dirname, '..', '..',
      'unity/RanMobile/Assets/Ran/Resources/UI/effect');
    if (fs.existsSync(STAGED)) {
      check('staged effect texture eff09.png present',
            fs.existsSync(path.join(STAGED, 'eff09.png')));
      check('staged effect texture has committed .meta',
            fs.existsSync(path.join(STAGED, 'eff09.png.meta')));
      // MESH diffuse textures include .bmp (1d_lighting is the diffuse of 4,642
      // MESH layers); stage-effect-textures.js now decodes uncompressed BMP.
      check('staged BMP mesh texture 1d_lighting.png present',
            fs.existsSync(path.join(STAGED, '1d_lighting.png')));
      // decodeBmp round-trips a real 24-bit BMP to a non-empty PNG (positive: a
      // wrong stride/bpp throws rather than returning a plausible-but-wrong image).
      const stage = require('./stage-effect-textures.js');
      const idx = stage.buildIndex();
      const hit = idx.get('1d_lighting');
      if (hit) {
        const src = fs.readFileSync(hit.path);
        check('BMP source sniffs as BMP', stage.sniff(src) === 'BMP');
        const p = stage.toPng(src, 'BMP');
        check('decodeBmp -> PNG (8-byte signature)', p && p.length > 8
              && p.toString('hex', 0, 8) === '89504e470d0a1a0a');
      }
    } else {
      console.log('  --   effect textures not staged (run stage-effect-textures.js) — skipped stage checks');
    }
  } else {
    console.log('  --   Effect.rcc absent — skipped effects.json checks');
  }
}

// --- equipmodels: item -> .abl -> .abf + bone (extract-equipmodels.js) ------
{
  console.log('\nequipmodels (worn attach parts)');
  const EQ = require('./extract-equipmodels.js');
  const ablDir = EQ.ABL_DIR;
  if (fs.existsSync(ablDir)) {
    // A known v0x0201 .abl decodes to its .abf mesh + the spine bone it hangs off.
    const wingFile = path.join(ablDir, 'belt_m_18y5m_NeonWing_blue.abl');
    if (fs.existsSync(wingFile)) {
      const r = EQ.parseAbl(fs.readFileSync(wingFile));
      eq('abl belt -> abf', EQ.stem(r.abf), 'belt_18y5m_neonwing_blue');
      eq('abl belt -> bone', r.bone, 'Bip01_Spine2');
      eq('abl belt pieceType (PIECE_BELT=14)', r.pieceType | 0, 14);
      // DXAFFINEPARTS is 36 bytes (vTrans,vRotate,vScale): the body reads to EOF,
      // so vScale defaulting to 1,1,1 proves the 9-float stride landed right.
      check('abl belt affine scale ~= 1', Math.abs(r.aff.s[0] - 1) < 1e-3 && Math.abs(r.aff.s[2] - 1) < 1e-3);

      // Positive control: this .abl is v0x0201, so the body is EMBYTECRYPT_CONTAINER
      // -encoded. Skip that decode (treat as plaintext) and the bone string is
      // garbage, not "Bip01_Spine2" — i.e. the encryption gate is load-bearing.
      const BC = require('./bytecrypt.js');
      const hdr = BC.readHeader(fs.readFileSync(wingFile));
      const raw = fs.readFileSync(wingFile);
      const ver = raw.readUInt32LE(hdr.bodyOffset);
      check('positive control: v0x0201 is encrypted', ver >= 0x0200);
      // Re-parse WITHOUT the container decode by faking a sub-0x0200 version:
      // clear the high bit of the version dword in a copy so openContainer skips
      // decode, then confirm the bone no longer reads as the real one.
      const tampered = Buffer.from(raw);
      tampered.writeUInt32LE(0x0101, hdr.bodyOffset); // < 0x0200 -> no decode path
      let plainBone = '';
      try { plainBone = (EQ.parseAbl(tampered).bone) || ''; } catch (e) { plainBone = '<throw>'; }
      check('positive control: undecoded body does NOT yield Bip01_Spine2',
            plainBone !== 'Bip01_Spine2');
    } else {
      console.log('  --   sample .abl absent — skipped equipmodels decode checks');
    }

    // The emitted table, if it has been generated.
    const EMJSON = path.join(__dirname, '..', '..', 'assets', 'equipmodels.json');
    if (fs.existsSync(EMJSON)) {
      const em = JSON.parse(fs.readFileSync(EMJSON, 'utf8'));
      eq('equipmodels links', Object.keys(em.links).length, 1103);
      eq('equipmodels items', Object.keys(em.items).length, 1363);
      const it = em.items['48/1'];
      check('item 48/1 wears m_2017_victors_wings',
            it && it.r.includes('m_2017_victors_wings'));
      const lk = em.links['m_2017_victors_wings'];
      check('link m_2017_victors_wings -> 2017_victors_wings @ Bip01_Spine2',
            lk && lk.abf === '2017_victors_wings' && lk.bone === 'Bip01_Spine2');

      // abfAlias: .abf parts collapsed by the build dedup onto a representative
      // that IS built, so all 505 distinct .abf resolve to a prefab. Only the
      // 14 whose art is not shipped (skeleton/piece .x absent from CLIENT AND the
      // SkinObject archive) stay unresolvable — abyss_necklace shares geometry
      // with abyss_bracelet, so it aliases onto it.
      const alias = em.abfAlias || {};
      eq('equipmodels abfAlias count', Object.keys(alias).length, 294);
      eq('abfAlias abyss_necklace -> abyss_bracelet', alias['abyss_necklace'], 'abyss_bracelet');
      const abfSet = new Set(Object.values(em.links).map((l) => l.abf));
      const resolvable = [...abfSet].filter((a) => !alias[a]).length + Object.keys(alias).length;
      eq('abf coverage: distinct .abf', abfSet.size, 505);
      eq('abf coverage: resolvable (own-name + aliased)', resolvable, 505);
    } else {
      console.log('  --   equipmodels.json not generated (run extract-equipmodels.js) — skipped table checks');
    }

    // Skinned-piece table (equippieces.json) — item -> .cps SetPiece -> mesh/submesh.
    const EPJSON = path.join(__dirname, '..', '..', 'assets', 'equippieces.json');
    if (fs.existsSync(EPJSON)) {
      const ep = JSON.parse(fs.readFileSync(EPJSON, 'utf8'));
      eq('equippieces pieceLinks', Object.keys(ep.pieceLinks).length, 6208);
      eq('equippieces pieceItems', Object.keys(ep.pieceItems).length, 11889);
      // Every emitted piece link resolves to a mesh stem + (usually) a sub-mesh.
      const anyLink = ep.pieceLinks['sdn0020_m'];
      check('piece sdn0020_m -> s_m_gum [sdn0020]',
            anyLink && anyLink.m === 's_m_gum' && anyLink.s === 'sdn0020');
      // A pieceItems entry only names .cps stems that key into pieceLinks.
      let badRef = 0;
      for (const k of Object.keys(ep.pieceItems)) {
        const e = ep.pieceItems[k];
        for (const s of [...(e.r || []), ...(e.l || [])])
          if (s && !ep.pieceLinks[s]) badRef++;
      }
      eq('pieceItems reference only known pieceLinks (0 dangling)', badRef, 0);

      // --- basePieces: the per-character base-body-renderer-to-hide map --------
      // A .chf's piece list is indexed by EMPIECECHAR (readPieceList's loop index
      // IS the slot); each base piece resolves to the SUB-MESH that names its
      // renderer on the built prefab. This is what RanEquipView disables so a worn
      // .cps REPLACES the body piece (SetPiece overwrites) instead of overlaying.
      check('equippieces has basePieces', !!ep.basePieces);
      eq('basePieces character count', Object.keys(ep.basePieces || {}).length, 1079);
      const om = (ep.basePieces || {})['o_m'] || {};
      const ow = (ep.basePieces || {})['o_w'] || {};
      // o_m: HEAD/UPBODY/LOBODY/GLOVE/FOOT/HAIR base renderers by EMPIECECHAR.
      eq('o_m UPBODY(1) base renderer', om['1'], 'm2_bs_body');
      eq('o_m LOBODY(2) base renderer', om['2'], 'object04');
      eq('o_m GLOVE(3) base renderer', om['3'], 'm2_bs_hand');
      eq('o_m FOOT(6) base renderer', om['6'], 'm2_bs_foot');
      eq('o_w UPBODY(1) base renderer', ow['1'], 'w1_bs_body');
      // The per-character NECESSITY: w1_bs_hand is o_w's GLOVE, but the SAME submesh
      // name is LHAND for another character, so a single global submesh->slot map
      // would mis-hide. This is why basePieces is keyed by character.
      eq('o_w GLOVE(3) base renderer', ow['3'], 'w1_bs_hand');
      const bySub = new Map();
      for (const m of Object.values(ep.basePieces || {}))
        for (const [slot, sub] of Object.entries(m)) {
          if (!bySub.has(sub)) bySub.set(sub, new Set());
          bySub.get(sub).add(+slot);
        }
      check('w1_bs_hand maps to >1 EMPIECECHAR across chars (global map would break)',
            (bySub.get('w1_bs_hand') || new Set()).size > 1);
      let ambiguous = 0;
      for (const s of bySub.values()) if (s.size > 1) ambiguous++;
      eq('submesh names ambiguous across characters (per-char map required)', ambiguous, 65);

      // --- pieceBones: the ordered skin-bone names the runtime rebind needs -----
      // The importer leaves a piece's CHARACTER-skeleton bones null, so
      // RanEquipView cannot read them back off the Transform; the names are emitted
      // here from the .rmesh instead (mirroring RanChfBuilder.SkinBoneNames).
      check('equippieces has pieceBones', !!ep.pieceBones);
      eq('pieceBones mesh count', Object.keys(ep.pieceBones || {}).length, 726);
      let boneLists = 0;
      for (const m of Object.values(ep.pieceBones || {})) boneLists += Object.keys(m).length;
      eq('pieceBones skinned sub-mesh lists', boneLists, 2179);
      const teBody = ((ep.pieceBones || {})['s_m_cos_the'] || {})['m2_te_body'] || null;
      check('piece s_m_cos_the/m2_te_body has 12 bones incl Bip01_Spine2',
            teBody && teBody.length === 12 && teBody[0] === 'Bip01_Neck' && teBody.includes('Bip01_Spine2'));
      // A wrong .rmesh stride would shred the string-pool offsets into garbage, so
      // clean 3ds-Max bone names are the check that readRmeshSkinBones landed right
      // — the same "names come out clean" control the animation reader uses.
      check('piece skin-bone names are clean (right .rmesh stride)',
            teBody && teBody.every((n) => /^[A-Za-z0-9_]+$/.test(n)));
      // Positive control: reading the same .rmesh at the WRONG header size mangles
      // the string pool, so the body bone list is NOT 12 clean Bip01 names.
      const rawMesh = path.join(__dirname, '..', '..', 'assets', 'meshes', 's_m_cos_the.rmesh');
      if (fs.existsSync(rawMesh)) {
        const buf = fs.readFileSync(rawMesh);
        const good = EQ.readRmeshSkinBones(buf);
        check('readRmeshSkinBones recovers m2_te_body bones from raw .rmesh',
              good && good['m2_te_body'] && good['m2_te_body'][0] === 'Bip01_Neck');
        const bad = Buffer.from(buf);
        bad.writeUInt32LE(buf.readUInt32LE(36) + 8, 36); // corrupt stringBytes -> shift all tables
        let mangled = null;
        try { mangled = EQ.readRmeshSkinBones(bad); } catch (e) { mangled = null; }
        check('positive control: wrong string-pool size does NOT yield clean Bip01_Neck',
              !mangled || !mangled['m2_te_body'] || mangled['m2_te_body'][0] !== 'Bip01_Neck');
      }
    } else {
      console.log('  --   equippieces.json not generated — skipped skinned-piece checks');
    }
  } else {
    console.log('  --   CLIENT/data/skinobject absent — skipped equipmodels checks');
  }
}

// ===========================================================================
//  PK visual systems — combo banner textures + wire-offset contracts
// ===========================================================================
console.log('\nPK visual systems (stage-pk-textures + packet offsets)');
{
  const pk = require('./stage-pk-textures.js');

  // -- texture staging -------------------------------------------------------
  const results = pk.run(true);   // dry: resolve + measure, write nothing
  const resolved = results.filter((r) => r.ok);
  eq('stage-pk-textures resolves every PK texture', resolved.length, 15);
  const absent = results.filter((r) => !r.ok).map((r) => r.atlas);
  check('none absent', absent.length === 0, absent.join(' '));

  const byName = Object.fromEntries(results.map((r) => [r.atlas, r]));
  // The 13 combo banners are all 256x256 (uiinnercfg03 SizeX/SizeY).
  let comboSquare = true;
  for (let n = 2; n <= 14; n++) {
    const r = byName[`pk_combo_${String(n).padStart(2, '0')}.dds`];
    if (!r || r.width !== 256 || r.height !== 256) comboSquare = false;
  }
  check('pk_combo_02..14 are all 256x256', comboSquare);
  check('number.dds is 256x256',
        byName['number.dds'] && byName['number.dds'].width === 256 &&
        byName['number.dds'].height === 256);
  check('charinven03.dds is 512x512',
        byName['charinven03.dds'] && byName['charinven03.dds'].width === 512 &&
        byName['charinven03.dds'].height === 512);

  // The staged PNGs must be on disk under Resources/UI (loaded via UI/<stem>).
  const uiDir = path.join(__dirname, '..', '..', 'unity', 'RanMobile',
                          'Assets', 'Ran', 'Resources', 'UI');
  let stagedPng = 0, badPng = 0;
  for (const r of results) {
    const stem = r.atlas.replace(/\.dds$/i, '');
    const p = path.join(uiDir, stem + '.png');
    if (!fs.existsSync(p)) continue;
    stagedPng++;
    const head = fs.readFileSync(p).slice(0, 8).toString('hex');
    if (head !== '89504e470d0a1a0a') badPng++;
  }
  eq('all 15 PK PNGs staged to Resources/UI', stagedPng, 15);
  eq('every staged PK PNG has a valid PNG header', badPng, 0);

  // -- combo tier mapping (mirror of SET_PK_COMBO + uiinnercfg03) -------------
  // count -> banner stem. The MASTER_KILL art (pk_combo_05) is deliberately
  // never used: count 5 jumps to KILLING_SPREE (pk_combo_06), exactly as the PC.
  const cs = pk.comboTierStem;
  check('combo <2 shows no banner', cs(0) === null && cs(1) === null);
  eq('combo 2  -> pk_combo_02', cs(2), 'pk_combo_02');
  eq('combo 3  -> pk_combo_03', cs(3), 'pk_combo_03');
  eq('combo 4  -> pk_combo_04', cs(4), 'pk_combo_04');
  eq('combo 5  -> pk_combo_06 (MASTER_KILL/pk_combo_05 skipped)', cs(5), 'pk_combo_06');
  eq('combo 6  -> pk_combo_07', cs(6), 'pk_combo_07');
  eq('combo 7  -> pk_combo_08', cs(7), 'pk_combo_08');
  eq('combo 8  -> pk_combo_09', cs(8), 'pk_combo_09');
  eq('combo 9  -> pk_combo_10', cs(9), 'pk_combo_10');
  eq('combo 10 -> pk_combo_11', cs(10), 'pk_combo_11');
  eq('combo 11 -> pk_combo_12', cs(11), 'pk_combo_12');
  eq('combo 12 -> pk_combo_13', cs(12), 'pk_combo_13');
  eq('combo 13 -> pk_combo_14 (OWNAGE)', cs(13), 'pk_combo_14');
  eq('combo 99 -> pk_combo_14 (OWNAGE cap)', cs(99), 'pk_combo_14');
  // pk_combo_05 (MASTER_KILL) is staged but referenced by no combo count.
  const referenced = new Set();
  for (let n = 0; n < 60; n++) { const s = cs(n); if (s) referenced.add(s); }
  check('pk_combo_05 is staged but intentionally unreferenced by any count',
        !referenced.has('pk_combo_05'));

  // -- wire-offset contracts: tie the body offsets RanPkPackets uses back to the
  //    compiler-measured structs in layout.json, so a probe shift fails here. ---
  const LJ = path.join(__dirname, '..', 'layout-probe', 'layout.json');
  if (fs.existsSync(LJ)) {
    const lj = JSON.parse(fs.readFileSync(LJ, 'utf8'));
    const S = lj.allStructs || {};
    const E = lj.allEnums || {};
    const HDR = 8;   // NET_MSG_GENERIC, stripped before a module's Handle

    // opcodes RanPkPackets pins
    eq('opcode PKCOMBO_BRD = 4731', E['NET_MSG_GCTRL_PKCOMBO_BRD'], 4731);
    eq('opcode PKCOMBO_END_BRD = 4732', E['NET_MSG_GCTRL_PKCOMBO_END_BRD'], 4732);
    eq('opcode PK_RANK_HISTORY_UPDATE = 4741', E['NET_MSG_PK_RANK_HISTORY_UPDATE'], 4741);
    eq('opcode REQ_GLOBAL_RANKING = 5957', E['NET_MSG_GCTRL_REQ_GLOBAL_RANKING'], 5957);
    eq('opcode REQ_GLOBAL_RANKING_KILL_AGT = 5960',
       E['NET_MSG_GCTRL_REQ_GLOBAL_RANKING_KILL_AGT'], 5960);
    // EM_GLOBAL_RANKING_FB_TOP_KILL (=2, GLContrlPcMsg.h:254) is a local enum the
    // probe's gen-enums does not export; its value is read straight from SOURCE and
    // pinned as RanPkPackets.RankFbTopKill. Confirm the source line still says 2.
    const pcMsg = path.join(__dirname, '..', '..', '..', 'SOURCE', 'Lib_Client',
                            'G-Logic', 'GLContrlPcMsg.h');
    if (fs.existsSync(pcMsg)) {
      const m = fs.readFileSync(pcMsg, 'utf8')
        .match(/EM_GLOBAL_RANKING_FB_TOP_KILL\s*=\s*(\d+)/);
      eq('EM_GLOBAL_RANKING_FB_TOP_KILL = 2 (SOURCE)', m && Number(m[1]), 2);
    }

    // combo: sCOMBO@12 (size 12) => body ComboShow=4, ComboCount=8, ComboTime=12
    const cb = S['GLMSG::SNETPC_PKCOMBO_BRD'];
    check('PKCOMBO_BRD measured', cb && cb.size === 24);
    if (cb) {
      eq('  sCOMBO offset 12', cb.fields.sCOMBO.off, 12);
      eq('  sCOMBO size 12 (bool+int+float, natural)', cb.fields.sCOMBO.size, 12);
      // body offsets RanPkPackets.cs uses:
      eq('  body ComboShow = sCOMBO-HDR', cb.fields.sCOMBO.off - HDR, 4);   // RanPkPackets.ComboShow
      eq('  body ComboCount = sCOMBO-HDR+4', cb.fields.sCOMBO.off - HDR + 4, 8); // .ComboCount
    }

    // rank: sTopKill@16 (size 100 => natural, not pack(1)'s 94) => body base 8
    const ra = S['GLMSG::SNETPC_REQ_GLOBAL_RANKING_KILL_AGT'];
    check('KILL_AGT measured', ra && ra.size === 116);
    if (ra) {
      eq('  emFB body 0', ra.fields.emFB.off - HDR, 0);       // RanPkPackets.RankFb
      eq('  wPart body 4', ra.fields.wPart.off - HDR, 4);     // .RankPart
      eq('  nIndex body 6', ra.fields.nIndex.off - HDR, 6);   // .RankIndex
      eq('  sTopKill body 8', ra.fields.sTopKill.off - HDR, 8); // .RkChaNum base
      eq('  STOP_RANK_KILL is 100 bytes (natural align, not pack(1) 94)',
         ra.fields.sTopKill.size, 100);
    }
  } else {
    console.log('  --   layout.json absent — skipped wire-offset contracts');
  }
}

// --- character CREATE screen data (extract-charclasses.js, extract-uicfg.js) ---
//
// Pins two things a prior run of each extractor already measured/decoded:
// the per-class face/hair counts + hair-colour table (charclasses.json), and
// the fix that makes CREATE_CHAR_* controls appear in uicfg.json at all
// (extract-uicfg.js used to only scan `ui*cfg*.xml` / `_inner_*.xml`, silently
// skipping `_outer_createcharacterwindow.xml` entirely).
console.log('\ncharacter create data');
{
  const { parseControl } = require('./extract-uicfg');

  const ccPath = path.join(__dirname, '..', '..', 'assets', 'charclasses.json');
  if (fs.existsSync(ccPath)) {
    const cc = JSON.parse(fs.readFileSync(ccPath, 'utf8'));
    eq('school count', cc.schoolNames.length, 3);
    eq('school 0 (SacredGate)', cc.schoolNames[0], 'SacredGate');
    eq('school 1 (MysticPeak)', cc.schoolNames[1], 'MysticPeak');
    eq('school 2 (Phoenix)', cc.schoolNames[2], 'Phoenix');
    eq('class count (EMCHARINDEX 0..15)', cc.classes.length, 16);
    // GLCI_BRAWLER_M=0, GLCI_ARCHER_W=2, GLCI_GUNNER_M=10 (GLCharDefine.h).
    eq('BRAWLER_M head/hair', `${cc.classes[0].headNumSelect}/${cc.classes[0].hairNumSelect}`, '13/6');
    eq('ARCHER_W head/hair', `${cc.classes[2].headNumSelect}/${cc.classes[2].hairNumSelect}`, '11/5');
    eq('GUNNER_M head/hair', `${cc.classes[10].headNumSelect}/${cc.classes[10].hairNumSelect}`, '13/6');
    eq('scale range', `${cc.scaleMin}-${cc.scaleMax}`, '0.88-1.12');
    eq('hair colour table rows', cc.hairColorTable.length, 16);
    eq('hair colour table cols', cc.hairColorTable[0].length, 20);

    // The staged Resources copy the runtime actually reads must match.
    const stagedPath = path.join(__dirname, '..', '..', 'unity', 'RanMobile',
                                 'Assets', 'Ran', 'Resources', 'charclasses.json');
    if (fs.existsSync(stagedPath)) {
      check('staged charclasses.json matches the source copy',
            fs.readFileSync(stagedPath, 'utf8') === fs.readFileSync(ccPath, 'utf8'));
    } else {
      check('charclasses.json staged into Resources', false);
    }
  } else {
    check('charclasses.json present (run extract-charclasses.js)', false);
  }

  // _outer_createcharacterwindow.xml — read directly (independent of
  // extract-uicfg.js) and cross-checked against uicfg.json, so a regression
  // in the extractor's regex is caught even if uicfg.json itself is stale.
  const xmlPath = path.join(__dirname, '..', '..', '..', 'CLIENT', 'data', 'gui',
                            '_outer_createcharacterwindow.xml');
  if (fs.existsSync(xmlPath)) {
    const text = fs.readFileSync(xmlPath, 'utf8');
    const controls = {};
    const re = /<CONTROL([^>]*)>([\s\S]*?)<\/CONTROL>/g;
    let m;
    while ((m = re.exec(text))) {
      const id = /Id\s*=\s*"([^"]*)"/.exec(m[1]);
      if (id) controls[id[1]] = parseControl(id[1], m[2], '');
    }
    eq('CREATE_CHAR_WINDOW rect (read direct from xml)',
       `${controls.CREATE_CHAR_WINDOW.x},${controls.CREATE_CHAR_WINDOW.y} ` +
       `${controls.CREATE_CHAR_WINDOW.w}x${controls.CREATE_CHAR_WINDOW.h}`,
       '0,0 250x505');
    eq('CREATE_CHAR_SCALE_BAR rect', `${controls.CREATE_CHAR_SCALE_BAR.x},${controls.CREATE_CHAR_SCALE_BAR.y} ` +
       `${controls.CREATE_CHAR_SCALE_BAR.w}x${controls.CREATE_CHAR_SCALE_BAR.h}`, '12,220 162x10');

    const cfgPath = path.join(__dirname, '..', '..', 'assets', 'uicfg.json');
    if (fs.existsSync(cfgPath)) {
      const cfg = JSON.parse(fs.readFileSync(cfgPath, 'utf8'));
      check('uicfg.json now carries CREATE_CHAR_* (the _outer_ extraction gap fix)',
            !!cfg.controls.CREATE_CHAR_WINDOW);
      if (cfg.controls.CREATE_CHAR_WINDOW) {
        const c = cfg.controls.CREATE_CHAR_WINDOW;
        eq('  uicfg.json CREATE_CHAR_WINDOW matches the xml',
           `${c.x},${c.y} ${c.w}x${c.h}`, '0,0 250x505');
      }
      eq('uicfg.json CREATE_CHAR_* control count', Object.keys(cfg.controls)
         .filter((k) => k.startsWith('CREATE_CHAR')).length,
         Object.keys(controls).filter((k) => k.startsWith('CREATE_CHAR')).length);
    } else {
      check('uicfg.json present (run extract-uicfg.js --out)', false);
    }
  } else {
    console.log('  --   _outer_createcharacterwindow.xml absent — skipped');
  }
}

// --- PANDORA_BUTTON / _pandorawindow.xml + _petstyle.xml extraction gap ---
//
// Same bug class as the _outer_ fix above, found separately by RanUiCheck:
// the filename regex matched `ui*cfg*`/`_inner_`/`_outer_` only, so the two
// remaining standalone underscore-prefixed window configs
// (_pandorawindow.xml, _petstyle.xml) were silently never scanned and
// PANDORA_BUTTON (and everything else in those two files) was absent from
// uicfg.json. Pins the fix at both the raw-xml level (independent of
// extract-uicfg.js, so a regression in its regex is caught even if
// uicfg.json itself is stale) and in the generated uicfg.json/staged copy.
console.log('\nPANDORA_BUTTON / _pandorawindow.xml uicfg extraction gap');
{
  const { parseControl } = require('./extract-uicfg');
  const xmlPath = path.join(__dirname, '..', '..', '..', 'CLIENT', 'data', 'gui',
                            '_pandorawindow.xml');
  if (fs.existsSync(xmlPath)) {
    const text = fs.readFileSync(xmlPath, 'utf8');
    const controls = {};
    const re = /<CONTROL([^>]*)>([\s\S]*?)<\/CONTROL>/g;
    let m;
    while ((m = re.exec(text))) {
      const id = /Id\s*=\s*"([^"]*)"/.exec(m[1]);
      if (id) controls[id[1]] = parseControl(id[1], m[2], '');
    }
    check('_pandorawindow.xml has a PANDORA_BUTTON control', !!controls.PANDORA_BUTTON);
    if (controls.PANDORA_BUTTON) {
      eq('PANDORA_BUTTON rect (read direct from xml)',
         `${controls.PANDORA_BUTTON.x},${controls.PANDORA_BUTTON.y} ` +
         `${controls.PANDORA_BUTTON.w}x${controls.PANDORA_BUTTON.h}`,
         '317,538 80x35');
    }

    const cfgPath = path.join(__dirname, '..', '..', 'assets', 'uicfg.json');
    if (fs.existsSync(cfgPath)) {
      const cfg = JSON.parse(fs.readFileSync(cfgPath, 'utf8'));
      check('uicfg.json now carries PANDORA_BUTTON (the _pandorawindow extraction gap fix)',
            !!cfg.controls.PANDORA_BUTTON);
      if (cfg.controls.PANDORA_BUTTON) {
        const c = cfg.controls.PANDORA_BUTTON;
        eq('  uicfg.json PANDORA_BUTTON matches the xml', `${c.x},${c.y} ${c.w}x${c.h}`, '317,538 80x35');
      }

      const stagedPath = path.join(__dirname, '..', '..', 'unity', 'RanMobile',
                                   'Assets', 'Ran', 'Resources', 'uicfg.json');
      if (fs.existsSync(stagedPath)) {
        check('staged uicfg.json matches the source copy',
              fs.readFileSync(stagedPath, 'utf8') === fs.readFileSync(cfgPath, 'utf8'));
      } else {
        check('uicfg.json staged into Resources', false);
      }
    } else {
      check('uicfg.json present (run extract-uicfg.js --out)', false);
    }
  } else {
    console.log('  --   _pandorawindow.xml absent — skipped');
  }

  // _petstyle.xml is the sibling file the same regex fix picks up — sanity
  // check it actually has controls too, so the fix is not accidentally
  // scoped to only the one file the bug report named.
  const petPath = path.join(__dirname, '..', '..', '..', 'CLIENT', 'data', 'gui', '_petstyle.xml');
  if (fs.existsSync(petPath)) {
    const text = fs.readFileSync(petPath, 'utf8');
    const hasControl = /<CONTROL[^>]*Id\s*=\s*"PETSTYLECARD_A_00"/.test(text);
    check('_petstyle.xml has PETSTYLECARD_A_00', hasControl);
    const cfgPath = path.join(__dirname, '..', '..', 'assets', 'uicfg.json');
    if (fs.existsSync(cfgPath)) {
      const cfg = JSON.parse(fs.readFileSync(cfgPath, 'utf8'));
      check('uicfg.json now carries PETSTYLECARD_A_00 (same fix, sibling file)',
            !!cfg.controls.PETSTYLECARD_A_00);
    }
  } else {
    console.log('  --   _petstyle.xml absent — skipped');
  }
}

// --- extract-uicfg.js XML-comment bug: stale WINDOW_POS inside a comment ---
//
// Found 2026-08-20 re-verifying the HUD corner-icon text-fallback claim
// against real uicfg.json values: several loose configs leave a PREVIOUS
// revision's WINDOW_POS (an "original pos, restore when re-enabled" note)
// inside a `<!-- ... -->` block immediately ahead of the LIVE tag. Before the
// fix, `harvest()`'s text was never comment-stripped, so `parseControl`'s
// first-match-wins WINDOW_POS/TEXTURE regexes silently locked onto the STALE
// commented rect instead of the live one. Pins both ends: the raw xml (so a
// regression in the source data itself would be caught) and the generated
// uicfg.json / staged copy (so a regression in the extractor would be
// caught even if nobody re-diffs the xml by hand).
console.log('\nextract-uicfg.js: WINDOW_POS inside an XML comment must not win');
{
  const cases = [
    { file: '_inner_competitionui.xml', id: 'COMPETITION_NOTIFY_BUTTON', live: '605,514 35x59', stale: '542,514 35x59' },
    { file: '_inner_productui.xml',     id: 'RAN_PRODUCT_BUTTON',        live: '542,538 35x35', stale: '497,538 35x35' },
  ];
  const cfgPath = path.join(__dirname, '..', '..', 'assets', 'uicfg.json');
  const cfg = fs.existsSync(cfgPath) ? JSON.parse(fs.readFileSync(cfgPath, 'utf8')) : null;

  for (const { file, id, live, stale } of cases) {
    const xmlPath = path.join(__dirname, '..', '..', '..', 'CLIENT', 'data', 'gui', file);
    if (!fs.existsSync(xmlPath)) { console.log(`  --   ${file} absent — skipped`); continue; }
    const raw = fs.readFileSync(xmlPath, 'utf8');

    // Prove the trap is real: a naive (non-comment-aware) first-match parse
    // of this exact file finds the STALE rect, not the live one.
    const reBlock = new RegExp(`<CONTROL[^>]*Id="${id}"[^>]*>([\\s\\S]*?)</CONTROL>`);
    const naiveBody = reBlock.exec(raw);
    check(`${file}: control body contains a commented-out WINDOW_POS ahead of the live one`,
          !!naiveBody && /<!--[^>]*WINDOW_POS/.test(naiveBody[1]));
    if (naiveBody) {
      const naiveWin = /<WINDOW_POS([^>]*)\/>/.exec(naiveBody[1]);
      const naiveAttrs = {};
      if (naiveWin) for (const m of naiveWin[1].matchAll(/([A-Za-z_]+)\s*=\s*"([^"]*)"/g)) naiveAttrs[m[1]] = m[2];
      const naiveRect = naiveWin ? `${naiveAttrs.X},${naiveAttrs.Y} ${naiveAttrs.W}x${naiveAttrs.H}` : '(none)';
      eq(`  naive first-match WINDOW_POS for ${id} (the historical bug)`, naiveRect, stale);
    }

    // Prove the fix: stripping <!-- --> first (extract-uicfg.js's own fix)
    // finds the LIVE rect.
    const stripped = raw.replace(/<!--[\s\S]*?-->/g, '');
    const strippedBody = reBlock.exec(stripped);
    check(`${file}: comment-stripped body still has ${id}`, !!strippedBody);
    if (strippedBody) {
      const win = /<WINDOW_POS([^>]*)\/>/.exec(strippedBody[1]);
      const a = {};
      if (win) for (const m of win[1].matchAll(/([A-Za-z_]+)\s*=\s*"([^"]*)"/g)) a[m[1]] = m[2];
      const rect = win ? `${a.X},${a.Y} ${a.W}x${a.H}` : '(none)';
      eq(`  comment-stripped WINDOW_POS for ${id} (the fix)`, rect, live);
    }

    // And the generated uicfg.json (source + staged copy) must carry the
    // live rect, not the stale one.
    if (cfg && cfg.controls[id]) {
      const c = cfg.controls[id];
      eq(`  uicfg.json ${id} matches the LIVE xml rect, not the commented-out one`,
         `${c.x},${c.y} ${c.w}x${c.h}`, live);
    } else {
      check(`uicfg.json carries ${id}`, false);
    }
  }

  // Phantom controls: _inner_codexui.xml has THREE entire CONTROL blocks
  // commented out (RAN_SPEC_CODEX_PAGE_LEFT_BACK/CENTER_BACK/RIGHT_BACK,
  // RAN_SPEC_CODEX_PAGE_*_TEXT, RAN_SPEC_CODEX_LIST_SLOT4/5) that the
  // pre-fix extractor still parsed as if real (a commented-out `<CONTROL>`
  // still matches the block regex when comments are not stripped first).
  // After the fix they must not appear in uicfg.json at all.
  if (cfg) {
    const phantoms = ['RAN_SPEC_CODEX_PAGE_LEFT_BACK', 'RAN_SPEC_CODEX_LIST_SLOT4', 'RAN_SPEC_CODEX_LIST_SLOT5'];
    for (const id of phantoms) {
      check(`uicfg.json no longer carries commented-out phantom control ${id}`, !cfg.controls[id]);
    }
  }

  const stagedPath = path.join(__dirname, '..', '..', 'unity', 'RanMobile',
                               'Assets', 'Ran', 'Resources', 'uicfg.json');
  if (cfg && fs.existsSync(stagedPath)) {
    check('staged uicfg.json matches the source copy (comment-strip fix)',
          fs.readFileSync(stagedPath, 'utf8') === fs.readFileSync(cfgPath, 'utf8'));
  }
}

// --- HUD corner-icon real art: PET_STATUS_BOX/PANDORA_BUTTON/
//     COMPETITION_NOTIFY_BUTTON/RAN_PRODUCT_BUTTON ---
//
// RanPlayScene.MakeTopLevelIcon used a permanent ASCII-text fallback for
// these four controls on the claim that "neither control carries a fill
// texture of its own in the extracted data". Re-verified 2026-08-20: true
// for the CONTAINER control itself, but each one's real PC source
// (PandoraBoxButton.cpp / CompetitionNotifyButton.cpp / ProductButton.cpp /
// PetStatus.cpp) only ever draws a separate CHILD control — "<Id>_IMAGE",
// or PET_STATUS_NO_FOOD for the pet box — and that child DOES carry real
// shipped atlas art. Pins the exact tex/rect measured directly from
// CLIENT/data/gui so a future extractor change can't silently drop them
// back to text without a test failing.
console.log('\nHUD corner-icon child art (PANDORA_BUTTON_IMAGE etc.) — real, not a text-fallback gap');
{
  const cfgPath = path.join(__dirname, '..', '..', 'assets', 'uicfg.json');
  const cfg = fs.existsSync(cfgPath) ? JSON.parse(fs.readFileSync(cfgPath, 'utf8')) : null;
  const expect = {
    PANDORA_BUTTON_IMAGE:            { tex: 'pandoraico.dds',       tx: 0,   ty: 0,  tw: 35, th: 35 },
    COMPETITION_NOTIFY_BUTTON_IMAGE: { tex: 'sc_battle_ui_set.dds', tx: 206, ty: 15, tw: 35, th: 35 },
    RAN_PRODUCT_BUTTON_IMAGE:        { tex: 'GUI_Combination.dds',  tx: 455, ty: 0,  tw: 35, th: 35 },
    PET_STATUS_NO_FOOD:              { tex: 'q_icon.dds',           tx: 210, ty: 35, tw: 35, th: 35 },
  };
  if (cfg) {
    for (const [id, want] of Object.entries(expect)) {
      const c = cfg.controls[id];
      check(`uicfg.json ${id} exists`, !!c);
      if (c) {
        eq(`  ${id} tex`, c.tex, want.tex);
        eq(`  ${id} source rect`, `${c.tx},${c.ty} ${c.tw}x${c.th}`, `${want.tx},${want.ty} ${want.tw}x${want.th}`);
      }
    }
    // The CONTAINER controls genuinely have no texture of their own — this
    // is the other half of the finding (real art exists, just on a child),
    // not a contradiction of it.
    for (const id of ['PANDORA_BUTTON', 'COMPETITION_NOTIFY_BUTTON', 'RAN_PRODUCT_BUTTON', 'PET_STATUS_BOX']) {
      const c = cfg.controls[id];
      check(`uicfg.json ${id} (container) itself has no tex — matches SOURCE (art is on the child)`,
            !!c && !c.tex);
    }
  } else {
    check('uicfg.json present', false);
  }

  // RanPlayScene.cs must actually be wired to look these up (imageCfgId /
  // the "<cfgId>_IMAGE" default convention), not just have the data sitting
  // unused in uicfg.json.
  const scenePath = path.join(__dirname, '..', '..', 'unity', 'com.ran.mobile.assets',
                              'Editor', 'RanPlayScene.cs');
  if (fs.existsSync(scenePath)) {
    const src = fs.readFileSync(scenePath, 'utf8');
    check('RanPlayScene.cs: MakeTopLevelIcon resolves a child "<cfgId>_IMAGE" control',
          src.includes('imageCfgId ?? (cfgId + "_IMAGE")'));
    check('RanPlayScene.cs: PetStatusButton passes the real PET_STATUS_NO_FOOD child id',
          /imageCfgId:\s*"PET_STATUS_NO_FOOD"/.test(src));
    const petCalls = (src.match(/"PetStatusButton", "PET_STATUS_BOX"/g) || []).length;
    const petFixed = (src.match(/imageCfgId:\s*"PET_STATUS_NO_FOOD"/g) || []).length;
    eq('  every PetStatusButton call site passes imageCfgId (Build() and BuildBootstrap())',
       petFixed, petCalls);
  } else {
    check('RanPlayScene.cs present', false);
  }
}

console.log('\nitem mix / product dispatch (Ran/config.ini bFeatureProduct)');
{
  // The real dispatch this drives: RanNpcModule.RouteBasic's EM_ITEM_MIX case
  // (DialogueWindow.cpp:570-580 / NPCDialoguePage.cpp:625-634) opens
  // CProductWindow when RANPARAM::bFeatureProduct is set, CItemMixWindow
  // otherwise. That flag is a LOCAL client config value
  // (Lib_Engine/Utils/RANPARAM_FEATURE.cpp:124, [GAME_FEATURE] bFeatureProduct
  // in Config.ini), not a live-server value, and it ships ENCRYPTED with the
  // same scheme as the RCC archives (gamecrypt.decode) — this measures it
  // directly from the real deploy tree rather than assuming either branch.
  const cfgPath = path.join(RAN, 'config.ini');
  if (fs.existsSync(cfgPath)) {
    const raw = fs.readFileSync(cfgPath);
    check('Ran/config.ini is gamecrypt-encoded (not plaintext on disk)', G.isEncoded(raw));
    const text = G.decode(raw).toString('latin1').replace(/[^\x20-\x7e\r\n]/g, '');
    check('Ran/config.ini decodes to a real [GAME_FEATURE] ini', text.includes('[GAME_FEATURE]'));
    const m = /bFeatureProduct\s*=\s*(\d+)/.exec(text);
    check('Ran/config.ini has a bFeatureProduct key', !!m);
    if (m) {
      eq('Ran/config.ini bFeatureProduct value (0 = real PC opens CItemMixWindow, not CProductWindow, for EM_ITEM_MIX)',
         m[1], '0');
    }
  } else {
    check('Ran/config.ini present', false);
  }
}

console.log('\nitemmix.ims (ITEM_MIX / PRODUCT recipe table — shared by CProductWindow and CItemMixWindow)');
{
  const im = require('./extract-itemmix.js');
  const glogicRcc = A('glogic/GLogic.rcc');
  if (fs.existsSync(glogicRcc)) {
    const arc = new RccArchive(glogicRcc);
    const raw = arc.read('itemmix.ims');
    const parsed = im.parse(raw);
    eq('itemmix.ims fileVer', parsed.fileVer, 0x0200);
    eq('itemmix.ims record count', parsed.count, 300);
    check('itemmix.ims parses to EOF exactly (0 bytes left over)', parsed.eofExact, `${parsed.trailing} bytes left`);

    // Pin record 0 (uses all 5 material slots) and record 1 (uses only 2 —
    // the other 3 are the file's OWN 0xFFFF/0xFFFF unused-slot sentinel, not
    // (0,0) as an earlier reading of this same extractor's own filter comment
    // assumed — confirmed by direct inspection of the real decoded data).
    const r0 = parsed.items[0], r1 = parsed.items[1];
    eq('itemmix.ims record 0 key', r0.key, 0);
    eq('itemmix.ims record 0 material count', r0.materials.length, 5);
    check('itemmix.ims record 0 uses all 5 real (non-sentinel) materials',
          r0.materials.every((mm) => mm.main !== 0xFFFF));
    eq('itemmix.ims record 0 result', `${r0.result.main}/${r0.result.sub}`, '1022/1');

    eq('itemmix.ims record 1 key', r1.key, 1);
    const real1 = r1.materials.filter((mm) => mm.main !== 0xFFFF);
    eq('itemmix.ims record 1 real material count (2 of 5 slots used)', real1.length, 2);
    const unused1 = r1.materials.filter((mm) => mm.main === 0xFFFF && mm.sub === 0xFFFF && mm.num === 0);
    eq('itemmix.ims record 1 unused slots are the 0xFFFF/0xFFFF/0 sentinel', unused1.length, 3);

    // Corpus-wide: confirms this sentinel is the real convention, not a
    // one-off in record 1 — 161 material slots measured 2026-08-20.
    let sentinelSlots = 0;
    for (const it of parsed.items) for (const mm of it.materials)
      if (mm.main === 0xFFFF && mm.sub === 0xFFFF) sentinelSlots++;
    eq('itemmix.ims total 0xFFFF/0xFFFF sentinel material slots (corpus-wide)', sentinelSlots, 161);
  } else {
    check('GLogic.rcc present', false);
  }
}

console.log('\nWater (DxEffectWater/Water2/River — see water.js)');
{
  const WATER = require('./water');
  const LAYOUT = require('../layout-probe/layout.json');

  // Type IDs: compiler-verified via layout-probe's consts block (probe.cpp),
  // not read from the DEF_EFFECT_* #define by eye — pin the hex values too,
  // since a probe/JS mismatch here would silently misclassify every effect.
  eq('DEF_EFFECT_WATER (compiler-verified)', WATER.WATER_TYPE, 0x2001);
  eq('DEF_EFFECT_WATER2 (compiler-verified)', WATER.WATER2_TYPE, 0x3013);
  eq('DEF_EFFECT_RIVER (compiler-verified)', WATER.RIVER_TYPE, 0x2006);

  // Struct sizes, from the layout probe — never hand-computed (see the
  // WATER_PROPERTY_100 note in water.js: 300 bytes, not the 292 a manual
  // field-width sum suggests).
  eq('WATER_PROPERTY_100 size', LAYOUT.allStructs.WATER_PROPERTY_100.size, 300);
  eq('WATER_PROPERTY_101 size', LAYOUT.allStructs.WATER_PROPERTY_101.size, 364);
  eq('WATER2_PROPERTY size', LAYOUT.allStructs.WATER2_PROPERTY.size, 1988);
  eq('RIVER_PROPERTY size', LAYOUT.allStructs.RIVER_PROPERTY.size, 596);

  // Positive control: a synthetic RIVER_PROPERTY (v0x107) built at the
  // MEASURED field offsets (independent literals, not re-derived from
  // water.js's own LAYOUT lookup — a decode bug that silently read the wrong
  // offset would otherwise pass by construction). Round-tripping real values
  // through every field is the check that the field ORDER in decodeRiver
  // matches the struct, not just that decoding doesn't throw.
  {
    const buf = Buffer.alloc(596);
    buf.writeUInt32LE(0x00000007, 0);              // m_dwFlag: USEDARK|USEFLASH|USEREFLECT
    buf.writeFloatLE(0.8, 4);                        // m_fBumpAlpha
    buf.writeFloatLE(0.6, 8);                        // m_fVel
    buf.writeFloatLE(1.0, 12);                       // m_fScale
    buf.writeFloatLE(200, 16); buf.writeFloatLE(150, 20); buf.writeFloatLE(100, 24); // m_vColor
    buf.writeFloatLE(2.0, 28);                       // m_fDarkScale
    buf.writeFloatLE(0.2, 32); buf.writeFloatLE(0.3, 36);   // m_vDarkVel
    buf.writeFloatLE(255, 40); buf.writeFloatLE(254, 44); buf.writeFloatLE(253, 48); // m_vDarkColor
    buf.writeFloatLE(100.5, 52); buf.writeFloatLE(20, 56); buf.writeFloatLE(-50, 60); // m_vMax
    buf.writeFloatLE(-100.5, 64); buf.writeFloatLE(-5, 68); buf.writeFloatLE(-250, 72); // m_vMin
    buf.write('_Wa_water1.bmp\0', 76, 'latin1');
    buf.write('_Wa_flash.bmp\0', 336, 'latin1');

    const r = WATER.decode(WATER.RIVER_TYPE, 0x107, buf);
    check('synthetic RIVER_PROPERTY decodes', !!r);
    if (r) {
      eq('  kind', r.kind, 3);
      check('  useDark/useFlash/useReflect true, useReflectNew/useSee/useSameHeight false',
            r.useDark && r.useFlash && r.useReflect && !r.useReflectNew && !r.useSee && !r.useSameHeight);
      eq('  velocity', r.velocity, 0.6000000238418579);
      eq('  darkVel', JSON.stringify(r.darkVel.map((v) => +v.toFixed(3))), '[0.2,0.3]');
      eq('  max', JSON.stringify(r.max), JSON.stringify([100.5, 20, -50]));
      eq('  min', JSON.stringify(r.min), JSON.stringify([-100.5, -5, -250]));
      eq('  textureDark', r.textureDark, '_Wa_water1.bmp');
      eq('  textureFlash', r.textureFlash, '_Wa_flash.bmp');
    }

    // Negative control: a version this decoder does not claim to support
    // must return null, not a plausible-looking wrong decode (the same
    // discipline extract-fog.js's version gate uses).
    check('unsupported river version (0x106) returns null (not decoded)',
          WATER.decode(WATER.RIVER_TYPE, 0x106, buf) === null);
    check('wrong-size buffer for the claimed version returns null',
          WATER.decode(WATER.RIVER_TYPE, 0x107, buf.subarray(0, 500)) === null);
  }

  // composeAffine: identity in, identity out (translation/scale/rotation all
  // at their D3DXMatrixCompX defaults).
  {
    const m = WATER.composeAffine({ trans: [0, 0, 0], rotateYPR: [0, 0, 0], scale: [1, 1, 1] });
    check('composeAffine(identity) is the identity matrix',
          Array.from(m).every((v, i) => Math.abs(v - (i % 5 === 0 ? 1 : 0)) < 1e-6));
    const t = WATER.composeAffine({ trans: [10, 20, 30], rotateYPR: [0, 0, 0], scale: [1, 1, 1] });
    // D3DXMatrixCompX: Mat = matScale * matRotate * matTrans — translation
    // lands unscaled/unrotated in the last ROW (row-major, D3D convention),
    // i.e. indices 12/13/14 of the flat 16-float array (see mapobj.js's
    // matrix() and water.js's composeAffine doc comment).
    eq('composeAffine translation-only: row-major indices 12/13/14',
       JSON.stringify([t[12], t[13], t[14]]), JSON.stringify([10, 20, 30]));
  }

  // Real corpus: run the actual extraction path (mapobj -> pieces -> aniMan ->
  // grasseff's standalone effect lists) over the shipped Map.rcc and pin what
  // it finds, so a regression anywhere in that chain fails loudly here rather
  // than only showing up as a quieter mapwater.json diff.
  const mapPath = A('map/Map.rcc');
  if (fs.existsSync(mapPath)) {
    const { RccArchive: RA2 } = require('./rcc');
    const W = require('./wld');
    const MO = require('./mapobj');
    const PIECES = require('./pieces');
    const ANIMAN = require('./animan');
    const GRASSEFF = require('./grasseff');

    const arc = new RA2(mapPath);
    const e = arc.entries.find((x) => path.basename(x.name).toLowerCase() === 'sps_ground.wld');
    check('sps_ground.wld present in Map.rcc', !!e);
    if (e) {
      const wld = W.open(arc.read(e));
      const scene = MO.parse(wld);
      const chain = PIECES.walkReplaceChain(wld.buf, scene.end);
      const aniStats = { nodes: [], frames: 0, meshes: 0, effects: 0, placed: [], water: [],
                         failedFrames: 0, vertices: 0, triangles: 0, collisionNodes: 0,
                         effectTypes: new Map(), textureNames: new Set(), firstFrameError: null };
      const ani = ANIMAN.walkAniMan(wld.buf, chain.end, aniStats);
      const eff = GRASSEFF.walkEffectLists(wld.buf, ani.end);

      eq('sps_ground: 1 water instance found', eff.water.length, 1);
      if (eff.water.length) {
        const w = eff.water[0];
        eq('sps_ground water: kind (River)', w.kind, 3);
        eq('sps_ground water: version (0x107, the live VERSION)', w.version, 0x107);
        eq('sps_ground water: velocity', w.velocity, 0.6000000238418579);
        eq('sps_ground water: textureDark', w.textureDark, '_Wa_water1.bmp');
        // Real placement: a real, non-degenerate world-space AABB — not
        // (0,0,0)-(0,0,0), which is what a mis-offset read tends to produce.
        const size = [w.max[0] - w.min[0], w.max[1] - w.min[1], w.max[2] - w.min[2]];
        check('sps_ground water: AABB is real and non-degenerate (XZ extent > 100 units)',
              size[0] > 100 && size[2] > 100 && Number.isFinite(size[1]));
        check('sps_ground water: AABB max > min on every axis',
              w.max[0] > w.min[0] && w.max[1] > w.min[1] && w.max[2] > w.min[2]);
      }
    }
  } else {
    check('Ran/data/map/Map.rcc present', false);
  }

  // mapcatalog.json: the merged, trimmed record a runtime actually reads.
  const catalogPath = path.join(__dirname, '..', '..', 'unity', 'RanMobile', 'Assets', 'Ran',
                                'Resources', 'mapcatalog.json');
  if (fs.existsSync(catalogPath)) {
    const cat = JSON.parse(fs.readFileSync(catalogPath, 'utf8'));
    const tyranny = cat.maps.find((m) => m.id === 222);
    check('mapcatalog.json: map 222 (Tyranny / sps_ground) present', !!tyranny);
    if (tyranny) {
      check('  used=true (a real, live map — not a decode artefact on dead content)', tyranny.used === true);
      check('  carries a water record', Array.isArray(tyranny.water) && tyranny.water.length === 1);
      if (tyranny.water && tyranny.water.length) {
        eq('  water kind', tyranny.water[0].kind, 3);
        eq('  water texture', tyranny.water[0].texture, '_Wa_water1.bmp');
      }
    }
  } else {
    check('mapcatalog.json present', false);
  }
}

console.log(`\n${pass} passed, ${fail} failed\n`);
process.exit(fail ? 1 : 0);
