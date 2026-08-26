'use strict';
//
// Emit the machine-readable map-id -> scene catalog the mobile client resolves a
// server spawn/gate against.
//
//   node extract-mapcatalog.js                 # -> Resources/mapcatalog.json
//   node extract-mapcatalog.js --out PATH      # custom path (relative to repo root)
//   node extract-mapcatalog.js --list          # print id -> scene, do not write
//
// Two hops, both from the SHIPPED Ran/ deploy tree (not CLIENT/):
//   1. Ran/data/glogic/GLogic.rcc      -> mapslist.mst   (map id -> .lev + meta)
//   2. Ran/data/glogic/level/Level.rcc -> each .lev       (.lev  -> .wld -> scene)
//
// Optionally merges per-scene fog (Assets/Ran/fog.json, from extract-fog.js) and
// sky/cloud (Assets/Ran/sky.json, from extract-sky.js) so the runtime map loader
// can apply the map's own fog and sky without a second lookup.
//
// The catalog keeps EVERY map (121), used or not, so a spawn on any of them
// resolves; the Unity side decides which have a built scene and falls back
// gracefully for the rest.
//
const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc');
const MC = require('./mapcatalog');

const base = path.resolve(__dirname, '..', '..', '..');
const RAN = path.join(base, 'Ran');
const GLOGIC = path.join(RAN, 'data', 'glogic', 'GLogic.rcc');
const LEVEL = path.join(RAN, 'data', 'glogic', 'level', 'Level.rcc');
const FOG = path.join(base, 'MOBILE', 'unity', 'RanMobile', 'Assets', 'Ran', 'fog.json');
const SKY = path.join(base, 'MOBILE', 'unity', 'RanMobile', 'Assets', 'Ran', 'sky.json');
const WATER = path.join(base, 'MOBILE', 'unity', 'RanMobile', 'Assets', 'Ran', 'water.json');
const DEFAULT_OUT = path.join('MOBILE', 'unity', 'RanMobile', 'Assets', 'Ran',
                              'Resources', 'mapcatalog.json');

/** Read mapslist.mst out of GLogic.rcc and parse it. */
function readMapList() {
  const arc = new RccArchive(GLOGIC);
  const entry = arc.entries.find((e) => /(^|[\\/])mapslist\.mst$/i.test(e.name));
  if (!entry) throw new Error('mapslist.mst not found in GLogic.rcc');
  return MC.parseMapList(arc.read(entry));
}

/** A case-insensitive basename -> entry index for Level.rcc. */
function levelIndex() {
  const arc = new RccArchive(LEVEL);
  const byName = new Map();
  for (const e of arc.entries) {
    const bn = e.name.replace(/\\/g, '/').split('/').pop().toLowerCase();
    byName.set(bn, e);
  }
  return { arc, byName };
}

/** fog.json (optional) keyed by scene name. */
function readFog() {
  if (!fs.existsSync(FOG)) return null;
  try { return JSON.parse(fs.readFileSync(FOG, 'utf8')).maps || null; }
  catch { return null; }
}

/** sky.json (optional) keyed by scene name. */
function readSky() {
  if (!fs.existsSync(SKY)) return null;
  try { return JSON.parse(fs.readFileSync(SKY, 'utf8')).maps || null; }
  catch { return null; }
}

/** water.json (optional) keyed by scene name, from extract-mapobj.js/water.js. */
function readWater() {
  if (!fs.existsSync(WATER)) return null;
  try { return JSON.parse(fs.readFileSync(WATER, 'utf8')).maps || null; }
  catch { return null; }
}

function build() {
  const list = readMapList();
  const { arc, byName } = levelIndex();
  const fog = readFog();
  const sky = readSky();
  const water = readWater();

  let resolved = 0, guarded = 0, fellBack = 0, foggy = 0, skied = 0, watered = 0;
  const maps = list.maps.map((m) => {
    const levBn = m.lev.replace(/\\/g, '/').split('/').pop().toLowerCase();
    let scene = MC.sceneFromLev(m.lev);   // fallback: .lev basename
    let wld = '';
    const e = byName.get(levBn);
    if (e) {
      try {
        const r = MC.resolveWld(arc.read(e));
        if (r.wld) {
          wld = r.wld;
          scene = MC.sceneFromWld(r.wld);
          resolved++;
          if (r.ok) guarded++;
        } else fellBack++;
      } catch { fellBack++; }
    } else fellBack++;

    const entry = {
      id: m.id, main: m.main, sub: m.sub,
      scene, lev: m.lev, wld,
      name: m.name, bgm: m.bgm, loading: m.loading,
      used: m.used, fieldSID: m.fieldSID,
      peace: m.flags.peace, pk: m.flags.pk, freePK: m.flags.freePK,
      itemDrop: m.flags.itemDrop, restart: m.flags.restart, instant: m.flags.instant,
    };

    if (fog && fog[scene]) {
      const f = fog[scene];
      entry.fog = {
        start: f.start, end: f.end,
        day: f.day ? { r: f.day.r, g: f.day.g, b: f.day.b } : null,
      };
      foggy++;
    }

    if (sky && sky[scene] && sky[scene].hasSky) {
      const s = sky[scene];
      entry.sky = {
        skyEnable: s.skyEnable, cloudEnable: s.cloudEnable,
        radioAxis: s.radioAxis, axisValue: s.axisValue,
      };
      skied++;
    }

    // Trimmed to what a runtime renderer needs — the full decode (flags,
    // bump/reflection alpha, dark-vs-flash split) stays in mapwater.json for
    // anyone extending the renderer; the catalog carries what RanMapLoader
    // actually draws with today. See water.js for the field meanings.
    if (water && water[scene] && water[scene].length) {
      entry.water = water[scene].map((w) => {
        const base = { kind: w.kind, velocity: w.velocity,
                       color: w.color, texture: w.textureDark || w.texture };
        if (w.kind === 3) return { ...base, min: w.min, max: w.max, darkVel: w.darkVel };
        return {
          ...base, sizeX: w.sizeX, sizeZ: w.sizeZ,
          col: w.col, row: w.row, waveCycle: w.waveCycle,
          heightChange: w.heightChange,
          frameMatrix: w.frameMatrix,
        };
      });
      watered++;
    }
    return entry;
  });

  return {
    stats: { count: list.count, version: list.version, resolved, guarded, fellBack, foggy, skied, watered },
    catalog: { count: maps.length, maps },
  };
}

const { stats, catalog } = build();

console.log(`maps: ${catalog.count}  (mapslist v0x${stats.version.toString(16)})`);
console.log(`.wld resolved: ${stats.resolved}  (${stats.guarded} passed the head block guard), ` +
            `fell back to .lev name: ${stats.fellBack}`);
console.log(`fog merged: ${stats.foggy}`);
console.log(`sky merged: ${stats.skied}`);
console.log(`water merged: ${stats.watered}`);

if (process.argv.includes('--list')) {
  for (const m of catalog.maps) {
    if (!m.used && process.argv.includes('--used')) continue;
    console.log(`  ${String(m.id).padStart(6)}  ${m.scene.padEnd(24)} ` +
                `${m.used ? 'used' : '    '}  ${m.name}`);
  }
}

const at = process.argv.indexOf('--out');
const outRel = at > 0 ? process.argv[at + 1] : DEFAULT_OUT;
if (!process.argv.includes('--list') || at > 0) {
  const outAbs = path.isAbsolute(outRel) ? outRel : path.join(base, outRel);
  fs.mkdirSync(path.dirname(outAbs), { recursive: true });
  fs.writeFileSync(outAbs, JSON.stringify(catalog));
  console.log(`wrote ${outRel}`);
}

module.exports = { build };
