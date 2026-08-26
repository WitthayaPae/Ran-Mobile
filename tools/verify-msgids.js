// Regression guard: every message id the mobile client hardcodes, checked
// against the measured enum table (layout.json allEnums). If the enums are ever
// re-extracted and a value moves, this fails loudly rather than letting the
// client send to the wrong id.
//
//   node tools/verify-msgids.js
//
// Keep this in sync with the Msg* constants in the Runtime/Ran*.cs files.

const path = require('path');
const L = require(path.join(__dirname, 'layout-probe', 'layout.json'));
const E = L.allEnums || {};

// name in NET_MSG enum  ->  value the C# uses
const EXPECT = {
  // movement
  NET_MSG_GCTRL_GOTO: 3034,
  NET_MSG_GCTRL_GOTO_BRD: 3035,
  // player + entity state
  NET_MSG_GCTRL_UPDATE_STATE: 3046,
  NET_MSG_GCTRL_UPDATE_STATE_BRD: 3053,
  // entity lifecycle
  NET_MSG_GCTRL_DROP_CROW: 3016,
  NET_MSG_GCTRL_DROP_ITEM: 3012,
  NET_MSG_GCTRL_CROW_MOVETO: 3503,
  NET_MSG_GCTRL_DROP_OUT: 3019,
  // combat
  NET_MSG_GCTRL_ATTACK: 3036,
  NET_MSG_GCTRL_ATTACK_DAMAGE_BRD: 3044,
  NET_MSG_GCTRL_REQ_SKILL: 3303,
  // chat
  NET_MSG_CHAT: 2992,
  NET_MSG_CHAT_FB: 2993,
  // inventory / items
  NET_MSG_GCTRL_REQ_INVENDRUG: 3292,
  NET_MSG_GCTRL_REQ_FIELD_TO_INVEN: 3193,
  // shop
  NET_MSG_GCTRL_REQ_BUY_FROM_NPC: 3242,
  NET_MSG_GCTRL_REQ_SALE_TO_NPC: 3243,
};

let pass = 0, fail = 0;
for (const [name, want] of Object.entries(EXPECT)) {
  const got = E[name];
  if (got === want) { pass++; }
  else {
    fail++;
    console.log(`  FAIL ${name}: client uses ${want}, layout says ${got === undefined ? 'MISSING' : got}`);
  }
}

console.log(`\nmessage ids: ${pass} matched, ${fail} mismatched`);
process.exit(fail === 0 ? 0 : 1);
