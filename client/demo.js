'use strict';
//
// End-to-end exercise of the Phase 1 client core.
//
//   node demo.js                      # credentials from ../spike/local.json
//   node demo.js --walk 3             # take 3 steps after entering the world
//
// Same safety rule as the spike: a non-loopback host needs allowRemote.
//
const fs = require('fs');
const path = require('path');
const { Session } = require('./session');

const cfgPath = path.join(__dirname, '..', 'spike', 'local.json');
const cfg = fs.existsSync(cfgPath)
  ? JSON.parse(fs.readFileSync(cfgPath, 'utf8'))
  : {};

const argv = process.argv.slice(2);
const argOf = (name, dflt) => {
  const i = argv.indexOf(name);
  return i === -1 ? dflt : argv[i + 1];
};
const steps = Number(argOf('--walk', 2));

const t0 = Date.now();
const log = (...m) => console.log(`[${String(Date.now() - t0).padStart(6)}ms]`, ...m);

(async () => {
  const s = new Session({
    host: cfg.host,
    port: cfg.port || 12004,
    user: cfg.user,
    pass: cfg.pass,
    allowRemote: cfg.allowRemote,
    pwMode: cfg.pwMode || 'plain',
  });

  s.on('state', (st) => log(`state -> ${st}`));
  s.on('reconnecting', (i) =>
    log(`reconnecting ${i.role} attempt ${i.attempt} in ${i.delay}ms`));
  s.on('login', (fb) => log(`login ok (${fb.name})`));
  s.on('characters', (cs) =>
    log(`characters: ${cs.map((c) => `${c.name}#${c.charId}`).join(', ')}`));
  s.on('handoff', (h) => log(`field handoff -> ${h.ip}:${h.port} gaea=${h.gaeaId}`));
  s.on('spawn', (sp) =>
    log(`spawn map=${sp.mapId} at (${sp.pos.map((v) => v.toFixed(1)).join(', ')})`));
  s.on('ready', () => log('READY — character is in play'));
  s.on('warn', (w) => log('WARN', w));
  s.on('error', (e) => log('ERROR', e.message));

  try {
    await s.run();

    if (s.state !== 'playing') {
      log('did not reach playing state (no characters?)');
      s.close();
      process.exit(0);
    }

    // Walk, then read back what the server actually believes.
    for (let i = 0; i < steps; i++) {
      const from = s.position;
      const to = [from[0] + 20, from[1], from[2]];
      log(`step ${i + 1}: -> (${to.map((v) => v.toFixed(1)).join(', ')})`);
      s.moveTo(to);
      await new Promise((r) => setTimeout(r, 3000));
      const at = await s.syncPosition();
      log(`  server says (${at.map((v) => v.toFixed(1)).join(', ')})`);
    }

    const w = s.world;
    const inf=[...w.entities.values()].filter(e=>e.inferred).length;
    log(`world: ${w.count} in view (${inf} inferred), stats ` + JSON.stringify(w.stats));
    for (const e of w.nearest(s.position, 3)) {
      log(`  #${e.globId} native=${e.nativeId} dist=${e.dist.toFixed(1)}` + (e.inferred ? ' (inferred)' : ''));
    }
    log('done');
    s.close();
    process.exit(0);
  } catch (err) {
    log('FAILED:', err.message);
    s.close();
    process.exit(1);
  }
})();
