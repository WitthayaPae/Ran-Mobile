'use strict';
//
// Map object placement — the `DxOctree` scene graph inside every `.wld`.
//
// This is what puts the extracted `.x` meshes into the world. The `.wld0`
// sidecar holds terrain; this holds everything standing on it.
//
//   DxOctree::LoadFile        DxOctreeSaveLoad.cpp:60
//     BOOL   m_bSubDivided
//     vec3   vMax, vMin
//     DWORD  m_DataAddress    absolute offset of the DxFrame payload
//     DWORD  m_DataSize       payload size, INCLUDING its leading BOOL
//     <payload>               BOOL bExist, then DxFrame if set
//     8x [ BOOL bExist -> child DxOctree ]
//
// `m_DataSize` is the whole reason this is tractable: the engine's own dynamic
// loading path skips the payload with it (`SetOffSet(CurPos + m_DataSize)`), so
// the tree structure can be walked without decoding a single DxFrame — and a
// DxFrame that fails to decode costs one node, not the rest of the map.
//
//   DxFrame::LoadFile         DxFrameSaveLoad.cpp:186
//     vec3   vTreeMax, vTreeMin
//     D3DXMATRIXA16 matRot, matRotOrig, matCombined     <- 64 bytes each
//     BOOL -> [int len][len chars]   name
//     BOOL -> DxMeshes
//     DWORD count -> effects
//     DWORD count -> effects
//     BOOL -> sibling DxFrame        (recursive)
//     BOOL -> firstChild DxFrame     (recursive)
//
// Sizes come from the layout probe, never from arithmetic here:
// `D3DXMATRIXA16` is 64, `D3DMATERIALQ` 68, `D3DEXMATERIAL` 8, so the fixed
// DxFrame prefix is 216 bytes.
//
// **Strings here are NOT the CSerialFile std::string convention.** DxFrame and
// DxMeshes read `int StrLength` then exactly that many chars with ReadBuffer,
// and append the NUL themselves — so the length EXCLUDES the terminator. The
// `.wld0` path uses the opposite convention (length includes the NUL). Mixing
// them up shifts every following field by one byte.
//
const LAYOUT = require('../layout-probe/layout.json');
const FVF = require('./fvf');
const OCT = require('./octree');
const WATER = require('./water');

const SZ = LAYOUT.structs.MAPOBJ_SIZES.fields;
const MATRIX = SZ.D3DXMATRIXA16.size;        // 64
const MATERIAL = SZ.D3DMATERIALQ.size;       // 68
const EXMATERIAL = SZ.D3DEXMATERIAL.size;    // 8
const FRAME_PREFIX = SZ.DXFRAME_PREFIX.size; // 216
const ATTRIB_RANGE = SZ.D3DXATTRIBUTERANGE.size; // 20
const AFFINE_PARTS = SZ.DXAFFINEPARTS.size;      // 36
const VEC3 = 12;

class Cursor {
  constructor(buf, at) { this.b = buf; this.p = at; }
  get left() { return this.b.length - this.p; }
  need(n, what) {
    if (n < 0 || this.p + n > this.b.length) {
      throw new Error(`mapobj: ${what} runs past end (need ${n}, have ${this.left})`);
    }
  }
  u32() { this.need(4, 'u32'); const v = this.b.readUInt32LE(this.p); this.p += 4; return v; }
  i32() { this.need(4, 'i32'); const v = this.b.readInt32LE(this.p); this.p += 4; return v; }
  bool() { return this.u32() !== 0; }
  f32() { this.need(4, 'f32'); const v = this.b.readFloatLE(this.p); this.p += 4; return v; }
  vec3() {
    this.need(VEC3, 'vec3');
    const v = [this.b.readFloatLE(this.p), this.b.readFloatLE(this.p + 4), this.b.readFloatLE(this.p + 8)];
    this.p += VEC3;
    return v;
  }
  matrix() {
    this.need(MATRIX, 'matrix');
    const m = new Float32Array(16);
    for (let i = 0; i < 16; i++) m[i] = this.b.readFloatLE(this.p + i * 4);
    this.p += MATRIX;
    return m;
  }
  bytes(n, what) { this.need(n, what); const s = this.b.subarray(this.p, this.p + n); this.p += n; return s; }
  /** ReadBuffer-style string: length EXCLUDES the NUL. See the header note. */
  rawStr(what) {
    const n = this.i32();
    if (n < 0 || n > 4096) throw new Error(`mapobj: bad ${what} length ${n}`);
    if (n === 0) return '';
    return this.bytes(n, what).toString('latin1').replace(/\0.*$/, '');
  }
}

/**
 * Walk the octree structure only, using `m_DataSize` to step over payloads.
 * Never throws on payload content — that is the point of doing it this way.
 */
function walkTree(c, opts, out, depth = 0) {
  if (depth > 64) throw new Error('mapobj: octree deeper than 64');
  const subdivided = c.bool();
  const vMax = c.vec3();
  const vMin = c.vec3();
  const dataAddress = c.u32();
  const dataSize = c.u32();
  const payloadAt = c.p;

  const node = { vMax, vMin, subdivided, dataAddress, dataSize, frames: [] };
  out.nodes.push(node);

  if (dataSize > 0) {
    if (!opts.structureOnly) {
      try {
        const pc = new Cursor(c.b, payloadAt);
        if (pc.bool()) readFrame(pc, node.frames, out);
      } catch (err) {
        out.failedFrames++;
        out.firstFrameError = out.firstFrameError || err.message;
      }
    }
    // Always resume from the declared size, exactly as DynamicLoad does. A
    // frame that decoded short or long must not shift the rest of the tree.
    c.need(dataSize, 'octree payload');
    c.p = payloadAt + dataSize;
  }

  for (let i = 0; i < 8; i++) {
    if (c.bool()) walkTree(c, opts, out, depth + 1);
  }
  return node;
}

/**
 * One DxFrame, plus its sibling and child chains.
 *
 * Siblings are followed iteratively; the shipped scene graphs are wide (a node
 * can hold hundreds of props in one sibling chain) and deep recursion there
 * would blow the stack on real maps.
 */
function readFrame(c, out, stats) {
  // The two link flags are NOT adjacent. `LoadFile` reads the sibling flag and,
  // if set, parses the ENTIRE sibling subtree inline before it reads the child
  // flag:
  //
  //   [body][BOOL hasSibling][sibling...][BOOL hasChild][child...]
  //
  // So a loop that follows siblings iteratively — reading both flags together —
  // desyncs on the first frame that has a sibling, which is nearly all of them.
  // Exactly the same shape as the DxAABBNode collision tree in `.wld0`.
  //
  // Modelled with an explicit frame stack rather than real recursion: sibling
  // chains run to hundreds of entries and the depth is data-controlled.
  const stack = [{ stage: 0 }];
  let guard = 0;
  while (stack.length) {
    if (++guard > 2000000) throw new Error('mapobj: frame walk did not terminate');
    const f = stack[stack.length - 1];
    if (f.stage === 0) {
      f.stage = 1;
      readFrameBody(c, out, stats);
    } else if (f.stage === 1) {
      f.stage = 2;
      if (c.bool()) stack.push({ stage: 0 });    // sibling subtree, inline
    } else if (f.stage === 2) {
      f.stage = 3;
      if (c.bool()) stack.push({ stage: 0 });    // first child, inline
    } else {
      stack.pop();
    }
  }
}

/** Everything in a DxFrame up to (but not including) its two link flags. */
function readFrameBody(c, out, stats) {
  const vTreeMax = c.vec3();
  const vTreeMin = c.vec3();
  const matRot = c.matrix();
  c.bytes(MATRIX, 'matRotOrig');            // redundant once matCombined exists
  const matCombined = c.matrix();

  let name = null;
  if (c.bool()) name = c.rawStr('frame name');

  const meshes = [];
  if (c.bool()) readMeshes(c, meshes, stats);

  // Two effect lists.
  for (let list = 0; list < 2; list++) {
    const count = c.u32();
    if (count > 65536) throw new Error(`mapobj: implausible effect count ${count}`);
    for (let i = 0; i < count; i++) {
      const fx = readEffect(c, stats);
      // Named effects with a placement are the only ones a scene can use: an
      // unnamed record has no .egp to instantiate, and one without affine parts
      // has nowhere to go.
      if (fx && fx.name && fx.translate) stats.placed.push(fx);
      // Water found on this path (DxFrame::LoadEffect) gets `AdaptToDxFrame`
      // called on it (DxFrameSaveLoad.cpp:168), which sets `m_matFrameComb =
      // pFrame->matCombined` — THIS frame. Its own optional affine parts still
      // feed `m_pmatLocal` first (SetAffineValue, called before
      // AdaptToDxFrame), so the final placement is `local * frame`, same
      // composition order `DxEffectWater::Render` uses (DxEffectWater.cpp:472).
      if (fx && fx.water) {
        const local = fx.affine ? WATER.composeAffine(fx.affine) : WATER.IDENTITY;
        // The property's own cached matFrameComb (if any) is IGNORED here —
        // AdaptToDxFrame (DxFrameSaveLoad.cpp:168) overwrites it with THIS
        // frame's live matCombined right after SetProperty, so that (not the
        // saved snapshot) is what the engine actually renders with.
        stats.water.push({ ...fx.water, name: fx.name, frameMatrix: WATER.mul4(local, matCombined) });
      }
    }
    stats.effects += count;
  }

  out.push({ vTreeMax, vTreeMin, matRot, matCombined, name, meshes });
  stats.frames++;
}

/**
 * One effect on a DxFrame (`DxFrame::LoadEffect`, DxFrameSaveLoad.cpp:116).
 *
 * Not the same shape as the `.wld0` texture effects, which are just
 * [typeId][version][size]. Here the record opens with an OPTIONAL NAME and
 * closes with an optional blitted `DXAFFINEPARTS`, and only the middle blob is
 * size-prefixed:
 *
 *   BOOL -> [int len][len chars]        name
 *   DWORD typeId, DWORD version, DWORD size
 *   size bytes                          property blob
 *   BOOL -> DXAFFINEPARTS (36 bytes)
 *   <LoadBuffer>                        polymorphic
 *
 * `LoadBuffer` is virtual per effect type. The base implementation reads one
 * DWORD and nothing else; every override reads that same leading DWORD as a
 * buffer size and then parses structurally, ignoring it for versions it knows.
 * Treating the DWORD as a skip length is therefore the one interpretation that
 * satisfies both, and the corpus is the arbiter — see the note in the README.
 */
function readEffect(c, stats) {
  let name = null;
  if (c.bool()) name = c.rawStr('effect name');

  const typeId = c.u32();
  const version = c.u32();
  const size = c.u32();
  let water = null;
  if (size) {
    const propBuf = c.bytes(size, 'effect property');
    if (typeId === WATER.WATER_TYPE || typeId === WATER.WATER2_TYPE) {
      try { water = WATER.decode(typeId, version, propBuf); } catch { water = null; }
    }
  }

  // DXAFFINEPARTS field order is now settled: `struct DXAFFINEPARTS { vTrans,
  // vRotate, vScale; }` (SOURCE/Lib_Engine/DxCommon/DxMethods.h:86-98) — three
  // D3DXVECTOR3s, 12 bytes each, TRANSLATION FIRST. That was found chasing the
  // water placement (below): the earlier guess here read the LAST 12 bytes as
  // translation and got (1,1,1) on every record — a scale vector — which is
  // consistent with translation being FIRST, not last, once the real struct is
  // read instead of guessed. `vRotate` is (yaw, pitch, roll) — the exact order
  // `D3DXQuaternionRotationYawPitchRoll` takes it in (DxMethods.cpp:206) — not
  // a plain XYZ Euler triple.
  //
  // For most effect types here this is still of limited use: the names are
  // FRAME names like "Object05[Mesh]", and 0 of 1,179 resolve against the
  // 4,204 built effect prefabs, because DxFrame::LoadEffect attaches an effect
  // COMPONENT to a frame — it does not reference an .egp file. Water (below)
  // is the one type here where the placement is actually consumed.
  let translate = null;
  let affine = null;
  if (c.bool()) {
    const raw = c.bytes(AFFINE_PARTS, 'affine parts');
    if (raw && raw.length >= AFFINE_PARTS) {
      affine = WATER.decodeAffineParts(raw, 0);
      translate = affine.trans;
    }
  }

  const bufSize = c.u32();
  if (bufSize) c.bytes(bufSize, 'effect buffer');

  stats.effectTypes.set(typeId, (stats.effectTypes.get(typeId) || 0) + 1);
  return { name, typeId, version, size, bufSize, translate, affine, water };
}

/** DxMeshes chain: materials, texture names, geometry, then the next mesh. */
function readMeshes(c, out, stats) {
  for (;;) {
    const materialCount = c.u32();
    const textures = [];
    if (materialCount > 4096) throw new Error(`mapobj: implausible material count ${materialCount}`);
    if (materialCount !== 0) {
      // Blitted arrays — these two strides are the reason the probe was extended.
      c.bytes(materialCount * MATERIAL, 'materials');
      c.bytes(materialCount * EXMATERIAL, 'ex materials');
      if (c.bool()) {
        for (let i = 0; i < materialCount; i++) textures.push(c.rawStr('texture name'));
      }
    }

    let name = null;
    if (c.bool()) name = c.rawStr('mesh name');

    // DxOctreeMesh follows. Its geometry is not decoded here; see note below.
    const geometry = readOctreeMesh(c, stats);

    out.push({ materialCount, textures, name, geometry });
    stats.meshes++;
    for (const t of textures) if (t) stats.textureNames.add(t.toLowerCase());

    if (!c.bool()) return;                    // no pMeshNext
  }
}

/**
 * `DxOctreeMesh::LoadFile` (DxOctreeMeshSaveLoad.cpp) — the per-object geometry,
 * stored inline. Map objects are NOT references to `.x` files.
 *
 * There is **no version and no size prefix**, so this must be walked exactly;
 * there is no recovery point if it desyncs. Three things make that easy to get
 * wrong:
 *
 *   * The vertex stride comes from `D3DXGetFVFVertexSize(m_dwFVF)` — the same
 *     FVF decode the terrain uses. A fixed 32-byte OCTREEVERTEX misaligns every
 *     editor-imported baked mesh, which stores 36-byte VERTEXNORCOLORTEX.
 *   * The trailing collision tree is a `DxAABBNode`, whose two child flags are
 *     NOT adjacent — the left subtree is written between them. Same shape as the
 *     `.wld0` collision tree, so the same walker handles it.
 *   * **`m_bUnlit` exists only when the FVF carries `D3DFVF_DIFFUSE`.** A
 *     conditional trailing field: read it unconditionally and every
 *     non-diffuse mesh loses 4 bytes; skip it and every diffuse one gains 4.
 *     Either way the parent DxFrame desyncs, not this function.
 */
function readOctreeMesh(c, stats) {
  const fvf = c.u32();
  const stride = FVF.vertexSize(fvf);
  if (!stride) throw new Error(`mapobj: FVF 0x${fvf.toString(16)} has zero stride`);

  const vertexCount = c.u32();
  if (vertexCount > 0x100000) throw new Error(`mapobj: implausible vertex count ${vertexCount}`);
  const vertexBlob = vertexCount > 0 ? c.bytes(vertexCount * stride, 'object vertices') : null;

  const faceCount = c.u32();
  if (faceCount > 0x100000) throw new Error(`mapobj: implausible face count ${faceCount}`);
  const indexBlob = faceCount > 0 ? c.bytes(faceCount * 6, 'object indices') : null;

  const attribCount = c.u32();
  if (attribCount > 65536) throw new Error(`mapobj: implausible attribute count ${attribCount}`);
  // D3DXATTRIBUTERANGE: AttribId, FaceStart, FaceCount, VertexStart, VertexCount.
  const attribs = [];
  if (attribCount > 0) {
    const blob = c.bytes(attribCount * ATTRIB_RANGE, 'attribute table');
    for (let i = 0; i < attribCount; i++) {
      const o = i * ATTRIB_RANGE;
      attribs.push({
        attribId: blob.readUInt32LE(o),
        faceStart: blob.readUInt32LE(o + 4),
        faceCount: blob.readUInt32LE(o + 8),
        vertexStart: blob.readUInt32LE(o + 12),
        vertexCount: blob.readUInt32LE(o + 16),
      });
    }
  }

  // Same DxAABBNode tree as the terrain path, so the same walker handles it —
  // including the interleaved child flags. `dwFace` leaves are collected only
  // when asked for, since that allocates a Set per mesh.
  let collisionNodes = 0;
  let collisionFaces = null;
  if (c.bool()) {
    collisionFaces = stats.collisionFaces ? new Set() : null;
    collisionNodes = OCT.skipCollisionTree(c, collisionFaces);
  }

  let unlit = false;
  if (fvf & FVF.DIFFUSE) unlit = c.u32() !== 0;

  stats.vertices += vertexCount;
  stats.triangles += faceCount;
  stats.collisionNodes += collisionNodes;

  const g = { fvf, stride, vertexCount, faceCount, attribs, collisionNodes, unlit };
  if (collisionFaces) g.collisionFaces = collisionFaces;
  if (!stats.headerOnly && vertexBlob && indexBlob) {
    const lay = FVF.layout(fvf);
    const positions = new Float32Array(vertexCount * 3);
    for (let i = 0; i < vertexCount; i++) {
      const o = i * stride + lay.position;
      positions[i * 3] = vertexBlob.readFloatLE(o);
      positions[i * 3 + 1] = vertexBlob.readFloatLE(o + 4);
      positions[i * 3 + 2] = vertexBlob.readFloatLE(o + 8);
    }
    const indices = new Uint16Array(faceCount * 3);
    for (let i = 0; i < indices.length; i++) indices[i] = indexBlob.readUInt16LE(i * 2);
    g.positions = positions;
    g.indices = indices;
    if (lay.texcoords.length) {
      const uvs = new Float32Array(vertexCount * 2);
      const uo = lay.texcoords[0].offset;
      for (let i = 0; i < vertexCount; i++) {
        uvs[i * 2] = vertexBlob.readFloatLE(i * stride + uo);
        uvs[i * 2 + 1] = vertexBlob.readFloatLE(i * stride + uo + 4);
      }
      g.uvs = uvs;
    }
  }
  return g;
}

/**
 * Parse the object scene graph of an opened `.wld`.
 * @param {object} wld the object `wld.open()` returned
 * @param {{structureOnly?:boolean}} [opts]
 */
function parse(wld, opts = {}) {
  const out = { nodes: [], frames: 0, meshes: 0, effects: 0, placed: [], water: [], failedFrames: 0,
                vertices: 0, triangles: 0, collisionNodes: 0,
                headerOnly: !!opts.headerOnly, collisionFaces: !!opts.collisionFaces,
                effectTypes: new Map(),
                textureNames: new Set(), firstFrameError: null };
  const c = new Cursor(wld.buf, octreeOffset(wld));
  walkTree(c, opts, out);
  out.end = c.p;
  return out;
}

/**
 * Where the DxOctree section begins.
 *
 * LoadFile_VER200 reads mapID, the map name buffer, then the filemark, and goes
 * straight into the octree — so this is derived from the header layout rather
 * than from a mark. There is no mark for it.
 */
function octreeOffset(wld) {
  // 132 header + mapId(4) + name(MAXLANDNAME) + filemarkVersion(4) + size(4) + marks(16)
  return wld.objectSectionOffset;
}

module.exports = { parse, walkTree, readFrame, readMeshes, readOctreeMesh, readEffect, Cursor,
                   octreeOffset, MATRIX, MATERIAL, EXMATERIAL, FRAME_PREFIX, ATTRIB_RANGE };
