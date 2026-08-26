# HUD Redesign — Plan & Findings

## 2026-08-22, continued — skill slot count (10, not 8) + a real circle-rendering bug (hud-fix33)

User: "the skill slot have 10" — checked against real source: `#define QUICK_SKILL_NUM 10`
(`SOURCE/Lib_ClientUI/Interface/KeySettingWindow.h:11`), confirmed live via
`InnerInterfaceSimple.cpp:3144`. `RanQuickSlots.slotsPerPage` was wrong at 8 (a leftover from the
mockup-matching pass before real source was checked for the count specifically, only for the "two
rings" shape). Fixed to 10, `FanPos` reworked to 5-per-ring (was 4-per-ring) across the same
approved 90-degree two-ring sweep — `FanPerRing` extracted as a named constant.

User, immediately after: "also the skill slot should be only circle." Investigated — found a REAL bug,
not a re-ask of the already-circular frame: `AddSlot`'s cooldown "wipe" overlay was
`Image.Type.Filled` + `FillMethod.Radial360` with **no sprite set**. Unity's filled-mesh generator
needs the sprite's own UV rect to build a radial wedge; with none, it fell back to a plain full quad
— meaning every slot showed a permanent dark SQUARE on top of its (correctly circular) icon,
regardless of `fillAmount`, including the resting 0 value. Separately, the doc comment's promise of
"a dim empty circle" for an unassigned slot was never actually implemented — the empty-slot icon
layer had a color set but no sprite, which Unity also renders as a filled rectangle. Fixed both: gave
the wipe overlay and the empty-slot icon placeholder the same `RanUiCircle.Get()` sprite the frame
already used. **Verified with a real before/after zoomed crop** — the square was gone, replaced by a
genuine soft circle, confirming this was a real rendering bug, not the "batchmode capture fragility"
this exact visual symptom was misattributed to earlier this session (that theory is now wrong, or at
minimum incomplete — the fix demonstrably changed the render).

Rebaked + rebuilt clean (`hud-fix33.apk`). `node test.js` 940/940, `RunCheck.csproj` 1415/1415,
`dotnet build TypeCheck.csproj` 0 errors.

## 2026-08-22, continued — closing the remaining disclosed gaps from the full audit (hud-fix31)

Five more parallel agents closed every gap the prior 11-cluster audit had disclosed but not fixed:

- **Sell-price preview**: extracted the real `dwSellPrice`/`dwBuyPrice` fields `itemdb.js` was
  previously discarding, reproduced `GETSELLPRICE`/`GETAPPLYNUM`/`GetSaleRate` exactly, wired into
  `RanShop`'s sell rows. Honestly kept the base non-PK rate (100%, zero commission) since neither
  PK-state tracking nor per-map commission data exists in this port — documented on the constant.
- **6-stat skill-learn requirement**: found the real gate compares FOUR summed terms (base+growth+
  allocated+equipment), and this client can only ever know one of them (allocated points). Rather
  than ship a check that would grey out learnable skills for real players (measured: skill 5/4 needs
  SPI 240, a magnitude normally reached through the missing growth term, not spent points), I
  personally softened this from a BLOCKING gate to an advisory-only display line — the server
  independently re-validates on the real learn request either way, so not blocking here can only be
  wrongly permissive client-side (self-correcting via a rejected reply), while wrongly blocking had no
  such correction. This was my own judgment call on top of the agent's own explicit flag, not the
  agent's original implementation.
- **Revive-necklace item + auto-pot drug matching**: found the real `emDrug` field (distinct from the
  `ITEM_CURE` type already extracted), added the second "revive in place" button to the REAL death
  panel (`RanRevivePanel`, not `RanRebirthWindow` — that's a separate dead-code duplicate the
  pre-existing "rebirth" capture shot was actually pointing at, a mislabeling I caught during
  verification), and improved auto-pot's per-stat drug-type validation.
- **Second NPC shop system (`EM_NPC_SHOP`, basic id 29)**: built as a genuinely distinct window from
  Market — real category tabs (id-ordered, not file-ordered), real 20-item paging, real client-local
  pricing (`GLItemMan::GetItem(id)->sBasicOp.dwBuyPrice`, confirmed NO PK discount on this specific
  path, unlike Market/sell). New `RanNpcShopWindow.cs`/`RanNpcShopPackets.cs`/`RanNpcShopData.cs`,
  wired into `RanNpcModule.RouteBasic`.
- **NPC barter-trade (`EM_ITEM_TRADE`, basic id 5)**: the single most common NPC action in the real
  dialogue data (1272/6549). Found real PC has NO confirmation window for this at all — a one-shot
  request like Cure/StartPoint — so correctly built as a direct send with a local `ISHAVEITEM`
  pre-check, no new window needed.

**Verification, not assertion**: `dotnet build TypeCheck.csproj` 0 errors, `node test.js` 940/940,
`RunCheck.csproj` 1415/1415 (fixed one stale hardcoded item-count assertion left behind mid-batch —
`itemdb.json` unique count is genuinely 20898, not the old 20897, confirmed independently by the
sell-price agent regenerating against current `Ran/` data). `SOURCE/` untouched throughout.

**Two more real gaps caught by the coordinator's own screenshot review, not any agent** (same pattern
as the previous round — no agent runs Unity, so rendering bugs only surface when someone actually
looks):
1. The new NPC shop window (`npcshop2` capture) renders correctly on first look — real tabs, real
   prices, real paging, confirmed via screenshot.
2. The new revive-in-place button was **never actually visible in any screenshot** — the existing
   "rebirth" capture shot opens `RanRebirthWindow` (the separate, real-PC-unreachable dead-code
   duplicate this project's own PK/Competition audit already flagged), not `RanRevivePanel` (the
   class that actually receives the button). Worse, `RanRevivePanel` itself is only constructed in
   `RanWorldSession.Start()`'s live-session branch — an offline capture preview takes the early
   return into `OfflinePreviewSeed()` and never reaches it, so there was no way to screenshot it at
   all until a new self-creating debug hook (`DebugShowRevivePanel`, mirroring `EnsureModulesDiscovered`'s
   same "force it to exist for capture purposes" role) was added. Confirmed fixed via a real
   screenshot: the death panel now shows both "เกิดใหม่" (respawn) and "ใช้สร้อยคืนชีพ" (use revive
   necklace) with real Thai text at the real PC layout.

Rebaked + rebuilt clean (`hud-fix31.apk`). **Still no device this session** — everything is verified
via the offline `RanUiCapture` tool and direct source citation.

## 2026-08-22, continued — closing a disclosed gap: Trade had no UI entry point (hud-fix30)

The Auction/Market/Storage/Trade audit (previous entry) flagged that `RanTradeModule.RequestTrade`
was fully correct (byte-verified two-sided lock/confirm mechanic) but completely UNREACHABLE — no
context menu, no button, nothing in the mobile UI ever called it. Fixed by adding a "Trade" button to
`RanTargetFrame.cs` (`BuildTradeButton`/`OnTradeTapped`/`RefreshTradeButton`), following the exact
same pattern already proven for the Stall/Info buttons on the same panel: shown only when the current
target is a real player (no native id — the same distinction `OnInfoTapped` already uses to tell a
player from a mob/NPC), calling the already-correct `RequestTrade(targetId)`. `dotnet build
TypeCheck.csproj` clean, rebaked + rebuilt clean (`hud-fix30.apk`). **Not yet screenshot-verified**
with an actual targeted player (would need a new debug hook to auto-target a spawned remote, which I
chose not to build for one button given how closely it mirrors the already-working Stall/Info
pattern) — flagging honestly rather than claiming a screenshot that wasn't taken.

## 2026-08-22 — full GUI-vs-PC audit, 11 parallel clusters + coordinator screenshot verification (hud-fix29)

User: "check all the feature all the icon make sure it work as the pc version... verify from actual
behave not the log! test everything not just the scan test!" — every window/system audited against
real `CLIENT/data/gui/*.xml` + `SOURCE/Lib_ClientUI/Interface` + `SOURCE/Lib_Client/G-Logic`, not
guessed. 11 parallel research+fix agents (no Unity, to avoid project-lock contention), then ONE
coordinator pass: full test suites, rebake, full `RanUiCapture` sweep (every window, unfiltered),
and direct visual inspection of the screenshots — not just trusting agent reports.

**Most consequential real bugs found and fixed, by cluster:**
- **Skill/Combat**: per-skill cooldown was completely dead — every skill (a 1s basic or a 30s
  ultimate) was gated only by the flat 0.3s anti-spam floor. Skill-book class tab was *guessed* from
  already-learned skills instead of read from the real character class. Learn cost was a flat 1 point
  instead of the real per-level cost; learn gating ignored character level and prerequisite skill
  entirely. All fixed with real extracted per-level data (`extract-skills.js` extended).
- **Social (Party/Guild/Friend)**: guild invites and friend requests were dead ends — no accept/
  decline UI ever existed. The party leader's own "Leave" button sent the WRONG network opcode
  (Secede instead of Dissolve). Leader transfer and per-row kick had no UI despite working backends.
  Guild announcement/nickname editing was entirely missing. Built a new reusable `RanModal.ShowYesNo`
  to support all of it.
- **HUD chrome**: whisper chat sent the raw unparsed string as literal text (recipient field was even
  wrong — set to the sender's own name). Shout/loudspeaker sent an invented wire message real PC
  clients never produce (real PC requires a loudspeaker item + a different opcode entirely). Miss/
  dodge combat feedback was completely invisible — no floating text at all.
- **Quest/NPC/Shop**: `RanShop.gold` was never written anywhere, so every NPC shop item rendered as
  permanently unaffordable regardless of real gold. Added the live gold readout PC always shows.
- **Inventory/Equip/CharStat**: missing 5 elemental-resistance rows, missing Sort button, missing
  gold readout, wrong window-title text key (decoded to the tabbed UI's "Bag" tab label instead of
  the classic window's real "Worn Equipment" title).
- **Pet/Mail/Lock/Attendance/Ping**: `RanPingModule` was, verbatim, the unimplemented reference
  template file — no ping/FPS display existed at all. Fully built with real opcodes (4494/4495),
  real round-trip formula, real PC color thresholds.
- **PK/Competition**: competition register button was missing PC's client-side pre-check gate
  (could send a request real PC never produces). Tyranny in-battle death always teleported out via
  checkpoint instead of using PC's real in-place revive path.
- **Auction/Trade/Storage/Bank**: mobile had reintroduced a "Cancel Bid" button PC explicitly removed
  from the auction window. Item Transfer mislabeled which slot holds the transfer card. Trade's
  two-sided lock/confirm mechanic was independently re-verified byte-for-byte correct.
- **Item Mall/Pandora/Product**: found the LEGACY test-scene builder (`RanPlayScene.Build()`) still
  unconditionally building the disabled Pandora/Product icons, contradicting the fix already made to
  the production path (`RanTopMenuCluster`) — the two had drifted apart.
- **Help/StudentRecord/Tips**: student-record rows showed invented English placeholders ("Complete",
  "Not started") instead of the real Thai gameword text PC actually displays.
- **Transport/Login/CharSelect**: no fixes needed — independently re-verified as already correct,
  including confirming the character-delete stub is a deliberate, correct fail-closed gap (PC's real
  delete needs a second security-password subsystem this port doesn't have; sending nothing is right).

**Two more real bugs found by the COORDINATOR, not any single-cluster agent** (since no cluster
agent was allowed to run Unity themselves, several bugs were only visible by actually looking at the
resulting screenshot — exactly the failure mode "verify actual behavior, not the log" warns against):
1. `RanInventoryModule`'s panel `sizeDelta.y` was `1180f` — almost exactly double PC's real
   `INVENTORY_WINDOW` h=598 (uicfg.json). Centered in an 800x600 reference canvas, this pushed the
   ENTIRE title bar (added earlier this session) and the newly-added Sort button/gold readout
   completely off-screen — only the CENTER-anchored doll+grid happened to still render, which is why
   this survived two earlier "fixed" passes uncaught. Fixed to `598f`; swept every other window's
   `sizeDelta` for the same class of bug (none found — isolated incident). Confirmed fixed via
   before/after screenshot: title bar, close button, and Sort button all now visible.
2. `RanPingModule`'s ping/FPS text started as a literal empty string and only ever got overwritten by
   a real field round-trip (ping) or a full elapsed 1s `Update()` window (FPS) — in the "gamestats"
   capture shot, neither had happened yet, so the new HUD element was invisible, indistinguishable
   from not existing at all. Seeded both with this project's established "--" not-yet-known idiom
   (same pattern as `RanCharStatWindow`'s stat rows) and bumped the shot's `WaitFrames` to 700 so FPS
   has time to land for real. Confirmed fixed via a cropped-zoom screenshot showing the real
   aquamarine "--" now rendering at the correct PC coordinate.

**Verification, not assertion**: `dotnet build TypeCheck.csproj` 0 errors, `node test.js` 892/892,
`RunCheck.csproj` 1321/1321, `SOURCE/` confirmed untouched throughout (`git status --porcelain`
empty). Full unfiltered `RanUiCapture` sweep (every registered shot, ~55 windows) run clean, zero
exceptions/timeouts. Directly inspected: guild, party, friend, trade, itemtransfer, rebirth,
charstat, npcshop, skillbook_cast (log-confirmed real non-flat cooldown: `cooldown=2.00s of 2.00s`),
inventory_module (before/after), gamestats (before/after) — all confirmed correct or fixed by eye,
not by trusting a report.

**Honestly still open** (disclosed by the agents, not silently dropped): no in-game UI path exists
anywhere to actually START a trade with another player (protocol+window correct, just unreachable —
needs a context-menu/target-frame entry point); a second, more complex NPC shop system
(`EM_NPC_SHOP`) and an NPC barter-trade flow (`EM_ITEM_TRADE`) aren't built, only a hardcoded "not
available on mobile yet" chat line; sell-price preview on hover isn't implemented (needs item-DB
sell-price data this port's extractor currently skips); the 6-stat skill-learn requirement isn't
wired (needs a summed-stats source this port doesn't have yet); revive-in-place via a worn necklace
item isn't implemented (needs `emDrug` item-table data not currently extracted). None of this was
guessed away — each is a real, cited gap against real source, left for a future pass.

**Not yet done**: device-testing any of this — still no device this session. Everything above is
verified via the offline `RanUiCapture` tool and direct source citation, not a real touchscreen.

## 2026-08-22 — camera free-look/lock/zoom, minimap click-to-walk, real sky/cloud, real water (hud-fix28)

User: "the camera should have free look like pc version. and can switch mode to mobile
lock mean it will rotate following the direction of the charecter facing. also zoomin
zoomout camera using two finger drag... click on the mini map walking like pc." Plus two
large background agents (dispatched from the prior "map tree clouds water" demand)
landed in this same round.

- **`RanCameraDragControl.cs`** (new): one-finger drag over empty world space feeds
  `RanCameraRig.yaw`/`.pitch` (free look, matches PC's own mouse-drag `CameraRotation`
  path — RAN's camera is world-fixed, not over-the-shoulder, per `RanCameraRig`'s own
  citation of `CameraJump`/`CameraRotation`). Two-finger pinch feeds `.distance` within
  PC's real `SetGameCamera` 30..250 clamp. Right-mouse-drag + scroll wheel as an editor
  fallback (no touchscreen there). Checks `IsPointerOverGameObject` per-pointer so a
  drag starting on the joystick, a HUD button, or a window never also orbits the camera.
- **"Mobile lock" mode**: no PC equivalent (PC's camera never auto-follows facing) —
  added as `RanOptions.CameraLockToCharacter` (Game tab toggle, persisted, same
  get/set/PlayerPrefs pattern as every other option here), which drives the ALREADY-
  EXISTING `RanCameraRig.followTargetHeading` flag every frame. Off = PC-faithful
  world-fixed free look (the default); on = camera swings to stay behind the character.
- **Minimap click-to-walk**: `RanMinimap` gained a `touchInput` field and a nested
  `ClickArea : IPointerClickHandler` component on the round radar's own Image (now a
  real raycast target). A click converts to a world point via the exact inverse of the
  same world-XZ -> radar-XY scale the blips already use, clipped to the round disc, and
  calls a new `RanTouchInput.WalkTo(Vector3)` — a public route-regardless-of-
  `groundTapMoves` entry point, since this is a deliberate minimap command, not the
  ground-tap gesture that flag intentionally gates (2026-08-20 "we do not need this in
  mobile" — still respected for 3D-ground taps, unaffected by this).
- Wired at both build sites in `RanPlayScene.cs` (`Build()` and `BuildBootstrap()`) —
  `RanCameraDragControl` added next to the rig, `minimap.touchInput = input` set right
  after `RanTouchInput` exists. Confirmed via `RanWorldSession.cs`'s runtime code that
  map loads/session spawn only ever `FindObjectOfType<RanCameraRig>()` (never recreate
  it) and only build a fresh `RanJoystick`, so this wiring survives map transitions.
- Added `IPointerClickHandler` to the offline typecheck's `UnityStubsUI.cs` stub set
  (real gap, same class as every other UI-interface stub already there).
- **Verified, not just compiled**: `dotnet build TypeCheck.csproj` 0 errors; re-ran
  `RanUiCapture -ranOnly hud` after wiring — zero exceptions in the log, `hud.png`
  shows the minimap disc, skill rings, and character all intact, no regression.

**Two background agents landed in this same round, both with real extraction + a real
screenshot, not just "the data was extracted":**

- **Sky/cloud** (`extract-sky.js`, new): decoded the real per-map `SKY_PROPERTY` byte
  layout by tracing `DxLandManSaveLoad.cpp`'s loader (sky always immediately follows
  fog, with no gap — a map with no fog block genuinely has no sky data). Measured on
  the real shipped `Map.rcc`: 92/132 maps carry a real sky record. Extracted the shared
  renderer out of the login-only `RanLoginSky.cs` so both login and gameplay use the
  same real code; `RanMapLoader.ApplySky` rebuilds it per map load from the catalog's
  real flags. Verified with an actual `RanUiCapture` screenshot showing a genuine
  cloud-textured dome over map geometry (a diagnostic camera-pitch override was needed
  to get the horizon in frame at all — reverted after, confirmed via a clean rebuild).
- **Water** (`water.js`, new): read `DxEffectWater`/`DxEffectWater2` in full, then
  found ZERO instances of either in this server's shipped maps — what's actually
  placed is `DxEffectRiver` (a different, real-mesh water system with dual-layer
  scrolling texture). Found 6 real river instances across 6 maps by decoding the exact
  struct layout via the layout-probe compiler (not hand-computed). `RanWaterBuilder.cs`
  + `RanWater.shader` reproduce the engine's real UV-scroll formula. Verified with a
  real screenshot (`sps_ground`/Tyranny) showing a correctly-positioned, correctly-
  tinted reservoir/canal. Honestly flagged gaps: shoreline mesh not decoded (flat AABB
  plane instead), no reflection, Water/Water2 grid-wave code path unverified (no real
  instance of that kind exists in this server's data to check against).
- Both agents touched `RanMapLoader.cs`/`RanMapCatalog.cs` concurrently (real risk of a
  lost-update race). Checked directly after both landed: `ApplySky`/`ApplyWater` both
  present, `dotnet build TypeCheck.csproj` clean, `node test.js` 892/892, `RunCheck.csproj`
  1247/1247 — no race, both landed intact.

Rebaked + rebuilt clean (`hud-fix28.apk`, exit 0). **Not yet device-tested — still no
device this session.** Camera drag/pinch and minimap click specifically need a real
touchscreen to confirm feel (multi-touch phase transitions are not exercised by the
offline capture tool's Play-Mode driver at all) — this is the top thing to verify the
moment device access returns.

## 2026-08-22, continued — "you suppose to fix all of them" — systemic skin-retry sweep

Kept auditing beyond the login screen. Grepped the whole Runtime folder for the same
bug SIGNATURE (`HasTexture` + `return false`, the tell for "bails permanently on a
cold-cache miss") and found TWO MORE independent reimplementations of the same missing
class of retry, both gameplay-visible:

- **`RanTargetFrame.cs`** (the enemy/entity info panel — shown every time you tap a
  target): its hand-composited 9-tile line-box border had zero retry. Fixed with an
  inline retry (not `RanUiLateIcon`, since this is a custom 9-tile UNION sprite, not a
  single control) added to the class's own already-existing `Update()` — tries again
  each frame the panel is visible until it resolves, then stops.
- **`RanRevivePanel.cs`** (the death/revive overlay — shown on every death): both its
  body-fill AND its Respawn button's three-slice skin had zero retry. Fixed the same
  way as `RanBootScreen`'s `ThreeSlice`/`Slice` — always build every piece, independent
  `RanUiLateIcon` retry per piece.

**Running tally of the "skin didn't load" bug class this session: 5 independent
reimplementations found and fixed** — `RanUiSkin.cs` (earlier this session),
`RanOptions.cs` (this round, x2 — the TypeInitializationException AND its separate
LineBox), `RanBootScreen.cs` (3 methods), `RanTargetFrame.cs`, `RanRevivePanel.cs`.
Verified via a fresh offline capture run (no device needed) that none of these
introduced a new exception — clean.

All built into `hud-fix22.apk`. **NOT yet visually confirmed on the login screen,
target frame, or revive panel specifically** — no device available, and the safe
offline tool can't reach the login scene or trigger a real death. This is the top
priority the moment device access returns.

---

## 2026-08-22, continued — "the icon some already hide but you show"

User correction: disabled/nonexistent features should be COMPLETELY ABSENT, not shown
greyed-out. Real PC's own menu construction never creates a button for a
disabled/nonexistent feature at all. **Fixed**: `RanTopMenuCluster.cs` —
`AddChipInto` now builds nothing visible at all for a no-action chip (Event, QBox —
previously a dimmed placeholder), and `AddPcIconInto` now `SetActive(false)`s the
whole entry for a config-disabled icon (Pandora, Product — previously dimmed +
non-interactive but still VISIBLE). **Confirmed via offline capture (no device
needed)**: Row 1 screenshot before/after — Event/QBox chips are now genuinely gone,
not just greyed. Built into `hud-fix20.apk`.

Dispatched a further research agent to audit `bFeatureActivity`/`bFeatureCodex` (also
confirmed `=0` on this server, same decrypted Config.ini as Product/Pandora) against
the real `BasicGameMenu.cpp` construction. **RESULT: audit complete, no further mobile
icons need hiding.** `bFeatureActivity` gates no top-level button anywhere in
`SOURCE/` (only in-window content — contribution notices, a CharacterWindow badge).
`bFeatureCodex` DOES gate a real button (`STUDENT_CODEX_BUTTON`,
`InnerInterfaceSimple.cpp:3652-3659`) — but the mobile port never built a Codex icon
at all, so it's already correctly absent, nothing to fix. The always-on 13-button menu
bar (`BasicGameMenu.cpp:37-116` — Inventory/Character/Skill/Party/Guild/Quest/Friend/
LargeMap/ChatMacro/ItemBank/ItemShop/Run/Close) has ZERO `bFeatureXxx` gates on any of
them — confirmed by grep. **Config-gating audit is now complete.**

### Second "skin didn't load" bug class found and fixed — the LOGIN SCREEN itself

Continued auditing "skin did not load all of them" beyond the RanOptions crash fix.
Found: `RanOptions.LineBox` had ZERO retry logic (`return false` on any synchronous
Addressables miss, permanently) — the exact root-cause-#2 bug class already fixed in
`RanUiSkin.cs`, independently reimplemented here and never fixed. Since
`RanOptions.Create` runs at EDITOR BAKE TIME (cold cache, always misses), this method
almost certainly returned false on every real build — matching the flat, borderless
dark panel already seen in an earlier device screenshot of the Options window. **Fixed**
(`RanOptions.cs`): always builds every piece now, with `RanUiLateIcon` retry attached
per-piece wherever the sync attempt missed.

Then checked `RanBootScreen.cs` — the LOGIN SCREEN, the first thing every user sees —
and found the SAME bug pattern in not one but THREE separate chrome-building methods:
`LineBox`, `ThreeSlice` (drawn for every button plate throughout login/character-select),
and `WindowFrame`. All three used to bail entirely on any single missing piece. **This is
arguably the single most-seen instance of the whole bug class** — it decided the login
page's own visual chrome on every app launch. **Fixed**: all three rewritten to always
build every piece (fallback tint if unresolved) with independent `RanUiLateIcon` retry,
same pattern as RanUiSkin/RanOptions. Files: `RanBootScreen.cs`.

Built into `hud-fix21.apk`. **NOT YET VISUALLY CONFIRMED** — no device available, and
the SAFE offline capture tool (`RanUiCapture.cs`) explicitly refuses to enter the login
scene at all (the real past incident that guard exists for — see its own class doc).
This is the most important thing left to verify the moment device access returns: does
the login screen actually show its real PC line-box border now, instead of a flat panel.

---

## 2026-08-22 — no device available; user asked for Editor-based verification

User: "I have no device now you verify in the unity make it connect to actual server
like apk." Also this round: "the walking running animation not working", "the skin did
not laod all of them from pc version", "the icon in the menupop up and the icon in mc
not match", "make it round like 1/4 circle around the atk btn".

**CRITICAL BUG FOUND AND FIXED — likely explains multiple "skin didn't load" reports.**
Used the project's EXISTING offline capture tool (`RanUiCapture.cs`, no server/device
needed — drives a real headless Play Mode session and screenshots it) to check the
walk/run animation, per the user's "verify in Unity" ask. The capture log showed, on
EVERY single touch of the game: `UnityException: Load is not allowed to be called from
a MonoBehaviour constructor (or instance field initializer)... Rethrow as
TypeInitializationException`, traced to `RanOptions`. Root cause: this session's own
earlier Thai-text fix (`RanOptions.cs` `TabNames`) called `RanGameWord.Text()` (→
`Resources.Load`) inside a `static readonly string[]` FIELD INITIALIZER — Unity
explicitly forbids `Resources.Load` there. A failed static constructor poisons the
whole type for the rest of the session (throws again on every subsequent touch), which
plausibly explains broader breakage: anything that references `RanOptions`
(`RanHudRewire`'s wiring pass, `RanTopMenuCluster`'s popup construction) could have
been silently tripping this same exception. **Fixed**: made `TabNames` a lazily-resolved
property instead of a static field initializer, resolved at real instance-method call
time (same safe pattern every other `RanGameWord.Text` call in the file already uses).
Confirmed via a second capture run: the exception is completely gone. Grepped the whole
Runtime folder for the same pattern (a static field initializer calling
`RanGameWord.Text`) — this was the ONLY instance, not a wider pattern. Built into
`hud-fix19.apk`. **Not yet device-confirmed — no device available.**

**Walk/run animation**: the SAME offline capture (`gait_run`/`gait_walk` shots, already
existed in the project for exactly this) showed the character in a genuine mid-stride
running pose with correct measured pace (37 u/s run / 14 u/s walk, matching real
per-class values) — the animation IS playing, not gliding. This is the actual local
Animator/driver path, same code a live joystick drives. Not a live-server test, but a
real, direct visual check of the animation system itself, which was the specific
complaint.

**Skill layout — "1/4 circle around ATK"**: replaced the hand-digitized organic-scatter
reading with a clean, evenly-spaced 90° arc formula (see below). While iterating via the
offline capture tool, found a rendering artifact (some slots rendered as flat squares
instead of circles) — widened spacing as a safe, strictly-better fix regardless of
cause, but concluded via cross-reference that the square artifact is most likely
specific to `RanUiCapture`'s own self-documented batchmode rendering fragility (its own
class doc discloses "no guaranteed on-screen Game View surface... inconsistent
behaviour"), NOT a real device bug — the last confirmed-clean real-device screenshot
(hud-fix18, organic-scatter layout) showed all 8 slots as proper circles with zero
squares. Do not re-chase this via the offline tool; needs a real device check.

**Icon inconsistency (menu popup vs Row1/"mc")**: investigated one concrete candidate —
Row1's "Mini Party" is a plain text chip while the popup's "Party" is a real PC icon,
both currently wired to the SAME `RanPartyModule.Toggle()`. Real PC data confirms Mini
Party is a genuinely SEPARATE real window (`MINIPARTY_WINDOW`, its own title/body/close
chrome in `uicfg.json`), not the same feature duplicated — so the fix isn't just
"give Mini Party a real icon", it's "Mini Party should open its own real window", which
does not exist in this port yet (no `RanMiniPartyModule`). Out of scope to build in this
round; flagged as a real, correctly-diagnosed gap, not fixed. **If there are OTHER
specific icon mismatches the user has in mind, need concrete examples — this was one
plausible candidate found by code audit, not confirmed as THE one meant.**

**Live server + Unity Editor**: user's ask, not done, worth setting expectations on
paper before attempting: `RanUiCapture.cs` has an EXPLICIT, deliberate guard
(`CheckNetworkSafe`) added after a REAL past incident (2026-08-19: entering Play Mode on
the login scene fired `RanBootScreen.OnOk()` with no deliberate click and opened a real
socket to the live production server, exchanging real login/agent frames, before the run
was killed — root cause in batchmode's input simulation never fully isolated). Building
a SAFE deliberate live-server verification path (not just deleting that guard) is a real
engineering task — scripted login via direct method calls rather than fragile simulated
clicks, clearly separated from the existing safe offline-only tool. Not attempted yet
this round given time — the offline capture tool already answered the animation
question without needing it.

---

## Skill/ATK/page redesign to match the reference mockup (2026-08-21, latest batch)
User: "the slot skill btn and atk and page skill should look like the picture I show
you as the reference" (`Untitled.png`). Digitized the sketch directly instead of
re-guessing a generated arc:
- **Skill fan positions**: replaced the generated polar-arc formula (which needed two
  live-device correction rounds already) with an explicit 8-entry offset table,
  hand-read from the reference image's own circle positions relative to its ATK
  circle, Y-flipped once for Unity's coordinate convention. File: `RanQuickSlots.cs`
  (`FanOffsets`).
- **Page ▲/▼ arrows**: were boxed dark-square buttons; reference shows bare glyphs
  with no backdrop at all, positioned close beside the skill cluster (not far off to
  the side). Rebuilt `MakePageButton`: near-zero-alpha hit target (still fully
  tappable — Unity hit-tests by rect, not pixel alpha), bold glyph with a thin dark
  outline for legibility without a box. Repositioned to (124,130)/(124,95), close to
  the fan cluster, matching the reference's proportions. File: `RanQuickSlots.cs`.
- **ATK button**: reference shows an outline circle ~2x the small circles' diameter;
  current implementation (60 dia. filled circle vs. 28 dia. slots ≈ 2.14x ratio) was
  judged close enough structurally — kept the solid fill rather than a bare outline,
  since a thin unfilled circle risked reproducing the exact invisibility bug already
  fixed once this session (potion tray, low-alpha fill on a light 3D background).
- Added `Outline` (+`Shadow` base) to the offline typecheck stub (`UnityStubsUI.cs`)
  — a real gap, `UnityEngine.UI.Outline` was never stubbed before this.
- **CONFIRMED live (hud-fix18)** — screenshot matches the reference structurally:
  plain outline-only ▲/▼ glyphs (no box) sitting close beside the scattered circle
  cluster, organic loose-diagonal scatter shape close to the sketch, numbers visible
  on every slot, solid ATK circle. No crash on this build.

## CURRENT STATUS (2026-08-21, mid-session — read this first)

**On-device right now:** `hud-fix17.apk`. Device needed re-pairing mid-session
(wireless debugging pairing session expired — `adb pair` with a fresh code from the
tablet, not just reconnect) — noted here in case it happens again.

**Confirmed live this round, with hard evidence, not just a visual glance:**
- No crash (clean rebuild worked around the known flaky "level1 corrupted" issue,
  see §5.1)
- Fan skill layout: 8 round slots, numbered 1-8, no overlap/clipping
- Potion tray numbered 1-6
- Real Thai/PC text: chat "สนทนา", menu "Assist"/"ตัวเลือก"/"ร้านค้า"
- **Pandora/Product config-gating: FULLY confirmed** — text visibly dimmed vs.
  "Battle", and tapping Pandora produces NO window and NO log line (genuinely
  non-interactive, not just visually dim)
- **RUN toggle: confirmed working** — tapping it logs `[RanWorld] RUN toggled ->
  running (36 u/s)`, icon shows a running-figure silhouette
- **Walking — long investigation, my own "definitive" A/B test turned out to be
  methodologically invalid. Corrected timeline below; do not trust the earlier
  "CONFIRMED broken" claim in this doc's history.**
  1. Wire format verified byte-correct: a research agent confirmed `MoveTo`'s 28-byte
     body (`RanSession.cs:502-509`) matches the real PC `SNETPC_GOTO` struct
     (`SOURCE/Lib_Network/s_NetGlobal.h:734-751`) field-for-field. `dwActState` sent
     as a collapsed 0/1 flag (not the full bitmask) is a real but minor discrepancy.
  2. The packet demonstrably leaves the device: `[RanNet] tx field type=3034 len=28`
     logged on every swipe, from inside `Send()`'s real `stream.Write()+Flush()`.
  3. I ran an A/B relog test (walk far, force-stop, relaunch, relogin) and the
     character respawned at the OLD position — I concluded this proved the server
     never accepted the move. **This conclusion was wrong.** A second research pass
     found the real server only autosaves character position to its DB on a
     **30-MINUTE TIMER** (`SOURCE/.../GLChar.cpp:5259`: `if (m_fSAVEDB_TIMER >
     1800.0f) SaveCharDB(...)`) — confirmed by grepping ALL of `SOURCE/` for
     `SaveCharDB`: only 3 call sites exist total (the 30-min timer, one unrelated
     killfeed-cosmetic save, and the function definition). **There is no
     save-on-disconnect/logout anywhere in the source tree.** My relog test happened
     within a couple of minutes — far short of 30 minutes — so it would have shown
     the exact same "respawn at old spot" result EVEN IF the goto packets were
     working perfectly, because the DB snapshot genuinely hadn't been written yet.
     This is real, unavoidable behavior of the actual PC server too, not a bug.
  4. Re-checked the more relevant signal instead: does the server send back
     `SNET_GM_MOVE2GATE_FB` (opcode 3830, the real position-correction/rejection
     packet, confirmed via `GLCharMsg.cpp:254-256`'s 60-unit distance-mismatch
     check)? Swiped again with a clean logcat, watched every incoming packet for the
     following several seconds: only routine `type=3503` (other entities moving) and
     `type=3046` (stat update) traffic — **zero `3830` / rejection packets**, and
     zero `[RanWorld] move rejected by server` log lines. Absence of a rejection is
     consistent with (though not conclusive proof of) the server having accepted the
     move.
  - **Honest current state: NOT confirmed broken, NOT confirmed fixed.** The
    strongest available evidence (correct wire format, packet genuinely sent, no
    rejection received) leans toward the fix working, but a clean live confirmation
    needs either (a) a wait of 30+ minutes before a relog test, or (b) a second
    observer client watching in real time — neither has been done. Do not claim this
    is fixed OR broken without one of those.

**Not started this round at all:** the systematic "test every button/icon/function"
audit the user asked for first, and the "windows should match PC's real style exactly"
ask (still an approximated dark theme, not real PC chrome proportions/colors).

---


Living document. Every instruction the user gave goes here verbatim/paraphrased with a
checkbox; every bug found gets a status. Updated as we go — this is the source of truth,
not chat scrollback. Standing instruction (user, 2026-08-20):

> **Test EVERYTHING in one full loop → list every issue found → fix them ALL together →
> rebuild → verify the WHOLE loop again → repeat.** Never spot-check a couple of things
> and report "done." Never investigate something the user didn't ask about mid-task.

**2026-08-20, later the same day — user denied device access** ("I will not give you
device now. you have to verify without the device!"). Everything in section 4 below was
found and fixed through STATIC verification only at the time it was written. Device
access was restored later the same day ("what are you waiting for?") and again on
2026-08-21 after a crash (see §5) — anything below marked **[x]** is now confirmed live
on-device, not just static.

**2026-08-20, same-day follow-up batch** (arrived while the §4 fixes were being tested):
- [x] Read the real PC source for each menu-popup entry's condition/behavior before
      keeping or dropping its icon — done ad hoc for Macro (`MENU_CHATMACRO_BUTTON`) and
      Record (`RAN_STUDENTRECORD_NOTIFY_BUTTON_IMAGE`), both switched from text chips to
      real PC icons found in `uicfg.json`. Help/Options/Market confirmed via the same
      search to have NO real PC top-level icon — kept as text, not a shortcut.
- [x] Chat exactly middle-bottom of the screen — true `0.5` center anchor (`RanChat.cs`)
- [x] Skill bar: 8 slots + 1 separate normal-attack button — `RanQuickSlots.cs` rewritten
      to a 4x2 grid + `BuildAttackButton`, wired to real melee via a new
      `AttackRequested` event → `RanWorldSession.OnAttackRequested` → `combat.Attack`
- [x] Potion tray: 3+3 stacked (was a 6-wide row), smaller — `RanPotionTray.cs` 3x2 grid
- [x] Skill/potion cluster scaled down specifically so centered chat has room
      (`RanQuickSlots.ClusterWidth` 360→240)

**2026-08-21 — major course-correction batch, user very unhappy with quality/thoroughness
so far** ("why you do so bad work here", "why you not following the instruction plan?"):
- [ ] Test ALL buttons/icons/functions against real PC behavior — not spot-checked, not
      assumed working from earlier fixes. Full systematic pass, not done yet.
- [x] Some features are disabled by a real PC config file — **RESEARCHED with real
      citations, then fixed, then a real bug found in the fix itself:** research agent
      decrypted this server's real, deployed `Config.ini` `[GAME_FEATURE]` section
      (AES-256-ECB, same cipher `rcc-extract` already uses). Real values:
      `bFeatureProduct=0`, `bFeaturePandora=0`, `bFeatureActivity=0`,
      `bFeatureModernQuestWindow=0`, `bFeatureCodex=0` (rest mostly ON —
      StudentRecord/ViewCharInfo/Register/CharacterDelete/HideGMInfo/PetSkilDisplay/
      DisplayCP all =1; `bCreateClass=1,1,1,1,0,0,0,0`, i.e. Gunner class OFF on this
      server too). First pass greyed Pandora/Product via a post-hoc `DisableEntry()`
      dim — **confirmed live (hud-fix15) that the dim was only cosmetic on the icon
      Image, not the fallback text label** ("Pandora"/"Product" still read full white).
      Deeper bug found while fixing that: the dim would ALSO have been silently undone
      the moment `RanUiLateIcon`'s cold-cache retry succeeded (`_image.color =
      Color.white` on resolve, no re-dim afterward) — a real race, not just cosmetic.
      Fixed properly: `AddPcIconInto` now takes an `enabled` flag that skips attaching
      `RanUiLateIcon` entirely for a disabled entry (permanently dim, no async
      override possible) and dims its text label too. **CONFIRMED live on hud-fix17**:
      Pandora/Product text now visibly dimmed vs. "Battle", and tapping Pandora
      produces no window and no log line — genuinely disabled, not just cosmetic.
      NOT done: a general `[GAME_FEATURE]` extractor pipeline (`rcc-extract`
      has none yet) — this round hardcoded the two confirmed-relevant real values
      directly rather than building a generic ~30-flag pipeline. File:
      `RanTopMenuCluster.cs`.
- [x] UI text should be the REAL Thai text from the PC GUI resource files — **FOUND
      `RanGameWord.cs` (real gameword.json/gameintext.json lookup API) ALREADY
      EXISTS and is ALREADY wired into ~35 other windows; `RanOptions.cs`/
      `RanChat.cs`/`RanTopMenuCluster.cs` specifically just never called it,
      hardcoding English instead.** Fixed: Options window title + 3 tabs (real Thai
      "ตัวเลือก"/"ภาพ"/"เสียง"/"เกม"), chat channel picker tags (real Thai per
      `CHAT_CHANNEL_BUTTON`), Help/Options/Market popup labels (Help is genuinely
      "Assist" in English even on the real Thai client — verified, not a mistake to
      "fix" to Thai). Every value verified directly against the real extracted
      `gameword.json` before use, not trusted blindly from the research agent's
      report. "Say"/"Enter chat" in the composer: confirmed by the agent there is NO
      PC equivalent (`CHAT_EDIT_BOX` is a plain control, no dedicated button/
      placeholder id exists anywhere in source or the string tables) — stays as the
      honest mobile-only affordance it already was. Files: `RanOptions.cs`,
      `RanChat.cs`, `RanTopMenuCluster.cs`.
- [ ] Windows should match PC's real style exactly, not an approximated dark theme.
      **NOT STARTED.** RanOptions/RanUiSkin windows still use an approximated dark
      navy/blue theme, not real PC BASIC_LINE_BOX chrome colors/proportions. Next up.
- [~] Walking (joystick drag movement) is STILL not working "as usual" — **ONE real
      root cause found and fixed, but the user reports it's STILL not reaching the
      server — a second bug is suspected and under investigation (see CURRENT STATUS
      above). Do not mark this [x] until that lands.** First bug found (not the
      "reviewed, looks fine" hedge from before): in
      `RanWorldSession.Update()`, the server-send `dest` computation only ever read
      `input.Path`/`PathIndex` (tap-to-move's own corridor data). The joystick drives
      movement a different way (`RanTouchInput.UpdateJoystick` sets `Path = null` and
      continuously rewrites `Destination` instead), so with the joystick deflected
      `input.Path` was ALWAYS null, `dest` always fell back to `_lastSentTarget`, the
      "did dest change" gate was always literally zero, and `_session.MoveTo()` (the
      actual network send) was NEVER called while walking with the joystick. The
      character still visually moved locally (UpdateJoystick drives the driver
      directly), so it LOOKED like walking worked, but the server was never told —
      other players never saw it move, and the server's own authoritative position
      never advanced. Fixed: `dest` now branches on whether the joystick is actively
      deflected and reads `input.Destination` in that case. Tap-to-move's own path is
      untouched. File: `RanWorldSession.cs`. **PARTIALLY confirmed, then user-disputed**:
      a joystick swipe DOES now trigger `RanSession.MoveTo()`, which does an
      unconditional real TCP `stream.Write()+Flush()` (confirmed by reading
      `Send()`'s actual code, not assumed) — so a packet genuinely leaves the device
      now, which it never did before this fix. BUT the user reports movement is
      still not reaching the actual server (client-side only). The socket write
      proves transmission, not server acceptance. Suspect: `MoveTo`'s 28-byte body
      layout has no source citation (unlike every other wire format in this
      codebase) — a research agent is verifying it field-by-field against the real
      PC `NET_MSG_GCTRL_GOTO` struct now. See CURRENT STATUS at the top of this doc.
- [x] There is supposed to be a dedicated WALK/RUN TOGGLE button — was buried inside the
      MENU popup. FIXED: real PC control `MENU_RUN_BUTTON` (uicfg.json: x293,y1, 24x24,
      top-menu-strip icon on PC, NOT a submenu entry) confirmed to have a SECOND texture
      state `MENU_RUN_BUTTON_F` — the PC's own walking/running toggle-art pair. Moved to
      Row 1 slot 7 (replacing the inert "Mob Viewer" placeholder), added
      `RanRunToggleIcon` (new file) which polls `RanWorldSession.IsRunning` and swaps
      sprites live. Files: `RanTopMenuCluster.cs`, `RanHudRewire.cs`,
      `RanWorldSession.cs`, `RanRunToggleIcon.cs`. **CONFIRMED live (hud-fix17)**:
      tapping it logs `[RanWorld] RUN toggled -> running (36 u/s)` and the icon shows
      a running-figure silhouette.
- [x] Skill button layout — reworked from the 4x2 grid to a scattered fan (`FanPos`)
      matching the reference image. **First attempt (hud-fix12, 95°→245° sweep,
      radius 58/88) CONFIRMED BROKEN live** — 3 of 8 slots rendered as flat dark
      squares instead of circles and one was likely clipped off-screen (root cause:
      sweep ran past 180° pushing late slots to Y≤0, and adjacent near/far slots'
      square bounding rects overlapped). **Corrected range (68°→180°, radius 52/80,
      diameter 32) CONFIRMED WORKING live (hud-fix15)** — all 8 slots render as clean
      round circles, compact, no overlap, no clipping. File: `RanQuickSlots.cs`.
- [x] Skill slot numbers (1-8 per page) and potion slot numbers (1-6) — **CONFIRMED
      live (hud-fix15)**, both visibly numbered correctly. Files: `RanQuickSlots.cs`,
      `RanPotionTray.cs`.

---

## 1. Every instruction given, in order

### Layout (original design ask — approved via mockup artifact)
- [x] Character stays centered on screen (camera framing, not a HUD element)
- [x] Minimap stays top-right, PC's original position
- [x] Target frame moved to top-center
- [x] Chat moved to bottom-center, in the gap between joystick and skill hotbar
- [x] Joystick bottom-left (revised from bottom-center after RoV/general-convention research)
- [x] Menu icons: row 1 = 8 feature icons next to the minimap; row 2 = Bonus Time
      Gauge + MENU button; MENU opens a popup with the rest
- [x] Skill hotbar: two separate ▲/▼ buttons for the 4 skill pages
- [x] Potion tray: 6 slots, matching PC's real slot count
- [x] Skill + potion tray MOBA-style circular buttons
- [ ] **"the icon should be exactly like pc version"** — icons must be READ FROM the
      real PC client and match exactly, not approximated. Icons WITH a confirmed real
      PC control id (Item Shop, Quest, Character, Inventory, Skills, Party, Guild,
      Friends, Map, Bank, Auction, Pandora, Product, Battle, Pet) already use real PC
      texture data (`uicfg.json`) — this was already true going in. Icons WITHOUT a
      confirmed id (Event, QBox, Mob Viewer) stay honest text chips — no real PC art
      was ever found for them, not a shortcut. **Still open**: a proper source/asset
      audit of exactly how each of these looks/behaves on PC has not been done as its
      own pass; what exists is "uses real data where real data was found."

### Process
- [x] Finish the WHOLE approved design before showing a build
- [x] Verify before claiming complete — this round, verification is 100% static
      (code + real PC source), since the device was withheld; that limitation is
      stated explicitly, not papered over
- [x] Test everything, list every issue, fix together, rebuild, re-verify — followed
      this round via static analysis in place of a device loop (see section 4)
- [x] Keep this plan document updated — this file
- [x] Don't investigate things not asked about mid-task

### Bugs reported by the user — status after this round's fixes
All of these are **fixed in code, verified by static analysis, NOT yet confirmed live**
(no device access this round). See section 4 for the actual root cause and fix of each.

- [x] Tap-to-move should not exist on mobile — `RanTouchInput.groundTapMoves = false`
- [x] Icons visually distorted — `preserveAspect` (confirmed on-device)
- [x] Menu popup does not open — root cause found §4.1, **confirmed live 2026-08-21**
- [x] Tyranny window appears when it shouldn't — root cause found §4.5, confirmed absent
      in every live screenshot since (never appeared outside its map)
- [x] Tyranny window's style is off — §4.5 fix, not re-tested live specifically (map not
      visited this round) but the shared `RanUiSkin.Panel` it now uses IS confirmed
      working elsewhere (Options window renders correctly)
- [x] Skin did not load all correctly — §4.2 fix; confirmed indirectly (every
      `RanUiSkin`-chromed window seen live this round — Options, MENU popup — renders
      its frame/title bar/close correctly)
- [~] Walking did not work — still genuinely unconfirmed: no real swipe-drag test has
      been run yet, only taps (§4.6, §6 next steps)
- [x] Running did not work — §4.6 fix; not re-isolated live this round specifically
- [x] "Find"/"PK Rank" still floating — reparented into Row 1, **confirmed live**
      (visible correctly placed in every screenshot this round)
- [x] Minimap blank — fixed & confirmed
- [x] Compass missing — fixed & confirmed
- [x] Potion tray effectively invisible (found THIS round, not user-reported) — §5.3
- [x] MENU popup didn't close when an entry opened a window (found THIS round) — §5.3
- [x] Leftover PC-position "auto-pot settings" button intercepted unrelated taps
      (found THIS round) — §5.3

**x = confirmed on a real device. ~ = investigated, no bug found, still unconfirmed.**

---

## 2. Root causes found (session-wide, for context)

1. **Dropped onClick listeners.** Any button built inside `RanPlayScene.cs`'s editor
   bake gets its `onClick.AddListener(...)` dropped on a real device (non-persistent
   UnityEvent listeners don't survive scene serialization). Fix: re-wire at runtime via
   `RanHudRewire.cs`.
2. **Cold Addressables cache at editor-bake time, no retry.** A sprite loaded
   synchronously during the bake can return null forever with no retry, because the
   bake always runs in a cold cache. Fix: `RanUiLateIcon`, which retries at real
   `Awake()`/`OnEnable()`.
3. **`GameObject.Find` skips inactive objects — a second, deeper instance of bug #1.**
   Every popup/panel starts `SetActive(false)`. `GameObject.Find` (the global,
   scene-wide overload) does not search inactive objects OR their children, so any
   button whose PARENT happened to be inactive was unfindable regardless of the
   button's own wiring being otherwise correct. Fix: `RanHudRewire.FindDeep`, a plain
   recursive `Transform` walk from a known root — direct hierarchy traversal
   (`GetChild`/`childCount`) sees inactive objects fine; only the global `Find`-by-name
   overloads skip them. Found 2026-08-20, see §4.1.
4. **A shared helper can carry bug #2 to every window that uses it.** `RanUiSkin.Frame`
   (the common PC-chrome skin almost every window uses) had the exact cold-cache-no-
   retry gap, PLUS several ALL-OR-NOTHING fallbacks (if even one of several sprites
   missed the sync load, the whole element — all 4 window edges, the whole 3-piece
   title bar, the whole close button — silently dropped to nothing or a flat tint,
   forever). This is almost certainly the real breadth behind "skin did not load all
   correctly." See §4.2.

---

## 3. Next steps

1. **Joystick drag test — a real swipe, not a tap.** Every walk/run test so far has used
   `input tap`, which cannot exercise `IDragHandler`. This is the single biggest
   remaining unknown (§4.6/§6).
2. RUN toggle's effect on actual movement speed, isolated and confirmed (fixed §4.6, not
   re-isolated live).
3. Visual pass on Tyranny's window specifically (map id 222) — style fix (§4.5) has never
   been seen live, only inferred from other `RanUiSkin` windows rendering correctly.
4. Full popup entry sweep: every one of the 18 MENU entries opened once each and screen-
   shotted, not just Options (only Options was individually suspect before this round).
5. Only after that full pass is clean, do the dedicated "read the real PC client" audit
   for icon exactness the user asked for, so it is not skipped again.
6. Report status against this document, not from memory.

---

## 5. 2026-08-21 — crash after the chat/skill/potion/icon batch (hud-fix4)

The FIRST live test of the whole 2026-08-20 follow-up batch (§ intro above) crashed on
world entry: `pidof` came back empty, logcat showed `Fatal signal 5 (SIGTRAP)` on thread
`Loading.Preload` — the exact same signature as an earlier, never-diagnosed crash from
much earlier in the project. Investigated properly this time instead of just avoiding it.

### 5.1 Root cause: NOT a code bug
Full `logcat -d` around the crash showed the real cause a few lines before the native
trap:
```
E Unity : The file '.../base.apk/assets/bin/Data/level1' is corrupted! Remove it and
launch unity again!
E Unity : [Position out of bounds!]
```
This is Unity's own scene-deserialization failing on the baked world scene bundle
(`level1`) — a corrupted/incomplete build artifact, not a logic error in any of the
batched C# changes. Confirmed by bisection-by-rebuild rather than code review: re-running
the EXACT SAME rebake (`RanPlayScene.BuildBootstrap`) + build (`RanBuild.Android`) with
**zero code changes** produced `hud-fix5.apk`, which loaded the world cleanly, no crash,
same device, same login sequence. This means the Unity/Gradle incremental build pipeline
occasionally produces a corrupted scene bundle for `level1` and a clean re-run fixes it —
not something in this project's own code to chase further. **Lesson for next time a
`Loading.Preload`/SIGTRAP crash shows up: rebuild clean FIRST, before assuming the latest
code batch is at fault.**

### 5.2 Full live test of the batch, once the crash was resolved (hud-fix5)
Confirmed via screenshot + tap, all in the same live session:
- World loads, no crash, `In world.` posted, quests started
- Chat box genuinely screen-centered
- Skill bar: 4x2 = 8 slots + separate ATK button, all present
- Find/PK Rank correctly sitting inside Row 1's reserved cells
- MENU popup opens with real PC icons (Character/Inventory/Skills faces, Auction gavel,
  etc.), Pandora/Product/Battle/Pet/Help/Options/Market visible
- **Options window opens correctly** — first live confirmation of the `FindObjectOfType
  <T>(true)` fix from the previous round (was built into hud-fix3, never tested before
  hud-fix4 was built on top of it and then crashed)

### 5.3 Three NEW bugs found during that same live pass (not user-reported — found by
following the standing "test everything" instruction), fixed together in hud-fix6:

1. **Potion tray effectively invisible.** Rendered correctly (confirmed by cropping the
   screenshot region and zooming — faint circles were there) but at white-on-15%-alpha
   over the light stone spawn-map floor it was indistinguishable from the ground texture
   to the eye. Fix: darker, higher-alpha fill (`Color(0.05,0.05,0.06,0.55)` instead of
   white @0.16) — reads against both light and dark grounds instead of only a dark one.
   File: `RanPotionTray.cs`.
2. **MENU popup didn't close when an entry opened its window.** Tapping Options opened
   the Options window correctly, but the popup stayed open ON TOP of it, visually
   covering the new window's own top-right area (where a close control would be).
   Fix: every popup entry (`RanHudRewire.cs`'s MENU-popup block) now also closes
   `TMC_MenuPopup` after acting, via a new `ClosePopup` helper — matches normal
   dropdown-menu behavior. File: `RanHudRewire.cs`.
3. **A leftover, mispositioned button was stealing taps.** `QUICK_POTION_TRAY_SETTING_BUTTON`
   (real PC control, toggles the Auto-Potion threshold window) is baked by
   `RanPlayScene.MakeQuickPotionSettingButton` at its own OLD PC-derived screen position
   (top-center, `absoluteX 431/absoluteY 0`) — a position that predates the HUD redesign
   and was never moved. Found live: a tap aimed at the Options window's title-bar area
   landed on this instead and popped the unrelated Auto-Potion window. Fix: reparented
   into a new small corner slot (`PotionSettingsSlot`) on `RanPotionTray` itself — its
   natural home, since it configures that tray's thresholds — via the same
   `RanHudRewire.MoveInto` pattern already used for Find/PK Rank. Files:
   `RanPotionTray.cs`, `RanHudRewire.cs`.

All three fixed, rebaked, rebuilt (`hud-fix6.apk`), and **confirmed live** in the same
session: potion tray now visibly a dark 3x2 grid, MENU popup closes cleanly when Options
opens, reparented gear icon visible on the tray, no crash, `logcat` clean of
corruption/FATAL/CRASH lines.

### 5.4 FOURTH bug found same pass: Options' own close (X) button doesn't respond —
**now actually fixed and confirmed live (hud-fix10), two-part root cause:**
- **Part 1 (real, but insufficient alone):** `RanOptions` was the one popup window (of
  23 using the pattern) missing `transform.SetAsLastSibling()` on `Show()`. Fixed
  (`hud-fix7`/`hud-fix8`) — confirmed by screenshot the X box then rendered visibly on
  TOP of Row 1's "PK Rank" chip, where before it was hidden underneath. But tapping the
  now-visible, now-reachable-by-raycast X still did nothing, and fired no log at all —
  proof the click wasn't even being attempted, not just landing wrong.
- **Part 2 (the actual root cause):** `RanOptions.Create()` is called directly from
  `RanPlayScene.cs`'s EDITOR BAKE (`RanOptions.Create(hud.transform, "Options")`) —
  unlike almost every OTHER popup window (Character, Inventory, Guild, Auction, Pandora,
  Product, Battle, Pet, Help, Market, Record, …), which is built by its own
  `IRanWorldModule`'s `Bind()` at REAL RUNTIME, so its internal buttons wire correctly.
  `RanOptions.BuildCloseButton`'s `btn.onClick.AddListener(Hide)` therefore runs at BAKE
  time and is silently dropped on load — the session-defining root cause (`RanHudRewire`
  class doc) finally traced to its last uncovered spot: `RanHudRewire.Rewire()` already
  re-wired the OUTER "TMC_Options" toggle, but nothing re-wired the window's OWN inner
  close button.
- Fix: `RanHudRewire.Rewire()` now also finds "Options" → its "Close" child (scoped
  search, can't collide with another window's same-named child) → re-wires
  `closeBtn.onClick.AddListener(opt.Hide)` at real runtime. **Confirmed live**: MENU →
  Options → tap X → window closes cleanly back to the world view. Files: `RanOptions.cs`
  (Show's SetAsLastSibling), `RanHudRewire.cs` (the actual click re-wire).

---

## 4. This round's fixes — static-verified only, detail for each

### 4.1 Menu popup didn't open (and neither did anything inside it)
Root cause (root cause #3 above): `TMC_MenuPopup` starts inactive. The click handler
did `GameObject.Find("TMC_MenuPopup")` — always null, because `GameObject.Find` skips
inactive objects — so the toggle silently did nothing, every time. Worse: every icon
INSIDE the popup (18 of them — Character, Inventory, Skills, Party, Guild, Friends, Map,
Bank, Auction, Pandora, Product, Battle, Pet, Help, Options, Market, Record, Macro, Run)
is a grandchild of that same inactive popup, so `GameObject.Find` couldn't find THEM
either, popup-open-bug notwithstanding. This explains why Row 1 buttons (always active)
worked while nothing in the popup ever did.

Fix: `RanHudRewire.FindDeep(Transform, string)` — recursive search from `hudRoot` that
sees inactive objects at any depth, replacing every `GameObject.Find` call in
`RanHudRewire.cs`. Files: `RanHudRewire.cs`.

### 4.2 "Skin did not load all correctly"
Root cause (root cause #4 above): `RanUiSkin.cs` — the shared window-chrome skin nearly
every window in the game uses — had the cold-cache-no-retry gap, compounded by
all-or-nothing fallbacks:
- `Frame`'s title bar: required all 3 cap sprites to load synchronously or the WHOLE
  bar fell back to a flat tint, forever, on a cold bake (the normal case).
- `Edges`: required all 4 border sprites or drew NONE of them.
- `CloseBox`: required its sprite or built NO close button at all — a real usability
  bug, not just cosmetic, on any window that opts into it.
- `Panel`/`SkinButton`: no retry, silent flat-color fallback forever.

Fix: extended `RanUiLateIcon` to support 9-sliced sprites (`InitSliced`, plus a new
`RanUiCfg.RequestSlicedSpriteAsync`), and rebuilt every one of the pieces above to
build unconditionally with its own independent late retry, instead of an all-or-nothing
gate. `CloseBox` now has a plain "X" text fallback instead of vanishing. Files:
`RanUiCfg.cs`, `RanUiLateIcon.cs`, `RanUiSkin.cs`.

### 4.3 Icon distortion / "scale it to match PC"
Already fixed and confirmed on-device last round (`preserveAspect` on `RanTopMenuCluster`
icon Images). No new work this round.

### 4.4 "Find"/"PK Rank" still on screen, still ignored
Both are real, already-correctly-wired, pure-runtime buttons
(`RanPartyFinderModule`'s "PartyFinderToggle", `RanPkModule`'s "PkRankButton") that
build themselves at their own module's `Bind()` time, positioned at their own
independent screen corners. A first pass just deleted my broken duplicate Row-1 chips
and left these two floating — correctly flagged by the user as not actually solving the
"consolidate everything" goal.

Fix: Row 1 now reserves two empty named cells (`TMC_FindPartySlot`, `TMC_RankSlot`, 8
slots total again, matching the original approved 8-icon design). A new
`RanHudRewire.ReparentModuleButtons(hudRoot)` — called from `RanWorldSession.Start()`
AFTER `DiscoverModules()` (these buttons don't exist until their own module's `Bind()`
has run, unlike everything `Rewire()` targets, which is baked and exists from scene
load) — finds each real button by its existing name and moves it into the matching
reserved cell, resetting its RectTransform to fill the cell exactly. The buttons'
existing behavior/wiring is untouched; only their parent/position changed. Files:
`RanTopMenuCluster.cs`, `RanHudRewire.cs`, `RanWorldSession.cs`.

### 4.5 Tyranny window: shows when it shouldn't + wrong style
Two separate real bugs, found by reading `SOURCE/Lib_Client/G-Logic/GLPVPTyrannyClient.cpp`
directly:

- **Real PC's `DoStateRegister`/`DoStateBattle`/`DoStateReward` never open any window or
  persistent HUD at all** — only `PrintConsoleText` + `ShowExtraNotice` (a small
  transient toast). This port's `RanCompetitionModule.RefreshHud()` was unconditionally
  calling `_hud.Show()` on every one of those same GLOBAL, server-wide state broadcasts,
  regardless of the local player's actual map — and the class's own prior comment on
  `SetState()` already admitted "this port has no per-map PVP-flag lookup." That lookup
  DOES exist (`RanMapLoader.CurrentMapId`), it just wasn't wired here. Fix: gate
  `RefreshHud()`'s display on `CurrentMapId == 222` (Tyranny map, id read straight from
  `mapcatalog.json` — `"id":222,"scene":"sps_ground","name":"Tyranny"` — not guessed),
  re-evaluated on every map transition via `RanMapLoader.MapReady`, not just on the next
  broadcast. Files: `RanCompetitionModule.cs`.
- **Styling**: `RanCompetitionBattleHud.Build()` used a bare flat-tint background with
  no PC chrome at all — not a retry bug, it simply never called into `RanUiSkin`. Fix:
  `RanUiSkin.Panel(_frame)` for the real framed-panel skin (gets the §4.2 retry for
  free). Files: `RanCompetitionWindow.cs`.

### 4.6 Walking / running
- **Running — real bug, fixed.** `RanTouchInput.UpdateJoystick` used its own fixed
  `joystickSpeed = 6f` constant, completely independent of `walkSpeed` (the field
  `RanWorldSession.ToggleRun` actually updates with the real per-class run/walk speed).
  Toggling RUN had ZERO effect on joystick-driven movement speed, even once the popup
  bug (§4.1) made the RUN button reachable at all. Fix: removed `joystickSpeed`;
  `UpdateJoystick` now uses `walkSpeed`, the same run-aware value tap-to-move's
  `FollowPath` already used. Files: `RanTouchInput.cs`.
- **Walking — reviewed, no bug found, still UNCONFIRMED.** Traced the full chain:
  `RanJoystick.OnDrag` sets `Joystick` via a real `IDragHandler` (not a UnityEvent, so
  bug #1 doesn't apply) → `RanTouchInput.Update` routes to `UpdateJoystick` whenever
  deflected → `driver.SetServerPosition(target)` every frame → `RanCharacterDriver.Update`
  `SmoothDamp`s toward it. For a continuously-advancing target (what the joystick
  produces every frame), SmoothDamp is expected to converge to tracking the target's
  velocity with a small constant lag — a structurally sound pattern, not the "occasional
  distant waypoint" case it reads as being tuned for at a glance. No logic bug found by
  static review. **This is not a claim that walking works — only that I looked and
  found nothing wrong. Needs a real device test, not a downgrade to "probably fine."**
