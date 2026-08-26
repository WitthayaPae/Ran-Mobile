'use strict';
//
// Phase 0's heartbeat item: stay connected for N minutes without a disconnect.
//
//   node soak.js --minutes 12
//
// Credentials come from ../spike/local.json, which is gitignored. Nothing here
// prints them.
//
// This is ONE client, idle, on a live production server. It is deliberately not
// a load test: reconnecting en masse against a server with real players on it is
// a different decision and is not made here.
//
// What it watches for, because "it stayed up" is not by itself informative:
//   * heartbeat requests seen, and answers sent
//   * the longest gap between heartbeats — a stall shows up here before it
//     shows up as a disconnect
//   * bytes/sec, so the idle cost measured in Phase 1 can be re-checked
//   * any state change away from `playing`
//
const fs = require('fs');
const path = require('path');
const { Session } = require('./session');

const cfgPath = path.join(__dirname, '..', 'spike', 'local.json');
if (!fs.existsSync(cfgPath)) {
  console.error('no ../spike/local.json — cannot run without a target');
  process.exit(2);
}
const cfg = JSON.parse(fs.readFileSync(cfgPath, 'utf8'));

const argv = process.argv.slice(2);
const argOf = (name, dflt) => {
  const i = argv.indexOf(name);
  return i >= 0 && argv[i + 1] !== undefined ? argv[i + 1] : dflt;
};
const minutes = parseFloat(argOf('--minutes', '12'));

const stats = {
  hbReq: 0,
  hbAns: 0,
  bytes: 0,
  frames: 0,
  states: [],
  maxGapMs: 0,
  lastHb: 0,
  disconnects: 0,
  errors: [],
};

(async () => {
  const started = Date.now();
  const session = new Session({ ...cfg, timeoutMs: 30000 });

  session.on('state', (s) => {
    stats.states.push({ at: Date.now() - started, state: s });
    if (s === 'closed' && Date.now() - started < minutes * 60000) stats.disconnects++;
  });
  session.on('error', (e) => stats.errors.push(String(e && e.message || e)));

  // Instrument the raw frame path rather than the decoded one, so a heartbeat
  // that arrives but fails to decode is still counted.
  session.on('frame', (info) => {
    stats.frames++;
    stats.bytes += info && info.size ? info.size : 0;
  });
  session.on('heartbeat', () => {
    const now = Date.now();
    if (stats.lastHb) stats.maxGapMs = Math.max(stats.maxGapMs, now - stats.lastHb);
    stats.lastHb = now;
    stats.hbReq++;
  });

  console.log(`connecting to ${cfg.host}:${cfg.port || 12004} for ${minutes} min...`);
  await session.run();

  if (!session.characters.length) {
    console.error('account has no characters — cannot enter the world');
    process.exit(3);
  }
  console.log(`entered world as "${session.characters[0].name}"`);

  const deadline = started + minutes * 60000;
  const tick = setInterval(() => {
    const mins = ((Date.now() - started) / 60000).toFixed(1);
    const bps = (stats.bytes / ((Date.now() - started) / 1000)).toFixed(0);
    console.log(`  t+${mins}m  state=${session.state}  frames=${stats.frames}  ` +
                `hb=${stats.hbReq}  ${bps} B/s`);
  }, 60000);

  await new Promise((r) => setTimeout(r, deadline - Date.now()));
  clearInterval(tick);

  const elapsedMin = (Date.now() - started) / 60000;
  const ok = session.state === 'playing' && stats.disconnects === 0;

  console.log('\n--- soak result ---');
  console.log(`elapsed        ${elapsedMin.toFixed(1)} min`);
  console.log(`final state    ${session.state}`);
  console.log(`frames         ${stats.frames}`);
  console.log(`heartbeats     ${stats.hbReq} seen`);
  console.log(`max hb gap     ${(stats.maxGapMs / 1000).toFixed(1)} s`);
  console.log(`idle traffic   ${(stats.bytes / (elapsedMin * 60)).toFixed(0)} B/s`);
  console.log(`disconnects    ${stats.disconnects}`);
  if (stats.errors.length) console.log(`errors         ${stats.errors.slice(0, 5).join(' | ')}`);
  console.log(`\n${ok ? 'PASS' : 'FAIL'} — stayed connected for ${elapsedMin.toFixed(1)} min`);

  session.close();
  process.exit(ok ? 0 : 1);
})().catch((e) => {
  console.error('soak failed:', e && e.message || e);
  process.exit(1);
});
