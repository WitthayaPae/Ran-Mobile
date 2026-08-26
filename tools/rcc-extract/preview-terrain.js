'use strict';
//
// Render a map's extracted terrain to a PNG, to LOOK at it.
//
//   node preview-terrain.js MAP --out FILE.png [--size 1024] [--side]
//
// Structural validation proves the numbers are self-consistent. It cannot prove
// the map is the map — a plausible-but-wrong decode produces clean numbers and
// nonsense geometry. Every other asset class here was checked by eye; this is
// the equivalent for terrain.
//
// Deliberately crude: a top-down orthographic z-buffer with flat per-triangle
// shading from the face normal. No textures, no perspective. The question it
// answers is "does this read as a coherent place", which does not need either.
//
const fs = require('fs');
const path = require('path');
const { RccArchive } = require('./rcc');
const SM = require('./staticmesh');
const png = require('./png');

const RAN = path.join(__dirname, '..', '..', '..', 'Ran');
const argv = process.argv.slice(2);
const has = (f) => argv.includes(f);
const val = (f, d) => { const i = argv.indexOf(f); return i === -1 ? d : argv[i + 1]; };

const mapName = argv[0] && !argv[0].startsWith('--') ? argv[0].toLowerCase() : null;
const outFile = val('--out', null);
const SIZE = parseInt(val('--size', '1024'), 10);
const side = has('--side');

if (!mapName || !outFile) {
  console.error('usage: node preview-terrain.js MAP --out FILE.png [--size N] [--side]');
  process.exit(2);
}

// -- find the sidecar ---------------------------------------------------------
let raw = null;
(function walk(dir) {
  if (raw) return;
  let items;
  try { items = fs.readdirSync(dir, { withFileTypes: true }); } catch { return; }
  for (const it of items) {
    if (raw) return;
    const p = path.join(dir, it.name);
    if (it.isDirectory()) { walk(p); continue; }
    if (!it.name.toLowerCase().endsWith('.rcc')) continue;
    let ar;
    try { ar = new RccArchive(p); } catch { continue; }
    for (const e of ar.entries) {
      if (path.basename(e.name).toLowerCase() === `${mapName}.wld0`) {
        raw = ar.read(e);
        return;
      }
    }
  }
})(RAN);

if (!raw) { console.error(`no ${mapName}.wld0 found under ${RAN}`); process.exit(1); }

const sm = SM.parse(raw);

// Bounds from the node boxes, never the header — the header AABB is stale in
// several maps (blue_zone1 misses its own geometry by 6,602 units).
const lo = [Infinity, Infinity, Infinity];
const hi = [-Infinity, -Infinity, -Infinity];
const tris = [];
for (const b of sm.buckets) for (const m of b.meshes) {
  if (!m.octree) continue;
  for (const n of m.octree.nodes) {
    const g = n.geometry;
    if (!g || !g.vertexCount) continue;
    for (let a = 0; a < 3; a++) {
      if (n.vMin[a] < lo[a]) lo[a] = n.vMin[a];
      if (n.vMax[a] > hi[a]) hi[a] = n.vMax[a];
    }
    tris.push(g);
  }
}
if (!Number.isFinite(lo[0])) { console.error('no geometry'); process.exit(1); }

// Project onto the two widest axes so a tall thin map is not rendered as a line.
// Top-down is X/Z with Y as depth; --side swaps to X/Y with Z as depth.
const [ax, ay, az] = side ? [0, 1, 2] : [0, 2, 1];
const spanX = hi[ax] - lo[ax] || 1;
const spanY = hi[ay] - lo[ay] || 1;
const scale = (SIZE - 2) / Math.max(spanX, spanY);
const W = Math.max(2, Math.round(spanX * scale) + 2);
const H = Math.max(2, Math.round(spanY * scale) + 2);

const rgba = Buffer.alloc(W * H * 4);
for (let i = 0; i < W * H; i++) rgba[i * 4 + 3] = 255;   // opaque background
const depth = new Float32Array(W * H).fill(Infinity);

const px = (v) => (v[ax] - lo[ax]) * scale + 1;
// Flip vertically: image Y grows downward, world Y/Z grows "up" on the page.
const py = (v) => H - 1 - ((v[ay] - lo[ay]) * scale + 1);

let drawn = 0;
const p0 = [0, 0, 0], p1 = [0, 0, 0], p2 = [0, 0, 0];

for (const g of tris) {
  for (let t = 0; t + 2 < g.indices.length; t += 3) {
    for (let k = 0; k < 3; k++) {
      const i = g.indices[t + k] * 3;
      const dst = k === 0 ? p0 : k === 1 ? p1 : p2;
      dst[0] = g.positions[i]; dst[1] = g.positions[i + 1]; dst[2] = g.positions[i + 2];
    }

    // Flat shade from the face normal against a fixed light, so slopes and
    // walls separate from flat ground. Height alone would wash the map out.
    const ux = p1[0] - p0[0], uy = p1[1] - p0[1], uz = p1[2] - p0[2];
    const vx = p2[0] - p0[0], vy = p2[1] - p0[1], vz = p2[2] - p0[2];
    let nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    const len = Math.hypot(nx, ny, nz) || 1;
    nx /= len; ny /= len; nz /= len;
    const lamb = Math.abs(nx * 0.35 + ny * 0.87 + nz * 0.35);
    const shade = 0.25 + 0.75 * lamb;

    // Tint by height so terraces and floors stay legible.
    const midDepth = (p0[az] + p1[az] + p2[az]) / 3;
    const hNorm = (midDepth - lo[az]) / ((hi[az] - lo[az]) || 1);
    const r = (60 + 195 * hNorm) * shade;
    const gg = (90 + 130 * hNorm) * shade;
    const bl = (130 + 60 * (1 - hNorm)) * shade;

    const x0 = px(p0), y0 = py(p0), x1 = px(p1), y1 = py(p1), x2 = px(p2), y2 = py(p2);
    const minX = Math.max(0, Math.floor(Math.min(x0, x1, x2)));
    const maxX = Math.min(W - 1, Math.ceil(Math.max(x0, x1, x2)));
    const minY = Math.max(0, Math.floor(Math.min(y0, y1, y2)));
    const maxY = Math.min(H - 1, Math.ceil(Math.max(y0, y1, y2)));
    const area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (!area) continue;
    drawn++;

    for (let y = minY; y <= maxY; y++) {
      for (let x = minX; x <= maxX; x++) {
        const cx = x + 0.5, cy = y + 0.5;
        // Barycentric coverage; sign-agnostic so winding does not matter here.
        const w0 = ((x1 - cx) * (y2 - cy) - (x2 - cx) * (y1 - cy)) / area;
        const w1 = ((x2 - cx) * (y0 - cy) - (x0 - cx) * (y2 - cy)) / area;
        const w2 = 1 - w0 - w1;
        if (w0 < 0 || w1 < 0 || w2 < 0) continue;
        const d = -(w0 * p0[az] + w1 * p1[az] + w2 * p2[az]);   // nearer = higher
        const o = y * W + x;
        if (d >= depth[o]) continue;
        depth[o] = d;
        rgba[o * 4] = Math.min(255, r);
        rgba[o * 4 + 1] = Math.min(255, gg);
        rgba[o * 4 + 2] = Math.min(255, bl);
      }
    }
  }
}

fs.writeFileSync(outFile, png.encode(W, H, rgba));
let covered = 0;
for (let i = 0; i < W * H; i++) if (depth[i] !== Infinity) covered++;
console.log(`${mapName}: ${W}x${H}, ${drawn.toLocaleString()} triangles rasterised, ` +
            `${(100 * covered / (W * H)).toFixed(1)}% of frame covered`);
console.log(`bounds ${lo.map((v) => v.toFixed(0)).join(',')} .. ${hi.map((v) => v.toFixed(0)).join(',')}`);
console.log(`-> ${outFile}`);
