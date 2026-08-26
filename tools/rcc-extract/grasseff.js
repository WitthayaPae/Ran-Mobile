'use strict';
//
// The standalone effect lists of a `.wld` — where the GROUND VEGETATION lives.
//
// `DxLandMan::LoadFile_VER116` reads three counted lists (m_pEffectList,
// _AFTER, _AFTER_1) after the animation managers. Each entry
// (EffectLoadToList, DxLandManSaveLoad.cpp:574):
//
//   [BOOL hasName] -> [i32 len][len bytes]     the adapt-frame name
//   [u32 TypeID]
//   [u32 dwVer][u32 propSize][propSize bytes]
//   [BOOL hasAffine] -> DXAFFINEPARTS (36 bytes: trans/rotate/scale vec3s)
//   ...then the type's own LoadBuffer payload
//
// Entries have NO total size, so an unknown TypeID ends the walk — the prefix
// decoded so far stays valid, and the caller reports what was skipped.
//
// Decoded types:
//
//   DEF_EFFECT_TILING 0x2014 (DxEffectTilingSaveLoad.cpp:333, ver 0x102/0x103)
//     ground blend overlay. Consumed to keep the stream aligned; its geometry
//     re-draws the terrain with transition blending and is NOT extracted —
//     re-rendering it over the terrain without the engine's bias would only
//     z-fight.
//
//   DEF_EFFECT_GRASS 0x3007 (DxEffectGrass.cpp:896, ver 0x106 = VERSION)
//     the swaying grass. property GRASS_PROPERTY (360 bytes): u32 flags,
//     f32 power, vec3 max, vec3 min, u32 color, f32[16] world, char[260] tex.
//     payload: [u32 buffSize][u32 clusters] x BASEGRASS { vec3 pos,
//     u32 objects, f32 width, f32 height, f32 length, GRASS[objects] } with
//     GRASS = { vec3 pos, vec3 direct, f32 width, f32 height, f32 rotate,
//     f32 catch } (40 bytes). Each blade renders as two quads crossed at 90
//     degrees — GRASSVERTEX has position, normal, diffuse and one UV set.
//
const TILING = 0x2014;
const GRASS = 0x3007;
const SHADOW = 0x3015;
const SHADOWEFF = require('./shadoweff');
const WATER = require('./water');

//   DEF_EFFECT_WATER 0x2001 / DEF_EFFECT_WATER2 0x3013 (SOURCE/Lib_Engine/
//   DxEffect/DxEffectWater[2].{h,cpp}) — the map water surface. THIS is where
//   it actually lives: `DxLandMan::LoadFile_VER200/116/...` calls
//   `EffectLoadToList` for exactly these three lists right after the octree
//   and ani-man tree (DxLandManSaveLoad.cpp:1042-1081), and `EffectLoadToList`
//   is byte-for-byte the same record shape as `DxFrame::LoadEffect` — see
//   water.js. It is NOT frame-attached here: this loader never calls
//   `AdaptToDxFrame`/`AdaptToEffList`, so `m_matFrameComb` stays the identity
//   the constructor sets and the effect's own (usually present) affine-parts
//   block is the ENTIRE placement (`m_pmatLocal`, composed with
//   `D3DXMatrixCompX` — water.js's `composeAffine`, DxLandMan.cpp:1656-1665
//   confirms this is exactly how map water survives a partial scene rebuild).
//   Neither class overrides `LoadBuffer`, so the trailing "own buffer" is
//   just the base class's plain `DWORD(0)` (DxEffectFrameSetSaveLoad.cpp:21).

function walkEffectLists(buf, at) {
  let o = at;
  const u32 = () => { const v = buf.readUInt32LE(o); o += 4; return v; };
  const f32 = () => { const v = buf.readFloatLE(o); o += 4; return v; };

  const grass = [];
  const water = [];
  let tiling = 0;
  let shadow = null;
  let stopped = null;

  for (let list = 0; list < 3 && !stopped; list++) {
    const count = u32();
    if (count > 100000) { stopped = `list ${list} count ${count}`; break; }
    for (let e = 0; e < count; e++) {
      const entryStart = o;
      let name = null;
      if (u32() === 1) {
        const n = u32();
        if (n > 1024) { stopped = 'name length'; break; }
        name = buf.toString('latin1', o, o + n - 1); o += n;
      }
      const typeId = u32();
      const ver = u32();
      const propSize = u32();
      const propAt = o; o += propSize;
      let affineAt = -1;
      if (u32() === 1) { affineAt = o; o += 36; }     // DXAFFINEPARTS

      if (typeId === TILING && (ver === 0x102 || ver === 0x103 || ver === 0x105)) {
        tiling++;
        u32();                                        // blend size
        // NOT `o += u32() * 24`: compound assignment reads the OLD o before
        // the call's side effect runs, silently undoing the +4.
        const nPts = u32();
        o += nPts * 24;                               // POINTEX[]
        u32();                                        // blend size
        const nMats = u32();
        const SM = require('./staticmesh');
        const c = new SM.Cursor(buf, o);
        for (let i = 0; i < nMats; i++) {
          c.u32();                                    // material id
          c.p += 255;                                 // texture name
          if (ver !== 0x105) {
            const nFaces = c.u32();                   // same pitfall as above:
            c.p += nFaces * 108;                      // read count FIRST
          }
        }
        c.u32();                                      // blend size
        for (let i = 0; i < nMats; i++) {
          c.u32();                                    // material id
          if (c.u32() === 1) SM.readSingleTexMesh(c, {});
        }
        o = c.p;
      } else if (typeId === GRASS && ver === 0x106 && propSize === 360) {
        const flags = buf.readUInt32LE(propAt);
        const color = buf.readUInt32LE(propAt + 32);
        const tex = buf.toString('latin1', propAt + 100, propAt + 360).split('\0')[0];
        u32();                                        // buffSize
        const clusters = u32();
        const blades = [];
        for (let cl = 0; cl < clusters; cl++) {
          o += 12;                                    // cluster pos
          const objects = u32();
          o += 12;                                    // width/height/length
          for (let b = 0; b < objects; b++) {
            blades.push({
              pos: [f32(), f32(), f32()],
              direct: [f32(), f32(), f32()],
              width: f32(), height: f32(), rotate: f32(),
            });
            o += 4;                                   // fCatch
          }
        }
        grass.push({ name, texture: tex, color, flags, blades });
      } else if (typeId === SHADOW && propSize === 852) {
        // The ground-shadow decal — decode from its own entry start (the
        // shadow reader re-reads the envelope) and advance past it.
        const s = SHADOWEFF.decodeShadow(buf, entryStart);
        if (!s) { stopped = `shadow decode failed`; break; }
        shadow = s;
        o = s.end;
      } else if (typeId === WATER.WATER_TYPE || typeId === WATER.WATER2_TYPE) {
        let w = null;
        try { w = WATER.decode(typeId, ver, buf.subarray(propAt, propAt + propSize)); } catch { w = null; }
        if (w) {
          // Placement: local affine (rare here — usually absent) composed
          // with the CACHED matFrameComb the property blob itself carries.
          // See water.js's decodeWater/decodeWater2 doc comment for why the
          // property — not a live DxFrame — is the placement source on this
          // (standalone-list) path.
          const local = affineAt >= 0
            ? WATER.composeAffine(WATER.decodeAffineParts(buf, affineAt))
            : WATER.IDENTITY;
          const cached = w.frameMatrix || WATER.IDENTITY;
          water.push({ ...w, name, list, frameMatrix: WATER.mul4(local, cached) });
        }
        o += 4;                                        // base LoadBuffer: DWORD(0)
      } else if (typeId === WATER.RIVER_TYPE) {
        // River OVERRIDES LoadBuffer with real streamed mesh geometry
        // (DxWaterTree) that this project does not decode (see water.js's
        // decodeRiver doc comment) — so unlike Water/Water2 above, there is
        // no safe length to skip past it. Decode what is measured (placement
        // + parameters, already world-space for v0x107) and stop this list,
        // exactly like an unrecognised type would — the only difference is
        // real data is captured first instead of none.
        let w = null;
        try { w = WATER.decode(typeId, ver, buf.subarray(propAt, propAt + propSize)); } catch { w = null; }
        if (w) water.push({ ...w, name, list, frameMatrix: null });
        stopped = `type 0x${typeId.toString(16)} ver 0x${ver.toString(16)} (river geometry not decoded)`;
        break;
      } else {
        stopped = `type 0x${typeId.toString(16)} ver 0x${ver.toString(16)}`;
        break;
      }
    }
  }
  return { grass, water, tiling, shadow, end: o, stopped };
}

/**
 * Blades to geometry: two quads crossed at 90 degrees per blade, base at the
 * blade position, D3D-convention UVs (v=0 at the blade top). Returns a mesh
 * per source effect entry, in the encode()-compatible shape.
 */
function grassToMeshes(grassEntries) {
  const meshes = [];
  const CHUNK = 8000;   // 8 verts a blade; 8191 would hit the u16 index limit
  for (const g of grassEntries) {
    for (let at = 0; at < g.blades.length; at += CHUNK) {
      meshes.push(bladeChunk(g, g.blades.slice(at, at + CHUNK)));
    }
  }
  return meshes;
}

function bladeChunk(g, blades) {
  {
    const n = blades.length;
    const positions = new Float32Array(n * 8 * 3);
    const uvs = new Float32Array(n * 8 * 2);
    const colors = new Uint32Array(n * 8);
    const indices = [];
    let v = 0;
    const argb = g.color >>> 0;
    for (let b = 0; b < n; b++) {
      const bl = blades[b];
      const hw = bl.width * 0.5;
      for (let quad = 0; quad < 2; quad++) {
        const ang = bl.rotate + quad * Math.PI / 2;
        const dx = Math.cos(ang) * hw, dz = Math.sin(ang) * hw;
        const base = v;
        // top-left, top-right, bottom-left, bottom-right
        const px = bl.pos[0], py = bl.pos[1], pz = bl.pos[2];
        const tops = [[px - dx, py + bl.height, pz - dz], [px + dx, py + bl.height, pz + dz]];
        const bots = [[px - dx, py, pz - dz], [px + dx, py, pz + dz]];
        const quadVerts = [tops[0], tops[1], bots[0], bots[1]];
        const quadUvs = [[0, 0], [1, 0], [0, 1], [1, 1]];
        for (let k = 0; k < 4; k++, v++) {
          positions[v * 3] = quadVerts[k][0];
          positions[v * 3 + 1] = quadVerts[k][1];
          positions[v * 3 + 2] = quadVerts[k][2];
          uvs[v * 2] = quadUvs[k][0];
          uvs[v * 2 + 1] = quadUvs[k][1];
          colors[v] = argb;
        }
        indices.push(base, base + 1, base + 2, base + 2, base + 1, base + 3);
      }
    }
    // Straight-up normals: the classic foliage trick — grass lights like the
    // ground it stands on, never edge-on dark.
    const normals = new Float32Array(n * 8 * 3);
    for (let i = 0; i < n * 8; i++) normals[i * 3 + 1] = 1;
    return {
      geometry: {
        fvf: 0x152, vertexCount: n * 8, faceCount: n * 4,
        positions, uvs, indices: new Uint16Array(indices), vbColors: colors,
        vbNormals: normals,
      },
      texture: g.texture, bucket: 'alpha',
    };
  }
}

module.exports = { walkEffectLists, grassToMeshes };
