'use strict';
//
// `.crowsale` — an NPC's sale inventory (`SaleInvenLoadFile`, GLCrowDataLoad.cpp:27).
//
// This is the list a "Shop" (IsMARKET) NPC offers to BUY. It is a plaintext
// `gltexfile` (INI-ish, tab/space/comma/bracket separated), so no decryption —
// just tokenisation. The engine reads it into a 6x8 grid inventory
// (SALE_INVEN_X=6, SALE_INVEN_Y=8), and the BUY packet later identifies an item
// by that grid slot, so the layout matters (computed by extract-npcshops.js).
//
// Two line shapes, chosen by the FIRST cInventory line's token count
// (getparamnum includes the "cInventory" tag → 11/12 vs 13/14):
//
//   11/12 tokens  auto-placed   [tag main sub turnNum dmg def r r r r r (price)]
//   13/14 tokens  explicit slot [tag posX posY main sub turnNum dmg def r r r r r (price)]
//
// The trailing per-line price (token 11 / token 13) is `GetNpcSellPrice`; when it
// is absent the engine falls back to the item's own dwBuyPrice.
//
// Tokeniser mirrors gltexfile: split on \t space , [ ] and drop empty tokens; a
// token beginning "//" (or the "//" inside one) truncates the rest of the line
// (gltexfile.cpp:66-81). Comments here are the Korean item names after each row.

// gltexfile separators registered by SaleInvenLoadFile: \t space , [ ]
const SEP = /[\t ,[\]]+/;

function tokenize(line) {
  const cut = line.indexOf('//');           // gltexfile drops from the comment on
  const body = cut === -1 ? line : line.slice(0, cut);
  return body.split(SEP).filter((t) => t.length > 0);
}

function toInt(tok) {
  const n = parseInt(tok, 10);
  return Number.isFinite(n) ? n : 0;
}

/**
 * Parse a `.crowsale` text (latin1 string preserves bytes 1:1).
 * @returns {{version, saleType, positioned, items:[{main,sub,turnNum,posX,posY,price}]}}
 */
function parse(text) {
  let version = 0;
  let saleType = '';
  const rows = [];                            // raw cInventory token arrays, file order

  for (const rawLine of text.split(/\r?\n/)) {
    const toks = tokenize(rawLine);
    if (toks.length === 0) continue;
    const key = toks[0];
    if (key === 'VERSION') { version = toInt(toks[1]); continue; }
    if (key === 'szSaleType') { saleType = toks.slice(1).join(' '); continue; }
    if (key === 'cInventory') rows.push(toks);
  }

  if (rows.length === 0) return { version, saleType, positioned: false, items: [] };

  // Branch on the first row's token count, exactly as SaleInvenLoadFile does with
  // getparamnum("cInventory").
  const first = rows[0].length;
  const positioned = first === 13 || first === 14;
  const items = [];

  for (const t of rows) {
    const n = t.length;                       // this row's own token count (price is per-row)
    if (!positioned) {
      // [tag main sub turnNum dmg def r r r r r (price)]
      const it = {
        main: toInt(t[1]), sub: toInt(t[2]), turnNum: toInt(t[3]),
        posX: null, posY: null, price: null,
      };
      if (n >= 12) it.price = toInt(t[11]);
      items.push(it);
    } else {
      // [tag posX posY main sub turnNum dmg def r r r r r (price)]
      const it = {
        posX: toInt(t[1]), posY: toInt(t[2]),
        main: toInt(t[3]), sub: toInt(t[4]), turnNum: toInt(t[5]),
        price: null,
      };
      if (n >= 14) it.price = toInt(t[13]);
      items.push(it);
    }
  }

  return { version, saleType, positioned, items };
}

module.exports = { parse, tokenize };
