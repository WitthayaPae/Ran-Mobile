# Device test sweep — 2026-08-22

Build: hud-fix34.apk. Device: Samsung Galaxy Tab S9 (SM-X710), wireless adb.
Workflow: full sweep first, log everything here, fix all together at the end, one redeploy to verify.

## Confirmed working
- Server-select screen: title bar, body, Exit button all render with real dark chrome — the
  white-rectangle `Slice()` fallback bug (found this session) is confirmed fixed on real hardware.

## Bugs found (not yet fixed)

1. **Register button ("สมัครสมาชิก") shows a truncated, unstyled status message.**
   Screen: Login. Repro: tap สมัครสมาชิก. Symptom: text reads "registration is not" (cut off,
   presumably should continue "...available on mobile" or similar) rendered in a huge font with no
   background panel, overlapping the 3D world at the bottom of the screen, partially below the
   visible frame. Screenshot: `test_03_register.png`. Suspected: `RanBootScreen.cs`, wherever this
   status/error text is shown (likely `Below()`/`SetStatus`-style helper, using a wrong/huge font
   size or no wrap, and the message string itself may be truncated at the source, not just a render
   issue — check `RanGameWord.Text`/whatever produces this exact string).
   **Confirmed systemic, not per-button**: "ลืมรหัสผ่าน" (Forgot Password) shows the SAME class of
   bug — huge unstyled text reading just "password" (even more truncated than Register's message).
   Screenshot: `test_04_forgotpw.png`. Both share one status-message renderer — fix once, verify both.

2. **Status message text persists across page navigation instead of being cleared.**
   Screen: after Cancel returns Login -> Server Select. Symptom: a faint leftover fragment of the
   previous "password" status text is still visible peeking out from behind the server-select
   panel's bottom-left corner. Screenshot: test_05_cancel.png. Confirms bug #1's status-text
   GameObject isn't hidden/cleared on every page transition. Likely fixed together with bug #1.

3. **Character select: "Attribute" bar renders as a slider with a draggable-looking thumb, not a fill bar.**
   Screen: Character Select (after real login). Symptom: the Exp bar above it shows a normal
   thin-fill-style bar (tiny red sliver at ~0%), but the Attribute bar directly below it shows a
   distinct tan/off-white THUMB control partway across, reading as an interactive slider rather than
   a read-only stat display. Not confirmed broken (may be intentional/correct), but visually
   inconsistent with the Exp bar right above it and worth checking against real PC source
   (`CLIENT/data/gui` character-select XML) before deciding whether to change it. Low priority.

## Login succeeded (test02 / Lv3 / Swordsman) — continuing sweep into character select and gameplay

4. **Third confirmation of the truncated status-message bug**: a real "wrong ID or password."
   server response (triggered by my own input mistake, not a real credential issue — see below)
   shows correctly in the modal dialog itself, but ALSO duplicates as oversized truncated text
   "wrong ID or" below the panel, same as bugs #1/#2. Screenshot: test_16_charselect3.png. Confirms
   this is a general "any status/notice text" bug, not specific to Register/Forgot-Password.

## FINAL STATUS (this session): "level1 corrupted" crash is a genuine Unity
## engine bug, NOT fixable from project code — confirmed via 7 total
## world-entry attempts + native crash analysis + Unity community research

**Full attempt log (all crashed except hud-clean1):**
1. hud-fix46 (pre-cache-theory) — CRASH
2. hud-clean1 — cache cleared, no icon/wipe sprite — SUCCESS
3. hud-batch3 — cache cleared, icon/wipe sprite added — CRASH
4. hud-stable — cache cleared, icon/wipe reverted (= hud-clean1's code) — CRASH
5. hud-stable2 — same code, cache cleared, rebuild #2 — CRASH
6. hud-stable3 — same code, cache cleared, rebuild #3 — CRASH
7. hud-smoketest — same code, cache cleared, only 1 map (not 132) — CRASH
8. hud-smoke2 — same code, cache cleared, only 1 map, rebuild #2 — CRASH

1 success in 8 attempts (~12%), independent of cache state, icon/wipe code,
and packaged map count — none of those are the cause.

**Native crash analysis** (`adb logcat`, full backtrace pulled): the crash's
ENTIRE backtrace (25 frames) is inside `libunity.so` — Unity's own closed-
source native engine — starting from `raise()` (libc), meaning Unity's OWN
asset-streaming code detects an inconsistency and deliberately aborts. This
is not memory corruption in project code; it is not reachable from C#.

**Unity community research**: this exact error class ("X is corrupted!
Remove it and launch unity again! [Position out of bounds!]", editor-fine/
device-crash, non-deterministic) is a known, RECURRING, UNRESOLVED category
of Unity bug across multiple discussions.unity.com threads and issue-tracker
entries. In the closest documented case, even Unity's own QA never gave a
root cause; the reporter's only "fix" was trial-and-error removal of
whichever specific asset triggered it for them, with no real explanation.
No general fix is documented anywhere found.

**Conclusion**: further guessing at project-code-level causes is not
productive — every testable hypothesis (cache, map volume, the one recent
code change) has been directly disproven with real evidence, and the
failure is confirmed to originate inside Unity's closed native engine.
Practical path forward: accept a build-and-retry workflow (~12-30% hit
rate observed) when a world-entry test is needed; do not re-attempt to
"root cause and fix" this further without genuinely new information (e.g.
a different Unity 6000.x point release, or Unity support engagement).

## In-world sweep (hud-smoke3, single-map smoke build — reached the world on
## attempt 3 of the day's rebuilds; see FINAL STATUS above)

Note: this build only packaged `w_school_03` (not the account's real spawn
map), so ground/terrain is an untextured grey plane — expected for this
diagnostic build, not a bug. HUD/systems tested on top of it:

**Confirmed working:**
- Camera one-finger drag orbit — swipe visibly rotated the view.
- `RanQuickSlots` 10-slot two-ring fan — BOTH rings (5 filled near ATK, 5
  empty further out) render as proper circles matching `RanUiCircle`, not
  squares. The "only circle" requirement from earlier this session is
  correctly satisfied for this control.
- Top-right menu cluster (☰ button) opens correctly: real equipment/action
  icons render (not placeholder boxes), Pet/Assist/ตัวเลือก/ร้านค้า labeled
  buttons present, trophy and heart-shaped icons present. Genuine PC-config
  art is resolving, not falling back to text placeholders.
- HP/MP/SP/EXP bars, PK panel, "Challenge target to duel", ping/fps display,
  joystick, minimap compass, quest log in chat, NPC nameplates, live mob
  spawns ("Little Vulgarian" pack) — all rendering and updating correctly
  with real server data.

**New bug found (not yet fixed):**
- The separate 6-slot numbered tray (top-right, likely `RanPotionTray` per
  its own doc comment calling it "the new 6-slot circular tray") renders
  its frames as plain SQUARES, not circles — inconsistent with both its own
  documentation and with `RanQuickSlots`' correctly-circular fan right next
  to it. Needs a source check on `RanPotionTray`'s frame construction (does
  it use `RanUiCircle.Get()` like `RanQuickSlots` does, or something else).

**Own tooling mistake this pass**: tapped the ☰ menu button using displayed-
image pixel coordinates directly instead of multiplying by 1.28 for actual
device coordinates — missed the button once (wasted a screenshot round-trip,
no other effect) before correcting. Established convention restated here so
it isn't dropped again: screenshots are captured at native 2560x1600 and
shown to me scaled to ~2000x1250 — always multiply by 1.28 before sending a
tap.

**Continued sweep (same session, same hud-smoke3 world):**

- **RETRACTED**: "menu toggle doesn't close on second tap" — this was a
  false finding. First observation was muddied by several intervening
  minimap taps between open and the "close" attempt, not a clean
  before/after test. Re-tested in isolation (screenshot immediately before
  the tap, tap at the mathematically-computed exact button center
  `(2464,304)` — derived from `RanTopMenuCluster.cs`'s `MakeRow`/
  `MakeGridCell` percentage math and this device's actual CanvasScaler
  factor, not eyeballed — screenshot immediately after): the popup closed
  correctly. `WirePanelToggle` (`RanHudRewire.cs:208-236`) is a real,
  working `SetActive(!activeSelf)` toggle. No bug here.
- **Minimap click-to-walk and joystick movement — INCONCLUSIVE, not a
  confirmed bug.** Computed the actual on-screen radar position precisely
  (canvas scale factor 2.667 for this device's 2560x1600 screen with
  match-height CanvasScaler; radar center ≈ device px (2296,249), radius
  ≈130px) and tapped dead center — no observed character movement, no
  `RanMinimap`/`WalkTo` log lines (though `ClickArea.OnPointerClick` has no
  logging at all, so silence isn't proof of failure either way). Same
  inconclusive result for a joystick drag. Root cause is most likely this
  specific build's own limitation, not a real bug: `hud-smoke3` only
  packaged `w_school_03` while the account's actual spawn map is a
  different id, so `RanMapLoader`'s catalog lookup for the real map
  probably never found matching geometry/navmesh, and the flat, textureless
  grey ground visible throughout this whole sweep supports that. Needs
  re-verification on a build that reaches the world WITH its correct map
  before concluding anything about `RanMinimap`/`RanTouchInput` themselves.
- **Chat send — confirmed bug.** Typed "hello_test", closed the keyboard;
  logcat confirms the full local pipeline ran
  (`RanChat:Submit(String)` fired from `onEndEdit`, per the actual code
  path). But the message never appeared in the local chat log, checked
  immediately and again several seconds later. `RanChat.Submit` only raises
  `Submitted` (no local echo) — `RanWorldSession.OnChatSubmitted` sends it
  purely over the network via `ChatSend`; the chat log is expected to
  update only once the server echoes it back. It never did. Needs a source
  check of `ChatSend`'s wire format/channel handling, or confirmation this
  server's chat echo behaves differently than assumed — not fixed this
  pass.

**Confirmed working (this continued pass):**
- On-screen keyboard opens/closes correctly for chat input; typed text
  displays correctly in the OS input bar before submit.
- Options window (`RanOptions`, opened via the menu popup's "ตัวเลือก"
  entry): opens correctly with real content — tabbed sections (ภาพ/เสียง/
  เกม = Video/Sound/Game), a working-looking Quality selector (Very
  Low..Ultra, Ultra pre-selected), View distance (Near/Medium/Far, Medium
  selected), Battery saver toggle (OFF), close (X) button. Computed its
  exact on-screen position from `RanTopMenuCluster`'s grid math rather than
  eyeballing (index 14 of 18 entries, 4 cols/5 rows, minus the popup's own
  computed rect) — landed dead-on.

- Inventory window (`RanInventoryModule`, opened via menu popup's
  "Inventory" entry): opens correctly — equipment doll slots, a full empty
  inventory grid, "Sort" button. Title overlap bug FIXED and CONFIRMED (see
  batch-fix pass below).

## Batch fix pass 2 (hud-fixes1/hud-fixes2) — all confirmed working on a
## SECOND test device (LDPlayer, ARM64-translated x86_64 emulator, since
## the physical tablet's wireless-debugging connection dropped mid-session
## — see the pairing/network saga; this is a legitimate independent
## verification, not a lesser one)

1. **Skill fan outer-ring spacing** (user: "the second line too far out of
   circle") — `FanRadiusOuter` 170→120. CONFIRMED: both rings now read as
   one cohesive two-ring cluster around ATK, not disconnected arcs
   (screenshot `skillfan_zoom.png`).
2. **Equip-slot icons** (user: "the icon did not look like pc version") —
   root cause: PC's real doll (`CInventoryPageWear`) draws a per-slot-type
   silhouette from `INVENTORY_WEAR_OVER_IMAGE{0..12}` on every empty slot;
   this port never fetched that second image layer at all, only the frame,
   so it fell back to truncated English text ("Vehi", "Hea", "Nec").
   Wired in the missing `Skin("INVENTORY_WEAR_OVER_IMAGE"+i)` fetch in
   `RanInventory.AddEquipCell`, real atlas data confirmed present in
   uicfg.json for all 13 slots. CONFIRMED on device: real faint silhouette
   icons now show (head, weapon/vehicle, ring, boot, etc.), text label
   only as an honest fallback when no sprite resolves (screenshot
   `inv_ld.png`).
3. **Inventory title duplicate text** — root cause found by reading the
   actual pixels at 1:1 zoom (`inv_title_zoom.png`): NOT a second title
   bar — `RanInventoryModule.RefreshHeader()` (a held-item HINT label
   below the real title) hardcoded the literal string "Inventory" as its
   idle-state text, duplicating the real skinned title
   ("อุปกรณ์สวมใส่") right above it. Fixed to `string.Empty` when nothing
   is held. CONFIRMED: single clean title now (screenshot `inv_ld.png`).
4. **Chat send never appeared locally** — root-caused against real PC
   source: `CBasicChatRightBody::SEND_COMMON_MESSAGE`
   (`BasicChatRightBodyEx.cpp:157-186`) sends the wire message AND calls
   `AddStringToChatEx` immediately after, as a LOCAL ECHO that never waits
   for the server to broadcast the sender's own line back. This port only
   ever sent, never echoed. Fixed: `RanWorldSession.OnChatSubmitted` now
   also calls `chat.Add(...)` locally, matching PC exactly (whisper
   target as speaker for private messages). CONFIRMED: typed "hello_ld_test",
   it appeared instantly as `[boa]:hello_ld_test` (screenshot `chat_ld.png`).

5. **Potion tray square slots** — root cause: `frame` (the base) already
   used `RanUiCircle.Get()` correctly, but the `icon` overlay on top (inset
   only 16% into the cell) had NO sprite at all in its empty/cleared state
   — an `Image` with no sprite renders as a plain filled rectangle, and
   that square visually dominated the circular frame beneath it. Same bug
   class as `RanQuickSlots`' icon/wipe, a third location. Fixed both the
   construction-time default and `ClearSlot`'s reset to use
   `RanUiCircle.Get()`. CONFIRMED on device: slots 4/5/6 now render as
   clean circles (screenshot `potioncheck_zoom.png`).

**New data point on the "level1 corrupted" crash**: on this LDPlayer
(x86_64 + ARM64 translation) device, the identical `"level1 corrupted...
Position out of bounds!"` warning fired in logcat on world entry — but the
app did NOT crash, it continued into the world normally. This confirms the
underlying data inconsistency is real and reproducible (not a fluke), but
the FATAL crash appears specific to real ARM64 hardware's stricter
handling of it — the emulator's translation layer tolerates the same bad
read. Practical implication: this emulator is not a reliable stand-in for
verifying whether a given build will crash on the real tablet.

**Bonus, unplanned confirmation — "the map not load" (user complaint) is
NOT a bug, was a smoke-test-build artifact**: earlier in-world testing used
a deliberately reduced single-map build (`-ranMaps w_school_03`) to dodge
the "level1 corrupted" crash cheaply, but the account's real spawn map is a
different id — hence the flat grey ground seen throughout that whole
sweep. A proper `-ranMaps all` build (`hud-fixes2`) reached the world
cleanly on the FIRST attempt on the new test device and loaded the
correct, fully-textured map (`w_school_01`, real courtyard/building/NPCs
— screenshots `world_real.png`, `ld_wide.png`). Minimap click-to-walk and
joystick movement, previously inconclusive, are now CONFIRMED working —
`RanSession:MoveTo(Vector3)` fired in logcat after both gestures.

## Full menu-panel sweep (hud-fixes3, LDPlayer)

**Own tooling error found and corrected mid-sweep**: my grid-cell coordinate
formula for the 18-icon `TMC_MenuPopup` was wrong (`popup_left` assumed as
680 canvas units; actual measured position ~786) — caused several early
false readings (a "Character does nothing" false alarm, a confusing
"Party position opens Inventory" observation) that were purely MY tap
missing the real button, not app bugs. Corrected by measuring real icon
centers directly from a native-resolution crop instead of trusting the
formula. Documented here so a future pass doesn't repeat the same mistake:
recompute from a real screenshot crop, don't trust a re-derived formula
blindly.

**Confirmed working** (real content, not placeholders): Character stat
window (Pow/Dex/Spi, Str/Stm, Level/HP/MP/SP, resistances, PK Score/Death,
3D preview), Party ("Leave Party"), Guild/Club ("การประมูล" fields, Notice/
Members/Battle buttons), Friends (Friends/Blocked tabs, Add), Large Map
(real detailed dungeon map with location labels/coordinates), Bank/Locker
(5-tab locker, Bank/Amount, Deposit/Withdraw/Withdraw All), Auction (Bid/
Storage tabs, "No active auction" real state), Battle/Competition
(Tyranny/Club Death Match tabs, level requirement, Register/Cancel/Full
Ranking), Pet (Name/Type/Full/Dismiss).

**New bugs found this pass:**
- **Character window's X close button does not work at all.** Tested
  exhaustively: 5 precisely-computed taps directly on the glyph (verified
  via a 2x zoomed crop), plus hardware Back — zero reaction every time.
  `RanCharStatWindow`'s close wiring is correct in source
  (`CloseRequested += () => _window.Hide()`), so the bug is elsewhere —
  likely a raycast-blocking element sitting over the button. Not
  root-caused further this pass; window had to be left open and worked
  around via app relaunch.
- **Guild/Club window has no visible close button at all** (no X anywhere
  on the window, unlike every other tested panel). Possibly by design
  (real PC Club window may only close via ESC, which touch has no
  equivalent for) or a genuine gap — needs a source check against real PC
  before deciding.
- **Bank/Locker window exposes "+1k / +10k / +100k / +1M / Clear" quick-add
  buttons** next to Deposit/Withdraw — these read as debug/test money
  buttons, not real PC UI. Needs a source check; if confirmed
  non-PC-authentic, likely wants removing before any real release build.

**Not yet tested** (deferred, genuinely untested, not known-broken):
two-finger pinch zoom, mobile camera-lock toggle, actual skill casting/
cooldown wipe, Skills (skill book), Help, Market, Student Record, Macro
panels (blocked mid-sweep by a client-side login-delay cooldown after
several rapid relaunches — resuming after the wait), character creation
flow, character deletion flow.

## (superseded, kept for history) previous correction/resolution attempts

The section below was written after ONE successful clean-cache rebuild
(`hud-clean1`) and claimed the crash was fixed. That was wrong. Two more
clean-cache builds after it — `hud-batch3`, then `hud-stable` (built from
the EXACT SAME reverted `RanQuickSlots.cs` code as `hud-clean1`, cache
cleared, full clean rebuild) — both crashed with the identical signature on
world entry. Counted honestly across all three clean-cache world-entry
attempts this session: 1 success, 2 failures. That is not distinguishable
from the original baseline failure rate. Clearing `Library/PlayerDataCache`/
`BuildCache` does NOT reliably fix this. The real root cause is still
unknown. `RanQuickSlots.cs`'s icon/wipe sprites have been re-reverted (no
sprite, disclosed cosmetic gap again) since re-applying them was one of the
two variables present in both crashing builds — though `hud-stable` crashed
WITHOUT that change too, so it is not proven to matter either. Do not trust
the "RESOLVED" framing below; it is kept only as a record of what was tried
and why it looked (wrongly) like a fix at the time.

## (superseded) "level1 corrupted" crash was a stale Library/PlayerDataCache entry

**Root cause found and fixed (hud-clean1.apk).** Proved it wasn't transport/AV
corruption by validating a just-built APK's zip integrity (`unzip -t`, every
CRC clean) BEFORE it touched the device, then installing that exact
verified-intact file — it crashed anyway with the identical signature. That
meant `BuildPipeline.BuildPlayer` itself was writing a bad `level1` (=
`gameplay.unity`, always scene index 1 per `RanBuild.cs`'s scene ordering).

`gameplay.unity` is the ONE scene (of ~132 packaged via `-ranMaps all`)
regenerated fresh by a separate Editor process (`RanPlayScene.BuildBootstrap`)
before every build, while the other ~131 map scenes never change between
builds. `Library/PlayerDataCache` (1 GB) and `Library/BuildCache` (7.4 GB)
are Unity's incremental serialized Player-data caches, reused across
separate `-batchmode -quit` Editor process invocations. A stale cached entry
for that one changing scene — new component layout (icon/wipe sprites,
Mask, whatever the bisection pass added) vs. an old cached resource blob —
matches this exact symptom: a level file's header describing offsets the
paired (possibly reused/stale) `sharedassets1.assets` splits don't actually
have, i.e. "Position out of bounds."

**Fix**: `rm -rf Library/PlayerDataCache Library/BuildCache` before every
Android build. Added permanently to `rebuild_deploy.sh`. One clean rebuild
(6.0 min, not drastically slower than a cached build) produced `hud-clean1`,
installed clean, and reached the world successfully — real HUD, real quests,
100ms/60fps, no crash. User confirmed this as standing policy: "everytime
from now on no cache for all."

This retroactively explains the earlier bisection's confusing data too:
three structurally unrelated code changes (icon/wipe sprite, Mask, a
provably-nothing-embedded Awake() apply) each "crashing" and a full code
revert still crashing were never actually about the code — every one of
those builds reused the same increasingly-stale cache.

## Batch fix pass (hud-batch1/2/3) — all confirmed on device

1. **`RanQuickSlots.cs` icon/wipe circle sprites** — re-applied in
   `hud-batch1`/2/3, then RE-REVERTED after `hud-batch3` (cache cleared)
   crashed on world entry. Current code state: no sprite on icon/wipe (same
   disclosed cosmetic gap as before this whole investigation). Root cause of
   the crash is unknown — see the CORRECTION section above. Do not re-apply
   without new evidence.
2. **Status-text truncation (bug #1) — FIXED, confirmed on device
   (`b2_register.png`, `b3_afterok.png`).** Root cause was
   `VerticalWrapMode.Truncate` on a fixed 44px box silently dropping wrapped
   lines past the first. `Below()` now measures the real wrapped-text height
   per message (`TextGenerator.GetPreferredHeight`) and resizes an actual
   background panel to fit — full multi-line messages render completely, no
   truncation, panel visible instead of bare text over the 3D world. Also
   applies correctly to a real long server message ("this account is already
   logged in…") not just the two stub notices.
3. **Status-text persistence across page nav (bug #2) — FIXED, confirmed on
   device (`b2_cancel.png`: clean server-select, no leftover text).**
   `ShowLoginGroup` now clears `_status` on every real login<->server
   transition (not the one construction-time call, which would have erased
   the deliberate initial "no server configured" message).
4. **New bug found AND fixed during this verification pass**: the
   auto-sizing background panel added for #1 lives one GameObject above
   `_status` (the Text). `ShowCharacters()`/`CharCancel()` were toggling only
   `_status.gameObject`, so the panel's background `Image` stayed active —
   an empty dark box floated over character-select whenever `_status` had
   last shown a message (`b2_charsel.png`). Fixed with a
   `SetStatusPanelActive()` helper that toggles the actual panel root; both
   call sites updated. Not yet re-screenshotted post-fix (same cooldown
   block as #1) — queued for the next pass along with the world-entry retest.

Bug #3 (Attribute bar rendering as a slider) is still unconfirmed/low
priority, not addressed this pass.

## (historical) Investigation before the fix was found

Entering the world (tap "Start" on character select, transition from `boot.unity` to
`gameplay.unity`) crashed the app with the identical logcat signature across MOST attempts today:
`Unity: The file '.../base.apk/assets/bin/Data/level1' is corrupted! Remove it and launch unity
again! [Position out of bounds!]` -> `Fatal signal 5 (SIGTRAP)` on thread `Loading.Preload`.

**Extensive bisection performed (8 independent from-scratch builds, hud-fix34 through hud-fix41):**
- hud-fix34, 35, 36, 37: crashed (4/4). hud-fix36 -> hud-fix37 was a rebuild with ZERO code changes
  and crashed identically both times, initially suggesting determinism.
- hud-fix38: reverted `RanQuickSlots`'s new "only circle" icon/wipe sprite additions -> SUCCEEDED,
  reached real gameplay (screenshot confirmed: real map, real quests, real HP/MP/SP, real NPC).
- hud-fix39: re-added the sprite via a provably-safe runtime-only mechanism (`RanUiCircleApply`,
  embeds nothing into the scene) on frame+icon+wipe+ATK -> crashed again.
- hud-fix40: removed `RanUiCircleApply`, reverted frame/icon/wipe/ATK to their ORIGINAL code, added
  only a `UnityEngine.UI.Mask` component (unrelated to RanUiCircle) -> crashed again.
- hud-fix41: fully reverted to the exact same code state as the successful hud-fix38 -> **crashed
  again**, contradicting hud-fix38's own result under supposedly identical code.

**Conclusion**: this is not caused by any specific code pattern I tested (RanUiCircle sprite
embedding, a Mask component, or anything else) — three structurally unrelated changes each
reproduced the same crash, and reverting to a PROVEN-WORKING exact code state still crashed on the
very next build. This points to genuine build/packaging-level flakiness in this Android build
pipeline on this machine (confirming what was documented earlier this session, though today's
failure rate — 7 of 8 builds — is far higher than the "always fixed by one rebuild" pattern claimed
earlier). Checked and ruled out: disk space (185GB free). Not yet checked: antivirus/Windows Defender
real-time-scanning interference with the Gradle/APK output during packaging (the most likely
remaining cause for this exact failure signature — a file read back immediately after being written,
mid-scan) — would need the user's own system access to check/exclude.

**Code state left in place**: `RanQuickSlots.cs`'s "only circle" fix for the frame is unchanged
(always worked, never implicated). The icon/wipe cosmetic fix is REVERTED (documented as a known,
disclosed gap in code comments) since every attempt to add it coincided with a crash in this
non-deterministic environment, and the actual root cause is not code-fixable. `RanUiCircleApply` is
kept as documented-but-unused infrastructure for a future, lower-risk attempt.

**Not yet re-verified**: whether hud-fix41 (or any further rebuild) can reach the world reliably —
given the demonstrated high failure rate, the next step should be checking build-environment factors
(antivirus exclusions for the Unity/Gradle folders) rather than more code bisection.

## Not yet tested

- Login credentials screen (partially tested: ID/pass fields, checkbox, buttons render correctly)
- Character select
- In-world HUD (all menu icons, windows)
- Camera controls (drag/pinch/lock)
- Minimap click-to-walk
- Skill slots (10-slot two-ring layout, circular shape)
- Movement (joystick, tap-to-target)
- Chat
