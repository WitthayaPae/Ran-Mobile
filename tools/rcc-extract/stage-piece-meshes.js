'use strict';
// Copy every equippieces.json piece MESH .rmesh that is not yet imported in
// the editor project from the extraction (MOBILE/assets/meshes) into
// Assets/Ran/meshes, with a deterministic .meta (md5-of-path GUID, same scheme
// stage-characters.js uses) and the SAME RanMeshImporter settings as the
// project's existing meshes. 2026-08-23, "make all the skins work": 124 of the
// 1,063 piece meshes only existed in the extraction, so their prefabs could
// never be built.
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const base = path.resolve(__dirname, '../../..');
const SRC = path.join(base, 'MOBILE/assets/meshes');
const DST = path.join(base, 'MOBILE/unity/RanMobile/Assets/Ran/meshes');
const EP = path.join(base, 'MOBILE/unity/RanMobile/Assets/Ran/Resources/equippieces.json');

const ep = JSON.parse(fs.readFileSync(EP, 'utf8'));
const meshes = new Set();
for (const k in ep.pieceLinks) {
  const v = ep.pieceLinks[k];
  if (v && v.m) meshes.add(String(v.m).toLowerCase());
}

const meta = (relPath) => `fileFormatVersion: 2
guid: ${crypto.createHash('md5').update(relPath).digest('hex')}
ScriptedImporter:
  internalIDToNameTable: []
  externalObjects: {}
  serializedVersion: 2
  userData: 
  assetBundleName: 
  assetBundleVariant: 
  script: {fileID: 11500000, guid: 9978348fe00b1d24cbede006161d4d23, type: 3}
  textureFolder: Assets/Ran/Textures
  materialTemplate: {instanceID: 0}
  recalculateMissingNormals: 1
`;

const have = new Set(fs.readdirSync(DST).map(f => f.toLowerCase()));
let copied = 0, present = 0, absent = 0;
for (const m of meshes) {
  const f = m + '.rmesh';
  if (have.has(f)) { present++; continue; }
  const src = path.join(SRC, f);
  if (!fs.existsSync(src)) { absent++; console.log('  MISSING SOURCE ' + f); continue; }
  fs.copyFileSync(src, path.join(DST, f));
  fs.writeFileSync(path.join(DST, f + '.meta'), meta('Assets/Ran/meshes/' + f));
  copied++;
}
console.log(`piece meshes: ${meshes.size} | already imported ${present} | copied ${copied} | no source ${absent}`);
