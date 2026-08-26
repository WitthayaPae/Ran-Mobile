'use strict';
//
// `.npcshop` — CNPCShopWindow's item catalog (SNPC_SHOP::LoadData,
// GLCrowDataNPCShop.cpp:13). This is a SEPARATE, more complex NPC-shop system
// from the `.crowsale`-fed sale-slot MARKET flow crowsale.js/RanShop.cs cover:
// EM_NPC_SHOP (SNpcTalk::EM_BASIC id 29, measured — see layout-probe's curated
// "npcBasic" dump) opens it via CNPCShopWindow::InitShop(sidCrow), which reads
// straight off the talked-to NPC's OWN crow record (pcrow->m_sNPCShop) — no
// network round trip for the catalog. 7 files ship (GLogic.rcc): betatestitem,
// clothing, kaderaitem, merchant, ninja_seller, pet, vehicle.
//
// FORMAT: a `CIniLoader` text file (NOT the gltexfile format crowsale.js
// parses) — key/value sections split on the FIRST '=' (IniLoader.cpp:182),
// then the value split on the registered separator set ",[]\t" (NPCShopWindow
// registers ",[]\t" — LoadData's reg_sep call). Crucially this set does NOT
// include space, so a value with an internal space (a shop/type title) comes
// back with its embedded spaces intact and only needs outer trim.
//
//   [SHOP_BASIC]
//   SHOP_TITLE = Merchant                  1 field
//   SHOP_OPTION = 0                        1 field (wShopType; currency kind —
//                                           measured 0 "gold" in all 7 files)
//   [SHOP_TYPE_LIST]
//   SHOP_TYPE = 10, Common Type            2 fields: wID, strName
//   [SHOP_ITEM_LIST]
//   SHOP_ITEM = 10, 161, 6[0,0,0,0,0,0,0,0]  11 fields: wType, wMainID, wSubID,
//     cDAMAGE, cDEFENSE, cRESIST_{FIRE,ICE,ELEC,POISON,SPIRIT}, bGenerateRandomValue
//
// PRICE IS NOT IN THIS FILE. Unlike `.crowsale` (which carries an optional
// per-row price), `SNPC_SHOP_ITEM` has no price field at all — the real
// client looks it up LIVE from item.isf at selection time
// (CNPCShopWindow::SelectItem, NPCShopWindow.cpp:544: `pItem->sBasicOp.
// dwBuyPrice`) and the server re-validates the SAME field at purchase
// (GLCharInvenMsg2.cpp:497). So this parser (and npcshoptables.json) carries
// only {type, mainId, subId} per item; price resolves through RanItemDb at
// display/request time, exactly mirroring the real architecture rather than
// baking in a value that could drift from the item table.
//
// The per-item combat-stat overrides (cDAMAGE/cDEFENSE/resist bytes,
// bGenerateRandomValue) are parsed but NOT carried into the JSON: they are
// applied SERVER-SIDE onto the generated item at purchase
// (GLCharInvenMsg2.cpp:584-590) and never travel in the buy request
// (SNETPC_REQ_NPCSHOP_PURCHASE_MONEY carries only sidCrow/sidItem/wShopType —
// measured via the layout probe), so the client never needs them to build a
// correct request or a correct list row.

const SEP = /[,[\]\t]/;   // CIniLoader field separator set registered by NPCShopWindow ",[]\t" (no space)

// Split a value string on the FIRST '=' the way CIniLoader::GetKeyData does
// (CString::Tokenize("=", pos) then Right(...)), so a title containing '='
// (none observed, but matching the real split point rather than a naive
// String.split keeps this exact).
function splitKeyValue(line) {
  const eq = line.indexOf('=');
  if (eq < 0) return null;
  return { key: line.slice(0, eq).trim(), value: line.slice(eq + 1).trim() };
}

// CIniLoader::split/advplain (IniLoader.cpp:334-364): scans for the next
// separator char from the current position and takes everything before it as
// a field, but ONLY pushes when the field is non-empty ("if (j > i)") — a
// separator immediately following another separator (or a leading one, e.g.
// the stray "," right before "[" some SHOP_ITEM rows have —
// "10, 111, 4,[0,0,...]" vs "10, 0, 0[0,0,...]") yields a zero-length field
// that is silently DROPPED, not emitted as "". Net effect, verified against
// both row shapes in the real merchant.npcshop/betatestitem.npcshop data:
// equivalent to splitting on a run of separator chars and filtering empties
// (so both row shapes end up with exactly 11 fields, not 12 or 10).
function splitFields(value) {
  return value.split(SEP).filter((t) => t.length > 0);
}

function isNote(line) {
  const t = line.replace(/^[ \t]+/, '');
  return t.startsWith(';');
}

function isSection(line) {
  const t = line.trim();
  if (t.length < 2 || t[0] !== '[' || t[t.length - 1] !== ']') return null;
  return t.slice(1, -1).trim();
}

/**
 * Parse a `.npcshop` text (latin1 string preserves bytes 1:1; gamecrypt-decode
 * the raw RCC bytes before calling this — same convention as crowsale.parse).
 * @returns {{title:string, shopType:number, types:[{id,name}], items:[{type,main,sub}]}}
 */
function parse(text) {
  let title = '';
  let shopType = 0;
  const types = [];
  const items = [];

  let section = '';
  for (const rawLine of text.split(/\r?\n/)) {
    const line = rawLine.replace(/\r$/, '');
    if (line.trim().length === 0 || isNote(line)) continue;
    const sec = isSection(line);
    if (sec !== null) { section = sec; continue; }

    const kv = splitKeyValue(line);
    if (!kv) continue;
    const fields = kv.value.length ? splitFields(kv.value) : [];

    if (section === 'SHOP_BASIC') {
      if (kv.key === 'SHOP_TITLE') title = fields[0] !== undefined ? fields[0].trim() : '';
      else if (kv.key === 'SHOP_OPTION') shopType = parseInt(fields[0], 10) || 0;
    } else if (section === 'SHOP_TYPE_LIST' && kv.key === 'SHOP_TYPE') {
      if (fields.length < 2) continue;
      const id = parseInt(fields[0], 10);
      const name = fields[1].trim();
      if (Number.isFinite(id)) types.push({ id, name });
    } else if (section === 'SHOP_ITEM_LIST' && kv.key === 'SHOP_ITEM') {
      if (fields.length < 11) continue;
      const type = parseInt(fields[0], 10);
      const main = parseInt(fields[1], 10);
      const sub = parseInt(fields[2], 10);
      if (Number.isFinite(type) && Number.isFinite(main) && Number.isFinite(sub)) {
        items.push({ type, main, sub });
      }
    }
  }

  // SNPC_SHOP_TYPE_MAP is a std::map<WORD,SNPC_SHOP_TYPE> — CNPCShopWindow::
  // InitShop populates the category tab list by iterating it, i.e. in id
  // order, NOT file order (NPCShopWindow.cpp:410-419). Reproduced here so the
  // extracted tab order matches what the real window shows. wItemNum (the
  // "(N)" suffix CreateSub's InitShop appends) is derived the same way
  // SNPC_SHOP::LoadData derives it: one increment per item whose wType
  // resolves to that shop type (GLCrowDataNPCShop.cpp:69-71).
  types.sort((a, b) => a.id - b.id);
  const countByType = new Map();
  for (const it of items) countByType.set(it.type, (countByType.get(it.type) || 0) + 1);
  for (const t of types) t.count = countByType.get(t.id) || 0;

  return { title, shopType, types, items };
}

module.exports = { parse, splitFields, splitKeyValue };
