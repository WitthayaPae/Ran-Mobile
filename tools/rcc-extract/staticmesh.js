'use strict';
//
// `DxStaticMesh` / `DxSingleTexMesh` reader — the `.wld0` sidecar.
//
// Container (DxEffect/DxStaticMesh.cpp):
//
//   132-byte CSerialFile header      "default" / "Default_Crypt"
//   u32 version                      0x0100..0x0103, 0x0200
//   u32 blockSize (= 24)
//   D3DXVECTOR3 vMax, vMin
//   5 material buckets               order varies by container version
//
// Each bucket: [u32 count] then per entry [key string][DxSingleTexMesh].
//
const W = require('./wld');
const OCT = require('./octree');

const BODY = 132;

// Bucket order per container version, read off the loader bodies
// (DxStaticMesh::Load_101 / _102 / _103 / _200). The buckets are identical in
// format; versions differ only in WHICH ones and IN WHAT ORDER.
//
// Two things worth spelling out, because guessing either one desyncs the file:
//   * 0x0100 and 0x0101 both dispatch to Load_101, so they share a list — and
//     it is THREE buckets, not one or two.
//   * ALPHA moves. It is second in 0x0102 but LAST in 0x0103/0x0200.
const BUCKETS = {
  0x0100: ['mesh', 'alpha', 'softAlpha'],
  0x0101: ['mesh', 'alpha', 'softAlpha'],
  0x0102: ['mesh', 'alpha', 'softAlpha', 'softAlpha01', 'softAlpha02'],
  0x0103: ['mesh', 'softAlpha', 'softAlpha01', 'softAlpha02', 'alpha'],
  0x0200: ['mesh', 'softAlpha', 'softAlpha01', 'softAlpha02', 'alpha'],
};

class Cursor extends OCT.Cursor {
  /**
   * CSerialFile std::string: [u32 byteCount][byteCount chars].
   * byteCount INCLUDES the NUL (writer emits length()+1), so an empty string is
   * [1]['\0'] and never [0]. Off by one here shifts every following field.
   */
  str() {
    const n = this.u32();
    if (n === 0 || n > 4096) throw new Error(`staticmesh: bad string length ${n}`);
    const raw = this.bytes(n, 'string');
    const nul = raw.indexOf(0);
    return raw.toString('latin1', 0, nul === -1 ? n : nul);
  }
}

/**
 * One DxSingleTexMesh (NsOCTreeSaveLoad.cpp:809).
 *
 * Only three header versions ship, and 0x0101 — the most common — puts the flag
 * BEFORE the name, the reverse of 0x0102. All three fall through to a common
 * tail with no `_dwType` lightmap discriminator; that field only exists in
 * 0x0104+, which never ships here.
 *
 * `bufferSize` covers ONLY the header + effect list. `Save` calls EndBlock
 * before writing bExist and the octree, so skipping by it lands on bExist, not
 * past the mesh.
 */
function readSingleTexMesh(c, opts) {
  const version = c.u32();
  const bufferSize = c.u32();
  const headerEnd = c.p + bufferSize;

  let texName = null;
  let flag = 0;
  const effects = [];

  switch (version) {
    case 0x0100:
      texName = c.str();                       // name only
      break;
    case 0x0101:
      flag = c.u32();                          // flag FIRST
      texName = c.str();
      readEffects(c, effects);
      break;
    case 0x0102:
      texName = c.str();
      flag = c.u32();
      readEffects(c, effects);
      break;
    default:
      // Unknown header layout. The block size still gets us to bExist, so the
      // geometry stays readable — degrade to "no material metadata".
      c.p = headerEnd;
      break;
  }

  // Common tail. No _dwType here for any shipped version.
  let octree = null;
  if (c.bool()) octree = OCT.parse(c.b, c.p, opts);
  if (octree) c.p = octree.end;

  return { version, flag, texName, effects, octree };
}

// Effect type ids (DxTexEff.h:5-9). Bit flags, not a dense enum.
const TEXEFF = {
  DIFFUSE: 0x0001,
  FLOWUV: 0x0002,
  ROTATE: 0x0004,
  SPECULAR: 0x0008,
  VISUALMATERIAL: 0x0010,
};

// Field lists per (typeId, psfVersion), read off each LoadPSF / PROPERTY::Load.
// 's' = string, 'f' = float, 'd' = DWORD/BOOL — all 4 bytes except strings.
// Field ORDER changes between versions of the same effect (Diffuse 0x0101 leads
// with bSpeed; 0x0102+ leads with strTex), so version must select the list.
const EFFECT_FIELDS = {
  [TEXEFF.DIFFUSE]: {
    0x0103: ['s:tex', 'f:speed', 'd:bSpeed', 'f:alpha', 'd:whenDayOff'],
    0x0102: ['s:tex', 'f:speed', 'd:bSpeed', 'f:alpha'],
    0x0101: ['d:bSpeed', 'f:speed', 'f:alpha', 's:tex'],
    0x0100: ['f:speed', 's:tex'],
  },
  [TEXEFF.FLOWUV]: {
    0x0101: ['d:flag', 's:alphaTex', 's:flowTex', 'f:scale', 'f:speedX', 'f:speedY',
             'f:speed', 'f:alpha', 'f:dirX', 'f:dirY'],
    0x0100: ['d:flag', 'f:scale', 'f:alpha', 'f:speedX', 'f:speedY', 'f:speed',
             'f:dirX', 'f:dirY', 's:alphaTex', 's:flowTex'],
  },
  [TEXEFF.ROTATE]: {
    0x0101: ['s:tex', 'f:speed'],
    0x0100: ['f:speed', 's:tex'],
  },
  [TEXEFF.SPECULAR]: {
    0x0101: ['s:tex'],
    0x0100: ['s:tex'],                     // both versions share one Load body
  },
};

/**
 * Effect list.
 *
 * Every LoadPSF writes [u32 typeId][u32 version][u32 size] — but `size` is NOT
 * trustworthy for a version this build recognises. The engine reads it and then
 * ignores it on every known-version path, using it only to skip an UNKNOWN
 * version; older writers left sizes that undercount their own payload. In
 * `srp_ground.wld0` a Diffuse 0x0101 record declares 31 bytes and occupies 43,
 * and the enclosing DxSingleTexMesh block undercounts by the same 12 — so
 * trusting either size walks straight into the middle of a texture name.
 *
 * Parse structurally and mirror the engine exactly: size-skip only when the
 * (type, version) pair has no known field list.
 */
function readEffects(c, out) {
  const count = c.u32();
  if (count > 4096) throw new Error(`staticmesh: implausible effect count ${count}`);
  for (let i = 0; i < count; i++) {
    const typeId = c.u32();
    const psfVersion = c.u32();
    const psfSize = c.u32();
    const start = c.p;
    const eff = { typeId, psfVersion, psfSize, props: {}, textures: [] };

    const fields = (EFFECT_FIELDS[typeId] || {})[psfVersion];
    if (fields) {
      readFields(c, fields, eff);
    } else if (typeId === TEXEFF.VISUALMATERIAL &&
               (psfVersion === 0x0102 || psfVersion === 0x0100)) {
      // Variable-length: a name, two counts, then `dw0` texture names.
      eff.props.visualMaterial = c.str();
      const dw0 = c.u32();
      eff.props.dw1 = c.u32();
      if (dw0 > 256) throw new Error(`staticmesh: implausible texture count ${dw0}`);
      for (let k = 0; k < dw0; k++) eff.textures.push(c.str());
      // 0x0102 alone closes with a trailing sized block (Load_102 vs Load_100).
      // Save writes DWORD(4) then DWORD(0), so it is 8 bytes in practice — but
      // read the size rather than assuming it.
      if (psfVersion === 0x0102) c.bytes(c.u32(), 'visual material tail');
    } else {
      c.p = start + psfSize;               // the engine's own unknown-version path
      eff.unknown = true;
    }
    eff.bytes = c.p - start;
    out.push(eff);
  }
}

function readFields(c, fields, eff) {
  for (const f of fields) {
    const [kind, name] = f.split(':');
    if (kind === 's') {
      const v = c.str();
      eff.props[name] = v;
      if (v) eff.textures.push(v);
    } else if (kind === 'f') {
      eff.props[name] = c.f32();
    } else {
      eff.props[name] = c.u32();
    }
  }
}

/**
 * Parse a whole `.wld0`.
 * @param {Buffer} raw file bytes (already RCC-decrypted if from an archive)
 * @param {{headerOnly?:boolean}} [opts]
 */
function parse(raw, opts = {}) {
  const type = W.readTypeString(raw);
  const buf = W.isEncryptedType(type) ? W.decrypt(Buffer.from(raw)) : raw;
  if (buf.length < BODY + 8) throw new Error('.wld0 too short');

  const c = new Cursor(buf, BODY);
  const version = c.u32();
  const blockSize = c.u32();
  const vMax = c.vec3();
  const vMin = c.vec3();

  const names = BUCKETS[version];
  if (!names) throw new Error(`.wld0 unknown container version 0x${version.toString(16)}`);

  const buckets = [];
  for (const name of names) {
    const count = c.u32();
    if (count > 100000) throw new Error(`staticmesh: implausible mesh count ${count}`);
    const meshes = [];
    for (let i = 0; i < count; i++) {
      const key = c.str();                     // map key, repeats the tex name
      const m = readSingleTexMesh(c, opts);
      m.key = key;
      meshes.push(m);
    }
    buckets.push({ name, meshes });
  }

  return { type, encrypted: W.isEncryptedType(type), version, blockSize,
           vMax, vMin, buckets, bytesRead: c.p, fileSize: buf.length };
}

/** Totals across every bucket, for reporting and tests. */
function summarise(sm) {
  let meshes = 0, nodes = 0, verts = 0, tris = 0, skipped = 0;
  const landTypes = new Map();
  const textures = new Set();
  for (const b of sm.buckets) {
    for (const m of b.meshes) {
      meshes++;
      if (m.texName) textures.add(m.texName.toLowerCase());
      // Effects reference their own textures (flow/alpha maps, specular maps);
      // the mobile pipeline needs those converted too.
      for (const e of m.effects) {
        for (const t of e.textures) textures.add(t.toLowerCase());
      }
      if (!m.octree) continue;
      skipped += m.octree.skipped;
      for (const n of m.octree.nodes) {
        nodes++;
        if (!n.geometry) continue;
        verts += n.geometry.vertexCount;
        tris += n.geometry.faceCount;
        landTypes.set(n.geometry.landType, (landTypes.get(n.geometry.landType) || 0) + 1);
      }
    }
  }
  return { meshes, nodes, verts, tris, skipped, landTypes, textures };
}

module.exports = { parse, summarise, readSingleTexMesh, readEffects,
                   BUCKETS, TEXEFF, EFFECT_FIELDS, Cursor };
