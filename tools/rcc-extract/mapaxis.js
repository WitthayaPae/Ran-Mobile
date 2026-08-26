'use strict';
//
// .mmp map-axis reader — the per-map minimap image + world-bounds metadata.
//
// SOURCE OF TRUTH: GLMapAxisInfo::LoadFile (Lib_Client/G-Logic/GLMapAxisInfo.cpp:26).
// The PC large map (CLargeMapWindow / CLargeMapWindowImage) blits a RENDERED
// per-map picture and plots marks on it. Everything it needs comes from a small
// TEXT config file the engine loads through `gltexfile`:
//
//   bool GLMapAxisInfo::LoadFile ( const char *szFile )
//   {
//       CString MapName;  STRUTIL::ChangeExt(szFile, MapName, ".mmp");   // <lev>.mmp
//       ...
//       cFILE.getflag ( "MINIMAPNAME",  1, 1, m_strMapTexture );        // the .dds name
//       cFILE.getflag ( "MAPSIZE_X",    1, 2, m_nMapSizeX );            // world extent X
//       cFILE.getflag ( "MAPSIZE_X",    2, 2, m_nMapStartX );           // world min   X
//       cFILE.getflag ( "MAPSIZE_Y",    1, 2, m_nMapSizeY );            // world extent Z
//       cFILE.getflag ( "MAPSIZE_Y",    2, 2, m_nMapStartY );           // world min   Z
//       cFILE.getflag ( "TEXTURE_SIZE", 1, 2, m_vecTextureSize.x );
//       cFILE.getflag ( "TEXTURE_SIZE", 2, 2, m_vecTextureSize.y );
//       cFILE.getflag ( "TEXTURE_POS",  1..4, 4, m_fTextureMapPos[0..3] );
//   }
//
// The KEY is the map's `.lev` basename, NOT its `.wld`/scene name. The engine
// derives the `.mmp` from `pMapNode->strFile` (the .lev, GLGaeaServerEx.cpp:76),
// and STRUTIL::ChangeExt just swaps the extension after lowercasing. So map 3
// (scene w_city_01, lev w_city_s_01.Lev) reads `w_city_s_01.mmp`, and there IS a
// `w_city_s_01.mmp` in the archive but no `w_city_01.mmp` — measured, not assumed.
// This is MEASURED against the shipped Map.rcc: 106 of the 121 catalog maps carry
// a `.mmp` keyed by their .lev stem.
//
// FORMAT (measured): a plaintext, line-based `gltexfile`. Tokens are separated by
// tab / space / comma; `//` begins a comment; the first token on a line is the
// flag key and the rest are its values. `getflag(FLAG, nIDX, nSIZE, out)` reads
// the nIDX-th token (1-based, so token[0] is the flag itself) as the value —
// atoi for ints, atof for floats, verbatim for strings.
//
// WORLD BOUNDS MEANING (from CLargeMapWindow::CONVERT_MAP2WORLD, LargeMapWindow.cpp:1153
// — the inverse blit, so it reads cleanly):
//   world X in [startX, startX + sizeX], mapped left->right across the image;
//   world Z in [startY, startY + sizeY], mapped bottom->top (a HIGH z is the TOP,
//     the engine's z flip). startX = MapStartX, sizeX = MapSizeX, etc.
//   NOTE the .mmp's MAPSIZE_X line is "SIZE START" (extent first, min second).
//
// This file is byte-source only: it decodes the config, it never guesses. A map
// whose `.mmp` is absent is reported absent; a `.mmp` whose MINIMAPNAME points at
// a texture not shipped is reported image-less. Nothing is substituted.

/**
 * Tokenise a `.mmp` into { FLAG: [values...] }, mirroring gltexfile::open.
 * Values EXCLUDE the flag key (unlike the C++ vector, where token[0] is the flag).
 * @param {Buffer|string} data
 * @returns {Object<string,string[]>}
 */
function parseMmp(data) {
  const text = Buffer.isBuffer(data) ? data.toString('latin1') : String(data);
  const flags = {};
  for (let line of text.split(/\r?\n/)) {
    // gltexfile strips a token at the first "//" and drops the rest of the line.
    const ci = line.indexOf('//');
    if (ci >= 0) line = line.slice(0, ci);
    const toks = line.split(/[\s,]+/).filter(Boolean);
    if (toks.length === 0) continue;
    // std::multimap: last write wins here is fine — every shipped .mmp lists each
    // flag exactly once (verified across all 192 entries).
    flags[toks[0]] = toks.slice(1);
  }
  return flags;
}

/** getflag(FLAG, nIDX): the nIDX-th value (1-based), or undefined. */
function gf(flags, name, idx) {
  const v = flags[name];
  return v ? v[idx - 1] : undefined;
}

// atoi/atof: leading numeric prefix, 0/0.0 on garbage — matching the C stdlib the
// engine uses (a wrong-typed field would silently read 0 there too).
const atoi = (s) => { const n = parseInt(s, 10); return Number.isFinite(n) ? n : 0; };
const atof = (s) => { const n = parseFloat(s); return Number.isFinite(n) ? n : 0; };

/**
 * Decode a `.mmp` into the same fields GLMapAxisInfo exposes.
 * @param {Buffer|string} data
 * @returns {{minMapTex:string, sizeX:number, startX:number, sizeY:number,
 *            startY:number, texW:number, texH:number, texPos:number[]}}
 */
function readAxis(data) {
  const f = parseMmp(data);
  return {
    // MINIMAPNAME <name.dds>
    minMapTex: (gf(f, 'MINIMAPNAME', 1) || '').trim(),
    // MAPSIZE_X <sizeX> <startX>   (extent first, origin second)
    sizeX: atoi(gf(f, 'MAPSIZE_X', 1)),
    startX: atoi(gf(f, 'MAPSIZE_X', 2)),
    // MAPSIZE_Y <sizeY> <startY>
    sizeY: atoi(gf(f, 'MAPSIZE_Y', 1)),
    startY: atoi(gf(f, 'MAPSIZE_Y', 2)),
    // TEXTURE_SIZE <w> <h>
    texW: atof(gf(f, 'TEXTURE_SIZE', 1)),
    texH: atof(gf(f, 'TEXTURE_SIZE', 2)),
    // TEXTURE_POS <x> <y> <w> <h>  (the sub-rect inside the atlas; always the
    // whole texture for the shipped minimaps, kept for fidelity)
    texPos: [
      atof(gf(f, 'TEXTURE_POS', 1)), atof(gf(f, 'TEXTURE_POS', 2)),
      atof(gf(f, 'TEXTURE_POS', 3)), atof(gf(f, 'TEXTURE_POS', 4)),
    ],
    _flags: f,
  };
}

/** The lowercased basename stem of a path, e.g. "data/map/W_City_S_01.Lev" -> "w_city_s_01". */
function stem(p) {
  const base = String(p).replace(/\\/g, '/').split('/').pop();
  const dot = base.lastIndexOf('.');
  return (dot < 0 ? base : base.slice(0, dot)).toLowerCase();
}

module.exports = { parseMmp, readAxis, gf, stem };
