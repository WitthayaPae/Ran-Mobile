'use strict';
//
// Stage character prefabs into a Resources folder so the runtime can load them.
//
//   node stage-characters.js               # default scope: npc + mob + bodies
//   node stage-characters.js --scope npc    # npc + player bodies only
//   node stage-characters.js --scope bodies # player bodies only
//   node stage-characters.js --dry          # measure + log, copy nothing
//   node stage-characters.js --clean        # remove the staged copies
//
// WHY A COPY INTO Resources/Characters
// -----------------------------------
// The 977 prefabs under Assets/Ran/Characters are editor-only: nothing outside a
// Resources/ folder is loadable in a build, so RanEntityView can never
// Resources.Load them and every mob/NPC falls back to a capsule. Copying a
// selected prefab into Assets/Ran/Resources/Characters/ makes it
// Resources.Load("Characters/<name>")-able; Unity then pulls that prefab's whole
// dependency graph (the .rmesh mesh + material sub-assets, and the material's
// Texture2D) into the build automatically — those are referenced by GUID and
// need not move. So only the prefab is staged; textures/meshes come along.
//
// The copy carries a FRESH deterministic .meta GUID (md5 of its Resources path,
// the same scheme stage-ui-atlases.js uses) rather than the original's, because
// two assets may not share a GUID. It is a non-destructive copy: the editor set
// under Characters/ is left exactly as-is.
//
// SCOPE — the FULL referenced set, driven by crowmodels.json
// ----------------------------------------------------------
// The default scope now stages EVERY referenced entity skin the crow table names
// (all crow skin stems + the o_m/o_w player bodies), not just the subset that
// happens to have a prefab today. The NPC nativeID list references a subset of
// the same crow skins, so it needs no separate merge. This is what makes the set
// reproducible from data alone: whatever crowmodels.json references is what gets
// staged, so no live mob/NPC is a capsule for lack of a staged model.
//
// Some of those prefabs are built LATER (RanCharacterBuilder.BuildMissing writes
// the ~158 that were dedup-collapsed under other names). A referenced skin whose
// prefab does not exist yet is reported as `pending` and skip-and-warned in the
// copy loop — never an error — so the recommended order is: BuildMissing (in
// Unity), then re-run this script to pick up the rest. Running it before
// BuildMissing stages what exists and lists the remainder.
//
// Narrower scopes remain for measurement: `--scope npc` (NPC skins + bodies),
// `--scope mob` (everything else + bodies), `--scope bodies` (o_m/o_w only).
//
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const base = path.resolve(__dirname, '../../..');
const ASSETS = path.join(base, 'MOBILE/unity/RanMobile/Assets/Ran');
const CH = path.join(ASSETS, 'Characters');
const MESHES = path.join(ASSETS, 'meshes');
const TEX = path.join(ASSETS, 'Textures');
const OUT = path.join(ASSETS, 'Resources/Characters');
const CROWMODELS = path.join(ASSETS, 'Resources/crowmodels.json');
// equipmodels.json (extract-equipmodels.js) names the worn attach parts
// (.abl -> .abf) RanEquipView loads; `--scope equip` stages those .abf/.chf
// attach prefabs into the same Resources/Characters folder as the bodies.
const EQUIPMODELS = path.join(ASSETS, 'Resources/equipmodels.json');

const args = process.argv.slice(2);
const DRY = args.includes('--dry');
const CLEAN = args.includes('--clean');
const scopeArg = args.indexOf('--scope');
// >= 0, not > 0: `--scope` can be argv[0] here (args already drops node+script).
const SCOPE = scopeArg >= 0 ? args[scopeArg + 1] : 'all';   // all | npc | mob | bodies

// --- prefab dependency footprint --------------------------------------------
function nibbleSwap(h) { let o = ''; for (let i = 0; i < h.length; i += 2) o += h[i + 1] + h[i]; return o; }
function indexMeshes() {
  const out = [];
  for (const f of fs.readdirSync(MESHES)) {
    if (!f.endsWith('.rmesh.meta')) continue;
    const stem = f.slice(0, -5);
    const g = /guid: ([0-9a-f]{32})/.exec(fs.readFileSync(path.join(MESHES, f), 'utf8'));
    if (!g) continue;
    let size = 0; try { size = fs.statSync(path.join(MESHES, stem)).size; } catch {}
    out.push({ buf: Buffer.from(nibbleSwap(g[1]), 'hex'), name: stem, size });
  }
  return out;
}
function rmeshTextures(file) {
  const b = fs.readFileSync(file);
  if (b.length < 44 || b.toString('latin1', 0, 4) !== 'RMSH') return [];
  const bones = b.readUInt32LE(12), meshCount = b.readUInt32LE(16);
  const submeshCount = b.readUInt32LE(20), stringsLen = b.readUInt32LE(36);
  const poolStart = 44;                                  // head(44), strings, ...
  const subStart = 44 + stringsLen + bones * 72 + meshCount * 40;
  const names = new Set();
  for (let i = 0; i < submeshCount; i++) {
    const off = b.readUInt32LE(subStart + i * 12);
    const at = poolStart + off; if (at < poolStart || at >= b.length) continue;
    let e = at; while (e < b.length && b[e] !== 0) e++;
    const s = b.toString('latin1', at, e); if (s) names.add(s.toLowerCase());
  }
  return [...names];
}
function texSize(name) {
  const stem = name.replace(/\.[a-z0-9]+$/, '') + '.png';
  try { return fs.statSync(path.join(TEX, stem)).size; } catch { return 0; }
}
function measure(prefabNames, meshIdx) {
  const meshes = new Set(), textures = new Set();
  for (const nm of prefabNames) {
    const pf = path.join(CH, nm + '.prefab');
    if (!fs.existsSync(pf)) continue;
    const bytes = fs.readFileSync(pf);
    for (const m of meshIdx) if (bytes.includes(m.buf)) meshes.add(m.name);
  }
  let meshBytes = 0;
  for (const mn of meshes) {
    const rec = meshIdx.find(m => m.name === mn); meshBytes += rec ? rec.size : 0;
    for (const t of rmeshTextures(path.join(MESHES, mn))) textures.add(t);
  }
  let texBytes = 0, texHave = 0;
  for (const t of textures) { const s = texSize(t); if (s) texHave++; texBytes += s; }
  return { meshes: meshes.size, meshMB: meshBytes / 1048576,
           textures: textures.size, texHave, texMB: texBytes / 1048576 };
}

// The worn attach-part prefabs RanEquipView needs: every distinct .abf named by
// an equipmodels.json link, mapped to the prefab file that actually exists
// (`<abf>.abf`, or `<abf>.chf`/bare as a fallback). Prefabs not built yet are
// simply absent from the returned set and reported as pending by the caller.
function selectEquipPrefabs(prefabsAvail) {
  if (!fs.existsSync(EQUIPMODELS)) {
    console.log(`! equipmodels.json missing (${path.relative(base, EQUIPMODELS)}) — run extract-equipmodels.js first`);
    return new Set();
  }
  const em = JSON.parse(fs.readFileSync(EQUIPMODELS, 'utf8'));
  const abfs = new Set(Object.values(em.links || {}).map(l => String(l.abf).toLowerCase()));
  const out = new Set();
  for (const abf of abfs) {
    for (const cand of [abf + '.abf', abf + '.chf', abf]) {
      if (prefabsAvail.has(cand)) { out.add(cand); break; }
    }
  }
  return out;
}

// --- selection ---------------------------------------------------------------
function main() {
  const prefabsAvail = new Set(fs.readdirSync(CH)
    .filter(f => f.endsWith('.prefab')).map(f => f.slice(0, -7).toLowerCase()));

  const cm = JSON.parse(fs.readFileSync(CROWMODELS, 'utf8'));
  // crows is a flat [id, name, id, name, ...] array; players is [bit, body, ...].
  const crowSkins = new Set();
  const idToStem = new Map();
  for (let i = 0; i < cm.crows.length; i += 2) {
    const stem = String(cm.crows[i + 1]).toLowerCase();
    idToStem.set(cm.crows[i], stem);
    crowSkins.add(stem);
  }
  const playerBodies = new Set();
  for (let i = 1; i < cm.players.length; i += 2) playerBodies.add(String(cm.players[i]).toLowerCase());

  // NPC skins come from the CROW_NPC nativeID list (m_emCrow == CROW_NPC), not a
  // name-prefix guess: the runtime tags tappable NPCs off exactly these ids, and
  // every skin they point at is one of the crow skins already. mob = the rest.
  const npcSkins = new Set();
  for (const id of cm.npcs || []) { const s = idToStem.get(id); if (s) npcSkins.add(s); }
  const mobSkins = new Set([...crowSkins].filter(s => !npcSkins.has(s)));

  // The FULL referenced set, driven purely by crowmodels.json — NOT filtered to
  // what has a prefab today. That is the whole point: the 158 crow skins whose
  // prefab is built later by RanCharacterBuilder.BuildMissing must be in the
  // selection now, so a single re-run after BuildMissing stages them. A prefab
  // that does not exist yet is skip-and-warned below, never an error.
  let selected;
  if (SCOPE === 'bodies') selected = [...playerBodies];
  else if (SCOPE === 'npc') selected = [...new Set([...npcSkins, ...playerBodies])];
  else if (SCOPE === 'mob') selected = [...new Set([...mobSkins, ...playerBodies])];
  else if (SCOPE === 'equip') selected = [...selectEquipPrefabs(prefabsAvail)]; // worn attach parts only
  else selected = [...new Set([...crowSkins, ...playerBodies])];   // 'all' = every referenced skin

  const present = selected.filter(s => prefabsAvail.has(s));
  const pending = selected.filter(s => !prefabsAvail.has(s));   // referenced but not built yet

  console.log(`scope: ${SCOPE}`);
  console.log(`referenced entity skins: crow ${crowSkins.size} (npc ${npcSkins.size}, mob/other ${mobSkins.size}), ` +
              `player bodies ${playerBodies.size}`);
  console.log(`selected to stage (full referenced set): ${selected.length}  ` +
              `(prefab built ${present.length}, not built yet ${pending.length})`);

  if (CLEAN) {
    if (fs.existsSync(OUT)) fs.rmSync(OUT, { recursive: true, force: true });
    console.log('removed staged copies at Resources/Characters');
    return;
  }

  const meshIdx = indexMeshes();
  const foot = measure(present, meshIdx);
  console.log(`footprint of built (source PNG/mesh, deduped): meshes ${foot.meshes} = ${foot.meshMB.toFixed(1)} MB, ` +
              `textures ${foot.textures} (${foot.texHave} present) = ${foot.texMB.toFixed(1)} MB, ` +
              `TOTAL ${(foot.meshMB + foot.texMB).toFixed(1)} MB`);
  console.log(`  (in-APK is smaller: textures transcode PNG->ETC2/ASTC at build)`);

  // Pending prefabs are reported, not dropped silently: they belong in the set
  // and will stage on the next run once BuildMissing has produced them.
  if (pending.length) {
    console.log(`pending: ${pending.length} referenced skins have NO prefab YET ` +
                `(run RanCharacterBuilder.BuildMissing, then re-run this). ` +
                `First: ${pending.slice(0, 12).join(', ')}${pending.length > 12 ? ' ...' : ''}`);
  } else {
    console.log('pending: 0 — every referenced skin has a built prefab.');
  }
  const nonCrowPrefabs = prefabsAvail.size - present.length;
  console.log(`not bundled: ${nonCrowPrefabs} built prefabs are not referenced by any crow/body ` +
              `(costumes/wings/weapons/pets).`);

  if (DRY) {
    console.log(`(dry run — would stage ${present.length} built prefabs now, ` +
                `${pending.length} more after BuildMissing; copied nothing)`);
    return;
  }

  fs.mkdirSync(OUT, { recursive: true });
  let copied = 0, skipped = 0, warned = 0;
  for (const nm of selected) {
    const srcPrefab = path.join(CH, nm + '.prefab');
    if (!fs.existsSync(srcPrefab)) { warned++; continue; }   // pending: build it first, then re-run
    const dstPrefab = path.join(OUT, nm + '.prefab');
    if (fs.existsSync(dstPrefab)) { skipped++; continue; }
    fs.copyFileSync(srcPrefab, dstPrefab);
    const guid = crypto.createHash('md5')
      .update('Assets/Ran/Resources/Characters/' + nm + '.prefab').digest('hex').slice(0, 32);
    fs.writeFileSync(dstPrefab + '.meta',
      `fileFormatVersion: 2\nguid: ${guid}\nPrefabImporter:\n` +
      `  externalObjects: {}\n  userData: \n  assetBundleName: \n  assetBundleVariant: \n`);
    copied++;
  }
  console.log(`staged ${copied} prefabs (${skipped} already present, ` +
              `${warned} skipped — not built yet) into Assets/Ran/Resources/Characters`);

  // Verify the count staged matches what a runtime lookup would find.
  const onDisk = fs.readdirSync(OUT).filter(f => f.endsWith('.prefab')).length;
  console.log(`verify: ${onDisk} prefabs now under Resources/Characters (loadable as "Characters/<name>")`);
  if (warned) console.log(`note: re-run after BuildMissing to stage the remaining ${warned}.`);
}

main();
