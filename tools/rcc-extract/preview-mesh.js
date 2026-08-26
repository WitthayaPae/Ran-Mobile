'use strict';
//
// Render an extracted `.rmesh` to a PNG, to LOOK at it.
//
//   node preview-mesh.js NAME --out FILE.png [--size 800] [--axis front|side|top]
//
// Same reasoning as `preview-terrain.js`: structural validation proves the
// numbers are self-consistent, not that the model is the model. A wrong vertex
// stride or a dropped bind pose still passes every cross-reference check.
//
// Flat-shaded z-buffer, no textures, no perspective. The question is "does this
// read as a character", which needs none of them.
//
const fs = require('fs');
const path = require('path');
const png = require('./png');

const ASSETS = path.join(__dirname, '..', '..', 'assets', 'meshes');
const argv = process.argv.slice(2);
const has = (f) => argv.includes(f);
const val = (f, d) => { const i = argv.indexOf(f); return i === -1 ? d : argv[i + 1]; };

const name = argv[0] && !argv[0].startsWith('--') ? argv[0] : null;
const outFile = val('--out', null);
const SIZE = parseInt(val('--size', '800'), 10);
const axis = val('--axis', 'front');

if (!name || !outFile) {
  console.error('usage: node preview-mesh.js NAME --out FILE.png [--size N] [--axis front|side|top]');
  process.exit(2);
}

const file = path.join(ASSETS, name.replace(/\.rmesh$/i, '') + '.rmesh');
if (!fs.existsSync(file)) { console.error(`no such mesh: ${file}`); process.exit(1); }
const b = fs.readFileSync(file);
if (b.toString('latin1', 0, 4) !== 'RMSH') { console.error('not an RMSH file'); process.exit(1); }

const skin = (b.readUInt32LE(8) & 1) !== 0;
const boneCount = b.readUInt32LE(12);
const meshCount = b.readUInt32LE(16);
const subCount = b.readUInt32LE(20);
const skinCount = b.readUInt32LE(24);
const vCount = b.readUInt32LE(28);
const iCount = b.readUInt32LE(32);
const strBytes = b.readUInt32LE(36);

const strAt = 44;
const boneAt = strAt + strBytes;
const meshAt = boneAt + boneCount * 72;
const subAt = meshAt + meshCount * 40;
const skinAt = subAt + subCount * 12;
const posAt = skinAt + skinCount * 72;
const idxAt = posAt + vCount * 12 + vCount * 12 + vCount * 8 +
              (skin ? vCount * 8 + vCount * 16 : 0);

// Gather triangles in the bind pose (what the file stores).
const lo = [Infinity, Infinity, Infinity];
const hi = [-Infinity, -Infinity, -Infinity];
const tris = [];
for (let m = 0; m < meshCount; m++) {
  const o = meshAt + m * 40;
  const vOff = b.readUInt32LE(o + 4), vCnt = b.readUInt32LE(o + 8);
  const iOff = b.readUInt32LE(o + 12), iCnt = b.readUInt32LE(o + 16);
  if (!vCnt || !iCnt) continue;
  const pos = new Float32Array(vCnt * 3);
  for (let v = 0; v < vCnt * 3; v++) pos[v] = b.readFloatLE(posAt + (vOff * 3 + v) * 4);
  for (let v = 0; v < vCnt; v++) {
    for (let a = 0; a < 3; a++) {
      const x = pos[v * 3 + a];
      if (x < lo[a]) lo[a] = x;
      if (x > hi[a]) hi[a] = x;
    }
  }
  const idx = new Uint32Array(iCnt);
  for (let k = 0; k < iCnt; k++) idx[k] = b.readUInt32LE(idxAt + (iOff + k) * 4);
  tris.push({ pos, idx });
}
if (!Number.isFinite(lo[0])) { console.error('no geometry'); process.exit(1); }

// front = X/Y with Z depth; side = Z/Y with X depth; top = X/Z with Y depth.
const AX = { front: [0, 1, 2], side: [2, 1, 0], top: [0, 2, 1] }[axis] || [0, 1, 2];
const [ax, ay, az] = AX;
const spanX = hi[ax] - lo[ax] || 1;
const spanY = hi[ay] - lo[ay] || 1;
const scale = (SIZE - 2) / Math.max(spanX, spanY);
const W = Math.max(2, Math.round(spanX * scale) + 2);
const H = Math.max(2, Math.round(spanY * scale) + 2);

const rgba = Buffer.alloc(W * H * 4);
for (let i = 0; i < W * H; i++) rgba[i * 4 + 3] = 255;
const depth = new Float32Array(W * H).fill(Infinity);

let drawn = 0;
const p0 = [0, 0, 0], p1 = [0, 0, 0], p2 = [0, 0, 0];
for (const t of tris) {
  for (let k = 0; k + 2 < t.idx.length; k += 3) {
    for (let c = 0; c < 3; c++) {
      const i = t.idx[k + c] * 3;
      const d = c === 0 ? p0 : c === 1 ? p1 : p2;
      d[0] = t.pos[i]; d[1] = t.pos[i + 1]; d[2] = t.pos[i + 2];
    }
    const ux = p1[0] - p0[0], uy = p1[1] - p0[1], uz = p1[2] - p0[2];
    const vx = p2[0] - p0[0], vy = p2[1] - p0[1], vz = p2[2] - p0[2];
    let nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    const len = Math.hypot(nx, ny, nz) || 1;
    nx /= len; ny /= len; nz /= len;
    const shade = 0.25 + 0.75 * Math.abs(nx * 0.4 + ny * 0.5 + nz * 0.77);

    const x0 = (p0[ax] - lo[ax]) * scale + 1, y0 = H - 1 - ((p0[ay] - lo[ay]) * scale + 1);
    const x1 = (p1[ax] - lo[ax]) * scale + 1, y1 = H - 1 - ((p1[ay] - lo[ay]) * scale + 1);
    const x2 = (p2[ax] - lo[ax]) * scale + 1, y2 = H - 1 - ((p2[ay] - lo[ay]) * scale + 1);
    const area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (!area) continue;
    drawn++;

    const minX = Math.max(0, Math.floor(Math.min(x0, x1, x2)));
    const maxX = Math.min(W - 1, Math.ceil(Math.max(x0, x1, x2)));
    const minY = Math.max(0, Math.floor(Math.min(y0, y1, y2)));
    const maxY = Math.min(H - 1, Math.ceil(Math.max(y0, y1, y2)));
    for (let y = minY; y <= maxY; y++) {
      for (let x = minX; x <= maxX; x++) {
        const cx = x + 0.5, cy = y + 0.5;
        const w0 = ((x1 - cx) * (y2 - cy) - (x2 - cx) * (y1 - cy)) / area;
        const w1 = ((x2 - cx) * (y0 - cy) - (x0 - cx) * (y2 - cy)) / area;
        const w2 = 1 - w0 - w1;
        if (w0 < 0 || w1 < 0 || w2 < 0) continue;
        const d = -(w0 * p0[az] + w1 * p1[az] + w2 * p2[az]);
        const o = y * W + x;
        if (d >= depth[o]) continue;
        depth[o] = d;
        rgba[o * 4] = Math.min(255, 210 * shade);
        rgba[o * 4 + 1] = Math.min(255, 200 * shade);
        rgba[o * 4 + 2] = Math.min(255, 185 * shade);
      }
    }
  }
}

fs.writeFileSync(outFile, png.encode(W, H, rgba));
console.log(`${name}: ${W}x${H}, ${meshCount} meshes, ${boneCount} bones, ` +
            `${vCount.toLocaleString()} verts, ${drawn.toLocaleString()} triangles drawn` +
            `${skin ? `, skinned (${skinCount} skin bones)` : ''}`);
console.log(`bounds ${lo.map((v) => v.toFixed(1)).join(',')} .. ${hi.map((v) => v.toFixed(1)).join(',')}`);
console.log(`-> ${outFile}`);
