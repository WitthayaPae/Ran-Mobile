'use strict';
//
// Offline tests for the Phase 1 client core. No live server required — the
// reconnect tests run against a throwaway loopback server so backoff behaviour
// is verified without waiting on a real outage.
//
//   node test.js
//
const net = require('net');
const { Connection } = require('./connection');
const M = require('./messages');
const P = require('../spike/protocol');

let pass = 0, fail = 0;
const check = (name, cond, detail) => {
  if (cond) { pass++; console.log(`  ok   ${name}`); }
  else { fail++; console.log(`  FAIL ${name}${detail ? ` — ${detail}` : ''}`); }
};
const eq = (name, got, want) => check(name, got === want, `got ${got}, want ${want}`);

function listen(onConn) {
  return new Promise((resolve) => {
    const srv = net.createServer(onConn);
    srv.listen(0, '127.0.0.1', () => resolve({ srv, port: srv.address().port }));
  });
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
  console.log('\nmessage builders (sizes from the probe, not literals)');
  {
    const login = M.chinaLogin('user', 'pass');
    eq('chinaLogin body size',
       login.body.length, P.S.CHINA_NET_LOGIN_DATA.size - P.HEADER_SIZE);
    eq('chinaLogin type', login.type, P.E.CHINA_NET_MSG_LOGIN);

    // Whether the password is encrypted depends on its LENGTH, which is easy
    // to get wrong. minTea pads to a multiple of 4 (minimum 8) and then appends
    // a NUL; if that total exceeds nMaxLength (20 here) it copies nothing and
    // the buffer stays plaintext. So:
    //   <= 16 chars -> padded <= 16, +1 fits  -> ENCRYPTED
    //   >= 17 chars -> padded  = 20, +1 = 21  -> bail, PLAINTEXT
    // The 19-char MD5 hex used by pwMode 'md5' always lands in the second case;
    // a short human password lands in the first.
    const F = P.S.CHINA_NET_LOGIN_DATA.fields;
    const off = F.szPassword.off - P.HEADER_SIZE;
    check('short password IS encrypted',
          login.body.toString('latin1', off, off + 4) !== 'pass');

    const md5login = M.chinaLogin('user', 'pass', 0, 'md5');
    const hex = require('crypto').createHash('md5')
      .update('pass', 'latin1').digest('hex').slice(0, 19);
    eq('19-char md5 hits the bail and stays plaintext',
       md5login.body.toString('latin1', off, off + 19), hex);

    // Round-trip through the same primitive the server uses.
    const tea = require('../spike/tea');
    const field = login.body.subarray(off, off + F.szPassword.size);
    eq('short password round-trips',
       tea.decryptString(field.subarray(0, P.C.USR_PASS_LENGTH),
                         P.C.USR_PASS_LENGTH), 'pass');

    const g = M.goto([1, 2, 3], [4, 5, 6]);
    eq('goto body size', g.body.length, 36 - P.HEADER_SIZE);
    eq('goto reads back vTarPos.x',
       g.body.readFloatLE(P.fieldOff('GLMSG::SNETPC_GOTO', 'vTarPos')), 4);
    eq('landIn/ready are bare headers', M.landIn().body.length + M.ready().body.length, 0);
  }

  console.log('\nbackoff');
  {
    const c = new Connection({ host: '127.0.0.1', port: 1, role: 'login',
                               backoffBaseMs: 100, backoffMaxMs: 1000 });
    // Full jitter: delay is uniform in [0, ceiling), and ceiling doubles per
    // attempt up to the cap. Assert the bound, not an exact value.
    for (const [attempt, ceiling] of [[0, 100], [1, 200], [4, 1000], [20, 1000]]) {
      c.attempt = attempt;
      let max = 0;
      for (let i = 0; i < 400; i++) max = Math.max(max, c.nextDelay());
      check(`attempt ${attempt}: delay < ${ceiling}`, max < ceiling, `saw ${max}`);
      check(`attempt ${attempt}: uses most of the range`, max > ceiling * 0.5,
            `max ${max} of ${ceiling}`);
    }
    c.attempt = 0;
    const zero = Array.from({ length: 50 }, () => c.nextDelay());
    check('jitter varies', new Set(zero).size > 1);
  }

  console.log('\nreconnect against a real socket');
  {
    let connects = 0;
    const { srv, port } = await listen((sock) => { connects++; sock.destroy(); });

    const c = new Connection({ host: '127.0.0.1', port, role: 'login',
                               backoffBaseMs: 20, backoffMaxMs: 40 });
    await c.connect();
    await sleep(400); // server hangs up immediately; expect repeated retries
    check('auto-reconnects after server drops', connects > 2, `${connects} connects`);
    c.destroy();
    await sleep(100);
    const settled = connects;
    await sleep(200);
    eq('stops reconnecting after destroy()', connects, settled);
    srv.close();
  }

  console.log('\nno reconnect when disabled');
  {
    let connects = 0;
    const { srv, port } = await listen((sock) => { connects++; sock.destroy(); });
    const c = new Connection({ host: '127.0.0.1', port, role: 'login',
                               autoReconnect: false, backoffBaseMs: 10 });
    await c.connect();
    await sleep(250);
    eq('single connection only', connects, 1);
    c.destroy();
    srv.close();
  }

  console.log('\nCOMPRESS unwrapping (batched and LZO)');
  {
    // Batched: bCompress=false means several messages concatenated.
    const inner = Buffer.concat([
      P.frame(1234, Buffer.from([1, 2, 3, 4]), null),
      P.frame(5678, Buffer.from([9]), null),
    ]);
    const wrapBody = Buffer.concat([
      Buffer.alloc(P.S.NET_COMPRESS.size - P.HEADER_SIZE), // bCompress=0 + pad
      inner,
    ]);

    let srvSock;
    const { srv, port } = await listen((s) => { srvSock = s; });
    const c = new Connection({ host: '127.0.0.1', port, role: 'login',
                               autoReconnect: false });
    const seen = [];
    c.on('message', (p) => seen.push(p.type));
    await c.connect();
    await sleep(50);
    srvSock.write(P.frame(P.E.NET_MSG_COMPRESS, wrapBody, null));
    await sleep(150);

    eq('wrapper is unwrapped, not surfaced', seen.includes(P.E.NET_MSG_COMPRESS), false);
    eq('both inner messages delivered', seen.length, 2);
    eq('inner type 1', seen[0], 1234);
    eq('inner type 2', seen[1], 5678);
    c.destroy();
    srv.close();
  }

  console.log('\ndesync is surfaced, not silently resynced');
  {
    let srvSock;
    const { srv, port } = await listen((s) => { srvSock = s; });
    const c = new Connection({ host: '127.0.0.1', port, role: 'login',
                               autoReconnect: false });
    let desync = null;
    c.on('desync', (e) => { desync = e; });
    await c.connect();
    await sleep(50);
    const bad = Buffer.alloc(8);
    bad.writeUInt32LE(3, 0); // dwSize below the header size is impossible
    bad.writeInt32LE(7, 4);
    srvSock.write(bad);
    await sleep(150);
    check('desync event emitted', desync !== null);
    c.destroy();
    srv.close();
  }

  console.log('\nworld model (entity lifecycle)');
  {
    const { World, OFFSETS } = require('./world');
    const A = P.LAYOUT.allEnums;
    const S = P.LAYOUT.allStructs;

    // Offsets are composed from two compiler-measured values; assert the
    // composition against a REAL captured DROP_CROW body rather than trusting
    // the arithmetic. This is the exact prefix seen on the wire.
    const real = Buffer.from(
      '35000100' + '08000000' + 'f10b0000' + '7d030000' +
      'd25eec40' + '79d99fc3' + '53088bc5', 'hex');
    eq('captured packet: nativeId', real.readUInt32LE(OFFSETS.DROP.nativeId), 65589);
    eq('captured packet: mapId 8', real.readUInt32LE(4), 8);
    eq('captured packet: globId', real.readUInt32LE(OFFSETS.DROP.globId), 893);

    const dropBody = (globId, nativeId, pos) => {
      const b = Buffer.alloc(S['GLMSG::SNETDROP_CROW'].size - P.HEADER_SIZE);
      b.writeUInt32LE(nativeId, OFFSETS.DROP.nativeId);
      b.writeUInt32LE(globId, OFFSETS.DROP.globId);
      for (let i = 0; i < 3; i++) b.writeFloatLE(pos[i], OFFSETS.DROP.pos + i * 4);
      b.writeUInt32LE(100, OFFSETS.DROP.hp);
      return b;
    };
    const moveBody = (globId, cur, tar) => {
      const b = Buffer.alloc(S['GLMSG::SNETCROW_MOVETO'].size - P.HEADER_SIZE);
      b.writeUInt32LE(globId, OFFSETS.MOVE.globId);
      for (let i = 0; i < 3; i++) b.writeFloatLE(cur[i], OFFSETS.MOVE.cur + i * 4);
      for (let i = 0; i < 3; i++) b.writeFloatLE(tar[i], OFFSETS.MOVE.tar + i * 4);
      return b;
    };
    const outBody = (ids) => {
      const b = Buffer.alloc(OFFSETS.OUT.list + ids.length * OFFSETS.OUT.stride);
      b.writeUInt8(ids.length, OFFSETS.OUT.num);
      ids.forEach((id, i) => {
        const o = OFFSETS.OUT.list + i * OFFSETS.OUT.stride;
        b.writeUInt16LE(0, o + S.STARID.fields.wCrow.off);
        b.writeUInt16LE(id & 0xffff, o + S.STARID.fields.wID.off);
      });
      return b;
    };

    const w = new World();
    const events = [];
    w.on('enter', (e) => events.push(['enter', e.globId]));
    w.on('move', (e) => events.push(['move', e.globId]));
    w.on('leave', (e) => events.push(['leave', e.globId]));

    w.handle({ type: A.NET_MSG_GCTRL_DROP_CROW, body: dropBody(10, 777, [0, 0, 0]) });
    w.handle({ type: A.NET_MSG_GCTRL_DROP_CROW, body: dropBody(11, 888, [50, 0, 0]) });
    eq('two entities tracked', w.count, 2);
    eq('nativeId decoded', w.entities.get(10).nativeId, 777);

    w.handle({ type: A.NET_MSG_GCTRL_CROW_MOVETO,
               body: moveBody(10, [5, 0, 0], [30, 0, 0]) });
    eq('position updated', w.entities.get(10).pos[0], 5);
    eq('target recorded', w.entities.get(10).target[0], 30);
    eq('move counted', w.stats.moved, 1);

    // Movement for an unannounced entity must not be dropped silently.
    w.handle({ type: A.NET_MSG_GCTRL_CROW_MOVETO,
               body: moveBody(99, [1, 2, 3], [4, 5, 6]) });
    eq('unknown mover tracked', w.count, 3);
    eq('  flagged as inferred', w.entities.get(99).inferred, true);
    eq('  counted separately', w.stats.unknownMove, 1);

    eq('nearest() orders by distance', w.nearest([0, 0, 0], 1)[0].globId, 99);

    w.handle({ type: A.NET_MSG_GCTRL_DROP_OUT, body: outBody([10, 11]) });
    eq('entities removed', w.count, 1);
    eq('remove counted', w.stats.removed, 2);
    eq('event order', events.map((e) => e[0]).join(','),
       'enter,enter,move,enter,leave,leave');

    // A truncated batch must not read past the buffer.
    const short = outBody([10]).subarray(0, OFFSETS.OUT.list + 2);
    short.writeUInt8(200, OFFSETS.OUT.num); // lies about the count
    let threw = false;
    try { w.handle({ type: A.NET_MSG_GCTRL_DROP_OUT, body: short }); }
    catch { threw = true; }
    check('truncated DROP_OUT does not throw', !threw);

    eq('unrelated messages ignored',
       w.handle({ type: 12345, body: Buffer.alloc(4) }), false);
  }

  console.log(`\n${pass} passed, ${fail} failed\n`);
  process.exit(fail ? 1 : 0);
})();
