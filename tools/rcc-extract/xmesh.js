'use strict';
//
// .x -> mesh data.
//
// Walks the token stream from xfile.js into a node tree, then pulls out the
// data a mobile renderer actually needs: vertices, triangles, UVs, normals,
// materials, the frame hierarchy with its transforms, and skinning weights.
//
// The .x format is self-describing via `template` blocks, but the templates in
// these files are the standard D3DRM set, so this reads the known node types
// directly and skips the rest. That is deliberate: a fully generic reader would
// need the template definitions honoured for every custom node, and nothing in
// Ran/ uses one that matters for geometry.
//
// Binary and text share one node model, so downstream code never branches on
// the container format.
//
const X = require('./xfile');

// ---------------------------------------------------------------------------
// Text tokenizer — produces the same shape as xfile.binaryTokens.
// ---------------------------------------------------------------------------
function* textTokens(body) {
  const src = body.toString('latin1');
  const re = /\/\/[^\n]*|#[^\n]*|"([^"]*)"|([A-Za-z_][A-Za-z0-9_]*)|(-?\d+\.\d+(?:[eE][-+]?\d+)?|-?\.\d+|-?\d+)|([{}<>,;()\[\]])/g;
  let m;
  while ((m = re.exec(src)) !== null) {
    if (m[0].startsWith('//') || m[0].startsWith('#')) continue;
    if (m[1] !== undefined) { yield { type: 'STRING', value: m[1] }; continue; }
    if (m[2] !== undefined) { yield { type: 'NAME', value: m[2] }; continue; }
    if (m[3] !== undefined) {
      const n = Number(m[3]);
      yield { type: m[3].includes('.') || m[3].includes('e') || m[3].includes('E')
        ? 'FLOAT' : 'INTEGER', value: n };
      continue;
    }
    if (m[4] === '<') { // GUID literal
      const end = src.indexOf('>', re.lastIndex);
      yield { type: 'GUID', value: src.slice(re.lastIndex, end) };
      re.lastIndex = end + 1;
      continue;
    }
    yield { type: m[4] };
  }
}

/**
 * Flatten tokens into a stream of scalars plus structure markers, so binary
 * (which packs runs into INTEGER_LIST/FLOAT_LIST) and text (which emits one
 * number at a time) can be consumed identically.
 */
function normalize(tokens) {
  const out = [];
  for (const t of tokens) {
    if (t.type === 'INTEGER_LIST') {
      for (const v of t.value) out.push({ type: 'NUM', value: v });
    } else if (t.type === 'FLOAT_LIST') {
      for (const v of t.value) out.push({ type: 'NUM', value: v });
    } else if (t.type === 'INTEGER' || t.type === 'FLOAT') {
      out.push({ type: 'NUM', value: t.value });
    } else if (t.type === ';' || t.type === ',') {
      // separators carry no information once runs are flattened
    } else {
      out.push(t);
    }
  }
  return out;
}

/**
 * Parse into a tree of { kind, name, nums, children }.
 * `nums` is every scalar that appeared directly inside the node.
 */
function parseNodes(toks) {
  let i = 0;
  const roots = [];

  // Data Object REFERENCE registry: `{ ObjectName }` (a brace with no
  // preceding NAME in that position) does not open a new definition — it
  // points at an object named elsewhere in the file. 3ds Max's .x exporter
  // uses exactly this to dedupe materials shared by more than one mesh:
  // the first mesh to use a material defines it inline
  // (`Material Foo { ... TextureFilename {...} }`), every later user just
  // references it (`{Foo}`). Keyed by name, first-defined-wins (duplicate
  // names are not expected here and shipped data has not shown any).
  const byName = new Map();

  function parseBlock(into) {
    while (i < toks.length) {
      const t = toks[i];
      if (t.type === '}') { i++; return; }
      if (t.type === 'NAME') {
        const kind = t.value;
        i++;
        // Optional instance name before the brace.
        let name = null;
        if (i < toks.length && toks[i].type === 'NAME' && toks[i + 1] &&
            toks[i + 1].type === '{') { name = toks[i].value; i++; }
        if (i < toks.length && toks[i].type === '{') {
          i++;
          const node = { kind, name, nums: [], strings: [], children: [] };
          parseInner(node);
          into.push(node);
          if (name) byName.set(name, node);
        }
        continue;
      }
      i++; // stray token
    }
  }

  function parseInner(node) {
    while (i < toks.length) {
      const t = toks[i];
      if (t.type === '}') { i++; return; }
      if (t.type === 'NUM') { node.nums.push(t.value); i++; continue; }
      if (t.type === 'STRING') { node.strings.push(t.value); i++; continue; }
      if (t.type === 'GUID') { i++; continue; }
      if (t.type === 'NAME') {
        const kind = t.value;
        const next = toks[i + 1];
        const next2 = toks[i + 2];
        if (next && next.type === '{') {
          i += 2;
          const child = { kind, name: null, nums: [], strings: [], children: [] };
          parseInner(child);
          node.children.push(child);
          continue;
        }
        if (next && next.type === 'NAME' && next2 && next2.type === '{') {
          i += 3;
          const child = { kind, name: next.value, nums: [], strings: [], children: [] };
          parseInner(child);
          node.children.push(child);
          byName.set(child.name, child);
          continue;
        }
        // A bare name is a reference (e.g. `{ MeshName }`) — record and move on.
        node.strings.push(kind);
        i++;
        continue;
      }
      // A standalone `{` here (not consumed above as part of `NAME {` or
      // `NAME NAME {`) is an object REFERENCE: `{ Name }`, optionally
      // `{ Name1, Name2, ... }`, terminated by its own `}`. It must be
      // parsed as its OWN self-contained frame — recursing into parseInner
      // lets the reference's closing `}` return from THIS nested call only.
      //
      // MEASURED bug (before this branch existed): a naked `{` fell through
      // to the trailing `i++` below and was silently skipped, so the
      // reference's NAME then got picked up by the *enclosing* node's own
      // NAME handling (as a spurious bare-name reference) and the
      // reference's closing `}` was misread as the ENCLOSING node's own
      // closing brace — returning one nesting level early. `bir01.x`
      // (`s_w_bs`/`bir01`/`bir02`-style character body pieces routinely hit
      // this: MeshMaterialList with 3 materials-by-reference) cascaded
      // through three such premature returns, detaching
      // MeshTextureCoords/XSkinMeshHeader/9x SkinWeights from the Mesh
      // entirely — the 1230-vertex torso mesh, skinned in the source to
      // Bip01_Spine/Spine1/Spine2/Neck/Head/L+R_UpperArm/L+R_Forearm, came
      // out of the OLD parser as `skinBones=0`, which the character
      // assembler (`RanChfBuilder`) then treated as a RIGID part with no
      // matching bone anywhere in its own hierarchy — orphaned, parented to
      // the character root at its raw unposed local offset, landing ~4-10
      // units BELOW the feet. That is the measured, confirmed root cause of
      // "boa's main body invisible" on a real device: the body mesh was
      // never missing, it was buried under the floor.
      if (t.type === '{') {
        i++;
        const refNames = [];
        const ref = { kind: '<ref>', name: null, nums: [], strings: refNames, children: [] };
        parseInner(ref);
        // Resolve each referenced name against objects already parsed. A
        // reference to something not yet seen (a forward reference — not
        // observed in shipped data, but not assumed absent either) is left
        // unresolved rather than guessed at.
        for (const n of refNames) {
          const resolved = byName.get(n);
          if (resolved) node.children.push(resolved);
        }
        continue;
      }
      i++;
    }
  }

  // Skip `template` definitions at the top level; they describe layout we
  // already know and would otherwise be parsed as data nodes.
  while (i < toks.length) {
    if (toks[i].type === 'TEMPLATE' ||
        (toks[i].type === 'NAME' && toks[i].value === 'template')) {
      i++;
      while (i < toks.length && toks[i].type !== '{') i++;
      let depth = 0;
      while (i < toks.length) {
        if (toks[i].type === '{') depth++;
        else if (toks[i].type === '}') { depth--; if (depth === 0) { i++; break; } }
        i++;
      }
      continue;
    }
    const before = i;
    parseBlock(roots);
    if (i === before) i++;
  }
  return roots;
}

const findAll = (node, kind, acc = []) => {
  for (const c of node.children || []) {
    if (c.kind === kind) acc.push(c);
    findAll(c, kind, acc);
  }
  return acc;
};

/** Pull geometry out of a Mesh node. */
function readMesh(node) {
  const n = node.nums;
  let p = 0;
  const vertexCount = n[p++];
  const positions = new Float32Array(vertexCount * 3);
  for (let v = 0; v < vertexCount * 3; v++) positions[v] = n[p++];

  const faceCount = n[p++];
  const indices = [];
  for (let f = 0; f < faceCount; f++) {
    const c = n[p++];
    const face = [];
    for (let k = 0; k < c; k++) face.push(n[p++]);
    // Triangulate fans; the format allows n-gons but ships mostly triangles.
    for (let k = 2; k < face.length; k++) indices.push(face[0], face[k - 1], face[k]);
  }

  const mesh = {
    name: node.name,
    vertexCount,
    faceCount,
    positions,
    indices: Uint32Array.from(indices),
    normals: null,
    uvs: null,
    materials: 0,
    skinBones: 0,
  };

  const normals = node.children.find((c) => c.kind === 'MeshNormals');
  if (normals) {
    const m = normals.nums;
    const count = m[0];
    mesh.normals = new Float32Array(count * 3);
    for (let v = 0; v < count * 3; v++) mesh.normals[v] = m[1 + v];
  }

  const tex = node.children.find((c) => c.kind === 'MeshTextureCoords');
  if (tex) {
    const m = tex.nums;
    const count = m[0];
    mesh.uvs = new Float32Array(count * 2);
    for (let v = 0; v < count * 2; v++) mesh.uvs[v] = m[1 + v];
  }

  const matList = node.children.find((c) => c.kind === 'MeshMaterialList');
  if (matList) {
    mesh.materials = matList.nums[0] || 0;
    // nums is [nMaterials, nFaceIndexes, ...faceIndexes]; the per-face material
    // assignment is what lets one mesh split into several Unity submeshes.
    const nFaces = matList.nums[1] || 0;
    if (nFaces) mesh.faceMaterials = Uint32Array.from(matList.nums.slice(2, 2 + nFaces));
    mesh.textures = matList.children
      .filter((c) => c.kind === 'Material')
      .map((m) => {
        const tf = m.children.find((c) => c.kind === 'TextureFilename');
        return (tf && tf.strings[0]) || '';
      });
  }

  const skinHeader = node.children.find((c) => c.kind === 'XSkinMeshHeader');
  const bones = node.children.filter((c) => c.kind === 'SkinWeights');
  mesh.skinBones = bones.length;
  if (bones.length) {
    mesh.skin = bones.map((b) => {
      const count = b.nums[0];
      return {
        bone: b.strings[0] || null,
        indices: Uint32Array.from(b.nums.slice(1, 1 + count)),
        weights: Float32Array.from(b.nums.slice(1 + count, 1 + count * 2)),
        // The 16 floats after the weights are matrixOffset — the inverse bind
        // pose. Dropping it makes the mesh "parse" and render as a puddle,
        // because every vertex then skins from an unbound rest position.
        bindPose: Float32Array.from(b.nums.slice(1 + count * 2, 1 + count * 2 + 16)),
      };
    });
    if (skinHeader) {
      mesh.maxWeightsPerVertex = skinHeader.nums[0];
      mesh.maxWeightsPerFace = skinHeader.nums[1];
      mesh.boneCount = skinHeader.nums[2];
    }
  }
  return mesh;
}

/**
 * Parse a .x file into { frames, meshes }.
 * @param {Buffer} buf raw file bytes
 * @param {{tolerant?:boolean}} [opts]
 */
function parse(buf, opts = {}) {
  const f = X.open(buf, opts);
  const raw = f.format === 'bin'
    ? X.binaryTokens(f.body, f.floatBits, opts)
    : textTokens(f.body);
  const nodes = parseNodes(normalize(raw));

  const root = { kind: 'root', children: nodes };

  // Collect meshes together with the name of the Frame that encloses them.
  //
  // A `.cps` piece selects ONE mesh out of a shared `.x` by name — `s_13.X`
  // holds both `13body` and `13weapon` — but the exporter leaves the Mesh
  // instance name blank on essentially every shipped file, so the only name
  // that survives is the parent Frame's. Without it a piece cannot be resolved
  // to its geometry at all, and both meshes would be drawn where one was meant.
  const meshNodes = [];
  (function collect(list, frameName) {
    for (const n of list) {
      const here = n.kind === 'Frame' ? (n.name || frameName) : frameName;
      if (n.kind === 'Mesh') meshNodes.push({ node: n, frameName });
      if (n.children && n.children.length) collect(n.children, here);
    }
  })(nodes, null);
  const frames = [];
  for (const n of nodes) {
    if (n.kind === 'Frame') frames.push(n);
    frames.push(...findAll(n, 'Frame'));
  }

  return {
    truncated: !!f.truncated,
    format: f.format,
    frameCount: frames.length,
    // The skeleton. Bones are Frames, and skinning needs the hierarchy and each
    // frame's local transform, not just how many there were.
    bones: flattenFrames(nodes),
    meshes: meshNodes.map(({ node, frameName }) => {
      const m = readMesh(node);
      m.frameName = frameName || '';
      return m;
    }),
  };
}

/**
 * Flatten the Frame tree into a parent-indexed bone array.
 *
 * A parent index rather than nested children, because that is what both the
 * export format and Unity's `SkinnedMeshRenderer.bones` want, and because a
 * parent always precedes its children here — the walk is depth-first from the
 * roots, so a consumer can compute world transforms in one forward pass.
 *
 * Frame names are NOT unique in shipped data (`arch_w_bg.x` has `Bone01`
 * fifteen times, and several frames have no name at all), so bones are
 * identified by index and the name is carried along only as a label. Anything
 * that resolves a SkinWeights bone name has to cope with duplicates.
 */
function flattenFrames(roots) {
  const bones = [];
  const walk = (node, parent) => {
    const ftm = node.children.find((c) => c.kind === 'FrameTransformMatrix');
    const index = bones.length;
    bones.push({
      name: node.name || '',
      parent,
      // Row-major 4x4, as .x stores it. Left unconverted here; the exporter
      // decides what the consumer wants.
      transform: ftm && ftm.nums.length >= 16
        ? Float32Array.from(ftm.nums.slice(0, 16))
        : null,
    });
    for (const c of node.children) {
      if (c.kind === 'Frame') walk(c, index);
    }
  };
  for (const n of roots) {
    if (n.kind === 'Frame') walk(n, -1);
  }
  return bones;
}

module.exports = { parse, parseNodes, normalize, textTokens, readMesh, flattenFrames };
