'use strict';
//
// `DxAABBOctree` / `OBJOCTree` reader — the actual terrain geometry in `.wld0`.
//
// Despite the name it is a **binary** AABB tree, not an 8-way octree, stored
// pre-order with a presence flag per link (`NSOCTREE::LoadOctree`,
// NsOCTreeSaveLoad.cpp:1763-1780):
//
//   [BOOL exists][node][left subtree][right subtree]
//
// A zero flag terminates that branch and consumes nothing further.
//
// Reference: SOURCE/Lib_Engine/DxCommon/NsOCTreeSaveLoad.cpp
//
const FVF = require('./fvf');

const BOOL_SIZE = 4;            // BOOL is int; there is no 1-byte overload
const VEC3 = 12;
const DIRECTPOINTCOLOR = 16;    // 8 x WORD: alpha, day RGB, night RGB, temp
const AABB_OCTREE_VERSION = 0x0100;
const OBJOCTREE_VERSION = 0x10001;
const OBJOCTREE_VERSION_100 = 0x10000;
const AABB_NONINDEX = 0xffffffff;   // Collision.h:17 — interior-node sentinel

const LAND_TYPE = { DAYNIGHT_ON: 0, DAYNIGHT_OFF: 1, PIECE: 2 };

class Cursor {
  constructor(buf, at) { this.b = buf; this.p = at; }
  get left() { return this.b.length - this.p; }
  need(n, what) {
    if (this.p + n > this.b.length) {
      throw new Error(`octree: ${what} runs past end (need ${n}, have ${this.left})`);
    }
  }
  u32() { this.need(4, 'u32'); const v = this.b.readUInt32LE(this.p); this.p += 4; return v; }
  i32() { this.need(4, 'i32'); const v = this.b.readInt32LE(this.p); this.p += 4; return v; }
  bool() { return this.u32() !== 0; }
  f32() { this.need(4, 'f32'); const v = this.b.readFloatLE(this.p); this.p += 4; return v; }
  vec3() {
    this.need(VEC3, 'vec3');
    const v = [this.b.readFloatLE(this.p), this.b.readFloatLE(this.p + 4),
               this.b.readFloatLE(this.p + 8)];
    this.p += VEC3;
    return v;
  }
  bytes(n, what) { this.need(n, what); const s = this.b.subarray(this.p, this.p + n); this.p += n; return s; }
}

/**
 * The geometry payload of one node (`OBJOCTree::DynamicLoad`, :141-211).
 *
 * The version DWORD here is overloaded: anything BELOW 0x10000 is not a version
 * at all, it is the FVF (`OctreeLoadOLD`, :567). Treating it as a version
 * silently mis-strides every vertex, so the comparison order matters.
 */
function readGeometry(c, opts) {
  const present = c.bool();
  if (!present) return null;

  const dwVer = c.u32();
  let fvf;
  let landType = LAND_TYPE.PIECE;
  let vertCount;
  let faceCount;

  if (dwVer === OBJOCTREE_VERSION || dwVer === OBJOCTREE_VERSION_100) {
    // Identical on disk; they differ only in how the engine rebuilds in memory.
    fvf = c.u32();
    vertCount = c.u32();
    faceCount = c.u32();
    landType = c.u32();
  } else if (dwVer < OBJOCTREE_VERSION_100) {
    fvf = dwVer;                 // <- the overload
    vertCount = c.u32();
    faceCount = c.u32();
  } else {
    // Newer than anything this build knows. There is no size prefix on this
    // blob, so the stream cannot be resynchronised — bail rather than guess.
    throw new Error(`octree: unknown geometry version 0x${dwVer.toString(16)}`);
  }

  const stride = FVF.vertexSize(fvf);
  if (!stride) throw new Error(`octree: FVF 0x${fvf.toString(16)} has zero stride`);
  if (vertCount > 0x10000 || faceCount > 0x1000000) {
    throw new Error(`octree: implausible counts v=${vertCount} f=${faceCount}`);
  }

  const vertexBlob = c.bytes(vertCount * stride, 'vertex blob');
  const indexBlob = c.bytes(faceCount * 3 * 2, 'index blob');

  // Only the DAYNIGHT_ON bake carries the colour table (:308). Reading it
  // unconditionally would eat 16 bytes/vertex that are not there.
  let colors = null;
  if (dwVer >= OBJOCTREE_VERSION_100 && landType === LAND_TYPE.DAYNIGHT_ON) {
    colors = c.bytes(vertCount * DIRECTPOINTCOLOR, 'day/night colour table');
  }

  const g = { fvf, stride, landType, vertexCount: vertCount, faceCount };
  if (!opts.headerOnly) {
    const lay = FVF.layout(fvf);
    const positions = new Float32Array(vertCount * 3);
    for (let i = 0; i < vertCount; i++) {
      const o = i * stride + lay.position;
      positions[i * 3] = vertexBlob.readFloatLE(o);
      positions[i * 3 + 1] = vertexBlob.readFloatLE(o + 4);
      positions[i * 3 + 2] = vertexBlob.readFloatLE(o + 8);
    }
    const indices = new Uint16Array(faceCount * 3);
    for (let i = 0; i < indices.length; i++) indices[i] = indexBlob.readUInt16LE(i * 2);

    g.positions = positions;
    g.indices = indices;
    // The in-VB diffuse. For DAYNIGHT_ON land the colour thread overwrites it
    // from the day/night table, but every other landType keeps what the file
    // shipped (OBJOCTree::FrameMoveCOLOR is a no-op when m_pColor is NULL) —
    // and the land path renders with D3DRS_LIGHTING=FALSE, so for pieces this
    // diffuse IS the baked lighting. Dropping it is how the login trees lost
    // their shading.
    if (lay.diffuse != null) {
      const vbColors = new Uint32Array(vertCount);
      for (let i = 0; i < vertCount; i++) {
        vbColors[i] = vertexBlob.readUInt32LE(i * stride + lay.diffuse);
      }
      g.vbColors = vbColors;
    }
    // Authored normals. Foliage normals in RAN are NOT face normals — they
    // are authored to point up and outward so the leaves light like the
    // ground under them. Recalculating from the cross-plane geometry makes
    // half of every tree face away from the sun and drop to the ambient
    // floor, which reads as a black tree.
    if (lay.normal != null) {
      const vbNormals = new Float32Array(vertCount * 3);
      for (let i = 0; i < vertCount; i++) {
        const o = i * stride + lay.normal;
        vbNormals[i * 3] = vertexBlob.readFloatLE(o);
        vbNormals[i * 3 + 1] = vertexBlob.readFloatLE(o + 4);
        vbNormals[i * 3 + 2] = vertexBlob.readFloatLE(o + 8);
      }
      g.vbNormals = vbNormals;
    }
    if (lay.texcoords.length) {
      const uvs = new Float32Array(vertCount * 2);
      const uo = lay.texcoords[0].offset;
      for (let i = 0; i < vertCount; i++) {
        uvs[i * 2] = vertexBlob.readFloatLE(i * stride + uo);
        uvs[i * 2 + 1] = vertexBlob.readFloatLE(i * stride + uo + 4);
      }
      g.uvs = uvs;
    }
    if (colors) g.dayNightColors = Buffer.from(colors);
  }
  return g;
}

/**
 * The collision sub-tree (`DxAABBNode::LoadFile`, Collision.cpp:1236).
 * Fixed 32-byte node, recursive, and crucially **no version and no size**, so it
 * must be walked exactly — it cannot be skipped.
 */
function skipCollisionTree(c, faces) {
  // The two child flags are NOT adjacent: the left subtree is written inline
  // between them —
  //   [max][min][dwFace][BOOL hasLeft][left...][BOOL hasRight][right...]
  // so a loop that reads both flags together desyncs on any node with a left
  // child. Model the interleaving with an explicit frame stack rather than a
  // counter, and avoid real recursion since depth is data-controlled.
  const stack = [{ stage: 0 }];
  let nodes = 0;
  while (stack.length) {
    const f = stack[stack.length - 1];
    if (f.stage === 0) {
      const rec = c.bytes(VEC3 * 2 + 4, 'collision node');   // vMax, vMin, dwFace
      nodes++;
      // `dwFace` is a TRIANGLE index into the owning mesh's index buffer, not
      // geometry (`DxOctreeMesh::IsCollision` does `pwIndexB + dwFace * 3`).
      // Interior nodes carry AABB_NONINDEX; only leaves name a face.
      if (faces) {
        const dwFace = rec.readUInt32LE(VEC3 * 2);
        if (dwFace !== AABB_NONINDEX) faces.add(dwFace);
      }
      f.stage = 1;
    } else if (f.stage === 1) {
      f.stage = 2;
      if (c.bool()) stack.push({ stage: 0 });     // left subtree, inline
    } else if (f.stage === 2) {
      f.stage = 3;
      if (c.bool()) stack.push({ stage: 0 });     // right subtree, inline
    } else {
      stack.pop();
    }
  }
  return nodes;
}

/**
 * Walk `DxAABBOctree::Load` at `offset`.
 * @param {Buffer} buf   decrypted .wld0
 * @param {number} offset
 * @param {{headerOnly?:boolean}} [opts] headerOnly skips vertex/index decode
 * @returns {{vMax,vMin,nodes:object[],skipped:number}}
 */
function parse(buf, offset, opts = {}) {
  const c = new Cursor(buf, offset);
  const version = c.u32();
  const bufferSize = c.u32();
  const blockEnd = c.p + bufferSize;

  if (version !== AABB_OCTREE_VERSION) {
    // The engine logs and seeks past. Do the same — the size makes it safe.
    return { version, vMax: null, vMin: null, nodes: [], skipped: 1,
             end: blockEnd, walkEnd: blockEnd };
  }

  const vMax = c.vec3();
  const vMin = c.vec3();
  const nodes = [];
  let skipped = 0;

  if (c.bool()) {
    // Iterative pre-order walk. Depth is data-controlled, so no recursion.
    let pending = 1;
    while (pending > 0) {
      pending--;
      if (!c.bool()) continue;          // absent node: nothing follows for it

      const nodeMax = c.vec3();
      const nodeMin = c.vec3();
      const nodeSize = c.u32();
      const dataStart = c.p;

      let geometry = null;
      try {
        geometry = readGeometry(c, opts);
        if (geometry) skipCollisionTreeIfPresent(c, geometry);
      } catch (err) {
        skipped++;                      // unreadable payload; the size saves us
      }
      // ALWAYS re-seek. The engine does not (:117-121) and neither does its
      // error path — a latent desync we deliberately do not reproduce.
      c.p = dataStart + nodeSize;

      nodes.push({ vMax: nodeMax, vMin: nodeMin, geometry });
      pending += 2;                     // left and right links follow
    }
  }

  // `end` is what callers seek to — the block size is authoritative, since
  // DxAABBOctree::Save closes the block AFTER the whole tree (:706-721).
  // `walkEnd` is where our own walk actually landed. They must agree; when they
  // do not, the tree walk is wrong and `end` is silently hiding it.
  return { version, vMax, vMin, nodes, skipped, end: blockEnd, walkEnd: c.p };

  // The collision BVH belongs to the geometry it indexes — `dwFace` is a
  // triangle index into THAT node's index buffer — so the leaf set is attached
  // there rather than accumulated separately.
  function skipCollisionTreeIfPresent(cur, geometry) {
    if (!cur.bool()) return;
    const faces = opts.collisionFaces ? new Set() : null;
    const n = skipCollisionTree(cur, faces);
    geometry.collisionNodes = n;
    if (faces) geometry.collisionFaces = faces;
  }
}

module.exports = {
  parse, readGeometry, skipCollisionTree, Cursor,
  LAND_TYPE, AABB_OCTREE_VERSION, OBJOCTREE_VERSION, DIRECTPOINTCOLOR, BOOL_SIZE,
  AABB_NONINDEX,
};
