'use strict';
//
// Offline self-test: validates framing, the garbage layer, and struct packing
// against the probe-generated layout. Runs with no server.
//
//   node selftest.js
//
// This exists so that when the spike fails against a real server, framing bugs
// are already ruled out and the problem is genuinely protocol-level.
//

const P = require('./protocol');
const tea = require('./tea');

let pass = 0, fail = 0;
function check(name, cond, detail) {
  if (cond) { pass++; console.log(`  ok   ${name}`); }
  else { fail++; console.log(`  FAIL ${name}${detail ? ` — ${detail}` : ''}`); }
}
function eq(name, got, want) {
  check(name, got === want, `got ${got}, want ${want}`);
}

console.log('\nlayout (from MSVC probe)');
eq('sizeof NET_MSG_GENERIC', P.S.NET_MSG_GENERIC.size, 8);
eq('sizeof NET_CLIENT_VERSION', P.S.NET_CLIENT_VERSION.size, 16);
// 8 (hdr) + 4 (int) + 21 + 21 = 54, padded to 56 by natural alignment.
// s_NetGlobal.h has NO #pragma pack — contrast with SNETPC_GOTO below.
eq('sizeof THAI_NET_LOGIN_DATA', P.S.THAI_NET_LOGIN_DATA.size, 56);
eq('  szPassword before szUserid',
   P.S.THAI_NET_LOGIN_DATA.fields.szPassword.off <
   P.S.THAI_NET_LOGIN_DATA.fields.szUserid.off, true);
// GLContrlPcMsg.h IS #pragma pack(1): 8 + 4 + 12 + 12 = 36, no padding.
eq('sizeof SNETPC_GOTO (pack1)', P.S['GLMSG::SNETPC_GOTO'].size, 36);
eq('  vTarPos offset', P.S['GLMSG::SNETPC_GOTO'].fields.vTarPos.off, 24);

console.log('\nenums (live values, not the commented-out ones)');
eq('NET_MSG_LOBBY', P.C.NET_MSG_LOBBY, 1942);
eq('THAI_NET_MSG_LOGIN', P.E.THAI_NET_MSG_LOGIN, 2082);
check('NET_MSG_LOBBY is not the stale 2005',
      P.C.NET_MSG_LOBBY !== 2005,
      'picked up the commented-out NET_MSG_BASE+1013 definition');

console.log('\nframing');
{
  const body = Buffer.from([1, 2, 3, 4]);
  const w = P.frame(1234, body, null);
  eq('dwSize covers header+body', w.readUInt32LE(0), 12);
  eq('nType', w.readInt32LE(4), 1234);
  eq('total length', w.length, 12);

  const { packets, rest } = P.parse(w, false);
  eq('parsed count', packets.length, 1);
  eq('round-trip type', packets[0].type, 1234);
  check('round-trip body', packets[0].body.equals(body));
  eq('no trailing bytes', rest.length, 0);
}

console.log('\nframing — partial and batched delivery');
{
  const a = P.frame(11, Buffer.from([0xaa]), null);
  const b = P.frame(22, Buffer.from([0xbb, 0xcc]), null);
  const both = Buffer.concat([a, b]);

  // Split mid-packet: parser must buffer, not desync.
  const cut = a.length + 3;
  let r1 = P.parse(both.subarray(0, cut), false);
  eq('first packet available', r1.packets.length, 1);
  eq('partial second held back', r1.rest.length, 3);

  const r2 = P.parse(Buffer.concat([r1.rest, both.subarray(cut)]), false);
  eq('second packet completes', r2.packets.length, 1);
  eq('second type', r2.packets[0].type, 22);

  // Both at once.
  const r3 = P.parse(both, false);
  eq('batched parse', r3.packets.length, 2);
}

console.log('\nframing — desync detection');
{
  const bad = Buffer.alloc(8);
  bad.writeUInt32LE(3, 0); // < HEADER_SIZE, impossible
  bad.writeInt32LE(5, 4);
  let threw = false;
  try { P.parse(bad, false); } catch { threw = true; }
  check('implausible dwSize throws', threw);
}

console.log('\ngarbage layer');
{
  const g = new P.Garbage();
  const seen = [];
  for (let i = 0; i < 200; i++) seen.push(g.next().toString('latin1'));

  check('only table entries used',
        seen.every((s) => P.GARBAGE_DATA.includes(s)));
  // GetGarbageMsg() rejects a pick matching EITHER stored slot, so consecutive
  // packets can never carry the same filler.
  check('no two consecutive identical',
        seen.every((s, i) => i === 0 || s !== seen[i - 1]));
  check('more than one distinct value used', new Set(seen).size > 1);
}

console.log('\ngarbage — round trip through frame/parse');
{
  const g = new P.Garbage();
  const body = Buffer.from('PAYLOAD', 'latin1');
  const w = P.frame(77, body, g);

  check('wire is longer than body+header', w.length > 8 + body.length);
  eq('dwSize includes garbage', w.readUInt32LE(0), w.length);

  const { packets } = P.parse(w, true);
  eq('parsed one', packets.length, 1);
  check('garbage identified', packets[0].garbage !== null);
  check('body recovered intact', packets[0].body.equals(body),
        `got ${packets[0].body.toString('latin1')}`);
}

console.log('\nstruct packing');
{
  const b = P.allocBody('THAI_NET_LOGIN_DATA');
  eq('body size = struct - header', b.length, 56 - 8);

  P.writeInt32(b, 'THAI_NET_LOGIN_DATA', 'nChannel', 0);
  P.writeCharArray(b, 'THAI_NET_LOGIN_DATA', 'szUserid', 'testuser');
  P.writeCharArray(b, 'THAI_NET_LOGIN_DATA', 'szPassword', 'testpass');

  const uOff = P.fieldOff('THAI_NET_LOGIN_DATA', 'szUserid');
  const pOff = P.fieldOff('THAI_NET_LOGIN_DATA', 'szPassword');
  eq('userid readback', b.toString('latin1', uOff, uOff + 8), 'testuser');
  eq('password readback', b.toString('latin1', pOff, pOff + 8), 'testpass');
  eq('userid NUL-terminated', b[uOff + 8], 0);

  let threw = false;
  try {
    P.writeCharArray(b, 'THAI_NET_LOGIN_DATA', 'szUserid', 'x'.repeat(25));
  } catch { threw = true; }
  check('oversized string rejected', threw);
}

console.log('\nChina login layout (this server runs service_provider 3 = SP_CHINA)');
{
  eq('SP_CHINA', P.C.SP_CHINA, 3);
  eq('CHINA_NET_MSG_LOGIN', P.E.CHINA_NET_MSG_LOGIN, 2055);
  const c = P.S.CHINA_NET_LOGIN_DATA;
  // 8 + 4 + 11 + 25 + 25 = 73, padded to 76.
  eq('sizeof CHINA_NET_LOGIN_DATA', c.size, 76);
  eq('  szRandomPassword size', c.fields.szRandomPassword.size, 11);
  eq('  szPassword size', c.fields.szPassword.size, 25);
  eq('  szUserid size', c.fields.szUserid.size, 25);
  // Field order differs from the Thai struct: random, password, userid.
  check('  field order random < password < userid',
        c.fields.szRandomPassword.off < c.fields.szPassword.off &&
        c.fields.szPassword.off < c.fields.szUserid.off);
}

console.log('\nTEA — JS port vs vectors from the real minTea.cpp');
{
  // Fixed-length (USR_ID_LENGTH+1 = 21) vectors.
  for (const [sample, hex] of Object.entries(P.LAYOUT.teaVectors)) {
    const got = tea.encryptField(sample, P.C.USR_ID_LENGTH + 1).toString('hex');
    eq(`encryptField("${sample}", 21)`, got, hex);
  }

  // Variable-length vectors, keyed "<len>:<sample>". These cover the overflow
  // bail, which the fixed-length set does not exercise.
  for (const [key, hex] of Object.entries(P.LAYOUT.teaVectorsByLen)) {
    const idx = key.indexOf(':');
    const len = parseInt(key.slice(0, idx), 10);
    const sample = key.slice(idx + 1);
    const got = tea.encryptField(sample, len).toString('hex');
    eq(`encryptField("${sample}", ${len})`, got, hex);
  }

  // The bail must leave PLAINTEXT, not ciphertext. 19 chars in a 20-byte field
  // pads to 20 and needs 21 -> minTea copies nothing. This is exactly what the
  // China password field does, so getting it wrong breaks login silently.
  const md5ish = '5f4dcc3b5aa765d61d8';
  const bailed = tea.encryptField(md5ish, 20);
  eq('overflow bail leaves plaintext', bailed.toString('latin1', 0, 19), md5ish);
  eq('  and NUL-terminates', bailed[19], 0);

  // A field with room does get encrypted.
  const enc = tea.encryptField('xx11', 21);
  check('short input in roomy field IS encrypted',
        enc.toString('latin1', 0, 4) !== 'xx11');
  eq('  round-trips', tea.decryptString(enc, 21), 'xx11');
}

console.log('\nstruct codegen vs hand-curated probe entries');
{
  // Two independent paths produce layouts for the same structs: the curated
  // DUMP_FIELD calls written by hand in probe.cpp, and gen-structs.js which
  // extracts every struct mechanically. They must agree exactly. A mismatch
  // means one of them is measuring something other than what it claims —
  // which is the whole failure mode this tooling exists to prevent.
  const all = P.LAYOUT.allStructs || {};
  eq('codegen produced structs', Object.keys(all).length > 100, true);

  let compared = 0;
  let mismatches = 0;
  for (const [name, curated] of Object.entries(P.S)) {
    const gen = all[name];
    if (!gen) continue; // mirrored/pack(1) structs are not in s_NetGlobal.h
    compared++;
    if (gen.size !== curated.size) {
      mismatches++;
      console.log(`  FAIL ${name}: size ${gen.size} (codegen) vs ` +
                  `${curated.size} (curated)`);
      continue;
    }
    for (const [f, cf] of Object.entries(curated.fields)) {
      const gf = gen.fields[f];
      if (!gf) continue; // curated uses nested paths like "gscil.szServerIP"
      if (gf.off !== cf.off || gf.size !== cf.size) {
        mismatches++;
        console.log(`  FAIL ${name}.${f}: codegen off=${gf.off} size=${gf.size}, ` +
                    `curated off=${cf.off} size=${cf.size}`);
      }
    }
  }
  eq('structs cross-checked', compared > 5, true);
  eq('codegen/curated mismatches', mismatches, 0);

  // Spot-check the two structs that actually carry credentials and spawn data,
  // so a silent regression in either is caught offline.
  eq('CHINA_NET_LOGIN_DATA size', all.CHINA_NET_LOGIN_DATA.size, 76);
  eq('NET_LOGIN_FEEDBACK_DATA nResult off',
     all.NET_LOGIN_FEEDBACK_DATA.fields.nResult.off, 30);

  // The GLMSG headers now compile in the probe, so the real gameplay structs
  // are measured rather than mirrored. Assert the mirrors were right — if a
  // mirror ever drifts from upstream this fails offline instead of producing
  // corrupt packets on the wire.
  const realGoto = all['GLMSG::SNETPC_GOTO'];
  eq('real SNETPC_GOTO measured', !!realGoto, true);
  eq('  size matches mirror', realGoto.size, P.S['GLMSG::SNETPC_GOTO'].size);
  eq('  vCurPos matches mirror', realGoto.fields.vCurPos.off,
     P.S['GLMSG::SNETPC_GOTO'].fields.vCurPos.off);
  eq('  vTarPos matches mirror', realGoto.fields.vTarPos.off,
     P.S['GLMSG::SNETPC_GOTO'].fields.vTarPos.off);

  const realJoin = all['GLMSG::SNETLOBBY_CHARJOIN'];
  eq('real SNETLOBBY_CHARJOIN measured', !!realJoin, true);
  // The live server sent exactly this many bytes for the spawn packet.
  eq('  sizeof matches observed wire size', realJoin.size, 1030);
  eq('  vPos matches prefix mirror', realJoin.fields.vPos.off,
     P.S['GLMSG::SNETLOBBY_CHARJOIN_PREFIX'].fields.vPos.off);

  // Any of the 442 structs is addressable by name without hand-transcription.
  eq('resolver finds codegen-only struct',
     P.getStruct('NET_CHA_DEL').size > 0, true);
}

console.log(`\n${pass} passed, ${fail} failed\n`);
process.exit(fail ? 1 : 0);
