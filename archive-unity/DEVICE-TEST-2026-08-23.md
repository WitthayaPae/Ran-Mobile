# Device test log — 2026-08-23 (LDPlayer emulator-5554, APK: hud-diag)

Methodology this session (user instruction, saved to memory as
`port-methodology-master-pc-source-first`): read the COMPLETE PC C++ mechanism for a
feature end-to-end FIRST, then make mobile match that mechanism — never
guess-and-patch symptoms. Every claim below has a measured artifact.

## Gameplay parity closure (LDPlayer, `pc-parity-final7.apk`)

This section supersedes the `final6` closure for gameplay behavior.

- Added exact live land-state decode for `SNETPC_LAND_INFO` (3027, measured
  57-byte body), including Tyranny/school-war/CTF flags and shop commission.
- Added the PC's two item-cooldown maps from lobby batch 2346 and live update
  4445. Auto-pot now scans all six potion-tray slots every 0.01 seconds, checks
  `ITEM_CURE`, the exact `EMITEM_DRUG` switch, and both cooldown maps; only HP
  is suppressed in Tyranny.
- Added Bright from CHAR_JOIN and update 3056. NPC selling now uses the PC PK
  price bands and subtracts live land commission.
- NPC classification now uses the extracted native-character resolver instead
  of a guessed `npc_` skin prefix. Competition state prefers live land flags.
- Offline regression: `1445 passed, 0 failed`. Typecheck: 0 errors (11
  pre-existing warnings). Full Unity 6000.5.8f1 Android build: 130 map scenes,
  0 errors. APK: `MOBILE/unity/RanMobile/Builds/pc-parity-final7.apk`;
  log: `MOBILE/unity/RanMobile/verify-build-pc-parity-final7-20260823.log`.
- Installed with LDPlayer-local ADB and entered live map 2 (`w_school_01`).
  CHAR_JOIN restored 2/60 skills and 3/6 drug actions; opcode 3027 arrived with
  length 57; discovery loaded 38 modules including `RanItemCooldownModule` and
  `RanLandStateModule`; the local `o_w` controller mounted; 19 entities rendered;
  continuous live packet and ping traffic remained healthy.
- Live proof: `MOBILE/unity/RanMobile/ran-final7-final-world.png`. A first delayed
  join was reset after the character screen sat idle during LDPlayer coordinate
  diagnosis; a fresh immediate join succeeded.
- Unity Personal was valid. The earlier sandboxed batch failure was an IPC/
  privilege-channel issue, not a missing license; the licensed editor completed
  the full build outside that isolation boundary.

## Final PC-parity closure (LDPlayer, `pc-parity-final6.apk`)

This section supersedes the older pending notes later in this same-day log.

- Built `MOBILE/unity/RanMobile/Builds/pc-parity-final6.apk` with Unity
  `6000.5.8f1`: build succeeded, 3,197 MB, 0 errors. Build transcript:
  `MOBILE/unity/RanMobile/verify-build-pc-parity-final6-20260823.log`.
- Installed through the LDPlayer-local ADB at
  `C:\LDPlayer\LDPlayer14\adb.exe`, target `emulator-5554`.
- Logged into the live account `xx11` with the owner-provided password and
  entered the world successfully. The password is intentionally not recorded
  in this document.
- PC `SNETLOBBY_CHARJOIN` quick bars decode live: active page 0, 2/60 saved
  skills, 3/6 saved drug actions. The two saved skills restore into slots 0 and
  1 and both taps reach the cast path. With no target in range the combat layer
  correctly declines the cast instead of silently losing the button event.
- Quick-bar page controls are live and scoped to the quick bar (not hidden
  windows with duplicate `PageUp`/`PageDown` names): device log proves `page
  2/4`. The ATK button logs `attack requested`.
- Empty quick slots render as empty dark cells; the incorrect white square
  placeholders are gone.
- Saved skill icons now visibly render from the real PC `skill_magic` atlas.
  `final5` exposed that the atlas existed under packaged `Resources/UI` but was
  absent from the Android Addressables catalog; `final6` adds the narrow
  packaged-atlas fallback. Live proof shows the red and blue icons in slots 1
  and 2, and both remain functional.
- Live worn appearance resolves and mounts: attach slot 14 item 929/28
  (`december_guardian`), skinned slot 3 item 981/219
  (`s_w_cos_swimming2012`), and slot 5 item 1/9 (`s_w_jipang`). Missing gear
  prefabs were built and registered as Addressables (1,135 -> 1,137 entries).
- The runtime skill table is now the byte-identical PC-derived `skillflat.json`;
  the skill book and quick bar bind to the same table after world join.
- The fake permanent `Bonus Time` HUD state was removed. Inventory uses the
  real item feed, icons, and quantities. Saved drug actions restore only when
  the referenced item is still present in carried inventory; the live account's
  three server entries did not produce a matching carried-item icon, so no
  consumable was fired merely for a visual test.
- Final live proof: `MOBILE/unity/RanMobile/verify-final6-proof-20260823.png`.
  Earlier inventory proof:
  `MOBILE/unity/RanMobile/verify-livefix-inventory-20260823.png`.
- Final offline regression after all source edits: `1425 passed, 0 failed`.
  The final APK compile also proves the real Unity/Android source path, not only
  the typecheck stubs.

Verification scope is explicit: mobile was checked live against the real server
and PC packet/data/source behavior. A simultaneous visual capture of the PC
client was not obtained because no PC game window was exposed to the automation
desktop; therefore this is not presented as a pixel-for-pixel two-window signoff.

## Fixed and VERIFIED this session

1. **Movement architecture = Actor::Update exactly**
   PC (`Lib_Engine/NaviMesh/actor.cpp:292-398`) steps a clamped constant velocity
   toward the next waypoint — no easing. Mobile's `SmoothDamp` spring was this
   port's own invention and the root of speed/flicker/imprecise-stop complaints.
   `RanCharacterDriver.Update` rewritten to the exact clamp
   (`maxSpeed * dt`); lead distance reverted to one frame.
   **Measured**: speed now flat `40.00` u/s = the Shaman's exact RunVelo from
   charclasses.json (was ~37.35 through the spring era, ~5.2 before that).
   Clean deterministic stops (`2.60 → 0.02 → 0.00`), smooth Y through slopes.

2. **Per-character animator controllers (PC's SELECTANI scope)**
   PC: each `.chf` loads only ITS OWN anim containers; `SELECTANI` never sees
   other characters' clips. Mobile baked one controller per SKELETON — union of
   every character sharing it — which put `mob_ky_sangongdock` (a mob spell,
   subtype 0, 3.37s) into player o_w's Attack pool where longest-clip won.
   Now: `animchars.json` (per-.chf clip lists, 1095 chars) → 1053 per-character
   controllers, `RanChfBuilder` prefers them; clip ties resolve by declared
   .chf list order (PC container load order, first-wins) — this also fixed
   Guard picking `a_w_board_runfunny` over `a_w_00_stay`.
   **Measured** (`RanAnimatorBuilder.Inspect -ranMesh o_w`): Guard=a_w_00_stay,
   Walk=a_w_00_walk, Run=a_w_00_run, Attack=a_w_00_att01, Shock=a_w_00_strock,
   Die=a_w_die, all Cast_4_* = player a_w_* clips. On-device logcat confirms
   `controller=o_w` live.

3. **Hit-effect timing = AttackProc's strike frames**
   PC fires the hit effect when the attack timer crosses the clip's own strike
   frame (`GLCharacter.cpp:5255-5284`), never at cast. Staged per-clip strike
   seconds into animflat.json (`st`, 2118 clips; ticks/4800), added
   `RanStrikeTable` + `RanCharacterDriver.QueueStrike/PollStrike` (same
   timer-vs-frame check), `RanCombat` routes the hit effect through it. Cast
   (SELFZONE) effect stays instant, matching PC.
   **Verified at data level**; on-device visual pending a reachable target.

4. **Remote players move like PC** (earlier this same day): `EntityGoto` now
   reads dwActState's run bit + real class Walk/RunVelo (was flat 130 u/s,
   no gait state) — the same fix `EntityMove` already had for mobs.

5. **GUI restyle toward official-mobile reference (user screenshot)**
   ATK = gold-ring dark disc with ⚔; skill slots dark translucent; joystick =
   dark directional pad with ▲▼◀▶ arrows + dark knob. Color/text-only where
   the RanQuickSlots crash-bisect note forbids new sprite refs.
   **Verified**: screenshot on-device, all elements render; walk regression
   clean on the same APK (40.00 u/s log above).

## Added later the same day: ResolveMotionOnMesh port (level-jump fix)

User's first real PC-vs-mobile side-by-side screenshot showed the mobile character
standing ON the tower platform while the server (PC view) had her on the LOW
ground — she climbed a ledge PC treats as solid. Root cause: joystick steps were
validated with GLOBAL 2D XZ cell lookups, which cannot distinguish STACKED
surfaces; PC resolves every step edge-by-edge from a TRACKED current cell
(`NavigationMesh::ResolveMotionOnMesh`, NavigationMesh.cpp:273-369) with
wall-slide (0.98 friction), making non-adjacent levels unreachable by
construction. Ported faithfully as `RanNavmeshAsset.ResolveMotion` +
`FindCellNearestY` seeding + tracked cell in `RanTouchInput`
(drops on `SeedDestination` teleports).

First on-device run exposed a port flaw: height was mapped even when the end
point sat OUTSIDE the final cell — barycentric weights EXTRAPOLATE there, and
the per-frame residue compounded into a monotonic dive (measured: Y=-44 on
ground whose only cell sits at +10.02). Fixed by clamping the end point into
the ending cell before reading height (PC's bFirstTimeSame skip + tiny steps
mask the same hazard).

**Verified on-device after the clamp fix**:
- Slope descent smooth to the real -9.94 level, then EXACT clamp at the x=-60.00
  wall boundary (same edge earlier navmesh queries mapped) — slide, no dive.
- Standing at the plaza ledge base (pit floor -9.94, plaza +10.02 above):
  pushing straight into the ledge face = ZERO movement across 4s of held input.
  The level-jump from the user's screenshot is now physically impossible.

## Added later again: stuck-in-place fix + fuzz harness

User got stuck live (position on/near a cell boundary at (-205,-1978), zero
movement any direction) and asked why they keep finding bugs testing that I
don't. Honest answer: scripted single-path tests vs free roaming. Response: a
fuzz harness (`scratchpad/fuzz_resolve.js` + `fuzz_lead.js`) that mirrors the
C# ResolveMotion 1:1 against the real .navmesh binary and simulates hundreds
of thousands of movement steps offline.

Root cause (found by re-reading the PC loop, NOT the first theory): PC's
integer-truncation early-out (`int(A.x)==int(B.x)…`, NavigationMesh.cpp:294)
skips resolution for sub-unit motions. PC feeds start->FAR-WAYPOINT segments so
it never matters; the mobile joystick fed 0.67-unit one-frame steps, so the
walk almost never ran, the final clamp dragged the target back into the
CURRENT cell every frame, and cell crossings became impossible — stuck sliding
along borders. Fix: joystick lead = walkSpeed*0.25 (~10 units, waypoint-like);
ResolveMotion wall-clamps it and the driver still caps real motion per frame.
Two defensive nudges (on-edge start, on-edge rest) added the same pass.

**Measured**: fuzz p10 travel 187/200 units vs old 136/200 (median 199 vs 191);
on-device 8-direction sweep at the user's exact stuck area: 6 free directions
at full 36-40 u/s, 2 correctly blocked by a REAL 10-unit ledge (verified in
navmesh data), and walking back out of the wall contact resumes at instant
40.00 — the reported "can't walk back out" is gone.

## Added: observer-delay fix (PC sees mobile move late / rest in wrong place)

User side-by-side: PC observers saw the mobile character only in short delayed
hops and standing wrong after stops; mobile saw PC players fine. Mechanism: the
GOTO's target IS what the server's own simulation walks (Actor::GotoLocation) —
PC sends the far click point once and the server walks it all, which is why PC
players look smooth. Mobile sent a ~10-unit-ahead target only every 55 travelled
units: the server walked 10 then idled 45, and the final tail was never sent.

Fix (RanTouchInput + RanWorldSession sender): (1) network destination is a
~55-unit wall-clamped far lead along the heading, re-sent every ~35 travelled
units so the server simulation never runs dry; (2) vCurPos is the REAL current
position each send (SetPosition before MoveTo), like PC's packet; (3) joystick
release sends one immediate stop-flush GOTO at the true final position.

**Verified (packet side, logcat)**: MoveTo every ~0.9s during a 4s run at 40u/s
(= ~35-unit cadence) plus one flush at release carrying the exact stop point.
Observer-side smoothness needs the user's own PC-client eyes.

Follow-up ("still a bit delay"): PC's gate is destination-changed->send-NOW —
the first packet of every walk was ~0.9s late here, so the server trailed that
fixed gap all walk. Added WALK-START immediate send + STEERING immediate send
(>25 deg heading change = a new destination in PC terms). First cut of the
steering trigger re-fired EVERY frame through turns (the follow camera yaws,
rotating the camera-relative joystick frame): 8 sends in 0.7s — the speedhack-
detector packet pattern. Fixed with a 0.3s time floor on the steering trigger.
**Verified**: 8 sends over a 4s curved walk+release — instant first packet,
0.31s minimum gaps through the turn, ~0.9s pacing straight, stop flush at
release. Max ~3/s, typical ~1/s.

## Skins: full piece-prefab coverage (`hud-skins.apk`)

User: "focus for all the skins now make it work." Census against
equippieces.json found the real gap: of 1,063 distinct skinned-gear meshes the
item tables reference, only THREE had prefabs (s_m_dw_wlb, s_w_cos_swimming2012,
s_w_jipang) — every other equip was a silent "missing piece prefab"
skip-and-warn. Attach (.abf) side was already ~fine (502/506 via charflat).

Fix: `stage-piece-meshes.js` copied the 124 source-only .rmesh into
Assets/Ran/meshes (deterministic md5 metas, RanMeshImporter template);
`RanPieceBuilder.BuildAll` (new) mass-built prefabs for all 1,063 —
**1,060 built, 0 failed**; `MarkCharacterAssets` re-marked: 1,137 -> 2,197
Addressables. APK `hud-skins` (3,746 MB, +550 MB of gear).

**Live verified (xx11, map 2)**: logcat shows pieces from the NEW set mounting
— `s_m_hb` body/leg/foot (slots 1/2/4) and `s_m_gongyong` (slots 5/6) on remote
players, plus her own swimming2012/jipang/december_guardian as before; ZERO
"missing piece prefab" warnings. Screenshot proof: test02 wears the full school
uniform instead of the underwear-only body of every earlier capture.

Known residue: 4 attach stems referenced by items lack any charflat entry
(9th_arwens_ear, belt_300lv_wing, belt_april_butterfly_g,
belt_cos_grand_rwct_wings + a few alias-only: gs9y cloaks, s_cos_vip_stick,
female_ghost) — rare cosmetics, no .chf/.abf record to build from.

## Pending / not yet verified

- **Attack swing on-device**: ATK button tap did not register from adb input at
  the expected coords (no `[RanQuickSlots] attack requested` log); controller
  wiring is data-verified. Needs a session with a reachable mob/target.
- **Skinjobject (equip visuals)**: `RanUiCapture -ranOnly equip_after` ran but
  wrote no fresh artifact (old shot from 08-22 remains); test character also
  has no equipped items. Nothing this session touched attach code.
- **Strike-timed hit effect visually** on-device (see 3).
- **GUI phase 2**: top-left portrait+level chip, top-right icon strip with
  minimap below, quest tracker panel — deferred to protect baked-scene
  stability (documented crash fragility); joystick AUTO button.
- **Two-client side-by-side vs real PC** for final "100% like PC" sign-off —
  cannot drive the PC client synthetically (DirectInput ignores injected input).

## Build/pipeline facts (also in memory)

- `RanPipeline.RunAll` must run before Android build when any animator/clip
  logic changes — RanBuild.Android alone never rebuilds controllers.
- 22 of 1075 per-char controller builds failed (chars whose skeleton clip
  folder lacks their clips) — fall back to skeleton controller; not user-facing
  for the player bodies. Listed in 40_pipeline_subtype.log.
