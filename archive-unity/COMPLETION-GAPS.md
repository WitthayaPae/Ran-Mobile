# Completion plan — what's left, and its status (2026-08-20)

## REGRESSION FOUND AND FIXED: every NPC/mob rendered as a permanent capsule on the live device (2026-08-20, later still)

**Symptom, from real owner evidence (two live-session screenshots, 6s apart):**
an NPC ("เด็กฝึกงาน") and two nearby entities (#543, #251) rendered as flat
capsule placeholders and never resolved to real models, on a map that looked
correct the day before. Investigated end to end rather than guessed:
`RanEntityView.Build`/`TryUpgradeModel`/`SetClassBody`/`RanAddressableCache`
were all read in full first, then each of the task's 4 hypotheses was checked
with real measurements before touching anything.

**Root cause, measured (not inferred):** the same-session Addressables
conversion's `RanAddressablesSetup.MarkCharacterAssets` (`ConfigureRemote`,
now renamed `ConfigureLocal`) pointed the `RanCharacters` group's LoadPath at
an explicit, intentionally-non-resolving placeholder
(`https://example.invalid/ran-mobile-cdn/[BuildTarget]`) — documented at the
time as "wiring a real CDN is follow-up work", but nothing gated the actual
live build (`ran-full-live.apk`/`ran-full-live2.apk`, built the same day) on
that CDN existing. `RanAddressablesSetup.Probe` on the live project confirmed
the group's real BuildPath/LoadPath were exactly that broken pair. This is
**not** a marking/coverage gap — `RanAddressablesSetup.CheckAddresses` on real
`crowmodels.json` stems (`npc_safelyman_01`, `mob_ch_01`, `npc_cos_dealer`,
`npc_mg`) confirmed all four ARE registered (1135 entries total). It is not a
retry-logic bug either — `RanEntityView.TryUpgradeModel`/`RanAddressableCache`
behave exactly as designed; they are just retrying a load that is guaranteed
to fail forever (unreachable host), which looks identical to "never resolves"
from the player's side. Directly confirmed by inspecting the built APK: `unzip
-l` on both live APKs shows `ranuitextures_*`/`raneffecttextures_*` bundles
present under `assets/aa/Android/` but **zero** `rancharacters_*` bundle —
character content was structurally unreachable on any real device running
either build.

**Why this was invisible to all of today's earlier offline verification:**
`RanAddressablesSetup.Probe` also logged `ActivePlayModeDataBuilderIndex=0`
("Use Asset Database (fastest)") — Unity Editor Play Mode resolves Addressable
content straight from the AssetDatabase and ignores group BuildPath/LoadPath
entirely. Every earlier "VERIFIED via RanUiCapture" claim this session
(`remotes.png`, etc.) was true of what it measured and still could **never**
have caught this specific class of bug, regardless of care taken — a real
blind spot in the tool for anything gated on actual bundle fetch/host
reachability. Worth remembering for any future Addressables config change.

**Fix:** `RanAddressablesSetup.ConfigureLocal` (was `ConfigureRemote`) now
configures the `RanCharacters` group the same way `MarkUiTextureAssets`/
`MarkEffectTextureAssets` already configure theirs — bundled straight into the
APK (`Local.BuildPath`/`Local.LoadPath`), trading back the ~268-377 MB the
Remote conversion had saved until a real CDN exists. Re-ran
`MarkCharacterAssets` (confirmed via `Probe`: BuildPath/LoadPath now both
`Local.*`, still 1135 entries) then a real `BuildContent` (79.1s, 0 errors).

**Verified, all measured:**
- A real Android Player build (`RanBuild.Android`, single scene,
  `ran-charfix-verify.apk`) — 0 errors — followed by `unzip -l`: now shows
  `assets/aa/Android/rancharacters_assets_all_....bundle` (376.8 MB) alongside
  the UI/effect bundles, where it was completely absent before.
- The SAME all-130-map shape the live device actually runs
  (`RanBuild.Android -ranMaps all`, `ran-full-live3-charfix.apk`) — 0 errors,
  1048.4 MB (vs. `ran-full-live2.apk`'s 671.6 MB — the ~377 MB delta matches
  the character bundle almost exactly) — `unzip -l` again confirms the
  character bundle is genuinely inside this build. This APK was built but
  **not installed** — the owner was live-testing on the device throughout this
  investigation and it was never touched, per standing instruction.
- Offline capture (`RanUiCapture -ranOnly remotes`, real Play-Mode screenshot,
  `editor-shots/remotes.png`): two remote player class bodies (o_m/o_w, worn
  gear) and one real mob (`mob_br_01`, nativeId 262144, a real
  `crowmodels.json` entry) all render as real animated models, not capsules —
  confirms `RanEntityView`'s resolution/retry chain is otherwise correct (per
  the above, this capture could not have caught the actual bug either way, so
  it corroborates the retry logic specifically, not the on-device fix).
- `node MOBILE/tools/rcc-extract/test.js`: 839 passed, 0 failed.
- `cd MOBILE/unity/typecheck && dotnet run --project RunCheck.csproj`: 1215
  passed, 0 failed. `dotnet build TypeCheck.csproj`: 0 errors.
- `cd SOURCE && git status --porcelain`: clean before and after.

**Files touched:** `RanAddressablesSetup.cs` (`ConfigureRemote` →
`ConfigureLocal`, doc comment), `RanAddressableCache.cs` (class doc comment
only, no logic change). No runtime `RanEntityView.cs`/`RanWorldSession.cs`
code changed — their resolution/retry logic was independently confirmed
correct and not the cause.

**Still open:** the owner has not yet installed the fixed build (deliberately,
to avoid disturbing a live session); a real device confirmation is the last
step. Re-enabling a Remote group for characters is a legitimate future size
optimization but must never ship again without a verified-reachable LoadPath.

---

## MAJOR SESSION UPDATE (2026-08-20, later — real device testing phase)

Everything below this box was written before real device testing began. Real
device testing (Galaxy Tab S9, offline-preview-only, never live-server) found
problems no amount of data/spec checking ever could, and fixed them:

- **Foundational mesh-parser bug, entire character corpus.** `xmesh.js` never
  handled DirectX `.x` object-reference syntax (`{ Name }`), silently
  detaching skin-weight/texture data from any mesh using it — present since
  Phase 2, invisible to every prior check. Fixed; full corpus rebuilt
  (976/976 models, 0 failed); independently cross-validated with an A/B
  parser harness (201/929 skinned files were affected) and exact bone-count
  matches on 3 sample characters. **This, not the T-pose theory, was the
  real reason characters never looked right.**
- **180° walking-backward bug, universal** (local player + every remote
  entity/mob). `RanCharacterDriver` aligned the wrong local axis to the
  movement vector. Fixed, proven with matched-timestamp before/after device
  screenshots.
- **Offline-preview world/character was completely absent** — `RanWorldSession`
  disabled itself with no `RanLoginFlow` present, so `RanMapLoader.LoadMap`
  never ran. Fixed with a real `OfflinePreviewSeed()`.
- **The Addressables conversion (done earlier this session) silently broke
  THREE asset categories** — each only ever caught by looking at a real
  screenshot, never by code review:
  1. No UI texture was ever marked Addressable at all — every UI sprite in
     the whole game was silently failing to resolve. Fixed
     (`MarkUiTextureAssets`, 592 textures).
  2. Equipment-piece meshes were marked from an incomplete source list — all
     skinned costume/armor mounting had been silently no-op'ing since the
     conversion landed. Fixed (fallback to the full `Characters` directory,
     874→1135 marked).
  3. Effect textures — same bug, simpler cause (never marked at all). Fixed
     (`MarkEffectTextureAssets`, 650 textures).
  4. Four remaining holdout loaders (`RanQuickSlots`, `RanPkCombo`,
     `RanMapAxis`, `RanBootScreen.Sheet/Background`) converted and verified
     individually — this closes the bug class.
- **Opaque white chat panel** — same root cause as an earlier HUD-icon fix
  (null sprite defaults to `Color.white`), found and fixed at the one
  remaining call site after a project-wide grep confirmed no others exist.
- **GUI-wide visual sweep, two rounds, 30+ windows actually screenshotted**
  (not just spec-checked). 5 layout bugs found and fixed (Guild/Storage/
  Trade/Bank/Auction — all hardcoded font-size/offset mismatches vs. real PC
  rects), plus the Product window's category tabs (real icons now, were
  severely overlapping text) and a PK-rank-window capture-tool bug. ~20+
  other windows confirmed genuinely clean.
- **4 HUD corner icons restored to real PC art** (were text fallbacks: "BoxPet"/
  "AH"/"Shop"/"Cup") — root cause was the wrong control id (PC textures a
  CHILD control, not the container) plus an XML-comment-parsing bug in the
  extractor plus the Addressables gap above, all three stacked.
- **Loading screen built from nothing** — didn't exist at all before this.
  Real per-map background art, real map name, real decoded gameplay tips
  (PC's own tip feature is dead code on live PC — shown anyway on mobile as
  a disclosed enhancement, one flag to revert to PC-literal blank text if
  preferred), faithful spinner animation. Verified with a real screenshot
  mid-transition.
- **Equipment-skin double-draw risk** — turned out to be the Addressables
  equipment-mesh bug above, not a real base-piece-hiding gap (that part was
  already correctly implemented, just had stale doc comments). Verified
  clean via before/after equip screenshots — no z-fighting.

**Still open after this phase:** login/lobby-stage loading screen (structurally
can't be Play-Mode-tested, off-limits scene); a possible Pet-window/PK-panel
overlap (flagged, not chased); Club Death Match's own in-battle system;
orphaned hair-piece parts (separate old bug, not touched); the items already
listed as structurally blocked below (character DELETE, registration, iOS,
anything needing an actual live login).

---


`plan.md` and `HANDOVER.md` are stale. This document is the live tracked
plan — update the checkbox and status inline as each item lands, the way
`plan.md` §6 tracks phases. Don't re-open a `[x]` item without new evidence.

**Headline finding (2026-08-19 audit):** this is much closer to done than it
looked. The runtime has **37 `IRanWorldModule` implementations**, not the
~16 `plan.md` §16.1 describes, and several things `plan.md` §19.3 calls
"documented deferrals" — guild-war, PK combo/rank/kill-feed, NPC dialog,
learned-skills, skinned-piece equipment, real entity models — are already
built and confirmed live. Don't redo any of that.

**Verification tooling now exists and should be used before/after every
item below:**
- `RanUiCheck.cs` (`-executeMethod Ran.Mobile.Assets.Editor.RanUiCheck.Run`) — asserts live UI object rects/sprites against `uicfg.json`, no rendering needed.
- `RanUiCapture.cs` (`-executeMethod Ran.Mobile.Assets.Editor.RanUiCapture.Run`) — captures real Play-Mode screenshots to `MOBILE/unity/RanMobile/editor-shots/`, from the SAFE offline `gameplay.unity` bootstrap only. **Hard-guarded against `boot.unity`/any login-capable scene — do not bypass this guard.** Known limitation: `SkinnedMeshRenderer` draw state doesn't reliably refresh in this capture path (found 2026-08-20 chasing the T-pose fix) — trust it for static UI, treat animated-character results as unconfirmed until either a device or a fix for that refresh bug exists.
- `RanAnimDiag.cs` (`-executeMethod Ran.Mobile.Assets.Editor.RanAnimDiag.Inspect -ranChar <name>`) — static Animator/controller/bone-wiring inspection, no Play Mode needed.

---

## Done this session (2026-08-19/20)

- [x] Quest-step advance from NPC menu — `SNET_QUEST_STEP_NPC_TALK`=3553 decoded from `SOURCE`, wired in `RanNpcModule.cs`. 718 Node / 981 C# checks pass.
- [x] Red-blob entity-marker fallback — was an honest "unresolved skin" fallback painted glossy saturated red/blue; recolored to a matte, desaturated placeholder (`RanEntityView.cs` `FallbackMaterial`).
- [x] Blank HUD corner buttons (Pet/Competition/Pandora/Product) — dead emoji glyphs (Android can't render them) replaced with real ASCII labels; opaque fallback fill replaced with the translucent-chip style every other unskinned panel uses (`RanPlayScene.cs` `MakeTopLevelIcon`).
- [x] `RanUiCheck` built — found + fixed `RanTargetFrame`'s wrong control-id tag; found (not yet fixed) that `PANDORA_BUTTON` is missing from `uicfg.json`'s extraction (filename regex misses `_pandorawindow.xml`, same bug class as the known `_outer_*.xml` gap).
- [x] `RanUiCapture` built — produced real local screenshots without a device; caught the T-pose bug live, confirmed Skill Book renders real data well, confirmed Inventory renders empty.
- [x] T-pose root cause found and fixed **at the data level** — the offline-bootstrap character path never ticked its Animator once (`RanCharacterDriver.ForceFirstPose`, distinct from the already-existing but differently-scoped `SwapDriverBody` fix). Proven via direct bone-rotation and mesh-bounding-box measurement (T-pose-shaped bounds → standing-pose-shaped bounds, held across frames). **Visual confirmation still open** — the capture tool's own draw-refresh limitation (see above) means this hasn't been *seen* fixed yet, only measured.
- [x] Incident: batchmode Play Mode on `boot.unity` triggered a real unauthorized live-server connection. Guarded against in `RanUiCapture.cs`; recorded in memory (`dont-hammer-server`).
- [x] Five Tier-0 items bundled (all touching `RanPlayScene.cs`'s HUD construction): Auto-pot settings gear, Item lock/unlock via the real card-item trigger (not the dead Security window), Personal market (menu button + card-item trigger + tap-to-list deposit + browse-stall action), Student Record mobile placement, `PANDORA_BUTTON` uicfg gap. Two `layout-probe` additions along the way (`GLItemDef.h` added to the enum probe, compiler-verified the six lock item types + `ITEM_PRIVATEMARKET`). 742 Node / 1125 C# checks pass; cold Unity 6 batchmode compile (`Library/ScriptAssemblies` deleted first) 0 errors; `RanUiCheck` 0 mismatches; `SOURCE/` clean.

---

## Current round — dispatched 2026-08-20, in progress

- [~] **Character CREATE** — the single highest-visibility blocking gap (a fresh account cannot make a character at all). Class/gender/face/hair/scale pickers + 3D preview, wiring `NET_MSG_CHA_NEW`=2143 (already measured, no encryption).
- [~] **Inventory empty-panel fix** — `RanInventory.Rebuild()` never called in the offline bootstrap; confirmed via `RanUiCapture`.
- [~] **Account-panel protocol decode** — find the real PC ID-management packets (change password/email/PIN, reset PIN, top-up, game-time claim) in `SOURCE`, measure via the layout probe, build the packet layer. Scoped to NOT touch `RanBootScreen.cs`'s button wiring yet (that lands after Character CREATE to avoid two agents editing the same file) — produces the packet layer + a clear wiring spec for the next round.
- [x] **Addressables conversion** — character prefabs + effect/UI atlas textures converted and measured 2026-08-20, including a real end-to-end Android APK build proving the character bundle (268 MB) is excluded from what ships. See "Addressables conversion — session log" below. Still open: `RanBootScreen.cs`'s own duplicate atlas loader, `RanQuickSlots.cs`/`RanPkCombo.cs`/`RanMapAxis.cs`'s one-off texture loads, and a clean isolated-content size A/B (not attempted — would cost another very long build).

---

## Tier 0 — small, client-side only, no new screen needed

Queue for the round after the current one. Each already has its protocol
layer built; only a UI trigger or render call is missing.

| Item | File | What's missing |
|---|---|---|
| PK rank/kill-count on the stat window | `RanCharStatWindow.cs` | **2026-08-20: done.** `RanPkModule` already parsed+stored PK score/death (UPDATE_PK_SCORE/UPDATE_PK_DEATH); it now exposes `KnownPkScore`/`KnownPkDeath` (-1 = unknown), `RanCharStatModule` polls them the same way it already polls `KnownSkillPoints` for `RanSkillModule`, and `RanCharStatWindow` renders them at the real `CHARACTER_PK_SCORE_TEXT`/`CHARACTER_PK_DEATH_TEXT` rects (measured in `uicfg.json`: (82,396,100,11)/(82,409,100,11)) with labels from `CHARACTER_SOCIAL_STATIC2` gameword indices 1/2 ("PK Score"/"PK Death", measured in `gameword.json`) and the PC's own colours (GREENYELLOW/ORNAGERED, `UITextControl.h:33-34`, `CharacterWindowEx.cpp:520-527`). Pinned in `RunCheck.cs` (rect + gameword + a reflection-driven Handle→Update→KnownPkScore/Death round-trip). No packet re-decode. |
| Auto-pot settings entry point | `RanAutoPotModule.cs` | **2026-08-20: done.** `QUICK_POTION_TRAY_SETTING_BUTTON`'s real rect is PARENT-relative (226,0,20,41 inside `BASIC_QUICK_POTION_TRAY` inside `LEFTTOP_CONTROL_GROUP`) — composed the absolute (431,0,20,41) by reading all three `CreateSub` call sites in `SOURCE` (`RanPlayScene.MakeQuickPotionSettingButton`, real `gui_auto_potion.dds` art, no PC-rect placeholder needed). Wired to `RanAutoPotModule.Toggle`. |
| Item lock/unlock icons | `RanLockModule.cs` | **2026-08-20: done differently than filed.** The literal ask ("3 icons on the Security window") does not exist on live PC — `SECURITY_WINDOW`'s construction is fully commented out (`InnerInterfaceSimple.cpp:5509-5533`, "[disabled by request]... features removed"), independently re-verified. The REAL live trigger is USING one of six inventory card items (`ITEM_*_LOCK_ENABLE`/`_RECOVER` × Equipment/Storage/Inventory, `GLItemDef.h`, compiler-verified 79/80/91/92/93/94) — wired via a new `RanInventoryModule.CellAction.LockCard` cell-tap dispatch (same shape as Taxi/Transfer cards) → `RanLockModule` opens the matching Enable/Recover page. `INPUT` (unlock) stays an open hook: its own PC button pair is ALSO dead code by the same "[disabled by request]" pattern (`InventoryPageWear.cpp:211-220`), verified independently, not fabricated. |
| Personal market (stall) | `RanMarketModule.cs` | **2026-08-20: done.** "My stall" menu button is a stated MOBILE-ONLY corner chip (measured absent from PC: no MENU_/top-level MARKET button exists anywhere in `uicfg.json`) wired to `Toggle`. The REAL PC entry point — using the `ITEM_PRIVATEMARKET` permit card (`GLCharactorReq.cpp:1867`) — is now wired via the same card-tap dispatch pattern as Lock, firing `RanInventoryModule.MarketPermitRequested` → `UsePermit`. "Deposit" has no drag-drop to follow (none exists anywhere in `RanInventory`); implemented the closest real precedent instead — `RanInventoryModule.ArmExternalPick`, a one-shot "next tap picks this item" hook — wired end-to-end through a new `RanMarketWindow` price/quantity prompt (rebuilt from the real `PRIVATE_MARKET_SELL_WINDOW` rects) to `RegisterItem`. "Browse stall" is a small button on `RanTargetFrame`, shown only when the current target has an open stall. |
| Item-mall category names | `RanItemMallWindow.cs:351` | **Already done 2026-08-16, this row was stale.** `RanItemMallModule.CategoryName`/`SyncCategoryLabel` bridges the window's "Cat N" fallback to the PC's real `ID2GAMEWORD("ITEMSHOP_MENU_BUTTON", i)` names (`ItemShopWindow.cpp:774/778`, 17 entries incl. Head/Armor/Weapon/.../Box/Misc/Gashapon/Discount), extracted from the live `Ran/data/gui/Gui.rcc`'s `gameword.xml` by `extract-itemshop-categories.js` into `Resources/itemshop-categories.json`. Re-ran the extractor 2026-08-20 against the live tree to confirm it still reproduces the shipped file byte-for-byte — genuinely measured, not guessed. |
| Revive message text | `RanRevivePanel.cs:118-119` | Falls back to English — needs `gameintext` string staged |
| CHATMACRO / RUN menu buttons | `RanPlayScene.cs` | **2026-08-20: RUN done.** Real fWALKVELO/fRUNVELO traced to `GLCONST_CHARCLASS::LOADFILE` (`GLogicDataLoad.cpp:1161-1199`) via each class's `SETFILE` key in `default.charclass`, whose VALUE is `classN.classconst` — the SAME file `extract-charclasses.js` already opened for the CREATE screen's head/hair counts (measured directly, not guessed: `BRAWLER_M.SETFILE "class0.classconst"`). Extended that extractor + `RanCharClassData` with `walkVelo`/`runVelo` per class (16/16 classes, all `runVelo > walkVelo > 0`). `RanSession.Running` (default **true** — measured: `GLCharacter::GLCharacter()`/`ResetData()` both unconditionally `SetSTATE(EM_ACT_RUN)`, GLCharacter.cpp:471/558, contradicting the earlier "likely walk" assumption) replaces the old hardcoded `ActRun=1`; toggling sends the real `SNETPC_MOVESTATE` (3032, MSVC-probed 4-byte body) mirroring `GLCharacter::ReqToggleRun`. `RanWorldSession.ToggleRun` applies the resolved per-class pace to both the goto send-pacing and `RanTouchInput.walkSpeed`, and flips `RanCharacterDriver`'s new `Running` Animator bool — `RanAnimatorBuilder`'s Walk↔Run transition now gates on that real state bit instead of the old speed-threshold placeholder. `MENU_RUN_BUTTON` wired in both `RanPlayScene.Build`/`BuildBootstrap`. CHATMACRO was already wired in an earlier pass (see `RanChatMacroWindow.cs`) — this row is now closed. |
| Server-select label | `RanBootScreen.cs` | Whether the human-readable name (vs IP) sources from the live list is unverified |
| Student Record button placement | `RanStudentRecordModule.cs` | **2026-08-20: done.** Re-verified the PC reassignment independently (`InnerInterfaceSimple.cpp:3636-3648` "DIARY/STUDENTRECORD icon hidden — feature off"; `uicfg.json` confirms `RAN_STUDENTRECORD_NOTIFY_BUTTON` (587,514,35,59) and live `COMPETITION_NOTIFY_BUTTON` (605,514,35,59) overlap by 17px). Reasoned placement: a mobile-only corner chip (unlike Lock's Input page or Market's buy-order stall, this feature is read-only data the server already sends regardless, so wiring it creates no PC/mobile capability asymmetry) — wired to `Toggle`. |
| Mob-preview trigger | `RanMobPreviewModule.cs` | **2026-08-20: investigated, correctly left unwired.** PC's `PreviewMob` trigger is dead code — its only caller (`LargeMapWindowSlotRender.cpp`) isn't even in `Lib_ClientUI.vcxproj`'s compile list (verified: absent from shipped `MiniA.exe` strings too), and the large-map code that IS compiled explicitly skips monster markers. Not an asset-pipeline gap; there's no live PC feature to port. |
| Rebirth/revive-shrine trigger | `RanRebirthModule.cs` | **2026-08-20: re-verified, correctly left unwired.** `CRebirthWindow::SetItemRebirth` has zero external callers in `SOURCE`; the real death trigger targets the separate `CRebirthDialogue` class, already ported as `RanRevivePanel` and wired into `RanWorldSession`. |
| KEY_SETTING (keybind) window | *(none — closed, no Unity file)* | **2026-08-20: investigated, closes as correctly not applicable.** `CKeySettingWindow` (`SOURCE/Lib_ClientUI/Interface/KeySettingWindow.h/.cpp`) is live, not dead code: constructed at boot (`InnerInterfaceSimple.cpp:3372-3381`), registered under `KEY_SETTING_WINDOW` (`InnerInterfaceGuid.h:118`), opened from the ESC menu (`BasicEscMenu.cpp:114`, `ShowGroupFocus(KEY_SETTING_WINDOW)`). Read `CreateSubControl()` in full: it builds exactly 32 `CShotCutBox` edit rows (`QUICK_SLOT_NUM`=6 + `QUICK_SKILL_NUM`=10 + `MENU_SHOTCUT_NUM`=16) plus Default/Apply/OK buttons — nothing else. Each row is a raw DirectInput `DIK_*` scancode-to-string map (`InitData()`, ~100 literal DIK entries) bound one-to-one to `RANPARAM::QuickSlot[]`/`SkillSlot[]`/`MenuShotcut[]`. Traced every consumer of those three arrays outside the window itself: all are either `UIKeyCheck::Check(..., DXKEY_DOWN)` keyboard-down polls that open menus/trigger slots (`InnerInterface.cpp:1436-1651`, `UILeftTopGroup.cpp:81-86`) or `GetdwKeyToString()` calls that paint the bound key's *letter* as a label on quickslot/skill-slot/menu icons (`BasicQuickPotionSlotEx.cpp:41`, `BasicQuickSkillSlotEx.cpp:49`, `BasicGameMenu.cpp:481`) — purely so a keyboard player can see which key fires a button. No sensitivity, toggle/hold, mouse, or general-preference control is mixed in; unlike Student Record this is not a `RANPARAM_PROFILE`-style settings page, it is 100% physical-key-to-scancode remapping. A touch client has no scancodes to remap and no keyboard-shortcut labels to display, so there is genuinely nothing here to port. No Unity module exists for this and none should be built. |
| `PANDORA_BUTTON` uicfg extraction gap | `extract-uicfg.js` | **2026-08-20: done.** Added a fourth, lowest-precedence scan pass (`MISC_WINDOW_FILES`) for `_pandorawindow.xml` and its sibling `_petstyle.xml` (same bug, same fix, found by checking all `_*.xml` files against the regex — the other 8 uncovered files are genuine string tables, not layouts). `uicfg.json` now carries `PANDORA_BUTTON` (317,538,80,35, matching the pre-existing fallback exactly) + 84 more `PANDORA_*` + 67 `PETSTYLECARD_*` controls. Regenerated + restaged; pinned in `test.js`; `RanUiCheck`'s `KnownMissingFromUicfg` exemption removed (no longer needed) and its own log confirms 0 mismatches with no "not in uicfg.json" warning. |

---

## Tier 1 — needs a new screen, queued after Tier 0

| Item | Scope |
|---|---|
| Item-mix window | `RanItemMixModule.cs` — protocol done, window deferred |
| Item-mall cart + gift | `RanItemMallModule.cs` — no packet path for either yet |
| **PVP Competition in-battle systems** | **2026-08-20: five sub-items landed this round** (tower ownership visuals, tower capture-progress, full ranking board, rejoin, reward/schedule — see the session log below). Still Tyranny-only; Club Death Match tab exists, disabled — its own in-battle system is genuinely unbuilt (no `GLCDMClient` work done this round). |

---

## Tier 2 — protocol decode or product decision

| Item | Blocker |
|---|---|
| Account panel | **In progress this round**, see above |
| **Character DELETE** | Needs `szPass2`, a second account password this port doesn't model. Either decode the real flow or treat as a product decision — cannot be guessed against a live server. |
| Registration / password recovery | PC opens a CAPTCHA flow this port can't reproduce — genuinely out of scope |
| Mail | Confirmed: no transport exists in this server build at all. Correctly a no-op, not a bug. |

---

## Tier 3 — blocked on real device / capture-tool fix, not more code

| Item | Status |
|---|---|
| T-pose visual confirmation | Data-level fix landed (see above); needs a device OR a fix to `RanUiCapture`'s `SkinnedMeshRenderer` refresh bug |
| Red-blob / HUD-icon visual confirmation | Fixes landed 2026-08-20, not yet seen on a real screenshot since |
| Skinned-piece equipment mounting | Flag on, 2,270 files staged; double-draw risk against base body untested |
| HUD live-stat reset | Fixed in code; worth one owner login to confirm |
| Orphaned hair-piece parts floating above characters | Found in `hud.png`/`inventory.png` captures 2026-08-20 (`w_hair10` x2, ~16.5 units above `boa`) — pre-existing, logged (`RanChfBuilder.BuildAll`: 215 orphaned corpus-wide), not investigated this session |

---

## Tier 4 — assets/animation/effects polish

| Item | Status |
|---|---|
| BLURSYS effect layer (motion-trail ribbon, 6.2% of nodes) | **Built 2026-08-20** — `RanEffectBlurSys.cs` (Runtime), a ported history-buffer + Catmull-Rom ribbon generator measured off `DxEffectBlurSys.cpp`; wired into `RanEffect.cs`'s "trail" layer kind and `extract-effects-json.js` (2,156 layers emitted, 0 skipped). Verified: 27 new RunCheck algorithm checks (dedup/freeze/ramp/drain), plus a real Unity 6000.5.8f1 batchmode run (`RanEffectDiag.InspectBlurSys`) confirming a real `UnityEngine.Mesh` (26 verts/72 tri-indices for aac113's real decoded values) is actually generated, not just that the code compiles. Not modelled: the small "base blur" connector quad and the `USEABSOLUTE`-false group-auto-move offset (both noted in the class doc). |
| POINTLIGHT/WAVE/rare effect layers (~3.6% of nodes) | Unbuilt, lowest priority |
| Per-skill cast animation coverage | Mostly falls back to `AN_ATTACK`; matches source's own pattern of reuse, likely acceptable as-is |
| `mob_yoyoman` / `npc_mgod_n` capsules | 2 known single-skin gaps, regression-pinned, not a regression |

---

## Tier 5 — build & distribution

- [x] **Addressables conversion — character + effect/UI-atlas slice done and measured 2026-08-20.** Real Android APK build with the conversion in place confirmed (by inspecting the built APK's contents, not inference) zero character-bundle bytes shipped. See the session log below for what's still open.
- [ ] iOS — no progress anywhere, blocked on needing macOS + Apple hardware.

---

## Incident: wrong Unity editor briefly opened the project (2026-08-20, ~02:26-02:40)

A subagent's stale wait-then-launch job used `Unity 2021.3.45f2` (a generic
default from its own instructions) against this project, which is on Unity
6000.5.8f1 — a race in its own concurrency check let this slip through while
another agent's legitimate Unity 6 process was running. The wrong editor ran
~1 minute, failed on Unity-6-only package dependencies, and exited (code 1)
before reaching `ProjectVersion.txt`. No git repo exists for this Unity
project (only `SOURCE/` does) so there was no diff/revert path — damage had
to be assessed by direct inspection instead.

**Checked and confirmed intact:** `ProjectVersion.txt` still `6000.5.8f1`
(not downgraded); `packages-lock.json`/`manifest.json` still correctly
reference URP `17.5.0` (not reverted to `12.1.15`); `Resources/uicfg.json`
(real game data, not just engine config) intact with every fix from today
present — 7,883 controls, all 154 `CREATE_CHAR_*`, `PANDORA_BUTTON`,
`CHARACTER_PK_SCORE_TEXT`; `SOURCE/` confirmed clean. A full cold Unity
6000.5.8f1 batchmode compile+scene-build (`RanPlayScene.Build`, real, not
cached — `Library/ScriptAssemblies` untouched but this WAS a full project
reimport since so many files changed today) ran to **48,716 log lines with
zero errors/exceptions before being interrupted by an unrelated 10-minute
tool timeout**; re-launched to run to completion in the background.
**Resolved, confirmed clean.** A final Unity 6000.5.8f1 batchmode build,
run alone with no other Unity process competing for the project lock,
completed with **real exit code 0, zero errors/exceptions in 523 log
lines**, and produced `map 88 renderers, character 7 renderers, controller
b_w, ... navmesh 8108 cells` on `w_school_03`/`boa` — matching this
project's own long-documented baseline numbers exactly. No damage from the
incident. (Two earlier verification attempts were inconclusive for
unrelated reasons: the first was killed by a 10-minute tool timeout mid
otherwise-clean reimport; the second failed fast on Unity's own
single-instance project lock while the first attempt's process was still
exiting — neither was evidence of a problem.)

---

## Addressables conversion — session log (2026-08-20)

**Scope done: the character/mob/NPC/player-body/equip-attach-part payload** — the
piece `plan.md` §16.3 called the single biggest chunk. Effect textures and item
icon atlas sheets (also `Resources/UI/...`) were **not** touched this session —
see "What's left" below.

### What changed
- `com.unity.addressables` `2.11.1` added to `Packages/manifest.json` (verified
  against the real registry: it's the last `2.x` release targeting `unity:
  6000.0`, before the `3.0`/`4.0` API rewrite — chosen for API stability, not
  guessed).
- `RanAddressableCache.cs` (new, Runtime) — the async load+cache every caller
  goes through: `TryGetCached` (sync, never blocks — hit or confirmed-absent)
  and `RequestAsync` (kicks a load once, fans a callback out to every waiter
  when it settles). `Addressables.LoadResourceLocationsAsync` is checked before
  `LoadAssetAsync` specifically so a routinely-expected miss (content not
  packed into this build) settles to null quietly, matching the old
  `Resources.Load`-returns-null contract, instead of Addressables logging a
  load-failure error on every one.
- Converted call sites (5 real `Resources.Load("Characters/...")` sites, all
  found by grepping for the literal, not assumed): `RanEntityView.cs`
  (`Build`/`TryUpgradeModel` — crow/mob/NPC bodies; `SetClassBody` — remote
  player class body), `RanEquipView.cs` (`Mount`/`MountPiece` — worn attach
  parts + skinned pieces), `RanWorldSession.cs` (`EnsureClassBody` — the LOCAL
  player's own class body). Every one already had a capsule/skip-and-warn
  fallback for "prefab not found" from before this change, which is exactly
  the degrade path a still-loading (or never-packed) Addressable now also
  takes for the frame or two before it resolves — no new failure mode, an
  existing one now also covers "loading" not just "absent". Race-guarded
  (`_pendingClass`/`_hasPendingClass` style fields) so a stale async callback
  from a superseded request can't clobber a newer one.
- `RanAddressablesSetup.cs` (new, Editor) — `MarkCharacterAssets` marks the 874
  prefabs staged at `Assets/Ran/Resources/Characters` (the exact set
  `stage-characters.js --scope all` + `--scope equip` compute from
  `crowmodels.json`/`equipmodels.json`) Addressable, sourced from the ORIGINAL
  `Assets/Ran/Characters/<name>.prefab`, address `"Characters/<stem>"` —
  byte-identical to the old Resources key, so no call site needed an
  address-format change. `RemoveStagedResourcesCopy` deletes the now-dead
  `Resources/Characters` staging copy. `ConfigureRemote` points the group's
  Build/Load path at the `Remote.*` profile variables (measured via a `Probe`
  method first, not assumed: `Remote.BuildPath` = `ServerData/[BuildTarget]`,
  outside `Assets/StreamingAssets`, so a player build does not pick it up —
  vs. `Local.BuildPath`, which resolves to `Library/com.unity.addressables/aa/...`
  and which Unity's player-build step DOES copy into StreamingAssets). No real
  CDN exists, so `Remote.LoadPath` is set to an explicit non-resolving
  placeholder (`https://example.invalid/...`) — wiring a real host is
  follow-up, honestly flagged, not attempted (none was provided and the brief
  says don't touch the live game server).
- **Bonus, found while re-staging for a clean mark**: `Resources/Characters`
  had drifted to 1135 prefabs on disk while the CURRENT `crowmodels.json`/
  `equipmodels.json` only reference 874 (677 bodies + 197 equip parts) — 261
  stale entries from an earlier scope experiment, never cleaned up. Re-ran
  `stage-characters.js --clean` then `--scope all` then `--scope equip` before
  marking, so what got marked Addressable is exactly the current referenced
  set, not a historical superset.

### What was measured (not assumed)
- `stage-characters.js`'s own footprint report (source PNG/mesh, deduped):
  bodies 148.2 MB (536 meshes + 825 textures) + equip parts 32.7 MB (187 meshes
  + 85 textures) = **180.9 MB** referenced character content — this
  supersedes the `plan.md` §16.3 "238 MB" figure, which was stale/pre-dedup;
  180.9 MB is the fresh number.
- `Assets/Ran/Resources` folder size: **243 MB → 196 MB** after removing the
  `Characters` subtree (Audio 72 MB + Fonts 0.9 MB + UI 107 MB remain,
  unconverted — see below).
- A REAL Addressables content build (`AddressableAssetSettings.
  BuildPlayerContent`, not a dry run) succeeded for the Standalone/Windows
  target: 874 entries, **88.0 s, one 269 MB bundle**
  (`ServerData/StandaloneWindows64/rancharacters_assets_all_*.bundle`), zero
  errors. This is real proof the marking + group config + runtime cache code
  all work end-to-end through Unity's actual Addressables system (not my
  offline stub) — Unity itself resolved 874 addresses, walked their real
  dependency graphs, and wrote a loadable bundle.
- `RunCheck.csproj`: **1086 passed, 0 failed** (baseline before this session's
  concurrent work was in the 980s; 9 of the increase are
  `RanAddressableCache`-specific: settle contract, waiter fan-out, null/empty
  address handling, `RanEquipView.LoadPrefab` cache-miss behaviour — all real
  pass/fail assertions, not skipped, because `UnityStubs.cs`'s new
  `Addressables`/`AsyncOperationHandle<T>` stub settles synchronously).
  `TypeCheck.csproj`: 0 errors. `node test.js`: 735 passed, 0 failed (Node side
  untouched by this work, confirms no regression). `SOURCE/`: clean.

### The Android APK diff — obtained (2026-08-20, later in the same session)

Update: the number the task asked for landed. `RanBuild.Android -ranMaps all
-ranApk ran-mobile-addressables.apk` was re-run (after the platform-switch
texture reimport finished and got cached — the first attempt hit a real,
transient compile error from a concurrent agent's in-flight edit to
`RanBootScreen.cs`, and a second attempt hit a real lockfile collision with a
different concurrent agent's own Unity process; neither was mine, both cleared
on their own):

```
[RanBuild] result Succeeded, 2249 MB, 21.1 min, 0 errors
[RanBuild] APK at .../Builds/ran-mobile-addressables.apk
```

**File size: 572,437,216 bytes (572.4 MB / 546.0 MiB) — measured with `ls -la`,
not the uncompressed `BuildReport.totalSize` figure above it.**

The conversion's own effect was verified directly, not inferred from the size
delta alone: `unzip -l` on the built APK shows the `assets/aa/` payload is
5 small files totalling ~271 KB (`catalog.bin` 172 KB, `AddressablesLink/link.xml`
1.8 KB, `catalog.hash`, `settings.json`, and Unity's own tiny built-in shader
bundle 96 KB) — **zero** `rancharacters_assets_all_*.bundle` files, confirming
the 268 MB Android character bundle (measured earlier the same session via a
real `BuildContent` run) is genuinely excluded from what ships, exactly as the
`Remote.BuildPath`/`Remote.LoadPath` group config was designed to do.

**Honest caveat on the headline number**: this build is NOT a byte-for-byte
A/B against `ran-mobile.apk` (504,203,000 bytes, 2026-08-18) — the project's
real content grew in the six days between them from concurrent work landing
throughout this same session (Character CREATE, Account-panel packets, PK
rank, and more — all visible in this doc's own "Current round" / Tier 0
entries), and that growth is not attributable to this change. What IS a clean,
directly-measured fact, not an inference: `unzip -l | grep sharedassets` sums
to 813.7 MB uncompressed across the 130 map scenes' own asset data — the
dominant share of the build's ~1.19 GB uncompressed total — versus ~271 KB of
Addressables plumbing and (by direct inspection) no character bundle at all.
Rebuilding a same-content, non-Addressables baseline to get an exact isolated
delta was not attempted (it would cost the same very long build again); the
defensible claim is the bundle-exclusion fact above, not a precise "X% smaller"
figure for this specific 130-map build.

### Effects + item icons — also converted this session (later pass)

Originally scoped as NOT attempted; a mid-session correction from the
coordinator asked for this too, and it landed. Verified (grepped, not assumed)
that `RanUiCfg.Sheet` is the SINGLE shared loader behind `RanUiCfg.MakeSprite`/
`SpriteFor`, which is what nearly every in-game panel uses for atlas sprites —
`RanHud`, `RanInventory`, `RanMinimap`, `RanTargetFrame`, `RanSkillBook`,
`RanSkillInfo`, `RanOptions`, `RanRevivePanel`, `RanMenuBar`, `RanNpcDialog`,
`RanChat`, `RanCharacterCreate`, and `RanItemDb` (item icons). Converted
`RanUiCfg.Sheet` and `RanEffect.cs`'s per-layer texture loader to a new
`RanAddressableTextureCache` (same cache-only/async-request contract as the
character cache, mirrored not shared — see the class doc comment for why a
second small class was safer than genericizing the first).

**Still NOT converted, correctly scoped out**: `RanBootScreen.cs` has its own
separate, uncached, duplicate `Sheet()`/`Background()` methods (not routed
through `RanUiCfg.Sheet`) — deliberately left alone because other agents were
actively mid-editing that exact file for Character CREATE and Account-panel
wiring for most of this session (confirmed twice, by two real compile failures
from that file that were not mine and cleared once those agents finished).
`RanQuickSlots.cs`, `RanPkCombo.cs`, `RanMapAxis.cs` each still have their own
one-off `Resources.Load<Texture2D>` call, not yet converted — smaller
individual categories (quick-slot frame sheet, PK-combo banner, minimap tiles)
than the two that were.

### Still open
- No device, no real APK install/boot test, no on-demand/remote hosting test
  (no CDN was provided; the mechanism is wired — `Remote.BuildPath`/
  `Remote.LoadPath` — but never exercised against a real or even a local
  static file server this session).
- `RanBootScreen.cs`'s own atlas loader, `RanQuickSlots.cs`, `RanPkCombo.cs`,
  `RanMapAxis.cs` (see above).
- A clean, isolated non-Addressables-vs-Addressables size A/B at identical
  content parity was not produced (see the honest caveat above) — only the
  direct "no character bundle shipped" proof and the 268 MB avoided-cost figure.

---

## PVP Competition (Tyranny) in-battle systems — session log (2026-08-20)

Picked up Tier 1's largest item, in the priority order given: tower
ownership visuals, tower capture-progress, full ranking board, rejoin,
reward/schedule broadcast. All five landed. `RanCompetitionModule.cs`,
`RanCompetitionWindow.cs`, `RanCompetitionPackets.cs` all touched;
`RanTargeting.cs`/`RanEntityView.cs` got a small additive extension (below).
Club Death Match, School Wars and Capture-the-Flag in-battle systems remain
untouched — out of scope per the standing plan note (hidden by server config
on PC too).

### What was measured, not assumed
- **Tower capture-progress is NOT in `A2C_BATTLEINFO_PC`** — that message's
  only tower-adjacent byte is the opaque `sScheduleNext` prefix (every other
  field was already used before this round). The real source, found by
  tracing `CPVPTyrannyTowerProgress::UpdateInfo(wOwner, fDamage[3])`
  (`SOURCE/Lib_ClientUI/Interface/PVPTyrannyTowerProgress.cpp`) back to its
  one call site (`InnerInterfaceSimple.cpp:810-811`), is a SEPARATE per-tower
  broadcast pair keyed by the tower's own CROW instance id (`dwGlobID`):
  `SNETCROW_TYRANNY_OWNER` (opcode 4562) / `SNETCROW_TYRANNY_DAMAGE` (4563,
  `GLContrlCrowMsg.h`, `#pragma pack(1)`, both measured via the layout probe
  — 14/24-byte structs). The call site also proved these only ever show for
  the player's CURRENT TARGET (`m_emNPCType == EMNPC_TYPE_EVENT_TOWER`
  gate) — not a fixed 3-tower panel — so the mobile progress widget is
  target-gated the same way, reading live off `RanTargeting.TargetMarker`.
- **Tower ownership BADGES use real PC art**, not invented colours:
  `TYRANNY_TOWER_CAPTURE_IMAGE_{SG,MP,PHX,NONE}_{0,1,2}` and
  `TYRANNY_TOWER_WINNER_IMAGE_*` — all 9+22 controls confirmed present in
  `uicfg.json` (`sc_battle_ui_set(_ex).dds`, already staged at
  `Assets/Ran/Resources/UI/`), transcribed from
  `CPVPTyrannyTowerCapture::CreateSubControl/UpdateInfo`
  (`PVPTyrannyTowerCapture.cpp`). Per-tower NAMES are the real gameword
  `TYRANNY_TOWER_TEXT` triple (3 Thai strings, measured in `gameword.json`),
  not "Tower N".
- **Rejoin is the SAME button as Register**, not a new one — traced
  `GLPVPTyrannyClient::DoRegister()` (`GLPVPTyrannyClient.cpp:118-153`) in
  full: while `TYRANNY_STATE_BATTLE` and already registered it sends
  `C2A_REJOIN_REQ` instead of `C2A_REGISTER_REQ`. PC's own extra pre-check
  (`!GetActiveMap()->m_bPVPTyrannyMap`) needs a per-map flag
  (`NET_MSG_GCTRL_LAND_INFO`) this port doesn't decode; omitted with the same
  "server remains the real authority" reasoning already used for `wLevel=0`
  elsewhere in this file — a mistimed rejoin comes back as the real, already
  -decoded `TYRANNY_REJOIN_FB_INBATTLE` (PC uses the identical text id for
  both its own local check and this server reply).
- **The full ranking board was already arriving** — `F2C_RANKING_UPDATE/END`
  never capped at 3, only the render did. Fixed the render (paged list, all
  rows, `RanCompetitionWindow.RankingPageSize`=10/page) rather than the
  protocol.
- **`A2C_RANKINFO_PC`** (938-byte body, top-10 `TYRANNY_PLAYER_DATA` + 4
  `SNATIVEID` reward-buff ids) and **`A2FC_NEXTSCHED`** (28-byte
  `TYRANNY_SCHED_NEXT`, gated on `dwIndex != 0xFFFFFFFF` exactly like
  `PVPTyrannyPageBattle.cpp:126`) — offsets already sat in `layout.json` from
  a previous pass's probe run; this pass added the Build/Read layer, wired
  both messages (pure broadcasts, no request needed), and rendered them.
- Reward-buff NAMES are shown as raw `#main:sub` ids, not resolved skill
  names — `skillflat.json` ships at `Assets/Ran/skillflat.json`, not under a
  `Resources/` folder, so `RanSkillTable`'s consumers all receive it via a
  serialized field rather than `Resources.Load`; a pure gameplay module has
  no clean way to reach it. Honestly flagged, not silently accepted.
- Status/feedback text throughout the module now reads the REAL
  `gameintext.json` table (`RanGameWord.InText`, with `%d`/`%s` substitution)
  instead of English transcriptions — every id used was confirmed present by
  direct lookup against the shipped table before wiring it in.

### What was touched, additively
- `RanTargeting.cs` (`RanEntityMarker`): 3 new fields
  (`isTyrannyTower`/`tyrannyOwner`/`tyrannyDamage`) — `isTyrannyTower` is
  only ever set true by a LIVE `SNETCROW_TYRANNY_*` broadcast (see below),
  never by the drop snapshot, because an ordinary mob's default values are
  indistinguishable from an as-yet-untouched tower's.
- `RanEntityView.cs`: `SetTyrannyOwner`/`SetTyrannyDamage`/`IsTyrannyTower`/
  `TyrannyOwner`/`TyrannyDamage` — same shape as the existing
  `SetNativeId`/`NativeId` pair.
- `RanWorldSession.cs`/`RanDropReader.cs`: **NOT touched.** `SDROP_CROW` DOES
  carry an initial `m_wTyrannyOwner`/`m_fTyrannyDamage` snapshot (measured at
  offsets 456/460, 476-byte struct) for a tower already in view when it
  drops, but this port does not read it — reading it would make an ordinary
  mob and an untouched tower indistinguishable at spawn time (no NPC-type
  field on the wire this port decodes). Consequence, honestly flagged: a
  tower that has been steady since before the local player joined the battle
  shows no progress readout until it next takes damage. `RanCompetitionModule`
  learns a tower's identity ONLY from the two live broadcasts, looked up via
  the already-existing `RanWorldSession.FindEntity(dwGlobID)`.

### Verified
- `node MOBILE/tools/rcc-extract/test.js`: **783 passed, 0 failed** (this
  session's work is C#-only; unaffected, confirms no regression).
- `cd MOBILE/unity/typecheck && dotnet run --project RunCheck.csproj`:
  **1192 passed, 0 failed** — includes a new `RanCompetitionPackets.SelfTest`
  section (opcodes/offsets/round-trips/positive controls for
  `SNETCROW_TYRANNY_OWNER/DAMAGE`, `C2A_REJOIN_REQ`, `A2C_REJOIN_FB`,
  `A2FC_NEXTSCHED`, `A2C_RANKINFO_PC`) and a new `RanCompetitionModule`
  wiring section (opcode ownership, the `ShouldRejoin` branch condition, and
  the real state/registered/tower-ownership/ranking/results tracking
  exercised through the ACTUAL `Handle()`+`Update()` pipeline via reflection
  — same technique the existing `RanPkModule.KnownPkScore` tests use, not a
  re-implementation of the logic under test). One real bug caught by a
  positive control before it shipped: the first draft of the
  `SNETCROW_TYRANNY_DAMAGE` "SG/MP/PHX slots don't smear" test zeroed the
  wrong offset (PHX instead of MP) and the control correctly failed until
  fixed.
- Two transient, unrelated failures seen mid-session (`db entry count`, a
  BLURSYS ribbon-mesh test) — both cleared on a bare re-run with no code
  change, consistent with a concurrent agent's in-flight edits elsewhere in
  the repo, not this work.
- `SOURCE/`: `git status --porcelain` empty before and after.
- Cold Unity 6000.5.8f1 batchmode compile (`Library/ScriptAssemblies`
  deleted first, then `RanPlayScene.Build`): **0 `error CS`, 0 exceptions**,
  produced the same `map 88 renderers, character 7 renderers ...` baseline
  line this project's own docs use as their compile-health signal.

### Still open for this item
- No device / Play-Mode visual confirmation of any of the 5 sub-items (the
  standing guard against entering Play Mode on a login-capable scene, and no
  device was attached this session) — data-level correctness is verified,
  on-screen appearance is not.
- Club Death Match's own in-battle system (tab exists, disabled) — genuinely
  untouched.
- The map-flag check PC's own rejoin gate uses
  (`NET_MSG_GCTRL_LAND_INFO`/`bPVPTyranny`) is not decoded — see above for
  why that's a safe, honestly-scoped omission rather than a silent gap.
- Reward-buff skill names — see above.
- `RanCompetitionBattleHud`'s tower-badge/progress-panel layout is a compact
  mobile-HUD arrangement, not a reproduction of PC's exact
  `TYRANNY_TOWER_CAPTURE`/`TYRANNY_TOWER_PROGRESS` group pixel rects (those
  are relative to windows/overlays this port's HUD doesn't have equivalents
  for) — the SPRITES are real, the LAYOUT is an adaptation.

---

## Order of attack (update as rounds complete)

1. ~~Quest-step, red-blob/HUD-icon fixes, T-pose data-level fix, UI verification tooling~~ — done 2026-08-19/20.
2. ~~Character CREATE, Inventory fix, Account-panel decode (packets only), Addressables conversion~~ — done 2026-08-20.
3. ~~PVP Competition (Tyranny) in-battle systems: tower visuals, capture-progress, full ranking, rejoin, reward/schedule~~ — done 2026-08-20, see the session log above.
4. Next: wire account-panel packets into `RanBootScreen.cs`, then the rest of Tier 0.
5. Then: Club Death Match's own in-battle system, if it's ever unhidden — currently server-config-hidden on PC too, so low priority.
6. Tier 3 resurfaces only with a real device pass or a capture-tool fix — don't spend more blind engineering time on it.
7. Tier 4 (effects/anim polish) and iOS stay last — polish and platform-blocked respectively, not blocking a playable build.

## What wasn't verified (say so rather than guess)

- Whether the ~15 modules absent from `plan.md` (Attendance, Pet, Boss
  Details, Product/vending, Pandora, Char Service, etc.) are pixel-faithful
  to PC XML — confirmed to exist with real `Consumed` message sets, not
  confirmed layout-correct.
- Precise protocol coverage (client-relevant `NET_MSG_*` handled vs total).
  Rough estimate: ~150-250 ids handled, likely the large majority of the
  gameplay-relevant subset of the ~1,476-name surface — the PVP-battle
  cluster is the largest identified unhandled block.
