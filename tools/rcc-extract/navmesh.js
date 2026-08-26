'use strict';
//
// Navigation mesh reader (`NavigationMesh::LoadFile`,
// Lib_Engine/NaviMesh/NavigationSaveLoad.cpp:51-106).
//
// No version gating, no magic, no garbage reads — the payload is byte-identical
// across all nine `.wld` versions. Stream order:
//
//   i32                  vertex count
//   D3DXVECTOR3 * count  vertex pool (12 B each)
//   u32                  cell count
//   cell * count         188 B each
//   link table           per cell, per side 0..2: BOOL(4) [+ u32 LinkID if TRUE]
//
// The link table is the only variable-length part — it cannot be skipped by
// arithmetic, it has to be walked.
//
// For a ray-cast reimplementation only the vertex pool and each cell's
// m_Vertex[3] are needed: NavigationMesh::IsCollision (navagationtree.cpp:421)
// consults nothing else. The auxiliary per-cell fields serve LineOfSightTest
// and the A* build, so they are decoded only on request.
//
// The AABB tree is NOT stored — MakeAABBTree() rebuilds it at load time, so any
// reimplementation must build its own acceleration structure.
//
const LAYOUT = require('../layout-probe/layout.json');

// Strides come from the probe, never from arithmetic in this file. Line2D is 28
// rather than 24 because a trailing `mutable bool` takes 3 bytes of tail
// padding, so ~9 bytes of uninitialised memory are baked into every cell of
// every shipped map. The 188-byte record is a SUM of written field sizes —
// sizeof(NavigationCell) is 220 and would be the wrong number, because
// NavigationCell::LoadFile writes field by field and never blits the class.
const SZ = LAYOUT.structs.NAVMESH_SIZES.fields;
const CELL_RECORD = SZ.CELL_RECORD.size;   // 188
const VEC3 = SZ.D3DXVECTOR3.size;          // 12
const PLANE = SZ.Plane.size;               // 28
const LINE2D = SZ.Line2D.size;             // 28

// Field offsets within a cell record, in WRITE order — which is not the class
// declaration order (each field is a separate ReadBuffer call).
const CELL = (() => {
  let o = 0;
  const at = (n) => { const v = o; o += n; return v; };
  return {
    cellId: at(4),
    vertex: at(4 * 3),
    side: at(LINE2D * 3),
    plane: at(PLANE),
    centerPoint: at(VEC3),
    wallMidpoint: at(VEC3 * 3),
    wallDistance: at(4 * 3),
    size: o,
  };
})();

if (CELL.size !== CELL_RECORD) {
  throw new Error(`cell record mismatch: composed ${CELL.size}, probe ${CELL_RECORD}`);
}

const MAX_VERTICES = 8e6;
const MAX_CELLS = 8e6;

/**
 * @param {Buffer} buf   the whole (decrypted) .wld
 * @param {number} offset absolute file offset of the navmesh section
 * @param {{aux?:boolean}} [opts] aux: also decode plane/side/centre/wall fields
 * @returns {null|{vertices:Float32Array,cellIds:Uint32Array,corners:Uint32Array,
 *                 links:Int32Array,cellCount:number,vertexCount:number}}
 *   null when the section exists but holds no mesh (bExist == 0).
 */
function parse(buf, offset, opts = {}) {
  let p = offset;
  const need = (n, what) => {
    if (p + n > buf.length) throw new Error(`navmesh: ${what} runs past end of file`);
  };

  need(4, 'bExist');
  const bExist = buf.readUInt32LE(p); p += 4;
  if (bExist === 0) return null;
  if (bExist !== 1) throw new Error(`navmesh: bad bExist ${bExist}`);

  need(4, 'vertex count');
  const vertexCount = buf.readInt32LE(p); p += 4;
  if (vertexCount < 0 || vertexCount > MAX_VERTICES) {
    throw new Error(`navmesh: implausible vertex count ${vertexCount}`);
  }
  need(vertexCount * VEC3, 'vertex pool');
  // Copy rather than view: the source buffer is often a whole decrypted map and
  // holding a view would pin tens of MB per mesh.
  const vertices = new Float32Array(vertexCount * 3);
  for (let i = 0; i < vertexCount * 3; i++) {
    vertices[i] = buf.readFloatLE(p + i * 4);
  }
  p += vertexCount * VEC3;

  need(4, 'cell count');
  const cellCount = buf.readUInt32LE(p); p += 4;
  if (cellCount > MAX_CELLS) {
    throw new Error(`navmesh: implausible cell count ${cellCount}`);
  }
  need(cellCount * CELL_RECORD, 'cell records');

  const cellIds = new Uint32Array(cellCount);
  const corners = new Uint32Array(cellCount * 3);
  const aux = opts.aux ? {
    plane: new Float32Array(cellCount * 7),      // normal(3) point(3) distance
    centre: new Float32Array(cellCount * 3),
    wallMidpoint: new Float32Array(cellCount * 9),
    wallDistance: new Float32Array(cellCount * 3),
  } : null;

  const cellsAt = p;
  for (let c = 0; c < cellCount; c++) {
    const base = cellsAt + c * CELL_RECORD;
    cellIds[c] = buf.readUInt32LE(base + CELL.cellId);
    for (let k = 0; k < 3; k++) {
      corners[c * 3 + k] = buf.readUInt32LE(base + CELL.vertex + k * 4);
    }
    if (aux) {
      for (let k = 0; k < 7; k++) {
        aux.plane[c * 7 + k] = buf.readFloatLE(base + CELL.plane + k * 4);
      }
      for (let k = 0; k < 3; k++) {
        aux.centre[c * 3 + k] = buf.readFloatLE(base + CELL.centerPoint + k * 4);
        aux.wallDistance[c * 3 + k] = buf.readFloatLE(base + CELL.wallDistance + k * 4);
      }
      for (let k = 0; k < 9; k++) {
        aux.wallMidpoint[c * 9 + k] = buf.readFloatLE(base + CELL.wallMidpoint + k * 4);
      }
    }
  }
  p = cellsAt + cellCount * CELL_RECORD;

  // Link table: 3 sides per cell, each a BOOL and — only when true — an id.
  // -1 means "no neighbour" (a wall).
  const links = new Int32Array(cellCount * 3).fill(-1);
  for (let c = 0; c < cellCount; c++) {
    for (let side = 0; side < 3; side++) {
      need(4, 'link flag');
      const present = buf.readUInt32LE(p); p += 4;
      if (present) {
        need(4, 'link id');
        links[c * 3 + side] = buf.readUInt32LE(p); p += 4;
      }
    }
  }

  const result = { vertexCount, cellCount, vertices, cellIds, corners, links,
                   bytesRead: p - offset };
  if (aux) result.aux = aux;
  return result;
}

/**
 * Structural checks that a wrong stride would fail even though parsing
 * "succeeded" — the lesson from the mesh and animation readers.
 */
function validate(mesh) {
  const problems = [];
  for (let i = 0; i < mesh.corners.length; i++) {
    if (mesh.corners[i] >= mesh.vertexCount) {
      problems.push(`corner ${i} indexes vertex ${mesh.corners[i]} of ${mesh.vertexCount}`);
      break;
    }
  }
  for (let i = 0; i < mesh.links.length; i++) {
    const l = mesh.links[i];
    if (l !== -1 && l >= mesh.cellCount) {
      problems.push(`link ${i} references cell ${l} of ${mesh.cellCount}`);
      break;
    }
  }
  let nonFinite = 0;
  for (let i = 0; i < mesh.vertices.length; i++) {
    if (!Number.isFinite(mesh.vertices[i])) { nonFinite++; if (nonFinite > 3) break; }
  }
  if (nonFinite) problems.push(`${nonFinite}+ non-finite vertex components`);
  return problems;
}

/** Axis-aligned bounds of the vertex pool. */
function bounds(mesh) {
  const min = [Infinity, Infinity, Infinity];
  const max = [-Infinity, -Infinity, -Infinity];
  for (let i = 0; i < mesh.vertexCount; i++) {
    for (let a = 0; a < 3; a++) {
      const v = mesh.vertices[i * 3 + a];
      if (v < min[a]) min[a] = v;
      if (v > max[a]) max[a] = v;
    }
  }
  return { min, max };
}

module.exports = { parse, validate, bounds, CELL, CELL_RECORD, VEC3, PLANE, LINE2D };
