'use strict';
//
// Build the two staged tables the mobile NPC system needs, from GLogic.rcc +
// NpcTalk.rcc:
//
//   node extract-npcshops.js           write npcshops.json + npcdialogue.json
//   node extract-npcshops.js --dry     parse + report, write nothing
//   node extract-npcshops.js --out DIR write the two json into DIR
//
// PIPELINE (all cites in the module headers crowdata.js / crowsale.js /
// npcdialogue.js):
//
//   Crow.mnsf ──crowdata──▶ per-NPC { nativeID, nameKey, talkFile, saleFiles[3] }
//   CrowStrTable.txt ─────▶ nameKey ("CN_mmm_sss") → display name (cp874)
//   item.isf  ──itemdata──▶ (mainId,subId) → { invenX, invenY, dwBuyPrice }
//
//   BUY STOCK  = each saleFile (.crowsale) parsed → items laid into a 6x8 grid
//     exactly as GLInventory does (row-major FindInsrtable, explicit slots when
//     the file gives them). The BUY packet keys on (channel, slotX, slotY), so
//     the grid position is baked in, and the price is the file's own value or the
//     item's dwBuyPrice fallback (GLCharInvenMsg.cpp:4004-4015).
//
//   DIALOGUE   = each talkFile (.ntk) parsed → greeting (dialogue nid 1's default
//     case, nSTARTINDEX) + service flags (IsMARKET→shop, IsSTARTPOINT→warp, any
//     EM_QUEST_START talk→quest). Missing / unparseable talk files are skipped so
//     that NPC keeps the generic dialog fallback at runtime.
//
// cp874 strings (name, greeting) are stored latin1 (bytes 1:1 as chars 0..255),
// the exact shape RanText.DecodeCp874(string) expects, matching itemdb/questdb.

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { RccArchive } = require('./rcc.js');
const gamecrypt = require('./gamecrypt.js');
const crowdata = require('./crowdata.js');
const crowsale = require('./crowsale.js');
const itemdata = require('./itemdata.js');
const npcdialogue = require('./npcdialogue.js');
const npcshop = require('./npcshop.js');

const base = path.resolve(__dirname, '../../..');
const RAN = path.join(base, 'Ran');
const RES = path.join(base, 'MOBILE/unity/RanMobile/Assets/Ran/Resources');
const OUT_SHOPS = path.join(RES, 'npcshops.json');
const OUT_DLG = path.join(RES, 'npcdialogue.json');
const OUT_SHOPTABLES = path.join(RES, 'npcshoptables.json');

const argv = process.argv.slice(2);
const has = (f) => argv.includes(f);
const val = (f, d) => { const i = argv.indexOf(f); return i === -1 ? d : argv[i + 1]; };

// Sale inventory grid — SALE_INVEN_X/Y (GLCharDefine.h:82).
const GRID_X = 6, GRID_Y = 8;

function findRcc(re) {
  let found = null;
  (function scan(dir) {
    for (const it of fs.readdirSync(dir, { withFileTypes: true })) {
      if (found) return;
      const p = path.join(dir, it.name);
      if (it.isDirectory()) scan(p);
      else if (re.test(it.name)) found = p;
    }
  })(path.join(RAN, 'data'));
  return found;
}

function loadGlogic(arc, name) {
  let b = arc.read(name);
  if (gamecrypt.isEncoded(b)) b = gamecrypt.decode(b);
  return b;
}

// CrowStrTable.txt: KEY<tab>Name, comment cut at first '/', tokenised on TAB
// (GLStringTable.cpp:103-110). Names are raw cp874, kept latin1.
function parseCrowStrTable(buf) {
  const map = new Map();
  const text = buf.toString('latin1');
  for (const line of text.split(/\r?\n/)) {
    const noComment = line.split('/')[0];           // SpanExcluding("/")
    const parts = noComment.split('\t').filter((t) => t.length > 0);
    if (parts.length < 1) continue;
    const key = parts[0].trim();
    if (!key) continue;
    const name = parts.length > 1 ? parts[1].replace(/\s+$/, '') : '';
    if (!map.has(key)) map.set(key, name);
  }
  return map;
}

// Item lookup keyed by SNATIVEID.dwID = mainId | (subId<<16).
function buildItemMap(itemBuf) {
  const parsed = itemdata.parse(itemBuf);
  const map = new Map();
  for (const it of parsed.items) {
    if (!it.basic) continue;
    const b = it.basic;
    map.set(b.nativeId, { x: b.invenX, y: b.invenY, price: b.buyPrice >>> 0 });
  }
  return { map, parsedCount: parsed.items.length };
}

// Lay a channel's items into the 6x8 grid, mirroring GLInventory. Returns the
// placed entries with their final (slotX, slotY). Unknown items (no item-DB
// record → GetItem null → InsertItem FALSE) are skipped and consume no cell.
function placeChannel(items, itemMap, channel, stats) {
  const grid = Array.from({ length: GRID_X }, () => new Array(GRID_Y).fill(false));
  const free = (x, y, w, h) => {
    if (x + w > GRID_X || y + h > GRID_Y) return false;
    for (let i = 0; i < w; i++) for (let j = 0; j < h; j++) if (grid[x + i][y + j]) return false;
    return true;
  };
  const mark = (x, y, w, h) => { for (let i = 0; i < w; i++) for (let j = 0; j < h; j++) grid[x + i][y + j] = true; };

  const out = [];
  for (const it of items) {
    const dwID = (it.main | (it.sub << 16)) >>> 0;
    const info = itemMap.get(dwID);
    if (!info) { stats.unknownItems++; continue; }
    const w = info.x, h = info.y;
    if (w < 1 || h < 1 || w > GRID_X || h > GRID_Y) { stats.badSize++; continue; }

    let sx = -1, sy = -1;
    if (it.posX !== null) {                            // explicit slot (13/14-param)
      if (free(it.posX, it.posY, w, h)) { sx = it.posX; sy = it.posY; }
      // overlap → skipped, exactly as InsertItem(bLOAD) logs and drops
    } else {                                            // auto-insert (11/12-param)
      outer:
      for (let cy = 0; cy <= GRID_Y - h; cy++)
        for (let cx = 0; cx <= GRID_X - w; cx++)
          if (free(cx, cy, w, h)) { sx = cx; sy = cy; break outer; }
    }
    if (sx < 0) { stats.noRoom++; continue; }
    mark(sx, sy, w, h);
    const price = it.price !== null ? (it.price >>> 0) : info.price;
    out.push({ m: it.main, s: it.sub, c: channel, x: sx, y: sy, p: price });
  }
  return out;
}

function run() {
  const glogicRcc = findRcc(/^glogic\.rcc$/i);
  const ntkRcc = findRcc(/^npctalk\.rcc$/i);
  if (!glogicRcc || !ntkRcc) { console.log('! GLogic.rcc / NpcTalk.rcc not found'); process.exit(1); }
  console.log('GLogic.rcc :', path.relative(base, glogicRcc));
  console.log('NpcTalk.rcc:', path.relative(base, ntkRcc));

  const gArc = new RccArchive(glogicRcc);
  const ntkArc = new RccArchive(ntkRcc);
  const ntkNames = new Set(ntkArc.list().map((n) => n.toLowerCase()));

  // --- crow index ---
  const crowParsed = crowdata.parse(gArc.read('Crow.mnsf'));
  console.log(`Crow.mnsf: ${crowParsed.count} records, EOF-exact=${crowParsed.eofExact} (trailing ${crowParsed.trailing})`);

  // --- name table ---
  let nameMap = new Map();
  const strTableName = gArc.list().find((n) => /^crowstrtable\.txt$/i.test(n));
  if (strTableName) { nameMap = parseCrowStrTable(loadGlogic(gArc, strTableName)); }
  console.log(`CrowStrTable.txt: ${nameMap.size} name keys`);

  // --- item DB ---
  let itemBuf = gArc.read('item.isf');
  if (gamecrypt.isEncoded(itemBuf)) itemBuf = gamecrypt.decode(itemBuf);
  const { map: itemMap, parsedCount } = buildItemMap(itemBuf);
  console.log(`item.isf: ${parsedCount} items, ${itemMap.size} keyed`);

  // --- CNPCShopWindow tables (.npcshop) ---
  // A SEPARATE, more complex system from the sale-slot MARKET flow above:
  // EM_NPC_SHOP (SNpcTalk::EM_BASIC id 29 — measured by the layout probe's
  // curated "npcBasic" dump, not hand-counted) opens CNPCShopWindow straight
  // off the talked-to NPC's OWN crow record (m_sNPCShop, loaded from
  // m_strShopFile at crow-load time) — categorized by SHOP_TYPE_LIST, paged
  // NPC_SHOP_MAX_ITEM=20 per page, items named by (mainId,subId) directly
  // (not a sale-inventory slot). See npcshop.js's header for the full format
  // + why price is NOT stored here (resolved live from item.isf instead, the
  // same way the real client does).
  const shopFiles = new Map(gArc.list()
    .filter((n) => /\.npcshop$/i.test(n))
    .map((n) => [n.toLowerCase(), n]));
  console.log(`GLogic.rcc: ${shopFiles.size} .npcshop files`);

  const shopTables = {};
  const shopTableStats = { parsed: 0, failed: 0 };
  for (const [lc, realName] of shopFiles) {
    try {
      const r = npcshop.parse(loadGlogic(gArc, realName).toString('latin1'));
      shopTables[lc] = {
        t: r.title,
        s: r.shopType,
        ty: r.types.map((t) => [t.id, t.name, t.count]),
        it: r.items.map((it) => [it.type, it.main, it.sub]),
      };
      shopTableStats.parsed++;
    } catch (e) {
      shopTableStats.failed++;
      console.log(`  ! ${realName} failed: ${e.message}`);
    }
  }
  console.log(`.npcshop tables: parsed=${shopTableStats.parsed} failed=${shopTableStats.failed}`);

  // --- build shops + dialogue ---
  const shops = {};
  const npcs = {};
  const stockStats = { unknownItems: 0, badSize: 0, noRoom: 0 };
  const dlgCache = new Map();   // talkFile(lc) -> parsed result | null
  const rep = {
    npcsWithSale: 0, shopsEmitted: 0, totalStock: 0, emptyShops: 0,
    talkRefs: 0, talkMissing: 0, talkParseFail: 0, dialoguesEmitted: 0,
    withGreeting: 0, offerShop: 0, offerWarp: 0, offerQuest: 0,
    pagesEmitted: 0, answersEmitted: 0,
    npcShopRefs: 0, npcShopMissing: 0, npcShopLinked: 0,
  };
  const failures = [];

  for (const cr of crowParsed.crows) {
    const key = `${cr.main}:${cr.sub}`;
    const name = nameMap.get(cr.nameKey) || cr.nameKey || '';

    // ----- buy stock -----
    if (cr.saleFiles.some((f) => f)) {
      rep.npcsWithSale++;
      const stock = [];
      for (let ch = 0; ch < 3; ch++) {
        const fn = cr.saleFiles[ch];
        if (!fn) continue;
        let text;
        try { text = loadGlogic(gArc, fn).toString('latin1'); }
        catch { failures.push(`${key}: sale file "${fn}" not in archive`); continue; }
        const cs = crowsale.parse(text);
        for (const e of placeChannel(cs.items, itemMap, ch, stockStats)) stock.push(e);
      }
      if (stock.length) { shops[key] = stock; rep.shopsEmitted++; rep.totalStock += stock.length; }
      else rep.emptyShops++;
    }

    // ----- dialogue -----
    if (cr.talkFile) {
      rep.talkRefs++;
      const lc = cr.talkFile.toLowerCase();
      let parsed = dlgCache.get(lc);
      if (parsed === undefined) {
        if (!ntkNames.has(lc)) { parsed = null; rep.talkMissing++; failures.push(`${key}: talk file "${cr.talkFile}" not in NpcTalk.rcc`); }
        else {
          try { parsed = npcdialogue.parse(ntkArc.read(cr.talkFile)); }
          catch (e) { parsed = null; rep.talkParseFail++; failures.push(`${key}: talk file "${cr.talkFile}" parse: ${e.message}`); }
        }
        dlgCache.set(lc, parsed);
      }
      if (parsed) {
        const shop = parsed.flags.market ? 1 : 0;
        const warp = parsed.flags.startPoint ? 1 : 0;
        const quest = parsed.hasQuest ? 1 : 0;
        const greeting = parsed.greetingRaw.toString('latin1');
        // Full authored page tree (greeting + answer list per page). The runtime
        // renders the tapped NPC's REAL answers instead of a fixed 4-button menu,
        // and navigates page-move answers within this tree. shop/warp/quest above
        // stay for back-compat and as the generic fallback when pg is absent.
        const tree = npcdialogue.buildNpcTree(parsed);
        const rec = { nm: name, g: greeting, shop, warp, quest, sp: tree.startPage, pg: tree.pages };
        npcs[key] = rec;
        rep.dialoguesEmitted++;
        rep.pagesEmitted += Object.keys(tree.pages).length;
        for (const p in tree.pages) rep.answersEmitted += tree.pages[p].a.length;
        if (greeting) rep.withGreeting++;
        if (shop) rep.offerShop++;
        if (warp) rep.offerWarp++;
        if (quest) rep.offerQuest++;
      }
    }

    // ----- CNPCShopWindow link (m_strShopFile -> .npcshop) -----
    // Independent of whether the talk tree parsed: a shop-only NPC (no
    // decoded .ntk, e.g. a test/legacy record) still gets an "ns" entry so
    // RanNpcShopWindow can open FOR IT if the caller reaches it some other
    // way; the generic-menu fallback also reads this flag to offer an
    // "NPC Shop" button, mirroring the shop/warp/quest generic flags above.
    if (cr.shopFile) {
      rep.npcShopRefs++;
      const lc = cr.shopFile.toLowerCase();
      if (!shopTables[lc]) {
        rep.npcShopMissing++;
        failures.push(`${key}: shop file "${cr.shopFile}" not in GLogic.rcc`);
      } else {
        rep.npcShopLinked++;
        if (!npcs[key]) npcs[key] = { nm: name, g: '', shop: 0, warp: 0, quest: 0, sp: 1, pg: {} };
        npcs[key].ns = lc;
      }
    }
  }

  console.log(`\nSHOPS  NPCs-with-sale=${rep.npcsWithSale}  emitted=${rep.shopsEmitted}  stock-entries=${rep.totalStock}  empty=${rep.emptyShops}`);
  console.log(`       placement: unknown-item=${stockStats.unknownItems}  bad-size=${stockStats.badSize}  no-room=${stockStats.noRoom}`);
  console.log(`DLG    talk-refs=${rep.talkRefs}  emitted=${rep.dialoguesEmitted}  missing=${rep.talkMissing}  parse-fail=${rep.talkParseFail}`);
  console.log(`       greeting=${rep.withGreeting}  offer shop=${rep.offerShop} warp=${rep.offerWarp} quest=${rep.offerQuest}`);
  console.log(`       tree: pages=${rep.pagesEmitted}  answers=${rep.answersEmitted}`);
  console.log(`NPCSHOP refs=${rep.npcShopRefs}  linked=${rep.npcShopLinked}  missing=${rep.npcShopMissing}  tables=${Object.keys(shopTables).length}`);
  if (failures.length) {
    console.log(`\nanomalies (${failures.length}, all expected — test/legacy NPCs):`);
    for (const f of failures.slice(0, 12)) console.log('  ' + f);
  }

  if (has('--dry')) { console.log('\n--dry: nothing written'); return; }

  const outDir = val('--out', null);
  const shopsPath = outDir ? path.join(outDir, 'npcshops.json') : OUT_SHOPS;
  const dlgPath = outDir ? path.join(outDir, 'npcdialogue.json') : OUT_DLG;
  const shopTablesPath = outDir ? path.join(outDir, 'npcshoptables.json') : OUT_SHOPTABLES;

  const shopNote = 'NPC buy stock from Crow.mnsf sale files (.crowsale). key "main:sub" -> ' +
    '[{m,s,c(channel),x,y(sale-grid slot),p(price)}]. See extract-npcshops.js.';
  const dlgNote = 'NPC dialogue from .ntk (CNpcDialogueSet). key "main:sub" -> ' +
    '{nm(name),g(greeting) raw cp874; shop/warp/quest 0|1 (generic fallback); ' +
    'sp(startPage); pg{<nid>:{g(greeting),a:[{t(text cp874),k(action kind),' +
    'b(basic sub),tg(target page),ic(icon idx),gid(talk glob),q(quest id)}]}}; ' +
    'ns(lowercased .npcshop filename, into npcshoptables.json, when this NPC has ' +
    'the SEPARATE CNPCShopWindow system, EM_NPC_SHOP/basic id 29)}. ' +
    'See extract-npcshops.js + npcdialogue.js buildNpcTree + npcshop.js.';
  const shopTablesNote = 'CNPCShopWindow catalogs (.npcshop, GLCrowDataNPCShop.cpp). ' +
    'key = lowercased filename -> {t(title),s(wShopType; 0=gold, all 7 shipped files), ' +
    'ty:[[id,name,itemCount],...] sorted by id (std::map order, matches the real tab ' +
    'list), it:[[type,mainId,subId],...]}. NO PRICE FIELD — .npcshop carries none; ' +
    'the real client resolves it live from item.isf (dwBuyPrice) at selection time ' +
    '(NPCShopWindow.cpp:544) and the server re-validates the same field at purchase ' +
    '(GLCharInvenMsg2.cpp:497), so this port does the same via RanItemDb rather than ' +
    'baking in a value that could drift. See extract-npcshops.js + npcshop.js.';

  writeJson(shopsPath, { note: shopNote, shops }, 'Assets/Ran/Resources/npcshops.json');
  writeJson(dlgPath, { note: dlgNote, npcs }, 'Assets/Ran/Resources/npcdialogue.json');
  writeJson(shopTablesPath, { note: shopTablesNote, tables: shopTables }, 'Assets/Ran/Resources/npcshoptables.json');
}

function writeJson(outPath, obj, assetPath) {
  fs.writeFileSync(outPath, JSON.stringify(obj));
  console.log(`wrote ${path.relative(base, outPath)} (${(fs.statSync(outPath).size / 1024).toFixed(0)} KB)`);
  const metaPath = outPath + '.meta';
  if (!fs.existsSync(metaPath)) {
    const guid = crypto.createHash('md5').update(assetPath).digest('hex').slice(0, 32);
    fs.writeFileSync(metaPath, `fileFormatVersion: 2
guid: ${guid}
TextScriptImporter:
  externalObjects: {}
  userData:
  assetBundleName:
  assetBundleVariant:
`);
    console.log(`wrote ${path.basename(outPath)}.meta (guid ${guid})`);
  }
}

if (require.main === module) run();
module.exports = { parseCrowStrTable, placeChannel, findRcc };
