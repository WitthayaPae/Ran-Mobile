# RAN mobile — STATUS

**This is the living document. It is updated at the end of every working session.**
If anything here disagrees with another file, this file wins.

- **Last updated:** 2026-09-25
- **Approach:** compile the real PC client (`SOURCE/`) for mobile. Decided 2026-08-24.
- **Current phase:** 1 complete · 2 complete · **3 in progress — login works end to end; character-select scene and models remain**
- **Builds:** `cd MOBILE/native && ./build.sh` → 0 errors, produces `out/arm64-v8a/libran.so`
- **On device:** renders on the x86_64 test device (Adreno 750, GLES 3.1) at a steady 60 fps.
  APKs: `out/ran-phase3.apk` (current), `out/ran-phase2.apk` (headless, kept for comparison).

---

## 2026-09-25 (2) — Anti-bot check, open CDM entry, Tyranny tower lock, event/quest blink

Asked 2026-09-25. All four implemented. What was verified, and how, is noted for
each one; the server halves need the new ServerAgent/ServerField to be deployed
before they do anything live.

### Yellow warning blink (event icon, quest alarm): verified on LDPlayer
- **Cause.** Mobile-only code switched both off on purpose:
  - `CCompetitionNotifyButton` forced its blink hidden every frame.
  - `DxGameStage::MobileKeepMenuIconsAbove` hid `QUEST_ALARM` every frame, which
    also cleared its alarm.
  - The event icon lives inside the MENU grid, so nothing could flash while the
    menu was shut.
- **Blink art.** New cell (384,128) in `mobile_icons.dds`: the PC's yellow
  `q_icon` frame redrawn as an outline on the mobile tile's rounded edge (tile
  edge measured at 8..120 x 10..119, r~20).
- **Event icon.**
  - The PC's blink is back. On mobile it is driven by the clock: the grid puts the
    control in two UI lists, so Update runs twice a frame. Measured: 120 calls/s.
    A summed timer blinked at twice the PC's rate.
  - While the menu is shut, the MENU button carries a yellow ring blinking at 0.2 s
    (`RanTouch_SetMenuAlert`). It is drawn live, outside the cached HUD geometry.
- **Quest alarm.** A corner tile (`QUEST_ALARM_BUTTON_M`, the quest scroll)
  beside the quest box, blinking with the same outline.
- **Measured.** Yellow-pixel counts across frames:
  - menu ring: 1892 / 212
  - quest tile: 18 / 532
  - grid event icon: 15 / 1902
  - Alarm off: the outline disappears.
- **Diagnostic.** Flag file `eventalarm` forces the event alarm on; deleting it
  resets the alarm.

### CDM: any club member can enter (server; not testable against live)
- `GLCLUB::CanJoinCDM` (member or master) replaces `IsMemberFlgCDM` at all five
  entry gates:
  - agent `MsgReqGateOut`, `CheckStartMap`
  - field `RequestGateOutReq`, `RequestInvenRecall`, `GLChar::CheckStartMap`
- The master's CDM tick in the sub-master dialog is hidden. The flag is still
  loaded and sent back unchanged.
- **Effect.** Nothing now caps a club's headcount (the old cap was the 7
  assigned members). CDM rewards go to everyone standing on the map, so more
  items will be handed out.

### Tyranny: towers damageable only in the last 5 minutes (server; client parts verified)
- `TYRANNY_TOWER_OPEN_TIME` is 300 s.
- **Field server.**
  - Keeps `m_fBattleRemain` from the agent's STATE_BATTLE `fTime` and counts it
    down.
  - `GLCrow::IsTyrannyTowerLocked` gates:
    - `GLChar::IsReActionable` (attacks, skills, summons)
    - `GLCrow::ReceiveDamage` and `VAR_BODY_POINT` (damage only)
    - `TyrannyDamageCheck`
  - A field that missed the battle start (remaining 0) leaves the towers open.
- **Client.**
  - A locked tower is refused in `IsReActionable` / `MobReaction`, with a message
    at most every 3 s.
  - Auto-target skips locked towers.
  - The tower HUD shows "ฐานเปิดใน 5 นาทีสุดท้าย".
  - A one-time notice fires when 5 minutes remain.
- **Captions.** `PVP_TYRANNY_BATTLE` now mentions the rule. The rule also sits on
  the Tyranny page's note line: it was first put on the goal line, which cut it
  off (seen on device).
- **Not changed.** `tyranny.ntk` (encrypted NPC talk) may still describe the old
  rule.

### Anti-bot check (server + both clients; window verified with a local demo)
- **When it asks.** Every `dwAntiBotIntervalSec` (3600) +/- `dwAntiBotJitterSec`
  (1200) of hunting time. Hunting time is spent outside safe zones and event
  maps, alive, not trading, duelling or running a shop, at or above
  `dwAntiBotMinLevel`. GMs and fake PCs are exempt.
- **The question.** The field asks a+b with four answers.
- **Failure.** No answer within `dwAntiBotTimeoutSec` (180), or `dwAntiBotMaxTry`
  (3) wrong answers, disconnects the character (cheat type 5, "ANTIBOT_KICKED").
  A wrong answer brings a new sum on the same clock.
- **Config.** `bFeatureAntiBot` (default ON) and the values above, in
  `[GAME_FEATURE]` of the field server's Config.ini.
- **Log.** `_antibot.txt`: ASK / PASS / WRONG / KICK / GM, with response time.
- **GM command.** `/antibot <name>` asks that character now (same field server
  only).
- **Code.**
  - Messages: `NET_MSG_GCTRL_ANTIBOT_*` (+3920..3923).
  - Structs: `GLContrlServerMsg.h`.
  - Server logic: `GLCharAntiBot.cpp`.
  - Window: `CAntiBotWindow`, controls in `_inner_inventory_lock.xml`. Its X does
    nothing, and it re-opens if hidden while a question is pending.
- **Verified on LDPlayer** with the `antibotdemo` diagnostic (question made and
  answered on the device, nothing sent):
  - The window shows the sum, four 40-unit buttons, and the countdown with tries
    left.
  - A wrong answer shows "คำตอบไม่ถูกต้อง ลองข้อใหม่" with a new sum and 2
    tries, and the clock keeps running.
  - The X does not close it; the right answer does.
- **Not verified.** The server round trip (needs the new field server).
- **Limit.** A bot that reads client memory can do the sum. This stops click,
  macro and pixel bots.

### Still open
- [ ] Deploy ServerAgent + ServerField + the PC client (user).
- [ ] Ship the Android + iOS patch: glow fix, launcher minApk ordering, all of the
  above, plus data (`Gui.rcc`, `mobile_icons.dds`).
- [ ] Tablet check of the glow and the blink.

---

## 2026-09-25 (1) — Weapon/costume neon glow: on, in the right place, whole

Test01's costume bow (BDN0038_M_one.cps, NEON effect) and testp2's glove glow
on PC and showed nothing on mobile. Three causes, each measured:

1. **Never switched on.** `ran_app.cpp`'s copy of `RestoreDeviceObjects` skipped
   `DxGlowMan::SetProjectActiveON()` / `DxPostProcess::SetProjectActiveON()`,
   which the PC calls there. Added; the pink appeared.
2. **Mirrored top-to-bottom.** Frame dumps (`glowdump` switch, below) showed the
   neon source texture and the blur exactly on the weapons, and the final frame
   showing the glow reflected about the screen's horizontal centre. The glove
   glowed low, and the bow's arc was drawn backwards. The shim stored
   render targets bottom row first, while the client samples them with D3D
   UVs (v=0 at the top). RT-to-RT passes flipped twice and cancelled out; the
   last composite onto the frame flipped once. `uFlipY` had been uploaded per
   draw since the first commit, but the vertex shader never declared it. The
   render-target cull flip was written for a mirror that never happened, so
   off-screen world geometry was also culled inside out. Fix: the shader
   mirrors into RTs, so rows are stored in D3D order. RT viewport and
   scissor now use top-down y, and `StretchRect` converts each side separately
   (RT as-is, frame inverted).
3. **Fragments that faded.** The shim gives each RT its own depth buffer, and
   that buffer was cleared only when the RT was created. The PC's neon pass uses
   the scene depth, rebuilt every frame. Each frame's glow was therefore tested
   against the nearest depth of everywhere the weapon had ever been. Fix: clear
   RT depth on each RT's first bind every frame (`RanRT::depthFrame`). Sharing
   the scene depth isn't possible: it's panel-sized and the glow source is
   1280x720. Remaining difference from PC: a glow isn't hidden behind another
   object in front of the weapon.

Verified on LDPlayer: source dump shows the whole glove and bow; the frame
shows each glow on its weapon; no GL errors or incomplete targets. Tablet
not yet checked.

Diagnostic: flag file `glowdump` in the data root writes `glow_scene.ppm`,
`glow_src.ppm` and `glow_blur.ppm` every 120 glow frames
(`RanGLR_DumpTarget`). RT dumps are top row first, the frame bottom row first.

---

## 2026-09-24 (3) — ROV-style touch feedback, target lock, range circle, settings

All in shared code, so Android and iOS both have it; iOS CI build of it passed
(run 36033074739). Verified on the emulator:

- **Tap ring**: white ring wherever the screen is touched, every page (login
  included), drawn last in Present. Screenshot on the login page.
- **Target rule** drop-down (nearest / lowest HP / lowest HP %): after picking
  HP %, the log shows `auto-pick (rule 2)`.
- **Auto-lock follows a tap on an enemy**; an NPC/friendly can no longer hold
  the auto-lock (it used to, for ever - measured). The switch itself was NOT
  seen live: mobs never stood still in reach of a tap on the emulator. Per-tap
  log line `RanTarget: tap on kind=.. id=..` shows what each tap hit.
- **Range circle**: white outline only, attack reach while attack is held,
  skill reach on a skill press, follows the ground. Seen on screen.
- **Settings > Function**: players drawn, graphics quality and target rule are
  drop-downs; tap ring and range circle are check boxes (range off verified:
  no circle); HUD editor button last.
- **Chat button layers**: open, the fold plate draws right after the chat
  window; folded, the icon draws with the ride button - both under the
  inventory (screenshots).

## 2026-09-24 (2) — Billboards, the copyright line, and iOS catching up with Android

**Map billboards.** The 18 `textures/map/ad_ppl*.dds` still carried 2010-2012
Philippine sponsor ads. They now carry RAN LEGACY M ads (prompts in
`AD-BRIEF.md`, only features live on production). The image model drew its own
fake logo on every one; `tools/ad-art/put-emblem.py` removes it with LaMa
inpainting and puts the real `ran_mark.png` in its place, and
`tools/ad-art/make-ads.py` writes each DDS in its original format with the full
mip chain, keeping the five painted frames. Checked at full size and at the
512x256 the game draws. Three more old ads (ad_ppl2_13, ad_ppl3_04/05 - a
dated sale and a Taiwanese club promo with real player names) have prompts and
wait for art.

**Copyright.** Login footer, map-loading line and the PC launcher's About now
say "Copyright (c) 2026 Invis Dev." / "All Rights Reserved." - gameword.xml
and launcher.xml edited and repacked into Gui.rcc (packed == loose, verified),
seen on the emulator's login screen.

**iOS patch page = Android's.** The 2026-09-23 Android rebuild (moving bar,
Thai text, retry instead of starting on a failed patch) had never reached iOS.
It has now: RanPatchBar draws the Java's bar, every string is the Java's Thai,
failures count down and retry, and only a too-old app is fatal. Compiled on CI;
not yet seen on an iPhone.

**Still open:** the store-543 upload set built earlier is STALE (it predates the
Gui.rcc copyright change and the iOS page) - rebuild with MAKE-PATCH before any
upload. The patch also carries the user's GLogic.rcc/NpcTalk.rcc beta-shop
edits and the level-up-card client commit.

## 2026-09-24 (1) — The challenge login, and the hash checked against production

The account leak found in the security review is being closed the cheap way: the
password stops crossing the wire, and nothing else about the protocol changes.

**How it works.** The server sends the account's salt and a nonce chosen for that
connection; the client computes `UserPassHash` - the salted, 1000-round SHA-256
the database already stores (DB/FIX_03) - and answers with
`HMAC-SHA256(hash, nonce)`. The server holds that hash, so it can check the
answer without ever holding the password. A capture is worth nothing: the nonce
is used once and the hash itself is never sent.

**Verified against the live database**, not inferred. `fn_PassHash` on the
production server (SQL Server 2019) and `RanPassHash` in the client agree byte
for byte on every case, including the trailing-space one that FIX_05 records:

    abc      36cf56011cff8fb8bb96d2a131f472e3823cb1b37dae413499db0ca0d7500095
    "abc   " 36cf56011cff8fb8bb96d2a131f472e3823cb1b37dae413499db0ca0d7500095
    " lead"  a16fc20a324fe22c16eff25e3857143d74b141ee1d3c193c687bf40ac610e7fd
    1234     0b78de26de9bb52ffa327d7097176ec26a0318e7d0f8e577af5f89ee6a812d9e

The live account table is one row and it is hashed, so there is nothing to
migrate.

**One thing this development machine nearly hid.** It runs SQL Server 2008 R2,
where `HASHBYTES('SHA2_256', ...)` returns NULL - so `fn_PassHash` yields NULL
here and the hashing looks broken. The live server is 2019 and is fine. The
lesson stuck anyway: the hash is computed by the SERVER (RanPassHash), not by
SQL, so the login does not depend on the database's version at all. On 2012 and
up the bytes are identical, which is what the table above proves.

**Built:** the hash primitive, the three wire messages, DB/FIX_06
(user_challenge_fetch, user_verify_hashed), the ODBC layer (ChallengeFetch,
SetPassHash, UserCheckHashed), both agent handlers (nonce per connection,
constant-time compare, one answer per nonce, no account-existence tell), and the
client's login flow. Solution builds clean, all targets; mobile builds clean.

**The fallback is what makes this shippable, and it is tested.** A server that
does not know NET_MSG_LOGIN_SALT drops it without a word, so silence is the
answer that means "not this one": the client waits three seconds and sends the
old login. Against the live server, which has not been updated:

    RanChal : salt asked (channel 0)
    RanChal : no salt answer in 3000 ms - falling back to the old login
    RanLogin: result=0                      <- EM_LOGIN_FB_SUB_OK, in world as Test01

So the client can ship before the server does, and the password only stops
crossing the wire once the agent is updated. PC is untouched - the only call
into any of this sits inside RAN_MOBILE.

**One thing that test nearly blamed on the wrong change.** The first run failed
with "wrong id or password", which looks exactly like a broken login flow. It
was not: the account in `native/.login` does not exist. `RanUser.dbo.UserInfo`
holds one row, `test01`, and that is what the rig should use.

**Two things hardened after the first pass.** The stand-in salt for an unknown
id was derived under a secret each process invented at startup - fine within one
sitting, the opposite across a restart, because a real account's salt never
changes and a made-up one's changed every time. It now comes from FIX_06's
ServerSecret, read once per run. And the three message structs assert their own
sizes (32, 40, 68), compiled on both the 32-bit MSVC server and the 64-bit clang
client: both ends check dwSize before reading, so a drifted layout would not
crash, it would silently fall back forever.

**DB/FIX_06 is applied to live** (RanUser on 143.14.11.244, SQL Server 2019,
2026-09-24), verified after it ran: ServerSecret holds one 32-byte row,
user_verify_hashed answers 1 for test01 and 0 for an id that does not exist,
and user_verify is untouched so old clients are unaffected.

**The agent is built and staged** at `DEPLOY/challenge-login-2026-09-24/`
(ServerAgent.exe, checksums, and a README with the rollback). A clean rebuild
of the whole solution first needed one build fix: Lib_Engine's include path had
`Tik/Lua` where lua.h lives in `Tik/Lua/include`, which only ever showed up in a
from-scratch build.

**It runs end to end on live.** With the new ServerAgent deployed, the shipping
APK logged in without the password leaving the device:

    RanChal : salt asked (channel 0)
    RanChal : salt answered - sending the challenge
    RanLogin: result=0                      <- no fallback line, in as Test01

That is the whole feature working: salt and nonce out, HMAC back, and the
account's password never on the wire.

**What went wrong in production, and what fixed it (2026-09-24, measured):**

1. The first agent build said OK and then could not load characters
   ("ไม่สามารถเข้าถึงข้อมูลตัวละคร" = CHARACTERSTAGE_DAUM_GAME_JOIN_FAIL, the
   join wait timing out). The challenge handler skipped MsgLogIn's pre-DB steps,
   above all IncreaseChannelUser + SetChannel, so the channel stayed -1 and
   GetFieldServer(field, -1) had no answer. All four steps now run in MsgLogIn's
   order.
2. The message then grew the encrypt key (68 -> 80 bytes). The 02:30 agent
   accepted only 80, and v542 - already in players' hands, no stage-2 fallback -
   hung on "checking data". The agent now reads both shapes
   (NET_LOGIN_CHALLENGE_DATA_V1 asserted at 68).

Verified on live against the deployed agent, both clients, into the world:

    v542 client (68-byte):  salt answered - sending the challenge / result=0
    new client  (80-byte):  salt answered - sending the challenge / result=0

The lesson: a new login path must reproduce the WHOLE handler it stands in for,
read end to end; and once a message shape ships, the server reads it forever.

**Mobile security sign-off for go-live (2026-09-24, measured on device):** the
password never leaves the phone (challenge accepted for v542 and the newer
client); after a login the device log holds neither the id nor the password;
nothing in the app's data folder stores either (the one digit match,
`activity.bin`, is byte-identical to shipped game data). Accepted and left open
by decision: chat and gameplay traffic in the clear, and the 3-second fallback
an active attacker on a hostile network could force. Cleanups, not blockers:
drop MANAGE_EXTERNAL_STORAGE when the migration window closes; move
KEYSTORE-BACKUP/ offline. DB exposure (1433 + sa) is the user's, deferred.

**Shipped as store version 542** (APK versionCode 148 / iOS 1.0.148, minIos 148).
The upload set is 4 blobs, 50.5 MB - the APK, the HUD atlas and the two ini
files - plus ios/ (RanLegacyM.ipa 11.3 MB, source.json) and the manifest. The
iOS binary was checked for a string only this build contains before publishing
it, and its Info.plist reads 1.0.148.

## 2026-09-23 (9) — End-to-end pass on the shipping build, and the one thing it caught

Everything today had been verified one change at a time. One pass over the build
that would actually go out, on the emulator as `test01`, found one real problem -
which is the argument for doing it.

**The chat fold button was live, correctly placed, and drew nothing.** Not a
regression in the code: the patch test earlier in the day deleted
`textures/gui/*.dds` to force a download, and the patch restored the SERVER's
copy - version 540, which predates the repack that added the chat cells. Device
md5 `c9be1cd6...`, local `ce03bd31...`. The build asks for cell 23; that sheet
has 22; an empty cell is transparent, and there is no way to ask a sheet how many
cells it holds.

That is the shape of every code-and-data split in this port: the .so rides the
APK, the sheet rides the patch, and a player can have one without the other. So
`RanTouch_RenderChatTop` now draws the vector plate or bubble FIRST and lays the
painted cell over it. Worst case the drawn shape shows, best case the painting
covers it; two dozen vertices a frame. Verified both ways - the old sheet on the
device draws the plain plate, and pushing the correct sheet puts the painted one
back over it.

**The rest of the pass, all on the final build:** plain drag inside the bag; tap
opens the action sheet; long-press drag back; drag a potion onto a quick slot;
unequip and re-equip the amulet by dragging to and from the doll; the quick slots
bound to a held item fade while the other two stay lit; chat folds to the painted
icon beside the ride button and comes back; a tap on bare ground clears the
target (`tap cleared 531`); the video page ends at "แสดงNPCที่เลือก" with both
item-FX rows gone and ตกลง applying without a crash.

One thing the pass proved that was not a bug: tapping an NPC opens its dialogue,
and while that window is up a tap on the world does nothing at all - including
clearing the target. That is `IsCharMoveBlock` doing its job, the same guard that
stops a tap on a window walking the character.

## 2026-09-23 (8) — Security review of the mobile client, and the one finding that was ours to fix

Asked for a client-side security review. Four things worth acting on; one of them
was a bug in this port and is fixed here, the rest are decisions or inherited
design and are written down so they are not rediscovered.

### Fixed: the diagnostic switches were read from shared storage

`ran_plat.cpp:13` defaults the diagnostic root to `/sdcard/ran`, and
`RanPlat_SetDiagRoot` (line 141) had **no caller on Android** - iOS sets it
(`ran_ios_plat.mm:132`), Android never did. Every switch is looked up by name in
that directory, which any app with a storage permission can write, as can the
player with a file manager.

Demonstrated on the shipped build before the fix: `echo x > /sdcard/ran/nohud`,
relaunch, and the whole touch pad was gone. The one that matters is not `nohud`
though - `drawlimit` (`gl_render.cpp:1220`) reads a number from that file and
stops the frame after N draws, which takes geometry out of the scene and leaves
whatever it was hiding in plain sight. `effskip`, `sectionskip`, `nooutline`,
`renderscale` and `worldscale` are reachable the same way, and `ran.log` was
written there too.

`android_main.cpp` now calls `RanPlat_SetDiagRoot(root)` at boot, with the data
root - the app's own external files directory, which no other app can reach on
Android 11 and up. Verified both ways: with `/sdcard/ran/nohud` present the HUD
draws normally, and with the same file in the private root it still disappears,
so the development tooling keeps working over adb. **Diagnostic files now live
in `/sdcard/Android/data/com.ran.native/files/`, not `/sdcard/ran/`.**

### Not ours to fix, and the reasoning

* **Credentials on the wire.** `LoginPage.cpp:237` -> `CNetClient::SndLogin`
  (`s_NetClientMsgLogin.cpp:28`) sends id and password over plain TCP, each field
  passed through XXTEA; `minTea m_Tea;` (`s_NetClient.h:117`) is default
  constructed, so the key is the literal at `minTea.cpp:20` - the same in every
  copy of the client. Anyone on the same network with a copy of the APK recovers
  the account. Inherited EP9 design; the fix is TLS in front of the login server
  with a pinned certificate, which is a server change.
* **The release APK is signed with the debug keystore.** `build-apk.sh:163-173`:
  alias `androiddebugkey`, store and key password `android`, generated if
  missing; `KEYSTORE-BACKUP/ran-signing.keystore` is byte-identical to it. It is
  NOT in git (`git ls-files`, and `.gitignore:51`). It matters because the app
  updates itself through PackageInstaller and the signature is the only check on
  an update. Treat the file as a secret; rotating it forces every player to
  reinstall, so that is a version-boundary decision.
* **Permissions.** `MANAGE_EXTERNAL_STORAGE` plus read/write external storage are
  held only to migrate a pre-private-root install. All-files access kept forever
  for a one-off; drop it when the migration window closes.

### Checked and sound

Manifest ECDSA-verified before it is parsed; blobs content-addressed and
SHA-256 checked before rename; `safeDest` blocks absolute paths, backslashes,
`..` and anything whose canonical path escapes the root; bodies have a size
ceiling; an older manifest version is refused; the APK is streamed into the
installer and hashed in flight. No custom `TrustManager` or `HostnameVerifier`;
cleartext off except loopback. `allowBackup=false`, no `android:debuggable`,
`NativeActivity` not exported. The payload is built from `CLIENT/` only, so the
server cfg with its DB credentials cannot ride along - the device tree confirms
it. No credential is written to disk: `GETUSERID_ENC` has no caller and the
account name appears nowhere under the data root.

### First slice of the server side: the item move this port now automates

The client change made today turns one gesture into a lift and a place, so the
question is whether the server decides those or merely records them. It decides:

* `GLChar::MsgReqInvenToHold` (`GLCharInvenMsg.cpp:1495`) looks the item up at
  the cell the client named and refuses when there is none, refuses when the hand
  is already full, and refuses an unknown item id. The coordinates are a lookup
  key, never content.
* `GLChar::MsgReqHoldToInven` (`:2983`) refuses unless something is held, re-reads
  the held item's own size from the item table rather than trusting the packet,
  and calls `IsInsertable` for that size at the requested cell before inserting.
* `GLInventory::IsInsertable` (`GLInventory.cpp:606`) rejects a zero or oversized
  item, and bounds the position with `(m_wCellSX-wSX) < wPosX` /
  `(wVALIDY-wSY) < wPosY` before it indexes the barrier grid. Those subtractions
  are int-promoted, so an under-sized `wVALIDY` goes negative rather than wrapping
  to a huge WORD - the grid cannot be indexed out of range from a crafted packet.

So a modified client can ask for a move it should not have; it cannot make one
happen, and cannot corrupt the grid trying.

Still not examined: the rest of server-side authority - skills, movement, trade,
the shop - the wider EP9 packet parsing surface, and the iOS distribution chain
beyond what ships in the patch.

## 2026-09-23 (7) — The patch page: one bar that both measures and moves, in Thai, and no way past it

"when it loading the loading bar load it should be something like loading
animation because right now when it struck somewhere it make like it did not
work", "text should be in Thai", and "if we check user did not have version same
as server it should force user to patch not to continue if can not connect to
patch server". Then, on seeing the first cut: "we can do the progress bar and
animation bar same... the right side the empty where the progress bar have to
load we make it animation bar".

**One bar, two jobs.** The gold fill is the number the patcher gave and nothing
animates it; the sweep runs along what is LEFT of it, clipped to the remainder.
So the page is visibly alive without ever claiming progress it has not made -
which matters because a patch spends minutes inside one big file with the number
motionless, and that is exactly when a player decides it has hung. Before there
is a number at all the fill is zero and the sweep has the whole track, so the
indeterminate case falls out of the same rule.

**It had to be ticked from outside onDraw.** `postInvalidateOnAnimation()` at the
end of `onDraw` is the obvious way to write it and it does not work: measured on
the device, the bar's pixels were byte for byte identical across four frames
while the countdown text beside it changed on every one. An invalidate issued
while the view is being drawn is swallowed. A posted Runnable at 16 ms is
outside that pass and schedules properly; it stops when the view leaves the
window.

**Thai throughout** - every string the launcher can show, from "กำลังเริ่ม" to
the APK-too-old stop. Numbers, sizes and the reason code stay as they are.

**And the game no longer starts on a failed patch.** It used to: a failure with
data already on disk said so for a second and played anyway. On a client whose
version does not match the server's that is worse than waiting - the player gets
in, plays against packets they do not understand, and reads the symptoms as the
game being broken. Whether the versions match is exactly what the launcher
cannot know while the server is unreachable: the local `.patchver` is the
version last applied, not the current one. A failure now waits and retries, with
the seconds counted on screen and the wait growing to half a minute, and clears
itself when the connection returns. `fail()` still ends the run for good - the
APK being too old is a hard stop.

**Measured on the device**, by pointing `.patchbase` at a dead host and by
deleting `textures/gui/*.dds` to force a real 180 MB download:

| what | result |
|---|---|
| dead host | `เชื่อมต่อเซิร์ฟเวอร์อัปเดตไม่ได้ \| จะลองใหม่ใน 5 วินาที ... (ConnectException)`, counting down, game never launched |
| sweep moving | peak at x=1189, 1203, 1197, 1399 across four frames |
| fill + sweep together | 8% gold with the sweep at x≈1200-1700; a moment later 30% gold, bright head at its edge, sweep at x≈1500-1900 |
| after the patch | version 540 written, 500 GUI textures restored, login page up |

## 2026-09-23 (6) — The quick slot's sheet opens on the hold, not on the release

"long press on the potion slot do not need to wait untill I let go to show the
option".

It opened on `UIMSG_RB_UP`, so a long press looked like nothing was happening
until the finger came off - while every other long press in the build acts on the
DOWN edge: the bag lifts there, the worn slot too. It now opens on
`UIMSG_RB_DOWN`, with a full hand still keeping its release, because that is a
carried item being placed in the slot.

The release cannot press whatever row lands under the finger: the sheet's rows
answer to `CHECK_MOUSEIN_LBUPLIKE`, the LEFT button, and this is the right one.
Verified by screenshotting mid-gesture, with the finger still down: the sheet is
up (ถอด / ตั้งค่า / ปิด), and the frame after the release is identical - the
release neither closed it nor opened it a second time. ปิด still closes it.

## 2026-09-23 (5) — "แสดง FX ไอเท็มบนพื้น" is gone from the options, and the effect with it

"remove the setting แสดง FX ไอเท็ม we do not need that for mobile and disable
this feature for mobile".

**The label and the flag are one row apart in this build.** The video page builds
three check boxes in order - CLICK_EFFECT, TARGET_EFFECT, MINE_EFFECT at authored
Y 235, 260, 285 - and labels the three statics beside them with gameword
`HWOPTION_VIDEO_OPTION` 12, 13, 14: "show the selected NPC", "show item FX on the
ground", "show the target on the ground". The flags themselves say otherwise:
`bClickEffect` drives the click marker, `bTargetEffect` the target effect, and
`bMineEffect` is the one that hangs `strMINE_EFFECT` on every item lying on the
ground (`GLLandManClient.cpp:461`). So the box in the row that READS "item FX"
toggles the target effect, and the item glow is toggled by the box in the row
below it.

So both rows come off the mobile page: the one that was named, and the one that
actually switches the feature being disabled - otherwise the page would keep a
box that does nothing. Not created rather than hidden, the lesson the potion
tray's own buttons recorded: an invisible check box still takes the tap. The two
pointers are now NULLed in the constructor, which they never were, and every use
tests them.

**The effect is off with no switch.** It is a passive effect per dropped item,
alive as long as the item is, and a field after a fight carries dozens - fill the
phone pays for every frame, for a marker it does not need, since the pick-up
button finds the nearest item itself. `bMineEffect` is forced FALSE after the
options file is read (so a settings file written by the PC client still parses
line for line) and the attach site is compiled out.

Verified on the device: the video page now ends at "แสดงNPCที่เลือก", and
ตกลง applies and closes with no crash - which is what the NULL guards are for.

## 2026-09-23 (4) — A tap on bare ground now lets the target go

"when user click on the empty place like other places on the map or on the
ground it should cancel the target (incase auto target not on)".

The tap latch in `GLCharacter::PlayerUpdate` only ever SET the target: a tap
that landed on an actor latched it, and a tap that landed on nothing was treated
as a move order and nothing else - the comment there said so in as many words.
With a mouse that is right, because pointing somewhere else is itself a change
of target and the client re-picks every frame; a finger has no pointer to move,
so once something was selected there was no way to select nothing. The panel
stayed up and the attack button stayed aimed at a mob the player had walked away
from.

A tap that resolves to `EMACTAR_NULL` now drops the target, through a new
`MobileDropTarget` which takes the six target panels down with it - zeroing the
target alone would have skipped `MobileTargetTick`'s own clean-up, which is
guarded by the target still being there. Auto-target keeps its lock: while it is
on and its pick is alive, a tap on the ground is only a walk, which is the
condition the request named. Taps on the interface never reach this code -
`IsCharMoveBlock` returns above it, the same guard that stops a tap on a window
walking the character.

**Measured on the device**, with auto-target off:

    RanTarget: tap latch -> 591 (was 4294967295, live=0, click L1 R0)
    RanTarget: tap cleared 591

and the selection ring under that player is gone from the next frame. With
auto-target on, the same two taps on bare ground produced no clear at all - only
`select-nearest -> 595`, its own pick, holding.

## 2026-09-23 (3) — The round potion slots never faded, and the macro buttons were a mouse's size

"when I pick up item in the inventory that use in the potion slot it did not
fade like pc version", and "the quick chat btn on top of the chat box... it's
hard to click on the phone".

**The fade.** `CBasicQuickPotionSlot::SetItem` draws a bound slot's picture at
alpha 160/255 when the bag holds none of that item - which is exactly what the
player sees the moment they pick the last stack up, because a carried item is
out of the bag and in the hand. The square slot still does this; the round row
does not draw through that control at all - the overlay draws the picture from a
texture handle the client hands over - so the fade was simply not carried across.
`RanTouch_SetPotionIcons` now takes a `dim` per icon, and the alpha uniform is
set per icon inside the loop rather than once for the row, because the fade is
per slot. The client fills it from `GetAmountActionQ`, which is the same number
the PC's rule reads.

Verified on the device: with a green potion in hand, both slots bound to it went
dim while the red and blue ones beside them stayed lit; putting it back lit them
again.

**The macro buttons.** 28 x 19 layout units - about 6 x 4 mm on a phone, and the
row is short in the direction that matters. The art stays (it is the chat's own
tab skin, in a row against the chat's edge) and only the rectangle the press is
tested against grows, which is what `SetTouchPad` was added for. It grew per
axis: `SetTouchPadXY(1, 9)`, because the gap between buttons is 3 and a wide pad
would only hand taps to the neighbour. `CUIControl` keeps a second pad for Y;
where it is not set, it falls back to the old single value, so nothing else
moves.

Measured, not felt: a tap 10 px above the button's top edge sends the macro
(`[Test01]:1` in the log), a tap 40 px above it does not.

## 2026-09-23 (2) — The quick-potion row was half again the size of the PC's

"the potion slot I see that it's bigger then the original one and too much space
between slot."

**Measured, both sides.** The authored tray (`uiinnercfg03.xml`) is six slots of
37 x 41 at a step of exactly 37 - contiguous, no gap. On the device the row was
52 units across on a 65 step: half again as big, and nearly twice the daylight.

**Why.** `MobileArrangeRound` sized the slots off the ATTACK RING
(`fSlotR = fAR * 0.36`). The ring is a thumb control and is sized for a thumb;
the potion row is a readout you tap, sits under the status bars, and has its own
authored size. Anchoring one to the other made it grow with a control it has
nothing to do with.

**Now** the row takes its size from the authored slot, captured once with the
anchor: `fSlotR = width * 0.37`, which puts the drawn bezel at ~44 and the art
painted inside it at the authored 37. The step is measured against that PAINTED
frame rather than the quad - at 3.38 radii the quads touched and the frames
still had nine units between them, which is the gap that was complained about -
so it is `fSlotR * 2.92`, and the frames sit against one another the way the
PC's do. The HUD editor's per-row and per-slot scales still multiply on top.

Verified at 1:1 against the same crop before and after.

**And the two buttons at the end of that tray were live, invisibly.** The
collapse arrow and the auto-pot gear sit at the END of the authored tray - local
226 and 246 - and the round row is drawn across that stretch by the overlay,
which draws after the bottom list. So both were covered and both still took
taps: a tap on what looked like street beside the last potion collapsed the tray
or opened the auto-pot window. The client was asked where they were rather than
guessed at: `(431,0 20x41)` and `(454,0 15x41)`, both reporting themselves
visible.

They are now not created at all in the mobile build, which is the lesson the
skill tray's own collapse arrow already records - `SetVisibleSingle` is not what
the hit test reads, so hiding one never sticks. Nothing is lost: the auto-pot
window is a row on the long-press sheet of any quick slot, verified still there
(ถอด / ตั้งค่า / ปิด), and collapsing the row that IS the HUD would leave no way
to bring it back.

## 2026-09-23 (1) — A drag never let go of the item, and the chat could not be folded away

"I see the bug here when I drag the skill or item to the slot then I release.
it's not let, go", and "the chat it should have the collapse btn top right of
the chat box. when collapsed it should become the btn chat".

### The drag

**Measured first.** On the emulator as `test01`, with the bag open: a long press
then a drag moved the item; a plain drag - press, move, release - did nothing at
all, and dragging a potion onto a quick slot did nothing either way. The log says
why: `GESTURE left(tap/drag) ... moved=1`, so the gesture layer pressed the LEFT
button at the source and released it at the target, and `CInnerInterface::
MobileItemTouch` read that pair as "pressed on one cell, let go on another" -
which it deliberately ignores, so a finger sliding across the bag does not open
the actions for wherever it stopped.

**What a drag actually is here.** The carried item is server state (`SLOT_HOLD`),
so a move is two requests: lift the source, then place into the target. A tap
does one of those per tap. A drag has to do both, and the two halves are a tenth
of a second apart, which is less than a round trip - so the naive version loses
the place, and the item stays stuck in the hand. That is the bug as the player
sees it: "I release and it doesn't let go."

Three things were needed:

* **Lift on travel, not on the cell.** `CInnerInterface::MobileDragFrame` watches
  the press: while the left button is down and the hand is empty, once the
  pointer has moved more than 8 UI units from where it went down, the cell that
  was pressed is lifted. It is a per-frame check and not a message handler
  because a window is only asked about a message while the finger is over IT -
  a drag from the bag to a quick slot leaves the bag on the first frame, so
  judged by messages the lift never happened. That was exactly the potion case.
* **A place that waits.** `MobilePlaceItem` performs the drop at once if the hand
  is already full, and otherwise remembers the target and does it the moment the
  hand fills (`MobilePlaceFrame`, giving up after 1.5 s so a refused lift does
  not place whatever turns up next). Every window's DROP now goes through it -
  bag, worn slot, bank, quick slot.
* **The quick slots had no drop at all.** `CBasicPotionTray` bound on `LB_UP`
  with a full hand only; a long press arrives as `RB_UP`, which did nothing, and
  a plain drag arrives before the hand is full. Both now place.

**Verified on device**, each one as a 1:1 crop before and after: plain drag
between two bag cells; plain drag bag -> quick slot (slot 4 filled); long press
then drag, which still works; unequip by dragging the amulet off the doll into
the bag, and equip by dragging it back; a tap still opens the action sheet; a tap
on an empty cell still does nothing.

### The tap that stole a skill

Reported while testing: "when click only one time on the skill icon it should
only show detail not pick it up". `CSkillSlot` took the skill on `LB_DOWN` -
the PC rule, where the cursor is already on the row and the pick-up is visible
under it. With a finger, a tap on a skill row is how you READ it, and every such
tap walked away carrying the skill. The press is now only remembered
(`MobileSkillPress`); the same travel test above carries it, and a long press
still takes it on the spot. Verified: a single tap now shows the detail panel
and carries nothing. The carry-and-drop half could not be re-tested on `test01`
- that character has no learned skill, and an unlearned row cannot be picked up
at all - so that half rests on it being the same code path the items use.

### The chat fold

A round button on the chat's top right corner folds the whole chat group away
and becomes the chat icon that brings it back, in the same place, under the same
thumb. Drawn by the touch overlay (`RANTOUCH_SLOT_CHAT`), so it is the same
object as the attack ring and the mode toggles; positioned from
`DxGameStage::MobileArrangeInterface` rather than from `CBasicChat::Update`,
because `CUIMan::UpdateList` skips a hidden group entirely - placed there the
button would have vanished with the window it is the only way back from.

**One rule had to be bent for it.** `RanTouch_PointerDown` hands any press that
lands on a client control straight to the client, so windows are not eaten by the
pad. This button sits ON the chat's frame on purpose, so it is tested before that
rule - the first and only overlay button that is.

**Shaped by review.** A steel disc with an arrow on it read as one more thumb
button parked on the chat, so open it is now the window's own corner control: a
chamfered plate straddling the top edge with a minimise bar across it, its right
edge 5 units in from the window's. Folded, it parks directly against the ride
button, one gap to its left and on its line - the chat is not on screen any more
to hang anything on, and that row is where the thumb already goes. The overlay
places it there itself, off the ride button's own centre, because the player can
move that button and only the overlay knows where it ended up.

**The painted pair landed the same day.** `chat.png` and `chat_close.png` are
cells 22 and 23 of `mobile_hud.dds` (the sheet is 5x5 and had three spare). The
plate cell is the one piece of art in that sheet that is not round: measured off
the file it paints 178 x 144 inside its 256 cell, so `RANTOUCH_CHATBAR_ASPECT`
is 1.236 and the cell is drawn at `R * 256/144` - which puts the painted plate
exactly on the rectangle the press is tested against. The vector bar and bubble
are still there for a build without the sheet.

**It had to be drawn in a pass of its own.** The plate belongs INSIDE the chat's
top right corner, and the whole pad is drawn as the interface's underlay - after
the bottom list, before the windows - so the chat drew straight over it. It now
has its own little pass, `RanTouch_RenderChatTop`, called from `DxGameStage`
right after `CInnerInterface::Render` beside the HUD editor's toolbar, which is
over the interface for exactly the same reason. The press was never the problem:
that is claimed before the "a window on top gets it" rule, which is what let the
button work at all while it was invisible behind the chat.

Its right edge is 16 units in from the window's, not 5: the scroll bar owns the
last ten or so and has its own button at the top, and the plate was sitting on
it.

Folded, the icon parks on the RIGHT of the ride button. `MOBILE/ICON-BRIEF.md` carries the prompt for
the painted `chat.png`, which drops into `mobile_hud.dds` beside the other round
controls when it exists.

**Verified on device:** fold, icon appears alone on the street, unfold, chat and
its corner button back.

**Builds:** arm64 and x86_64 0 errors; `SOURCE/RanOnline.sln` Release/Win32
(client + all servers) 0 errors.

## 2026-09-22 (1) — The NPC shop drew through the inventory's equipment column

"I found the bug for mobile when I buy from the npc then it pop up the page sell
next to the inventory but we build the new layout... it's overlaping", then
"maybe you can hide the equipment section like the trading view".

**The cause, measured.** Windows anchored with `UI_FLAG_RIGHT` are placed at
`X_RES - 800 + <authored X>` (`CUIControl::AlignMainControl`), so the authored
layout decides what sits where. `MARKET_WINDOW` and `STORAGE_WINDOW` are at
X=287 and the inventory at X=526: on PC they are exactly adjacent, 239 wide
each. On mobile `CInventoryWindow::MobileSideBySide` puts the equipment doll
*outside* the window to the left, at local X=-231 with a 5-unit plate, and
stretches the title bar over the pair — straight through the 236 units the shop
occupies. At the device's 1280-wide UI that is the shop's whole right half.

**The fix.** The doll column comes off while a window that opens at the
inventory's left is up, which is the shape the trade view already has:

- `CInventoryWindow::MobileShowWear(bool)` hides the doll page, its plate bands
  and its edges, and re-spans the title. Hiding the page is enough for the whole
  subtree: `CUIGroup::Render` returns at an invisible group and `IsNoUpdate()`
  is true for one, so it stops taking taps at the same time.
- The title layout moved out of `MobileSideBySide` into `MobileSpanTitle(left)`,
  which keeps the authored rect on its first pass — each later pass would
  otherwise be measuring the pass before it.
- `CInnerInterface::MobileInventoryWearFrame()` reads the five windows that open
  beside the inventory (`MARKET`, `STORAGE`, `CLUB_STORAGE`, `ITEMBANK`,
  `PRIVATE_MARKET`) every frame from `DxGameStage`, rather than setting on open
  and unsetting on close: each of those has several ways out and one of them
  would always be the one that forgot.

**The trap it cost a build to find.** Re-anchoring the tree with
`SetGlobalPos(D3DXVECTOR2)` carries every child to a new place *at the size its
global rect already had*. The title's local rect said 239 wide, its global rect
still said 475, and `CheckBoundary` then pushed it back inside the screen —
`left = X_RES - 475` — so the bar stayed 410px left of the window it belongs to
while the doll under it was already gone. The rect form of `SetGlobalPos` takes
each child's local size with it; `MobileReanchor()` is now that call, in both
paths.

**The flick, and where the decision belongs.** First cut read the five windows
at the top of `DxGameStage::FrameMove` (`MobileTouchControls`, line 1882) - and
the interface's own `FrameMove`, which is where the tap that opens the shop is
read, is line 1959. So the decision was always one frame behind the tap: the
window came up with the column still on it and dropped it on the next frame,
which the player sees as the inventory flicking open wide and then narrowing.
The call now sits at the end of the mobile block in `Render`, the last moment
before `CInnerInterface::Render`, so within the frame the tap is handled the
column is already off - and on the way out, already back.

**Verified on the emulator** as test01: shop open — shop window clear from 1535
to 2010 px, inventory from 2020, title bar over the inventory alone; shop
closed and the inventory reopened — doll, plate and the wide title all back.
PC solution rebuilds with 0 errors (Release/Win32, 562 warnings, unchanged).

---

## 2026-09-21 (5) — The skill slots use skillframe.png, and the icon is square again

"NOW FOR THE SKILL FRAME CAN WE USE THE skillframe.png"

`skillframe.png` has been cell 2 of the sheet since the set was packed and
nothing ever drew it. The slots borrowed `stick_base.png` — the joystick's
round seat — and the icon was cropped to a disc to fit it, which threw away the
corners of every skill picture. The frame is square and so is the picture.

**What changed** (all in the painted path; the vector fallback is untouched):

- the bezel is `kCellSkillFrame`, at the same 1.62 of the slot half-width, and
  an empty slot keeps it at a third strength instead of being drawn as a dim
  vector disc;
- `drawIconQuad()` draws the picture as a square. Its size comes from the
  frame's window, measured off the art: the hole reaches 0.61 of the half-width
  across and 0.53 down, so 0.53 is what an icon may fill without sliding under
  the frame — `1.62 * 0.53` of the slot half-width;
- the crop into the icon's own baked border stays at 0.84. At 0.90 a grey line
  from that border showed down the right and bottom edges;
- the recharge wipe is a rectangle over the window; `drawDiscBottom` was
  wiping a disc that no longer exists.

The potion row still draws the round seat with a disc-cropped icon — it was not
part of the ask, and its slots are round.

**Measured on LDPlayer:** `out/shots/frame2.png` (the whole arc: square frames,
empty ones dimmed, key numbers still readable) and `out/shots/slot1b.png` (slot
1 at 3x: square icon inside the window, no grey border, frame gold).

---

## 2026-09-21 (5) — The potion row uses skillframe.png

"NOW FOR THE SKILL FRAME CAN WE USE THE skillframe.png" — then, once it was on
the skill arc: "MY fault. can you revert the skill frame back? I mean the
potion slot use the skillframe.png".

`skillframe.png` has been cell 2 of the sheet since the set was packed and
nothing ever drew it. Both rows borrowed `stick_base.png`, the joystick's round
seat, and their pictures were cropped to a disc to fit it. Now the potion row
draws the square frame with a square picture; the skill arc keeps the round
seat and the disc crop, which is what it had.

**What was added**

- `drawIconQuad()` — the picture as a square. Its size comes from the frame's
  window, measured off the art: the hole reaches 0.61 of the half-width across
  and 0.53 down, so 0.53 is what may be filled without sliding under the
  frame — `1.62 * kFrameWindow` of the slot half-width.
- The crop into the icon's own baked border stays at 0.84, as the disc used. At
  0.90 a grey line from that border showed down the right and bottom edges.

**Measured on LDPlayer:** `out/shots/pot.png` — the potion row in square frames,
pictures square inside the window, empty slots showing the world through the
frame; `out/shots/arc_rev.png` — the skill arc round again, disc icons, key
numbers unchanged.

---

## 2026-09-22 (3) — The + buttons ask how many, instead of being pressed twenty times

"in the window ตัวละคร the btn + to update the status it's too small for mobile and
hard to click" — then "when user click the btn, show new window / อัพ status
{status}: {number} / then confirm. this will be more easy and precise?"

**First, the size.** They are authored 15 x 10, a mouse's target: about thirty
device pixels across on a phone, in a column of six, so a miss raises the wrong
stat rather than nothing. They cannot simply be made big — the rows are 18
units apart (POW at Y=71, DEX at Y=89), so anything taller overlaps its
neighbour. 16 is the most that fits and 24 x 16 is the authored 3:2 exactly, so
the glyph grows without stretching: 2.5x the area, through `ReSizeControl`,
because a flip button's pictures are children. Verified by tapping the strip
that only exists because of the enlargement — Pow 54(+4) → 55(+5).

**Then the real fix, which was their idea.** Twenty points is still twenty taps
on a target a finger covers entirely. So the tap asks instead: the client's own
number modal — the one a stack split already uses — opens with

> อัพ สเตตัส Pow : ใส่จำนวนที่ต้องการ (มี 447 แต้ม)

and ตกลง sends `ReqStatsUp(SCHARSTATS)` — the PC's own bulk path, one packet,
nothing new on the wire. The amount is clamped to the pool on this side,
because that overload drops the whole request when the total is too high, which
would look like the button doing nothing at all.

Which stat waits on the window between the tap and the OK, the way
`CInventoryWindow` parks a split's slot: the modal answers with a string and a
caller id and has no room for a payload of its own. Text lives in
`gameintext.xml` as `MOBILE_STATSUP_ASK`, so it translates with the rest, which
means `Gui.rcc` was repacked.

**Verified on the device:** typed 10, confirmed, Pow 55(+5) → 65(+15) and the
pool 447 → 437 (`out/shots/typed.png`, `out/shots/after.png`).

---

## 2026-09-22 (2) — The attack button loses its drawn ring too

"we remove the out line of the joy stick but can you do as well in the attack
btn?"

The attack button carried an amber bloom and a twelve-segment ring - the swing
timer, drawn full because the client never feeds a fraction in - over art that
already has a ring of its own (`atk_ring.png`, drawn at 1.18 of the button).
Two rings, and the drawn one was the brighter. Both are gone on the painted
path; the vector fallback keeps them, since without the sheet there is no other
ring.

**Measured:** the band just outside the painted art, mean brightness 94.2
before and 52.4 after - the background's own level (`out/shots/atk_c.png`,
`out/shots/atk2.png`). What is left is the art as painted.

---

## 2026-09-22 — The load-test crowd has guilds now, and a .cps that crashed the client

"in the load test character we only generate the character without the guild. I
have concern that if normal player have guild and it will be more lagging."

Right concern. A guild costs a second line of text on every name plate and an
emblem beside it, and the fakes had neither, so a field that ran at 60 said
nothing about a real town.

**What each fake gets now:** one of six guilds at random, two in eight none.
`dwGuild`, `dwGuildMarkVer` and `szNick` are all set - `CROWREN::INIT` reads
them and `CNameDisplayMan` draws `szNick` as the club line once `dwGuild` is
not `CLUB_NULL`. Six shared out rather than one each, because that is what a
field looks like and it exercises the mark cache the way a real crowd does; a
different emblem per plate would measure a cache miss no real crowd has.

The emblems are generated locally (a colour per guild, a mirrored shape, a
border) and installed with `DxClubMan::SetClubData` - the same call the
server's reply would make - so six invented club ids never reach the live
server's wire.

**Measured on the emulator:** 60 fakes, `mi:pc-drawn 20.0/frame`,
`ui:names 39.1/frame`, 56-60 fps against 59-60 on the empty field. The emulator
is vsync-capped at 60, so the real number is the iPhone's; the plates are what
changed, and they are now the plates a town has.

**Guild names verified on device** (`out/shots/guild2_c.png`): Legion01, 03, 04
and 06 above the character names, and a `Load028` with none.

**The emblem does not draw, and that is not about the fakes.** Instrumented
`CNameDisplay::Render`: the texture is non-null, the control is visible and its
rect is a sane 16x11 on screen, and nothing appears. The plate even reserves
the space - the school mark moves down a line, which only happens when
`m_bClub` is set. So a real guild member's emblem is missing on mobile too.
Left as a separate job rather than chased mid-task.

**A crash the load test walked into, worth more than the load test.**
`SMATERIAL_PIECE::LoadFile` reads a texture-name length off the stream and
`_alloca`s that many bytes. When the stream has desynced - and
`m_gznhc_earl_body.cps` reads its version as 7, which is not a version this
ever wrote - that length is whatever those four bytes happen to be, and an
`_alloca` of a few hundred megabytes does not fail, it walks off the stack:
SIGSEGV inside `LoadFile` with nothing to say why. A real player wearing that
piece would take down every client that could see them. The length is now
checked against a believable maximum (260, a file name) and the piece dropped
instead, and the unknown-version branch no longer falls through to
`LoadTexture` with nothing read.

---

## 2026-09-21 (11) — The mall's เติมเงิน button carries the account

"in the topup btn in the mall page. I think we already done something like pass
the id to the topup page... I wanted the mobile do the same."

They were right, and it is in the PC client. Two places already append the
logged-in account to the top-up address as **`ref1`**:

- `CWebLinkWindow::CreateWeb` — the embedded browser,
- `CInnerInterface`'s external-browser fallback.

Both build it the same way: `RANPARAM::GETUSERID_DEC()`, separator `&` if the
address already carries a query and `?` if it does not, id appended as it is,
http URLs only.

The mobile `ITEM_SHOP_TOPUP_BUTTON` opened the bare
`https://ran-legacy-m.com/topup/`, so the page had no idea who was topping up.
It now builds the URL character for character the way those two do, so one page
serves both clients.

**Verified on the device:** tapped เติมเงิน in ร้านค้าไอเทม and read the intent the
browser was handed - `ran-legacy-m.com/topup/?ref1=<account>`. Read back
redacted; the id itself was never printed.

---

## 2026-09-21 (10) — Opening a window stalled for a third of a second: a whole font atlas per letter

"when I open the menu it's lacking a bit... or even when I open the mall or
inventory."

**Measured, not guessed.** The once-a-second FRAME average is the wrong shape
for a hitch, so `ran_app.cpp` now prints a line for any frame over 33 ms: the
split, the four sections that grew most in that frame, and what texture upload
cost. `gl_render.cpp` counts upload time per frame and names any single upload
over 5 ms.

First open of the menu, on the emulator:

```
SLOW frame 311.5 ms = update 1.0 + render 310.5 + present 0.0
                    | texture upload 285.6 ms (11) | world 1.6ms ...
SLOW upload 25.6 ms: 2048x2048 level 0 format 28     (x11)
```

Eleven 2048x2048 A8 textures in one frame. Format 28 is `D3DFMT_A8` and those
are the **font atlases** - one per font, 4 MB each.

**Why the whole atlas moved.** `RanD3DXFont` wrote each glyph through
`LockRect(0, &lr, NULL, 0)`. A lock with no rectangle says the whole surface
was rewritten and the shim believes it, so the next sample re-uploaded all
4 MB for the sake of one letter. The frame that opens the menu touches eleven
fonts for the first time, so it did that eleven times. `ensureAtlas` made it
worse by locking the whole surface to memset it to zero - over a vector that
was already zero.

**Three changes:**

- both glyph writers lock the glyph's own rectangle. `pBits` lands on its
  top-left corner and `Pitch` is still the surface's, so only those rows move.
- `ensureAtlas` no longer locks and memsets what was already zero.
- `RanGLR_AllocClearTextureLevel`: a texture never uploaded whose dirty
  rectangle is a sub-rect gets its storage allocated with no data and cleared
  through a framebuffer - a GPU clear instead of a 4 MB transfer - and then
  only the written rectangle is sent.

**And the swizzle.** `D3DFMT_A8R8G8B8` is B,G,R,A in memory and GLES has no
BGRA upload, so every one of those textures was walked pixel by pixel into a
fresh heap buffer first: 4.2 million iterations and a 16 MB allocation for a
2048 sheet. GLES 3.0 swaps channels at sample time for free, so the bytes now
go up untouched with `GL_TEXTURE_SWIZZLE_R/B` set. Same for X8R8G8B8, X8B8G8R8
and R8G8B8, which also uploads as GL_RGB instead of being expanded to four
bytes. The partial-rect path uploads raw for the same reason - it has to match
the swizzle, or a rewritten rectangle would come out inverted against the sheet
around it.

**After, same tap:** no 300 ms frame at all. The worst frame opening the menu is
36 ms with **zero** texture uploads, and it is the client's own `update`, not
the port. Text verified on the device: `out/shots/menutext_c.png` (the whole
menu grid, Thai labels, title, world name plates) and `out/shots/inv_c.png`
(the inventory, อุปกรณ์สวมใส่, item icons, the money row).

---

## 2026-09-21 (9) — The iPhone lost half its frames to the on-screen controls

"now I plug in the iPhone even I did not close the crowd but it's with 32 fps...
can you check why?"

**It is not the crowd and it is not the world.** Read off the phone over USB,
standing in a field: `mi:pc-drawn 0.0/frame` — no other players drawn at all —
nine mobs, ten name plates. 31 fps, 26-30 ms a frame, of which **engine cpu
23-27 ms**, submit 2.7 ms, present 0.0, swap 0.2.

**The overlay was the whole gap.** A/B with the `nohud` diagnostic, same spot,
same second:

| | fps | engine cpu |
|---|---|---|
| overlay drawn | 31.0 | 23-27 ms |
| `nohud` set | 60.1 | 13 ms |

Set and cleared twice, both transitions measured. A HUD that issues five draws
a frame was costing 12-14 ms.

**Why: 45 write-then-draw pairs into one buffer.** Every painted control - each
button cell, each slot bezel, each icon - wrote its handful of vertices into
`g_texVbo` and drew from it immediately. On Apple that write lands on a buffer
the GPU is still reading and the driver stalls until it is free; the stall was
measured at ~450us in this codebase once before ([[ios-buffer-updates-stall]]),
and 45 of them is exactly the missing 12-14 ms. It is invisible to every shim
instrument because the overlay has its own GL path - submit stayed at 2.7 ms
throughout.

**Fix:** a ring of 128 buffers, so one is not written again for about three
frames. Attribute pointers are VAO state and name the buffer they were set
against, so each buffer carries its own VAO. Also `uTex` was fetched by name
with `glGetUniformLocation` on every painted control; it is looked up once now.

Android regression-checked on LDPlayer: the overlay draws exactly as before,
60 fps. **Not yet verified on the iPhone** - the fix is in iOS build 1.0.143,
published in patch 534, and cannot be measured until the phone updates through
SideStore.

---

## 2026-09-21 (8) — The upload set is what the SERVER lacks, not what the run added

"check the blob file it's too much. maybe update the script check from the
server too?"

It was too much: 824 MB staged, of which 101 MB was reachable from the current
manifest. Seventeen publishes in one sitting had staged seventeen APKs.

**The old rule** was "accumulate every blob added since the last confirmed
upload". That is safe - re-sending a blob is a no-op, because the name is the
hash - but it keeps blobs that no manifest names any more, and after a few
publishes that is most of the set.

**The new rule** is the honest one: stage a blob when THIS manifest names it
and the server does not already have it. A blob the current manifest does not
name cannot be asked for by a client reading it, whatever produced it.
`make-manifest.js` already fetched the live manifest to see whether the last
set had landed; it now reads its file list too (memoised - it is 3.6 MB and was
about to be fetched twice).

**Two guards, because being wrong here means a client failing on a file that
looks perfectly fine locally:**

- the server unreachable, or its manifest unreadable, and none of it runs: the
  old behaviour stands. Proved by publishing against `--live-base
  http://127.0.0.1:9/` — "server is at (unreachable) ... keeping the set", no
  drop line.
- a blob this manifest needs that the live manifest claims is up is still
  HEADed before it is dropped, so an upload that died halfway is caught rather
  than trusted. Only blobs already staged or added this run are checked — a
  handful, not the whole manifest.

**Result:** `dropped 19 blob(s), 723.0 MB`, leaving `data/gui/Gui.rcc`,
`textures/gui/mobile_hud.dds` and the APK. Re-verified afterwards: the
signature still verifies, the three blobs hash to their names, nothing needed
by 532 is missing from both the server and the set, and nothing staged is
unneeded.

---

## 2026-09-21 (7) — Patch 532 checked, and the iOS build caught up

"can you check the patch?" — then "what about ios?".

**The patch verifies.** `manifest.sig` checks out against the P-256 key
compiled into the APK; all 23,370 entries resolve to a blob; the 22 staged
blobs each hash to their own filename; the APK's sha256 and size in the
manifest match `out/RanMobile.apk`; no entry path is absolute or contains
`..`. Version 532, minApk 1, minIos 142, APK versionCode 142 "V120".

**Only 101 MB of the 824 MB has to go up.** Diffed against the live manifest
(the host was still serving 511 / V102 / minIos 124): the server lacks exactly
three of this manifest's blobs — `data/gui/Gui.rcc` (50.3 MB),
`textures/gui/mobile_hud.dds` (6.3 MB) and the APK (44.2 MB, fetched from
`blobs/<sha256>` like everything else). The other 19 blobs in `out/upload` are
intermediate builds from today's repeated publishes — 16 old APKs and two older
HUD sheets — which were never on the server and which no client will ever ask
for. `out/upload` accumulates them because the tool assumes anything staged may
already be up. Sending all of it is only slow, not wrong.

**iOS was a build behind, and now is not.** The staged `.ipa` was 1.0.127 from
yesterday, because CI builds it from the pushed SOURCE and MOBILE repos and
neither had been pushed since. Both are pushed now (SOURCE `3cc0788`, MOBILE
`d20f6a7`); the push filter on `native/shim/**` fired the workflow by itself,
it went green in 3m02s, and `make-ios-source.js` published the artifact as
**1.0.142**, the same build number as the Android APK. Republishing then raised
`minIos` to 142 on its own — it follows the build in `ios/source.json`. The APK
was not rebuilt, so its versionCode did not move.

---

## 2026-09-21 (6) — The item shop cell is out of the menu grid

"now the ไอเท็มช็อป icon in the menu we do not need this it's stale."

`MENU_ITEMSHOP_BUTTON` (label index 10 of `MOBILE_MENULABEL`) is out of
`nORDER` in `CBasicGameMenu::MobileArrangeMenu` and hidden explicitly, beside
the walk/run toggle and the collapse arrow — without that it would still draw
at its authored strip position, since the grid only shows what it places.

The button and its handler stay: the id is still switched on in
`TranslateUIMessage` and `ITEMSHOP_WINDOW` can be opened from elsewhere, so
nothing was removed that something else might reach for. The cash shop on this
server is the web page เติมเงิน opens.

**Measured on LDPlayer** (`out/shots/menu2_c.png`): 16 cells, 5/5/5/1, no gap
and no icon left behind — กระเป๋า ตัวละคร สกิล ปาร์ตี้ คลับ / ภารกิจ เพื่อน แผนที่
คำพูด ของจากเว็บ / ร้านค้า หาปาร์ตี้ อันดับ แข่งขัน บอส / ระบบ.

---

## 2026-09-21 (5) — skillframe.png on the potion row, and no gap on either row

"NOW FOR THE SKILL FRAME CAN WE USE THE skillframe.png" — then "MY fault. can
you revert the skill frame back? I mean the potion slot use the skillframe.png"
— then "now fix both for the skill and potion slot they both have gap between
the frame and the icon".

`skillframe.png` had been cell 2 of the sheet since the set was packed and
nothing ever drew it; both rows borrowed `stick_base.png`, the joystick's round
seat. Now the potion row draws the square frame with a square picture
(`drawIconQuad`), and the skill arc keeps the round seat and the disc crop.

**The gap was one number doing four jobs.** Neither frame is centred on its own
canvas and neither hole is square. Walking out from the centre of each cell
until the alpha comes up:

```
skillframe.png   L 0.619  R 0.614  T 0.589  B 0.534
stick_base.png   L 0.641  R 0.638  T 0.641  B 0.576
```

So each window has a half-size (the mean of the opposite pair) and an upward
offset (half their difference). Sizing a picture to the smallest of the four
left it short of the frame on the other three sides. `kFrameWin*` and
`kSeatWin/Up` now carry both numbers, `drawIconQuad` takes a half-width and a
half-height rather than one radius, and the skill icon is sized from the SLOT
(found by position, the way `iconPressScale` finds it) instead of from the
size the client authored it at — which was about four fifths of the slot and
left a dark ring of bezel showing all round it. The recharge wipe follows the
picture for the same reason.

**Measured on LDPlayer, 3x crops:** `out/shots/s2.png` — the skill picture
meets the seat's inner bevel; `out/shots/p2.png` — the potion picture reaches
the frame on all four sides. Before: `out/shots/s1.png`, `out/shots/p1.png`,
which show the ring of dead space this closed.

---

## 2026-09-21 (4) — The white halo round the joystick knob was a packing bug

"in the joy stick I dot like the white outline in the knob and the yellow
outline on the joy stick can you remove?"

**The white ring was never art.** `stick_knob.png` arrives as flat RGB on a
white ground (corner 254,254,254, no alpha channel at all), and `hud-pack.js`
handled an alpha-less control by borrowing a silhouette from the round BUTTONS
- a full disc with four diamond studs. The sphere does not fill that
silhouette, so the white background between the two survived into the sheet:
a white ring with four points, drawn round the knob on every device.

`bgMask()` now cuts a flat-RGB control from its own background first - a flood
from the border, seeded from the median border colour rather than a corner
(one corner of this canvas sits at 211 where the sphere's shadow reaches it,
which rejected the whole image), tolerance loose enough for a dark sphere on
white, and a check that the fill covers between a tenth and four fifths of the
canvas. The borrowed mask stays as the fallback for the gradient-ground case it
was written for.

**The code-drawn amber went; the painted gold stayed.** Removed from
`drawStickShapes`: the amber rim at full deflection, the heading wedge below
it, and the amber ring drawn over the knob while held.

The ring's own gold is `stick_base.png` and it stays exactly as painted - as
does every other bezel, since that cell is also what the skill slots and the
potion row draw. A steel copy of the seat was packed and wired to the stick for
one build and taken straight back out: "NOT THE GOLD AMBER FROM THE GUI OUT
LINE". The overlays were the complaint, not the art.

**Measured on LDPlayer:** gold ring, no white halo round the knob
(`out/shots/stick3.png`); held at full deflection through a protocol-B
`sendevent` drag, no rim and no wedge. The skill slots, attack button, pick-up,
PK, auto and F1-F4 are gold as before (`out/shots/arc_now.png`).

---

## 2026-09-21 (3) — The skill page is four buttons now, not two arrows

"now for the hub btn skill page up and down. we change it to btn F1 F2 F3 F4
better." Then: "the position of the btn skill page it should be under the skill
slot 1. linier f1 f2 f3 f4".

**Why it is a simplification, not a rewrite.** The two arrows already sent
`DIK_F1 + nPage` — `CSkillTrayTab::Update` watches F1..F4 and swaps the visible
tray itself. The arrows had to read the tray's index, step it and wrap it. A
button per page names the page it wants and there is no counter to keep.

**What changed**

- `touch_ui.h`: `RANTOUCH_SLOT_PAGE_PREV/NEXT` → `RANTOUCH_SLOT_F1..F4`
  (-2, -3, -10, -11). F3/F4 are appended to `g_buttons` rather than inserted,
  because the indices below them are written out in the group table, the
  outline table and the layout; their order on screen comes from the layout.
- `DxGameStage.cpp`: the slot names its own page, `nKey = DIK_F1 + nPage`.
- `placePageRow()` puts the four in a row under skill slot 1 — the thing a page
  turn replaces is that row of skills, so the control belongs against it. The
  arc is the client's and is handed over every frame, so the row is placed from
  `RanTouch_SetSkillCircles` as well as from `layout()`; calling `layout()`
  there would recentre the stick under a thumb that is holding it.
- The amber readout plate that sat between the arrows is gone. The lit button
  is the readout: `RanTouch_SetSkillPage` lights the one that matches.

**The sheet grew.** `mobile_hud.dds` was 4x4 at 256 and exactly full. Four
buttons with a lit state each is 22 controls, so it is now 5x5 (1280x1280,
6.25 MB) with `kHudCols = 5`; `tools/icon-art/hud-pack.js` holds the same
order and the two must not drift. `page_up.png` / `page_down.png` are out of
the sheet, still in `RanIcon/`.

**Measured on LDPlayer, in the world:**

| what | result |
|---|---|
| fresh login on page 1 | F1 lit, F2-F4 dark (`out/shots/row.png`) |
| tap F2 | F2 lit, F1 dark |
| tap F4 | F4 lit, and the arc goes empty — page 4 holds no skills (`out/shots/arc4.png`, mean diff 12.6 against page 1) |
| tap F3 after the move under slot 1 | F3 lit at the new position (`out/shots/row3_c.png`) |
| HUD editor: select the group | one outline round all four |
| HUD editor: drag | the row moves as one and the outline follows |

---

## 2026-09-21 (2) — Zoom died in a crowd for the same reason rotation did

"since we fix the rotate camera in the crowd and the problem now is happend
same as zoom in zoom out."

**Same bug, other half of the code.** `RanTouch_PointerDown` opens with a guard
so that an open window keeps its press instead of the pad eating it:

```
if (RanUI_PointInControl && RanUI_PointInControl((int)x, (int)y)) return 0;
```

It returns **before `addTouch`**, so a finger that lands on a control is never
recorded in `g_touch` at all. A name plate is a control. In town there is a
plate under almost every pixel, so neither finger was ever recorded,
`unclaimedCount()` never reached 2, `g_pinch` never started, no wheel event was
ever sent, and the camera could not be zoomed — exactly the shape of the
rotation bug, which was fixed by `RanUI_PointInDragControl` leaving
NAME_DISPLAY_MAN out of the drag decision.

**Fix:** the guard now asks the drag question (plates excluded), falling back to
the plain one only where the client does not export it. Windows still keep their
press; plates no longer swallow the finger before it is recorded.

**Measured on LDPlayer, Sacred Gate with the fake crowd, a two-finger
`sendevent` pinch (protocol B on /dev/input/event4 — `input` cannot do two
fingers):**

```
RANTEST guard x=580 y=350 plain=1 drag=0
RANTEST guard x=700 y=350 plain=1 drag=0
```

Both fingers landed on plates and on nothing else, so the old guard dropped
both. The camera then zoomed: the same lamp glow measured 694 bright pixels
before the pinch and 393 after (`out/shots/zoom_before.png`,
`out/shots/zoom_after.png`) — 0.75 linear. Instrumentation removed afterwards.

**Noted, not changed:** spreading the fingers zooms *out*. `CameraZoom` takes a
zoom-OUT amount and `fZoom += m_fVELOZOOM*dz/1000`, so this is what a PC wheel
does; it was not part of the report and is one line to flip if it feels
backwards on a phone.

---

## 2026-09-21 — A costume IS a material: that is how EP9 wears one

Asked: "why the costume item can be the material for upgrade item? also how do I
do the costume with the item?"

**Not a bug - it is the mechanism.** There is no costume slot in this client:
`EMSLOT` has no avatar slots, and `INVENTORY_PAGEWEAR_EX` is the extra
accessory row, not costumes. A costume is *applied onto* a real piece of gear:
`GLCharacter::ReqDisguise` stamps the costume's id into the target's
`SITEMCUSTOM::nidDISGUISE`, so the item keeps every stat and takes the
costume's look. `ReqCleanser` takes it off again. On the PC that is done by
carrying the costume and right-clicking the gear - `ReqInvenDrug`'s held-item
branch dispatches a DISGUISE item straight to `ReqDisguise`.

A finger cannot right-click while carrying something, which is why every
"apply this onto that" action lives in the อัพเกรดไอเทม window on mobile, and
why a costume shows up in its วัตถุดิบ (material) slot.

**How to do it on the phone:** tap the gear (or the costume) → **อัพเกรด** →
the window opens with that item already in the right slot (a costume lands in
วัตถุดิบ, gear lands in ไอเทม) → tap the empty slot to pick the other half →
อัพเกรด.

**Fixed while reading it:** `CMobileEnhanceWindow::CanTake` offered a costume
for any suit. ReqDisguise is stricter, so the window now asks what the server
asks: the same kind of gear (`emSuit`), a class both admit, not onto another
costume, and not onto something already wearing one. A row that can only fail
is worse than no row.

Verified at the code level against `ReqDisguise` - the new test is a strict
subset of its rules, so nothing the server accepts is hidden. Not device-proven:
the test account has no costume-and-matching-gear pair.

---

## 2026-09-20 (7) — Skills pick up on a long press too, and the drop no longer clears the slot

"what about skill?" - the same gesture, on the skill window and the arc.

**Two changes, both `#ifdef RAN_MOBILE`:**

- `CSkillSlot`: a skill is taken on RB_DOWN as well as LB_DOWN, so a long press
  lifts it while the finger is still on it and the icon can be dragged onto the
  arc. The tap already worked - that is the PC's own LB_DOWN - but a player who
  learned "hold to pick up" in the bag holds here too.
- `CBasicSkillTray`: RB_UP on a quick slot **assigns** whatever is carried
  instead of clearing. A long press is the touch build's right button, so a
  finger that lifted a skill and let go over a slot arrived as RB_UP - and that
  threw the skill away at the exact moment it was being assigned. With nothing
  in hand RB_UP still clears the slot, which is how a slot is emptied.

**Verified on the device, three gestures, one screenshot each:**

| what | result |
|---|---|
| long press Moon Strike, tap circle 1 | circle 1 draws Moon Strike |
| long press Shadow Peirce, drag to circle 1, release (`input motionevent`) | circle 1 draws Shadow Peirce - no clear |
| long press circle 1 with an empty hand | circle 1 is empty again |

---

## 2026-09-20 (6) — Long press picks it up, tap puts it down

"when I do long press on the item on the skill it should already like pick it up
in hand like left click". Asked where the action menu should go then: tap opens
it, long press picks up.

**The final rule** (`CInnerInterface::MobileItemTouch`):

| gesture | empty hand | full hand |
|---|---|---|
| long press | LIFT on the DOWN edge - item is in the hand while the finger is still on it, so the same press carries it | - |
| release after a long press | stays in hand if it never moved | DROP where it let go |
| tap | SHEET (use, equip, split, drop) | DROP (place, swap, split) |

Two details that are not obvious and both cost a build to find:

- the release of the press that lifted is not a drop, or a long press that did
  not move would put the item straight back. The helper remembers the cell it
  lifted from;
- that same key tells a tap from a short drag. Under the hold's 450 ms a moved
  finger arrives as a plain left press-and-release, and opening the actions for
  whatever cell it ended on is not what the player asked for.

**Measured on the device**, instrumented (`ITEMTOUCH ret=...`), then cleaned:

- long press on the bread at (0,2): it greys out in its cell - that is the
  client's "in hand" - and the next tap on an empty cell puts it there;
- `ret=2` (DROP) while carrying, `ret=3` (SHEET) with an empty hand, and the
  sheet is on screen in the screenshot;
- a drop onto an occupied cell is a SWAP, so the hand stays full - that is the
  PC's own behaviour (`SNETPC_REQ_INVEN_EX_HOLD`) and it is why a run of taps
  looked like nothing was clearing.

Skills need no change: `CSkillSlot` already picks on the press and
`CBasicSkillTray` assigns on the release, including into an empty arc circle.

---

## 2026-09-20 (5) — Items and skills move on a tap, the way the PC's click does

Asked: on the PC a click picks an item or skill up and another click puts it
down; make the phone do that with one press, drag and let go included.

**What the PC actually does** (read before touching anything): there is no
press-and-hold drag. `CItemMove` draws whatever `GET_HOLD_ITEM()` returns at the
cursor, and `ReqInvenTo` / `ReqSlotTo` / `ReqStorageTo` both LIFT (empty hand)
and PLACE (full hand) - so a left click picks up and the next left click drops.
Skills are the same shape: `CSkillSlot` puts the skill on `CSkillWindowToTray`
on LB_DOWN, and `CBasicSkillTray` assigns it to a quick slot on LB_UP.

**What mobile was doing.** A tap on a bag cell opened the action sheet and never
moved anything; lifting needed a deliberate long press (the touch layer's right
button). That is not how a bag is rearranged on a phone.

**The rule now**, in one place - `CInnerInterface::MobileItemTouch`:

| gesture | empty hand | full hand |
|---|---|---|
| tap | LIFT | DROP (place / swap / split) |
| press, move, release | LIFT on the press, DROP where it lets go | DROP |
| long press | SHEET (use, equip, split, drop) | DROP |

The lift happens on the press so the icon has something to follow during a
drag, and the release of *that* press is not a drop - the helper remembers
which cell the lift came from, or every tap would put the item straight back.
Callers: `InventoryWindow` (bag cells and worn slots), `StorageWindow`. Skills
already worked this way once the tap reached the slot; nothing to change there.

Also fixed in passing: `StorageWindow` had a copy of the mobile block pasted
*inside* the ALT-preview branch, where it was dead code on a phone and
swallowed the preview.

**Verified on the device**, bag cell (0,0) holding bread: tap the cell, tap an
empty cell six rows down - bread moves, (0,0) is empty, no long press anywhere.
Instrumented runs confirmed each edge: `ITEMTOUCH lbd=1 ... ` then `ITEMLIFT
touch=1 hr=00000000`, and the drop `touch=2`. Skills: `SKILLPICK 12,11` from the
skill window, then `SKILLDROP slot=0 carry=12,11` on an EMPTY arc circle, and
the circle then draws the skill. All instrumentation removed before the build.

---

## 2026-09-20 (4) — The แข่งขัน cell strobes because the blink covers it

"it still keep flicking! see the real code and do the real analysis!"

**Instrumented, then measured.** A test build forced the alarm on
(`SetCompetitionButtonAlarm(TRUE)` + `ShowGroupBottom`) so the cell could be
watched without waiting for a live event, and logged the button every second:

```
CMPBLINK alarm=1 vis=1 img(vis=1 rc 1047,311 56x56) blink(vis=1 rc 1047,311 56x56)
CMPBLINK alarm=1 vis=1 img(vis=1 rc 1047,311 56x56) blink(vis=0 rc 1047,311 56x56)
```

That is the whole story in two lines: **the blink child occupies the same
56x56 rect as the icon**, and `Update` toggles it every `BLINK_TIME_LOOP`
(0.2 s). On the PC that button is a 35x59 sliver in the corner and the blink is
a small highlight on it; in the menu grid it is a full cell, so whatever the
blink draws replaces the icon five times a second. Re-pointing the blink at the
lit ring (session 3) stopped it being a *different icon* - screenshots showed
swords, then swords under a bright ring - and did not stop it strobing.

**Fix:** on mobile the blink never draws. `#ifdef RAN_MOBILE` in both
`CCompetitionNotifyButton::Update` and `SetButtonAlarm`; the PC path is
untouched. The event is still announced in chat, and the cell is only in the
grid while the event is open.

**Verified on the device with the alarm forced on:** `blink(vis=0)` on every
log line, and eight consecutive frames of the cell are pixel-identical (258
bright pixels, same bounding box each time). Test hacks reverted before the
patch build.

**Patch 520 / APK V108 (versionCode 130).** Upload set is now 8 blobs, 319 MB,
accumulated since 512 - the host still serves 511 / V102.

---

## 2026-09-20 (3) — The แข่งขัน icon was swapping itself for the alert plate

Asked: "why icon แข่งขัน flick king switch with other?" — then, while I was
looking: "it switch with other icon!"

**Cause, read from the data.** `_inner_competitionui.xml` gives the competition
notify button two children pointing at *different* pictures in
`mobile_icons.dds`:

- `COMPETITION_NOTIFY_BUTTON_IMAGE` -> cell (768,128), the crossed swords
- `COMPETITION_NOTIFY_BUTTON_BLINK` -> cell (768,256), the **alert plate**

`CCompetitionNotifyButton::Update` toggles the blink child every
`BLINK_TIME_LOOP` (0.2 s) while an event is open, so five times a second the
icon was replaced by a different icon. It only happens during a live event,
which is why it reads as random.

**Fix.** The blink now points at cell (640,256) - the lit ring, transparent in
the middle - so the icon stays itself and lights up. Both cells were cropped
out of `atlas.png` to confirm which art each coordinate holds before editing.
`Gui.rcc` repacked (108 entries, verified on read-back) and pushed.

**Verification is partial and this says so.** The blink could not be caught on
screen: the icon only enters the menu grid when the server says an event is
open, and a test build that forced the blink and forced the icon "wanted" still
did not bring it into the grid. Cause and data change are measured; the live
blink is not. Both test hacks were reverted before the patch build.

**Shipped as patch 519 / APK V107 (versionCode 129)**, staged in
`native/out/upload` (7 blobs, 275 MB, accumulated since 512). The live host is
still at 511 / V102 - nothing here has reached a phone yet.

---

## 2026-09-20 (2) — Reset gave back the outline, not the picture

Asked: "I try on the qbox upscale and reset it not really reset. can you stop
guessing?"

**Measured, not eyeballed.** On LDPlayer, in the HUD editor: quest box selected,
size+ three times, then reset. The overlay's outline came back to its exact
baseline (ring 94 px tall both times) while the icon's own art stayed big -
49x47 authored, 38x38 at the old baseline, and still oversized after reset. The
drawn art and the control's rect had parted company.

**Why.** The size buttons were applied as *the change since last frame*
(`fSTEP = want / last`), because the layout pass runs every frame on live rects
and multiplying by the scale each time is scale^n. Stepping up works; stepping
back down does not undo it:

- `CUIGroup::SetGlobalPos(UIRECT)` rewrites every child from its **local** rect,
  so a child's size is silently restored to the authored one mid-pass;
- `CheckProtectSize()` refuses to shrink a control past its protected size.

So a step of 0.75 lands on some pieces and not others, and no sequence of steps
gets back to where it started.

**The fix is to stop accumulating.** `CUIControl::MobileScaleTree(pRoot, scale)`
(`UIControlEx.cpp`) remembers what each piece of a control tree was authored at
- offset from the root and size - and sizes it to `authored * scale` every
frame. Scale 1 is the authored size exactly, whatever happened in between. It
re-learns a control's authored rect when the size it finds is not the size it
last handed out, which is what a rebuilt or re-laid-out control looks like;
learning on *every* scale-1 frame was tried first and was wrong - it re-learned
offsets that had already been scaled, and the quest box's picture walked 51 px
out of its own outline.

Both call sites now use it: the corner icons in `DxGameStage.cpp` and the skill
slots in `SkillTrayTab.cpp`, each sized before the move (so the arc places the
slot by how big it really is) and again after it (the group move re-anchors
children from their unscaled local offsets).

**Verified on the device, by pixel measurement, for all three kinds:**

| | baseline | +3 sizes | after reset |
|---|---|---|---|
| quest box art | 49x47 at (2104,99) | grown, art inside its ring | **49x47 at (2104,99)** |
| skill slot 3 | authored | grown | back to authored size and place |
| potion slot 1 | ring 442..605 | grown | ring 442..605 |

The editor toolbar's own position resets with it.

**Not shipped yet.** The live host still serves **511 / APK V102** and the new
vehicle sheet's blob still answers 404; `native/out/upload` carries 513 onward
plus this fix and has to be uploaded before any of it reaches a phone.

---

## 2026-09-20 — Every character name was "inappropriate", and the mark over the gate

**Patch 517 / APK V105 (versionCode 127) / iOS 1.0.127.**

**Reset left two controls behind, and the pair still dragged together.**
`editDefaultsAll` never learned about `g_cornerAdj`, so reset put everything
else back and left the quest box and the party frame where they had been
dragged; and the move handler had branches for a skill slot and a potion slot
but not for a corner icon, so dragging one took the other with it. Both fixed
and verified on the device.

**And the new vehicle art is not on anyone's phone yet.** Measured against the
live host: it serves **version 511 / APK V102**, and the new sheet's blob
(`9b85927c…`) answers **404**. The live manifest still names the old sheet
`c61c3946…`. Patches 513 onward are built and staged but have not been
uploaded - `out/upload` is 136 MB and carries them.

**Then the half that was actually asked for: every button sizes on its own.**
"It should be separate for any button" - size is per CONTROL now, not per
group. A slot carries its own scale beside its own offset, the editor gives
size to whatever is selected, and the two corner icons became two things rather
than one: each picked, outlined, moved and sized alone. The saved arrangement
carries it - groups first, then every slot as (dx, dy, size), 110 floats.

Three bugs fell out of doing it, each measured on the device:

* `g_adj` had **eight initialisers for an array of eleven**. Menu, potion row
  and corner icons started at scale 0 and opacity 0 - and a zero scale is a
  button with no radius, so the menu button drew nothing and could not be hit,
  and the potion bezels drew at zero alpha. Nothing applied those two fields to
  those groups until this work did, which is why it had never shown. Defaults
  live on the members now, so the array cannot rot when a group is added.
* The scale was applied to the LIVE rect every frame, which compounds: 1.3 a
  frame is 1.3^n, and a slot ballooned off the screen inside a second and
  looked deleted. It is applied as the change since last frame.
* The toolbar's number read the group's size while the buttons changed the
  slot's, so it sat at 100 while the slot grew.

Verified: selecting skill slot 3 and pressing + three times grew that slot
alone, the readout showed 130, and cancel put it back.

**New vehicle art, and the three controls the editor could not resize.** The
vehicle cell was repacked from the delivered PNG (`hud-pack.js`, then
`topdds.js` which now takes its source as an argument rather than hard-coding
the menu atlas).

"Make it big or small ... some of them did not work" was three separate
reasons:

| control | why it ignored the size |
|---|---|
| menu button | `layout()` walks a six-row table with a loop that ran to five - the menu was the sixth |
| quest box / party frame | the client read `RanTouch_GetCornerAdjust`'s scale into a variable it never used |
| skill slots | the scale went to the arc RADIUS, which spreads the slots; the slots kept their authored size |

The menu button is verified on the device - three presses of + grew it, cancel
put it back. The other two are compile-verified only: this test character has
no quest box and no skills bound, so neither control is on screen to exercise.

**The name filter refused everything.** `SlangFilter::addSlang` builds its tree
key with `_snwprintf(buf, n, L"%s", slang.c_str())`. `%s` in a WIDE format
string means `char*` to bionic, where MSVC reads it as `wchar_t*` - so a UTF-32
string was read a byte at a time and stopped at the first zero: U+0E41 became
"A". `moblogic2.bin` bans about 53,000 code points by range, and between them
they cover every low byte there is, so all of ASCII ended up in the tree. Any
name came back `******` and the create screen said
ชื่อตัวละครมีคำไม่เหมาะสม.

Measured rather than guessed, in four steps: the chat filter passed "abcdef"
while the name filter starred it; the ranges parsed correctly (AC00..D7A3 and
twelve more, none of them ASCII); the matches were on 0041..0046 - the letters a
hex string is made of, which is what a UTF-32 buffer looks like read as bytes;
and a bounded copy in place of the printf fixed it. "abcdef" creates.

**The mark on the character pages.** `LOGIN_MARK` is drawn centred, which on a
phone is where the character stands - a gold disc hanging over the gate behind
them. Hidden on select and create, mobile only; the login page keeps it.

**A trap worth naming: a wide printf with `%s`.** MSVC and bionic disagree about
what that means, and the failure is silent and data-dependent. Worth grepping
for the next time something wide comes out truncated.

---

## 2026-09-19 — The potion row joins the round HUD, and three things the screen still carried

**Patch 509 / APK V101 (versionCode 123) / iOS 1.0.123.**

Asked: the quest tile on screen is stale, the potion slots are not in the skill
slots' style, the skill bezel can reuse `stick_base.png`, and the menu window's
top bar looks wrong.

**The quest tile.** A third copy of a notification: the quest box sits in the
corner with its own alarm and ภารกิจ is a cell in the menu. Hidden in
`DxGameStage::MobileKeepMenuIconsAbove`, which runs AFTER
`CInnerInterface::FrameMove` - hiding it before that only lasted until the
interface ran and turned it back on.

**The potion row.** Six slots moved into a row under the attack disc, drawn by
the overlay the way the skill slots are: `stick_base` as the bezel, the item's
own picture cropped into it. The client keeps the slots, so a tap still drinks.
Three measured corrections after the first build:

| Seen | Cause |
|---|---|
| squares and shortcut letters redrew over the discs | a text box draws on `IsVisible()` alone, and the slots re-show their art as they update - so the hide moved out into `MobileHideSquares()`, called after `FrameMove` |
| slots overlapped each other | the bezel is drawn at 1.62x the icon radius; a 2.25x step is less than two bezels. 3.45x |
| the first slot sat on the pick-up button | `touch_ui` puts pick-up directly under the attack disc. The row starts at 1.35 radii left instead of 0.5 |

**The editor's offsets are in `g_unit`, not fractions.** `kGrpPotion` was missing
from `groupAt`'s order list, so the row could not be grabbed at all; once added,
the first drag threw it several screens away. `g_adj[].dx` is in units of
`g_unit` - that is what makes a drag feel the same on a phone and a tablet - and
the tray was reading it as a fraction of the surface and multiplying by the
width. `RanTouch_GetPotionAdjust` now converts on the shim side, where `g_unit`
lives. Verified on LDPlayer: drag moves the row, cancel restores it.

**iOS caught a GLES2 call.** The potion hand-over asked the driver for the
texture's size with `glGetTexLevelParameteriv` / `GL_TEXTURE_WIDTH`, neither of
which exists in GLES2 - the Android NDK headers have them, Apple's do not, so
only CI saw it. It also bound a texture behind the bind cache's back. Both gone:
`RanGLR_TextureSize`, the way the skill icons already ask.

**Then two corrections, asked after seeing it.** The row showed only two slots
because an empty one reported nothing and so got no bezel; every slot reports
itself now, with a zero texture when it is empty, and the overlay draws the
bezel and leaves the middle blank - the row is six buttons long whatever is in
them. And it moved from under the attack disc to on top of the skill cluster:
`RanTouch_GetSkillBounds` hands the client the box the skill slots occupy, so
the row caps them, centred, and follows when the player moves the slots in the
HUD editor.

**Then the editor itself, asked for after using it.** Four changes:

* The menu WINDOW is off the editor's list. It is only on screen while a choice
  is being made, so arranging it means arranging something that is never there
  while playing; the menu BUTTON is its own group and always there. The
  client's `RanUI_MenuWindowRect` / `Move` pair went with it.
* Each potion slot moves on its own, like a skill slot, and is clamped on
  screen. It had to be: a slot dragged past the top sat at y = -29 and the row
  read as five buttons with no way to get the sixth back.
* The quest box and the small party frame join the editor. They are the
  client's controls, so it lends the overlay their box
  (`RanTouch_SetCornerBox`) and takes back the offset, exactly as the potion
  row does.
* The editor's own toolbar can be dragged by its plate. It sits across the top,
  which is where the status bars, the corner icons and the potion row are - the
  one thing that could not be moved was covering the things it was there to
  move.

**The potion row went back where the tray has always been** - beside the status
bars, anchored to the first slot's authored position, taken once before this
code has moved anything. A long press on a slot opens the auto-pot window,
which is what the tray's gear did before the gear went with the rest of the
square furniture.

**`nMOBILE_HUD_FLOATS` was too small and had been for a while.** It said 52
while `RanTouch_GetHudLayout` wanted 56, then 60: that function writes nothing
at all into a buffer too small for it, so pressing save in the editor saved the
arrangement that was already on file. 80 now, and `SetHudLayout` reads a short
(older) file instead of refusing it outright.

**And the iOS builds had been built from stale client code.** `SOURCE/` is its
own repository and CI checks it out separately; this session's client changes
sat uncommitted in the working tree for several releases. Commit SOURCE too, or
the .ipa is Android's code from last week.

**The framed slot in the very top left corner goes.** Two of them, in fact,
drawn on top of each other: `BASIC_QUICK_SKILL_SLOT`, which the PC shows while
the skill tray is closed, and the left-top group's single quick POTION slot,
which it turns back on whenever the potion tray closes. Both are stand-ins for
a tray; on the phone both trays are on screen all the time, so each previewed
something already in front of the player, in the corner the status bars and the
potion row share. The skill one is simply never shown; the potion one is hidden
after the interface has updated - subtree and rect both, or the corner still
takes taps.

Finding it took a walk of the whole control container logging every control
whose rect lands in the corner. Worth remembering: `SetVisibleSingle(FALSE)` on
the quick potion slot, which the client already does, hides the slot and not
its children, so the picture and frame carried on drawing.

**The camera would not turn in a crowd.** Reported as "I cannot rotate because
I drag where the crowd is". The gesture layer decides what a drag means by
asking whether the finger went down on a control: on the world it presses the
middle button, which is the camera; on a control it presses the left, which
drags the control. The world's name plates ARE controls, and in a busy town
they cover most of the screen - so `GESTURE left(tap/drag)` and the view never
moved. `CUIMan::IsPointInControlExcept` skips a named group, and the gesture
layer asks through `RanUI_PointInDragControl` with `NAME_DISPLAY_MAN` left out.
Only the drag decision changed: a tap is still delivered where the finger went
down, so tapping a name still selects that player. Both measured on LDPlayer -
the same drag now logs `GESTURE middle(camera)` and the view swings.

**The inventory is folded side by side for the phone.** Authored it is a column:
the equipment doll (159 tall), then ten item rows, then the money and Sort rows
- 598 in all, against the 589 rows an iPhone 15 gives the client. It used to fit
by squeezing the one-unit gaps out of the slot rows (9 units, exactly the
shortfall); that squeeze is gone. `CInventoryWindow::MobileSideBySide` shrinks
the window by the height the doll occupied, moves the list up into that space
and puts the doll at a negative local x - out to the left, level with the list.
Measured live: window 239x435, doll 225x159 beside it, 154 rows to spare on a
phone.

**Then three more, asked for after looking at it: "the top header should cover
both", "it is not aligned", "the inventory looks darker".** All three were true
and none of them showed in the numbers.

* The title bar was the window's, 239 wide, stopping where the list starts -
  the doll sat under open sky beside it. Its cap / stretch / cap pieces are
  laid out by hand now (UI_FLAG_XSIZE and UI_FLAG_RIGHT are ignored by the
  align pass, same as the body) and it spans from the doll's left edge to the
  window's right.
* The doll's plate was only as tall as the doll, so the columns did not line
  up. It runs the body's full height.
* And the backdrop. Four passes of the body art made both columns black slabs.
  The fix came from measuring rather than nudging: one pass of that art over
  the world IS the tone every other window in the game has, so the plate gets
  one and the list gets none - it already has the window's body under it.
  Sampled in one frame: four passes read 2,2,2 and 5,5,5 against the chat
  window's 41,47,47; one pass reads 33,32,26 and 27,27,26 against 31,31,29.

**And the empty column.** Asked "why the left side is dark space at btm?" - it
was: the plate ran the window's full height so the columns lined up, and the
doll is only 159 of those 412, so everything below the equipment was nothing at
all. Trimmed to the doll's box; the header spanning both columns is what ties
them together now. The edge pieces were laid out against half the plate's height
while its bands covered all of it, so the bottom rule had been sitting across
the middle of the column - both come off one height now.

**Sampling beats eyeballing, and `out/shots` does not survive a build.** Two
rounds were spent nudging opacity by eye before measuring; and `build-apk.sh`
clears `native/out`, which took the before/after frames with it. Copy a frame
out before rebuilding, or measure it first.

**It shipped half-done first, and a screenshot at half size hid it.** Asked "have
you looked at how it actually looks?" - the answer was no, not properly. At full
size: the window's rect said 239x435 while its frame still drew about 512 tall,
and the money, cash-point, Separate and Sort rows had gone off the bottom
altogether. Same cause for both, and it is the trap the mobile menu window is
built around - `CUIWindowBody`'s pieces carry `UI_FLAG_XSIZE`/`YSIZE` and the
footer `UI_FLAG_BOTTOM`, but every line in the align pass that acts on those
flags is commented out, so nothing follows a window that changes size. The nine
units the old compact mode took were too few for it to show. The body's pieces
are resized and the footer moved explicitly now.

The list and the doll also needed a backdrop: one pass of the body art is
see-through over a town, and players were visibly walking about inside the empty
slots. Four passes a band, two bands, the same arithmetic the menu window's
backdrop uses.

It is still ONE window: the doll stays a child, so the two open, close, drag and
raise together, and a tap on an equipped item opens its menu through the moved
panel (verified on the device). A dark plate sits behind the doll, created
*before* it in `CreateSubControl` because the container draws in registration
order - made afterwards it would have covered the thing it was backing.

**"Could not reach the update server" now says which failure it was.** Reported
from a phone. Checked against the live host first: manifest and signature
byte-identical to ours, blobs present (40-blob sample, none missing),
Cloudflare answering a Dalvik user agent with 200, and the whole Android
update run end to end on the emulator against the live server - APK downloaded
and installed, versionCode 117. So the host is not the fault.

That message is the catch around the WHOLE patch, so it stood for a refused
certificate, a wrong clock, a DNS failure, an HTTP status, a full disk and a
signature that did not verify - indistinguishable from the screen, with the
reason going only to the log. It now carries a short code: exception class, the
HTTP status where the message has one, and the cause's class - e.g.
`(UnknownHostException / GaiException)`. Only types and three digits, so a
screenshot cannot leak a host or a path. Verified by pointing `.patchbase` at a
dead host.

**And the black squares themselves: the client's own slot quads.** A photo of
the phone settled it - six black SQUARES along the top where the round bezels
should be, and the bezels nowhere, because the overlay draws under the client's
interface and the squares were on top of them.

The slot is kept visible on purpose (it takes the tap that drinks the potion)
and was made to "draw nothing" with a zero-size texture rect. That is not
nothing: it is a quad with all four corners on one texel. A desktop GL driver
folds it away - which is why the emulator has looked right all along - and the
phone's driver paints it, with texel (0,0) of that atlas being black.
`SetUseRender(FALSE)` is what draws nothing; verified on the device that the
slot still takes input with it off, because the long press still opens the
sheet.

**The potion slot blinking black in combat was sampler state, not the cache.**
Reported as "when I use the skill, or when I get attacked, it turns black and
back". Measured first: the item's texture pointer and its GL name were logged
every frame across casts and neither ever changed, so the cached handle was not
the fault.

`drawIconDisc` binds the CLIENT's texture and sampled it with whatever sampler
state the client had last set on it. An item icon has one level, so the moment
anything asks that texture for a mipmapped min filter it is incomplete - and an
incomplete texture samples as nothing at all, with no GL error. Combat is
exactly when the client is drawing effects and changing filters. The sheet path
has set MIN/MAG LINEAR and CLAMP since the painted controls went in, for this
same reason; this path never did.

It cannot be reproduced on the emulator: a desktop GL driver is lenient about
completeness. That is the second time that has cost a round trip - the emulator
is for layout, the phone is for anything that touches sampler or GPU state.

**A cleared slot drew a black disc.** After ถอด the slot kept the texture handle
of an item it no longer held - the cache is only refreshed while the slot still
HAS a picture to give, which is what carries it through the hiding pass, and
that is exactly wrong the moment the item leaves. The character's own quick-slot
entry decides: `NATIVEID_NULL` clears the cache. Same index the tray already
uses for `ReqActionQ`, so the mapping is the client's own.

**The potion slot's long press is a menu now.** ถอด (take it out of the slot)
and ตั้งค่า (the auto-pot thresholds, a new `MOBILE_ITEM_SHEET` word at index
20) - the quick-slot sheet the client already has, with the "use" row dropped
because a tap on the slot drinks it. The PC's right-click-clears is off on
mobile: the long press is that menu now. The item-name banner is off too - on
the PC it follows the mouse and costs nothing, but a finger only hovers by
pressing, so every tap threw the name across the top of the screen.

**Data does not ride in the APK.** The ตั้งค่า row came back blank and the
packed `Gui.rcc` was demonstrably correct: the device's copy was eight hours
old. `build-apk.sh` does not carry `CLIENT/data`; push the .rcc with adb (or
run the patch) after repacking, or the client keeps reading yesterday's
strings.

**And then, told to stop guessing and look: the bar itself was the fault.** At
1:1 it is an 18-pixel hairline across a 1234-pixel window, with the title's own
glyphs taller than the strip and hanging out of the bottom. Every close-box
adjustment before this was chasing a symptom.

The art draws at half the height of the rect it is handed, so `TITLE_ART` hands
it twice what it should draw and `TITLE_H` stays what the bar IS - 20 units -
which is what the label, the close box and the body's edges lay out against.
Window 416. Checked at 4x and 6x on the device: name and close box both sit
inside the bar.

**The menu's close box, and why the title bar never behaved.** Asked "the close
btn look odd". Logging the live rects said the bar and the box were both at
y 84..102 - identical - while the screen showed the X hanging through the bar's
lower edge. Measuring the drawn pixels across three builds explained it:

* the title art draws at HALF the height of the rect it is given (26 -> 12
  units on screen, 18 -> 8, 9 -> 4), stretching only sideways;
* a button's art draws at its own texel size, CENTRED in its rect.

So an 18-unit close box against an 18-unit bar put the X nine pixels below the
strip. That also explains why setting the bar to 26, then 20, then 18 never
changed what was drawn. The close box is half the bar's height now, at the art's
own 14 wide, centred in the 24-wide right cap; window 414 to match. Checked at
7x on the device: the X sits inside the bar.

**The menu's top bar.** `BASIC_WINDOW_TITLE_*` is authored 18 units tall and was
being stretched to 26 - 1.44x, which smeared its bevel into a flat grey slab.
`TITLE_H` is 20 (1.11x, which the art carries), the title is centred across the
612-unit bar instead of tucked into the left cap, and the close box fills the
right-hand cap. `MOBILE_MENU_WINDOW` is 612x420 in `uiinnercfg02.xml` to match.

---

## 2026-09-18 (7) — The menu grid: drawn icons, names, and five reasons a CUIWindowEx could not hold it

**Patch 454 / APK V069 (versionCode 91) / iOS 1.0.91.**

Four things were asked for. Three were small; the fourth took the day.

**Names under every icon.** `MOBILE_MENULABEL`: the twelve `GAMEMENU` strings
said shorter — those carry the PC keyboard shortcut,
"ช่องเก็บของ(I)", which is noise on a phone — plus the eight that were
never in that list. Looked up **by position, not by control id**: the strip's
twelve are that group's own local ids numbered from `NO_ID + 1`, so they are not
unique against the standalone buttons'.

**The quest box and the party icon stay in the corner.** Both are watched rather
than visited, and a badge nobody can see is not a badge.

**New art.** The originals are 24x24 for the strip and 35x35 for the rest, shown
at 56 units — 112 real pixels on a phone. There is nothing in a 24-pixel picture
to enlarge; that *is* the pixellation. All 21 redrawn at 128 and downsampled,
packed into `mobile_icons.dds`. The slot frame behind each is gone: the plate is
the box.

**The window.** It is a plain `CUIGroup` now. Five engine facts, each measured:

| | |
|---|---|
| `CUIControlContainer::InsertControl` | rejects a duplicate id and returns false, silently. `CUIWindow` numbers title, close box and body from `NO_ID + 1` — exactly where a derived class starts. The backdrop and first cells were never added. |
| `CUIWindow::CreateBody` | passes `UI_FLAG_XSIZE\|YSIZE`, and every line acting on those flags is commented out in `AlignSubControlEX`. The body keeps ~120 units forever — and it is what the window draws and clips against. That was a grid one column wide. |
| `CUIWindow::Update` | snaps the window to the pointer whenever it is the exclusive control. On touch that dragged it into the corner every frame. |
| `SetLocalPos` | does not touch what is drawn. A child's global is recomputed from its local only when the **parent's** `SetGlobalPos` runs. |
| `CreateSub` | does `SetUseRender(texture name non-empty)` — a keyword with no `TEXTURE` never renders. |

Two more behaviours: the placement ran on the shut-to-open transition and never
fired, because the window is created `ShowGroupFocus` then `HideGroup` and the
latch was already set; and the large dark panel was `GAME_MENU`'s own 2-unit bar
stretched across the grid, whose bottom edge fell exactly across the third row's
names. Suppressed with a zero-area source rect — `SetUseRender(false)` would take
the group's twelve icons with it.

The frame is now built from the same pieces every other window uses, laid in the
same order, and the fill is doubled per band because the art is semi-transparent
and names have to read over a street full of players.

**The HUD editor moves it.** Edit mode keeps every touch in the overlay by
design, so the overlay hit-tests the window, drags it, and the position is kept
in RANPARAM as a fraction of the screen.

**Two art faults caught on the device and fixed:** the podium ran 30..112, so its
middle sat seven units right of the plate's; and the crossed swords are drawn
upright then turned 39 degrees, and a rotated shape's footprint is smaller than
the box it was drawn in. A third was an edit that had silently gone into a
**commented-out** `WINDOW_POS` — that block records the control's original
position in a comment before the live rect — leaving the competition button at
35x59 and scaling it to 33 wide against everyone else's 56.

Verified on LDPlayer: opens under the compass, 20 icons and 20 names, a tap
opens the thing and closes the menu, the corner keeps its two, nothing is left
on screen when shut. **Not yet tested on the iPhone.**

**Three follow-ups, shipped as 456 / V070 / iOS 1.0.92.**

The walk/run cell is gone - the overlay has its own control for it, and a
second one in a menu is a second thing to keep in step. The item bank now sits
beside the item mall: it was the tenth cell of the strip and the mall was the
sixth standalone, four cells apart though they are the same errand. Both lists
carry the label index with the control now rather than taking it from the loop
counter, because the order is no longer the order the names were written in.

And the corner icons each own a slot. They were packed right to left, skipping
whichever was hidden, so opening the party panel took its icon off the corner
and the quest box slid across into the space - movement nobody asked for, under
a finger already on its way. Verified by screenshot either side of the tap: the
quest box does not move.

**The three overlays an icon wears, and what was wrong with each.** An icon in
this client is not one picture: a press shows `MENU_OVER_IMAGE`, an event
blinks `AUCTION_ALERT` / `COMPETITION_NOTIFY_BUTTON_BLINK` over it, and the
competition button can wear a lock. All three were still the original 24x24 and
35x35 art from `Interface_Main` and `q_icon`, laid over a rounded 128-pixel
plate - small hard boxes in the corner of a cell. They are drawn at 128 into
`mobile_icons.dds` now, on the plate's own corner radius.

Three faults found while doing it, each measured rather than guessed:

* **The plate's outline was lop-sided.** In the source art the rim read 146,
  144 and 145 on top, bottom and left - and **64 on the right**. It was an outer
  rounded rect with a reversed inner one inside it, and `roundRectPts` starts
  its path at the bottom-right corner, so the two segments bridging outer to
  inner both run along the right edge and cancel the fill exactly there under
  non-zero winding. A closed stroke has no seam. Re-measured 149/149/149/147.
* **The overlays carried an opaque plate.** They were built in the same loop as
  the icons, which draws `plate()` first - so the press ring and the event ring
  each came with a slab that blanked out the icon they were meant to decorate.
  Overlays skip the plate now.
* **The competition blink and lock sat at `Y="24"`**, the offset from the days
  when that button was a 35x59 box with a badge above the picture. The box is
  square now, so both drew below the icon.

And the icons stopped floating over everything. They are the client's own
controls, not children of the menu, so their place in the interface is whatever
`ShowGroupFocus` last made it - and that is an `InsertTail`. Raising them every
frame kept shoving them to the very top: with the menu up, opening the inventory
put its window correctly over the menu's panel and left the menu's icons on top
of it. They are raised on the rising edge now.

Worth knowing for any future work on the press state: it cannot be photographed
on the emulator. The gesture layer delivers a left press only on finger-up, and
a hold becomes a right-click after 450 ms (`GESTURE right(hold) ... after
450ms`), so the flip lives about one frame - and `CBasicButton::Update` clears
it at the top of every frame before `TranslateMouseMessage` can set it again.

**The amber event ring is gone again, by request.** `AUCTION_ALERT` and
`COMPETITION_NOTIFY_BUTTON_BLINK` draw nothing now - a zero-area source rect,
the same way the GAME_MENU bar is suppressed, so no behaviour changes and the
controls still exist for anything that looks them up. The press ring stays but
is neutral rather than cream, so no yellow ring is left anywhere on an icon.
The gold inside a glyph - the skill spark, the quest seal, the ranking crown -
is icon design, not a notification, and is untouched.

**Then put straight back, because the complaint was the "!" and not the ring.**
The exclamation mark was the skill page readout: `drawDigit` is a seven-segment
renderer, and a 1 there is segments b and c - a short bar above a shorter bar
with the middle segment's gap between them. Correct for a seven-segment
display; at a couple of dozen pixels, in amber, in a dark box beside the attack
ring, it reads as an exclamation mark. A 1 is one unbroken bar now (V073), and
`AUCTION_ALERT` / `COMPETITION_NOTIFY_BUTTON_BLINK` have their ring again.

**All 17 cells swept on LDPlayer.** Two looked dead - the ranking at 2.0% of the
screen changing and the competition button at 0.2% - and that was the test's
fault, not theirs: by that point the windows opened by earlier cells covered the
grid and were swallowing the taps. From a clean login they open in their own
right, 24.2% and 27.1%, and the ranking window was photographed with its tabs.
A sweep that leaves its own windows open cannot measure the cells underneath
them.

**The drawn icons are gone; the grid is painted now.** The vector glyphs were
honest about what each button did and wrong about what game they belonged to -
the client's own art is painted, and a flat pictogram on a grey plate reads as
a phone launcher beside it. `MOBILE/ICON-BRIEF.md` is the brief that went out:
the prompt, the format, and a subject line for every icon. Thirty-eight came
back at 1254x1254 and are kept in `MOBILE/tools/icon-art/` with `pack.js`, which
rebuilds the atlas from them.

Two things that had to be handled on the way in:

* **Four arrived as flat RGB on a grey gradient**, no alpha. A flood fill made a
  poor job of it - the background runs 200 at the corners to 140 mid-edge, and
  any tolerance loose enough to walk that starts eating the icon. But every menu
  icon is the same rounded plate in the same place, so the silhouette was
  already known from the ones that did have alpha: the mask is the median of
  five of them, which is exact rather than approximate.
* **Transparent texels still carry a colour**, and the sampler mixes it in at
  the edges when the icon is scaled. Every cell gets four passes of alpha bleed
  so the mix has nothing wrong to find.

The qbox and the mini party joined the atlas at the same time - they had been
left on the original `q_icon.dds` art and would have been the only flat things
left on screen.

**The sixteen on-screen controls were delivered too** and are not in yet: the
overlay draws those from shapes rather than from art, so they need a sheet and
the code to sample it. It already does exactly that for the skill pictures.


## 2026-09-18 (6) — The top-right corner is one HUD button and a grid window

The corner carried 21 taps: nine standalone buttons at 35 logical units and the
twelve-icon `GAME_MENU` strip at 26. On an iPhone 15 the client lays 1278
logical units across 852 pt, so those are **23 pt and 17 pt targets** against
Apple's 44 pt minimum and Material's 48 dp — a third to a half of what a finger
needs. That is why they were fiddly, and it is measured, not felt.

They are one button now. `CMobileMenuWindow` (new, `Lib_ClientUI/Interface`)
is an 11x2 grid on a 66-unit pitch — exactly 44 pt — and the icons in it are
**the client's own controls, moved, not copied**: each keeps its art, its
tooltip, its handler and its own rules about when it appears. Re-parenting
would have taken all of that with it.

What the grid taught us, each one a wrong assumption paid for on the device:

- **`AlignSubControl` only resizes children that carry `UI_FLAG_XSIZE/YSIZE`.**
  Six composite icons ignored every size we set. `ReSizeControl` scales by the
  parent ratio unconditionally and recurses, so that is what the layout uses.
- **Squashing art into a square moves the clickable child.** A 35x59 icon
  forced to 56x56 put the party finder's hit box somewhere else, so it read as
  dead. The fit is aspect-preserving now.
- **`CheckBoundary` clamps a negative left/top to 0.** Parking hidden icons at
  -4000 piled them in the top-left corner the moment the window closed, and
  `SetNonBoundaryCheck` does not reach child art. They are hidden instead, with
  `s_bIconWanted[]` remembering which ones the client actually wants back.
- **The window is in the focus list, so it ate the presses.** The icons sit
  above it via `ShowGroupFocus` (`InsertTail` = topmost).
- **`BASIC_WINDOW` is 120x28 with a 7-unit body piece**, so its own chrome
  covers about 140 of 404 units however large the window is set. The grid
  carries its own backdrop control instead.

**Popping up was slow** because the layout ran inside `MobileArrangeInterface`'s
once-a-second sweep: up to a full second of empty frames. `MobileArrangeMenu()`
is split out and runs every frame, above the gate. Screenshot at **150 ms**
after the tap shows it fully populated.

**The button matches the HUD now.** It was a `MENU` plate lifted out of
`Interface_Main.dds`, which looked like a piece of another UI parked next to the
compass. It is `RANTOUCH_SLOT_MENU` in the touch overlay — same disc, ring,
bevel and gloss as auto-target, PK and camera lock, four squares for a glyph,
and its own HUD-editor group so it moves and resizes with the rest.

All 22 cells were swept on LDPlayer, one tap each, log-verified. Three that
looked broken are not: the item shop is a `JP_PARAM` no-op in this build, the
quest box is a toggle, the party finder is async.

Ships with code **and** a repacked `Gui.rcc` — new keywords `MOBILE_MENU_WINDOW`
and `MOBILE_MENU_BUTTON`, new gameword `MOBILE_MENU`.

**Not yet tested on the iPhone.**

**Shipped as patch 450 / APK V067 (versionCode 89) / iOS 1.0.89.**

Getting there caught a release bug that had already cost two rounds:
CMakeLists stamps the iOS bundle version as `1.0.<android versionCode>`, read
straight out of `AndroidManifest.xml`, but that file was not in the iOS
workflow's push path filter - and MAKE-PATCH bumps the number *after* the code
is pushed. So CI stamped the version already installed on the phone and
AltStore had nothing to offer. The file is in the filter now, which the very
next push proved by triggering a build.

Upload set: 2 blobs, 94.4 MB, plus `manifest` and `ios/`.


## 2026-09-18 (5) — A trustworthy sweep at last, and what the character effects actually do

**The sweep works now.** 1.0.86 lifts the thermal clamp properly and the tool
reads the frame rate back per reading. Eight readings at a verified constant
60 fps, seven correctly refused:

| section | GPU | cost |
|---|---|---|
| *baseline* | 83% | — |
| `world-eff` | 56% | **27 points** |
| `interface` | 68% | **15 points** |
| `w:land` | 77% | 6 |
| `eff:animan` | 79% | 4 |
| `eff:tree-after` | 80% | 3 |
| `eff:alphamap` | 82% | 1 |
| `w:mobitem` | 86% | **-3, i.e. nothing** |

The characters cost nothing measurable, now at a checked constant frame rate
rather than inferred. The seven refusals were rejected because the rate fell to
51-58 fps: with the clamp lifted, **the phone physically cannot hold 60 in this
crowd once hot**. 83% GPU at 60 fps, decaying to ~51 fps. That is the shape of
the complaint, measured.

**What each pass submits, counted on the emulator** (a proxy for fill, not a
cost - the emulator has fill to spare). Per frame, blended triangles first:
`part:chareff` 111 draws / 38,426 blended; `world` 20,774 of 40,944;
`interface` 269 draws / 9,874 all blended; `eff:tree-after` 6,736. Against that,
`part:skinned` submits 143,527 triangles of which only 2,604 are blended - the
character meshes are opaque and cheap to fill, which agrees with the phone.

**And what part:chareff is**, from the effect profiler (`effprof`, re-read live):

| effect | calls/frame | draws | RT switches |
|---|---|---|---|
| `DxEffCharSpecular2` | 38.6 | 38.6 | 0 |
| `DxEffCharUserColor` | 38.6 | 38.6 | 0 |
| `DxEffCharLevel` | 34.3 | 38.9 | 0 |
| `DxEffCharMultiTex` | 13.8 | 39.5 | **71.9** |

Two things worth chasing. Every visible character piece is **redrawn about three
extra times, blended** (Specular2, UserColor, Level). And `DxEffCharMultiTex`
re-renders a scrolling texture into a shared 128x128 target before drawing the
piece with it, which costs **72 render-target switches a frame** - nearly free on
the emulator's immediate-mode desktop GPU, and a tile flush each on a
tile-based one. That asymmetry is exactly the kind of thing that shows on an
iPhone and not on LDPlayer, so it is a hypothesis with a reason, not a hunch.

**It is still only a hypothesis.** `part:chareff` sits inside `w:mobitem`, which
the phone measured at zero points. Either the emulator's crowd carries far more
character effects than the phone's did, or that -3 hid something. Not resolved.

**Next, and it needs no rebuild:** `effskip` takes an effect name, is re-read
live, and the names above are exactly what it matches. So on the phone -
`DxEffCharMultiTex`, then `DxEffCharSpecular2`, `DxEffCharUserColor`,
`DxEffCharLevel`, one at a time against `dvt graphics` - plus `part:chareff` and
the five `eff:` passes the throttle refused. All of it works on 1.0.86 as
installed.

**Commits.** MOBILE 5b4a5b7, e609638.

---

## 2026-09-18 (4) — The attribution sweep measured its own frame rate clamp

First run on the phone of the build made for measuring (1.0.84, confirmed on the
device by the `eff:` and `ui:names` sections appearing in its log). The sweep
produced numbers that could not be true, and the reason is worth keeping.

**What came out.** `world-eff` cost 12 points, and then every one of its eight
children cost 33 to 36. A child cannot cost three times its parent. That is what
gave it away - not a suspicion about the result, an arithmetic impossibility in
it.

**The cause was this morning's own thermal pacing.** The sweep takes several
minutes; four minutes in, the phone reached `serious` and the clamp dropped the
display link to 30 Hz. GPU utilisation fell with the frame rate, and every
reading after that point was measuring the 60-to-30 transition rather than a
section. A section attribution only compares if the frame rate is identical for
every reading, and nothing in the tool enforced that.

**Re-running while stably hot does not rescue it.** At 30 Hz the GPU sits at 28%
and all ten sections came back inside +/-3 points - everything is noise, because
the work is halved and the GPU has headroom to spare. Attribution is impossible
at both ends: too hot to hold 60, too idle at 30 to resolve anything.

So `noheatpace` (1.0.85): hold the display rate whatever the thermal state says.
Not a setting to ship - the clamp is doing its job - but without it the frame
cannot be attributed on a phone that heats in four minutes.

**What is valid.** The first six readings happened before the clamp engaged, at a
genuine 60 fps:

| section | 1.0.84 | the old build, 2026-09-18 (2) |
|---|---|---|
| *baseline* | **61%** | 82% |
| `world-eff` | **12 pts** | 39 pts |
| `interface` | **7 pts** | 30 pts |
| `w:mobitem` | ~2 (noise) | 12 pts |
| `w:land` | ~1 (noise) | 17 pts |

Everything is far cheaper than it was. **This is not claimed as an improvement
from the day's changes**: the two runs are different camera angles in a moving
crowd, and that alone moves these numbers. It needs a controlled re-measure with
`noheatpace`, which is the point of the next build.

**The pacing works, and does not fix the heat.** Mechanically it is exact - 689
consecutive `serious` samples at a 33.3 ms median, p95 and max all identical,
zero frames over 34 ms. But over eleven minutes at 30 Hz and 28% GPU the phone
never returned to `fair`. Halving the frame rate keeps the game smooth while hot;
it does not cool it down.

**Two confounds recorded before they are forgotten.** The phone is charging over
USB throughout, which is itself a heat source, so every thermal reading here is
pessimistic against how the game is actually played. And thermal state has
hysteresis, so a reading taken while descending from `serious` is not comparable
with one taken while climbing to it.

**Commits.** MOBILE f50fefe, fbf512e. Staged for upload: 1.0.85.

---

## 2026-09-18 (3) — How we work, written down, because three things were guessed at instead

Asked, twice and sharply: "why need the sideloadly? are you guessing?" and then
"I said we already setup the patch? have you check the memory? stop guessing!!!"

Both were right. Three separate claims about the iOS release path were made from
memory of a script header rather than from the scripts, and all three were wrong:

- **"Build it on your Mac."** There is no Mac. `.github/workflows/ios-build.yml`
  says so in its own header - the `macos-14` runner *is* the compiler.
- **"Sign it with Sideloadly."** `tools/patch/make-ios-source.js` exists
  specifically to remove that step: AltStore/SideStore re-signs the unsigned CI
  bundle with the player's own Apple ID. Reaching for Sideloadly means working
  around the setup that is already there.
- **"A patch cannot carry it."** Half true and useless as stated. The code cannot
  ride the *data* payload, but the `.ipa` and `ios/source.json` are published
  into the same `out/launcher_mobile` tree, so one upload serves both. That is
  what "we already setup the patch" meant.

`PATCHING.md` had **zero** mentions of iOS. That is the actual root cause: the
document that wins disputes did not cover half the platforms, so the gap got
filled with recollection. It now has a full section, read out of the scripts.

**The division of labour, stated so it is not re-invented.** Commit, push, run
the workflow, download the artifact, verify the binary, run `make-ios-source.js`
- none of that is the user's job. The user deploys `out/launcher_mobile`. That is
the only manual step and the only one that needs their machine.

**The sequence:**

    1. commit, then `git push`      (plain form; the refspec form is refused)
    2. gh workflow run ios-build.yml
    3. gh run watch <id> --exit-status
    4. gh run download <id> -n ran-ios-unsigned -D native/out/ios-ci
    5. grep the binary for a string only the new code has
    6. node tools/patch/make-ios-source.js native/out/ios-ci/RanLegacyM-unsigned.ipa
    7. hand over out/launcher_mobile

**Three traps found doing it, all now in PATCHING.md:**

- **`1.0.<versionCode>` is the whole iOS version.** CI rebuilt at `1.0.83` while
  the phone already had `1.0.83`, so AltStore would never have offered it - and
  this is why the phone had been running a pre-today binary all along, with every
  diagnostic flag pushed to it doing nothing. Bumped to 84 and rebuilt.
  `AndroidManifest.xml` is not in the workflow's push filter, so a version bump
  never triggers a build by itself: push, then dispatch.
- **`strings` returns nothing for a Mach-O on this machine.** The first "is my
  code in this build?" check reported every marker missing, which looks exactly
  like a failed build. `grep -qaF` on the binary works. A broken instrument that
  answers "no" to everything is worse than none - checked the instrument against
  a string known to be present, which is what caught it.
- **`git push origin HEAD:main` is refused** by the sandbox classifier as
  exfiltration where plain `git push` is not. Earlier pushes in the same session
  had used the plain form, which is why it looked like a new restriction.

**Shipped to the store tree:** `1.0.84`, CI run `35305784747`, verified by
grepping the arm64 binary for `eff:afterrender`, `pcname`, `ui:names-plate` and
`worldscale`. Nothing in it changes behaviour by default - name limit 0, world
scale 100, stage fold off - and `manifest.json` / `minIos` were deliberately not
touched, so no other player is told to update. It exists to be measured.

**Commits.** MOBILE ec5ce69, f6b6470 (+ this doc); SOURCE b64c045.

---

## 2026-09-18 (2) — Measured on the phone: the heat is effects and name plates, not characters

Asked, after the quiet-area test: is the phone hot in normal play or only in a
crowd? Answer, measured rather than assumed.

**Quiet area, no other players, 5.5 minutes.** 337 of 337 samples `heat
nominal`; 60.1 fps; 8.5 ms of a 16.6 ms budget; frame time flat across all ten
deciles (8.4 -> 8.5 ms), memory flat, no leak. The client on its own does not
heat this phone at all.

**The same crowd that prompted the complaint: 242 players seen, 40 drawn.** The
phone climbed nominal -> fair -> serious over about four minutes, and the whole
time it held 60 fps with the CPU half idle (9.6 ms flat, zero frames over 20 ms).
So this was never a frame rate problem. `dvt graphics` said what it was: the GPU
pinned at **82%**, Tiler and Renderer both, held for minutes.

Then `sectionskip` dropped one section at a time while `dvt graphics` watched:

| skipped | GPU | cost |
|---|---|---|
| *(baseline)* | 82% | — |
| `world-eff` | 43% | **39 points** |
| `interface` (UI + 242 names) | 52% | **30 points** |
| `w:land` | 65% | 17 points |
| `w:mobitem` (characters) | 70% | 12 points |

**The characters are fourth.** `pcdraw` from 40 to 10 - a 75% cut - moved the GPU
by 13 points, giving a floor of roughly 65% that is not other players at all.
The touch HUD, despite submitting 108k verts a frame, is worth 1 point and is
ruled out. World effects cost 0.2-0.6 ms of CPU and 39 points of GPU, which is
the signature of alpha-blended overdraw: nearly free to submit, brutal to fill.
That is why five sessions of draw-call work never touched it.

This retires three ideas, each of which looked reasonable until it was measured:

- **Character LOD** - 12 points available. Not the fix.
- **The world render scale** built earlier the same day - a real lever, but aimed
  at fill in general when two specific systems are the problem, and it costs
  picture quality to use. It stays as an option and is not the answer.
- **Baking the texture stage into the shader** - the frame has CPU headroom to
  spare and the emulator cannot measure fragment cost anyway (eight times the
  fragments changed LDPlayer's frame rate by nothing). Still off by default.

Done in response:

- **A name limit**, separate from the body limit and larger, reusing the ranking
  already built for bodies (target, action target, party, then club and PK, then
  nearest). A dropped name also skips its occlusion ray. Default 0 (every name,
  unchanged) until the phone gives a number; `/sdcard/ran/pcname` overrides.
  LDPlayer with `pcname 20`: names a frame 228 -> 29, 40 characters still drawn,
  60 fps, and the crowd stopped being a solid wall of plates.
- **`world-eff` split into its eight passes** (`eff:animan`, `eff:alphamap`,
  `eff:tree-after`, `eff:tree-after1`, `eff:afterrender`, `eff:alphapiece`,
  `eff:landeff`, `eff:weather`) so `sectionskip` can say which of them the 39
  points belong to. Being told the bundle costs 39 says nothing about what to fix.

**Still open.** The phone is running a build from before all of today's work -
none of `scene target`, `stage combos` or `gamma ramp` appear in its log, and the
`worldscale` flag had no effect on it. The next `build-ios.sh` gets the eight
effect sections and the name limit, and then the 39 points can be attributed and
the name limit given a measured default.

**Commits.** SOURCE 95d310c, 51dc2be, c7190f6, b64c045; MOBILE f89ce00, 28d3b1b,
762ac3d, 37b064e, c17ad29.

---

## 2026-09-18 (1) — The iPhone heat is fragments, not draw calls: draw the world smaller than the panel

Asked, after the HUD editor: "is it possible to keep 60 frame but no heat like
ROV?", then "read the real code and do the real analysis... do it until you fix
the problem".

**The measurement that redirected everything.** The session had been cutting GL
calls - names in two passes took the frame from 13,200 calls to 5,416. That is
worth having, but it is not the heat. On the iPhone `dvt energy` and `dvt
graphics` put the GPU at 59-70% busy and spending **more energy than the CPU**,
and on LDPlayer the frame is bound by `submit` (9.0 ms of GL calls through the
emulator's host bridge), which is an emulator artefact - a real phone submits
the same calls in about a fifth of the time. The two platforms have different
bottlenecks, and the phone's is fragments.

Fragments scale with the square of the resolution. The panel on an iPhone 15 is
2556x1179; the art was authored for 1024x768. Every one of those pixels is
shaded, blended and written, for the world, the effects and the overdraw on top.
Nothing about the scene's *detail* depends on them being 1:1 with the panel.

**The fix.** The 3D world renders into a target that is a fraction of the panel
and is stretched over it once, with a linear filter, at the end of the world
pass. The interface is deliberately not in that target: text, names and the HUD
are cheap to shade and do resolve as finely as the buffer allows, so they keep
the full panel and stay sharp at every setting. This is what the phone games
this one is compared against ship as "graphics quality".

The seams were already in place and this is why the change is small:

- `RanGL_DefaultFramebuffer()` was the one place that meant "the screen", so
  "the screen" becomes the scene target while the world is drawn, and the
  engine's own off-screen passes (glow, blur, refraction, post) come back to the
  right place when they restore render target 0.
- `RanGL_UIScale()` was already the factor the viewport and scissor paths
  multiply by; the world pass adds one more.
- Depth **and** stencil are attached - the shadow volumes clear stencil, and a
  target without one silently drops those clears.
- `glInvalidateFramebuffer` on the depth/stencil at the end of the pass, so a
  tile-based GPU never writes them out to memory just to discard them.

`Settings > Function` gains a graphics quality row: สูงสุด / สูง / ลื่น = 100 /
85 / 70 percent, saved in `RANPARAM::dwMobileWorldScale` and applied at startup.
`/sdcard/ran/worldscale` (a percentage) overrides it live, for measurement
without a rebuild.

**Verified on LDPlayer**, GM crowd, ~40 characters drawn and ~230 names: the
target switches live to 1792x1008 of a 2560x1440 panel, the world, shadows and
effects render correctly, the interface and every name stay sharp, and the frame
rate is unchanged (53-58 fps against a 55-57 baseline). Unchanged is the
expected result there and is the point of measuring it: the emulator is bound by
call submission, not by fill, so this proves the blit and the extra pass cost
nothing while the saving waits for a real GPU.

**Not yet verified on the iPhone** - the iOS binary is built on a Mac and signed
with Sideloadly, which is not reachable from here. That build and a heat reading
at ลื่น is the next step.

Also fixed along the way:

- **A real regression from the HUD editor commit.** `FUNCTION_HUDEDIT_BUTTON`
  had been inserted *inside* the `FUNCTION_PLAYERDRAW_20/40/60` fallthrough
  group, so tapping 20, 40 or 60 opened the HUD editor instead of setting the
  character count. Only ทั้งหมด worked.
- **`DxCharPart::Render` formatted a string per effect per part per frame** to
  decide whether to log a line it had already logged; the set filled with its 64
  names in the first second and the formatting carried on all session. Behind
  the `chareffdump` flag now.
- Names in two passes (plates, then text) and a cached name-occlusion ray, both
  committed: 13,200 -> 5,416 GL calls a frame, `interface` out of the top ten,
  names visually identical.
- iOS asks `CADisplayLink` for 30 Hz while `NSProcessInfo` reports thermal state
  serious or worse, which breaks the loop where a throttled frame misses the
  deadline that caused the throttling.

**What was ruled out, with the measurement.** Bone-palette dedupe (550 uploads a
frame, but costume parts of one character have different bone groups, so they
are not duplicates); light-block dedupe (the per-variant uniform cache already
does it - 113 of ~190 uploads a frame are a variant seeing the current block for
the first time, and the lights genuinely change 75 times a frame); folding
`alphaTest` out of the shader variant key (it flips 178 times a frame, the
largest single churn, but making `discard` reachable in every variant costs
early-Z on exactly the tile-based GPUs this is meant to help).

**Commits.** SOURCE 95d310c, 51dc2be, c7190f6; MOBILE f89ce00, 28d3b1b, 762ac3d.

---

## 2026-09-17 (1) — Phone hot after 5 minutes: the Android frame loop had no cap

Reported: "after I play for some times even 5 mins the phone is in the heat a
lot", with the condition "not losing any performance".

The Android loop is paced only by eglSwapInterval(1), so it runs at whatever the
PANEL refreshes at - 90 or 120 Hz on a recent phone - while the shim tells the
client the display is 60 Hz. Nothing called setFrameRate or read the refresh
rate. Every frame above 60 is heat the player cannot see; the client and
RanTouch_Frame step from real elapsed time, so drawing less often slows nothing.
(iOS has the same mechanism behind the pace30 diagnostic, off by default.)

android_main.cpp now paces the loop to 60 Hz with a nanosleep (not a spin),
rebasing when behind so a slow stretch is not repaid by running flat out.
/sdcard/ran/pacehz overrides it (0 = uncapped).

Measured on LDPlayer (a 60 Hz panel, so the default cap is a no-op there):
    default:    "frame loop paced at 60 Hz", 60.0 fps, no regression vs 59.9-60.0
    pacehz 30:  "frame loop paced at 30 Hz", 30.3-30.4 fps, swap wait 10 ms -> 0.8 ms
The second line is the proof the sleep does the pacing, not vsync.

NOT measured: the thermal drop itself. It depends on the phone being above 60
Hz, and no phone or tablet was reachable. On a 60 Hz phone this changes nothing
and the heat is the per-frame work instead - 284 draws / 1,832 GL calls per
frame on the idle login screen is the next thing to look at.

Two instrument traps hit on the way, both worth knowing:
* build-apk.sh piped through tail hid a run that packaged a STALE arm64 lib.
  Check the APK itself (the string in lib/<abi>/libran.so), not the console.
* Diagnostic files in /sdcard/ran are unreadable to the app unless
  READ_EXTERNAL_STORAGE is granted: it is declared but was granted=false on
  LDPlayer, so every diag switch silently read as absent. `pm grant
  com.ran.native android.permission.READ_EXTERNAL_STORAGE` fixed it.

## 2026-09-16 (9) — Typing on a real Android keyboard doubled characters: a composition appends, it does not replace

Reported from a phone: pressing a key twice put three characters in the field.

A soft keyboard does not send the letter just typed. It sends the WHOLE
composition again on every keystroke, and RanInputConnection.setComposingText
handed each one straight to nativeCommitText, so the client appended them all:

    press x   ->  setComposingText("x")    ->  buffer "x"
    press x   ->  setComposingText("xx")   ->  buffer "xxx"

and the commitText on the next word break appended the composition a third
time. The code knew: its own comment said proper composing "replaces rather
than appends, and is still to do".

RanInputConnection now remembers the composition in flight (mComposing) and
takes it back with nativeBackspace before inserting the new one. The count is
in CODE POINTS, not String.length() - nativeBackspace removes one whole UTF-8
character, while length() counts UTF-16 units and would leave half of anything
outside the BMP behind. Every other path that invalidates the composition
clears the memory: commitText, deleteSurroundingText, a real KEYCODE_DEL,
ENTER, performEditorAction and finishComposingText.

**Why every test here missed it, and how to test it next time.** LDPlayer's IME
is shown-but-dead: showSoftInput fires, Pinyin is the default, and no keyboard
is ever drawn - the screenshot after tapping the ID field shows the login page
with no keys at all. Typing on the emulator goes through ImeView.onKeyDown
instead ("adb shell input text" injects into the focused window), which is one
character per press and never touches the composing path. So this bug is
invisible on the emulator BY CONSTRUCTION, and the tablet was unreachable.
Anything touching text input has to be tested on a real device with a real
keyboard; the emulator can only prove the key-event path.

Verified by the user on an Android phone - the only place it can be verified.

Published as store 446, versionCode 78 "V059": the staged classes.dex contains
mComposing and dropComposing, so the blob carries the fix rather than a re-wrap
of the old one. minIos stays 77 (no iOS change; iOS types through a UITextField
and has never had this bug - UIKit owns the composition there). Upload set is
one 44.0 MB blob plus the manifest and ios/.

## 2026-09-16 (8) — Patching was slow because every patch shipped 153 MB of DWARF; iOS keyboard had no globe key

Two complaints, one session: "the patch is so slow? why", then "even the android
is slow patching" - so not an iOS-only problem, and not the 11 MB .ipa either.

**The APK was 331.6 MB and 92% of it was debug info.** The build is
RelWithDebInfo and the APK stores .so uncompressed (Android maps the library
straight out of the zip), so the DWARF *is* the download: libran.so was 168.5 MB
on arm64 and 155.2 MB on x86_64, of which ~153 MB and ~139 MB were debug
sections. The APK is one content-addressed blob, so **every** code change - a
one-line fix included - republished all 331.6 MB to every player on both
platforms. That is the whole of "slow patching"; the CDN was never the problem.

build-apk.sh now splits the debug info to out/<abi>/libran.debug and runs
llvm-strip --strip-debug on the *staged* copy only. out/<abi>/libran.so keeps
everything, so a crash address from a shipped build still symbolises -
llvm-symbolizer and addr2line take the .debug file.

    APK        331.6 -> 44.0 MB
    arm64 lib  168.5 -> 18.2 MB      x86_64 lib  155.2 -> 17.8 MB

Verified on the emulator, not just weighed: installed the stripped APK, logged
in, world renders at 60 fps with textures, Thai text, HUD and minimap intact.
Patch store 444 / versionCode 77 "V058" built on it; the old 331.6 MB blob was
pruned.

**The iOS keyboard could not switch to English** because RanViewController
conformed to UIKeyInput and nothing else. UIKeyInput is the *minimal* text
surface: it is not UITextInput, so the system has no text input to attach an
input mode to and the keyboard opens with no globe key - whatever language it
came up in is the only one reachable. Nothing regressed; the limitation shipped
with the original iOS port, which is why searching recent changes found nothing.

The fix is a zero-sized RanIMEField (a real UITextField, so a real UITextInput)
added to the view hierarchy and made first responder instead of the controller.
It holds one zero-width sentinel character, because iOS delivers no deletion to
an already-empty field and this field is always empty of meaning - the client's
CUIEditBox owns the buffer. Every edit is turned into the same RanIME_InsertUtf8
/ RanIME_Backspace / RanInput_KeyTap(0x1C) call the old path made and then
refused, which leaves the sentinel in place for the next backspace. Smart
quotes and dashes are off: CP874 has no room for curly punctuation.

Android is untouched - it has its own RanIME_* in android_main.cpp.

Built as iOS 1.0.77: the bundle version is not hardcoded, CMakeLists.txt reads
android:versionCode out of AndroidManifest.xml and makes it 1.0.<code>, so the
iOS version follows the APK bump rather than being chosen. Staged to
ios/source.json, which raised minIos to 77 on its own - make-manifest.js follows
the iOS build in the store and never lowers it. Store 445. versionCode stayed
77, correctly: the Android binary did not change, so store 444's APK is still
the current one and no Android rebuild was needed.

Not yet verified on device. The globe key is confirmed by construction and by
the CI compile only - there is no iPhone here, so it is proven when 1.0.77 is
installed. The vehicle ใช้งาน row from (5) is also still unverified; it needs a
character that owns a vehicle, and this cycle's one live login went on checking
the stripped APK.

## 2026-09-16 (7) — A text button's width is UI_FLAG_XSIZE, not the rect you give it

Five attempts at one button, and the first four were each wrong for a different
reason. Worth writing down, because nothing about it is guessable from the call
site and the next UI change will hit it again.

**What a CBasicTextButton actually is:** a group of three art pieces - a left
cap, a centre carrying UI_FLAG_XSIZE, a right cap carrying UI_FLAG_RIGHT.
`CreateBaseButton` builds them, then makes a throwaway CUIControl from the skin
keyword and calls `AlignSubControl ( own rect, skin rect )`. CUIGroup's override
resizes the button and walks its children with the button's old and new rects,
which is what stretches the centre piece.

**The trap:** `CUIControl::AlignSubControl` only changes sizeX when the control
itself carries UI_FLAG_XSIZE. ซื้อ has it, so it grows to the skin's 202. With
UI_FLAG_DEFAULT the button keeps BASIC_TEXT_BUTTON40's own 73 and **no rect set
afterwards changes anything** - SetLocalPos resizes the control, not the art
inside it. A button can therefore have a 202-wide rect and draw 73 wide, which
is exactly what the screenshots showed.

The four wrong turns, in order:

  1. UI_FLAG_XSIZE plus a hand-set rect - the rect was ignored and the button
     ran off both edges of the panel.
  2. UI_FLAG_DEFAULT sized from the category list - that rect (l13 w202) is the
     text column, and the button drew at 73 regardless.
  3. Sized from ซื้อ's own rect - same 73, for the same reason.
  4. AlignSubControl with a width derived from the scrollbar's left - the bar
     carries UI_FLAG_RIGHT, so its local left is not a panel-relative x; the
     width came out ~75 and the art faithfully drew that.

**The answer:** build it exactly like ซื้อ - same parent keyword, same skin,
same UI_FLAG_XSIZE - and then only MOVE it (`SetLocalPos ( D3DXVECTOR2 )`).
Never resize it. The skin gives l8 w202 h40; the top goes to where the list
starts, and the list shifts down by the height plus a gap. Its right edge lands
where the scrollbar begins, so the button fills the box and the bar sits below
it, untouched.

**Also disproved along the way**, so it does not get re-argued: `CheckProtectSize`
only ever grows a rect, never shrinks one, so it narrows nothing; and
`AlignSubControlEX`'s size branches are commented out, so that function no
longer resizes anything at all.

**And the scrollbar:** it carries UI_FLAG_RIGHT | UI_FLAG_YSIZE. A rect set on
it survives one frame and is then overwritten - top 0, sizeY the window's
height - so it re-stretched over the whole panel and through the points row.
Nothing may reposition it; the thumb is already sized from the list's own
visible-line count on every refresh.

**Measured from the client, which is what finally separated "wrong number" from
"the art does not follow the number":**

    SHOPRECT buy  local l8  t453 w202 h40 | global l0 t0 w73 h40
    SHOPRECT list local l13 t29  w202 h370 | window w615 h547

---

## 2026-09-16 (6) — Black screen on coming back from the browser; เติมเงิน moved and resized

The user: "when I click เติมเงิน it goes to browser but when I go back to the
game it show blank page instead of continue the game. but for iphone work fine",
and "the btn เติมเงิน it should be same size as the ซื้อ the big one. and the
place ment it should be on top of the category ไอเท็มทั้งหมด".

### The window comes back; the surface did not

Reproduced on LDPlayer - a black screenshot after Back - and the cause is three
lines of lifecycle, all of it Android:

  * `APP_CMD_TERM_WINDOW` only set `st->ready = false`. EGL was never told, so
    `g_ready` stayed true and `g_surface` kept pointing at a window Android had
    already destroyed.
  * The frame loop ran on `state.booted` alone, so frames kept drawing and
    swapping into that dead surface the whole time the browser was up.
  * `APP_CMD_INIT_WINDOW` calls `RanGL_Init`, which opens with
    `if (g_ready) return 1;` - so on the way back it did nothing at all, and
    nothing ever pointed at the NEW window. Black, permanently.

iOS never had this: its CAEAGLLayer lives as long as the view, which is exactly
why the same button was fine there.

**The fix keeps the context and rebuilds only the surface.** `RanGL_SurfaceLost`
unbinds (EGL keeps a destroyed surface alive while it is current, and the next
`eglMakeCurrent` would fail BAD_SURFACE) and destroys the surface;
`RanGL_SurfaceRestore` makes a new one against the new window and re-binds. The
context survives, so every texture, buffer and shader the client uploaded is
still there - recreating it would have meant loading the whole game again. That
needed the `EGLConfig` and native visual kept from the first init; both were
locals. The swap now returns early with no surface, and the frame loop wants
`state.ready` as well as `state.booted`.

**Verified:**

    21:25:26.723 I RanGL: surface released (window gone); context kept
    21:25:32.431 I RanGL: surface restored 2560x1440

and the frame after Back is the game, not black - world, shop window, item
icons, Thai text and HUD all intact. Textures surviving is the real test here:
a wrong call on keeping the context shows up as missing art, not a black screen.

This was never about the top-up button. A call, a notification tap or the
recents switcher would have done the same; the button just made it one tap away.

### เติมเงิน: ซื้อ's size, above the category list

`BASIC_TEXT_BUTTON40` / `SIZE40` with ซื้อ's own skin, spanning the category
list's width at the top of the left panel, with the list AND its scrollbar
shifted down by the button height plus a gap and shortened to match.

Positioned from `m_pListTextBox->GetLocalPos()` rather than numbers of my own:
those rects live in Gui.rcc, which ships packed, so copying them would mean two
places to change and no warning when they disagree. The scrollbar moves with
the list because it is a separate control - a list that moved alone would
scroll from a bar that did not, which looks subtly wrong rather than broken.

It also settles where the button belongs: the first position, at the bottom of
the panel, sits under the chat window in its default place, and the chat took
the press - which is why the first two taps did nothing. The top of the panel is
never covered.

---

## 2026-09-16 (5) — Item shop: เติมเงิน opens the browser, gift buttons say ส่ง

The user: "in the item shop we should add btn เติมเงิน that when click it will
open the link https://ran-legacy-m.com/topup/ in user phone default browser",
and "the btn ของขวัญ and ส่งของขวัญ change both to ส่ง".

### There was no way to open a link at all

`CInnerInterface::OpenWebLink` already exists, but its two paths are both dead
on a phone: the embedded window is a Win32 web control the port does not have,
and the fallback is `ShellExecute`, which `shim/win/windows.h` stubs to return
NULL. So this needed a real primitive, not a caller.

`RanPlat_OpenURL` in `ran_plat.h`, implemented per platform:

  * Android - JNI into a new `RanActivity.ranOpenUrl`, which does ACTION_VIEW
    with FLAG_ACTIVITY_NEW_TASK inside `runOnUiThread`. The hop is required:
    `startActivity` from the game thread is refused, and the flag is required
    because the link leaves this task for another app's.
  * iOS - `openURL:options:completionHandler:` on the main queue. The plain
    `openURL:` is gone from the SDKs this builds against.

The button is `ITEM_SHOP_TOPUP_BUTTON` in `CItemShopWindow` - which is the
*reworked* shop, registered under `ITEMSHOP_WINDOW_RN`. `ITEMSHOP_WINDOW` is a
different class (`CItemShopWindowWeb`), and testing that one would have proved
nothing.

### The labels are CP874 bytes in the source, not new gameword entries

`ID2GAMEWORD` has no "เติมเงิน" and no bare "ส่ง", and the word table ships
packed inside Gui.rcc - a new index means repacking that file and shipping it
in a patch for three words. The literals are CP874, the encoding the UI font
and every other client string use: `byte = codepoint - 0x0E00 + 0xA0`, so
ส่ง = `\xCA\xE8\xA7` and เติมเงิน = `\xE0\xB5\xD4\xC1\xE0\xA7\xD4\xB9`. A UTF-8
literal compiles just as cleanly and draws as Latin rubbish.

The ส่ง change is `RAN_MOBILE`-only, with `#else` branches keeping the PC's
full words. Written unguarded first, which would have changed the PC client
too - the MSVC build would not have caught it, because it compiles either way.

**Verified on LDPlayer**, and only after the first two taps did nothing:

    GESTURE left(tap/drag) at (660,707)
    ActivityTaskManager: START u0 {act=android.intent.action.VIEW
        dat=https://ran-legacy-m.com/... cmp=com.android.chrome/...IntentDispatcher}
    ActivityManager: Start proc 5559:com.android.chrome

Chrome opens on the link. Both gift buttons read ส่ง.

**Why the first taps were silent: the chat window overlaps the shop's
bottom-left panel** and was taking the clicks. The button only fired once the
shop had focus and drew on top. This is not new - the cart's own ซื้อ and ส่ง
sit in the same covered strip - but it means a player whose chat is in the
default place has to raise the shop first. Worth moving those three buttons out
of that strip; not done here.

**Builds:** x86_64, arm64-v8a and MSVC ServerField all 0 errors. Both libs carry
the URL, `ranOpenUrl`, its JNI signature and both CP874 labels; the packaged
APK's classes.dex carries `ranOpenUrl`.

---

## 2026-09-16 (4) — Three touch/login complaints: close buttons, camera drag, server select

The user, in one sitting: "the close btn of every window it's so hard to click
on mobile"; "when I pres on the screen and drag to rotate the camera but too
many char on the screen and I try to press and move the camera it's overlaping
with the long press on other player"; "in the login page we do not need the
select server just straight to login page that insert user and password".

### Close buttons: the pad existed, nothing used it

`CUIControl::MouseUpdate` already has a `RAN_MOBILE` branch that inflates a
control's hit rect by `m_fTouchPad`. Two controls used it - the chat bars, at
18. A close button had none, so its tap target was exactly its art: measured
off the inventory window on LDPlayer, the glyph is **x 2458..2474, y 138..147**,
which at scale 2 is about **8 x 5 logical pixels**. That is smaller than a
fingertip, and it is the control every player reaches for.

`SetTouchPad ( 12.0f )` in `CUIWindow::CreateCloseButton` (every standard
window) and in the item and skill tooltips, which build their own close
buttons rather than going through it. 12 and not 18: the button sits in the
top-right corner, and a pad reaching further starts eating the title bar, which
is what the window is dragged by. The art does not move - only the test.

**Verified:** tapped (2466,163), eight logical pixels BELOW the art, outside
anything drawn. The window's top frame line went from 54/54 bright pixels to
0/54 - it closed.

### Camera drag opened a player's menu

`RanGesture_Tick` fired the long press on elapsed time alone: 450 ms after
touch-down the right button went down wherever the finger was, with no
reference to whether it had moved. Turning the camera slowly does not cover the
30 px drag slop in 450 ms, so the hold always won that race - and with a crowd
on screen the finger is nearly always over another player, so dragging to look
around opened that player's menu instead.

The two gestures start identically and only movement separates them, so any
real movement (`kHoldSlop`, 10 px - below the wander a resting fingertip shows
anyway) now cancels the hold and leaves the touch to become a drag when it
crosses `kDragSlop`.

**Verified, and only after three bad tests.** Screenshots cannot answer this:
a 20-logical-px swipe is indistinguishable from a tap (both open a menu), an
ESC I sent myself put the system menu over a run, and a pixel diff of the world
is useless with 200 fake players walking - no-input drift measured 45.9%
against 46.9% for a drag. So `gesturePress` now logs which gesture a touch
became. The two runs that settle it:

    slow drag    GESTURE middle(camera) at (450,300) after 965ms, moved=1
    still press  GESTURE right(hold)    at (450,300) after 451ms, moved=0

The drag rotates the camera (the old code would have said `right(hold)` at
450 ms), and a still finger still long-presses on the deadline. The log line
stays: it is one line per press, and it is the only thing that can tell these
three gestures apart after the fact.

### Server select is driven, not skipped

Thailand (and `EMSERVICE_DEFAULT`) set `m_bCHANNEL`, so the flow is server list
-> channel list -> connect. That page is not decoration: selecting a channel is
what calls `DxGlobalStage::SetChannel`, stores the group and channel through
`SetConnectServerInfo` for every later login message, and opens the game-server
connection. Jumping to the login page without it would reach the password box
and fail at the first packet.

`CSelectServerPage::MobileAutoEnter`, called from `Update` once the login
server has answered `SndReqServerInfo`: take row 0 (the list is sorted by
population, so that is the emptiest), `LoadChannel`, take the first channel
whose `m_nServerState` is not `SERVER_NOVACANCY`, `Login()`. The same
functions in the same order as the two taps. One shot - if the connection
fails, its modal stands rather than reconnecting every frame.

The row index and the channel number are not the same thing: `LoadChannel`
adds a row only for channels that exist, `m_nServerState` is indexed by channel
number, and `GetSelected` returns a row. The loop counts rows exactly as
`LoadChannel` does.

**Verified:** the client boots to the login box - ID, Pass, and the Thai
buttons - with no server or channel page.

`ld-login.sh` had three hardcoded taps for the server row, channel and connect.
They now land on the login page, so they are gone (`ld-login.sh.bak` keeps the
old one). The first patch attempt matched zero times and its assertion stopped
it - the file is CRLF.

**Builds:** x86_64 and arm64-v8a 0 errors, MSVC `ServerField` 0 errors - the
`SOURCE` edits are all `RAN_MOBILE`-guarded and the server build still stands.

---

## 2026-09-16 (3) — iPhone crashed entering the world: decoded textures were kept forever

The user: "the problem now look at the log on iPhone because I entry the game
it crash. but LDplayer is working ok."

**What the device said.** No `ran-*.ips` and no JetsamEvent for the day: on
iOS a memory kill leaves neither a crash report nor a log line, which is why
"it crashed" had no crash to read. The three `ran-*.ips` files that *were* on
the phone are from the night before, builds 68/69, and all three are
`EXC_CRASH / SIGABRT` inside `-[UIApplication _applicationOpenURLAction:]` —
SideStore's URL-scheme launch failing an NSAssertion, nothing to do with the
game. `ran.log` ends mid-sentence in the middle of character-texture loading,
2,412 lines after the last rendered frame, and the last pacing line before it
stops reads `PACE … max 1207.8ms` — a 1.2-second stall, which is what memory
thrashing looks like from inside the frame loop.

**Cause.** `d3d9_impl.cpp` released a texture's decoded CPU copy after the
upload reached GL — but only when the format was compressed. The reason was
real: an uncompressed surface may be one the client keeps writing to, and the
font atlas is locked again for every new glyph, so dropping its pixels made
each glyph re-upload an otherwise empty atlas and blanked most of the text in
the game.

Format was the wrong question. **Provenance** is the right one. A texture that
came from a file is decoded once and only ever drawn; the atlas and the
client's scratch surfaces are created empty, with no path. Freeing by format
kept every uncompressed *file* texture's decode alive for the life of the
process — and a crowd of players in costume is mostly uncompressed `.png`
skins. Measured from the phone's own log for the entry that died: 53
uncompressed file textures, **94 MB** of decoded pixels held while the GPU
already had its own copy of every one of them. Android absorbs it (the same
scene on LDPlayer sits at 1.46 GB RSS and keeps going); an iPhone's per-app
limit does not.

**Fix.** Free the CPU copy when the texture is compressed **or** came from a
file (`!m_srcPath.empty()`). `m_srcPath` is set in exactly one place —
`imageToTexture`, and only when `g_loadingPath` is set, which happens only in
the two *FromFile* loaders — so the font atlas (created through
`CreateTexture`, no path) keeps its mirror and the blank-text regression cannot
come back. Freeing stays safe even if something does lock one later:
`ensureBits()` re-creates the storage and `LockRect` marks the whole surface
dirty when it had to, so the next upload is a full one. What would be lost is
the *old* pixels, so that case now logs a warning rather than passing silently.

**Verified on LDPlayer** (both ABIs, 0 errors): logged in, entered the world,
4,100+ textures streamed. Thai chat tabs, HP/MP/SP/EXP/CP labels, level text
and character names all render; every HUD and skill icon draws. **Zero**
`partial lock of a texture whose decoded copy was freed` warnings across the
whole run — nothing in the client partially rewrites a file-loaded texture.

**Not yet verified on the iPhone.** That needs a new signed build, which is
what the version bump to 74 is for.

**Also added:** a `MEM` line next to `PACE` on iOS, once a second —
`phys_footprint` (the number the per-app limit is applied to) and
`os_proc_available_memory` (what is left of it). Without it a memory kill is
invisible from the log, which is exactly how this one hid.

---

## 2026-09-16 (2) — GM load test (fake players) switched back ON for iPhone crowd testing

The user said "still not fix. can you do the analysis why this happend? it not
happend before also enable the fake player back I will test this with iphone."

**Switch:** `SOURCE/Lib_Client/G-Logic/GLGMLoadTest.h` now has
`#define RAN_GM_LOADTEST` uncommented, with a dated note to comment it out
again before production. That restores:
- the GM tool MOB tab's Fake +10 / +50 / Clear buttons;
- `/fake_pc N`;
- the client-only crowd from the `loadtest` diag file (a count spawns that many,
  0 removes them; the file is consumed);
- the agent and field NET_MSG_GM_FAKE_PC dispatch.

**Servers:** the builds with the guard off (2026-09-14) were never deployed, so
the running agent and field should still honour `/fake_pc`. If they do not, the
servers need rebuilding and redeploying.

**Done:**
- **Android.** Both ABIs built with 0 errors; the V056 APK (versionCode 73) is
  installed on LDPlayer.
- **Commits.** SOURCE 23f0e1a (define on); MOBILE e2b0128 (V056); iOS build
  1.0.73 dispatched as run 35008278096.

**LDPlayer loadtest check failed, but not in the code.** A file created with
`adb shell echo 10 > /sdcard/ran/loadtest` is seen by DiagExists, yet fopen
fails: RanOpen `FAILED (errno 2)`, then `retry same string: FAILED (errno 13)`,
with MediaProvider "Permission to access file ... denied" every second.
Shell-created files on /sdcard/ran are not readable by the app, so
existence-only switches (uvmediump, pace30) work and content switches
(loadtest) do not. The file was removed. On iOS, `apps push` writes into the
app container as the app, so `ios-device.sh flag loadtest 10` should work.
Server fakes use `/fake_pc N` instead.

**Server build verified (2026-09-16 01:37).**
- **Path conversion.** The first attempt failed with MSB1008: Git Bash turned
  `/t:Servers\...` into a path, so it needs MSYS_NO_PATHCONV=1.
- **Toolset.** The second failed with MSB8020: v143 build tools not found. The
  projects need VS2022 MSBuild, not VS2019.
- **Working command** (from SOURCE, Git Bash):
  `CL=/I"<SOURCE>\Tik\Lua\include" MSYS_NO_PATHCONV=1 "C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" RanOnline.sln "/t:Servers\ServerAgent;Servers\ServerField" /p:Configuration=Release /p:Platform=Win32 /m`
- **Result.** Exit 0 with 0 errors; the only warnings are LNK4099 (missing
  PDBs for libvorbis/libogg). The rebuilt exes exist only in `_Bin\Tool`:
  ServerAgent.exe and ServerField.exe, both 09-16 01:37. The link log names
  `_Bin\Data\ServerField.exe`, but no exe is present in `_Bin\Data` (checked).
  They are built with RAN_GM_LOADTEST on and are not deployed. Deploy only if
  `/fake_pc` does not respond on the running servers.

**iOS 1.0.73 built:** run 35008278096 (workflow_dispatch) green. The binary
carries "fake_pc : 0 to 50", "spawned %d, now %d on the map" and
"PACE %d frames". source.json = 1.0.73 only.

**Crowd test plan (iPhone on USB, in town):**
1. Record with `syslog live --process-name ran`: 60 s without fakes.
2. Spawn about 30 fakes, client-side with `tools/ios-device.sh flag loadtest 30`
   or server-side with `/fake_pc 30`, then record 60 s.
3. Compare FRAME fps and engine cpu, PACE gaps, sections (interface, world)
   and counts (pc-seen, mob-seen).
4. Clear with `flag loadtest 0` or `/fake_pc 0`.

**ServerField crashed after spawning fakes (user, 2026-09-16).** "after I fake
some player it crash the server field ... some equipment that crash the game."
No crash artefact yet: the field server writes to
`<ServerField>\Logs\ErrorLog\log.<date>.txt` (CDebugSet, SUBPATH::DEBUGINFO_ROOT
= `\Logs\ErrorLog\`, name `log.%Y%M%D%H%M.txt`). Asked the user for the newest
one plus the console text. Everything below is code reading, not evidence.

**Checked and cleared:**
- **Null item ids.** `NATIVEID_NULL()` and `SNATIVEID(false)` are both
  (0xFFFF,0xFFFF), so no mismatch between the resolve test and the validity test.
- **Missing items at spawn.** `GMCtrolFakePC` picks ids from the field server's
  own `GLItemMan` table, and `CreatePC` → `GLChar::CreateChar` →
  `GLCHARLOGIC::INIT_DATA` resolves every worn id and calls `RELEASE_SLOT_ITEM`
  when `GetItem` returns NULL, so slots cannot hold an unresolvable id at spawn.
- **`CHECK_ANISUB`** guards both hand pointers (GLogicEx.cpp 206-207).
- **Attack range in INIT_DATA** (GLogixExPC.cpp 1255): the `else` only runs when
  `emRHAtt != ITEMATT_NOTHING`, which implies the pointer is non-NULL.
- **Weapon link skill** (`GLChar::WeaponSkillProc`): `SITEMCUSTOM(SNATIVEID)`
  sets `sSkillLinkID` to NATIVEID_NULL and the function returns on an invalid id.

**Cause found, and it is not the equipment.** The user corrected the symptom:
no crash log is written, the server just stops answering and the client sits on
an endless loading screen - the same shape as the 2026-09-13 episode.

- **The work.** `GLLandMan::FrameMove` calls `pChar->UpdateViewAround()` for
  every PC in `m_GlobPCList`, unthrottled (GLLandMan.cpp 2470-2483). Fakes are
  in that list (DropPC adds them) and pass the `EM_GETVA_AFTER` early-out
  because `GetViewAround()` sets that state at spawn (GLCharEx.cpp 1067).
- **What each call does.** Walks the quad nodes in a MAX_VIEWRANGE (250) box and
  iterates the summon, pet, PC, mob and material lists in each, taking a
  `NEW_FIELDCROW` pool node per newly seen entity.
- **Why it never crashes.** `SENDTOCLIENT` returns immediately for any id past
  twice the client slots (GLGaeaServerMsg.cpp 41), so a fake's messages cost
  nothing - but the scanning and bookkeeping are paid in full. `CMemPool::New`
  never fails: it allocates when the free list is empty. So the field server
  slows and grows instead of faulting, and no log is written.
- **The shape.** N fakes standing together = N view rebuilds a tick, each
  scanning N characters, plus a pool node per pair.

**Fix (SOURCE, GLCharEx.cpp, fakes only):**
- `UpdateViewAround` returns S_FALSE at once for a fake PC.
- `GetViewAround` does only what makes a fake visible to real players - state
  flag, `m_fMoveDelay`, `RegistChar`, `INIT_DATA` - and returns, skipping the
  entity discovery and the page of client messages after it.
- `SendMsgViewAround` needed a fallback: recipients normally come from the
  sender's own view list, which a fake no longer has, so its once-a-second walk
  would have reached nobody and every fake would stand still on real clients.
  For a fake it now walks `m_pLandMan->m_GlobPCList`, skips other fakes and
  anyone past MAX_VIEWRANGE, and sends. That is one pass per message costing the
  number of real players, not the crowd squared.
- Real players are untouched: every branch is behind `IsFakePC`.
- **Built (2026-09-16 12:44):** MSBuild 2022 Release|Win32 ServerAgent +
  ServerField, exit 0 with 0 errors; Android arm64 and x86_64 both 0 errors
  (GLCharEx.cpp compiles into the mobile client too). Not committed, not
  deployed - the exes sit in `SOURCE\_Bin\Tool`.
- Real players still discover fakes through their own UpdateViewAround, because
  the fake is registered in the land cells by `RegistChar`.

**Was also checked and cleared** (the equipment theory): fakes are dressed from
the field server's own item table; `INIT_DATA` resolves every worn id and
releases the slot when `GetItem` returns NULL; both "empty id" constants are
identical; `CHECK_ANISUB`, the attack-range branch and `WeaponSkillProc` all
guard their pointers.

**Still worth hardening later:** `VALID_SLOT_ITEM` (GLogixExPC.cpp 4553)
tests only `m_PutOnItems[slot].sNativeID` and the arm-swap slots - never
`m_pITEMS[slot]`. `SUM_ITEM` (GLogixExPC.cpp 438) then does
`SITEM &sItem = *m_pITEMS[emSLOT];`. Any path that writes a worn id without
resolving the pointer and then re-sums stats is a null dereference on the field
server. The fake generator writes `pData->m_PutOnItems[...]` directly (bypassing
`SLOT_ITEM()`), which is safe only because INIT_DATA resolves afterwards.

**Also unchecked in the fake path:** the generator never calls `ISEMPTY_SLOT`,
so a fake can be handed a weapon its class cannot hold, a left-hand-only item in
the right hand, or a two-handed weapon with the off-hand filled - none of the
rules a real player's equip goes through.

**Precedent (2026-09-13):** the fuller fake-gear version (15 slots,
school/level-matched) made live joins hang; cause never found, and the spawn was
reverted to the 6-slot version now in use.

**Disconnect seen on LDPlayer.** After login ("ขาดการเชื่อมต่อ! ต้องการออกจากเกม
หรือไม่?"), the game log up to 01:32:13 shows only rendering lines. Cause not
yet found. No more logins this cycle.

## 2026-09-16 (1) — iPhone 1.0.70: "fps drop a bit when walking, look so spinning" — measuring

User report after installing 1.0.70; it was not there before. The iPhone was
not on USB, so no numbers yet.

**What changed for iOS since it last felt right** (git log c60db66~1..HEAD):
- **UI scale.** 2.0 became 1.6375, then 1.8422 (c60db66, 89ec493).
- **Font and coverage snap.** Fractional font raster and coverage snap, UI draws
  only (2dd06aa).
- **Patcher.** Parallel downloads and parts (6fcc05b); no gameplay effect.
- **Precision.** highp texture coordinates for every draw (b2568fc).

**Findings in code:**
- **Swipe-to-turn.** The camera turns by the mouse delta in logical pixels
  (dinput_mobile.cpp PointerMove adds dx to lX / DIMOFS_X, and DxViewPort reads
  it). Logical pixels per finger pixel went from 1/2.0 to 1/1.8422, so the same
  swipe now turns about 8.6% faster than the original build. Held until it is
  measured, because walking uses the stick, not the swipe.
- **Ruled out.** The camera-lock button (the overlay only draws its glyph) and
  ui_pan.cpp (moves the focused window for the keyboard only) do not turn the
  camera.
- **Frame-rate logging.** iOS compiles platform/android/ran_app.cpp, whose
  `FRAME %.1f fps | ... ms` line logs once a second, so the fps can be read over
  syslog.

**Measurement switch added.** The "uvmediump" diag file, read in RanGLR_Init,
sets `#define RAN_UVP mediump` in every variant preamble. The base kFS falls
back to highp. This lets the iPhone A/B highp against mediump texture
coordinates with a restart instead of a rebuild. It is off by default, and the
log says "texture coordinate precision: highp|mediump".

**Built:**
- **LDPlayer (V054, versionCode 71).** "texture coordinate precision: highp" by
  default. With /sdcard/ran/uvmediump present it logs "mediump (uvmediump
  diagnostic - measurement only)". Both settings build all 8 variants with
  glErr 0x0000. The flag was removed afterwards; use MSYS_NO_PATHCONV=1 for
  adb shell paths, because Git Bash rewrote /sdcard to C:/Program Files/Git/sdcard.
- **iOS 1.0.71.** MOBILE e9369a0, run 34999864916 green. The binary carries
  `in RAN_UVP vec2 vUV;` and the switch log text. source.json = 1.0.71 only.

**Measured on the iPhone (2026-09-16, iPhone 15 / iOS 27.0, 1.0.71, highp).**
The user dropped the "spin" report: walking only. Syslog was captured with
`syslog live --process-name ran` from 00:28:49 to 00:53, one process [890], so
no restart and everything was highp.

- **Per minute.** 00:28–00:33 averaged 50–52 fps (low 43.3); 00:34–00:53 held
  58–59 at the 60 Hz cap. Interface was 10.0–10.6 ms and world 2.9–3.1 ms every
  minute.
- **Buckets.** 499 slow seconds (<55 fps) against 915 fast (≥58):

  | | slow (<55 fps) | fast (≥58 fps) |
  |---|---|---|
  | fps | 51.5 | 59.8 |
  | engine cpu | 14.77 ms | 13.03 ms |
  | gl calls | 7300/frame | 7308/frame |
  | collision | 0 rays | 0 rays |
  | buffers | 0.16 ms | 0.13 ms |
  | swap | 0.2 ms | 0.2 ms |

- **Slowest second.** 00:32:09 at 43.3 fps: cpu 17.0 ms with interface 10.0 and
  world 3.2. At 60 fps the cpu is 12.7 ms with the same sections. Mob counts
  were 10–12 seen in both.

**Conclusion.**
- **Not the GPU.** Swap is 0.2 ms slow or fast, so the highp fix (a GPU cost)
  is not the cause. The uvmediump round B was not needed; the flag was removed
  from the phone and a pull confirmed it is gone.
- **Same work.** Draw and GL-call counts are identical, so no extra code path
  runs while walking. The same work took longer, clustered in the first ~5 min
  after launch: the signature of CPU throttling (thermal, charging, Low Power
  Mode, efficiency cores), not a regression.
- **Biggest steady cost.** The interface section at ~10 ms/frame, which
  includes world name labels and the touch overlay.

**Open:** ask the user about Low Power Mode, heat/charging and when they
walked. If that is inconclusive, log ProcessInfo thermalState and
lowPowerModeEnabled in the iOS FRAME line. The frame report prints only the
top 10 of up to 48 sections (ran_app.cpp).

**Tooling fixed (uncommitted).** tools/ios-device.sh used
`syslog live --process`, but this pymobiledevice3 wants `--process-name`. It
also used container paths `ran/...` for pull/push/rm; the diag root is
Documents/ran (RanIOS_DiagRoot), so every flag push and ran.log pull had
silently failed.

**User follow-up: "it still problem ... android it's more smooth. even the LD
player smoother at 40 frame".** So the complaint is smoothness, not the fps
number.
- **Clock ruled out.** The shim's timeGetTime, GetTickCount and QPC use
  CLOCK_MONOTONIC on both platforms (win_impl.cpp).
- **Sleeps ruled out.** The only Sleep calls are in desktop DXUT paths.
- **iOS frame loop.** A CADisplayLink on the main run loop runs one full
  RanApp_Frame per tick, with no preferredFrameRateRange set.
  presentRenderbuffer does not block (swap 0.2 ms).
- **Why it looks jerky.** Frames measured 17–19 ms against the 16.7 ms vsync of
  the 60 Hz iPhone 15 panel, so ticks are missed and the gaps alternate
  16.7/33.3 ms: "50 fps" that judders.
- **Added: PACE line (ran_ios_main.mm tick).** Once a second: median, p95 and
  max gap between display-link ticks, frames over 20 ms and over 34 ms.
- **Added: pace30 diag.** Read at display-link creation; paces the loop at an
  even 30 Hz (preferredFrameRateRange on iOS 15+, else preferredFramesPerSecond)
  and logs "frame loop paced at 30 Hz". Off by default.

**iOS 1.0.72 built:** MOBILE e5a466c, run 35004924417 green. The binary carries
"PACE %d frames", "pace30 diagnostic" and `in RAN_UVP vec2 vUV;`.
source.json = 1.0.72 only, written from a scratchpad copy of the .ipa. Android
V055 (versionCode 72) was repackaged only so the iOS version moves; it is the
same code as V054.

**Test plan (iPhone on USB):**
1. Default pacing: `syslog live --process-name ran` while walking, then read
   the PACE lines (share of frames over 20 ms and over 34 ms).
2. Push Documents/ran/pace30, restart, walk the same route, then compare PACE
   and ask the user which felt smoother.
3. Remove pace30.

**Next (needs the user):**
- Publish with MAKE-PATCH (it will raise minIos to 72), then install 1.0.72 via
  SideStore.
- iPhone on USB, walking the same route in interleaved rounds with and without
  Documents/ran/uvmediump (`tools/ios-device.sh flag uvmediump` /
  `unflag uvmediump`, app restarted each round), reading the syslog FRAME lines.
- Ask whether the "spinning" happens on swipe-turn or while walking with the
  stick.

## 2026-09-15 (14) — iPhone GUI and skill slots pixelated: mediump is 16-bit on Apple GPUs

Reported: the iOS GUI "look like it down scale", and "even the slot of the
skill ... pixel of the slot not look so good", unlike Android.

**First check: scale.** A side-by-side of the same HUD from the Android build
at scale 2.0 and 1.8422 showed the fractional scale softens a little. It did
not explain the skill slots, which are drawn by the touch overlay's own shader.

**Cause, from the code.** iOS draws at full native resolution
(`contentsScale = nativeScale`), so the difference is shader precision:
- **Skill icon shader.** The overlay's icon shader (touch_ui.cpp `kTexFS`) was
  `precision mediump float`, and its sharpen works in texel units
  (`t = vUV * uTexSize`, `floor(t)`, `t - i`).
- **Main shader.** gl_render.cpp `kFS` defaulted to mediump for `vUV`,
  `uTexSize`, `uUiSharpen` and `sharpUV()`, and the pixel-grid snap derives
  from `vUV`.
- **Why only iPhone.** On Apple GPUs mediump is a true 16-bit float: above 1024
  it cannot hold a fraction of a texel, and above 2048 it steps in 2s, so all of
  that texel maths collapsed. Adreno and the emulator run mediump at full
  precision, so Android never showed it.

**Fix.**
- **touch_ui.cpp.** Both overlay fragment shaders use `precision highp float`.
- **gl_render.cpp.** `in highp vec2 vUV/vUV2`, `uniform highp vec2 uTexSize`,
  `uniform highp float uUiSharpen`, and `highp` for `sharpUV()` and `uvS`. The
  rest of the stage (lighting, fog) stays mediump for fill cost. Neither value
  is declared in the vertex stage, so link precision cannot mismatch.
- **splash.cpp.** The fragment shader is highp (boot art up to 1600 texels).

**Verified on LDPlayer (V053, versionCode 70):** 8 shader variants built,
glErr 0x0000, no touch shader or link errors. In the world the HUD, potion and
quick slots and all skill icons render at 61 fps, unchanged from before as
expected (out/highp_world.png).

**iOS 1.0.70 built:** MOBILE b2568fc, run 34994971230 green. The binary carries
`in highp vec2 vUV;` and the parts downloader. make-ios-source.js wrote
source.json = 1.0.70 only, from a scratchpad copy of the .ipa. Bumping
versionCode was required for the iOS version, and it makes Android V053 a
331 MB update with no visible change.

**Not verified yet:** an iPhone screenshot of the HUD on 1.0.70, and iPhone fps.

## 2026-09-15 (13) — iOS patch slow after SideStore install: parallel downloads + parts on iOS

Reported: "after the SideStore the patch is so slow". The iPhone was not on USB,
so there is no speed measurement yet.

**Cause, from the code.** ran_ios_patch.mm downloaded one file at a time
(`for i < todo.count` → HttpToFile). It also had no `parts` support, so
Map.rcc and Animation.rcc came whole and uncached from the origin (Cloudflare
does not cache over 512 MB). Android has had 8 workers, keep-alive and parts
since 2026-09-14 (5.9x faster, fresh install 2:11).

**Fix (mirrors RanLauncher.java downloadAll/downloadOne/downloadParts).**
- **Workers.** `kDlThreads = 8` on a dispatch group, taking entries from one
  locked counter. The first failure stops new files; running ones finish.
- **Connections.** The ephemeral session sets `HTTPMaximumConnectionsPerHost = 8`;
  the iOS default is 4, so half the workers would queue.
- **Parts.** DownloadParts checks each part hash is 64 hex characters and that
  sizes sum to the file size, then downloads, hash-checks, appends and deletes
  each part. A failed join deletes the tmp. The whole-file hash is checked
  before the rename (DownloadOne).
- **Progress.** Reported from the waiting thread every 250 ms, not per file.

**Built:** MOBILE 6fcc05b, run 34991743291 green → iOS 1.0.69 (69); the binary
carries the parts code ("parts do not add up for") plus the cache and UI-scale
fixes. make-ios-source.js wrote launcher_mobile/ios/source.json = 1.0.69 only
(the .ipa was downloaded to the scratchpad, not out/, which MAKE-PATCH sweeps).
Server at that time: store 437, iOS 1.0.68, install.html live. The next
MAKE-PATCH should clear the stale 437 set, raise minIos to 69 (store 438) and
stage ios/. No Android bump: newestInput only watches libran.so and
native/android/, and no shared file changed.

**Not verified yet:** the speed on the iPhone (needs 1.0.69 installed, on USB).

## 2026-09-15 (12) — iOS distribution: SideStore, with a Thai install page

No App Store or Google Play. MINCOM holds the RAN Online copyright and
trademark worldwide and has said it will not sell service rights, so a store
listing would be taken down on complaint. iPhones install through SideStore
with a free Apple ID: 7-day signing refreshed on the device over LocalDevVPN,
3 sideloaded apps including SideStore.

- **Source.** ios/source.json already meets the AltStore/SideStore schema,
  checked against faq.altstore.io "Make a Source": apps and news at the top;
  name, bundleIdentifier, developerName, localizedDescription, iconURL,
  versions and appPermissions per app; version, buildVersion, date,
  downloadURL and size per version. `website` now points at the guide.
- **Guide.** out/launcher_mobile/ios/install.html
  (https://ran-legacy-m.com/launcher_mobile/ios/install.html) follows
  docs.sidestore.io: iloader on the PC, Trust (iOS 15-17) or Allow & Restart
  (iOS 18+), Developer Mode (iOS 16+), LocalDevVPN, the "7 DAYS" first refresh,
  Sources + URL, Browse/install, updates, the weekly refresh and the pitfalls
  (don't delete the app, keep the same Apple ID, 3-app limit, pairing file
  after iOS updates). It is self-contained Thai with a copy button for the
  source URL, and make-manifest stages it with the rest of ios/.
- **Not verified yet:** the full flow on an iPhone.
- **No-PC note (iOS 27).** install.html gained a warning section, "ไม่มีคอมพิวเตอร์? (iOS 27 ขึ้นไป)".
  - What it says: SideInstaller (FrizzleM/SideInstaller, official site sideinstaller.net; the GitHub page
    301-redirects there) installs SideStore with on-device pairing (Settings → Privacy & Security →
    Developer → Pair with Side Installer).
  - Why only a link: its first step installs SideInstaller with leaked enterprise certificates of other
    companies (49 listed on the page, e.g. Aramco, BOC, many expired), plus a DNS profile that blocks
    revocation checks.
  - What the page does: links only to the official site, flags sideinstaller.com as fake, states the
    risks (revocable certs, third-party DNS, Apple ID typed into that app), and resumes at step 4 once
    SideStore is installed. It does not host or explain the certificate step.
  - The PC method stays the documented default.
- **Two paths (user request: separate "have PC" / "no PC").** install.html was rewritten.
  - **Top:** two choice buttons, มีคอมพิวเตอร์ (default) and ไม่มีคอมพิวเตอร์ (iOS 27+). Each shows only
    its own path; shareable links `#pc` / `#nopc`, and the page opens at the top.
  - **PC path:** requirements, prepare (LocalDevVPN, iTunes, iloader), install SideStore, allow on iPhone.
  - **No-PC path:** risk box first, iOS 27 requirements, Developer Mode + LocalDevVPN, install SideInstaller
    from sideinstaller.net ("follow that page", with no certificate instructions), install SideStore with
    on-device pairing, set up SideStore, remove the DNS profile.
  - **Shared by both:** install the game (source link + copy button), updates, 7-day refresh, cautions.
  - Rendered at 500 px for both paths; staged in out/upload/ios.

## 2026-09-15 (11) — MAKE-PATCH is one click: no --uploaded, no --min-ios, one iOS entry

The user asked for one click, not typed flags, and a patch that doesn't change
so many files.

- **No more --uploaded.** make-manifest.js asks the server whether the last set
  landed. It fetches the live manifest.json with a cache-busting query; if the
  live version is at or past PREV.version, it clears out/upload and writes the
  live ios/source.json hash into out/.ios-uploaded. Unreachable server: keeps
  the set, since re-sending is safe. --uploaded still forces a clear.
  PATCH-UPLOADED.bat is no longer needed.
- **No more --min-ios.** minIos = max(previous minIos, build in
  launcher_mobile/ios/source.json). It never lowers on its own, and a raise
  counts as a change, so it publishes. --min-ios still wins.
- **One iOS entry.** make-ios-source.js lists only the current build. Every
  history entry pointed at the same ios/RanLegacyM.ipa, so a listed 1.0.64
  really downloaded the newest file.
- **Instructions.** MAKE-PATCH.bat's closing text no longer mentions
  --uploaded, and says iOS updates come from AltStore/SideStore.

**Test run (plain build-and-publish.js --verify, no flags), exit 0:**
- The server was at 436, so the old set (V050 blob) was cleared automatically
  and the mark set to the live 1.0.67 source (6e1dec01...).
- Store 437 was built with minIos 68 ("raised to the iOS build").
- out/upload holds the new APK blob (331.6 MB), ios/ (1.0.68, differs from the
  mark) and manifest.json/.sig, all listed in UPLOAD.txt.

**Side effect, not a player issue:** the version-bump check compares source
file dates, not the binary. Comment edits and ran_ios_patch.mm (iOS-only)
after V051 made it bump again, to versionCode 69 / V052. V051 was never
published, so players go V050 to V052, the same Android code as V051. iOS
stays 1.0.68; the next iOS build will be 1.0.69.

Why there were so many versions: V049 to V052 and iOS 1.0.65 to 1.0.68 in
one day, each a published attempt at the phone UI (fit, sharpness, size).
Patch 435 to 436 changed 0 of 23,368 game files; only the app did.
Iterations should be approved on LDPlayer before a version is cut.

## 2026-09-15 (10) — iOS app updates: minIos 68, update message names the real channel

The in-game patch replaces game data only. It cannot install the iOS app itself,
because iOS forbids an app installing code over itself and a sideloaded build
must be re-signed per device. So code fixes reach iPhones only through
AltStore/SideStore (ios/source.json) or Sideloadly.

- **Gate.** Store 437 is to be published with `MAKE-PATCH.bat --uploaded
  --min-ios 68`. make-manifest carries minIos forward afterwards. Any app older
  than 1.0.68 then refuses to patch and tells the player to update. ios/ sorts
  before manifest.json, so the 1.0.68 .ipa lands before the gate does.
- **Message.** The refusal text said "Update from TestFlight or the App Store",
  but the game is not distributed there. It now says "Update the app in AltStore
  or SideStore, or install the new .ipa with Sideloadly". It ships in the next
  iOS build (1.0.69+). 1.0.68 and older still show the old wording. Committed
  with [skip ci] so it does not rebuild a second, different 1.0.68.

## 2026-09-15 (9) — Phone UI too small at 720 rows: minimum is now 640

Reported after 1.0.67: "now it's too small". A 720-row minimum put the iPhone 15
at 1.6375x. The tallest in-game windows in the UI XML are ITEMSHOP_WINDOW 605,
ITEM_REBUILD 604, PARTY_WINDOW_RENEWAL 600 and INVENTORY_WINDOW 598, so
`kMinH` in RanGL_ChooseUIScale is now 640. The iPhone 15 lays out at 1387x640,
scale 1.8422, 12.5% larger than 1.6375. The tablet and LDPlayer are unchanged
(whole scale 2).

**Verified on LDPlayer at 2556x1179:**
- Log shows `laid out 1387x640 (UI scale 1.8422)`.
- Server select, channel, login form, character select and world entry all
  worked, with taps placed from screenshots.
- In the world the inventory is fully on screen, title to Sort row, at 60 fps
  (`out/phone_scale_640.png`).

LDPlayer resolution reset afterwards. Built locally only (APK still V050 /
versionCode 67); not pushed, no iOS build yet.

## 2026-09-15 (8) — Fractional UI scale looked unevenly scaled; coverage snap + fractional glyphs

Reported: "the icon not look good, look like it down scale". Measured on
LDPlayer at 2556x1179 with 3x nearest-neighbour crops. At scale 1.6375, icon
borders and HUD letters had one-texel lines landing as 1 or 2 screen pixels
unevenly.

**Two causes.**
- **UI shader.** The D3D9 pixel-grid snap in the fragment shader
  (`floor(gl_FragCoord/s)`) is exact at a whole scale, but at a fractional one it
  is nearest-neighbour onto an irregular grid.
- **Text.** `fontSuperSample` rounded up to 2, so glyphs were rasterised at 2x
  and squeezed to 1.6375x.

**Fix (Android built, not yet pushed).**
- **Shader.** Where a logical-pixel edge falls inside a screen pixel, it now
  samples between the two logical pixels by coverage. A Python simulation of
  the formula shows max |old-new| = 0 at scales 1, 2 and 3, so the tablet and
  LDPlayer are unchanged. At 1.6375 a one-texel line covers 1.637 screen pixels
  (even).
- **Text.** Glyphs rasterise at the exact fractional scale. Bold is
  round(ss); the outline pad and offsets are rounded to texels, and the outline
  quad uses pad/ss. All of it reduces to the old values at whole scales.

**Verified.** Same LDPlayer screen, before and after
(`out/phone_scale_hud_before_after.png`, `out/phone_scale_icons_before_after.png`):
border widths are now even, and HP/MP labels and numbers are smoother. The
inventory still fits, at 60 fps. A fractional scale is still inherently softer
than an exact 2x. Not yet on iOS.

## 2026-09-15 (7) — iPhone "manifest signature does not verify" on store 435: stale cached .sig

Reproduced on the iPhone (1.0.65, launched over USB): `RanPatch: manifest
signature does not verify - refusing this update`.

**The server is not at fault.** Live manifest.json/.sig (fetched with a
cache-busting query) are byte-identical to out/upload, and the signature verifies
in node with the key pinned in ran_ios_patch.mm.

**Cause, proven from the device.** The app's `Library/Caches/<bundle>/Cache.db`
was pulled with `pymobiledevice3 apps pull` (the dev-signed build allows container
access). Its only entry is `.../launcher_mobile/manifest.sig`, stored
2026-09-14 17:20:51 UTC, value `MEQCIBNe...`. The live signature is `MEUCIFyJ...`.
manifest.json is not cached (3.6 MB, over NSURLCache's per-entry limit). The server
sends no Cache-Control, so NSURLSession.sharedSession reused the old signature
with the fresh 435 manifest. Android is unaffected: HttpURLConnection has no cache
unless an HttpResponseCache is installed, and none is.

**Fix (MOBILE 36f6dbb, iOS 1.0.66):** the patcher uses an ephemeral session with
`URLCache = nil`, `NSURLRequestReloadIgnoringLocalCacheData` and a `Cache-Control:
no-cache` request header, for both HttpGet and HttpToFile. A refusal now logs body
size and hash plus the sig text. A server Cache-Control header would not help an
installed 1.0.65: it serves the cached entry without asking the server.

**Version trap:** the first rebuild (run 34934554624) still came out 1.0.65 (65).
The iOS version is read from android:versionCode, and MAKE-PATCH's bump to
66/V049 was uncommitted. Committed (d10fb9b), then dispatched run 34934815901:
1.0.66 (66), carries both the cache fix and the UI-scale code. source.json lists
1.0.66/65/64. ios/ is staged in out/upload with patch 435. Not yet installed on
the iPhone.

## 2026-09-15 (6) — Phones: inventory taller than the screen, fixed with a fractional UI scale

Reported on an iPhone 15: the inventory ran past the bottom of the screen.
Screenshot over USB (`out/ios-device/iphone_inv.png`, 2556x1179) confirmed it.

**Cause.** The UI scale was the largest whole number keeping the client >= 1100
across. 2556/2 = 1278 passes, so the scale was 2 and the client got 1278x**589**.
`INVENTORY_WINDOW` is 598 tall (item shop 605, party 600), so it could not fit.
The tablet (1280x800) and LDPlayer (1280x720) were never short, which is why it
only showed on a phone. Any 19.5:9 or 20:9 Android phone has the same problem.

**Fix.** `RanGL_ChooseUIScale` (shim/gl/gl_context.h), shared by Android and iOS:
whole-number scale as before, but if that leaves under 720 rows the scale becomes
panel height / 720. iPhone 15: 1.6375, client 1561x720. Tablet and LDPlayer still
get 2, so they are unchanged. `RanGL_UIScale`/`RanGL_InputScale` return float;
viewport and scissor round edges (not sizes); touch divides in float on both
platforms; font supersample is ceil(scale); the renderscale divisor is only
honoured for a whole scale.

**Verified** on LDPlayer with `wm size 2556x1179`. Log shows `laid out 1561x720
(UI scale 1.6375)`. Login, server select, character select and start were all
tapped at the converted positions and hit. In the world, the inventory (key I)
is fully on screen, title to Sort row (`out/phone_inventory_fit.png`), at 54-61 fps.
LDPlayer resolution was reset afterwards.
**iOS 1.0.65 built:** MOBILE c60db66, run 34933498221 green. The log shows
d3dx_font, gl_render, gl_context_ios and ran_ios_main recompiled, and the binary
carries the new `UI scale %.4f` log string. make-ios-source.js wrote
ios/source.json (1.0.65, 2 versions); ios/ copied into out/upload.
Not yet run on the iPhone; the user is uploading it with the Android patch.

**MAKE-PATCH dropped ios/ from out/upload.** make-manifest.js stages only
blobs and the manifest, and ios/ is written into launcher_mobile/ outside the
blob set, so the new iOS build never reached the upload folder. Fixed:
ios/ is now staged whenever its source.json differs from the hash
remembered in out/.ios-uploaded, which --uploaded writes before clearing
the set. For this run ios/ was copied by hand into the patch 435 upload
(V049, versionCode 66; both libran.so carry the new scale code).

## 2026-09-15 (5) — One branch per repo: main

SOURCE: the 69 commits not yet on GitHub carried 49 Visual Studio caches under
`.vs/` (21 of them 155-186 MB, over GitHub's 100 MB limit). Stripped with
`git filter-branch --index-filter "git rm -r --cached .vs" -- origin/main..main`;
commits already on GitHub untouched, so main pushed as a fast-forward (no
force). Checked: old and new tip differ only in .vs paths, 0 blobs over 100 MB
left, origin/main still an ancestor. `.vs/` added to .gitignore. The old tip
is kept locally as `refs/backup/main-before-vs-strip` (not pushed).

The iOS workflow now checks out SOURCE `main`; `ci/ios-source` and
`tools/sync-ci-source.sh` are gone. MOBILE and SOURCE both keep only `main`
(local and GitHub); the mobile-port/effects-resolution-and-text branches are
deleted after being fully merged. Work continues on main.

## 2026-09-15 (4) — Music freeze on song change (POWER UP box) fixed; volume-0 crash not reproduced

Reported: with music on, a POWER UP box froze the game just before the song
changed; on the Tab S9, volume 0 crashed the game.

**Cause (freeze).** The shim's `WaitForSingleObject` on a *thread* handle with a
finite timeout was `Sleep(ms); return WAIT_TIMEOUT;` - it never looked at the
thread. `DxBgmSound::StopThread` waits `10000` ms for the music thread, which
exits within a millisecond of its terminate event. Every song change goes
through it: the ?-item BGM in `GLCharacter` (ForceStop -> Stop -> StopThread),
map changes (`DxLandMan`), leaving the lobby, and muting music in the options
(`AudioOption_OK` -> `SetMute` -> `Stop`). Each froze the game thread for the
full 10 s. BgmSound is the only mobile caller of a timed thread wait.

**Fix (`shim/win/win_impl.cpp`).** Thread handles carry a shared exit record
(mutex, cond, done, two refs - handle and thread). The trampoline, `_endthreadex`,
`_endthread` and `ExitThread` mark it done; a timed wait blocks on its cond until
done or the deadline, then joins; `CloseHandle` drops the handle's ref. All three
thread creators go through one `ranStartThread`. `BgmSound.cpp` (RAN_MOBILE) logs
`music thread stop: exited|TIMED OUT after N ms`.

**Measured, LDPlayer x86_64, one login:** leaving the lobby into the world
`exited after 10 ms` (was a fixed 10,000 ms by construction); muting music in
the options `exited after 15 ms`, unmuting restarted the stream (new 1 MB ring
buffer), process alive throughout. POWER UP box itself not triggered - the
character has none; it runs the same ForceStop/SetFile/Play sequence.

**Volume 0 (Tab S9 crash): not reproduced.** SFX, ambience and music sliders all
dragged to 0 and OK pressed on LDPlayer: no crash, no ANR, no RanStall. The
slider path only stores a number and the mixer maps -10000 to gain 0. Best
unverified guess: the 10 s freeze on the loop thread tripping an Android ANR on
the tablet when the music was muted - the fix removes that, but it needs the
tablet's log (wireless debugging was off; mDNS found nothing) to confirm.
Both ABIs built 0 errors. Not patched.

## 2026-09-15 (3) — iPhone "update failed": manifests had no minIos

The iOS patcher (ran_ios_patch.mm) refuses a manifest without `minIos` ("this
patch server does not support the iOS client yet"). make-manifest.js only wrote
it when given --min-ios, and MAKE-PATCH.bat never passes it: the live 434 and
the earlier 433 both lacked the key (checked), so a fresh iPhone install could
not pass the gate. ran.log could not be pulled from the phone (not written
yet), so the cause is from the code path plus the live manifest, not a device
log.

Fix: make-manifest.js carries minIos forward from the previous manifest when
--min-ios is not given, and prints a REFUSE warning when a manifest would go
out without one. Rebuilt with --min-ios 1: version stays 434 (minIos is not in
the change key), out/upload = manifest.json + manifest.sig only. Note its
summary still says "nothing changed - no upload needed"; the manifest bytes did
change and must be uploaded. Not yet verified on the iPhone.

## 2026-09-15 (2) — Full fresh install: 4.7 GB in 2 min 11 s on LDPlayer

LDPlayer wiped (adb uninstall - Android/data/com.ran.native gone; the old
/sdcard/ran moved to /sdcard/ran.bak-freshtest so it could not be adopted),
V048 installed, launched. Store 434, cache warm, parts live.

"Downloading update | 23368 files, 4693.6 MB" at 00:07:36.254 -> "Updated |
version 434" at 00:09:47.688: **2 min 11 s** (about 36 MB/s including
hashing). 13,430 files / 2,935 MB after the first ~80 s. No permission prompt.
After: .patchver 434, 4,844 MB on disk, 0 .tmp/.part files, Map.rcc sha
cf01891b.. matches, the game activity took focus. The earlier estimate for the
old path was ~142 min.

Not measured: the Tab S9 or a real phone on Wi-Fi/4G (the emulator shares this
PC's ~860 Mbps line), iOS (still one file at a time, whole Map.rcc).

## 2026-09-15 (1) — 404s were cached for a month; purged, rule now never stores errors

The user uploaded manifest 434 before its blobs. I HEAD-checked the new blobs
while they were missing, and the Cache Rule (Edge TTL "ignore cache-control,
1 month") cached those 404s. After the upload the origin had 15/15 (checked
with a cache-busting query) but Cloudflare served cached 404 for 13 of 15 -
players would have failed on Map.rcc and the V048 APK. My probing caused it.

Fixed from the dashboard: Caching > Configuration > Custom Purge by URL, the 15
blob URLs -> all 15 then 200. Cache Rule "patch" now has Status code TTL:
>= 400 -> No store; verified a missing blob returns 404 cf-cache-status BYPASS
three times. The 14 parts pre-warmed with full GETs, each SHA-256 checked;
repeat GETs HIT at 70-94 MB/s. APK blob warm: HIT, 4.1 min.

Rule for the future: upload blobs first, manifest last; do not probe new blob
URLs before they are uploaded (or use a query string, which is its own cache
key).

## 2026-09-14 (20) — Patch 434 / V048 built (parts + parallel launcher)

build-and-publish.js --verify, run from here (the user saw MAKE-PATCH look
frozen: build.sh output is piped, so nothing prints until both ABIs finish).
Both ABIs ok; versionCode 64 -> 65, V047 -> V048; store 434; Map.rcc 9 parts,
Animation.rcc 5 parts, all present and summing to the file sizes; verify ok;
exit 0. It pruned the old V047 APK blob locally (331.6 MB).

make-manifest.js did not stage an upload set: 15 new blobs = 1,172.7 MB is
over its 1 GB "wholesale" limit, so it said to send the whole store. Staged by
hand into out/upload instead: the 15 blobs (each re-hashed against its name)
+ manifest.json/.sig, .since 434, UPLOAD.txt. Live server before upload: store
433 / V047, 0 of the 15 blobs present, iOS source 1.0.64.

After upload: pre-warm the new blobs, then time a full fresh install.

## 2026-09-14 (19) — Files over 256 MB also stored as 64 MB parts (Map.rcc cacheable)

Cloudflare caches nothing over 512 MB; Map.rcc is 548 MB and came from the
origin at ~1 MB/s. The game file is unchanged - only delivery:

* `make-manifest.js`: a file over SPLIT_OVER (256 MB) is sliced into PART_SIZE
  (64 MB) blobs, each named by its SHA-256, listed as `parts: [{sha256,size}]`
  in its manifest entry. The whole-file blob is still written (old launchers
  and iOS ask for it). Slicing is deterministic. Parts count in the version
  key, the prune "need" set and fsck. Today that splits Map.rcc (9 parts) and
  Animation.rcc (293 MB, 5 parts).
* `RanLauncher.java` `downloadParts`: validates part hashes are 64 hex and sizes
  sum to the file size, downloads each part resumably, checks each part hash,
  appends it and deletes it (peak = file + one part), then the existing
  whole-file hash check runs before the rename. A failed join deletes the
  partial file.

Tested on LDPlayer against a local signed copy of the live manifest (version
433, parts added, served over adb reverse with .patchbase, both removed
after):

| test | result |
|---|---|
| Map.rcc deleted, parts store | 9 part requests, whole blob never requested, 547.9 MB in 11.1 s, sha cf01891b.. matches, no .part/.tmp left |
| one part served with a flipped byte | `checksum failed for part 3 of data/map/Map.rcc`, nothing installed, no leftovers, .patchver not written, launcher continued into the game |
| same, clean store | re-downloaded in 10.8 s, sha matches, .patchver 433 |

Not shipped yet: the live store has no parts until MAKE-PATCH runs this
make-manifest.js, and parts then need uploading (about 841 MB) and
pre-warming. iOS still uses the whole blob and one-at-a-time downloads.

## 2026-09-14 (18) — Launcher downloads 8 files at once, sockets kept alive

`RanLauncher.java`: the one-at-a-time loop is now `downloadAll` - a fixed pool
of 8 workers pulling from a shared index, first failure stops new files and is
rethrown (nothing half-written is ever renamed in, so the next launch resumes).
`httpToFile` no longer calls disconnect() on success (body read to EOF and
closed returns the socket to the keep-alive pool); `http.maxConnections` raised
to 8 (default 5 idle). Progress is reported 4x a second from the patch thread
instead of once per file (1,351 -> 121 progress lines). Blob base resolved once
instead of a .patchbase stat per file.

Measured on LDPlayer, cache warm, same test both times (delete the folder and
.patchver, launch, "Downloading update" -> "Updated"):

| | before | after |
|---|---|---|
| data/skeleton, 1,351 files, 17.9 MB | 43.8 s | 7.5 s |
| textures/mob, 1,193 files, 232.9 MB | not measured | 10.5 s (22 MB/s incl. hashing) |

Files all present afterwards, no .tmp left, .patchver 433. Not tested: the
failure path under parallel workers, the Tab S9, iOS (ran_ios_patch.mm is still
one at a time). Map.rcc (548 MB, uncacheable) is now the largest remaining cost.

## 2026-09-14 (17) — Cloudflare cache rule fixed: patch files now cached

The existing Cache Rule "patch" matched `URI Path equals /launcher_mobile/blobs/`,
which only matches the folder itself, so every blob stayed DYNAMIC. Changed
(driven from the dashboard with the user's permission) to
`starts_with(http.request.uri.path, "/launcher_mobile/blobs/")`, Eligible for
cache, Edge TTL ignore cache-control 1 month. Saved; list shows "URI Path starts
with".

Measured after: small blobs MISS then HIT, TTFB 115-417 ms -> 55-61 ms.
manifest.json still DYNAMIC (correct). Same 20 MB range of the APK blob: MISS
169.8 s (0.12 MB/s - Cloudflare pulls the whole 331 MB object from the origin
first), then HIT 0.32 s (63 MB/s) and 0.43 s (46 MB/s).

**Pre-warmed (2026-09-14 23:00-23:16).** scratchpad warm.js GET every blob
under 512 MB once through Cloudflare, 8 at a time, SHA-256 checked: 21,818
files, 4,269 MB in 15.9 min, MISS 21,815 / HIT 3, 0 failures, 4.4-5.3 MB/s
average (origin-limited). Map.rcc (548 MB) skipped - over the cache cap.
After: 40 random small blobs 36 HIT / 4 MISS, 74 ms per file on one connection
(per-request latency, not bytes, is now the small-file cost); Animation.rcc
full 3.17 s (97 MB/s) HIT; APK 332 MB full 3.22 s (108 MB/s) HIT. Cache is per
Cloudflare location and can be evicted when cold; new blobs from each patch
need warming again.

Still true: the first request per Cloudflare location pays the slow origin
fetch; Map.rcc (548 MB) is over the 512 MB cache cap and is never cached;
the launcher still downloads one file at a time with a new connection each.

## 2026-09-14 (16) — Why a fresh install downloads so slowly (analysis only, nothing changed)

Fresh install = 23,368 files, 4,694 MB (+332 MB APK); 22,796 files are under
1 MB. Measured from the dev PC (Thai ISP, 10 hops to the origin, not the same
LAN):

* Cloudflare edge (BKK) to PC: 63 MB/s (speed.cloudflare.com, 50 MB).
* Origin direct: 68.8 MB/s; 14 ms per small file.
* Through Cloudflare, uncached (blobs have no extension, cf-cache-status
  DYNAMIC): 0.86-0.94 MB/s; TTFB 109 ms of which TLS is done at 33 ms, so
  ~76 ms per request is the edge-to-origin fetch. 75 ms/file on one reused
  connection, 130 ms/file with a new connection per file.
* 6 parallel streams through Cloudflare: 3.47 MB/s total.
* Loose data compresses to 0.43 with gzip (.dds 0.43, .x 0.34); .rcc and .ogg
  do not (0.97-1.0). Store ~4.7 GB raw -> ~2.6 GB compressed.

So the slow leg is Cloudflare edge <-> origin on uncached requests, not the
player side. The launcher also makes it worse: one file at a time, a new
HttpURLConnection + disconnect() per file (Android may close the socket), and
no compression. Estimated fresh install: ~142 min now.

What others do: Riot (League) moved from binary deltas to content-defined
chunks bundled into <5,000 files, zstd, 8 parallel HTTP/1.1 connections with
range requests - updates from 8+ min to <40 s. Steam: ~1 MB compressed chunks
on a CDN. General pattern: content-addressed packs on a caching CDN, compressed,
parallel, resumable. Cloudflare CDN terms restrict large-file serving unless
the content is on R2/Stream/Images; R2 has free egress (10 GB free storage).
The CDN cache cap is 512 MB (Map.rcc is 548 MB). APAC R2 speed complaints
exist in the community - must be measured before committing.

Not decided. See the chat report for options.

## 2026-09-14 (15) — iOS 1.0.64 built on HTTPS; patch downloads slow through Cloudflare

**iOS.** Actions billing works again. SOURCE committed (287348a), synced to
ci/ios-source (fb393b8); MOBILE pushed (44f5103); run 34859841470 green in
14m42s. The .ipa carries `https://ran-legacy-m.com/launcher_mobile/` (the only
launcher_mobile string in the binary), version 1.0.64 (64), ATS
NSAllowsLocalNetworking only. make-ios-source.js wrote ios/source.json with
HTTPS URLs; the dead 1.0.45 http entry dropped; ios/ copied into out/upload.
The installed 1.0.45 cannot reach anything (HTTP closed), so the phone needs
one manual Sideloadly install. Not run on a device yet.

Note: sync-ci-source.sh does not delete files removed on the port branch -
ci/ios-source still has Lib_ClientUI/Interface/MobileCountSheet.cpp/.h, which
nothing references.

**Android patch speed.** V047 (store 433, APK 331.6 MB) downloaded at ~0.54
MB/s on LDPlayer. Same 30 MB range from the PC: through Cloudflare 0.94 MB/s
(cf-cache-status DYNAMIC - blobs have no extension, never cached), direct to
origin 68.8 MB/s. Proposed: Cache Rule eligible-for-cache on
/launcher_mobile/blobs/ (content-addressed), else a DNS-only dl. host for
blobs with the manifest kept on Cloudflare.

## 2026-09-14 (14) — Patch host moved to HTTPS (patch.ran-legacy-m.com)

**Moved again, same day:** the store now lives at
`https://ran-legacy-m.com/launcher_mobile/` (root domain; the patch. name no
longer answers). Measured before switching: manifest.json / manifest.sig
byte-identical to the earlier copies (0c213c08.. / 739cb8f7..), version 432,
Range 206, cf-cache-status DYNAMIC. All code addresses below now read the root
domain; the measurements further down were taken on the patch. name.

The server now serves the mobile store through Cloudflare, Full (strict), at
`https://patch.ran-legacy-m.com/launcher_mobile/`. The old
`http://143.14.11.244:1521/launcher_mobile/` is being closed (see below).

Changed: `RanLauncher.java` BASE_DEFAULT, `ran_ios_patch.mm` kBaseDefault,
`make-ios-source.js` default BASE. Nothing else: HttpURLConnection does TLS
itself, and manifest.sig / per-blob SHA-256 checks are transport-independent.

Measured (curl, Dalvik UA): manifest.json and manifest.sig byte-identical over
HTTPS and old HTTP (sha 0c213c08.. / 739cb8f7..); store version 432, APK 63
V046; blob 488b89cf.. (3.1 MB) full 200 with matching hash, Range 1000-1999
returns 206 with the right bytes (resume works); manifest cf-cache-status
DYNAMIC (not cached). LDPlayer, this APK: log `patch base https://patch.
ran-legacy-m.com/launcher_mobile/`, "Up to date | version 432"; with .patchver
removed it reconciled and downloaded data/effect/char/EffectChar.rcc over
HTTPS, sha 85dd3131.. matching the manifest.

Plain HTTP closed (server not live, no players on the old address): the
cleartext exception for 143.14.11.244 removed from network_security_config.xml
(loopback kept for adb-reverse testing), iOS ATS NSAllowsArbitraryLoads
replaced by NSAllowsLocalNetworking. Measured on LDPlayer: default HTTPS "Up to
date | version 432"; .patchbase pointing at http://143.14.11.244:1521 fails
with "Cleartext HTTP traffic to 143.14.11.244 not permitted". Server side the
user closes 1521/80. Test devices with the old APK must be reinstalled by hand.
No http->https redirect is relied on; Bot Fight Mode would block the launcher.
iOS not tested on the new host.

Not yet shipped: needs a MAKE-PATCH (versionCode bump) uploaded to the HTTPS
store. Libs touched after the hand build-apk.sh.

## 2026-09-14 (13) — Taps on the skill arc and the minimap no longer reach the ground

Reported: tapping a skill slot also acted on the world behind it; then check the
other new HUD pieces.

**Mechanism:** `GLCharacter::PlayerUpdate` takes a left press/release as a ground
click unless `CInnerInterface::IsCharMoveBlock()`. That flag is reset every UI
frame and set by `CInnerInterface::TranslateUIMessage` when a top-level window
reports the pointer - except for the ids on its no-block list, and only within
the window's own rect.

**Skill arc:** the slots were moved out of the tray window's rect, so the tray
never reported the pointer (and `QUICK_SKILL_TRAY_TAB_WINDOW` has its own case
that does not block anyway). Now `CSkillTrayTab::MobileArcCoversPoint` tests the
visible page's ten slots out to the ring the overlay draws (1.6 half-widths),
and `CInnerInterface::FrameMove` blocks the ground click when a press or release
lands there. Press/release only: on touch the pointer stays where the last tap
left it, and a rest-over block would stop `PlayerUpdate` every frame.

**Minimap:** `MINIMAP` is on the no-block list (the PC compass is not tapped) and
only blocked for its fullscreen button, but the mobile map tile opens the large
map. `CMiniMap` now sends `UIMSG_MOBILE_MAP_PRESS` (`UIMSG_USER2`) on a press or
release on the tile, and the `MINIMAP` case blocks for it.

**Measured (LDPlayer, temporary clicklog in PlayerUpdate, one login per round):**
before - ground click, minimap click; after - ground click only. No ground click
from: minimap, large map X, skill slot 5 (empty), skill slot 1 (filled), chat
macro 1 and 2, menu strip icon (open and close), top-right ranking button, HP
bar. The bonus time gauge still passes a tap through: it is a passive display the
PC client also does not block (like buffs and the status block), left as is.
The log was removed afterwards. Tab S9 not checked.

## 2026-09-14 (12) — GM load test (fake players) switched off for production

Asked for: disable the load-test feature and its GM menu buttons for prod, but
keep the code (it may be needed again).

**Switch:** `SOURCE/Lib_Client/G-Logic/GLGMLoadTest.h` - `RAN_GM_LOADTEST`,
commented out. Nothing was deleted; only the ways in are guarded:
- GM tool MOB tab: the "Fake +10 / Fake +50 / Fake Clear" labels and their
  `DoButton` cases (`GMGenItemWindow.cpp`) - with no label the buttons are not made;
- the `/fake_pc` command (`dxincommand.cpp`);
- the client-only crowd: `MobileLoadTestTick` and the `loadtest` diagnostic file
  (`DxGameStage.cpp`);
- agent: the `NET_MSG_GM_FAKE_PC` dispatch (`GLAgentServerMsg.cpp`);
- field: the `NET_MSG_GM_FAKE_PC_FLD` dispatch (`GLGaeaServerMsg.cpp`).
Handlers, `FrameMoveFakePC` (returns at once with no fakes) and the send guards
for ids past the slot table stay compiled.

**To turn it back on:** uncomment the define, rebuild the client and the agent
and field servers.

**Verified:** mobile x86_64 and arm64 build with 0 errors; MSBuild Release|Win32
of Lib_Client, Lib_ClientUI, ServerAgent and ServerField all exit 0 (no errors).
LDPlayer, one login: the GM tool's มอนส tab shows only its six mob/NPC buttons, no
Fake buttons. The server-side guard applies only once the new ServerAgent.exe /
ServerField.exe (in `SOURCE\_Bin\Data`) are deployed; they were not deployed.
No fake players were on the map at the time of the check.

## 2026-09-14 (11) — Skill arc: outer row spaced like the inner row

Asked for: slots 6 7 8 9 0 with the same gap between them as 1 2 3 4 5.

**Cause:** `CSkillTrayTab::MobileArrangeArc` spread both rows over the same
quarter turn (22.5 degrees a step), so the outer row, on the larger radius, was
further apart in pixels by fOuter / fInner.

**Change (RAN_MOBILE):** the outer row's angle step is the inner step times
fInner / fOuter, so the distance along the arc between neighbours is the same on
both rows; its shorter sweep is centred on the quarter, so 6 and 0 each come in
by the same amount.

**Verified (LDPlayer, one login), centres read off the screenshot (display px,
+-5):** inner 1-2 96, 2-3 98, 3-4 97, 4-5 97; outer 6-7 97, 7-8 95, 8-9 99,
9-0 97 (6-7 was ~143 before). x86_64 and arm64 build with 0 errors; both
`libran.so` touched after the test APK so the next MAKE-PATCH bumps the version.
Tab S9 not checked.

## 2026-09-14 (10) — Tips (คำแนะนำ) removed on mobile

Asked for: the settings option คำแนะนำ is not needed on mobile; disable the feature
completely.

**What it is:** `RANPARAM::bSHOW_TIP` shows `SIMPLE_MESSAGE_MAN`, the rotating
keyboard/mouse hints at the top centre ("Press 'H'...", "Holding 'Ctrl'..."),
from `CInnerInterface` every frame (`InnerInterface.cpp`, the only place that
shows it). The option is `HWOPTION_GAMEPLAY_OPTION` index 7 in the เกม tab
(`HWOPTION_GAMEPLAY_SHOW_TIP_STATIC` / `_BUTTON`).

**Change (RAN_MOBILE):** `CInnerInterface` always hides `SIMPLE_MESSAGE_MAN`
whatever a saved profile says; `CBasicGamePlayOption` hides the tip label and
checkbox. It was the last row of its column (y 184, next to แสดงชื่อ on the right),
so nothing moves. The loading-screen hint is separate (`StartThreadLOAD` is passed
TRUE by every caller) and unchanged.

**Verified (LDPlayer, one login):** no tip text in the world screenshots; the
settings window's เกม tab lists the left column down to สื่อสาร with no คำแนะนำ row.
x86_64 and arm64 build with 0 errors. Both `libran.so` were touched after the
hand-packaged test APK, so the next MAKE-PATCH bumps the version.

## 2026-09-14 (9) — Item action panel flicker on open

Reported: tapping an inventory item, the new button panel (อัพเกรด / ลิงก์ในแชท /
ทิ้ง / ปิด) flickers a bit.

**How it was seen:** screencap and screenrecord on LDPlayer keep ~3 frames a
second, so a temporary capture read the panel's rect back with `glReadPixels`
in `RanGL_Present` for 40 presented frames, alongside a per-frame log of the
panel and item-detail rects with the frame index. One frame in the set showed the
panel displaced by 28 px (14 logical) up and left; the log for that frame had the
detail at 972,262 instead of 986,276, and the panel beside it.

**Cause:** `DxGameStage::MobileArrangeInterface`'s edge pass. Once a second it
moves every visible top-level control closer than the margin (14 logical) to a
screen edge in by that margin. The pinned item detail (`INFO_DISPLAY`, 294x444
at 986,276) is flush with the right and bottom edges, so every sweep pushed it
14 px up and left; its own `RePosControl` put it back the next frame, and the
action panel, placed against the detail each frame, jumped with it. A one-frame
jump of both, once a second, for as long as the panel is open.

**Fix (RAN_MOBILE):** the edge pass skips the controls that place themselves
every frame - `INFO_DISPLAY*`, `ITEM_INFOR_TOOLTIP*`, `SKILL_INFOR_TOOLTIP*` and
`MOBILE_ITEM_SHEET`. Verified with the same capture: 60 logged frames with one
panel/detail state, no edge move, and the captured panel rows identical frame to
frame (remaining pixel changes are names moving behind the translucent frame).

**Also (first attempt, kept):** `CMobileItemSheet::Open` sets every row
`SetFlip ( FALSE )` before the panel's first frame. The panel is shown from
inside the focus list's update, which walks a copy of the list, so it is drawn
once before its first `Update`, and rows were logged still flipped from
`CreateSubControl` (`flip before 1`) - one frame of pressed-looking buttons on
the first open. Not the reported flicker, but real.

The capture and logs were removed afterwards. Tab S9 not tested.

## 2026-09-14 (8) — Selected-target panel hidden on mobile

Asked for: the info panel at the bottom right that opens when a mob, player or
NPC is selected should not show on mobile.

**What it is:** `CROW_TARGET_INFO` (mob), `CROW_TARGET_INFO_NPC` and
`CROW_TARGET_INFO_PLAYER` - `RNCROW_TARGET_INFO*`, right/bottom aligned - shown
from seven places in `InnerInterfaceSimple.cpp` (874, 933, 1068, 1211, 1245,
1268, 1318). Nothing reads their visibility; mobile code only fills them
(`SetTargetInfoNpc` / `SetTargetInfoPlayer` from `GLCharacter.cpp`).

**Fix (RAN_MOBILE):** each of the three classes overrides the virtual
`SetVisibleSingle` to always pass FALSE, so every show call leaves it hidden while
the data is still filled in. The HP bar over the target's head is a different
control and is unchanged.

**Verified (LDPlayer, one login):** auto-target selected a Little Vulgarian (red
HP bar over it, target button lit) and no panel appeared bottom right; before,
this showed "Lv.2 Little Vulgarian 180/180". x86_64 and arm64 build with 0
errors. **Not seen directly:** a player or NPC selection (a tap on a walking fake
player landed on the ground) - same override, same mechanism; the Tab S9.

## 2026-09-14 (7) — Item shop icon drawn over the menu strip tooltip

Reported: the item shop icon was on top of the tooltip, unlike the other icons.

**Cause:** the menu strip's icon tooltip is its own child (`CBasicGameMenu::m_pInfo`,
opened 40 px up-left of the finger, over the corner button row), so it draws
with `GAME_MENU` in the bottom list - not in the top list like the corner
buttons' `SHOW_COMMON_LINEINFO` tooltips. `GAME_MENU` is shown at
`InnerInterfaceSimple.cpp:3929`; `AUCTION_BUTTON` (4751), `BOSS_VIEWER_BUTTON`
(4835) and `ITEMMALL_BUTTON` (5983) come after it and were drawn over the tooltip.

**Fix (RAN_MOBILE):** in the same end-of-creation block as the world labels,
`GAME_MENU` is moved to the tail of the bottom list. Nothing else in the bottom
list overlaps the strip, so only the tooltip's cover changes.

**Verified (LDPlayer, one login, long-press on the strip's inventory icon):**
before, the shop icon covered the "ช่องเก็บของ(I)" tooltip; after, the tooltip is
drawn whole over the bag, shop and ranking icons. x86_64 and arm64 build with 0
errors. Not verified on the Tab S9.

## 2026-09-14 (6) — Pet and vehicle status boxes under the buffs

Reported: with a vehicle or pet out, its status icon sat behind the chat box.

**Cause:** `PET_STATUS_BOX` (357,538) and `VEHICLE_STATUS_BOX` (307,538) are
right- and bottom-aligned 42x35 groups; on the 1280 layout they land at about
787/837,658, which is under the chat box's tab row now that the chat is centred
on the bottom edge. Nothing on mobile moved them (only the edge pass).

**Fix (RAN_MOBILE, `DxGameStage::MobileArrangeInterface`, every frame):** the
visible boxes (vehicle, then pet) are laid left to right from the buff row's
left edge, 4 px below it (`SKILL_TIME_DISPLAY`, measured at 56,86 588x35, one
row of 14 units, so the boxes start at y 125). If the capture-the-flag holder
icon (`PVP_CAPTURE_THE_FLAG_HOLD_ICON`, 57,146) is showing in that column they go
below it instead.

**Verified (LDPlayer, one login):** vehicle called out with the bike button; its
box (bike picture + power gauge) is at the top left under the status block, and
the chat box area is clear. x86_64 and arm64 build with 0 errors.
**Not verified:** the pet box (same loop, not summoned), placement with buffs
actually showing, and the Tab S9.

## 2026-09-14 (5) — Player names drawn over buttons

Reported: the ranking icon sat behind player names while other icons did not;
then the same for all the new GUI buttons.

**Cause, two parts.**
- The UI bottom list draws in the order groups were first shown
  (`CUIMan::ShowGroupBottom` InsertTail, `RenderList` head to tail).
  `GLOBAL_RANKING_BUTTON` is shown at `InnerInterfaceSimple.cpp:3597`, before
  `NAME_DISPLAY_MAN` at 3640; every other corner button comes after it.
- The touch pad (`RanTouch_Render`, own GL path) was drawn in
  `DxGameStage::Render` before the whole interface, and the names are part of
  the interface, so every name painted over the pad buttons.

**Fix (all RAN_MOBILE).**
- End of `CInnerInterface` creation: the world labels (`SIMPLE_HP`,
  `NAME_DISPLAY_MAN`, `PRIVATE_MARKET_SHOW_MAN`, `TARGETINFO_DISPLAY`,
  `DAMAGE_MAN`, `HEADCHAT_MAN`, `ITEM_SHOP_ICON_MAN`) move to the head of the
  bottom list in their original order, so every HUD button draws above them.
- `CUIMan` gets an underlay hook (`SetMobileUnderlay` id + func). `CUIMan::Render`
  walks the bottom list itself and, right after the last world label, flushes
  `CUIRenderQueue` and calls the pad draw. `DxGameStage` hands over
  `RanTouch_Render` and only draws the pad itself when the interface is off.
  Resulting order: world labels, touch pad, HUD, windows, top list.

**Verified (LDPlayer, one login):** "Little Vulgarian" is clipped by the corner
buttons; "Load010", "Load030" and "Little Vulgarian" are clipped by the pad's
skill, eye and crossed-swords buttons; with the inventory open the window still
covers both the names and the pad buttons. No crash in the log. x86_64 and arm64
build with 0 errors. Not seen: a name right on the ranking icon itself (the
cause is the same list order, fixed for all), and the Tab S9.

## 2026-09-14 (4) — A minimap in place of the compass (mobile only)

Asked for: no compass on the screen; a minimap the same size in its place, with
a faded transparent edge; the original minimap (`CLargeMapWindow`) untouched.

**What changed**
- `CMiniMap` (`MiniMap.cpp/.h`), all `#ifdef RAN_MOBILE`: the dial
  `MINIMAP_BACK` is hidden but kept as the position, the needle
  `MINIMAP_DIRECTION` is not drawn, and a new `CMobileMiniMap` draws into the
  dial's global rect (98x97 logical). The compass centre point stays on top as
  the player marker. `SetMapAxisInfo` forwards the level's `GLMapAxisInfo`.
  Nothing on the PC path changed.
- New `Lib_ClientUI/Interface/MobileMiniMap.h/.cpp` (whole file RAN_MOBILE,
  added to `sources_Lib_ClientUI.cmake`): loads the level's minimap texture
  through `TextureManager`, centres on `GetCharacterPos()` using
  `CLargeMapWindow`'s own world-to-texel formula (in floats), one texel per
  logical pixel and north-up, the same as the large map. The fade is a 64x64
  radial alpha texture made once (opaque to 62 % of the radius, smoothstep to 0
  at the edge) on stage 1, read with coordinate set 1. All states it changes
  are saved and restored.
- Shim stage-1 **mode 7**: a second 2D texture with `COLOROP SELECTARG2/CURRENT`,
  `ALPHAOP MODULATE TEXTURE/CURRENT` and `TEXCOORDINDEX 1` multiplies alpha by
  that texture's alpha at `vUV2` (`d3d9_impl.cpp`, `gl_render.cpp`). Before this
  the configuration fell through to mode 0 and the mask would have been
  ignored.

**Verified (LDPlayer, x86_64, one login):** in the school (`w_school_01`) the
compass is gone and the map sits in its place, the edge fades out into the
scene, and the view is centred at the Sacred Gate where the character stands.
The red dot under the centre marker is part of the map art (the texture has a
red marker at texel ~604,964 by the gate), not a stray draw. No stage-1 warnings
in the log. arm64 and x86_64 both build with 0 errors.

**Not verified:** the Tab S9; other maps and map changes (the texture is released
and reloaded when the name changes); how it reads while moving.

**Follow-up (same day):**
- Player marker is now the large map's own `LARGEMAP_MARK` (CharInven.dds
  305,321 37x37, the blue arrow disc), centred on the map and turned by the
  camera direction with the same maths as `CLargeMapWindow::UPDATE_CHAR_ROTATE`,
  linear filtered. The compass centre dot is no longer drawn.
- Tapping the minimap toggles `LARGEMAP_WINDOW`, the same call as the menu
  strip's map button: `MINIMAP_BACK` got id `MINIMAP_MOBILE_MAP` and stays
  visible for input, hidden only while the group draws.
- `MENU_LARGEMAP_BUTTON` removed from the menu strip (`BasicGameMenu.cpp`, with
  quest and item shop): chat macro / item bank / run slide to 168 / 193 / 218,
  strip 75 px narrower. The M key still opens the map.
- Verified on LDPlayer: marker shows as on the PC large map; the strip shows 9
  icons with no map icon; a tap at the minimap opened the large map. arm64 and
  x86_64 build with 0 errors.

## 2026-09-14 (3) — Interface icon outlines like the PC

Reported: some HUD icons have an "off" outline, not like PC. PC reference: the user's
screenshot of the top-right menu buttons (shop, stats, shield, magnifier, bag, Q, arm,
blue). On PC each has a 1-texel black outline on all four sides; on mobile it was solid
top/left and grey or missing right/bottom.

**Measured** on the shop button (`chatting_group_aa.dds` 35,36 35x35, UV inset +0.25 texel
from `InterfaceCfg`), mean luminance of the outline band, 0 = black:

| | left | top | right | bottom |
|---|---|---|---|---|
| PC capture | 0.0 | 0.0 | 0.0 | 6.5 |
| mobile before | grey | grey | ~0 | 80.7 |
| mobile after | 0.0 | 0.0 | 0.0 | 6.5 |

**Two causes, both fixed:**
1. **My HUD edge margin put controls on fractional positions** (`SOURCE/Lib_Client/DxGameStage.cpp`).
   The margin is 2% of the height (14.4 on 720), moves were not rounded: the top-right group
   went 940,0 -> 925.6,14.4. A fit of the texture to the capture put the button at 858.5, 68.5.
   Now the edge pass and the bonus gauge round to whole logical pixels (log: 940,0 -> 926,14).
   This alone left the outline uneven (bottom 80.7 -> 43.3).
2. **Magnified interface art sampled at GL pixel centres** (`MOBILE/native/shim/gl/gl_render.cpp`).
   The GUI is laid out at 1280 and drawn at 2x; a pre-transformed D3D9 pixel samples at its
   whole-number centre, GL at +0.5, so the second screen pixel of every 1-texel line blended
   with its neighbour. Simulated against the texture: D3D9 1:1 sampling magnified 2x equals the
   PC capture exactly (error 0.0); the old sampling error 18.3, a half-pixel shift 10.4.
   The fragment shader now samples magnified pre-transformed textures (texels > 1.33 px, main
   framebuffer only, new `uPanelH` uniform = framebuffer height, 0 in render targets) at the D3D9
   sample point of the fragment's logical pixel. Everything else keeps sharpUV.

**Verified (LDPlayer):** outline numbers above; the whole menu row matches the PC shot;
HP/MP/SP/EXP text and the level box are pixel-identical before and after (glyphs are
rasterised at the drawn size, one texel a pixel, so they are not snapped); no shader errors.

**Not verified:** Tab S9; other windows and art (inventory, dialogs) against PC captures.

---

## 2026-09-14 (2) — Effect weapons (Load002's bow, the GM's spear): three shim bugs

Reported: Load002's `m_gt_bow_flame_red.cps` bow "did not load correctly", same
kind as the GameMaster's weapon (`w_gt_spear_flame_red.cps`). The piece loads
(`ok` in the per-slot log); what is wrong is how its effects draw. Both pieces
carry DxEffCharSingle x2-3 (flame .egp), DxEffCharLevel, DxEffCharMultiTex (+Blur).

**1. The device was reported as HARDWARE vertex processing** (`shim/d3d/d3d9_impl.cpp`
GetCreationParameters). The PC client's `CGameClient2Wnd::ConfirmDevice` refuses every
HARDWARE mode, so on PC the device is MIXED. `DxEffectMan::InitDeviceObjects` clears
REALSPECULAR (and keeps the software-shader path) without MIXED, and every
DxEffCharLevel layer past ambient returns early.
- Measured (effprof): DxEffCharLevel ~60 calls/s, **0 draws**. Now reports
  MIXED|MULTITHREADED: realspec 1 on all 64 logged pieces, Level ~48 draws/s.

**2. Level's specular layer: stage 1 SELECTARG1(cube by camera-space normal) was not
implemented** ("stage 1 cube map with op 2 ... is not implemented"), so the pass drew
stage 0 = white TFACTOR, additively, lit - the whole weapon washed white-orange.
- Added stage-1 mode 6 (`gl_render.cpp` shader + RanGLR_SetStage1, `d3d9_impl.cpp`):
  cube colour replaces the stage-0 result, no vLit (no stage reads DIFFUSE).
- Verified on LDPlayer: warning gone, variants `04c2`/`0cc2` built, spear blade and
  shaft show their own dark metal with a faint reflection (screenshots before/after).

**3. D3DRS_BLENDOP was ignored** - no glBlendEquation anywhere, every blend was ADD.
Effect meshes use SUBTRACT/REVSUBTRACT/MIN/MAX (`DxEffectMesh`, `DxEffectParticleSysDraw`),
the sky REVSUBTRACT, glow and toon MAX. Measured on these flames: most
`flame_sword_eff.dds` layers are blend 4 (MAX). Now mapped in RanGLR_ApplyState.
- Verified: flames no longer blown out. Not verified: the sky and glow passes
  (no sky in the test view).

**4. Dark rectangles / flameless planes: CloneMeshFVF threw the UVs away** (`shim/d3d/d3dx_mesh.cpp`).
With a different FVF it copied the first min(stride) bytes of each vertex. Effect meshes
load as XYZ|NORMAL|TEX1 and DxSimMesh clones them to XYZ|TEX1, so the UV it read back
was the normal's x,y. Every flame / smoke plane sampled one texel: flat hard-edged squares,
and the flame sprite sheets never showed their frames.
- Found by: PC reference screenshots from the user (bow and spear fully in flame, soft
  smoke), a mesh-effect skip bisect (only the `black_hall_1304251.dds` layer made the
  squares), then a load-time UV log against the file parsed offline.
- Measured: `gt_plane.x` loaded UV (0,0) on every vertex vs 0.037-0.963 in the file;
  `att_001.x` all (0,1); `fire100827.x` all (0.081,0.781).
- Fix: copy each element (position, normal, psize, colours, each tex set) from its source
  offset to its destination offset, as D3DX does. After: `gt_plane.x` (0.963,1)(0.037,1)
  (0.963,-0.006)(0.037,-0.006), `fire100827.x`, `plane.x`, `att_001.x`, `04_plane.X` all
  equal the file. Screenshot: spear head and tip are red-orange flame spikes with a soft
  glow and no squares, like the PC reference.
- Scope: every DxSimMesh effect mesh cloned to a smaller FVF (135 meshes logged at load)
  had wrong UVs before; all effects using them change.
- Also measured, not a bug: MultiTex's 128x128 target holds real fire colour
  (D9A521 / B34600 / 8B2100 at probed texels).

Not compared yet at close range against the PC shot: Load002's bow (same effect files).

**Also:** the `texprobe` diagnostic crashed the emulator client
(SIGSEGV in the emulator's GLClientState::checkFramebufferCompleteness from
RanD3D_ProbeTextures). Pre-existing tool, not the fix; do not use it on LDPlayer.

Kept: `effskip` now also applies to DxCharPart::RenderEff (the Single/Blur pass).
Not verified on the Tab S9.

---

## 2026-09-14 — Costumes that never appeared: compressed meshes zlib refused

Reported: some characters' costumes do not show like on PC (Load002 given as the example).

**Measured**, with a temporary per-slot log in `GLCharClient::UpdateSuit` (removed
again) over 86 fake players: 14 worn pieces failed to load. Load002 itself was not
one of them: the server sent it a weapon only (slots 0-4 empty), so it shows the
default body - that is what the server gave it, not a load failure.

The 14, by cause:

| Cause | Pieces | Same on PC? |
|---|---|---|
| **MSZip stored block with no NLEN** (fixed) | Mihawk_body, Mihawk_feet, Nami_hand, w_cos_op_carrotsulong_hand, w_cos_december_body, m_sbx_jf_naruto_link_body | No - D3DX9 loads them |
| `.cps` not in client data | w_merchant1, Tai0004_W_E, w_sword_fire, w_cos_bm_leg, m_shark_upperbody_white | data gap |
| mesh `s_m_cos_mummy.x` not in client data | m_cos_mummy_hand | data gap |
| whole `.cps` byte-encoded `(b ^ 0x34) + 0x30`, header included | w_cos_holdem_leg (56 files have this; 15 `xv*` and 4 `yoyoman*` another variant) | MiniA runs the same LoadPiece, not tested on PC |
| truncated read past EOF | w_hp_ADexPL | the check is in MiniA too, not tested on PC |

**The fix** (`native/shim/d3d/xfile_parse.cpp`). Some `bzip` `.x` files end an MSZip
block with a stored deflate block that has LEN and then the bytes, no NLEN. zlib
stops with "invalid stored block lengths" and the whole mesh was dropped. Evidence:
- `d3dx9_43.dll` on this PC (`D3DXLoadMeshFromXW`) loads all five meshes: mihawk.x
  23543 faces / 18269 verts, naruto_link 37032 / 21970, nami 26942 / 18787 ...
- Decoding the block as LEN + data gives exactly the header's total size and a
  token stream that parses to the end with those same counts; reading NLEN gives
  a short output and a bad token part way.

zlib now decodes one block at a time (`Z_BLOCK`); at each block boundary a stored
header whose NLEN does not match is copied by hand and zlib restarted after it
with the last 32K as dictionary. Everything else stays zlib's.

**Verified:**
- Old vs new decoder built for x86_64 and run on the emulator over every `.x` in
  `skin/`, `skinobject/`, `object/` (769 bzip): 755 byte-identical, the 14 the old
  one refused now decode to their exact size and parse (also lwing_m/w, m_brooks,
  m_garp, w_blackwindow_eg, s_m_sbx_cos_l2_jdk, s_w_sbx_cos_aion_lfpl_a10a,
  s_w_cou_natsirtbspidey, s_w_blackcatyb_soraka_weapon). 0 failures left.
- In game (LDPlayer, one login): the 6 pieces log `ok`; Load016 wears the blue
  december dress, Load005 Mihawk's cloak, Load054 the carrot tail.

**Not verified:** Tab S9 (arm64 lib built, not run); whether PC shows holdem / hp_ADexPL.

### Still open
- Fake players are dressed more sparsely than the generator intends (about a third
  of armour slots filled against 80% per slot) - server side, not looked at.
- 5 missing `.cps` + `s_m_cos_mummy.x`: data.
- The `(b ^ 0x34) + 0x30` whole-file `.cps` encoding: check on PC before deciding
  whether to decode it.

---

## 2026-09-13 — Chat macro buttons (ALT+1..0) on the chat box

Asked: buttons for the chat macros that are set; tap sends like ALT+n; long press repeats
every 5 s; repeat blocked for general chat; the row follows the chat when it is expanded.
User's choices: repeat refused for general chat (tap still sends), tap the same button to
stop, one row on the chat's top edge.
PC mechanism read first: texts in `RANPARAM::ChatMacro[10]` (per-character .gameopt), set
in CChatMacroWindow; ALT+n -> `CInnerInterface::AddChatMacro` -> `CBasicChat::AddChatMacro`
-> `CBasicChatRightBody::AddChatMacro`, which takes the channel from the text's first
character (@ private, # party, $ to-all, % club, ! alliance, ^ regional, none = general) and
runs slash commands. Anti-spam `IsPapering` (general / to-all / regional only): the same
text more than 7 times in a row -> 30 s chat ban; kept as on PC.
Touch: a tap is left down/up; a 450 ms hold is a right press (touch_gesture.cpp) — the
long press is read as RB down on the button.
Implementation (RAN_MOBILE): `CMobileChatMacroBar` (new), id MOBILE_CHAT_MACRO_BAR; buttons
skinned like the chat channel tabs (SIZE19_RECT, 28x19); the repeating button shows
"n:secs" in orange; a repeat send waits while the player is typing. Placed every frame by
`CInnerInterface::MobileChatMacroFrame` from DxGameStage right after the chat is placed.
Data: `MOBILE_MACRO_BAR` / `MOBILE_MACRO_BUTTON` (uiinnercfg02.xml), gameword
`MOBILE_CHAT_MACRO` (3 Thai messages).

First device run (23:48, macros set in the J window: Alt+1 `macro1`, Alt+3 `#macro3`):
buttons 1 and 3 only, on the chat's top edge. Tap 1 -> `[GameMaster]:macro1` in chat,
correct. Long press: WRONG — the refusal printed 6 times for button 1, and button 3 printed
start / send / stop over and over. `CHECK_MOUSE_IN_RBDOWNLIKE` is true on every frame of a
held right button, so the toggle ran each frame. Fixed with a hold latch (acts once per
press; released by a frame with no right button on a macro button).

Second run (23:52), after the latch: tap 1 -> one `macro1`; long press 1 -> the refusal
once; long press 3 -> start message + `#macro3` at once, button "3:4" in orange; ~6 s later
a second `macro3`, label "3:1"; tap 3 -> stop message, label back to "3". All correct.
Found: the macros set in the J window were EMPTY after relaunch. The profile (.gameopt,
macros included) is written only by SAVE_PLAYERPROFILE on a clean shutdown
(DxGameStage::DeleteDeviceObjects); a force-stopped / swiped-away Android process never gets
there. Mobile: CChatMacroWindow's OK now saves the profile.
Chat expand (23:52): dragging the chat's grip up to ~y=535 carried the 1 / 3 buttons to the
new top edge in the same screenshot.
Save verified (23:55): after OK, `Logs/PlayInfo/GameMaster.gameopt` in the private data root
was rewritten with `CHATMACRO0 = macro1` and `CHATMACRO2 = #macro3`. Reading it back on the
next launch is the unchanged PC LOAD_PLAYERPROFILE path — not re-tested (one login per cycle).
**Not verified:** the Tab S9; the keyboard lifting the chat with the row on it.

User: the number is not in the middle of the button. Measured: the skin's label box
`BASIC_TEXT_BUTTON_IMAGE_TEXTBOX191` is 5,4 36x15 inside the 19-high button and aligns
CENTER_X only, so text starts at y=4 (the chat tabs have the same offset; Thai marks above
the line hide it, a digit does not). Fix in the macro bar only: `SetUseDynamic(FALSE)` (the
dynamic press/release code is what snaps the label back onto the skin's box), then
`CreateTextBox("MOBILE_MACRO_BUTTON_TEXT" 0,0 28x19, CENTER_X|CENTER_Y)`. Cost: the button
no longer shifts 1 px while pressed.
Measured after that build (device rows, LDPlayer): button frame 1059 / 1094, inside
1060..1093, middle 1076.5; digits "1", "3", "3:4" all at rows 1073..1089, middle 1081 —
still 4.5 device px (~2 logical) low. CENTER_Y centres the font's line box, which keeps room
above for Thai marks. The label box is now 28x15 (top 0), which lifts the text 2 logical px.
**Verified (00:0x, 09-14):** frame 1059 / 1094 (middle 1076.5); digits "1" 1070..1085
(middle 1077.5), "3" and orange "3:4" 1069..1085 (middle 1077.0) — within 1 device px.

## 2026-09-13 — Potion tray collapse arrow restored

**Verified on LDPlayer (23:11):** arrow drawn at the tray's right end (beside the gear); tap
-> tray collapses to one slot with the open arrow beside it; tap the open arrow -> full tray
back. Works with the edge margin (the group moves as one). Tablet not checked.

User: the arrow overlaps the auto-pot gear. Measured: layout (loose and packed) has the gear
at 226..246 and the arrow at 246..261 — flush, no gap, so they read as one block and are
easy to mis-tap. Mobile-only: the arrow moves 3 logical px right (the slots' own spacing).

The user noticed the potion tray's collapse button missing. Not caused by the edge margin:
an earlier tablet pass ("The item tray collapse arrow is gone", below) removed it on purpose —
`BasicPotionTray` did not create QUICK_POTION_TRAY_CLOSE_BUTTON under RAN_MOBILE, and
`CUILeftTopGroup::Update` hid QUICK_POTION_TRAY_OPEN_BUTTON and forced the tray open every
frame. Both mobile overrides removed, so the PC code runs as-is: close arrow on the tray,
open arrow while collapsed.

## 2026-09-13 — HUD kept one margin in from every screen edge

Asked: the chat already sits one small margin above the bottom; do the same on every side,
because phone panels curve at the sides. The chat's margin is `fH * 0.02f` (logical), about
29 device px on LDPlayer's 1440-high panel. Measured before: the status block, quick slots
(top-left) and the minimap / date / server name (top-right) sit flush at y=0 and the screen
sides. The touch overlay already keeps >= 40 device px from the sides.
Change: `MobileEdgeMargin()` in DxGameStage (one number, used by the chat too) and a pass
on the 1 s `MobileArrangeInterface` sweep that moves every visible, non-full-screen
top-level control inside that margin. Each move logs `RanEdge id ... -> x,y`.

First build (clamp each control on its own), measured by the log and a screenshot: right
side correct (minimap 940,0 -> 926,14; the icon rows follow it). Top-left wrong two ways:
* ids 4 (LEFTTOP_CONTROL_GROUP) and 5 (BASIC_INFO_VIEW) were re-logged every sweep —
  `CInnerInterface::MoveBasicInfoWindow` puts them back on their dummies every frame;
* the portrait (0..41) moved in under the bars (42..204): the block is separate controls
  laid edge to edge, and clamping each alone broke it.
Second build: controls chained edge to edge (gap <= 2 px, overlapping across) move as one
group by one amount; after a move `MobileFollowBasicInfoDummies()` puts the dummies where
the controls now are.

**Verified on LDPlayer (23:06).** Every RanEdge move logged exactly once (28 controls, no
repeats). Top-left block moved as one: portrait 0,0 -> 14,14, bars 42 -> 56, quick slots
205 -> 219, level bar 42,72 -> 56,86; screenshot shows the block intact and inset. Top-right:
minimap 940,0 -> 926,14, the server name moved ~27 device px in from the right, both icon
rows followed. Ping/fps line (id 136) 2 -> 16. Chat unchanged at the same margin. The touch
overlay already sits >= 40 device px from the sides and was not changed.
**Not verified:** the Tab S9; windows opened later near an edge (they will be moved in the
same way, and a window dragged against an edge is moved back in within a second).

User: the bonus-time gauge did not come down with the HUD. `BONUSTIME_EVENT_GAUGE` (XML
560,28 111x19, UI_FLAG_RIGHT) resolves to 1040,28 at 1280 wide (logged) — under the minimap's
date/clock, which moved to 926,14. Hidden until an event and touching no edge, so the pass
never saw it. It now keeps its layout position moved by the margin like the minimap
(x - 14, y + 14), applied while hidden too, logged as `RanEdge bonus gauge`.
First build moved y only (1040,28 -> 1040,42, logged); x added after.
**Verified (23:19):** log `bonus gauge 1040,28 111x19 -> 1026,42`, once, no RanEdge line
repeats. The gauge itself was not seen on screen — it only shows during a bonus-time event.
Arrow gap verified (23:17): zoomed crop shows the gap; tap collapses, open arrow reopens.

## 2026-09-13 — Settings "ฟังก์ชัน" tab: the PC F-key hotkeys

Asked: a tab next to "เกม" in the settings window (OPTION_HW_WINDOW) that exposes the PC
F-key hotkeys, Thai, mobile only. After a first version the user cut it to **F9, F10, F11
only** (F1-F4 page selection, F6 auto-pots and F8 GM camera removed).
* F9 character simple (InnerInterface, one master toggle over both flags) -> checkbox
  "ซ่อนเครื่องแต่งกาย (F9)".
* F10 hide skill effect -> checkbox "ซ่อนเอฟเฟกต์สกิล (F10)".
* F11 event scoreboard (`ToggleCDMRankingHotkey`, returns at once off a CDM/CW map) ->
  checkbox "ตารางคะแนนกิจกรรม (F11)", label grey (DARKGRAY; `DISABLE` is RED) off an event.
Design: the checkbox queues the F key; DxGameStage::MobileTouchControls releases it with
SetKeyState(DXKEY_DOWN) at the start of the next frame, ahead of the DxGameStage and
InnerInterface key checks, so each runs its PC handler. The page only reads state back.
Files: `Lib_ClientUI/Interface/FunctionOption.*` (new), BasicHWOptionWindow (tab + page),
DxGameStage (drain), InnerInterface.h (`MobileInCDMEvent`), cmake source list; data
`_inner_hwoptionwindow.xml` (four 54 px tabs, three rows) + gameword `HWOPTION_FUNCTION`,
repacked into Gui.rcc. Code under RAN_MOBILE.

**Device data root trap.** The app reads `/sdcard/Android/data/com.ran.native/files/`
(`pickDataRoot` tries externalDataPath first), not `/sdcard/ran`. A Gui.rcc pushed only to
`/sdcard/ran` changed nothing — the settings window kept three tabs. Push to the private root.

**Verified on LDPlayer (x86_64 build, 22:44).** Four tabs, three Thai rows. One tap each:
F11 off an event -> nothing, correct; F9 on -> "ตอนนี้ชุดตัวละครเป็นแบบจำลอง", box on, crowd
drawn simple; F10 on -> "ซ่อนเอฟเฟกต์สกิล", box on; F9 and F10 off -> "แสดงเต็มแล้ว" /
"แสดงเอฟเฟกต์สกิล", boxes off. No crash in logcat. **Not verified:** F11 on a CDM/CW event
map (needs a running event), the Tab S9.

## 2026-09-13 — Fake players on the server (in progress)

The 09-12 crowd lives only in one client. The user asked for a crowd the server owns, so the
emulator and the tablet see the same players at the same time and the sync traffic is real.

### How it works

`Fake +10` / `Fake +50` / `Fake Clear` run `/fake_pc N` (0 clears), which sends
`NET_MSG_GM_FAKE_PC` (GCTRL+3990) to the agent.

* **Agent** (`GLAgentServer::MsgGmFakePC`, `USER_MASTER` only) owns the gaea id space. It
  takes ids out of `m_FreePCGIDs` into a reserved pool, capped at a quarter of max clients,
  and never returns them. A clear only makes them reusable. The next spawn travels the same
  connection as the clear, so the field has emptied them by then. It sends
  `NET_MSG_GM_FAKE_PC_FLD` (GCTRL+3991) with the ids to every channel.
* **Field** (`GLGaeaServer::GMCtrolFakePC`) acts only where the GM stands. Each fake:
  - random class, school, face, hair and level 100–150;
  - equipment per slot from the item table, filtered by `dwReqCharClass`;
  - `CreatePC` with client id `FAKEPC_ID_BASE (0x7F000000) + gaea id`;
  - `GetViewAround()`, which registers it in the land cells so nearby clients receive it.
* **Walking.** `FrameMoveFakePC` makes about a third of the crowd walk or run somewhere
  each second (a real `SNETPC_GOTO`), so the movement traffic is real.
* **Clear.** Every field removes its own fakes with `DropOutPC`.

Why it can't touch real players (each from reading the code):
* `CClientManager::IsOnline` indexes `m_pClient[dwClient]` with no range check.
  `CFieldServer::SendClient` and `SendAgent(dwClient)` now reject ids ≥ `m_nMaxClient`,
  so a fake's id never reads past the array or reaches a real slot.
* `CreatePC` refused client ids ≥ max×2; ids in the fake range are now let through.
* `ClearReservedDropOutPC` saves every character leaving, and a server stop sends every
  character through it. Fakes are now dropped there with no `CDbActSaveChar`.

**Join refused after a load test, nothing on the server window (2026-09-13 18:26).**
The user's joins failed with "cannot access character data" after spawning 50 fakes, and
no error showed. Every agent-side refusal writes to the agent window; every refusal in
`GLGaeaServer::CreatePC` wrote only to `Logs\ErrorLog`, so the silent refusal was the field.
The one path found by reading the code where a fake blocks a real join: the agent's set-aside
gaea ids live only in agent memory, so an agent restart while the field keeps its fakes hands
those ids to players and `m_PCArray[_dwGaeaID]` refuses them. Not confirmed on the live server.
ServerField now (1) removes a fake whose gaea id a real player needs, with a console line, and
(2) prints every `CreatePC` refusal and its reason on the server window. ServerField rebuilt,
0 errors (`_Bin\Tool\ServerField.exe`, 18:26); not deployed, not run.

User then ran that ServerField: the client sat on the join wait box (`CHARACTERSTAGE_GAME_JOIN`,
60 s, no reply of any kind) and ServerField printed nothing. So `CreatePC` never ran or never
refused; the request stalls somewhere earlier and every hop on that path returns silently.
Added a `JOIN TRACE <n>` console line at each hop, both servers (18:39, 0 errors):
1 agent `CreatePC` ok · 2 agent `GameJoinToFieldSvr` sent to field (or STOP: char gone) ·
3 field `MsgGameJoinChar` received, then DB `CGetChaInfoAndJoinField` loaded · 4 field
`MsgFieldReqJoin` created · 5 agent `MsgLobbyCharJoinField` sends client to field (or STOP:
gaea mismatch / no field in cfg) · 6 field `MsgJoinInfoFromClient` entering game (or STOP: no
char). The last number printed names the hop that stalls. Not deployed yet.

User deployed both; ServerField showed GameMaster's join complete (3, 4, 6), then the spawn line,
then nothing, and joins hung. User: it worked before the full-gear change. Checked the one
server-side difference that could stall a frame (level taken from item requirements, uncapped):
`GLNEEDEXP` bounds-checks the table and level-up only fires when exp changes, so that is not
it. Cause still not found. **Reverted the spawn to the version that worked** (6 slots, 20% left
empty, level 100-150, no school/level filtering). ServerField rebuilt 18:51, 0 errors; mobile
x86_64 compiles. Kept: join trace, `CreatePC` refusal lines, fake eviction. Full gear is open.
**User deployed it: joins work again with fakes spawned (2026-09-13).**

### The MSVC build was already broken, by earlier port work

The first server build stopped on 106 errors, none in this change: port edits that
were only safe on mobile.
* **`GLCharacter.cpp`:** the `Mobile*` definitions (1451–2009) had no guard, but their
  declarations do.
* **`GLCharacterMsg.cpp`:** `RanShop_PurKey` is defined under `RAN_MOBILE`, but its call site
  wasn't guarded. Restored `pNetMsg->szPurKey` for the PC build.
* **`DxCharPart.cpp`:** `RAN_SECTION` was used, but its header is included only on mobile.
  Added the `((void)0)` fallback, same as `DxGameStage.cpp`.
* **`DxSoundMan.h`:** the member `operator<` was made `const` without a guard, which made
  MSVC's free operator ambiguous.
* **`LoadingThread.cpp`:** `LOADPROBE`, `RanGL_ProbeState`, `RanD3D_DumpTexture` and
  `RanPlat_Log` were used unguarded. Now guarded, with a `((void)0)` `LOADPROBE` for PC.
* **`lua_tinker.cpp`** can't find `lua.h`: the headers are in `Tik/Lua/include`, but the
  project lists `Tik/Lua`. Not edited; the build passes `CL=/I"<SOURCE>\Tik\Lua\include"`.

Result (2026-09-13): `MSBuild RanOnline.sln "/t:Servers\ServerAgent;Servers\ServerField"
/p:Configuration=Release /p:Platform=Win32` → **0 errors**. `_Bin/Tool/ServerAgent.exe`
(15:30) and `_Bin/Tool/ServerField.exe` (15:31) both contain the new "GM load test" code.
The projects sit in the `Servers` solution folder, so the target names need that prefix.
Mobile: arm64 and x86_64 both 0 errors.

The server exes in `_Bin/Tool` date from 08-18, before the port began, so nothing built
from this tree since then would have compiled. The 08-18 exes are backed up in the session
scratchpad before the rebuild overwrote them.

### Still open

* Build `ServerField` + `ServerAgent` (VS 2022, v143). **Not deployed.** The user chooses
  between the live server and a local stack (the RAN databases aren't restored locally).
* Verify on the emulator and the tablet at once: two GM-capable accounts, one login each.
* The PC client (`MiniA.exe`) was not rebuilt — by the user's choice. The client-only
  projects have not been compiled by MSVC since the port began and may carry more
  unguarded mobile edits. PC players will receive the fakes (plain server broadcast); a PC
  GM cannot spawn them (`/fake_pc` and the buttons are `RAN_MOBILE`-only).
* Publish the APK with `MAKE-PATCH.bat` only after the device test passes.
* **User confirmed the server crowd works (2026-09-13).** Two findings from their test:
  - ~~Some effects on the crowd render as plain black panels~~ **Fixed and A/B-verified.**
    `OPTMCharParticle` blends alpha-textured character particles with
    `DESTBLEND=DESTALPHA`. D3D reads destination alpha as 1 on the X8R8G8B8 back buffer
    and on the X1R5G5B5 scratch targets; GL kept a real alpha channel holding whatever
    the last blend wrote, so each particle replaced the scene under it with black.
    `RanGLR_SetTargetOpaque` (from SetRenderTarget, by surface format) now maps
    DESTALPHA→ONE and INVDESTALPHA→ZERO on alpha-less targets. On one live frame, blobs
    appear only with `nodstalphafix`. Found with two new live switches:
    - `sectionskip`: drop every draw inside one named frame section; that put it in `optm`.
    - `blendlog`: one line per new blend state per section; that put it in
      `optm:charparticle`, `dst=7`.

    Ruled out along the way by measurement: shadows (`shadowcount 0`), every
    character-effect type (`effskip`), the MultiTex render-target pass.
  - 100 fake players halve the frame rate, 60 → 30. Profile and bring it back to 60.
    **Measured so far (LDPlayer, ~85–96 players in view, debug build):**
    - 2026-09-13 19:07, ~100 fakes, 32 fps: `uiflushlog` names one flush site,
      `RanDevice::SetRenderState`, ~175/frame. By state: LIGHTING 0→1 131/frame,
      ZENABLE 0→1 35/frame, FOGCOLOR 8, SRCBLEND 1 — the shim text sprite's `End()`
      restoring state after each `CD3DFontX` draw (`D3DFontX.cpp:395`). gl_render never
      lights XYZRHW (`gl_render.cpp:2794`), so LIGHTING cannot change a UI batch. Trying
      `uibatchkeep` (off by default): batch survives no-op sets and screen-space LIGHTING.
      A/B 19:17 (same session, ~82 players seen): off 80 flushes/frame, 42–45 fps; on
      61/frame, 44–46 fps. LIGHTING and then CULLMODE (also never applied to XYZRHW,
      `gl_render.cpp:2774`) stop flushing, but SetTexture takes 21/frame of them back and
      ZENABLE (29/frame) is real — depth applies to UI (`gl_render.cpp:1831`). **Gain is
      within noise; left off, no visual change.** The interface is not the lever.
    - Same capture, sections: world 15.6 ms, ch:parts 11.1 (part:skinned 7.8,
      part:chareff 2.6), touch-hud 3.1; 1,369 draws/frame at 7 µs. Characters are the cost.
    - simpleperf 19:19 (15 s, ~100 fakes, 31 fps): render thread 99.5% of samples, **libc
      memcpy 74%**, callers not unwindable (dwarf or fp). Buffer uploads only ~290 KB/frame,
      texture uploads 0. ~10,800 GL calls/frame; LDPlayer's encoder copies each into its pipe.
    - Skip-switch A/B 19:25 (10 s each, baseline 31.0 before / 31.8 after): nulldraw 60,
      **nouniform 46.5**, nostream 42.8 (also drops those draws), notex 33.1, noattr 23.6
      (worse). Attribute setup is NOT the cost; uniforms are the largest category.
    - Uniform counters 19:32 (measurement only, 100 players seen, 28 fps, 1,790 draws):
      palette (uWorldM, 16 matrices) 1,160 calls **1,160 KB/frame**; single matrices 1,210
      calls 75 KB; light block 2,100 calls 57 KB; small 870 calls 7 KB. The palette is ~88% of
      uniform bytes, sent whole by `applyProgramUniforms` (`gl_render.cpp`) on nearly every
      skinned draw. Not yet measured: how many palette slots a draw actually reads.
    - Palette by draw kind 19:38 (97 players seen, 28 fps): **every skinned draw is indexed**
      (LASTBETA_UBYTE4) — ~1,140/frame, each uploading the full 16-matrix palette; ~630
      non-skinned draws upload none; blend-1/2/3 (non-indexed) draws: 0. So "send only the
      2-4 blend matrices" does not apply. Open: how many palette slots each indexed bone
      combination can reach, which decides whether a partial upload is safe.
    - Slots per indexed draw 19:42 (99 players seen, 27 fps, 1,170 indexed draws/frame):
      1-4 slots 520, 5-8 280, 9-12 214, 13-16 155, **mean 6.4 of 16**. Read from code:
      vertex palette indices are slots inside their group (`d3dx_hierarchy.cpp:551-592`),
      the engine fills WORLDMATRIX(0..NumBlend) and sets VERTEXBLEND = NumBlend
      (`DxSkinMesh9_NORMAL.cpp:121-206`), so a draw never reads past slot VERTEXBLEND.
      Uploading only those slots would send ~40% of the palette bytes (~1,160 KB -> ~470 KB
      per frame); the call count (1,170) would not change. Not yet known whether bytes or
      calls are what costs on LDPlayer — the A/B of that change is the test.
    - **`palettetrim` A/B/A 19:47 (same session, ~98 players seen):** off 28.8 fps (render
      29.8 ms) / **on 34.1 fps (render 24.9 ms)** / off again 27.8 fps. Palette bytes 1,136 ->
      ~450 KB/frame, calls unchanged — so on LDPlayer the uniform BYTES cost, not only calls.
      No encoder errors. Visual check: see below.
    - Light block uploads by cause, same capture: program cache stale ~290/frame, light
      count changed ~85, values changed ~38. Each is 6 GL calls, so ~2,500 calls/frame, and
      most are the same lights re-sent because each shader variant owns its own uniforms.
    - **Single-window A/B is not reliable with the fakes walking**: players in view drift
      91-99 and fps swings more than the effects measured (one run had trim OFF at 40.6 vs
      ON 32-34). Use interleaved rounds. `palettetrim` interleaved 19:58, 6 rounds x 8 s,
      92-99 seen: **on 32.4 / off 28.2 fps, on faster in every round (+4.2)**. Now the
      default; `nopalettetrim` turns it off.
    - Cost switches (measurement only, draw wrong while on) at 95-97 seen, single windows:
      `nomatrixuni` +0.5, `nosmalluni` +1.1 over a 33.7 baseline — not worth pursuing.
    - Interleaved 6 rounds x 8 s, 93-98 seen: `nolightblock` base 31.4 / skip 33.2 (at most
      +1.8, small); **`nostream` base 32.6 / skip 50.0 fps, +17 in every round, draws
      unchanged (~1,730)**. nostream only skips `g_streamVerts.write` (gl_render.cpp
      ~2605) — the draws still go out. So writing streamed vertices is the largest cost
      found, while the buffer report says only ~290 KB/frame and 0.02 ms on the CPU side.
    - 20:13, new counter: the draw path streams **~150 client-array writes, ~800 KB per
      frame** (not in the buffer report, which only counts dynamic-VB writes, ~340 KB).
      `streamsub` (glBufferSubData ring instead of the persistent map), interleaved 6 rounds
      at 60-72 seen: persistent 45.2 / subdata 45.8 fps — no difference. The write path is
      not the cost; the streamed bytes are. Next: which frame section streams them.
    - 20:22, stream sources by section + texture (~64 players seen): **interface, FVF 0x144,
      texture 2980 (2048x2048): ~63 writes, 680-700 KB/frame, largest write 810-1,782
      verts** — about 25,000 vertices a frame on one texture. Next biggest: interface tex
      2395 66 KB, world-eff dynamic VB 72 KB, optm:sequence 42-60 KB. Checking whether 2980
      is the font atlas and how many quads a glyph costs before choosing a fix.
    - Read from code: 2048x2048 matches the shim font atlas (`ATLAS_W` grows to fit). On
      mobile `CD3DFontX::DrawText` always takes the immediate path and, with
      `D3DFONT_SHADOW`, draws the string at every offset of a (2r+1)^2 square except the
      centre and then once more (`D3DFontX.cpp:404-472`) — 9 draws at r = 1. `drawRun`
      emits 6 verts x 28 B per glyph (`d3dx_font.cpp` drawRun). ~65 names x ~7 glyphs x 9
      x 168 B = ~690 KB, which is the measured 680-700 KB on that texture. Candidate fix
      (not built): one outline draw per glyph from a pre-combined mask; stacking one colour
      8 times gives coverage 1 - prod(1 - a_i), so the mask can match exactly if the
      outline colour is opaque. Checking radius, colour and the atlas packer first.
    - Confirmed: `m_iOutLine` = 1 (`D3DFontX.cpp:69`), names use `_DEFAULT_FONT_SHADOW_FLAG`
      = SHADOW|KSC5601, outline colour ARGB(255,10,10,10) — opaque.
    - **`nooutline` (measurement switch, `D3DFontX.cpp`, RAN_MOBILE) interleaved 20:32, 6
      rounds x 8 s, 94-100 seen: outline on 28.0 fps / 1,540 KB streamed; skipped 38.8 fps
      / 207 KB. +10.7 fps in every round.** The outline is the largest single cost found.
      Building the one-pass mask behind `outlinemask` (off by default) for a pixel compare.
    - **`outlinemask` built** (`d3dx_font.cpp` outlineFor / RanD3DXFont_DrawOutline;
      `D3DFontX.cpp` RAN_MOBILE calls it before the offset passes, which stay as fallback).
      Interleaved 20:41, 6 rounds x 8 s, 95-99 seen: **8-pass 26.9 fps / mask 35.0 fps, mask
      faster every round (+8.1)**; streamed 1,440 -> 350 KB/frame; no "glyph atlas full".
      HUD number crops (static, outlined font): **0 of 403,200 pixels differ** between the
      8 passes and the mask; control on the same crop, outline on vs skipped: 80,752 differ
      (20%, max 245), so the crop does go through the outline. **Now the default;
      `nooutlinemask` restores the passes.**
    - With palette trim + outline mask on, 20:53, interleaved 4 rounds x 8 s, 93-99 seen:
      `nostream` 35.9 -> 40.0 (+4), `nouniform` 35.6 -> 49.7 (**+14**), `notex` 35.8 -> 38.8
      (+3). The uniform kinds already measured account for ~4 of the +14; `nouniform` also
      skips `useVariant`/glUseProgram and each variant's own uniform cache. Counting
      program switches and variant changes per frame next.
    - 20:56, counters (~95 seen, 43-44 fps): **~650 glUseProgram / ~650 variant changes per
      frame** against ~1,690 draws — 38% of draws switch shader, and each switch brings a
      variant's own uniform cache (stale matrices, lights, material re-sent). Measuring which
      key bits flip and the uploads on switching draws before choosing a fix.
    - 21:00, variant key bit flips per frame (~665 changes, ~96 seen, 38.5 fps): **alpha test
      (b8) 352**, stage1 gloss mode (b5+b7) 178, indexed blend (b11) ~91, lighting (b1)
      ~89, pre-transformed (b0) ~57, texture (b10) 12, fog 5 (b12-31 = 1: first switch
      from the 0xFFFFFFFF sentinel). Uniform uploads on switching draws: ~1,770 calls,
      175 KB per frame (~30% of uniform calls). Reading the alpha-test shader path next.
    - `stickyatest` (reuse the alpha-test-on variant with uAlphaRef -1 for alpha-test-off
      draws; output identical) interleaved 21:05, 6 rounds, 79-99 seen: switches ~670 ->
      ~535, b8 flips ~355 -> ~240, but **fps 41.6 off / 40.8 on, lower every round**.
      Program switching is not the remaining cost. **Reverted.** Next: `nopaletteuni`
      (skip only the palette upload) against `nouniform`, to see whether palette bytes
      are what is left of the uniform cost.
    - 21:09, interleaved 5 rounds each (67-98 seen, crowd drifting, paired rounds):
      `nopaletteuni` 44.5 -> 47.4 (**~+3**; trimmed palette ~330-430 KB is now small);
      `nouniform` 43.5 -> 54.4 (**~+11**). The rest (~8) is spread over ~1,200 matrix,
      ~2,000 light-block and ~950 small uniform calls plus ~650 program switches, each
      worth ~1-2 fps alone. Profiling the current build before choosing the next lever.
    - simpleperf 21:14 (current build, ~89 seen, 41 fps): libc memcpy **56%** (was 74%);
      sections world 13.7, ch:parts 10.0 (skinned 7.4), **touch-hud 3.5 ms**, ch:pose 1.8.
    - `nohud` (existing switch) interleaved 21:16, 6 rounds, 84-96 seen: HUD 39.6 / none
      41.9 fps (~+2.3, noisy). touch_ui.cpp: the static half (~71,000 verts) is cached in a
      VBO and not re-uploaded; the joystick (~12,000 verts, ~290 KB) is rebuilt and sent
      through glBufferSubData every frame even when not held. Candidate: cache the idle
      stick (depends only on centre and radius); keep the held stick live.
    - **`stickcache`** (touch_ui.cpp: resting stick captured into its own VBO, rebuilt only
      when position/size/knob change; held stick live) interleaved 21:26, 6 rounds, 92-97
      seen: **live 36.9 / cached 39.6 fps, cached faster every round (~+2.7)**; touch-hud
      section 2.6-4.1 ms -> 0.5-2.2 ms; 0 rebuilds/s. The stick crop differs 9.17%, but the
      stick is translucent over ~95 moving players, so a pixel diff there cannot separate
      background motion from a wrong stick — checking the crops by eye.
      By eye the stick is the same (ring, bevel, rim light, 8 ticks, knob); only the player
      walking behind it differs. **Now the default; `nostickcache` builds it live.**
    - The `attrib` call counter adds a flat 10 per FVF change, but the ES 3.1 path issues
      ~21 calls per re-specification, so the real attribute traffic is unknown. Counting
      re-specifications and glBindVertexBuffer calls exactly next.
    - 21:28, exact (~95 seen, 33-40 fps, ~1,680 draws): **~122 FVF re-specifications a frame
      (~21 GL calls each, ~2,560 calls) + ~1,190 glBindVertexBuffer = ~3,750 attribute
      calls**, so the flat-10 counter (~3,950) was about right. touch-hud 1.2-1.9 ms with the
      stick cache on. Candidate: one VAO per FVF with its format set once (~-2,300 calls a
      frame). Care needed: this is where v426 broke ("sendVertexAttributes bad offset");
      disabled-attribute constants are context state, and the vertex/element buffer
      bindings are VAO state, so both have to be handled per VAO. Reading the bind code.
    - **`fvfvao`** (gl_render.cpp: a VAO per FVF, format described once at creation; each
      VAO keeps its own buffer/base/stride record; RanGLR_DeleteBuffer clears records naming
      a deleted buffer; the map is cleared with the context) 21:36: re-specs **139 -> 0 a
      frame**, buffer binds ~1,250 -> ~1,180, **0 encoder errors** ("sendVertexAttributes /
      bad offset") with it on. Interleaved 6 rounds, 94-98 seen: shared VAO 42.9 / per-FVF
      44.3 fps (~+1.4, faster in 5 of 6). Screenshots with it off and on: characters,
      weapons and effects draw normally (no flat dark shapes). **Now the default;
      `nofvfvao` goes back to the shared VAO.** Not yet checked on the tablet.
    - Running total on LDPlayer, ~95 players in view: ~27-28 fps at the start of this work
      -> ~44 fps with palette trim, outline mask, stick cache and per-FVF VAOs (each
      verified by interleaved A/B). Still short of 60; remaining measured levers are small
      (uniforms spread over many calls ~+11 if all removed, streamed bytes ~+4, texture
      binds ~+3).
    - Combined default build 21:40 (x86_64, 0 errors; arm64 also compiles, 0 errors):
      47.2 fps with ~71 players in view, 0 attribute re-specs, draw-path stream 244 KB,
      touch-hud 0.8 ms, 0 encoder/GL errors; crowd and HUD draw normally.
      **Not run on the Tab S9 yet** — everything above is LDPlayer only.
    - Still open for 60 fps with a crowd: light block as a shared uniform buffer (~+1.8 at
      most), remaining uniform calls per draw, texture binds, and a tablet measurement,
      since the emulator's encoder cost is not the tablet's.
      (One earlier attempt measured a stale APK after a failed build — build step now stops
      the script on any error.)
    - `nulldraw` (every GL call a draw makes dropped): steady **60 fps**, render 9.5 ms,
      engine CPU 3 ms. The client's own work is small; the frame goes on issuing about
      1,650 draws and 10,000–12,000 GL calls.
    - Priced by live `sectionskip` (same scene, baseline 28–31 fps):
      - `part:skinned` → 40 fps, submit 16.6 → 5.6 ms. Skinned character pieces, about
        11 ms.
      - `interface` → 41–45 fps, submit about 7 ms less. Only about 210 GL draws, yet
        about 31 µs each against 10 µs for a world draw. UI draws stream their vertices;
        world draws use their own buffers. Interface CPU with its draws dropped: 0.7 ms.
      - `part:chareff` → 33.5; `glow-tex` → 32.5; `optm`, `eff-group`, `touch-hud`,
        `part:rigid`, `environment`, `weather` → about 30 (each 1–2 ms).
    - simpleperf (debuggable build): 73–99% of main-thread samples in `memcpy` with no
      unwindable caller (DWARF too); with `nulldraw` it falls to 45%, the swap. It is
      the GL submission path, not the engine.
    - Tried, measured, **no gain:** the state epoch now moves only on a real change
      (d3d9_impl.cpp Set*State). Interface cost is unchanged, so redundant sets were
      not what split the UI batch. Kept, as it is correct and harmless.
    - **Incident, store version 426 (APK versionCode 58, V041).** The patch was built from
      the working tree while it still held two changes that had never run on a device:
      - the per-attribute vertex-format cache in gl_render.cpp;
      - the state epoch moving only on real changes (d3d9_impl.cpp).

      On LDPlayer, every character drew as a flat dark shape and UI text smeared. The
      emulator's GL encoder logged `sendVertexAttributes bad offset / len` on every draw.
      Both changes are **reverted**, back to the behaviour verified on screen before
      them; the destination-alpha fix and the diagnostic switches stay. The attribute
      cache's failure mechanism is not identified: the touch overlay and the splash use
      VAOs of their own. The fix goes out as the next patch, after a device check.
    - Next measurement needed: why UI draws flush, counted by reason. Then decide
      between cheaper streaming and fewer flushes.
    - These are emulator numbers; the Tab S9 is the target and prices GL calls
      differently. Four logins were used this cycle; the next changes are batched into
      one build and one login.
  - Fakes must wear **full gear**, every slot, or the frame-rate number flatters itself. In
    the user's screenshot most wear nothing at all despite an 80% chance per slot, so
    something strips the gear before it is seen — find it, then fill every slot.

## 2026-09-12 (2) — A crowd test, and the 8 bytes that killed the process

The ask: a GM-menu button that fills the screen with fake players, to see what a
crowd costs. It is in the **MOB** tab as `Fake +10`, `Fake +50`, `Fake Clear`,
and it is also driven by a file, because a button behind a GM account is no use
to an automated run: put a count in `loadtest` under the diagnostic root and the
client spawns that many, `0` removes them all. The file is consumed, so it can
be re-armed.

Each fake player picks a class at random out of all sixteen, a random school,
hair, face and level, is placed at a random angle and 60-300 units from the
player, and is dressed one slot at a time from the item table - filtered by
`dwReqCharClass`, which is the same test the client makes before letting a piece
on - with about one slot in five deliberately left empty. A crowd of identical
nudes stacked on one spot would measure overdraw, not characters.

Spawning is queued: three a frame. The first version placed ten in one frame,
which is ten characters' worth of disk loading on the thread that also answers
the window system, and Android killed it.

### Then it died anyway, and not for that reason

Twelve fake players took the process from 890 MB to 7.5 GB of **native heap** in
ten seconds, every time, and the kernel killed it - `SIGKILL`, so no tombstone,
no stack, nothing. Three instruments, in the order they were needed:

* a resident-size print either side of every `DropChar`, which said the growth
  was one character, not the crowd;
* `meshload` under the diagnostic root, which makes every mesh and every file
  open report itself - that named the last file before the silence;
* **`RanPlat_WatchdogArm`** - a watchdog thread that watches resident size and,
  past a limit, aborts *the thread that armed it*. `debuggerd` then dumps that
  thread as the crashing thread with its backtrace in the log, no root needed.
  That is the tool that turned "the process vanished" into one line.

The backtrace named `DxEffCharLine2BoneEff::LoadFile`, and the diagnostic line
underneath it named the cause:

    s_m_kk_illust_body.cps: line2bone ver 0103, property 232 bytes in the file, 240 bytes here

`EFFCHAR_PROPERTY_LINE2BONEEFF` embeds two `CMinMax<float>`, and `CMinMax` has a
**virtual destructor**: four bytes of vtable pointer in the shipped files, eight
here. `LoadFile` read `sizeof(m_Property)` bytes, stopped eight bytes short, and
the next thing in the stream is a list length - so the loader asked for a few
hundred million list entries. The flat read is wrong even where it fits: it
lands file bytes on the vtable pointers of a live object.

Fixed the way the 0101 and 0102 records already were: a field-wise reader that
skips four bytes where the vtable pointer sat, plus a seek to the end of the
record the file itself declares. That seek needs the size from *before* the
branch - every branch declares its own `dwSize` and hides it, and a seek written
against that local goes backwards to the start of the record.

`CMinMax` is used by no other property struct, so this file is the whole family.

### Two more, found only because a hundred characters is a hundred chances

* `DxSkinMesh9::FindMeshContainer` ran `strcmp` on a container name that can be
  `NULL`: `SetupNameOnMeshContainer` names a mesh after its frame or its parent
  frame, and a frame with neither leaves it null. The walk tests every container
  in the file, not only the ones that could match.
* `DxSkeleton::UpdateBones` wrote through a null `pBoneRoot` - a skeleton whose
  file produced no bones. It now says which file, once, and skips it.

Both guarded under `RAN_MOBILE`; neither changes a byte for MSVC.

### The crowd curve

LDPlayer, 60 Hz cap, the same spot, camera unmoved. `present` is idle time, so
read `render`:

    players    draws/frame    render      fps
      0            295         5.0 ms      60
     10            385         5.4 ms      60
     30            500         7.9 ms      60
     60            747         8.9 ms      60
    110           1142        13.3 ms      53

`Fake Clear` returns it to 292 draws and 4.2 ms. The tablet was off for this
run; the same build needs one pass there, since the emulator's 60 Hz cap hides
everything above 16.6 ms and the Tab S9 runs at 120.

### Still open

* The curve above is the emulator's. Repeat it on the Tab S9.
* Several fake players still render with black blocks around them.

### The rest of the family, guarded

The other 32 effect loaders read their property the same way, so they carry the
same risk. `RanReadRecord` in `basestream.h` now stands in front of all of them.

It deliberately does **less** than the first version of it. The record length
these loaders write is not always the property alone - `DxEffCharNeon` and four
others count the property *plus* the material array that follows it, and a guard
that seeked to the end of the record threw that array away. What the length can
always catch is the one case that is never right: a struct **bigger here than
the whole record it lives in**, which is exactly the shape that killed the
process. So the guard reads the struct as the client always has when it fits,
and when it does not it names both sizes, reads only what is there, and stops at
the record boundary. Measured at 110 characters: no site reports a mismatch, so
`DxEffCharLine2BoneEff` was the only one.

The 15 loaders under `DxEffect/Single/` were left alone - they read a matrix,
an affine block and a property out of one record, and are a different shape.

### Also found, by running 110 characters

`b_9th_arwens_ear.x` loads as a skeleton with no bones. Its attachment now says
so once instead of dereferencing a null root; why the file produces no bones is
not yet known.
  See `native/out/ld_crowd60.png` - the same flat effect panel the user deferred,
  now reproducible without a buff card.

---

## 2026-09-12 — The file index is lower-cased; the lookup was not

Second root cause behind the flat weapon effects, and this one reaches much
further than effects.

`CFileFindTree::PathRecurse` builds the index with **lower-cased** keys:

    std::transform ( strName.begin(), strName.end(), strName.begin(), tolower );
    m_mapFile.insert ( std::make_pair(strName,strPath) );

and `FindPathName` looked the caller's string up **as given**:

    FILEMAP_ITER iter = m_mapFile.find ( str );

So any asset whose stored name carries a capital missed the index —
`"1d_Lighting.bmp"` against `1d_lighting.bmp` on disk. On Windows that miss cost
nothing: the fallback opens the file by its own name and NTFS does not care
about case. Android's storage does, so the texture resolved to nothing, and an
effect with no texture draws flat colour the shape of its own quads.

Both lookups now lower-case the query, which is what the index already assumes —
`#ifdef RAN_MOBILE`, so the PC build is byte-identical.

**Verified on the Tab S9:** the weapon's fire effect went from grey cards to
orange fire with its texture, 121 fps, nothing else changed.

Not fully closed: pale translucent panels remain around the fire. `1d_Lighting.bmp`
is a 24-bit BMP — no alpha channel — and the load carries no colour key, so those
quads are opaque on any platform, PC included. Whether they are wrong therefore
needs a PC reference rather than another guess.

---

## 2026-09-11 (11) — The texture stages started at zero, and zero means DIFFUSE

"the effect of the weapon that i WEAR right now it not render correct ... you
see the the effect of the weapon that show sqare effect plain pic there?"

Grey wedges the shape of the effect's quads, around the flame. Four things were
ruled out by measurement first: the two vehicle-era optimisations (a runtime
toggle for each, all four combinations identical), the S3TC path (`nos3tc`, CPU
decode, identical), and the untextured-subset path (a probe that names any
subset drawn flat - it never fired for this effect). The effect data itself
reads correctly: parsed offline, `black_9th_freedom_wings.egp` gives mesh
`gt_plane.x`, flag `0x00218000` (USEOTHERTEX), texture `gt_sword_eff3-3.dds`,
blend mode 5, and the client's draws show exactly `blend=1(5,6)`. The texture
has a real alpha channel - 17% of it fully transparent.

Right geometry, right blend, right texture. So the fault was the **fragment
alpha**, and it was in the device's construction:

    memset(m_textureStageState, 0, sizeof(m_textureStageState));

and nothing else. Zero is a legal value for every field there and it *means*
something: `D3DTA_DIFFUSE` for an argument. D3D9 starts stage 0 at
`COLOROP=MODULATE, COLORARG1=TEXTURE, COLORARG2=CURRENT, ALPHAOP=SELECTARG1,
ALPHAARG1=TEXTURE, ALPHAARG2=CURRENT`, and the engine sets only what it wants
to change: `DxEffectMesh`'s state block sets `ALPHAOP=MODULATE` and
`ALPHAARG2=TFACTOR` and says nothing about `ALPHAARG1`, because in D3D it is
already TEXTURE.

On our device it was DIFFUSE, so every such draw computed
`alpha = diffuse.a * tfactor.a` and **never sampled the texture's alpha**. The
artwork's soft edge lives entirely in that channel, so the quad painted solid -
"square plain pic".

The stages now start where D3D9 starts them. Verified on the Tab S9: the wedges
are gone, the flame and the weapon read cleanly, world/HUD/text unchanged, 121
fps.

**The lesson, and it is not the first time in this port:** a shim that zeroes
its state is asserting a default, and the client reads that default. `memset` is
only right where zero is the documented value.

---

## 2026-09-11 (10) — The mesh cache put streamed data where it could not live

"the effect of the weapon that i WEAR right now it not render correct" — a
regression from the mesh buffer cache an hour earlier, and the defect was in the
lock flag.

`ensureGpuBuffers` filled the mesh's vertex buffer with `D3DLOCK_DISCARD`. That
flag tells the shim "this is streamed data", and the shim routes it into the
**vertex ring** — correct for geometry rewritten every frame, wrong for storage
that has to survive: the ring wraps, and a mesh uploaded once later reads back
whatever wrote over it. A plain lock keeps the buffer's own GL storage, which is
the entire point of caching it.

Two corrections:

* the fill uses a plain lock, so the buffers are real storage;
* a mesh found dirty on three draws in a row gives up its buffers and goes back
  to the streaming path for good. An effect that scrolls its own UVs
  (`DxSimMesh::SetMoveTex` rewrites every vertex every frame) would otherwise
  pay a blocking re-upload of its own buffer per frame — worse than streaming
  it, and byte-for-byte the behaviour it had before the cache existed.

So the cache now applies to what it was aimed at — the vehicle, props, items —
and animated effect meshes are untouched. Verified on the tablet: **mounted 120
fps, `veh:parts` 0.2 ms, 5 us a draw**, and the weapon's own effect back on the
path it had before.

---

## 2026-09-11 (9) — The vehicle: 83 fps to 120, and the batcher was making it worse

"when I use the vehicle it -20 fps or even lower". Measured on the Tab S9 with
the BMW S1000RR summoned: **83 fps mounted against 120 on foot**, 287 draws at
20 us each, `world` 7.3 ms, `veh:parts` 3.5 ms.

The profile named both halves.

**1. The UI batcher was batching the vehicle.** `RanDevice::batchUIDraw` merges
draws that share state, which is right for HUD quads and text and exactly wrong
for a large mesh: `appendTriangles` expands every index into a flat vertex list
and copies it, so a 25,000-triangle vehicle paid 75,000 vertex copies a frame to
save one draw call it never needed. 12.5% of the process sat in the vector
insert behind that expansion, with memmove behind it.

A draw above **256 triangles** now goes straight through - flushing whatever is
pending first, so nothing is reordered. **83 → 109.5 fps.**

**2. `RanMesh::DrawSubset` streamed the whole mesh on every call.**
`DrawIndexedPrimitiveUP` hands the driver the entire vertex array each time and
the driver copies it - 20% of the process, all of it re-sending geometry that
had not changed. `RanMesh` now keeps its own vertex and index buffers, filled on
first use and refilled only when something writes to the mesh
(`UnlockVertexBuffer`, `UnlockIndexBuffer`, the attribute sort), and draws from
them with `DrawIndexedPrimitive`. **109.5 → 120 fps, and 20 us a draw → 6 us.**

| mounted, Tab S9 | before | after |
|---|---|---|
| fps | 83 | **120–121** (vsync ceiling, same as on foot) |
| per draw | 20 us | 6 us |
| `world` | 7.3 ms | 1.9 ms |
| `veh:parts` | 3.5 ms | folded into the rest; no longer in the top ten |

Both changes help everything else that draws a mesh through D3DX - items,
props, effect meshes - not just the vehicle.

---

## 2026-09-11 (8) — Three more, all the same shape: work repeated on unchanged data

"can you check is other place can be optimize?" — read off the same profile,
which after the /sdcard fix named its next offenders plainly.

**1. `RanMesh::DrawSubset` was O(faces), per draw, per frame** (9.9% of the
process). It walked the whole attribute array to find the run of faces carrying
one attribute — a run the mesh already keeps in `m_attribTable`, built once and
invalidated by the only two things that can change it (`UnlockAttributeBuffer`
and the attribute sort). It now reads the table. Helps every mesh the shim
draws: effects, items, props.

**2. `TtfFace::GlyphIndex` re-parsed the cmap for every character** (2.0%): a
scan of the encoding records, then a linear walk of the format-4 segments, for
every character of every label every frame. Now a 128 KB table per face, filled
on first sight.

**3. `RanD3DXFont::glyphFor` was a `std::map` lookup per glyph** (2.8%), called
twice per character drawn (measure, then place). Glyph ids are dense, so a
vector of pointers into the map indexes it; `std::map` never moves a node, so
the pointers stay good.

**4. Shaping ran on the same strings every frame** (`applySubstLookup` 1.6% +
`coverageIndex` 1.4%). The client re-measures and re-draws its labels from
scratch, and the answer depends only on the characters, so `shapeRun` caches by
the string (cleared wholesale past 512 entries).

### Where the frame is now, on the Tab S9

| profile, self time | before today | now |
|---|---|---|
| `__faccessat` (diagnostic stats) | **14.8%** | gone |
| text: `DrawText` path | **19%** | ~1.4% (`drawRun`) |
| `RanMesh::DrawSubset` | 9.9% (1.2% self) | out of the top list |
| `__memmove_aarch64_simd` | 13.9% | 14.7% — Adreno's own, vertex streaming |
| `__ioctl` | 4.4% | 8.6% — submission, i.e. the GPU is the work now |

In the world: **120–121 fps** (the panel's ceiling), 8.3–8.5 ms a frame of which
3–5 ms is waiting for the swap. `interface` 1.6 ms, `world` 2.1 ms, 169 draws.
The client is no longer the bottleneck on this device; what is left in the
profile is the driver and waiting.

Text verified by eye at each step — Thai shaping, marks, the chat box and the
outer GUI are unchanged.

---

## 2026-09-11 (7) — The effects were never the problem: a stat() on /sdcard was

"the weapon that I equip it also cost so much fps ... can you check all like if
have some kind of this? optimize it all"

Bisecting by hand-placed timers went in circles, so it went to a sampling
profiler instead — simpleperf, `--app com.ran.native` (the APK needs
`DEBUGGABLE=1`; a plain `-p <pid>` record is refused with "Permission denied").
13,242 samples over 15 s named it in one line:

    15.38%   RanPlat_DiagExists          <- under DxEffectMesh::Render (15.16%)
    14.80%   __faccessat  (1960 self)

`DxEffectMesh::Render` asks "does the *effmesh* diagnostic file exist?" once per
effect mesh per frame. The diagnostic root is on /sdcard, which is FUSE on this
Android: **one access() is about 120 us.** Ten effect meshes a frame is 1.2 ms,
and every other per-mesh and per-character diagnostic added more.

A cache had been added earlier that day and did not help, for a reason worth
remembering: **its table held 16 names and the client uses 26.** The table
filled, and every name after it fell silently through to the live `access()` —
including `effmesh`. The table is now 64, the refresh is 1 s, and a name that
still does not fit logs a warning once instead of being quietly slow.

**The second find, from the same profile:** `CD3DFontX::DrawText` was 19% of the
process. `RanD3DXFont::drawRun` issued **one `DrawPrimitiveUP` per glyph** — a
triangle fan of four vertices, a stream write and a draw call for every
character on screen, and the chat box alone is hundreds a frame. Every glyph in
a run shares the atlas and the state around it, so the run now builds one
triangle list (six vertices a glyph, buffer kept between calls) and issues a
single draw.

### Measured on the Tab S9, same character, same spot

| scene | before | after |
|---|---|---|
| weapon equipped, no buff | 94–100 fps, `DxEffCharSingle` **3.3 ms/f** | **120 fps**, 0.13–0.20 ms/f |
| บัตรบัพ used | 66–85 fps, `eff-group` **2.9–3.2 ms** | **119–120 fps**, `eff-group` **0.3 ms** |
| `world` section | 5.3–6.5 ms | 2.0–2.5 ms |
| `w:chars` / `ch:mesh` | 4.4–5.5 ms | 1.1–1.6 ms |
| draws per frame | 211–229 | 171 |

120 fps is the panel's vsync ceiling, so the real headroom is larger than the
number shows: frame time is 7.7–8.6 ms of which 3–5 ms is waiting for the swap.

**What the profile says is left** (10,991 samples, after both fixes): 15.7%
`__memmove_aarch64_simd` whose callers are all Adreno driver frames — vertex
streaming, the floor for this workload; 4.4% `__ioctl` (submission); 2.8%
`RanD3DXFont::glyphFor` + 2.0% `TtfFace::GlyphIndex`, which is per-glyph map
lookups that could be cached per run if text ever matters again.

### The instruments that found it, kept

* `effprof` under the diagnostic root arms two profilers: **RanEffProf** (per
  character-effect type: ms, calls, draws, GL calls, render-target switches, at
  `DxCharPart`) and **RanEffNode** (exclusive ms per effect *node* type inside
  the single-effect tree, microsecond clock). Both silent without the file.
* `RAN_EFFNODE()` sits at the top of every `DxEffSingle` subclass's `Render`;
  `RAN_EFFSPAN(tag,"name")` measures a region inside one.
* And the lesson: **a diagnostic that stats a file is not free.** On FUSE it is
  120 us. Anything called per draw has to answer from memory.

---

## 2026-09-11 (6) — What a buff card costs, and a budget for other people's

Reported: using บัตรบัพ drops the frame rate, and "we active only our but if other do
it active this effect it will be so bad."

**Measured on the Tab S9**, logged in as the GM character, standing still:

| | fps | eff-group |
|---|---|---|
| before the card | 100–114 | 0 groups, 0.00 ms |
| after the card | 66–85 | **4 groups, 2.9 ms, 29 draws** |

The card grants five buffs and four of them leave a persistent
`DxEffSingleGroup` on the character. **0.7 ms per group, every frame.** At 29
draws for 2.9 ms that is 100 µs a draw against a frame-wide average of 13 µs —
these draws are eight times the cost of an ordinary one.

Where it goes, from the same scene with `nulldraw` (every GL call a draw makes
dropped, the engine-side work kept): eff-group fell 2.9 → 1.1 ms. So roughly
0.4 ms per group is the engine walking the effect and 0.3 ms is the GL it emits.
Neither half gets cheaper in a crowd, and the cost is linear in groups on
screen: thirty buffed players in view would be 120 groups, ~90 ms a frame.

### The budget

`DxEffGroupPlayer::Render` is now, on mobile, ordered and capped:

* the local player's own groups always draw — they are what you are looking at,
  and they are never counted against the budget;
* everyone else's are sorted by distance from the player and drawn nearest
  first, up to **6 groups a frame, at most 2 per character**, so one person
  standing next to you cannot spend the whole budget;
* the rest are skipped for that frame. Nothing is destroyed and nothing
  desyncs — this is a render-time decision only.

`DxEffGroupPlayer::SetMobileViewer(STARGETID, pos)` is fed once a frame from
`DxGameStage::FrameMove`, because Lib_Engine sits below Lib_Client and cannot
ask who the player is.

Both numbers are tunable at runtime with `effbudget` under the diagnostic root
(`"6 2"`). A third field counts the player's own effects against the budget too
— off in the game, and the only way to exercise the path on a machine with
nobody else standing next to you. That is how it was verified:

| effbudget | groups drawn | skipped | eff-group |
|---|---|---|---|
| default | 4 (all mine) | 0 | 2.3 ms |
| `2 1 1` | 1 | 3 | **0.8 ms**, 29 → 6 draws |

`RanEffGroup` logs a line a second while `effbudget` exists, and is silent
otherwise.

**A sticky diagnostic cost an hour of confusion.** The file was read only when
present, so deleting it left the last values in force - and the last values were
a test's `2 1 1`, which counts the player's own effects. The buff aura simply
stopped appearing. The read now resets to the defaults before looking for the
file, so no file means default behaviour. Any diagnostic that changes behaviour
has to answer for its own absence.

### Still open

The 0.7 ms per group is itself the anomaly — seven draws should not cost that.
Worth chasing next: which render states these draws churn, and whether the
effect tree re-walks work per frame that could be cached. Fixing that would
help the player's own effects too, which the budget deliberately does not touch.

---

## 2026-09-11 (5) — The lift measured itself, so it flickered

v417 shipped the window lift with a feedback loop in it, reported at once: "now
it more buggy? it flicking?"

`RanUI_FocusedEditRect` reported the box's live rectangle — from inside the
window the lift had just moved. So:

    frame 1   box is 160 under the keyboard   lift 160
    frame 2   box is clear                    lift 0, window dropped back
    frame 3   box is 160 under again          lift 160 ...

— the window bouncing between two positions at frame rate. The earlier
whole-layer pan did not have this because panning in the shader never touched
the control rectangles; moving the window really does.

The measurement now adds the applied lift back, so it answers where the box
*would* be with nothing lifted. That number is the same every frame, the 0.5 px
hysteresis absorbs the keyboard-height wobble, and after the first frame nothing
moves. The lift's state (`s_pLifted`, `s_fLiftOrigTop`, `s_fLiftApplied`) moved
to file scope in `UIEditBox.cpp` so both functions share it.

**Verified on the Tab S9:** four captures across two seconds with the split
window up — dialog title and buttons on the same rows in every one, HUD
untouched, 60–83 fps.

---

## 2026-09-11 (4) — Only the window being typed into moves

The keyboard fix earlier today slid the whole pre-transformed layer up, which is
what Android's own `adjustPan` does. It worked and it looked wrong: the health
bars, the minimap and the icon bar all rode up for a keyboard that had nothing
to do with them. Reported straight away — "it seem it move every UI even the
HUD up?"

Now `RanUI_LiftFocusedWindow` moves one window. From the focused box, walk
`GetParent()` to the top — only `CreateSub` ever sets a parent, so a top-level
window has none — and `CUIGroup::SetGlobalPos` carries every child with it. That
is the same call the chat already uses to sit above the keyboard, so it is known
to work at runtime; the earlier "a window's drawn position does not follow
SetGlobalPos" note (2026-09-11, keyboard section) was wrong about the mechanism,
and the reverted attempt must have been moving the wrong object.

The original top is remembered, not re-measured: reading the current position
and subtracting would compound once per frame and throw the window off the top,
which is exactly what the first attempt at this did back on 2026-09-10.

`shim/platform/ui_pan.cpp` still works out *how far* — `bottom + 2% − (height −
keyboard)`, clamped to `[0, top]` so the field never leaves the top edge — and
now calls the lift instead of feeding a shader. The shader uniform and the
matching touch-coordinate correction are gone: when the window really moves, its
controls' rectangles move with it, so the hit test follows for free.

**Verified on the Tab S9** with the real soft keyboard: แยก on a stack → the split
window rose clear of the keyboard while the HUD, the quick slots, the minimap
and the open inventory all stayed exactly where they were; dismissing the
keyboard put the window back on its centred spot; Cancel then closed it. No
crash, 103–113 fps throughout.

---

## 2026-09-11 (3) — Two ways to lose the client, neither of them a crash in the usual sense

### Tapping your own character took the client down

Measured on LDPlayer, one frame apart:

    RanTarget: tap latch -> 248 (was 4294967295, live=0, click L0 R1)
    signal 11 (SIGSEGV), fault addr 0x0
    #00 CInnerInterface::SetTargetInfoPlayer   InnerInterfaceSimple.cpp:1083
    #01 GLCharacter::MobileTargetTick          GLCharacter.cpp:1773

Line 1083 is `pCHAR->GetClass()`, and `pCHAR` is NULL. The two lookups disagree
about one actor and one only — yourself:

* `GLGaeaClient::GetCopyActor` answers `GetCharacter()` for our own id, so the
  target reads as live for ever;
* `GLGaeaClient::GetChar` answers NULL for it, because the local player is not
  in the map's character list.

`SetTargetInfoPlayer` uses `pCHAR` immediately and only checks it forty lines
later, where it already says `if ( !pCHAR ) return`. The mobile target is fed to
the panel every frame rather than re-picked from a cursor, so it goes down the
moment it is latched.

Fixed at both ends: the tap latch in `GLCharacter.cpp` now skips our own id —
the same rule `MobileFindNearestPvP` already keeps ("Never ourselves") — and
`SetTargetInfoPlayer` gets a `#ifdef RAN_MOBILE` null check at the top. A finger
hits this where a mouse does not: the tap that targets is also the tap that
walks, and the character stands in the middle of the screen.

### "The app crashed" on the tablet was an ANR

Nothing had crashed. From the tablet's own log, in order:

    18:42:18  ANR in com.ran.native ... Waited 10000ms for KeyEvent
    18:42:39  Killing 27974:com.ran.native (adj 0): user request after error
    18:42:40  Process 27974 exited due to signal 9 (Killed)

— that last line is the Close button on Android's "isn't responding" dialog.
The game thread was healthy at 120 fps right up to the kill and the Java main
thread was idle in its looper. The dump says where it actually was:

    DxGlobalStage::ChangeStage -> NLOADINGTHREAD::EndThread -> Sleep

A stage change blocks the loop thread for the whole load, and on a
NativeActivity that thread is what drains the input queue. Android gives a
window five seconds to consume a touch and ten for a key; a zone load is longer,
so anyone who taps during one gets the dialog.

`RanPlat_PumpEvents` now keeps the queue moving: the shim's `Sleep` calls it,
and it does something only on the loop thread. Input sources only — an app
command can destroy the surface, and doing that halfway through a stage change
is a different bug, so those stay queued for the real loop. Events read this way
are finished and discarded rather than dispatched; the client is between two
worlds and is in no state to handle a tap.

**Verified on the Tab S9** (SM-X710, the device the report came from): entered
the world while sending 25 key events across the load — no ANR, process alive,
116 fps after. Tapped and long-pressed the character's own body: no latch, no
crash. The keyboard pan from earlier today also confirmed on real hardware —
Enter opened the chat, the layer rose, Back closed it and everything went back.

---

## 2026-09-11 (2) — The whole 2D layer pans out of the keyboard's way

On the tablet the soft keyboard covers the split window's number box. The
earlier measurement said it would not, and it was right about a *phone*: a 400‰
keyboard clears a centred window. A tablet keyboard is a far larger share of the
screen, and at 550‰ the field is buried — along with, in principle, every other
edit box that happens to sit low.

**Why the window mover failed, and what replaced it.** Moving the window itself
was tried on 2026-09-10 and reverted: a client window's drawn position does not
follow `SetGlobalPos` at runtime the way the chat's does. Rather than keep
pulling that thread, this does what Android's own `adjustPan` does — slide the
entire pre-transformed layer up, by exactly as much as it takes to show the
focused box and no more. The window never resizes (`adjustNothing`, deliberate),
so something has to move, and moving the whole layer moves the window, its text,
its caret and its cursor together, with no client-side layout code at all.

Three pieces, and the only new client code is a getter:

| file | what it does |
|---|---|
| `SOURCE/Lib_ClientUI/Interface/UIEditBox.cpp` | `RanUI_FocusedEditRect` — the top and bottom of `s_pMobileEditing`, the box that raised the keyboard |
| `MOBILE/native/shim/platform/ui_pan.cpp` | computes the pan once a frame: `bottom + 2% - (height - keyboard)`, clamped to `[0, top]` so the field never leaves the top edge |
| `shim/gl/gl_render.cpp` | `uUIPanY`, subtracted from `aPos.y` in the pre-transformed branch of the vertex shader |
| `shim/platform/touch_gesture.cpp` | adds the same pan back to every touch before it enters the client |

The draw and the hit test read one number, so what you press is what you see.
`g_rtActive` gates it: an off-screen pass has its own coordinate space, and
panning there would move a reflection instead of the UI. The touch overlay
(stick, skill ring) has its own GL path and its own hit test, so it stays under
your thumbs — which is right, it is not what you are typing into.

**Measured on LDPlayer with `fakekb` 550** (the emulator will not raise a real
keyboard, so the inset is forced):

| step | result |
|---|---|
| outer register window, bottom field focused | layer up 218 px, field and its dialog clear of the keyboard |
| tapped Cancel *where it was drawn* | dialog closed — hit test agrees with the draw |
| login dialog, Pass focused | up 38 px, only what that box needed |
| in-world แยก split modal, number box focused | modal fully above the keyboard line, chat and HUD moved with it |
| tapped the modal's Cancel at its drawn place | closed, pan returned to 0, UI back where it was |

Not re-tested: typing in the chat. The chat still lifts itself in
`MobileArrangeInterface` and that keeps its own box above the keyboard, so the
pan computes 0 for it and the two do not fight — but LDPlayer would not open the
chat's edit box (Enter is a scan code a soft keyboard cannot send), so that is
reasoned, not measured.

---

## 2026-09-11 — The chat follows the keyboard only when the keyboard is its own
### Fields under the keyboard — checked, and mostly a non-issue

The obvious follow-up: if the chat no longer moves for other fields, does anything
else end up buried? A `fakekb` diagnostic was added to answer it, because
**LDPlayer never raises a soft keyboard** and the inset reads 0 there — put a
per-mille number in `fakekb` under the diagnostic root and it is used instead.

| forced keyboard | split window's number box |
|---|---|
| 400‰ (about a real phone keyboard) | **not covered** — no lift needed |
| 650‰ | covered |

So with a realistic keyboard the client's own windows, which are centred, stay
clear. The chat was the one bottom-anchored thing, and it is handled.

A general "lift whichever window is being typed into" pass was written and then
**reverted**. It does not work through this route, and the measurements say why:

* Moving the window and its whole subtree moves the children **twice** — the
  window went up 162 and its edit box went up 325. Sorting and de-duplicating
  the collected tree did not change that, so it is not a duplicate in the list.
* Moving only the root gives consistent numbers (window 310 → 148, edit box
  388 → 226, both −162, confirmed in the `kblift` log) but **nothing moves on
  screen** — the drawn window stays where it was.

So a client window's drawn position does not follow `SetGlobalPos` on the window
at runtime the way the chat's does, and the difference is not yet understood.
Shipping a half-working window mover is worse than not having one. If a field
ever does end up under a real keyboard, this is the thread to pull, and `fakekb`
plus the `kblift` log are the tools.



Splitting a stack opens the client's number window, that window raises the soft
keyboard, and the **chat box jumped up** out of the way of a keyboard that had
nothing to do with it.

`DxGameStage::MobileArrangeInterface` places the chat above the soft keyboard
every frame — the window deliberately does not resize (`adjustNothing`, because
letting Android resize it churns the surface mid-frame), so without this the chat
is buried while you type. But `RanPlat_ImeInsetPerMille()` answers for *whichever*
edit box raised the keyboard, and every other edit box in the game — a split
count, a name, a search — already has its own window mid-screen that the keyboard
does not cover.

The lift is now gated on `CBasicChat::IsCHAT_BEGIN()`, i.e.
`m_pEditBox->IsBegin()` on the chat's own edit box. That is true exactly while
the chat is the thing being typed into: `BeginEdit()` focuses that box, and focus
is what calls `RanIME_Show`.

**Not reproduced on LDPlayer.** It will not show the soft keyboard for the game
window at all — tried `settings put secure show_ime_with_hard_keyboard 1` and
selecting the pinyin IME explicitly — so the inset reads 0 there and the lift
never engages either way. The condition comes from reading the path that produces
the behaviour end to end, not from a captured symptom. Worth one look on the
tablet.

---

## 2026-09-10 (3) — Splitting a stack uses the client's own window again

The touch item sheet opened a bespoke "how many?" sheet — `CMobileCountSheet`,
steppers and presets — instead of the split window the client has always had. It
was written to dodge the on-screen keyboard, and it was a second way to do
something the client already does.

`ACT_SPLIT` and `ACT_STORAGE_SPLIT` now do exactly what the PC does:

```
pInvenWnd->SetSplitPos ( x, y );
DoModal ( ID2GAMEINTEXT("SPLIT_ITEM"), MODAL_QUESTION, EDITBOX_NUMBER, MODAL_SPLIT_ITEM );
```

`MODAL_SPLIT_ITEM` / `MODAL_STORAGE_SPLIT_ITEM` already read the slot — and the
channel — back off those windows and send `ReqInvenSplit` / `ReqStorageSplit`.
The storage window needed a getter to set the slot on; it sits next to the
inventory one and is `RAN_MOBILE`-guarded.

`CMobileCountSheet` is deleted along with `MobileAskCount`, the member, the
accessor, the creation block, its GUID and its entry in
`cmake/sources_Lib_ClientUI.cmake`. It had no other caller. The `MOBILE_COUNT`
gameword is now unused in `Gui.rcc`; harmless, no repack needed.

**Verified on LDPlayer:** inventory → stack of 599 → แยก → the client's own
*โปรดระบุจำนวนสิ่งของที่ต้องการแยกออกมา* window with its number box and
ตกลง/ยกเลิก → entered 100 → stack became **499** with a new stack of **100** in
the next free slot. `out/split_modal.png`, `out/split_done.png`.

One thing to watch: the keyboard question the sheet existed to dodge is back. The
modal sits mid-screen so a bottom-docked keyboard should clear it, but that has
only been checked on the emulator, where text is injected rather than typed.

---

## 2026-09-10 (2) — Effect meshes were being drawn in their frame's space, not the file's

**This is the "effect on the floor is wrong".** It is the map gate marker, and the
bug is in the mesh loader, so it is not confined to gates.

`D3DXLoadMeshFromX` **flattens** an `.x` file: every mesh comes back with the
transform of each frame above it already applied. That is how an artist places
the pieces of an effect — park a mesh under a moved frame. The shim returned the
mesh in its own frame's space and dropped those matrices, so each piece landed at
the file origin.

Read straight out of the shipped binary `.x` files:

| mesh | its frame carries | effect of dropping it |
|---|---|---|
| `gate_01.x` | z **+13.33** | the arrow sits away from the marker |
| `gate_line.x` | x **−14.63** | the outline sits ~15 units off the arrow |
| `gate_plane.x` | y **+0.28** | the plane sinks into the floor it should hover over |

Fixed in `shim/d3d/d3dx_mesh.cpp` (`flattenTransform`): walk the mesh's
ancestors, multiply their `FrameTransformMatrix` child-first, apply the full
matrix to positions and the rotation to normals. Returns false when every
ancestor is identity, so the common case costs nothing. **The hierarchy loader
deliberately does not use it** — there each frame keeps its own
`TransformationMatrix` and the engine applies it; baking it in as well would
transform the mesh twice.

Verified on LDPlayer from the vertex data, same gate, same map, before and after:

    gate_line   v0 x   5.0 -> -9.6    (-14.6; the file says -14.63)
    gate_plane  v0 y   0.0 ->  0.1    (lifted off the floor)
    gate_01     v0 z  -8.4 -> -12.1   (its frame carries rotation as well)

### How it was found, and what it cost

Most of the session went on identifying *what* the shape on the floor was. What
finally worked, and is now permanent tooling:

* `drawlimit` bisects to the draw — it now also logs a **symbolised backtrace**,
  which named `DXLANDEFF -> DxEffSingleGroup -> DxEffectMesh -> DxSimMesh` in one
  step instead of an afternoon of grepping.
* `meshtex` (SOURCE) prints, per mesh subset, the texture its material names and
  whether it loaded. That proved the gate meshes carry **no texture at all** in
  the shipped `.x`, so the flat colour is content, not a load failure.
* `effmesh` (SOURCE) prints each effect piece's local, group and final matrix.
  All pieces shared one group position with a zero local matrix, which cleared
  the effect system and pointed at the mesh loader.
* `nocull` / `cullflip` toggle world culling at runtime.

### Still open

* **`D3DXLoadMeshFromX` does not merge multiple meshes.** Real D3DX merges every
  mesh in the file into one; `findMesh` returns the first. Every `.x` checked
  here holds a single mesh, so nothing shipped depends on it — but it is a real
  gap.
* **The shim collapses `D3DCULL_CW` and `D3DCULL_CCW`** onto GL front face `CW`,
  deliberately and measured at the time against the world and character-select
  grounds. It is still a distinction the PC honours and this build does not.
  `nocull`/`cullflip` now make it testable without a rebuild.
* **No PC reference was obtained.** `Ran/MiniA.exe` launches windowed with the
  `iyaa...rundiwa` token, but the connection is refused from here ("the internet
  was disconnected"), so it never reached a map. Two `MiniA.exe` processes are
  left running and cannot be closed programmatically (Hackshield refuses
  `WM_CLOSE`, `CloseMainWindow` and `Stop-Process`).

---

## 2026-09-10 — Records written by a 32-bit client are now read at 32-bit widths

**Entering clubwar_inzone crashed the client. It no longer does, and the map renders.**

The crash was a null dereference in `DxLandMan::EffectLoadToList`, but that was the
symptom, three sections downstream of the cause.

`DXOCMATERIAL` is written into the `.wld` as a raw blob with a live texture
**pointer** inside it:

| | bytes |
|---|---|
| `D3DMATERIAL9` | 68 |
| `LPDIRECT3DTEXTURE9` — an address from a process that exited in 2005 | 4 |
| `char szTexture[MAX_PATH]` | 260 |
| **record** | **332** |

The file says 332 too: in `clubwar_inzone.wld` the first material name is at byte
691 and the second at 1023. A 64-bit build makes the same struct 344, so
`sizeof()` read 12 bytes too many per material, the names landed mid-record, the
octree mesh that follows read a garbage vertex count and swallowed the rest of the
file, and the effect list after it was read past the end — where a type of `0`
comes back and `CreateEffInstance` answers `NULL`. That matched the log the
previous commit's guard produced exactly: *unknown effect type 0 at offset 887627*,
132 bytes short of the file's 887,759.

**The whole family was then measured rather than eyeballed.**
`MOBILE/native/layout/blobprobe.cpp` + `blobrun.sh` take `sizeof()` of every type
read with `ReadBuffer(..., sizeof(T))` — 148 of them, from the real headers — on
both the ABI the data was written with (i686) and the one we run under (aarch64).
Thirteen differ. Fixed:

| type | file / this build | what it breaks |
|---|---|---|
| `DXOCMATERIAL` | 332 / 344 | map meshes — **the crash** |
| `DXMATERIAL_SPECULAR` | 528 / 536 | character specular |
| `DXMATERIAL_SPEC2` | 528 / 536 | |
| `DXMATERIAL_SPECREFLECT` | 528 / 536 | |
| `DXMATERIAL_NEON` | 552 / 560 | two pointers, not one |
| `DXUSERMATERIAL` | 536 / 544 | glow |
| `EFFCHAR_PROPERTY_LINE2BONEEFF_0101` | 232 / 248 | |
| `EFFCHAR_PROPERTY_LINE2BONEEFF_0102` | 232 / 240 | |

The last pair carries no pointer at all. It embeds `CMinMax<float>`, which has a
**virtual destructor**, so the vtable slot is the thing that grows. Same failure,
different cause — which is why the sweep compared measured sizes instead of
looking for pointer members. An earlier audit that looked only for pointers found
nothing here.

Each read is now field-by-field at the width the file holds; pointer and vtable
slots are read and discarded (`RanReadWin32Pointer` / `RanReadWin32Pad` in
`basestream.h`), and the pointer is set to `NULL`, which is what the caller does
with it anyway. MSVC keeps the original blob read in the `#else` branch.
`DXMATERIAL_CHAR_EFF` and `_100` already had readers from an earlier pass.

**Verified on LDPlayer** (x86_64 build, one login): walked into the clubwar TD
gate, `clubwar_inzone` loads and renders — real geometry, `clubwar_wall*` and
`clubwar_floor*` textures, the club-war score panel, 54 fps, process alive, and no
`unknown effect` line anywhere in the log. `out/inzone.png`.

Also measured and **clean**, so ruled out: all 22 nested effect `PROPERTY` structs
in the single-effect system (`propprobe.cpp`), including `DxLandGate::PROPERTY`
(732 bytes on both). Effect placement does not come from a width bug.

### Still open from this session

* **The misplaced map effect (TP gate) is not explained.** Ruled out with
  measurements: every effect `PROPERTY` struct, `DXAFFINEPARTS`, and
  `EFF_PROPERTY::GetSizeBase` are all width-stable, and the client never calls
  `DxLandGateMan::Render` (that is the editor's AABB debug draw), so what is on the
  floor is real map content drawn in the wrong place. Next step is a side-by-side
  against the shipped PC client at the same gate — deliberately not done in this
  session because it would be a second login to the live server.
* **`SITEMCUSTOM` (76 / 80) and `SINVENITEM_SAVE` (80 / 88) still differ.** Both are
  packet-side, read out of a `ByteStream` inside a message body, and belong with the
  wire-parity work rather than the file loaders. `SITEMCUSTOM` carries other
  players' equipment customisation, so this is a candidate for any remaining
  "skin loads wrong" report.
* **`TILE_TEX` blob-reads a `std::string`** (12 / 24), which is heap corruption on
  any ABI. Only reached from `DxBlend::LoadFile_Edit` — the map editor's path, which
  the client never takes. Left alone, recorded here.

---

## Where we are

| # | Phase | Delivers | State |
|---|---|---|---|
| 1 | **Compile** | Whole client builds + links for arm64; packet layout matches the server | ✅ **done 2026-08-24** |
| 2 | **Boot headless** | `android_main` starts the client, reads the RCC packs, runs the frame loop. No rendering. | ✅ **done 2026-08-24** |
| 3 | **Render** | D3D9→GLES3 device shim; login screen, then world and characters; asm shaders → GLSL; fonts | 🟨 **in progress** — login scene, UI, text, lighting, fog, render targets and skinned characters all draw; effect passes remain |
| 4 | **Input + sound** | Touch → mouse/key messages, soft keyboard, virtual joystick overlay, OpenSL/AAudio | 🟨 **started** — touch reaches the UI; soft keyboard, joystick and audio remain |
| 5 | **Polish + iOS** | State-change batching, ETC2/ASTC textures, then iOS off the same code | ⬜ |

Phase 3 is the bulk of the remaining work. **The first pixels arrived on 2026-08-24**, and by
the end of that session the server-select page renders with **correct Thai and Latin text** —
the real engine, the real GUI XML, the real game-text tables, drawn through the GLES backend.
The 3D login scene now draws too — tower, trees, buildings, foliage — through the same GLES
backend. What is still missing inside phase 3 is the terrain ground surface, lighting and fog,
the character models (`.X` mesh + animation loaders), and the effect/shader passes.

---

## Phase 1 — what was actually achieved (2026-08-24)

1. **Compiles.** Lib_Engine 415 TUs · Lib_Client 239 · Lib_ClientUI 472 · Lib_Network 26
   (client subset) · Lib_Helper 4, plus zlib / lua 5.0.3 / minilzo / ogg / vorbis / shim.
   0 errors, 0 failed TUs.
2. **Links.** `libran.so`, 163 MB with `--whole-archive`. 476 undefined symbols, of which
   303 are ordinary libc/libc++ resolved at load; the other **173 are the phase 3/4
   work-list** → `native/PHASE1-LINK-REPORT.md`.
3. **Packet layout verified against the real server ABI** — 1,288 message structs compared
   against ground truth from the actual MSVC x86 toolchain, **1,286 identical**, and the
   comparison caught a genuine bug (see below) → `native/layout/LAYOUT-REPORT.md`.

### Three findings worth remembering

- **The GLES backend has exactly one seam.** `Direct3DCreate9` is never called; the engine
  takes its device solely from `DXUTGetD3DDevice()`. The shim owns that
  (`native/shim/d3d/dxut_compat.cpp`), so phase 3 plugs in at `DXUTSetD3D(pD3D, pDevice)`.
- **MSXML and DXUT are dead code.** The UI XML goes through the hand-written
  `CRanXMLParser`, and DXUT was used only for two device accessors — both were removed
  rather than ported.
- **`CTime` alignment was a real protocol bug.** MFC lays `CTime` out with 4-byte alignment
  on x86; the natural arm64 layout is 8. `SONEMAPWEATHER` embeds one, so
  `SNETPC_MAPWEATHER` came out **128 bytes too large** — silent desync, no crash. Fixed
  with `#pragma pack(push,4)`. **Any shim type that can appear inside a packet struct must
  reproduce the MSVC x86 layout**, and the layout gate must be re-run after such a change.

---

## How to work on this

```bash
cd MOBILE/native
./build.sh                 # build everything for arm64-v8a
./iterate.sh Lib_Engine 20 # build one target, print a histogram of distinct errors
./layout/run.sh            # arm64 record-layout dump
../tools/layout-probe/msvcsizes.cmd   # MSVC x86 ground truth (needs VS 2022, installed)
```

Toolchain facts: clang needs `-fms-extensions -fms-compatibility
-fms-compatibility-version=19.30 -fdelayed-template-parsing`; the code is C++14 (uses
`auto_ptr`, `bind2nd`); each library force-includes its own `stdafx.h` to emulate MSVC `/Yu`.
NDK r27c / SDK / CMake all ship with Unity 6000.5.8f1 — no separate install.

Edits to `SOURCE/` are either `#ifdef RAN_MOBILE`-guarded or const-correctness fixes that
also compile under MSVC, so the PC build stays intact.

---

## Documentation map

| Where | What | Status |
|---|---|---|
| `MOBILE/STATUS.md` | **this file** — current state, updated every session | live |
| `MOBILE/NATIVE-PORT-PLAN.md` | the port plan: audit numbers, architecture, phases, status log | live |
| `MOBILE/PATCHING.md` | how to publish an update: the two jobs, server layout, store maintenance, traps | live |
| `MOBILE/native/PHASE1-LINK-REPORT.md` | the 173-symbol work-list for phases 3–4 | live |
| `MOBILE/native/PHASE2-BOOT.md` | how to install/run the headless boot APK and what its log means | live |
| `MOBILE/native/layout/LAYOUT-REPORT.md` | packet-layout gate: method, findings, how to re-run | live |
| `MOBILE/reference/` | decoded PC file formats and asset pipeline — **still true**, engine-independent | reference |
| `MOBILE/archive-unity/` | superseded Unity-rewrite docs, kept only as history | dead |
| `MOBILE/client/`, `MOBILE/spike/` | the JS protocol spike that preceded the port | **deleted 2026-09-01** |
| `MOBILE/unity/`, `MOBILE/assets/`, `MOBILE/build/` | the Unity project and its extracted assets | **deleted 2026-09-01** |
| `SOURCE/SOURCE_*.md` | maps of the original C++ codebase | reference |

Root-level `*.md` (EP1/EP7 ports, GM_COMMANDS, plan.md …) are the **server/content**
workstream, unrelated to this port. Untouched.

---

## Update rule

At the end of any session that changes something: update **Last updated**, the phase table,
and add an entry to the log below. Then update the status log in `NATIVE-PORT-PLAN.md` if
the change was structural.

## Open work

Kept at the top because it is the list that matters. Ordered by what blocks
what, not by when it was found.

### The loading screen drew dark and full of holes (fixed 2026-09-10)

The zone art rendered as a dim stipple - the scene recognisable, most pixels
black - on both platforms. Traced without a single guess, and the sequence is
worth keeping because each step ruled out a whole class:

1. **Per-draw pixel probe.** `RanGL_ProbePixel` reads the framebuffer back from
   inside the loading thread, which renders outside the frame path every other
   diagnostic hangs off. Probing after each of the six draws showed the art
   draw writing `231E15` and nothing after it changing that: not a later draw
   painting over the art.
2. **Whole-texture dump.** `RanD3D_DumpTexture` attaches an uploaded texture to
   an FBO and writes the entire level out as raw RGBA. `loading_054.dds` came
   back **perfect** - full brightness, 0.1% black texels, no zero alpha. Not the
   DXT1 decode, not the upload.
3. **Real GL state, not the shim's record of it.** `RanGL_ProbeState` asks GL
   itself. Everything was clean - `BLEND=0`, no alpha test, `colorMask=1111`,
   `samples=0`, gamma off, full viewport and scissor, `colorOp=SELECTARG1(TEXTURE)`
   - except **`DEPTH_TEST=1, depthMask=1`**.

Two defects, both real:

* **`D3DCLEAR_TARGET` without `D3DCLEAR_ZBUFFER`.** The loading loop clears only
  colour, and it inherits `D3DRS_ZENABLE` from the stage that was on screen when
  the change began. It was testing 2D art against the depth of a scene that no
  longer exists.
* **The pre-transformed depth mapping was wrong for GL.** D3D clip z is `[0,w]`,
  GL's is `[-w,w]`, and the vertex shader passed a `D3DFVF_XYZRHW` z straight
  through:

  ```glsl
  gl_Position = vec4(x, y, aPos.z, 1.0);          // z = 0 lands at depth 0.5
  gl_Position = vec4(x, y, aPos.z * 2.0 - 1.0, 1.0);   // z = 0 is the near plane
  ```

  A pre-transformed z of 0 means *the near plane* in D3D; unconverted it lands
  at window depth 0.5, in the middle of whatever the last scene left behind.
  This affected every 2D draw in the client, and only bit where a stale depth
  buffer was still being tested against.

Both fixed; the loading screen now draws the full bright art with its bands,
map-name banner, hint icon and spinner, and the world behind it is unchanged
(269 draws/frame, HUD and chat correct). The two fixes went in together, so
which one alone would have been enough is not measured - and is not worth a
login on the live server to find out.

The three probes are kept, armed by `/sdcard/ran/loadprobe`.

### The loading screen before login was a missing boot cover (fixed 2026-09-10)

Reported as "why it show loading screen? we do not show that before login like
android version".

Both platforms call `RanSplash_Begin` around `RanApp_Boot` - the call sites are
line for line the same - so the splash is not iOS-only. What differs is what it
draws:

```
Android   RanSplash: boot cover 1280x720 from the launcher
          RanSplash: boot screen up (art 0x0)

iOS       RanOpen: .../cache/bootcover.bin -> FAILED (errno 2)
          RanSplash: boot screen up (art 1024x512)
```

`RanLauncher.handOverPage` rasterises the launcher's own page into
`cache/bootcover.bin`, and `splash.cpp` draws that in preference to its own
art - so on Android the page runs unbroken from the patcher through the several
seconds of client boot. iOS never wrote the file, so the splash fell back to
`loading_002.dds`, a **zone loading screen**, which is what appears before
login.

That failed open was in the very first log pulled off the phone. It was written
off as "the launcher's own cache, absent on a first run" - wrong: it is absent
on *every* run, because nothing on iOS ever writes it. A file that is missing
every time is not a first-run artefact.

**Fix.** `RanPatchViewController` renders itself into a half-resolution RGBA
bitmap and writes the same `"RANC"` + width + height + pixels file the Java
writes, just before the swap. Orientation checked against the reader rather
than assumed: `splash.cpp` draws the cover with the same UVs as a DDS, and a
`CGBitmapContext` is top-down in memory exactly like Android's
`copyPixelsToBuffer`.

### The patch page's progress bar never appeared (fixed 2026-09-10)

The bottom band's frame was computed once, in `viewDidLayoutSubviews`. The bar
is hidden while the patcher has no percentage to report (`permille < 0`) and
shown again when it has - and hiding an arranged subview changes a
`UIStackView`'s height. The frame was therefore measured with the bar hidden,
and the bar had nowhere to appear when it came back. The band is on constraints
now, and a visibility change asks for a fresh layout.

The bar's colour was a second, separate difference: Android's is `#FFCB00` on a
dark track, about 6dp tall, sampled off a screenshot of the launcher page.
`UIProgressView` defaults to a thin system-blue line on light grey, and is a
fixed ~4.5pt whatever frame it is given, so the thickness comes from a
transform.

### GitHub Actions is out of minutes (blocking, 2026-09-10)

```
The job was not started because recent account payments have failed or your
spending limit needs to be increased.
```

The run failed in 8 seconds without consuming anything, so nothing was wasted -
but no iOS build can be produced until this is resolved. There is no Mac on the
development machine, so Actions is the only compiler.

**The arithmetic.** GitHub Free gives a personal account 2,000 CI/CD minutes a
month for private repositories, and **macOS bills at 10x** - so 200 real macOS
minutes. This job recompiled the whole client every run at about twelve
minutes, which is roughly sixteen builds a month, and there were 42 runs.

**Done about it:** the workflow now uses `ccache` with a rolling
`actions/cache` key, which should take an unchanged-headers build from twelve
minutes to three or four - four times as many builds for the same allowance.
The hit rate is printed each run, so a key that stops matching is visible
rather than silently costing full price.

**Still to decide, and it is not mine to decide:** wait for the monthly reset;
raise the spending limit (macOS is billed per minute); make both repositories
public, which makes Actions free and unlimited but publishes the client source;
or move the iOS job to a service with a free macOS tier. Each is recorded here
so the choice is not re-derived next time.

### The whole touch layer was Android-only (fixed 2026-09-10)

Reported as "the functionality that we implement for all in the android did not
work with the ios". It was one cause, not many.

**How it was found, since guessing at "functionality" is useless.** Two
mechanical diffs:

1. **Symbol tables of the two shipped binaries** (`llvm-nm --defined-only` on
   `libran.so` and the iOS Mach-O, filtered to `Ran*`). **No** function is
   defined on Android and missing on iOS. Nothing is stubbed out, so the
   divergence is not in the API surface.
2. **What each platform layer calls.** `android_main.cpp` is 930 lines against
   the iOS layer's 589, and the calls only Android makes name the gap exactly:

   ```
   gesturePress / gestureTick     the touch-to-mouse state machine
   RanUI_PointInControl           left-click vs camera-turn
   RanUI_MouseInControl
   RanTouch_IsPinching            pinch cancelling a drag
   RanUI_EndEditIfOutside         a tap outside an edit box closes the keyboard
   RanInput_Key
   RanApp_Shutdown / RanGL_Shutdown
   ```

**Cause.** The entire touch-to-mouse layer was a file-static inside
`android_main.cpp`, which iOS does not compile. On iOS a touch pressed the left
button on the way down and released it on the way up, and that was all there
was: no long press for the right button, no 30 px drag threshold, no camera
free-look, no window dragging, no pinch zoom, and the keyboard stayed up over
half the screen because nothing ended the edit.

**Fix.** Moved verbatim into `shim/platform/touch_gesture.cpp` - the rules, the
constants (450 ms hold, 30 px slop) and the comments recording why each was
chosen - and both platforms call `RanGesture_Down/Move/Up/Tick`. Copying it into
the iOS layer would have worked today and diverged again the next time either
side was touched; the shim is compiled by both, so it cannot.

**Verified both ways.** Android on a real run after the move: login taps drive
through the shared path, world entry, HUD, joystick, skill pad, chat and other
players all correct. iOS at the link: `touch_gesture.cpp` is in a *static*
library, so `RanGesture_*` is pulled into the binary only if something
references it - and all five are in the shipped Mach-O. The symbol diff is now
empty in both directions.

**Left alone deliberately**, so they are not mistaken for oversights:
`RanInput_Key` is scan-code key events a soft keyboard cannot produce - Enter is
already mapped through `UIKeyInput` - and `RanApp_Shutdown` / `RanGL_Shutdown`
are never reached on iOS because the system kills the process.

**Worth keeping as a method:** when a platform "does not work", diff the defined
symbols of the two binaries first (is anything missing?), then diff the calls
each platform layer makes (is anything unwired?). Both are mechanical and
neither depends on a hunch.

### iOS ran at 7 fps — glBufferSubData into an in-flight buffer (fixed 2026-09-10)

Once the framebuffer bug was fixed and the draws stopped being thrown away, the
real cost appeared: 6.4-7.3 fps, `render 138-150 ms`, of which `submit 78-82 ms`.

**Both candidate paths ran in the same frame, so they compare directly:**

```
FRAME buffer calls: orphan 0.0/f 0.00ms  map 64.3/f 28.89ms  sub 18.9/f 0.10ms  whole 0.0/f 0.00ms
                                             449 us each          5.3 us each
```

`map` is the streaming ring, which falls back to `glBufferSubData` when there is
no persistent mapping - and iOS reports `buffer_storage: no`. `sub` is
`RanGLR_UpdateBufferRangeUnsync`, an unsynchronised mapped range. Same device,
same driver, same frame, **85x apart**.

**The larger half is charged at draw time,** which the two stages show:

| | draws/frame | us per draw | ring writes/frame |
|---|---|---|---|
| character select | 275 | 35 | 13 |
| in the world | 294 | 277 | 64 |

Same call counts either side; the draws get eight times more expensive exactly
where there are five times more buffer writes. The buffer is in flight - earlier
draws in the same frame read it - so a plain `glBufferSubData` has to make that
safe, and the wait lands on the next draw that touches the range.

**Fix.** The ring already guarantees safety itself: it writes front to back and
respecifies the whole store when it wraps, so a slice is never written while
anything is reading it. That is exactly the promise `GL_MAP_UNSYNCHRONIZED_BIT`
makes, so the write goes through a mapped range on Apple, with
`glBufferSubData` still the fallback if the map is refused. Apple only - on an
emulated GL a call is expensive and a stall is not, and the measurement there
favours `glBufferSubData`.

**Ruled out on the way, so they are not chased again:** 20 shader variants, all
built before the slow frames, so no runtime recompilation; `glErr` is `0x0000`
throughout, so nothing is being rejected.

### iOS touch went to the overlay in the wrong space (fixed 2026-09-10)

Reported as "the functionality not look like android".

`RanTouch_Init` is handed `panel / RanGL_InputScale` - 1278x589 against a
2556x1179 panel - and the overlay lays itself out and hit-tests in that space.
Android divides once, up front, and gives the result to **both** consumers:

```c
const int scale = RanGL_InputScale();
px = AMotionEvent_getX(event, i) / scale;
if (RanTouch_PointerDown(pid, px, py)) return 1;   // overlay
RanInput_PointerMove(px, py);                       // client
```

iOS applied the division only on the client's side and offered the overlay raw
panel pixels, so every pad, skill button and camera control was hit-tested at
twice its coordinate. `RanUI_PointInControl`, which `RanTouch_PointerDown`
consults before claiming a press, was asked the same doubled question - so
presses that should have fallen through to a client window did not, either.

**Why it read as "renderscale 1" and hid.** The boot line printed `g_bufferDiv`
under the label `renderscale`:

```c
LOGI("panel %dx%d, drawing %dx%d, laid out %dx%d (UI scale %d, renderscale %d)",
     g_panelWidth, g_panelHeight, bufferW, bufferH,
     g_panelWidth / g_renderScale, g_panelHeight / g_renderScale,
     g_renderScale / g_bufferDiv, g_bufferDiv);
```

`g_renderScale` is 2 on this phone; the number printed last is the buffer
divisor, which is 1. The label now says what the number is.

### The iOS patch page — still open, now instrumented (2026-09-10)

Reported twice as not matching Android, and still not settled. What is known:

* The four PNGs **are** in the shipped bundle - checked in the `.ipa`, not
  inferred from a green build.
* The page carries nothing to do once the data is current: the whole log holds
  one `RanPatch` line, `patch base ...`, and the next line is already the GL
  view. The page is on screen for a fraction of a second.
* Screenshots over the USB tunnel take about five seconds, so they cannot catch
  it. Three attempts came back **100% pure black** - not the page's `#0B0E10` -
  which most likely means the phone's screen was off, and proves nothing either
  way.

Rather than guess again, the page now logs whether each image resolved
(`page art ran_loading: 1600x1105`, or `NOT IN THE BUNDLE`) and how long it is
on screen. One launch answers both.

### iOS lost the character, the interface and the touch pad (fixed 2026-09-10)

Reported as "some skin load not correctly". A screenshot off the phone showed
the terrain drawn and **nothing else** - no character, no mobs, no HUD, no touch
pad - while the log said all of it was being submitted: `skinned 51/frame`,
`ui 2581/frame`, `touch-hud: 7.0 draws/frame`, `interface 6.4ms`.

**The measurement that named it.** The per-frame stats carry a `glErr`:

```
draws=85123 (ui=6523 ...) glErr=0x0000     <- last frame before world entry
draws=51446 (ui=16326 ...) glErr=0x0506    <- first frame in the world
into render targets: 3900 draws, largest 512x512
```

`0x0506` is `GL_INVALID_FRAMEBUFFER_OPERATION`, and it appears on exactly the
frame where the engine starts drawing into off-screen targets. Android is
`0x0000` throughout.

**Cause.** On EAGL there is no default framebuffer. `gl_context_ios.mm` builds
the screen as an FBO around the CAEAGLLayer's renderbuffer, so framebuffer
**0 has no attachments and is incomplete**. The renderer finished an off-screen
pass with `glBindFramebuffer(GL_FRAMEBUFFER, 0)` - correct on EGL, where 0 is
the window surface - and from that point on every draw in the frame failed and
rendered nowhere. The world survived because it is drawn *before* the first
render-target pass; the character, the interface and the touch pad are drawn
after it. The present path rebinds the real FBO each frame, which is why the
damage repeated per frame instead of being permanent.

**Fix.** `RanGL_DefaultFramebuffer()` says which object is the screen - 0 on
EGL, `g_fbo` on EAGL - and every unbind goes through it. Three call sites, plus
the blit's "no destination texture means the screen" default.

**Ruled out while looking, so it is not chased again:** the 35 failed `.enm`
opens in the log are files that do not exist in the client data at all - the
engine asks for an optional per-model file and there is none - and
`bootcover.bin` is the launcher's own cache, absent on a first run. Neither is
a defect, and neither is iOS-specific.

### The iOS patch page was text on black (fixed 2026-09-10)

Android draws the real page - `ld_top`, the zone art, `ld_under`, the mark in
the top band, and status/bar/detail in the bottom one - from four PNGs in
`res/drawable-nodpi`. iOS drew a centred label and a bar on black, and the code
said why:

```objc
//  Text only - the Android page's art is packed in Gui.rcc, which is itself
//  part of what the patcher is downloading, so it cannot be drawn before it runs.
```

The first half is true and the second does not follow. Android does not read
that art from `Gui.rcc` either; it carries the same four files in its own
package and draws them before a byte is fetched. The PNGs are now bundled into
`ran.app` the same way and `RanPatchViewController` is laid out band for band
with `RanLauncher.java`: bands at 128/768 of the panel height, the art cropped
to fill the middle two thirds, the mark at 0.82 of the band, the same colours
(`#0B0E10`, `#F0F4F6`, `#AEB8BE`) and the same 15/12 pt text.

Laid out in `viewDidLayoutSubviews`, not with constraints, because the bands
are a fraction of a panel height that is not known until the view is sized.

### The iPhone hang was the same bug (confirmed 2026-09-10)

Its own log, pulled over USB from `Documents/ran/ran.log` (house arrest AFC,
bundle `com.ran.launcher.FALC84Z7MP` - Sideloadly appends the team id), ends
exactly where Android's did:

```
I RanAudio: out: 1 voices ... [bgm-ring loop(-10000)]
I RanLoad: ChangeStage entered, to=2
I RanLoad: thread handle = 0x132ec7100        <- last line from the main thread
I RanLoad: presented frame 1 ... 360, hr=0x00000000
I RanAudio: out: 1 voices ... []              <- the BGM voice goes away here
```

The file was still growing when pulled, so the app is alive, not crashed: the
loading thread presents forever while the main thread is parked. The voice list
emptying at that moment is the track change that enters `UnLoadSoundBuffer`.
No `RanBgm`, `RanStall` or `ChangeStage:` lines anywhere, which dates the build
as pre-fix. The only errors in 11,656 lines are a missing `bootcover.bin`, two
`/sdcard/ran/` diagnostic probes that cannot exist on iOS, and one `.x` mesh
with no `TextureFilename`.

Nothing iOS-specific to fix - all three fixes are shared code. iOS build
`34444486422` is green and carries them (`ov_read returned`, `ChangeStage: %s`
and `loading art draw` all present in the Mach-O); the `.ipa` is in the
Downloads folder as `RanLegacyM-unsigned.ipa`, waiting to be sideloaded.

### World entry hung forever — the BGM decoder (fixed 2026-09-10)

Reported as "it did not even get to the real map world like before", and that
was right: only `cha_select.wld` and `log_in.wld` ever loaded. The client
logged `ChangeStage entered, to=2`, started the loading thread, and then the
game thread produced no further output — for as long as the process was left
running.

**Measured, on the live stuck process, without a debugger:**

* `/proc/<pid>/task/<tid>/stat` — the game thread was `S` with **zero** utime
  growth over 5 s, and `/status` showed **zero** context switches in the same
  window. Not a slow load and not a `Sleep` loop: a hard futex block.
* One thread, though, was `R` at a full core and had been since login
  (`utime` ≈ wall clock).

Neither `run-as` nor root is available on LDPlayer, so `syscall`, `stack` and
`debuggerd` are all denied. The shim was made to name the block itself
instead, and that instrumentation is worth keeping:

* `EnterCriticalSection` now tries the lock, then a 3 s `pthread_mutex_timedlock`,
  and on timeout logs its own caller resolved through `dladdr` before blocking
  for real. Same for the event wait and, via a watchdog thread, `pthread_join`.
* `threadTrampoline` names every thread after the routine it runs, so
  `/proc/<pid>/task/<tid>/comm` identifies a spinning worker on a device with
  no debugger.
* `DxGlobalStage::ChangeStage`, `DxStage::SetActive` and
  `DxGameStage::InitDeviceObjects` log each step they enter.

One run then said it outright:

```
E RanStall: EnterCriticalSection cs=0x7adb10040508 blocked >3s at
           _ZN10DxBgmSound17UnLoadSoundBufferEv+0x33
```

and `comm` on the spinning thread read `laySoundEPv+0x0` — `DxBgmSound::PlaySound`,
the music streaming thread, holding `m_csBuffer` and never giving it back.

**Root cause,** `DxBgmSound::GetPCMBlock`:

```cpp
while ( bytes_to_read > 0 )
{
    ret = ov_read ( ... );
    if      ( ret == 0 ) { ...; break; }
    else if ( ret <  0 ) { /* ignore the hole and read again */ }
    else                 { bytes_to_read -= ret; }
}
```

A negative `ov_read` leaves `bytes_to_read` untouched. Ignoring a *transient*
hole is what the PC code intends, but a permanent error makes every call fail
and the loop never advances — an infinite spin. It runs under `m_csBuffer`
(added when the buffer-release race was fixed), so the next track change parks
the game thread in `UnLoadSoundBuffer` forever, and the world never loads.

The measured error is **-131, `OV_EBADPACKET`**, with 2 bytes of the block
still wanted.

**Fix:** count consecutive negatives, and after 64 give up on the block —
return what was decoded and report no more data, which is a path the caller
already handles. Guarded by `RAN_MOBILE`; the log names the error code.

Verified: one login walks straight into สถาบัน SG at 59/6, HUD, chat and NPC
dialog drawing, 31 fps, and no thread above idle CPU.

**Left open:** *why* `ov_read` returns `OV_EBADPACKET` at all. A bounded retry
stops the hang but the block it drops is a real audio gap.

### iOS

1. **First compile.** Every file under `native/platform/ios/`, plus
   `shim/gl/gl_context_ios.mm` and `shim/d3d/image_decode_ios.mm`, is written
   against documented APIs and has never seen a compiler. No Mac hardware is
   needed for this — a GitHub Actions `macos-14` runner has Xcode and the iOS
   SDK, and `build-ios.sh` is what it would run. What a Mac (or a signing
   identity) *is* needed for is putting the build on a device.

   **DONE 2026-09-09.** It compiles and links for arm64 iOS; the runner
   uploads `ran.app`. Nine runs, and what they found is written up in "iOS
   compiles and links" below. The eight defects found by reading beforehand are
   in the section after that.

   **The route onto a phone from Windows**, which needs no Mac: the CI job
   uploads an unsigned `ran.app`; zip it as `Payload/ran.app` into an
   `.ipa`, and Sideloadly or AltStore signs it with an Apple ID over USB. A
   free account expires after 7 days and allows 3 apps; a paid one lasts a
   year.
2. **Audio — DONE 2026-09-04 on Android; iOS needs only a sink.** The game had
   no sound at all, on either platform, and the missing backend was only the
   last of three reasons:

   * `CWnd::m_hWnd` was NULL in the shim, and `DxSoundMan::OneTimeSceneInit`
     returns before doing anything when the handle is null — so the sound layer
     was never entered.
   * `timeSetEvent` was a stub returning 0. `BgmSound` drives its streaming
     thread with a periodic multimedia timer that sets an event, so the thread
     waited forever and not one block of music was ever decoded.
   * The device's `option.ini` had every volume at `DSBVOLUME_MIN`. The engine
     defaults are 0, which is `DSBVOLUME_MAX`.

   Built: `audio_mix.cpp` (the portable mixer — WAV clips at 8/16/24/32-bit,
   voices in DirectSound's own volume and pan units, ring voices for streaming,
   an int accumulator so overlapping sounds do not clip against each other),
   `audio_opensl.cpp` (the Android sink, two 20 ms buffers), `dsound_mobile.cpp`
   (IDirectSound/IDirectSoundBuffer over the mixer — the music path needs a real
   ring with a real play cursor), and a real `dsutil_mobile.cpp`.

   Measured in the world on LDPlayer: 5 voices, peak 25972–32768 of 32767, the
   ring taking 183 MB of non-zero decoded PCM. Silence on every build before it.

   **iOS sink written** (`audio_audioqueue.mm`, uncompiled like the rest of the
   iOS work) with the same background-pause behaviour; nothing above the sink
   is platform-specific. Two known gaps on both platforms: no headroom
   (the mix reaches full scale with music at default volume, as the PC does),
   and nothing mutes audio when the app loses focus (`RanAudio_SetMuted` exists
   and is not wired).

3. **`minIos` in the manifest.** `make-manifest.js --min-ios <n>` — the flag
   exists and is wired (`make-manifest.js:164`). The iOS patcher refuses a
   manifest without it, deliberately, so this has to be published before an
   iOS client may talk to the live server. **That puts item 9, publishing the
   pending patch, in front of any test on a phone**: the client cannot fetch
   one byte of the 4.7 GB until the live manifest carries the key.
4. **No S3TC on Apple GPUs.** The shipped textures are DXT1/3/5, and no Apple
   GPU has ever exposed `GL_EXT_texture_compression_s3tc`. `haveS3TC()` in
   `gl_render.cpp` already detects this by extension string and falls back to
   decoding every block on the CPU, so iOS will *work* the day it builds - but
   at 4x the texture memory and a CPU cost per load. The real answer is
   transcoding the store to ASTC, which every iOS device since the A8
   supports; that is a patch-store change, not a client one. Expect this to be
   the first thing the simulator shows.

5. **Signing.** A development profile, TestFlight, or sideloading. Read the
   copyright section of `IOS-PORT-PLAN.md` first: that decision comes before the
   work, not after.

### Android

6. **Riding: the specular repaint is fixed, the model is not.** RESOLVED
   2026-09-04 for the repaint. It was **not** the navmesh slope test I first
   suspected - that never entered the top ten. It was `EMECF_SPECULAR2`, the
   multipass cube-map specular, redrawing every vehicle piece whole:

   ```
   before  12.4 fps  80.8 ms   veh:parts 54.2ms  part:chareff 27.5ms  alpha 106,440 verts
   after   17.2 fps  58.2 ms   veh:parts 30.6ms  part:chareff  3.5ms  alpha  29,577 verts
   ```

   The repaint now has a 4000-triangle budget (`specbudget` overrides it, 0
   restores PC behaviour). **What remains is the model**: the BMW S1000RR body
   is 26,580 triangles for one piece, and drawing it once is still ~24 ms here.
   That is a LOD question, and `USE_SKINMESH_LOD` is still a stub - `g_dwLOD`
   is set and never read. Fixing it would pay back on every character, not just
   vehicles.

7. **Tab S9 verification.** Everything since V016 has been checked on LDPlayer
   only; the tablet has been off adb. The keyboard inset
   (`RanPlat_ImeInsetPerMille`) in particular cannot be verified on the
   emulator, which has no on-screen keyboard and reports 0.

8. **Intermittent SIGSEGV** in `RanTexture::LockRect` by way of
   `RanD3DXFont::glyphFor`. Still unattributed, no tombstone kept.

   A DIFFERENT crash was caught and addressed on 2026-09-04: SIGSEGV in
   `DxSkinAniMan::DoInterimClean` from `DxGlobalStage::ChangeStage`, faulting
   at offset 8 of a bad pointer. That clean-up erased map entries by a name read
   back **out of the object it had just freed**, so any mismatch between that
   name and the key the map was built with left a freed pointer in the map for
   the next stage change to walk. Fixed by erasing with the key, and by skipping
   pointers already freed. Not reproduced: `DoInterimClean` does not run on the
   login-to-world path, and reaching it again means another live-server login.
   The new log lines say what each pass frees and whether a key ever mismatches.

9. **Publish the pending patch.** The store at `native/out/launcher_mobile` is
   version 412 (APK V025, versionCode 42); the working build is well past it.

10. **The corrupted path was `ChangeExtName` finding the wrong dot.** RESOLVED
    2026-09-08. It had sat here for weeks as "a corrupted string, not a missing
    file", and every theory about it was wrong: the string was never corrupt.

    ```
    rb /storage/emulated/0/Android/data/com.enm -> FAILED (errno 2)
    ```

    A `_Unwind_Backtrace` in `ran_fopen`, fired on any path shaped like a bare
    `/Android/data/<name>`, named the caller in one run:

    ```
    DxGlobalStage::InitDeviceObjects
      DxSkinCharDataContainer::LoadData -> DxSkinCharData::LoadFile
        -> SetPiece -> DxSkinPieceContainer::LoadPiece -> DxSkinPiece::LoadPiece
          -> DxSkinMesh9_NORMAL::Load -> CSerialFile::OpenFile -> ran_fopen
    ```

    `DxSkinMesh9_NORMAL::Load` ends with
    `strName = ChangeExtName( strName.c_str(), "enm" )`, and `ChangeExtName`
    (`DxMethods.cpp:1582`) is `strSrcName.find('.')` — the **first** dot in the
    whole string. On the PC that is the same as the extension dot, because the
    paths handed to it are relative and no directory in the install has a dot in
    its name. On Android the app path is
    `/storage/emulated/0/Android/data/com.ran.native/files/...`, whose first dot
    is inside the package name. So the function cut the path at `com` and
    appended `.enm`: 33 bytes of `/storage/emulated/0/Android/data/` plus
    `com.enm` is exactly the 40 that were dumped. Nothing was corrupt; the
    engine asked for that path.

    Fixed by scoping the search to the last path component, `#ifdef RAN_MOBILE`
    so MSVC compiles the original line. `GetSpecularName` on the line above has
    the identical defect and got the identical fix. (`DxClubMan.cpp:203` has the
    shape too but takes a bare filename, so it is left alone and noted here.)

    **Measured on LDPlayer, same scene, before and after:** 332 sightings a
    session -> **0**, and the paths now built are right —
    `.../files/data/skin/s_m_face.enm`, resolving into the directory where the
    115 shipped `.enm` files actually sit.

    **What it does NOT fix, stated plainly:** nothing visible. Every `.enm` in
    the store is 152–408 bytes and begins `"default\0"` followed by zeros, so
    `bExist` is 0 and `m_pSlimMesh` stays null exactly as it did when the open
    failed — and `DxSMeshContainerToon.cpp:266` builds one at runtime anyway.
    No successful `.enm` open was observed either, because the nine the login
    scene asks for (`s_m_face`, `s_m_bs`, …) are not among the 115 that ship.
    What is gained is a lead closed, 332 log lines a session, and a latent path
    bug that would have mangled any absolute path fed to either function.

    The backtrace instrumentation is kept: it costs nothing until a path that
    shape appears, and it is the tool that would have found this weeks ago.

## Log


- **2026-09-03 (later)** — **The platform seams, and the iOS files they made possible.**

  Everything Android-specific that shared code was reaching for now goes through a seam,
  and an iOS target exists that uses those seams. **No iOS code has been compiled** —
  there is no Mac here — so every file under `platform/ios/` and
  `shim/d3d/image_decode_ios.mm` is a prediction until the first `build-ios.sh` run.

  What *is* verified is that Android is untouched by it: after every change the Android
  build was reconfigured from scratch and rebuilt for **both ABIs**
  (`errors: 0  failed: 0`), the APK repacked and installed on LDPlayer, and the game
  logged in and played at 57 fps.

  | Seam | Was | Now |
  |---|---|---|
  | Diagnostic flag files | 60 `/sdcard/ran/...` literals | `RanPlat_DiagPath` / `DiagExists` / `DiagOpen` |
  | Logging | 70 `__android_log_print` calls in 24 files | `RanPlat_Log` |
  | Fonts | `/system/fonts` hardcoded | `RanPlat_SetFontDir` |
  | Image decode | `AImageDecoder` called directly | `RanImage_DecodePlatform`, one per platform |
  | Keyboard inset | **`RanAndroid_ImeInsetPerMille`**, called from `SOURCE` | **`RanPlat_ImeInsetPerMille`** |

  The last one mattered: an Android name had leaked into shared client code
  (`Lib_Client/DxGameStage.cpp`, inside `RAN_MOBILE`). Renamed, rebuilt and re-verified
  on device — tapping the chat line still raises the keyboard (`mInputShown=true`). The
  inset **value** is still unverified: LDPlayer has no on-screen keyboard, so it reports
  0. That needs the Tab S9.

  New iOS files: `ran_ios_main.mm` (UIApplicationMain, EAGL ES3 context, CADisplayLink
  frame loop mirroring `android_main.cpp`, touch slots into `RanTouch_*`, UIKeyInput into
  `RanIME_InsertUtf8` / `RanIME_Backspace`, keyboard inset from
  `UIKeyboardWillChangeFrame`), `ran_ios_plat.mm`, `Info.plist.in`,
  `image_decode_ios.mm` (ImageIO), `build-ios.sh`, and four redistributable fonts in the
  bundle (NotoSansThai OFL, Roboto Apache 2.0) named exactly as the Android system files,
  because `RanFont_Resolve` picks by filename. CMake grew one `if(RAN_IOS)` branch per
  Android-specific line, and `RAN_IOS` is set only by `CMAKE_SYSTEM_NAME=iOS`. Full
  detail and what remains: `IOS-PORT-PLAN.md`.

  **Damage report, honestly:** the tree-wide `sed -i` used for the rename also rewrote
  the build artefacts under `native/out/` — including one published patch blob (the V025
  APK, patch v412). sed is not binary-safe and the round trip is lossy: 4,169 bytes were
  lost to line-ending translation. The build artefacts were regenerated; the blob was
  re-downloaded from the patch host and its SHA-256 verified against its own filename.
  Nothing was lost and nothing on the server was touched. The lesson: never run
  `sed -i` across a tree that holds build output.

  While there, `shim/platform/ran_plat.cpp` turned out to hold a **literal NUL byte**
  inside what should have been a `'\0'` character literal — legal C++, compiled fine, but
  it made the file binary to every text tool. Fixed to the two-character escape.


- **2026-09-02** — **One flag moved every label and armed a crash: the outline fix.**

  Reported as mob names sitting left of their mob, on a build where they had been
  fine. It was a regression of mine, and the cause was a single change - not any of
  the things I altered afterwards while chasing it.

  Making `GetVersionEx` report the truth (Windows 7, rather than the unknown
  version `GetWinVer` derived from an unfilled `wProductType`) was what switched
  the black text outline on. It also flips `CD3DFontX::m_bWindows98` from TRUE to
  FALSE, and that flag chooses between **two different implementations of text
  measurement**:

      m_bWindows98 : m_pd3dxFont->DrawTextW( NULL, ..., DT_CALCRECT )
      else         : GetTextExtentPoint32W( m_hd3dxDC, ... )

  This port has always laid out against the first. The switch was harmless at the
  time only because `GetTextExtentPoint32W` was a stub: it left `SIZE` at zero and
  the `if ( Size.cx == 0 )` fallback quietly put the D3DX path back. **Implementing
  that stub later removed the fallback** and put the GDI numbers into use, and they
  disagree with what is actually drawn - by more the longer the string:

      gdi=111  d3dx=102        gdi=128  d3dx=101        gdi=150  d3dx=116

  Layout centres a box on the measured width, so 34 pixels of over-measure puts the
  visible text 17 pixels left of the mob. Mobile now measures with the call that
  draws.

  The same flag also switched on `CTextUtil`. Its `FrameMove` runs from
  `CUIMan::Render` whatever the font path does, and it took the process down:

      signal 11 (SIGSEGV) ... RanTexture::LockRect
      CTextTexture::FrameMove -> CTextUtil::FrameMove -> CUIMan::Render

  `m_bUsage` has to stay TRUE - it is what gates the outline - so the cache is idle
  on mobile instead. Using it for real means implementing the GDI it builds its
  textures with (`ExtTextOutW`, `GetTextExtentPoint32W`, `FillRect`,
  `CreateSolidBrush`, now written but unused), which is separate work and would
  also buy back the 1.6 ms a frame the outline costs.

  Also found and fixed while reading this path: the string table returns mob names
  with a trailing blank (`Little Vulgarian` is 17 bytes, the last `0x20`; player
  names have none), and `CNameDisplay` never resized its name box with the control,
  so a centred draw inside a 20-pixel box holding 126 pixels of text started at the
  box origin.

  **What this cost:** a long stretch of the session spent changing things and
  re-testing instead of finding the one cause. The lesson is recorded in memory:
  when something worked before, the first move is to list my own changes that touch
  the affected subsystem - shared machinery like text metrics moves everything that
  depends on it.


- **2026-09-01 (evening)** — **Four interface faults, each traced to the PC mechanism first.**

  **NPCs showed a health bar.** `MobileTargetTick` called `SetTargetInfo` for whatever
  was latched, which is the *mob* panel - it shows health and floats a bar over the
  target's head. The client never does that for an NPC, and the reason is a gate
  rather than a branch: `SetTargetInfo` is only reached when

      (emCrow==CROW_PC && emACTAR==EMACTAR_PC_PVP) || emACTAR==EMACTAR_MOB
      || (bBRIGHTEVENT && emACTAR!=EMACTAR_NPC)
      || (emCrow==CROW_SUMMON && emACTAR==EMACTAR_SUMMON_ATTACK)

  which excludes `EMACTAR_NPC` outright; NPCs reach `SetTargetInfoNpc`, handed a name
  and nothing else. The tick makes the same three-way split now.

  It also had **two owners**. The PC dispatch runs off `m_sACTAR`, the pick under the
  pointer: a mouse re-picks every frame so there is only ever one panel, but a
  finger leaves the pointer where it last touched, so that code showed a panel for
  whatever was last passed over while the tick showed one for the latched target -
  two panels stacked. Those calls are `#ifndef RAN_MOBILE` now.

  **No black outline on any text.** The outline needs `CTextUtil::m_bUsage &&
  m_iOutLine`, and both were off because the client believed it was on Windows 98.
  `GetWinVer` asks with a ZeroMemory'd `OSVERSIONINFOEX`; its 6.1 arm switches on
  `wProductType` with no else, the shim never filled that field, so the zero matched
  neither arm and `nVersion` kept `WUNKNOWN` (0) - below `WNTFIRST` (101), the test
  everything downstream reads as "9x". The shim reports `VER_NT_WORKSTATION` now.

  Fixing that alone **deleted every glyph in the interface**: `m_bUsage` also routes
  text through `CTextUtil`'s texture cache, which builds textures with `FillRect` and
  `ExtTextOutW` - stubs in the port that report success and draw nothing. Mobile
  takes the immediate font path unconditionally now: outline kept, cache skipped.
  Measured cost, `interface` **0.7 ms -> 2.3 ms** a frame, being eight extra passes
  per string. Implementing those GDI calls would buy it back.

  **Presses fell through open windows** to the pad underneath. This hit test has now
  been wrong three ways, and the third is the one to remember:

  | test | fails on |
  |---|---|
  | `IsVisible()` | GENDER_CHANGE_WINDOW - visible flag set, nothing drawn, killed the pad's toggles |
  | `IsNoRender()` on the top control | a window is a CUIGroup that paints nothing itself; whole windows became click-through |
  | tree walk without pruning | a closed window's children keep their own visible flags; blocked the world and the pad |

  It is now a virtual that recurses and stops dead at a hidden group. Verified with
  the settings window dragged over the camera-lock button, using the HUD cache as the
  detector - a press changes a button's `down` state, forcing a rebuild:

      PK button, nothing over it     82128 -> 82968 verts, 7 -> 9 draws   toggles
      world drag                     83.5% of sampled world pixels changed
      settings window over the lock  82128 verts, 7 draws                 blocked

  **Camera lock followed the walk direction.** It used `GetDirectionVector()`, so the
  view swung round on every course change and the target slid off screen. It now
  aims at `m_sMobileTarget`, reading the position fresh from the copy list rather
  than the `STARGETID` (whose position is the one it had when selected). With nothing
  selected the camera is left alone.

### A dying target is dropped in 52 ms, not 521 (measured 2026-09-02)

The complaint was that a killed mob keeps its bar and stays hittable. Two probe
runs, one line per frame, same mob type and same server.

Dropping on `GLAT_DIE` alone:

    09:37:11.513  hp=1/180 die=0      <- health stops here
    ...  about twenty frames ...
    09:37:12.034  hp=1/180 die=1      <- 521 ms later

Adding `GLAT_FALLING`:

    09:53:07.245  hp=1/180 die=0 fall=0
    09:53:07.271  hp=1/180 die=0 fall=0
    09:53:07.297  hp=1/180 die=0 fall=1   <- 52 ms

`FALLING` arrives while `DIE` is still 0, and that is the whole difference. The
client's own continuation logic already treated either action as target-gone;
`MobileTargetIsLive` now tests the same pair.

**The client's health for a mob never reaches zero** - it stops at 1 and the
server sends no final update, only the death action. So the zero-health test
added earlier in the session is dead code for mobs. It is kept only because a
player's bar does reach zero.

**Two things in this area were my own regressions, both now removed:** a
fallback in the nearest-mob search that re-picked with `FindClosedCrow` (no
state test) exactly when every candidate was dead, handing the corpse straight
back; and the belief that auto-select was broken, which came from pressing the
wrong pad button.

### Dead targets, measured (2026-09-02)

Killing something now drops it everywhere at once. Instrumented across a kill:

    find: 4 candidates, 0 dead, best=27.4 chose=one
    attack: auto-selected a target
    live: target at 0/180 - dropping
    find: 3 candidates, 0 dead, best=37.1 chose=one

* the zero-health drop fires on the killing blow, before the death action
  arrives - that was the window in which the corpse was still attackable
* the corpse is not offered again; the next search picks a different live mob
* auto-select works. The earlier note claiming the corpse filter had broken it
  was **wrong**

**The pad button map was read backwards, and it invalidated several results.**
The right column is laid out upward from the attack button, so top to bottom it
is CAMLOCK (eye), PK (blades), AUTO (crosshair) - not the reverse. Every
"camlock" test before this was toggling AUTO instead. That also retires the
"button lit while its flag reads 0" item below: there is no divergence, it was
the wrong button being read.

**The camera lock maths is right.** `DxViewPort::CameraRotation` (bFrom=FALSE)
was simulated offline against the easing: from 90 degrees off, the shipped sign
converges (-1.5708 to -0.146 over twelve frames) and the opposite sign diverges
to 180. The lock does nothing without a live target, which is by design and is
what the probe kept showing. Still unconfirmed on a device - the emulator would
not boot again after this session.

### Settings never persisted — same root cause, no new fix (2026-09-02)

Reported as: uncheck **Classic Name** in ตัวเลือก, press ตกลง, and it is back
next session.

Not a separate bug. `RANPARAM::SAVE_GAMEOPTION` was always working — it goes
through `CFile::Open`, which resolves paths correctly. The **load** was the
broken half: `LOAD_GAMEOPTION` gates on
`PathFileExists( <root>\option.ini )` before it reads a single key, and that
check was answering "missing" (see the entry above). So every option in
`[GAME OPTION]`, `[SCREEN OPTION]`, `[SOUND OPTION]` and `[GRAPHIC OPTION]` was
written to disk correctly and then ignored at every start, leaving the whole
settings window on compiled-in defaults.

Verified end to end on device, decoding the file each time
(`node tools/rcc-extract/gamecrypt.js` decodes option.ini):

| step | file | UI after restart |
|---|---|---|
| start | `bClassicNameDisplay = 1` | checked |
| uncheck + ตกลง | `= 0` | unchecked |
| check + ตกลง, restart | `= 1` | **checked** |

The last row is the one that proves it: the flag's compiled-in default is
`FALSE`, so it can only come back checked by being read out of `option.ini`.
Left at `0` (off), which is what was asked for.

### PathFileExists bypassed the path resolver, and it cost the whole GAME_FEATURE block (2026-09-02)

Reported as: long-press another player, choose **ดูข้อมูลส่วนตัว** (view personal
info) — nothing happens, no window, no error message.

**Cause: `PathFileExistsA` in `shim/win/windows.h` called plain `fopen`.** The
`#define fopen ran_fopen` sits further down that same header, so the call there
resolved to raw libc `fopen` and got the client's Windows path verbatim —
backslash separators, wrong case. It always answered "missing" for a file that
is present.

One client caller gates on it, and the damage is out of all proportion:

    RANPARAM::LOAD_GAMEOPTION()          RANPARAM_OPTION.cpp:22
        if ( !PathFileExists( <root>\option.ini ) ) return FALSE;

`RANPARAM::LOAD` runs `LOAD_PARAM` -> `LOAD_GAMEOPTION` -> `LOAD_FEATURE` and
aborts the chain on the first FALSE. So **`LOAD_FEATURE` never ran and every
`[GAME_FEATURE]` flag in Config.ini kept its compiled-in default**, most of them
`FALSE`. `bFeatureViewCharInfo` is one of them, and `RequestCharacterInfo`
returns silently when it is off — hence a button that does nothing and says
nothing. The CP bar in the HUD was another casualty; there will be more.

Measured, at the click:

    before:  RequestCharacterInfo id=27 feature=0 timer=5.02/5.00     (no packet)
    after:   RequestCharacterInfo id=27 feature=1 timer=5.01/5.00
             sent SNETPC_REQ_CHARINFO
             REQ_CHARINFO_FB emFB=2                                   (WEARINFO)

and at boot, after the fix:

    LOAD_FEATURE: opened <root>\Config.ini
    bFeatureViewCharInfo=1  bFeatureStudentRecord=1  bFeatureProduct=0

Fix: `PathFileExistsA` calls `ran_fopen` directly, declared immediately above it
so the macro's position in the header stops mattering. Verified on a clean
probe-free build: the character info window opens with test02's equipment, 3D
model, stats, school and guild.

**Ruled out along the way**, each by measurement rather than argument: the click
delivery (the whisper button in the same menu works and fills `@test02` into the
chat input), the request timer (`5.02/5.00`, past the 5s gate), the UI keyword
(`RAN_ANOTHER_CHAR_WINDOW 506,0 480x528` is present in the uicfg), the message
routing (`NET_MSG_GCTRL_REQ_CHARINFO_FB` is in the `DxGlobalStage` switch), and
the file itself (`Config.ini` decodes to `bFeatureViewCharInfo = 1`, and opens on
device with no `CIniLoader::open` error in the client's own ErrorLog).

**Worth re-testing generally.** Anything that reads `[GAME_FEATURE]` has been
running on defaults for the whole port, so features may appear that were never
seen working here before.

### The interface turning to garbage: root cause (2026-09-02)

Reproduced on LDPlayer and fixed. Trigger: **use a `กล่อง POWER UP` from the
inventory**. The buff draws its "POWER UP" banner, and from that moment every
string in the game renders as a solid white block — glyph quads in the right
places, at the right widths, with Thai marks stacked correctly, but filled flat.
Icons, world and window frames stay perfect.

**Cause: `g_gl.program` and `g_variantKey` are two halves of one piece of state,
and only one of them was being invalidated.**

`RanGLR_InvalidateStateCache()` calls `g_gl.reset()`, which zeroes the cached
program. It did **not** reset `g_variantKey`. `useVariant()` opened with
`if (key == g_variantKey) return;` — so after the touch HUD drew with its own GL
program and invalidated the cache, the next engine draw asking for that same
variant returned early, never issued `glUseProgram`, and rendered under the HUD's
program. Everything stayed wrong until some other variant happened to be asked
for. The banner is what pins the UI pass to a single variant, which is why the
POWER UP box makes it permanent rather than a one-frame flicker.

That is also the long-unexplained sticky `glErr=0x0502`: drawing with the wrong
or no program is exactly `GL_INVALID_OPERATION`.

Measured across the fault, at the text draw itself:

    before the box:  FONTDRAW ... prog=33
    after the box:   FONTDRAW ... prog=0          <- white text
    after the fix:   FONTDRAW cached prog=33  actually bound=33  variantKey=00000401

Fix, both halves: `useVariant` re-asserts `useProgram` even when the key has not
changed (`useProgram` is itself cached, so it is free when nothing moved), and
`RanGLR_InvalidateStateCache` clears `g_variantKey`. Verified on a clean
probe-free build: box used, all text correct, `glErr=0x0000`.

**What this was NOT.** Ruled out by measurement, each ruling out a theory that
looked right: the glyph atlas (read back off the GPU through an FBO mid-fault —
`alive=1 levels=2 minf=GL_LINEAR`, texels carrying real coverage `00 32 2A 23 40`,
not opaque white); the texture upload paths; `glGenerateMipmap`; atlas-full; the
text outline; the fixed-function stage-0 ops (`colorop=4 arg1=2 arg2=0` right
through the fault); render-target volume (identical, ~7000 draws/s, in working
and broken sessions).

### Two real bugs found on the way, both fixed, neither of them this one

**The unit-0 bind cache could disagree with GL.** `RanGLR_Draw` skips
`glBindTexture` when `g_gl.texture2D` already names the texture it wants, but
`RanGLR_UploadTextureLevel`, `RanGLR_UpdateTextureRect`, `RanGLR_FinishTexture`
and the render-target path all bound unit 0 without updating that field, and
`RanGLR_DeleteTexture` deleted a bound texture (GL unbinds it) without clearing
it. A draw could then sample the wrong texture *and* have
`RanGLR_ApplySampler` write its `glTexParameteri` onto that wrong texture, and
memoise the result against the right one. One `bindTex2D()` helper is now the
only way unit 0 is bound, and `RanGLR_DeleteTexture` forgets the name plus its
`g_texLevels` / `g_texDims` entries, which were otherwise inherited by whatever
id GL recycled next.

**`ATLAS_W`/`ATLAS_H` were file-scope and grew under fonts that had already
allocated.** A font holding a 1024 atlas would then pack and compute UVs against
2048, writing past the end of its locked bits. Per-font `m_atlasW`/`m_atlasH`
now; the global only sizes the next atlas.

### Test-rig notes from this session

* `login-ld.sh` was stale — it targeted `android.app.NativeActivity`, which the
  launcher split replaced, and typed credentials by tapping a client-drawn keypad
  that no longer exists. **`ld-login.sh` is the working one**: it starts
  `com.ran.launcher.RanActivity` and types through the IME. The dead component
  name is fixed in all the login scripts.
* Skipping the server-row/channel/connect taps and typing straight into the login
  box hangs forever with **no socket open at all** (`/proc/net/tcp` for the app's
  uid is empty). Check that before blaming the server.
* `adb shell input keyevent 37` (KEYCODE_I) opens the inventory — letters map to
  DirectInput scan codes in `scanCodeFor`. Far more reliable than hunting the
  icon row, whose contents shift as windows open.
* `กล่องของขวัญ POWER UP` is the *pack*; `กล่อง POWER UP` inside it is the item
  that triggers the bug. Using the pack proves nothing.


### Shipping: code now travels by patch too (2026-09-02)

Detail in `PATCHING.md`; this is the shape of it.

Native code cannot ride the data payload — since Android 10 an app targeting
API 29+ may not `dlopen` a library out of its own writable storage, and this one
targets 34. So there is no equivalent of dropping a new `MiniA.exe` in. Instead
the APK goes into the store as a blob, the manifest names it, and the launcher
installs it after the player confirms. The hash comes from the signed manifest
and the bytes stream straight into a `PackageInstaller` session, so they are
never a file anything could swap between the check and the install. Proven by
flipping one byte on the server: `checksum failed for the apk`, session
abandoned, player left on the working build.

`MAKE-PATCH.bat` is now the whole job. It compiles both ABIs, and only if
something the APK carries actually changed does it bump the version and
repackage — so a data-only patch never offers anyone a 320 MB reinstall of an
identical binary. Then it sweeps `out/` and the store down to what ships and
writes the manifest. Publishing an APK nobody would receive is a hard error, not
a warning, in both of the ways that happen silently: `versionCode` not bumped,
and code rebuilt without repackaging.

The APK is `RanMobile.apk` now - one name every release, so a link to it never
has to be reissued. `versionCode` stays a private counter that
only goes up, because Android compares it and refuses to install over a higher
one; `versionName` is the release label, and the file is named after it.

`option.ini` is seeded rather than shipped — installed when a player has none,
never overwritten after. It is the one file in the list the client writes, so
shipping it normally reset everyone's settings on every patch, silently undoing
the settings fix above.

**One-time:** the APK players hold has no updater in it, so it ignores the
manifest's `apk` block. That group needs `RanMobile.apk` by hand once.
Everything after is a patch.

**Both keys are single points of failure and are gitignored.**
`native/android/debug.keystore` is the app's identity — a different key means no
player can ever upgrade, only uninstall and re-download 1.7 GB.
`tools/patch/keys/manifest-signing-key.pem` signs the manifest — without it no
patch can ship at all. Back both up off the build machine.

### The `.ini` files, and a correction (2026-09-02)

**Correction.** An earlier version of this entry said the packs contain no
`.ini` at all, and that `Rank.ini` therefore cannot load on PC either. That was
wrong, and wrong because of a bad measurement: the check called a method the
rcc reader does not have, got array indices back instead of names, and counted
zero `.ini` in a pack that holds seventeen. `GLogic.rcc` contains `Rank.ini`,
`attendance.ini`, `busstation.ini`, `comment.ini`, `emoticon.ini`,
`pandorabox.ini`, `colortable.ini` and ten more. Whatever `LOADRANK` is
complaining about, "the file is not in the pack" is not it.

**What ships**, six files:

| | |
|---|---|
| `config.ini` | the `[GAME_FEATURE]` flags |
| `param.ini` | the game server address |
| `comment.ini` | read from the pack, but the PC client carries a root copy too, so it ships for parity |
| `option.ini` | seeded - installed when absent, never overwritten |
| `data/skin/desktop.ini`, `textures/item/desktop.ini` | Windows Explorer folder settings that the PC install happens to contain. A kilobyte between them, and present in `Ran/`, so they ship rather than open a hole in `--verify` |

**What does not**, and should not: the other 63 `.ini` in `CLIENT/`. They are
server-side or already inside `GLogic.rcc`, which is where the client reads them
from - `bGLOGIC_ZIPFILE` is always on and `gltexfile::open` has no loose
fallback. `Hackshield/*.ini` is skipped with the rest of that directory.

**One trap this exposed.** The duplicate check matches on the bare filename,
and an archive stands for the directory it lives in - so the root `comment.ini`
was dropped because `GLogic.rcc`, which is `data/glogic/`, holds an entry of
that name. Two different destinations agreeing on a filename. Root files are now
never deduplicated: there are four, they are a kilobyte each, and getting one
wrong costs more than shipping all of them.

### Security review of the delivery path (2026-09-02)

Full read of the fetch, verify, write and install paths. Two things fixed, the
rest recorded as checked so the next review starts from evidence.

**Fixed: all-files access no longer gates startup.** `onCreate` refused to run
without `MANAGE_EXTERNAL_STORAGE` - permission to read and write every file on
the device - even though the data root is this app's own external files
directory, which needs no permission at all. The only remaining use for it is
spotting a pre-private-root install under `/sdcard/ran` and moving it, saving
that player a 1.7 GB re-download; nobody installing fresh has any use for it.

It is now asked for once, only from someone who might benefit (no data in the
private root, permission not already held), and the run continues whatever they
answer. Measured with the permission set to `deny`: no prompt, boots, logs in,
reaches the world, `glErr=0x0000`.

**Fixed: `RanActivity` was `exported="true"`.** Any app on the device could
start the game directly, skipping the launcher and with it the update check and
the signature-verified manifest. Now `exported="false"`; `am start` on it
returns `Permission Denial: ... not exported from uid 10074`. The login scripts
go through `RanLauncher` instead, which is the path a player takes anyway.

**Checked and sound**, each read rather than recalled:

* the manifest is verified before it is parsed, fail-closed on missing,
  malformed or wrong signature (P-256 ECDSA, key compiled into the APK)
* `safeDest` blocks traversal twice - syntactic (absolute, drive, backslash,
  `..`) and canonical-path containment
* anti-rollback on both halves: the data version cannot go backwards, and only a
  strictly newer `versionCode` is offered
* the APK never exists as a file - streamed into a `PackageInstaller` session,
  hashed in flight, abandoned on mismatch. Proven by flipping one byte on the
  server
* blobs: size ceiling enforced mid-stream, hash checked before the atomic
  rename, temp file in the private root
* APK signed with v2 and v3 schemes, one signer, `CN=RAN Debug, O=RAN, C=TH` -
  a unique key, not the well-known public Android debug key
* `allowBackup="false"`, `debuggable` absent, cleartext scoped per host
* the native loader has no `/sdcard/ran` fallback for game data, so the private
  root really is the only place the C++ parsers read from

**Residual, accepted:** both signing keys are protected by file secrecy alone -
the keystore password is `android` in `build-apk.sh` and the manifest key is an
unencrypted PEM. Anyone holding `debug.keystore` can sign an APK that installs
over the real app through any channel, which is the argument for backing them up
*securely* rather than merely backing them up. Redirects are followed, which
cannot inject content since everything is hash- or signature-checked. A local
actor with root can write a high `.patchver` to stall updates.

### The patch payload was missing 3 GB, and no fresh install could have worked (2026-09-02)

Found while looking for the loading art. The payload carried `data/` and nothing
else. Against the shipped PC client:

| in `Ran/` | size | files | was in the manifest |
|---|---|---|---|
| `data/` | 2.4 G | 6,408 | yes |
| `textures/` | 2.8 G | 16,206 | **no** |
| `sounds/` | 256 M | 864 | **no** |
| `cVer.bin` | — | — | **no** |

Every item icon, all interface art, the loading screen, every sound, and the
version file the login compares. Nobody had hit it because every device so far -
the Tab S9 and LDPlayer included - was seeded by `push-data.sh`, a manual full
push. The patcher had never once provisioned a device on its own, which is
exactly what handing the APK to a new player would have done.

`--verify` missed it because it only walked `Ran/data`. It now walks the whole
client root, with a skip list for the parts of a PC install that have no
business on a phone (`.exe`, `.dll`, `GMTool`, `Hackshield`, `Logs`,
`cFileList.bin`, `Launcher.URS`). It passes clean.

`cache/` is still not shipped, and should not be: it is the font cache, created
and written by the client at runtime.

**Loose files already inside a pack are dropped.** Matched on the bare name -
which is how the reader resolves an entry - and then confirmed by comparing the
bytes, because a name collision between two different files would otherwise
silently drop one. 74 of 80 matches were byte-identical (14.2 MB, mostly `.x`
models also in SkinObject.rcc); the other 6 are kept, same name and different
content.

Payload is 1.68 GB -> 4.72 GB, 8,263 -> 25,259 files.

**Proven from scratch**, which is the only test that counts here: device data
root deleted entirely, then the launcher pointed at a local store. It fetched
all 4,722 MB, and the client booted on it - server select with full art, correct
Thai, the RAN mark. Before this it would have had no textures at all.

### The patch screen is the game's loading screen now (2026-09-02)

The player used to meet a bare dark panel, then a moment later the client's
loading art: two screens for one wait. The launcher now draws the same lobby art
(`loading_002.dds`), the RAN mark from the login page (`LOGIN_MARK`, taken from
the ui config rather than eyeballed), and the progress along the bottom. The
in-game splash drops the HINT badge and the corner spinner to match.

The art is a drawable in the APK, and has to be: on a first install this screen
is painted *while* the data root that holds it is still downloading.
`extract-launcher-art.js` regenerates both PNGs from the client textures.

**A regression of my own, caught on device.** Asking for storage permission on a
fresh install opened the Settings screen, which put the launcher in the
background exactly as the download started - and the permission change then
killed the process: `Killing com.ran.native (adj 900): MANAGE_EXTERNAL_STORAGE
changed`. The one case the prompt was meant to help, it broke. Nothing asks for
storage now: an updating player keeps the grant from the old build and gets the
migration, a fresh install has nothing to migrate. The launcher also holds
`FLAG_KEEP_SCREEN_ON`, since a 4.7 GB download dies with the screen.

### The payload mirrors the shipped client now, not the dev tree (2026-09-02)

`CLIENT/` carries client *and* server data, so "it is in CLIENT" was never a
reason to ship a file. Measured against `Ran/`, the real shipped client, the
payload held **2,061 files it does not have** - and four of its directories were
the reason:

| the shipped client has | our dev tree has |
|---|---|
| `quest/Quest.rcc` | 838 loose `.qst` |
| `npctalk/NpcTalk.rcc` | 665 loose `.ntk` |
| `level/Level.rcc` | 266 loose `.lev` |
| `effect/char/EffectChar.rcc` | 200 loose `.effskin_a` |

Those four archives were already being shipped - the directory walk picks them
up - but so was every loose source beside them: quest script, NPC dialogue and
level data handed to players as readable files, which the PC client has never
done.

The duplicate check only indexed the packs named in `SHIP`, so these four, which
arrive through the walk, were invisible to it. It now indexes **every `.rcc` in
the payload**. 2,035 of 2,041 name matches were confirmed byte-identical and
dropped; the 6 that differ are kept, which is the whole point of comparing bytes
rather than trusting names.

`.bak-`, Explorer's Thai "สำเนา" copies and a stray `test.effskin` are excluded
too - three dev leftovers that were being published.

23,294 files, 4,679 MB. What remains that `Ran/` lacks is 96 `.enm` and one
`.mxf`: costume entries newer than the reference install, so genuinely content
rather than leftovers.

`MOBILE/native/out/PAYLOAD.txt` lists every one of them — size, path, and an `S`
on the seeded entry — and is rewritten on every publish, so "what does a player
actually get" never needs a JSON reader on a 3.7 MB manifest. `PATCHING.md` has
the breakdown by weight.

**Checked functionally, not just by hash.** The loose sources were deleted off
the device, leaving only the archives, and the client was taken into the world:
`glErr=0x0000`, HUD, mobs, NPCs, Thai chat all correct. `QUEST load fail : 1307`
appears in that run's log - and in the two runs *before* the deletion, 12 times
in each. Pre-existing, and not caused by this.

### Still open from this session

* **The camera lock is not verified on a device.** Maths checked offline (see
  above); it no-ops without a live target by design. Needs one run on hardware:
  target a mob, lock (TOP icon), circle it - the mob should stay in front.
* Text costs 1.6 ms a frame more than it did. `ExtTextOutW`, `GetTextExtentPoint32W`,
  `FillRect` and `CreateSolidBrush` are the four stubs standing between the port and
  the text-texture cache that would remove it.


- **2026-09-01 (cleanup)** — **The Unity path is gone, and the port is the only client.**

  17.1 GB removed. `MOBILE/` was 23.6 GB and is now 6.5 GB, effectively all of it
  `native/`.

  | Removed | Size |
  |---|---|
  | `unity/` — the Unity project and its 7.6 GB of imported art | 8.9 GB |
  | `assets/` — rcc-extract staging, 16,072 textures | 3.7 GB |
  | 1,089 test screenshots in `native/out/` | 3.7 GB |
  | `ran-phase2.apk` and its idsig, superseded | 339 MB |
  | `build/mapobj` — extracted map objects | 29 MB |
  | `client/`, `spike/` — the JS protocol spike that preceded the port | 166 KB |
  | stray logs, a pulled `simpleperf`, `orphan-ui-atlases.txt` | — |

  None of it was in git: `unity/`, `assets/` and `build/` are all gitignored, so this
  is not recoverable, and it was chosen deliberately. Nothing had been touched
  since the native-port decision on 2026-08-24.

  **43 screenshots were kept** — the ones cited by name in this file and the other
  docs as evidence for past findings. Deleting those would have left the write-ups
  pointing at nothing; the list was built by scanning all 20 markdown files rather
  than guessing.

  Two things that looked like findings and were not, both checked before acting:
  every `.cpp` under `shim/` appears unlisted in `CMakeLists.txt` because it uses
  `file(GLOB_RECURSE SHIM_SRC ...)` — there is no dead source there; and `unity/`
  refused to delete with "Device or resource busy" from a transient handle, not an
  open Editor.

  `reference/` stays: the decoded file formats are engine-independent and still
  true. `archive-unity/` stays as history, 228 KB.

  Verified after: the payload signature still verifies, 8,017/8,017 blobs present,
  the release APK intact.

- **2026-09-01 (later)** — **The patcher audited, and six of seven findings closed.**

  The client verified every blob against a SHA-256 that came **out of the manifest**,
  and fetched the manifest over plain HTTP to a bare IP. So the check caught
  corruption and stopped no attacker at all: whoever writes the manifest decides
  what lands on the device, and that was anyone on the network path.

  | # | Finding | State |
  |---|---|---|
  | 1 | `manifest.json`'s `path` used as a destination with no validation | **fixed** |
  | 2 | Response bodies written until EOF, no size cap | **fixed** |
  | 3 | `REQUEST_INSTALL_PACKAGES` declared but never used | **removed** |
  | 4 | Manifest unauthenticated | **signed**; transport still cleartext |
  | 5 | `.patchbase` redirect readable by any app | **fixed** |
  | 6 | Version compared with `!=`, so downgrades accepted | **fixed** |
  | 7 | Data root readable/writable by any app with storage permission | **fixed** |

  **Path traversal (1).** `new File(rootDir, e.getString("path"))` with nothing
  checking it: `../../../../x` wrote outside the data root, and this app holds
  `MANAGE_EXTERNAL_STORAGE`. The download path also deletes the destination before
  renaming over it, so a hostile manifest could delete as well as create. Now
  rejected on both passes, and proven with a hostile manifest served over an adb
  reverse tunnel: `path escapes the data root: ../../../../sdcard/Download/...`,
  no file written, and the server log shows it never even asked for the blob.

  **Signing (4).** `manifest.json` is signed with a P-256 key; the client verifies it
  against the public half compiled into the APK, **before parsing the JSON** — a
  parser is the first thing an attacker reaches, so it must not run on unverified
  bytes. Fails closed, all three cases measured:

      valid signature                    Up to date  |  version 366
      one byte changed, sig kept         manifest signature does not verify
      manifest.sig removed from server   no manifest signature on the server

  An attacker who cannot sign cannot publish, whatever they do to the transport
  or to the host. **The private key is gitignored and must be backed up like the
  release keystore** — the public half is baked into every installed APK, so
  losing it means no further patch can reach existing installs at all.

  **Rollback (6).** A signature cannot stop an old manifest being replayed; it stays
  validly signed forever. Any version below the installed one is now refused. To
  ship old content deliberately, republish it under a higher number.

  **The data root (7).** `/sdcard/ran` is shared storage, and the client's C++ loaders
  are not hardened against hostile input — another app editing a `.rcc` in place
  was a route into this process. The root is now the app's own external files
  directory, unreachable by other apps on Android 11+, needing no permission, and
  still visible over adb. The native loader already tried that location; it just
  tried shared storage first, so the order is flipped and the old path stays as a
  fallback for adb-pushed test trees. That also moved `.patchbase` out of reach,
  which closes (5).

  Migration is the awkward part. Renaming would be instant and is tried first, but
  **Android refuses a rename from shared storage into `Android/data/<package>`
  whatever permissions are held** — measured with `MANAGE_EXTERNAL_STORAGE` granted,
  0 of 33 entries moved. So it copies, which took about five minutes for a real
  9.2 GB install on the emulator. `config.ini` is copied last and marks the tree
  complete, so an interrupt restarts rather than leaving half a tree; the old files
  go only once the new tree is good; any failure leaves the old root in charge.
  Verified end to end: migrated, booted from the private root, logged in, rendered.

  **What is left.** The transport is still cleartext HTTP. With the manifest signed
  that costs confidentiality, not integrity — nobody can change what is published,
  they can only watch it go past, and there is nothing secret in the payload. HTTPS
  needs a certificate on the patch host, which is not something this side can do:
  the client already speaks it, so it is `BASE_DEFAULT` plus removing the cleartext
  exception from `network_security_config.xml`. `MANAGE_EXTERNAL_STORAGE` is also
  still requested — needed now only to migrate an old install and to read the debug
  switches, and droppable once both are gone.

  Written up in `MOBILE/PATCHING.md`.


- **2026-09-01** — **The frame rate drop was the new HUD rebuilding itself sixty times a second.**

  Reported as "back to 35 fps on the tablet". Two things were true at once, and only
  one of them was a regression.

  **The 120 was never a tablet in-world number.** The 105-120 fps in this log is the
  emulator; the only in-world Tab S9 measurements ever taken are the 20.6 ms
  (~48 fps) in the character-shadow work below. So the honest comparison is
  ~48 fps against the reported 31, not 120 against 31.

  **The regression was real, and it was mine.** On the tablet the frame was 30.3 ms
  with GPU sections at 0.0 and swap at 0.5 - CPU-bound, not fill-bound - and the
  named sections only accounted for 9.3 ms of the 26.5 ms of engine CPU. Fifteen
  milliseconds a frame had no timer on them.

  It hid from both of the usual instruments. `/sdcard/ran/nulldraw` removes every GL
  call a draw makes and took the frame from 30.3 ms only to 25.0; the shim's
  `submit` timer reported 3.3 ms. Both only see draws issued through the renderer,
  and the touch overlay has its own program, VAO and buffer - so both reported a
  frame that was cheap while it was not. Five calls in `DxGameStage::Render` had no
  `RAN_SECTION` at all, `RanTouch_Render` among them; they have one now.

  With a timer on it, on LDPlayer:

  | | frame | of which submit | engine cpu |
  |---|---|---|---|
  | overlay drawn | 46.5 ms (21.5 fps) | 24 ms | 20.5 ms |
  | `/sdcard/ran/nohud` | 28.4 ms (35.2 fps) | 25 ms | 2.4 ms |

  The controls cost **18 ms a frame**, and were generating **94,086 vertices** to do
  it. Almost none of that changes between frames: the buttons do not move, and
  their faces, bevels, glosses and glyphs are identical. It was rebuilt every
  frame because there was nowhere to keep it.

  **Two fixes, and the first one alone made it worse.** Batching every shape into one
  draw took the overlay from a few hundred `glBufferSubData`+`glDrawArrays` pairs to
  7 a frame - and `touch-hud` went *up*, 8 ms to 18 ms, because expanding fans and
  strips to triangle lists tripled the vertex traffic. That is the measurement worth
  keeping: **the draw calls were never the cost.** Building the geometry was.

  So the static half is now built once into its own buffer and replayed from there,
  and rebuilt only when something it depends on changes - a button going down, a
  toggle lighting, the skill arc being rearranged, the window resizing. A 64-bit
  signature over exactly those inputs decides. The stick and the recharge wipes
  genuinely move every frame and are still built live; they are small. The segment
  list carries the blend mode, because that is the one piece of state the vertices
  cannot.

  **Result**, same scene, same build, toggled with `/sdcard/ran/nohud`:

  | | frame | of which submit | engine cpu |
  |---|---|---|---|
  | overlay drawn | 34.1 ms (29.3 fps) | 24.3 ms | 7.9 ms |
  | `/sdcard/ran/nohud` | 28.0 ms (35.9 fps) | 18.4 ms | 7.7 ms |

  **18 ms to 6 ms**, and the engine CPU is now identical with the overlay on and off -
  what remains is the driver drawing 94k cached vertices, which is LDPlayer being
  slow at vertex processing and should be a fraction of that on the tablet. The
  report says `0 rebuilds/s`; tapping PK produces exactly 2 (down, then toggled) and
  the lit state adds its bloom segment, 7 draws to 9.

  The tablet's missing 15 ms was CPU, and the CPU rebuild is what has been removed,
  so this should land there too - **but it has not been measured on the Tab S9**,
  which went offline mid-session. That is the one thing left to confirm.

  New: `/sdcard/ran/nohud` (live, re-read once a second) and a `touch-hud:` line
  reporting draws, vertices, rebuilds and cache size. Both join the list of debug
  switches to compile out for release.


- **2026-08-25 (evening)** — **The frame was being spent re-uploading vertex buffers, not drawing.**

  13 fps on the tablet reproduced exactly on the emulator, so the cause was not device-specific.
  Added a frame report (`FRAME` lines in logcat, `RAN_MOBILE`-guarded) that splits every frame into
  update / render / present, then splits render again into swap, draw submission and engine CPU, and
  finally times each section of the game stage. What it found:

  | | before | after |
  |---|---|---|
  | buffer uploads per frame | 94 (**7.5 MB**) | 86 (**390 KB**) |
  | time in those uploads | **45 ms** | **1.8 ms** |
  | in-game GUI section | 25 ms | 1.3 ms |
  | frame (same spot, emulator) | 77 ms (13 fps) | 49 ms (20 fps) |

  **Cause.** Every `Unlock` on a client vertex or index buffer re-specified the *entire* buffer with
  `glBufferData`, however little the client had written. The client locks a slice — a batch of UI
  quads, a patch of terrain, a run of particles — hundreds of times a frame. `glBufferData` also
  orphans the old storage, so the driver had to find new memory each time.

  **Fix.** D3D says what it is about to do at `Lock` time, so use it: a `D3DLOCK_READONLY` lock
  dirties nothing, every other lock records its byte range, and the range (not the buffer) goes up
  with `glBufferSubData`. A write that never told us a range still sends the whole buffer, so no path
  can leave GL holding nothing.

  **Two traps found on the way.**
  - The element-array binding lives *inside* the VAO. Routing uploads through the bind cache meant a
    later draw skipped the real bind as "already bound", so the draw had no index buffer; the
    emulator's GL encoder then treated the index offset as a host pointer and dereferenced null. The
    cache now forgets the element binding whenever the VAO changes or an upload runs outside a draw.
  - `glDeleteBuffers` frees the *name*, and `glGenBuffers` hands the same one back. The cached vertex
    layout is described against a buffer name, so a recycled name could skip re-specifying attribute
    pointers. Deleting a buffer now invalidates the layout.

  Also: textures no longer re-upload whole (a lock records its rectangle, and a partial update skips
  mip regeneration) — the font atlas was re-uploading and re-mipping for every glyph added.

  **Where the frame goes now (emulator, busy town):** swap 1.3 ms, engine CPU 10-19 ms, and draw
  submission 40-60 ms — of which `Render_MobItem` is nearly all of it, at ~222 GL draws a frame. That
  per-call cost is LDPlayer's GL translation (~0.3 ms/call); a real driver is roughly twenty times
  cheaper, so the tablet's profile will look different and needs its own `FRAME` line.

- **2026-08-25 (later)** — **Frame time halved again; the emulator had been running a stale ABI all along.**

  **LDPlayer runs the x86_64 slice, not arm64.** `./build.sh` alone rebuilds only arm64, so every
  emulator test since the LP64 fixes had been running an `x86_64/libran.so` from 00:23 — which is why
  world entry still died in `SHELPNODE::LoadFile` and the GUI still drew at desktop scale there. Both
  ABIs are now built before packaging (`./build.sh && ABI=x86_64 ./build.sh && ./build-apk.sh`), and
  with the current slice the emulator shows the real login scene, the mobile-scaled GUI and the world.

  **Five-matrix blend palette.** `ConvertToBlendedMesh` splits each skinned piece into groups whose
  bones fit the palette, and every group is one draw call. Raising the palette from four matrices to
  five (the ceiling for non-indexed blending: `D3DFVF_XYZB4` carries four weights and the fifth matrix
  takes the remainder) plus best-fit rather than first-fit packing cut the hand piece from 11 groups to
  6 and the body from 6 to 4. Measured in the same spot in town:

  | | before | after |
  |---|---|---|
  | skinned draws / frame | 152 | 54 |
  | frame time | 54.7 ms | 33 ms |

  Touched: `kMaxPalette`, the FVF choice, `MaxVertexBlendMatrices`, the shader (`aBlend` is a vec4,
  `uWorldM[5]`), the FVF walk (`XYZB4` now means four weights) and the device palette copy.

  **Win32 `clock()`.** The client measures its ping as a difference of `clock()` values and prints it
  as milliseconds — Win32 semantics (wall time, `CLOCKS_PER_SEC` 1000). Bionic gives processor time
  with `CLOCKS_PER_SEC` 1000000, so the ping display sat pinned at its 1000 ms clamp. The shim now
  provides `RanWin_clock()` and redefines both, so everything dividing by `CLOCKS_PER_SEC` stays
  consistent.

  **Characters: what the T-pose is and is not.** Instrumented the whole animation path on device
  (`ANIPROBE` lines, all `RAN_MOBILE`-guarded). Measured, in order:

  - animation `.bin`s load and go valid (`valid=1 ani=52 up=57`), so the RCC extraction path works;
  - every animation binds to a skeleton bone (`nobone=0`), and ~3300 quaternion keys are applied a second;
  - the key lookup advances with the clock (`count=62 last=9760 global=5557 -> key=35`);
  - the bones themselves move — spine, neck, upper arm and thigh all differ from bind;
  - the matrices reaching the GPU differ per bone and per frame.

  So the animation system is **not** the fault. What is visibly wrong is narrower: the torso and legs
  pose correctly while the arms stay stretched outward and a glove sits detached near the hip — the
  signature of the *attachment* path (`ppBoneMatrixPtrs[0..2]` = frame local x parent x bone in
  `SetupBoneMatrixPointersOnMesh`), not of vertex blending. Skin conversion was ruled out by
  measurement too: `pruned 0`, `maxInfl 2` — no face ever needs more bones than the palette holds.

  (Also added: `MOBILE/tools/pngcrop.js`, a dependency-free PNG crop/zoom so a 120-pixel character can
  actually be looked at; the guarded probes are still in `SOURCE/` for the next session.)

- **2026-08-25** — **World-geometry batching: tried, measured, reverted.**

  The UI batcher cut 210 UI draws a frame to 44, so the obvious next step was the same trick
  for world geometry. Two shapes were built and instrumented:

  1. **Index-range merging** for `DrawIndexedPrimitive` — consecutive draws out of the same
     vertex/index buffer, texture, state and transform whose index ranges follow one another
     become one draw, with no vertex copying. Result over 300 frames: `tried=14400 merged=0`.
     Nothing merged, and no rejection reason fired either — a state change lands between
     every pair of draws, so each one starts a fresh batch.
  2. **Extending the vertex batcher to non-UI `DrawPrimitiveUP`** (world geometry uses the UP
     path heavily), keyed on texture, state and transform, skipping skinned draws. Frame time
     was unchanged: 89.4 ms against 90.2 ms without it.

  Both are out again. The reason they cannot help is structural: the engine emits one draw per
  texture per mesh, and consecutive draws genuinely differ in texture or in world matrix.
  Merging them would need a texture atlas or bindless textures — a different project, and one
  that changes what the client draws rather than how the shim submits it.

  **Draw-path census** (per frame, in a busy town): `DrawPrimitiveUP` 202, indexed UP 235,
  `DrawIndexedPrimitive` 48, `DrawPrimitive` 31 — about 516 client calls, 330 GL draws after
  UI batching.

  So the emulator number stays where it was (~90 ms a frame in a dense area, ~0.25 ms per draw
  call through LDPlayer's GL translation). The next honest step for performance is a
  measurement on the tablet, not more shim work.

  Unrelated but done today: `SOURCE/` is committed. It was already a git repo (60 commits) —
  the `.git` lives in `SOURCE/`, not at the repo root, which is why an earlier check said
  otherwise — but the port's changes were uncommitted. There is now a baseline commit plus a
  `.gitignore` for the 2 GB of `_Build` intermediates, linker maps and build logs.

- **2026-08-25** — **Missing objects were missing data; UI batching cut the frame in half.**

  **"Some objects did not load" was the test device, not the port.** The emulator had never
  received `data/effect` (4,296 files — every skill, muzzle flash and lamp glow), `data/help`,
  the sound tree, or any map beyond the three pushed by hand, so anything referencing them
  drew nothing and the log filled with `.egp`/`.wld` open failures. After pushing them the
  count of failed opens for effects, sounds and maps is **zero**.

  What remains in the log is 378 lines of `item ran option setting file load fail` and
  `QUEST load fail`. Those are not a port defect: the PC client's own error log
  (`CLIENT/Logs/ErrorLog/`) contains the same 378 lines against the same data.

  **`CreateDirectory` did not go through the path layer.** The client asked for
  `\Data\Map\RanMapZipTemp\`, and Android created a single directory whose *name*
  contained backslashes. `RanPath_MakeDir` now normalises separators, matches existing
  components case-insensitively and creates the parents.

  **UI batching.** The client emits one draw per glyph, per icon, per bar segment: 210 of the
  233 draws in a world frame were pre-transformed UI. Consecutive UI draws that share a
  texture and a state epoch are now merged into one triangle list, flushed whenever anything
  they depend on changes (texture, render/texture-stage/sampler state, render target, clear,
  a world draw, end of frame). UI draws per frame: **210 → 44**. Frame time in the same spot:
  **~70 ms → 26 ms**.

  Also removed a heap allocation that ran on every draw (the light block was compared through
  a `std::vector`; it is a fixed buffer now).

  **What the emulator can still tell us: nothing more.** With the per-draw timer on, frame
  time tracks draw count almost exactly — 9.5 ms at 60 draws, 26 ms at 100, 87-95 ms at ~300
  — i.e. ~0.25 ms *per draw call*, which is LDPlayer's GL translation, not work the shim does.
  A real device does not pay that, so the next performance decision needs a tablet number;
  the tablet dropped off Wi-Fi before it could be re-measured with these changes.

  (The frame-budget line's draw timing is compile-time now — `RAN_TIME_DRAWS` in
  `native/CMakeLists.txt`, off by default, since it costs two clock reads a draw.)

- **2026-08-25** — **It runs on the real tablet, and it is faster.**

  **Galaxy Tab S9 (SM-X710, Adreno 740, Android 16, native arm64).** Two things were wrong
  there that the emulator never showed:

  1. **Scoped storage.** Every read of `/sdcard/ran` failed with `EACCES` and the client quit
     before the first frame. `MANAGE_EXTERNAL_STORAGE` has to be granted —
     `adb shell appops set com.ran.native MANAGE_EXTERNAL_STORAGE allow` — or the data has to
     live in the app-private directory. Noted in `push-tablet.sh`, which pushes the ~5.3 GB
     the client needs (only the maps it can actually reach).
  2. **The driver lied about S3TC.** Adreno advertises `GL_EXT_texture_compression_s3tc` and
     then answers `GL_INVALID_OPERATION` to the DXT uploads; the foliage came out as flat
     black and white slabs. The upload now checks `glGetError` on the first compressed
     texture and falls back to CPU decoding for the rest of the run, so no driver allow-list
     is needed.

  **The flicker was the swap.** The client draws like a D3D9 app with `D3DSWAPEFFECT_COPY`:
  it clears only its viewport rectangles and leaves the rest of the frame standing. EGL makes
  no such promise — after `eglSwapBuffers` the new back buffer holds whatever the rotating
  buffer had two frames ago. The surface now asks for `EGL_BUFFER_PRESERVED`, and where a
  driver refuses, partial clears are promoted to full-screen ones.

  **Performance work, measured rather than guessed.** A frame-budget line (total ms, and how
  much of it is inside draw submission) said two thirds of a 77 ms world frame was the shim
  submitting ~440 draws — about 125 µs each, which is two orders of magnitude off. Four fixes,
  in the order they mattered:

  | Change | Why it was slow |
  |--------|-----------------|
  | The client's vertex/index buffers get real GL buffer objects, refilled on unlock | every draw re-uploaded its whole slice through a streaming VBO |
  | `DrawPrimitiveUP` streams through a ring buffer | `glBufferData` per draw respecifies the store, so the driver allocates each time |
  | GL state cache: program, VAO, buffers, texture, blend/depth/cull | all of it was re-sent per draw, and consecutive draws share nearly all of it |
  | Vertex layout cache: attribute pointers only when FVF/stride/base/buffer change | ~10 GL calls a draw that almost never differed |
  | Uniform cache (flat array) + transform-derived values (MVP, view-projection, camera position) computed per pass instead of per draw | a matrix inverse and three multiplies ran on every draw |

  **And the frame is drawn at the size it is designed for.** The client runs at a logical
  ~1280x720; the frame used to be rendered at panel resolution and stretched by the GL
  viewport, shading four times the pixels for no extra detail. The window now asks for a
  logical-size buffer and the display compositor does the scaling, which is free.

  **Where that landed (LDPlayer, 2560x1440 panel):** login screen 105 ms → 9.5 ms a frame
  (~10 fps → ~105); in the world ~110 ms → ~70 ms (~9 fps → ~14). The world is still bound by
  GL call count, which on an emulator crosses a translation layer per call — the number that
  matters is a real-device one, and the tablet has not been re-measured since these changes.

  **Open on the tablet:** Android shows an ANR ("RAN isn't responding") while a map loads,
  because the load runs on the same thread that services input. The game keeps rendering
  behind the dialog; the fix is to move the game loop off the activity's main thread.

- **2026-08-25** — **The game renders: world, ground and full HUD.** Screenshot
  `native/out/final_world.png` — school grounds, other players with their names, the
  quickslot bars, HP/MP/SP/EXP, minimap compass, chat with its Thai quest lines, menu bar.
  Character-select is equally complete (`native/out/verify_cs5.png`).

  Three bugs stood between the last entry and this one, and all three came from the same
  place: **triangle winding**.

  1. **The ground and the whole HUD were being face-culled.** Disabling culling proved it in
     one build: everything appeared at once. The shim mapped `D3DRS_CULLMODE` to a GL front
     face on the theory that D3D and GL window spaces have opposite handedness, so the front
     face is the winding D3D culls. Measurement disagrees with the theory in this client:
     the world ground declares `D3DCULL_CCW`, the character-select ground declares
     `D3DCULL_CW`, and **both** are only visible with GL front = `CW`. One of the two maps
     disagrees with its own declared mode, which the PC build never notices because terrain
     draws unlit, where a back face is indistinguishable. So either winding now culls the
     same side, and `D3DCULL_NONE` still means none — the distinction that carries meaning.
  2. **UI geometry must not be culled at all.** The in-game HUD and the outer GUI emit
     opposite windings for their 2D quads; whichever front face was chosen, one of them
     vanished. Pre-transformed draws now skip culling entirely, which is what a cull mode
     means for a screen-space quad anyway.
  3. **`Clear` ignored its rectangle list.** `CD3DViewport::BeginScene` clears only its own
     rect; the shim cleared the whole screen every time, so a viewport that cleared late wiped
     the frame drawn before it. Clears are now scissored per rect.

  **A regression this session caught before it shipped:** freeing a texture's CPU copy after
  upload (the fix for running out of memory on the first world map) also wiped the font
  atlas, because the client re-locks that surface to add every new glyph — most of the game's
  text went blank. The copy is now freed only for compressed textures, which are never
  written to in place; that keeps essentially all of the saving, since the map set is DXT.

  **Measured now:** ~430 draws a frame in the world, `glErr=0x0000`, no allocation tripwire
  hits, login → character-select → world runs start to finish with `native/login.sh`.

  **Open:** frame rate in the LDPlayer emulator is ~9 fps at 2560x1440 (unbatched GL state
  and an ARM-translating emulator both contribute — needs a real device number before any
  optimisation work), effect/shadow/glow passes are still not exercised, and the virtual
  joystick and audio remain.

- **2026-08-25** — **The client is in the world.** Character-select → Start now loads the map,
  and the game renders school grounds, buildings, other players and this character, with the
  GUI at a size a finger can hit. Screenshot `native/out/world10.png`.

  **The GUI is a mobile GUI now.** The client was drawing its PC-sized controls at the panel’s
  2560x1440, so everything was half the size a touch needs. It now runs at a logical 1280x720
  and the frame is stretched to the panel (`RanGL_UIScale`, chosen so the logical width lands
  near 1280): every control, glyph and hit target doubles at once, touches divide by the same
  factor, and the viewport scales with them. It also costs a quarter of the fragments.
  Before/after: `native/out/mip1.png` vs `native/out/scale2.png`.

  **Four LP64 record traps, all the same shape.** These maps and data files were written by a
  32-bit build, so any record holding a pointer or a `size_type` is narrower on disk than
  `sizeof()` says on arm64. Reading one whole desynchronises everything after it:

  | Record | On disk vs arm64 | Symptom |
  |--------|------------------|---------|
  | `HELPNODE_SIZE` (`std::list::size_type`) | 4 vs 8 bytes | entering the world aborted on `malloc(8389754676365913933)` — the ASCII of `"Movement"` read as a size |
  | `POINTEX` (leads with `POINTA*`) | 24 vs 32 | map effect list walked off the end of the file |
  | `DXMATERIAL_MULTITEX` (holds `LPDIRECT3DTEXTURE9`) | 332 vs 344 | same, 12 bytes per material |
  | (audit) | — | `tools/layout-probe/audit-serialized-structs.js` now sweeps SOURCE for the pattern; it reports clean after these fixes |

  Each fix reads the record field by field and is byte-identical on MSVC. The one that found
  them is `shim/win/alloc_guard.cpp`: a tripwire on any allocation over 512 MB that logs the
  size, the size as text, and a symbolised backtrace — that turned a bare `std::bad_alloc` into
  `SHELPNODE::LoadFile` in two minutes.

  **Also landed:**
  - `CreateAdditionalSwapChain` is real (a view onto the device back buffer). `CD3DViewport`
    gives up on the whole in-game viewport when it fails, and the stub returned NULL.
  - `SHGetSpecialFolderPath` returns the data root, so per-character `.gameopt` and the error
    log stop resolving to `/Logs/...` at the filesystem root; the app creates those folders.
  - JPEG textures decode (`shim/d3d/image_decode_android.cpp`): 35 map textures are JPEG,
    `black.jpg` among them, and the terrain that references one drew untextured. Uses
    `AImageDecoder` resolved with `dlopen`, so the API-24 floor stands.
  - A texture’s decoded CPU copy is released once it is on the GPU — it was being kept for the
    life of the process on top of the GL copy.

  **Still open in the world:** the ground/terrain is loaded (92 octree nodes, 36,932 vertices,
  measured) but does not appear, and the in-game HUD draws nothing visible although ~229 UI
  draws a frame are issued. Both are next.

  RESOLVED (2026-08-26): both render. Terrain and the full HUD are visible in
  every in-world screenshot from the window sweep.

- **2026-08-25** — **Map textures now filter like the PC client.** The shim uploaded only mip
  level 0, filtered bilinear and clamped every texture, so the ground shimmered and blurred at
  distance. It now uploads the whole chain the DDS ships (every level already reaches
  `RanTexture` through `LockRect`), synthesises mips for single-level uncompressed textures, and
  honours stage 0's `D3DSAMP_MINFILTER` / `MAGFILTER` / `MIPFILTER`, `ADDRESSU/V` and
  `MAXANISOTROPY` — including the WRAP default, which had been clamped. Screenshot
  `native/out/mip1.png`.

  Also fixed while comparing against the PC client: with `D3DTSS_ALPHAOP = D3DTOP_DISABLE` the
  fixed-function pipeline takes alpha from the lit material rather than 1.0 — that is how
  `DxPieceQuickSort2::RenderPickAlpha` fades the objects between camera and player. The shader
  returned 1.0. A correctness fix; it did not change this screenshot.

  **Still open on the map:** flat sky-coloured gaps where some building geometry should be,
  visible in `native/out/band2.png`. Not fog and not a missing texture — no loader errors and
  `glErr=0x0000` — so the next step is finding which pass drops those pieces.

- **2026-08-25** — **Characters render.** `D3DXLoadMeshHierarchyFromX*` was a stub, so every
  skinned `.x` failed to load, every `DxSkinPiece` ended up with a NULL mesh container
  (measured: `DxCharPart::Render` bailed on `!m_pmcMesh` for 26000 of 26000 calls) and no
  character could draw. Four pieces were needed, all in `shim/d3d/d3dx_hierarchy.cpp`
  unless noted:

  1. **The hierarchy loader** — walks the parsed `.x` tree, drives the client's own
     `ID3DXALLOCATEHIERARCHY` (`CreateFrame` / `CreateMeshContainer`), links frames
     child/sibling, plus real `D3DXFrameDestroy` and `D3DXFrameCalculateBoundingSphere`.
  2. **`ID3DXSkinInfo`**, including `ConvertToBlendedMesh`: faces are grouped so no group
     needs more than four bones, vertices are duplicated per group and carry their weights
     in that group's palette-slot order, the bone-combination table names the original
     material per group, and subset ids are group indices — which is what
     `DxSkinMesh9_NORMAL::DrawMeshContainer` draws with `DrawSubset(iAttrib)`.
  3. **Mesh fixes** (`d3dx_mesh.cpp`): `MeshNormals` carries its own index list and rarely
     matches the vertex count (a 176-vertex piece ships 528 normals), so corner normals are
     folded onto vertices; a triangulated polygon now inherits its parent face's material
     instead of consuming one attribute slot per polygon.
  4. **Fixed-function vertex blending in the GLES backend** — `D3DTS_WORLDMATRIX(0..3)` plus
     `D3DRS_VERTEXBLEND` reach the shader, which blends position and normal per vertex.
     Blending applies only to draws whose FVF actually carries weights: the client leaves
     `D3DRS_VERTEXBLEND` set after a character.

  **The bug that cost the most time was in the patch, not the design:** the script that was
  supposed to add the blend branch to the shader matched nothing and said nothing, so the
  uniforms, the attribute and the device plumbing all existed while the shader still had no
  blending. Skinned meshes drew rigidly with palette slot 0, which looks exactly like a
  skinning bug — the body (spine bones, near-identity rotations) looked right while the
  forearms and hands flew off. Patch scripts now fail loudly when a replacement misses.

  **Verified against independent ground truth**: `tools/rcc-extract/xmesh.js` reads
  `CLIENT/data/skin/s_m_bs.x` as 253 frames and four mesh containers — 176v/220f/26 bones,
  256/358/5, 264/342/12, 108/100/6. The loader on device reports exactly those numbers, no
  piece reports a missing mesh container, and the character draws correctly posed with
  clothing, hands and face. Screenshot `native/out/clean1.png`.

- **2026-08-25** — **Render targets are real, and the character-select map draws.**
  The map was never missing: `CViewFrameMeshComponent::SetRenderState` (the character
  portrait panel) does `SetRenderTarget(0, m_pTargetSurface)` followed by
  `Clear(TARGET|ZBUFFER, 0x00000000)`, and the shim recorded the target without switching
  GL away from the default framebuffer — so that off-screen pass's black clear wiped the
  frame the scene had just drawn, every frame. The login screen has no portrait panel,
  which is why only this stage looked broken.

  `SetRenderTarget` now binds a cached FBO per target texture (colour attachment = the
  parent `RanTexture`'s GL texture, plus a depth renderbuffer), and switching back to the
  back buffer restores framebuffer 0 and the window viewport. Two consequences had to be
  handled: a texture rendered into must never be re-uploaded from its stale CPU bits
  (`m_isRenderTarget`), and GL texture rows run bottom-up, so an off-screen pass mirrors
  Y against the screen pass — one `uFlipY` uniform, with the front face reversed to match.
  Screenshot `native/out/rt1.png`.

  Diagnostics added while chasing this (kept for now): armed per-draw dumps of world draws
  (`RanDraw`), UI draws (`RanUI`) and texture uploads (`RanUp`), plus
  `native/login.sh`, which drives a cold start to character-select in one command.

- **2026-08-25** — **Login works end to end.** The client connects, authenticates, and reaches
  the character-select page with the account’s character listed. Screenshot
  `native/out/shot_login5.png`.

  Four bugs, in the order they surfaced:

  1. **Selecting a server aborted the process.** `CNetClient::CloseConnect` waits on its
     network thread and then closes the handle — join followed by detach. A `pthread_t` is
     valid exactly once, and bionic ABORTS on the second use. The shim now records whether a
     thread has been consumed.
  2. **The LZO wrapper wrote 8 bytes into a 4-byte int.** `MinLzo.cpp` casts `&int` to
     `lzo_uint*`; `lzo_uint` is pointer-sized, so the first compressed packet from the server
     smashed the stack canary (`__stack_chk_fail` in `CRcvMsgBuffer::getMsg`). Fixed with a
     properly typed local — identical behaviour on Win32, where the two types are the same
     width.
  3. **`printf("%s", someCString)` printed the object, not the text.** MSVC passes a class
     through varargs BY VALUE, which is what makes that MFC idiom work; the Itanium ABI clang
     uses passes it by invisible reference. The client’s version-file path arrived at
     `fopen` as `"/sdcard/ran/cVer.bin"` — libc++’s short-string header printed as text —
     so the client declared its own install corrupt and refused to log in. No layout trick
     fixes this: **248 call sites** now pass `GetString()` / `c_str()`, found and verified by
     `-Wnon-pod-varargs`, which is now on permanently. All of them compile unchanged on MSVC.
  4. The same investigation added the missing `RanOpen` failure detail (resolved path, errno,
     and a hex dump of the bytes) — that hex dump is what exposed bug 3.

  **Data on device now** (`push-data.sh`): map, piece, object, skeleton, skinobject, skin,
  animation and all texture trees — about 4 GB.

  **Known next problem:** the character model itself is not yet on screen at
  character-select (the map behind it now draws — see the entry above this one).

- **2026-08-24 (session 3, part 7)** — **The client reaches the login server.** The
  server-select page now lists the server the login server replies with, instead of showing
  a connection-error dialog.

  Two shim gaps, both in the same path:

  1. **A non-blocking connect reported the wrong error.** POSIX sets `EINPROGRESS`; Windows
     reports `WSAEWOULDBLOCK` for exactly the same situation, and `CNetClient::Connect`
     treats anything except `WSAEWOULDBLOCK` as a hard failure. The shim was translating the
     errno literally (10036 instead of 10035), so every attempt failed before a single byte
     was sent - visible in the log as `connect error:Code 10036`.
  2. **The socket-event layer was a no-op.** `WSAEventSelect`, `WSAEnumNetworkEvents` and
     `WaitForMultipleObjects` were stubs, and the last one only ever looked at the FIRST
     handle. The client’s network thread waits on three at once (kill, work, socket) and
     dispatches `FD_READ`/`FD_WRITE`/`FD_CLOSE`, so nothing would ever have been received
     even if the connect had succeeded. `shim/win/net_events.cpp` implements them for real:
     a socket-to-event binding table, `poll(2)` for readiness, `SO_ERROR` for the connect
     result, and a wait that returns the INDEX of the handle that fired. `FD_WRITE` is armed
     rather than level-reported, matching Windows - otherwise the network thread spins.

  The login address is read from the shipped (encrypted) `param.ini` and is now logged at
  boot, so a wrong address is visible immediately rather than looking like a network fault.

  **Next, and now the blocker for an actual login:** the soft keyboard. The ID and password
  fields cannot be typed into yet.

- **2026-08-24 (session 3, part 6)** — **Touch input reaches the UI.**

  Rather than divert the engine’s input code, the DirectInput device it asks for is now
  provided (`shim/platform/dinput_mobile.cpp`) and Android events are posted into it. The
  whole engine-side path - buffered `GetDeviceData`, key repeat, click timing, the outer UI’s
  hit testing - runs exactly as it does on PC. Touch maps to the left mouse button, moving
  the pointer before the press because the UI hit-tests on the button event and a touch
  delivers both at once. Android key codes map to DIK scan codes for the keys a login screen
  needs.

  One thing that was not obvious from the header: **the absolute pointer position does not
  come from DirectInput.** `DxInputDevice::UpdateMouseState` only integrates `DIMOFS_X/Y` when
  it owns a *device* mouse; otherwise it reads `GetCursorPos`. Feeding only the DirectInput
  queue produced button events at position (0,0) - the UI saw clicks that hit nothing. The
  shim’s `GetCursorPos` now reports the touch position.

  **Verified on device:** tapping the connection-error dialog’s button changed the dialog to
  the next state (“exit the game?”) and made the client retry its login connection - the
  click reached the real UI and ran the real handler. The app was stopped immediately
  afterwards; the standing rule about not repeatedly connecting to the live server applies to
  this test too, so it was run once.

  **Next:** the soft keyboard, so the ID and password fields can actually be typed into
  (`DXInputString` / `CIMEEdit` is the engine-side API), and a virtual joystick for the game
  stage. Character models still need a session, so they wait behind login.

- **2026-08-24 (session 3, part 5)** — **Lighting, fog, and the `.x` reader.**

  *Lighting and fog.* The fixed-function light, material and fog state was stored by the
  device and then dropped on the floor. The GLES backend now evaluates it: directional and
  point lights (range + attenuation), global ambient, material ambient/diffuse/emissive, and
  linear/EXP/EXP2 fog, all per pixel. Lighting is done in world space, which is where the
  engine’s lights live, so the world matrix and camera position go to the shader too. The
  login scene is now shaded and has atmospheric depth instead of being flat-lit.

  *The `.x` reader.* Skeletons, skins and map objects are all DirectX `.x` files, in three
  flavours that ship side by side: `bin` (binary token stream), `bzip` (the same stream
  MSZip-compressed) and `txt`. All three are read now:
  - `shim/d3d/xfile_parse.cpp` — the container: header, MSZip (each block deflated against
    the previous block’s output, fed in as a prepended stored block), binary tokeniser,
    text parser, and the standard D3DRM template GUIDs. Members are packed in stream order,
    which is the layout D3DX hands back from `Lock()` and what the engine casts.
  - `shim/d3d/xfile_com.cpp` — both enumeration APIs over one tree: `ID3DXFile*` (used by
    `DxFrameMesh`) and the legacy `IDirectXFile*` (used by `DxBoneCollector`).
  - `shim/d3d/d3dx_mesh.cpp` — `ID3DXMesh` with real vertex/index/attribute buffers, plus
    `D3DXLoadMeshFromXof` / `FromXInMemory` / `FromX{A,W}`, `D3DXCreateMeshFVF`,
    `D3DXCreateBuffer` and `D3DXDeclaratorFromFVF`. Faces with more than three corners are
    triangulated as a fan; MeshNormals / MeshTextureCoords / MeshMaterialList decide the FVF.

  **Verified against independent ground truth**, not just “it didn’t crash”: parsed one file of
  each flavour on the device and compared the object count with the JS reader in
  `tools/rcc-extract/xfile.js`. `b_13.x` (bin): 106 objects both sides. `b_2017_victors_wings.x`
  (MSZip): 10 both sides. `boxbox.x` (txt) parses to a Frame root. The self-test was removed
  afterwards.

  One trap worth recording: **a duplicate definition in a static library is not a link error.**
  The old stubs in `d3dx_loaders.cpp` and the new real loaders both defined the same symbols;
  the linker kept whichever object it pulled first, so the new code silently never ran. The
  stubs had to be deleted, not just superseded.

  **Next:** character models end to end (`DxBoneCollector` + `DxSkinMesh`) on the
  character-select page, which is what will exercise the mesh path for real.

- **2026-08-24 (session 3, part 4)** — **The login scene is complete: ground, grass, road, sky.**
  Screenshot `native/out/shot24.png`. Two more backend gaps, both found by A/B on the device
  rather than by reading code:

  1. **Texture stage 0’s combiner was ignored.** The shader always multiplied texture by
     vertex colour; the terrain selects the TEXTURE alone (`D3DTOP_SELECTARG1`) and carries
     black vertex colours, so the ground drew black. The stage’s colour/alpha op and both
     args are now uniforms, covering SELECTARG1/2, MODULATE/2X/4X, ADD and DISABLE, with
     `D3DTA_COMPLEMENT` / `D3DTA_ALPHAREPLICATE`.
  2. **The world path’s front face was inverted.** Disabling culling entirely brought the
     whole scene back, which is what identified it: solid ground was being culled while the
     two-sided foliage survived, so it looked like a missing ground mesh. Both paths use the
     same front face - the UI’s Y flip and the world path’s lack of one cancel against
     GL’s bottom-left window origin.

  Measured: `draws=70056 (ui=30756 textured=70056)`, 36.2M verts per 300 frames,
  `glErr=0x0000`, 60 fps. Diagnostics used to find these (per-mesh extent census, terrain
  node load counts, blend/cull A/B switches) were removed afterwards.

  **Next:** lighting and fog are still not applied (the scene is flat-lit), and character
  models need the `.X` mesh + animation loaders.

- **2026-08-24 (session 3, part 3)** — **The 3D login scene renders.** Screenshot
  `native/out/shot20.png`: the login map's tower, trees, buildings and foliage behind the
  server-select UI, at 60 fps, `glErr=0x0000`.

  Five bugs, found by measuring rather than guessing — each one hid the entire scene on its
  own, so they could only be found one at a time:

  1. **`D3DX_ALIGN16` was silently dropped.** The DX SDK applies `__declspec(align(16))` to a
     *typedef* (`D3DXMATRIXA16`); MSVC honours that, clang ignores it. `sizeof(SMatrixKey)`
     came out 68 instead of 80, so every animation-key array in a `.wld` read at the wrong
     stride — a desync that ended in a null-name `strcmp` crash. Fixed with the
     `__attribute__((aligned(16)))` spelling under `RAN_MOBILE`. (No packet struct uses
     `D3DXMATRIXA16`, so the layout gate is unaffected — re-checked.)
  2. **The frame loop was missing two thirds of itself.** `CGameClient2Wnd::FrameMove` calls
     `DxGlobalStage`, `DxResponseMan` **and** `DxViewPort`; the mobile app called only the
     first. Without `DxViewPort::FrameMove` the camera's clip volume was never computed, and
     since every renderer culls against it, **the whole world was discarded before drawing**.
     `Render()` now mirrors the PC one as well (light/camera shader constants, identity world
     matrix, MODULATE stage state).
  3. **`LOADINGDATALIST` is `std::list<DWORD>` holding pointers.** Fine on Win32, truncated on
     LP64 — the static-mesh loader thread dereferenced a half-pointer the moment culling
     started letting work through. Pointer-sized under `RAN_MOBILE`.
  4. **The MVP was transposed.** D3D is row-vector and stores rows contiguously; GL is
     column-vector and reads columns contiguously — the same bytes. Transposing applied the
     convention change twice and fed the shader the translation row where the projection
     column belongs: the scene drew as a fan of stretched triangles with `clip.w` in the
     hundreds of thousands. Copying instead of transposing gives `clip.w ≈ 609` for a vertex
     ~600 units away, which is what the camera says it should be.
  5. **The world path must not flip Y.** Only the UI path (screen pixels, Y down) needs the
     flip; flipping both drew the scene upside down. Because the two paths now disagree on
     winding, the front face is chosen per draw.

  Also fixed: `SetStreamSource`'s `OffsetInBytes` was ignored (the engine packs several
  meshes into one buffer and distinguishes them by it), and indexed draws used one number for
  both the index count and the vertex-buffer upload size — they are unrelated.

  Data: `push-data.sh` now also pushes `textures/` and the login maps. The engine-pack mode
  follows what is actually on the device — `Map.rcc` present means zip mode, otherwise loose
  files (a 548 MB pack plus a per-load scratch copy is not worth it on a phone).

  Diagnostics kept (they earn their place): engine `ToLogFile` mirrored to logcat (`RanEngine`),
  every failed file open (`RanOpen`), map load results (`RanLand`), static-mesh counts
  (`RanMesh`), per-interval draw census (`RanGL`). The one-shot traces used to find the above
  (cull decisions, `.wld` section offsets, per-draw dumps) were removed.

  **Next:** the terrain ground surface is still missing, and the scene has no lighting or fog
  yet. After that: character models, which need the `.X` mesh and animation loaders.

- **2026-08-24 (session 3, later)** — **Text works: the server-select page renders with real
  Thai.** Screenshot `native/out/shot12.png` shows `< เลือกเซิร์ฟเวอร์ >`, the connection-error
  modal with its ตกลง / ยกเลิก / ออก buttons, and the Latin copyright footer — all drawn by the
  engine's own font layer.

  The client draws text two ways and both are now real:
  - **ID3DXFont** (`shim/d3d/d3dx_font.cpp`) — the path `CD3DFontX` uses. Real
    `DrawTextW`/`DrawTextA` with `DT_CALCRECT` measurement, a 1024x1024 glyph atlas filled on
    demand, drawn as pre-transformed quads through the same device path as the UI. Also
    `ID3DXSprite`, `D3DXCreateFontIndirect{A,W}`, `D3DXCreateFontA`, `D3DXCreateSprite`.
  - **GDI** (`shim/win/gdi_text.cpp`) — the path `CD3DFont`/`CTextTexture` use:
    `CreateCompatibleDC`, `CreateDIBSection`, `CreateFont`, `SelectObject`, `SetTextColor`,
    `SetBkMode`, `GetTextExtentPoint32`, `ExtTextOut`, `GetDeviceCaps`.

  Both sit on `shim/win/ttf_raster.cpp`, a dependency-free TrueType reader and rasteriser
  written for this (cmap 0/4/6/12, simple + composite glyphs, quadratic flattening, nonzero
  scanline fill with 4x vertical supersampling and analytic horizontal coverage, synthetic
  bold/italic). The NDK ships no font library; glyphs come from the device's own
  `/system/fonts`.

  Three findings, each of which produced a *specific* wrong picture:
  1. **Every glyph was .notdef.** The device's Thai font (`NotoSansThai-Regular.ttf`) is
     21 KB and Thai-only — it has no Latin at all. Added a second face
     (`DroidSans.ttf`) consulted whenever the primary returns glyph 0.
  2. **CP_ACP was taken literally.** The engine passes codepage 0 to
     `MultiByteToWideChar` in several places. On the Thai Windows install it is built for,
     0 *is* 874; treating it as Latin-1 turned every Thai string into boxes. The shim now
     resolves 0/1 to the codepage `CHARSET::SetCodePage` last set.
  3. **The shipped text is UTF-8, not the legacy codepage the engine names.** Converting it
     as CP874 produced real Thai glyphs in the wrong order — readable as nonsense. The
     conversion now detects valid UTF-8 and decodes it as such, falling back to the codepage
     table otherwise. That is what turned the soup into `< เลือกเซิร์ฟเวอร์ >`.
     Also fixed `IsDBCSLeadByteEx`, which claimed lead bytes for single-byte codepages —
     under CP874 it made every Thai character swallow the next one.

  The client reaches the login server, fails to connect (expected — nothing was pointed at
  the live server) and shows the engine's own error modal. The app was force-stopped rather
  than left retrying, per the standing rule about the live server.

  **Next:** the 3D login scene. `DxLobyStage` loads `log_in.wld` and draws it before the UI;
  that needs the `.wld` terrain path plus the `.X` mesh/animation loaders
  (`D3DXFileCreate`, `D3DXLoadMeshFromXof`, `D3DXLoadMeshHierarchyFromX*`), which are still
  the stubs listed in the link report.

- **2026-08-24 (session 3)** — **Phase 3 first pixels: the login screen renders.** Built the
  GLES3 backend and wired the D3D9 shim to it end to end.

  What was added:
  - `shim/gl/gl_context.cpp` — EGL/GLES3 context on the frame-loop thread (ES3, depth24 +
    stencil8, 16-bit fallback, vsync).
  - `shim/gl/gl_render.cpp` — the fixed-function translation: one uber shader covering the
    pre-transformed (XYZRHW) UI path and the world path, D3D render state → GL state
    (blend / depth / cull / alpha test), FVF walking → attribute pointers, primitive-type
    mapping, texture upload including **DXT1/3/5 — uploaded natively when the GPU exposes
    S3TC, decoded on the CPU when it does not** (the test device has no S3TC).
  - `shim/d3d/image_decode.cpp` — real DDS / TGA / BMP / PNG decoding. DXT data is kept
    compressed on purpose: `TextureManager` keys its alpha handling off the created
    texture's format, so expanding it here would change how the game looks.
  - `shim/d3d/d3dx_loaders.cpp` — `D3DXCreateTextureFromFile{,Ex}{A,W}`,
    `...FromFileInMemory{,Ex}` and `D3DXGetImageInfoFrom*` are now real.
  - `RanDevice::Clear/Present/SetViewport` and all four draw entry points now reach GL.

  Four bugs found by running it, three of them invisible on Windows:
  1. **`CFileFind` never resolved its directory.** It called `opendir` on the raw Win32
     path, so `TextureManager`'s recursive scan of `\Textures` indexed **0 files** and
     every texture lookup silently missed — a blank screen with no error anywhere.
  2. **`"*.*"` was matched literally.** Win32 treats it as *everything*, including names
     with no dot — which is how the engine enumerates SUBDIRECTORIES. Matching it literally
     stopped every recursive scan at the top level. Fixing both took the texture tree from
     0 to **594 files**, and the frame from 0 textured draws to ~4.5 per frame.
  3. **`CFileFind::FindNextFile` had the wrong MFC semantics.** MFC returns FALSE on the
     LAST entry while that entry is still current; a readdir-as-you-go version returned
     TRUE for it and then a blank FALSE, which inserted an empty filename into the tree.
  4. **Back-face culling hid the entire UI.** Two inversions compose between D3D and GL —
     D3D names the winding that is *culled* while GL names the winding that is *front*, and
     flipping Y for GL's bottom-left origin reverses every triangle's apparent winding.
     Getting only one of them right culls everything that is drawn.

  Also fixed: vertex colours were read as RGBA when D3DCOLOR is B,G,R,A in memory (shader
  now swizzles), and `RanGLR_ApplyState` set uniforms with no program bound, so the
  alpha-test state was silently dropped.

  **Measured on device:** `Textures: 594 files` · `draws=1356 (ui=1356 textured=1356)` per
  300 frames · `glErr=0x0000` · 60 fps · screenshot `native/out/shot4.png` shows the RAN
  Online logo, the login window and the server-select panel. Data push now includes
  `CLIENT/textures` (`push-data.sh` gained a `push_tex` stage — textures live at
  `<root>/Textures`, not under `data/`).

  **Next in phase 3:** text (the font layer reaches `D3DXCreateSprite` and the GDI text
  path), then the 3D login scene — `.wld` terrain plus the `.X` mesh/animation loaders,
  which are still the stubs listed in the link report.

- **2026-08-24** — Decided to abandon the Unity rewrite and compile the PC client instead.
  Audited `SOURCE/` (renderer profile, MFC surface, LP64 hazards). Built `MOBILE/native/`:
  CMake + NDK harness, Win32/MFC shim, D3D9 headers reused verbatim. Got all five libraries
  to compile and link. Verified packet layout against real MSVC x86 and fixed the `CTime`
  alignment bug. Cleaned MOBILE from ~210 GB to ~13 GB (old APKs, logs, screenshots, Unity
  Library cache). Reorganised documentation into the map above.
- **2026-08-24 (session 2)** — Phase 2 built. Wrote the D3D9 shim (`Direct3DCreate9`, device,
  textures, VB/IB, state blocks — 325 COM methods, generated from the SDK header by
  `shim/d3d/gen-d3d9-impl.js` so no vtable can drift), all 33 out-of-line D3DX math
  functions, a silent DirectSound backend that keeps the real sound *logic* running, and
  inert stubs for IME/web/DirectInput. Re-enabled the real `CD3DApplication` framework and
  the font layer. Wrote `RanMobileApp` (mirrors `CGameClient2Wnd`) and `android_main`.
  **`libran.so` links with zero undefined symbols**, and `out/ran-phase2.apk` (158.5 MB,
  signed) is ready to install — see `native/PHASE2-BOOT.md`. Also fixed two more LP64 bugs
  found on the way: `long` vs `LONG` in the sound volume/pan calls, and `GLCLUB::GetMember`
  declared `inline` in a .cpp (MSVC emitted it, clang did not).
- **2026-08-24 (session 2, later)** — **PHASE 2 DONE: the real PC client boots and runs on
  Android.** Verified on the LDPlayer emulator (x86_64 — so an x86_64 build was added
  alongside arm64; the packet-layout gate was re-run for it and passes identically).
  Clean boot: RANPARAM read from the shipped `param.ini` (lang=5 Thai), **Gui.rcc indexed**
  (the encrypted RCC archives open and read on Android), game text parsed, D3D device
  created at 2560x1440, `OneTimeSceneInit` → `InitDeviceObjects` → `CreateObjects` →
  frame loop running at ~3.5 draws/frame with 13 textures (17.6 MB), 31 VBs, 16 IBs.
  Only one unimplemented D3DX entry point is reached at this stage: `D3DXCreateSprite`.

  Six real bugs found and fixed by running it — every one invisible on Windows:
  1. **`ran_fopen` recursed into itself.** The force-included `StdAfx.h` pulls in
     `windows.h` before the file's own guard line, so the `#define fopen` applied inside
     the redirect itself → stack overflow. Fixed with `#undef fopen` at the definition.
  2. **Windows paths.** The client's `SUBPATH` tables use `\Data\GUI\` and rely on a
     case-insensitive filesystem; the shipped tree is `data/gui`. Added
     `shim/win/path_resolve.cpp`: separator fix plus per-component case-insensitive
     resolution with a cache, wired into every file entry point.
  3. **minizip bypassed it.** The `.rcc` archives are opened by zlib's C code, which never
     includes `windows.h` — the resolver is now force-included into that target too. This
     was the difference between "GUI will be empty" and `Gui.rcc indexed`.
  4. **Null `CWnd`.** `DxResponseMan::OneTimeSceneInit` dereferences `pWndApp->m_hWnd`;
     passing NULL crashed at `0x8`. It now gets a real (inert) shim window object.
  5. **Device enumeration.** Advertising a fake 1280x720 for four pixel formats produced a
     mode list nothing matched, leaving `dwCurrentMode` pointing at uninitialised memory.
     The shim now advertises exactly one mode — the real surface, X8R8G8B8 — and the app
     asks for that size rather than RANPARAM's desktop resolution.
  6. **`CreateObjects` override dropped the base class's work.** Mirroring only the font
     half of `CGameClient2Wnd::CreateObjects` meant `InitDeviceObjects`/
     `RestoreDeviceObjects` never ran, so `DxGlobalStage` was uninitialised and the first
     frame null-derefed.

  Also hardened the generator: methods returning an interface through an out-parameter now
  null it and return failure instead of reporting success with the caller's pointer
  untouched — that pattern put the crash far from its cause twice.

- **2026-08-26 — the white sheet over the character, and what it really was.**
  The character at character select was drawn with a large white polygon across its
  torso, and the natural reading — that the skinning was wrong — was wrong. Chasing it
  by measurement rather than by patching produced four fixes, three of them general.

  **1. `D3DRS_VERTEXBLEND` is an enum, not a count.** An earlier optimisation raised the
  bone palette to five matrices, which made the engine set `D3DRS_VERTEXBLEND = 4` — a
  value D3D9 does not define — and the shim mapped anything outside `D3DVBF_1..3WEIGHTS`
  to "no blending", so those groups were drawn rigidly on their first bone. The palette is
  back to four (`kMaxPalette`, `MaxVertexBlendMatrices`, and the shader's `uWorldM[4]`),
  and an out-of-range mode is now logged rather than silently ignored. Verified against
  `DxSkinMesh9_NORMAL::DrawMeshContainer`, which computes `NumBlend` as the index of the
  last valid `BoneId` — so four matrices is the ceiling.

  **2. The draw batcher deferred draws past a state restore.** This was the white sheet.
  `DxSimpleMesh::Render` handles a subset whose texture failed to load by switching stage
  0 to `D3DTOP_SELECTARG2`, drawing, and restoring `D3DTOP_MODULATE`. The shim's UI batch
  flushed *after* that restore, so the subset was drawn modulating a texture that was
  never bound — which samples opaque white. `SetRenderState`, `SetTextureStageState`,
  `SetSamplerState`, `SetTexture` and `SetTransform` now end an open batch when the value
  actually changes. Draw count is unchanged (24168 per 5s window before and after), because
  the batch would have been flushed on the next draw anyway — only earlier.

  **3. `D3DTA_TFACTOR` was not implemented.** `argValue` in the fragment shader treated
  every argument that was not `D3DTA_TEXTURE` as the diffuse colour, so
  `D3DRS_TEXTUREFACTOR` — which the engine reaches for exactly when there is no texture to
  sample, and which `DxShadowMap` uses for the shadow circle — silently became the vertex
  colour. It is now a real uniform, defaulting to opaque white as D3D does.

  **4. Zeroed caps switched off every off-screen texture.** `DxSurfaceTex::InitDeviceObjects`
  clears `m_bDeviceEnable` unless `D3DDEVCAPS2_CAN_STRETCHRECT_FROM_TEXTURES` and
  `D3DPTFILTERCAPS_MINFPOINT` are both reported, and `RestoreDeviceObjects` then releases
  `m_pWaveTex` — taking refraction, glow, reflection and the post-process chain with it.
  `StretchRect` was a no-op returning `D3D_OK`; it is now a real `glBlitFramebuffer`
  between render-target FBOs (`RanGLR_BlitTexture`), and the two caps are reported.
  Measured after: `surface textures: device=1 option=1 wave=0x7dce7834e520`.

  Two smaller ones alongside: a render-target texture sampled before anything has been
  drawn into it now reads as transparent black rather than as no texture at all, and
  texture load failures name the file (path, byte count and magic) instead of sharing one
  anonymous "image decode failed" line.

  **What the measurement chain was**, since the wrong reading cost most of the time: a
  per-draw dump behind `/sdcard/ran/drawdump` reporting each draw's clip-space box, walked
  through *this draw's own indices* (walking the whole vertex buffer mixed every bone group
  together and produced the same four centroids for every draw — that artefact is what made
  the hand piece look exploded when it was correct); the engine naming each skinned draw
  through `RanGLR_DiagTag`; and `RanDiag_Backtrace`, which reduces return addresses to
  library offsets that `llvm-symbolizer` resolves against the unstripped `libran.so`. The
  backtrace is what turned "some quad" into `DxEffectBlurSys::RenderBlur` in one step.

  Verified: character select draws the full character — shirt, arms, hands, face, trousers,
  shoes — with the aura effect as ribbons rather than a slab; the world loads with map,
  water and fence textures correct and no skin piece missing a texture. `decomp_affine`
  was cleared by an off-device round-trip test (200000 rotations, worst error 0.0000), so
  the matrix decomposition is not a suspect.

  **Still open:** cube textures are unimplemented (`D3DXCreateCubeTextureFromFileExA`), so
  RESOLVED later the same day — cube textures are implemented; see the hair
  shading entry below.
  **(original note)**
  `LobbyCube.dds` never loads and character specular reflections are absent. This costs a
  reflection, not a white surface — nothing draws wrong because of it.

- **2026-08-26 (later) — hair, weapons and mob parts were being dropped by a four-byte
  record.** Reported as "some object did not load, some skin has no texture, some items
  don't show, hair doesn't show". All four were one bug.

  `DXMATERIAL_CHAR_EFF` contains an `LPDIRECT3DTEXTURE9`. The record the 32-bit client
  wrote is 596 bytes; this build compiles 600. Every
  `SFile.ReadBuffer( m_pMaterials, sizeof(DXMATERIAL_CHAR_EFF)*n )` in the character-effect
  loaders therefore consumed four bytes per material of whatever followed. Nothing
  complained about textures. Instead the effect loop in `DxSkinPiece::LoadPiece_02xx` read
  a *file version* where a type id belonged — `CreateEffInstance TypeID:512` is `0x0200`,
  `258` is `0x0102` — got NULL back, and dropped **the whole piece**. Hair, the sword and
  parts of some mobs simply never appeared.

  Fixed with `RanReadMaterialCharEff` / `RanReadMaterialCharEff100` in `DxMaterial.cpp`,
  which read the on-disk field layout one field at a time and discard the four-byte
  pointer. All 25 call sites across seven `DxEffChar*.cpp` files are `#ifdef RAN_MOBILE`
  guarded, so MSVC still compiles the original line.

  Found by logging each effect's TypeID **with the stream offset it was read at**: the
  effect *count* was sane, which ruled out everything before the loop and pointed at one
  effect's `LoadFile` consuming the wrong number of bytes. Measured after: zero piece
  failures, zero map-object failures, hair and weapon visible at character select and in
  world (`native/out/charselQ.png`, `native/out/worldA.png`).

- **2026-08-26 — `D3DRS_DEPTHBIAS` implemented.** `SMeshContainer::SetMaterial` packs a
  float into the state DWORD to lift decals, trims and effect layers off the surface they
  share; the shim ignored it, so those layers z-fought — "some skin, some texture is
  overlapping each other". Now mapped to `glPolygonOffset`, scaled by the depth buffer's
  resolution (`RanGL_DepthBits()`, 24 or 16 on the fallback config) because GL expresses
  the offset in smallest-resolvable-depth units while D3D adds it to depth directly. The
  sign carries through unchanged — the engine passes a negative bias to pull a layer
  toward the camera, and negative units do the same in GL.

  **Still open:** the loading screen never appears because `NLOADINGTHREAD::StartThreadLOAD`
  RESOLVED — the loading thread now hands the EGL context over; see the
  loading-screen entry below.
  **(original note)**
  renders it from a background thread, and the EGL context is current on the main thread
  only, so its GL calls go nowhere. Needs either a shared context made current on that
  thread or the loading frames pumped from the main thread between map-load steps.

- **2026-08-26 — item icons were a missing texture format.** Reported as "the icon of items
  did not show". They were drawing as solid black squares in every inventory and equipment
  slot.

  `RanGLR_UploadTextureLevel` had no case for `D3DFMT_R8G8B8`. Those fell into a `default:`
  that returned without uploading, on the reasoning that leaving the texture untouched beat
  uploading garbage. That reasoning is wrong on GLES: a texture object with no image data is
  incomplete, and an incomplete texture samples as **opaque black**. So the icon atlases —
  `textures/gui/School_Uniform.dds`, `Public_Weapon.dds`, `commercialset.dds`,
  `costume_GUI.dds`, all 512x256 24-bit uncompressed — produced perfect black rectangles
  while nothing failed anywhere: the DDS decoded, the texture object existed, it had a GL
  name, and `glErr` stayed `0x0000`.

  Added `D3DFMT_R8G8B8` (B,G,R in memory, expanded to RGBA) plus `A8B8G8R8`/`X8B8G8R8`, and
  made the `default:` name the format once instead of staying silent — which immediately
  surfaced a second unhandled format (`D3DFMT_A8B8G8R8`, a 128x128 texture) that nobody had
  noticed. Measured after: zero unhandled formats, icons visible for the uniform, trousers,
  shoes, weapon and bag contents (`native/out/inv9.png`).

  **How it was found, since three earlier guesses were wrong:** by following the PC path
  instead of the symptom. `CItemImage::SetItem` takes the atlas name from
  `pItemData->GetInventoryFile()` and the cell from `sBasicOp.sICONID`; logging both showed
  the names were correct and the cells in range, and `RanD3D_NoteTexturePath` (new — it
  records which file a GL texture id came from, through both the file and the RCC-memory
  loaders) showed the atlases loading fine. That left only the upload, and reading the DDS
  headers directly showed 24-bit uncompressed — a format the switch did not have.

  Also landed while chasing this: `SetDepthStencilSurface(NULL)` now really disables depth
  testing. The engine uses it to render into off-screen textures that have no depth of their
  own (`DxMeshTexMan`, and the glow and burn passes in `DxSurfaceTex`), while every
  render-target FBO in the shim keeps a depth attachment that nothing clears — so those
  passes were being depth-rejected. It was not what caused the black icons, but it is a real
  gap and it is fixed.

- **2026-08-26 — two more texture faults, both silent.**

  **24-bit and ABGR uploads were missing.** `RanGLR_UploadTextureLevel` had no case for
  `D3DFMT_R8G8B8`; those fell into a `default:` that uploaded nothing, and GLES samples a
  texture object with no image as opaque black. That was every item icon (the atlases are
  512x256 24-bit uncompressed), and it also cost the lamp glows, the buff row and the
  quick-slot icons. `D3DFMT_A8B8G8R8` / `X8B8G8R8` were missing too — found only because the
  `default:` now names the format once instead of staying quiet.

  **Textures uploaded from the loading thread were lost permanently.** The client renders its
  loading screen from a background thread, which has no EGL context: `glGenTextures` there
  produces nothing, and clearing the dirty flag made the loss permanent — the texture stayed
  blank even once the render thread reached it. Six textures were stuck at GL id 0, all of
  them loading-screen art (`loading_054.dds`, `ld_top`, `ld_back`, `ld_under`) plus
  `hinticon.dds` and `mapnameback.dds`. `RanGLR_OnRenderThread()` now guards `GlTexture()`,
  which leaves the texture dirty so the render thread uploads it properly. Measured after:
  no texture is left at GL id 0.

  Note this does **not** make the loading screen appear — that thread's draw calls still go
  nowhere. It makes its textures work for whoever draws them next.

  **Effects:** no defect measured in the starting area. The refraction pass reports
  `skipped, nothing asked to refract`, which is correct — `SetWave*` is what raises the flag
  and there is no water or refracting effect in view there. Ambient effects (lamp glows,
  the character aura, weapon trails) render. A concrete effect that looks wrong is needed to
  take this further.

- **2026-08-26 — hair shading and the white item on other players.**

  **Hair shading is a known gap with a name now.** The hair piece loads two character
  effects: `EMEFFCHAR_USERCOLOR` (25) and `EMEFFCHAR_SPECULAR2` (19). The sheen comes from
  `DxEffCharSpecular2::Render_Cube`, which binds a cube map to **texture stage 1** and
  redraws the mesh as a second pass with `pmtrlSpecular` supplying the effect texture. The
  shim binds one texture at a time and has no cube textures, so that pass runs without its
  cube map and the hair comes out flat. This is the first user-visible consequence of the
  stage-1 and cube-texture gaps already on the remaining-work list — they are what the hair
  needs, in that order.

  **The white item on other players is narrowed, not solved.** It is a skinned draw
  (`fvf=0x11A` = `XYZB3|NORMAL|TEX1`) with no texture bound, from the piece `blackcatyb_m`.
  Measured: the `.x` material has no `TextureFilename` node at all (the X loader reports it,
  and reports nothing else in the scene), and the `.cps` piece material's override string is
  empty too — so neither of the two sources `SMeshContainer::SetMaterial` normally uses has
  anything. The third source is `pmtrlSpecular[n].pEffTex`, which comes from
  `DXMATERIAL_CHAR_EFF::szEffTex` — the same records whose layout was wrong until today's
  LP64 fix. That is where to look next.

- **2026-08-26 — cube textures and texture stage 1, and the hair shading they were for.**

  Reported as "the hair does not look like the PC version, it has more colour shading".

  **Verified before building anything**, because the obvious answer was wrong. The natural
  read was that `DxEffCharSpecular2` needed fixed-function specular lighting, which the shim
  does not implement. Logging the branch said:

      specular2 [m_hair00[Mesh]]: cube path (flag=0x00000001, skinDetail=2, realSpecular=0, cubeTex=0x0)

  `Render()` picks its path from `m_dwFlag & EFFCHARSPEC2_CUBE` — effect data, not a cap — so
  the hair takes the **cube** path, and `realSpecular=0` means the vertex path would have
  returned early regardless. Specular lighting would have been wasted work.

  What the cube pass actually asks for, read out of the state block it applies
  (`m_pDrawCubeSB`): additive blending (`SRCBLEND=ONE`, `DESTBLEND=ONE`), stage 0
  `SELECTARG1` on the effect texture, stage 1 `MODULATE` with a cube map, and stage 1
  `TEXCOORDINDEX = D3DTSS_TCI_CAMERASPACENORMAL` with `D3DTTFF_COUNT3`. So: the effect
  texture times a cube map addressed by the camera-space normal, added on top.

  Implemented exactly that:
  - **Cube DDS decode** — `RanImage_DecodeCube` reads the six faces, each a full mip chain,
    face-major, which is how `lobbycube.dds` (256x256 DXT1, 9 levels, `caps2=0xFE00`) is laid
    out.
  - **`RanCubeTexture`** — a real `IDirect3DCubeTexture9`, uploading through
    `RanGLR_UploadCubeFaceLevel` to `GL_TEXTURE_CUBE_MAP` and honouring the render-thread
    rule, with the same S3TC-or-decode fallback the 2D path uses.
  - **The three `D3DXCreateCubeTexture*` entry points**, which were stubs returning
    `D3DERR_NOTAVAILABLE`.
  - **Texture stage 1** — `RanGLR_SetStage1` binds the cube map to texture unit 1, and the
    fragment shader modulates `texture(uTexCube, normalize(mat3(uView) * vNormal))` into the
    stage 0 result. Only the configuration the engine uses is implemented; any other stage-1
    op or texcoord source is reported once rather than drawn wrong.

  Measured after: `cube texture 6199 = /sdcard/ran/textures/effect/lobbycube.dds (256x256,
  9 levels)`, the hair effect's `cubeTex` is non-null, no stage-1 warnings, `glErr=0x0000`,
  and the hair renders with its shading instead of flat white. This also clears
  `D3DXCreateCubeTextureFromFileExA`, which was the last unimplemented entry point being
  reached.

- **2026-08-26 — the white item was two missing pieces of the fixed-function pipeline.**

  **Material diffuse was never applied to lighting.** `uMatDiffuse` was declared in the
  shader, given a uniform location and uploaded every draw — and never read in the shader
  body. D3D scales each light by the material (`light.Diffuse * material.Diffuse * N·L`, and
  `light.Ambient * material.Ambient`); without it every lit surface took the *light's* colour
  instead of its own. A piece with no texture therefore came out white. Fixed; the white
  signpost became grey metal and the white blob on the other player became a shaped figure.

  **Texture stage 1 did not exist.** Two configurations are used by the engine and both are
  now implemented, with anything else reported once rather than drawn wrong:
  - `MODULATE` with a **cube map** and `TEXCOORDINDEX = TCI_CAMERASPACENORMAL` — the
    character specular pass (hair sheen).
  - `MODULATE` of **`D3DTA_TFACTOR` with `D3DTA_CURRENT` and no texture at all** — how
    `DxEffCharAmbient::SettingState` tints a whole piece by a colour. This is what the
    guardian/pet pieces use, and it was being dropped entirely.

  The second one is worth noting for method: the first implementation required a stage-1
  texture, so the tint case fell through the guard silently. The "not implemented" log added
  alongside it reported `stage 1 with no texture, op 4 args 1/3`, which showed both that the
  case existed and that the arguments arrive as `(CURRENT, TFACTOR)` rather than the
  `(TFACTOR, CURRENT)` the engine source sets — `MODULATE` is commutative, so both orders are
  now accepted. Measured after: zero unimplemented stage-1 configurations, `glErr=0x0000`.

  **Still open on this piece:** `blackcatyb_m` and `december_guardian` genuinely carry no
  texture in the `.x` material or the `.cps` override — confirmed with per-frame naming after
  the first diagnostic proved unreliable (every mesh under a frame is unnamed, so it could
  only ever report the first one). They now draw with their material colour and tint rather
  than white, which is what the fixed-function pipeline gives them; whether that matches the
  PC exactly still needs a side-by-side.

  **Correction — the material diffuse change above darkened everything, and is now
  conditional.** Applying `uMatDiffuse` to the light term unconditionally was wrong: D3D takes
  the diffuse material from `D3DMCS_COLOR1` by default, meaning the *vertex colour* when the
  vertex has one, and the material only when it does not. The vertex colour is already
  multiplied in by the texture stage, so the material was being counted twice and the whole
  scene — GUI included — went dark. It now applies only when the vertex format carries no
  diffuse (`uHasVertexColor`, set per draw from the FVF, since it is a property of the vertex
  format rather than of any state block). Brightness measured back to where it was, and the
  untextured-material case still gets its colour.

- **2026-08-26 — off-screen passes verified, log noise cut, loading screen brought up.**

  **Off-screen chain (was: enabled but never looked at).** Five targets now render each
  frame — 1280x720, 512x512, 512x384, 256x256, 128x128 — so the glow and surface-texture
  passes do run, with `glErr=0x0000` and no visual break. Post-process stays inert, but by
  accident rather than design: `DxPostProcess::RestoreDeviceObjects` returns early when
  `CreatePixelShader` fails **without** clearing `m_bDeviceEnable`, and it is `m_pSufTEMP`
  being left NULL that keeps `m_bEnable` false.

  **Hazard recorded, deliberately not changed:** the shim reports
  `PixelShaderVersion = D3DPS_VERSION(2,0)` and `VertexShaderVersion = 1.1` while
  implementing no shaders at all — the mirror image of the caps bug fixed earlier today.
  Several systems branch on those (`DxShadowMap`, `DxCubeMap`, `DxEffectMan`), and
  `RenderDevice.cpp:454` uses them to decide whether the device is acceptable at all.
  Lowering them without a device to verify on risks breaking the boot, so it waits.

  **Log noise.** Two of my own diagnostics were flooding: the surface-texture state line ran
  every frame (3937 lines a session, now once per change), and the diagnostic flag files were
  probed once a second through the logging `fopen`, so every absent flag printed a failed
  open (`access()` now, 3949 -> 589). Removed the traces that had served their purpose — the
  per-effect TypeID trace, per-texture queue/complete logging, the per-part material dump,
  the specular2 path log, the item icon logs, and the old ANIPROBE matrix dump. Kept every
  once-per-name failure signal, each of which has already caught a real bug.

  **Loading screen now renders.** `NLOADINGTHREAD::LoadingThread` is a full
  `Clear`/`BeginScene`/draw/`Present` loop on a thread the client starts while the main
  thread loads the map, and EGL binds a context to one thread — so all of it went nowhere.
  The context is handed over for the duration: released on the main thread before
  `_beginthreadex`, acquired at the top of the loading thread, released again before it
  exits, re-acquired in `EndThread`. "The render thread" now means *the thread holding the
  context* (`RanGLR_OnRenderThread` -> `RanGL_HasContext`), without which uploads made while
  the loading screen is up would be skipped and the screen would stay blank.

  Verified on screen: loading art, the map-name plate, the hint icon, the step indicator and
  the copyright line all draw (`native/out/ldx3.png`); no `eglMakeCurrent` errors; no texture
  left at GL id 0; the game renders normally after the handover back.

- **2026-08-26 — the loading spinner, and text sitting off the line.**

  **`ID3DXSprite::Draw` was a stub** returning `D3D_OK` and drawing nothing, so the loading
  screen's step indicator was a blank circle where the PC shows an animated ring. It is now
  a real screen-space quad: the source rect picks the cell out of `loading_st.dds` (512x128,
  four 105x105 frames), `pCenter` is the point placed at `pPosition`, the sprite transform
  applies, the colour modulates, and `Begin`/`End` save and restore the device state D3DX
  touches — with the half-texel offset that stops an atlas cell bleeding into its
  neighbours. Verified: the segmented ring with its progress arc and "Loading.." draws, and
  `loading_st.dds` finally reaches the GPU. This fixes every sprite in the client, not just
  this one.

  **Text was about a fifth too tall and off-centre in its line.** `D3DXFONT_DESC.Height`
  follows `LOGFONT.lfHeight`: positive means the **cell** height, so Windows picks a face
  whose ascent + descent comes to that many pixels; negative means the em box.
  `CD3DFontX::RestoreDeviceObjects` passes a positive `m_iHeightScreen`, and the shim scaled
  by the em box regardless. `fontScale()` now divides by `AscenderUnits() + DescenderUnits()`
  for a positive height and by `UnitsPerEm()` for a negative one, and `GetDC` converts back
  to an em size before `CreateFontA` so the DC the client measures with describes the font
  actually being drawn. `CreateFontA` in `gdi_text.cpp` was already right — the client only
  ever passes a negative height there.

  Measured after: chat tab labels sit centred in their buttons, and the HUD's `7.01%` and
  `Lv. 4` fit their boxes instead of clipping.

## Text sat low in every button — substituted font metrics (fixed 2026-08-26)

**Symptom.** Button captions ("เริ่ม", "ยกเลิก", "ออก") sat low in their buttons
with a wide gap above; "สร้าง(1)" clipped at the bottom.

**Not a centring bug.** `CBasicTextButton::CreateButtonImage` sets
`TEXT_ALIGN_CENTER_X` only — vertical placement is top-alignment inside a caption
box whose rect comes from `CLIENT/data/gui/uiextcfg.xml`:

    BASIC_TEXT_BUTTON_IMAGE_TEXTBOX18   Y=3  H=15   (button 18 tall)
    BASIC_TEXT_BUTTON_IMAGE_TEXTBOX22   Y=4  H=18   (button 22 tall)

So the baseline is `box.top + tmAscent`. Nothing centres anything; the layout was
measured against one specific font's metrics.

**Cause.** `_DEFAULT_FONT` is **Tahoma** (`DxFontMan.cpp:13`), asked for at em
size 12 (`CD3DFontX` does `-MulDiv(9, 96, 72)`). The shim resolves that to
`/system/fonts/NotoSansThai-Regular.ttf` and then took ascent/descent from
*that* face. Noto's ascender is 1.16 em against Tahoma's 1.0005:

    face                 ascent  descent  lineH   (at em 12)
    Tahoma (GDI)             12        2     14
    NotoSansThai (was)       14        4     18

Two pixels of extra ascent in a caption box only 15 px tall is the whole caption
dropping toward the bottom edge — and `H=15` boxes could not hold an 18 px line,
which is what clipped "สร้าง(1)".

**Fix.** `RanFont_WinMetrics` / `RanFont_WinEmForCellHeight` in
`shim/win/ttf_raster.cpp`: a table of the OS/2 winAscent/winDescent of the faces
this client names (Tahoma, Verdana, Arial, Microsoft Sans Serif), rounded the way
GDI rounds. `gdi_text.cpp` and `d3dx_font.cpp` take ascent/descent/lineH from the
*named* face; glyphs still rasterise from the substituted face at the same em
size. Unknown face names fall back to the real face's metrics.

Rounding matters: GDI rounds each metric to nearest independently
(12.006 -> 12, 2.478 -> 2, tmHeight 14). Rounding the sum, or ceiling either
half, moves the baseline a pixel.

**Verified.**
* `GetTextMetricsA` on Windows 11, Tahoma `lfHeight=-12` -> ascent 12, descent 2,
  height 14. The shim now logs exactly `lineH 14 ascent 12`.
* PC client screenshot, "ออก" button: button top 820, ink rows 828..834,
  baseline +15, x-height 7.
* Mobile after the fix, same button: baseline +15, x-height 7 — identical.
* Char-select "ยกเลิก": button 739..780, ink 750..769 -> 11 px above, 11 below.
  Was ink 757..773 (18 above, 7 below).
* "สร้าง(1)" no longer clips.

**Still open, seen in the same shot:** the top-left char-select button renders
`ź` instead of its label — a codepage/text bug, unrelated to layout.

RESOLVED (2026-08-26) — that was the CP874/UTF-8 decoder guessing on
well-formedness alone. See "Thai text: short labels decoded as the wrong script".

## Thai text: short labels decoded as the wrong script (fixed 2026-08-26)

**Symptom.** The character-select delete button read `ź`. Other short Thai labels
were affected the same way; long strings were fine.

**Cause.** `looksUtf8()` in `shim/win/gdi_text.cpp` decided UTF-8 vs CP874 by
well-formedness alone. Both encodings are in play — the loose GUI XML is UTF-8,
the game-text tables inside `Gui.rcc` are CP874 — so a guess is unavoidable, but
well-formedness is not enough to make it:

    "ลบ"  = CP874  C5 BA
                 = UTF-8  U+017A  "ź"

CP874 Thai occupies 0xA1..0xFB, which overlaps UTF-8's continuation range
0x80..0xBF, so short Thai words are frequently valid UTF-8 by accident. Long
strings almost always trip over an invalid byte and fall back correctly — which
is exactly why only the shortest labels broke.

**Fix.** Decide on plausibility, not just validity. A real UTF-8 Thai string
decodes into the Thai block; CP874 misread as UTF-8 lands in Latin Extended-A,
Gujarati, Malayalam. `looksUtf8` now does a full validating decode and rejects
the run unless every multi-byte sequence yields Thai (U+0E00..U+0E7F) or one of
CP874's own nine non-Thai high characters. It also rejects overlong forms
(0xC0/0xC1 leads, and short 3/4-byte encodings) and surrogates, which kills a
whole family of two-byte Thai pairs on its own.

**Verified.**
* Offline sweep of every distinct Thai byte-run in `Gui.rcc`: strings the old
  rule misread 22946, the new rule 17. The residual 17 are the genuinely
  ambiguous `E0 B8/B9 xx` pattern (CP874 "เธ_"/"เน_" vs UTF-8 Thai), which
  resolves to UTF-8 — the right call.
* Round-trip: genuine UTF-8 Thai still detected and decoded; the same words in
  CP874 bytes are no longer mistaken for UTF-8.
* On screen: the button reads `ลบ`. Character select and in-world chat, tabs,
  NPC names and window labels all render correct Thai.

**Also checked**
* Glyph coverage: all 87 CP874 Thai codepoints are present in
  `/system/fonts/NotoSansThai-Regular.ttf`; all printable ASCII in the Roboto
  fallback (the Thai face has 1/95, so the fallback is load-bearing).
* Only the D3DX text path is live (`SetFontSys` forces `EMFONT_D3DX`), and both
  it and the GDI path share the corrected decoder.
* `IsDBCSLeadByteEx` correctly reports 874 as single-byte. The unqualified
  `IsDBCSLeadByte` is wrong for Thai but is only reached from BugTrap.

### Known remaining gap: no Thai shaping

`NotoSansThai-Regular.ttf` carries `GSUB: ccmp` and `GPOS: mark, mkmk`. The
rasteriser applies none of them — glyphs are laid out linearly by advance, and
combining marks land wherever their default outline puts them.

In practice the common cases come out right, because the default outlines are
drawn at heights that suit them: tone-mark-over-upper-vowel stacks correctly in
"เริ่ม", "ขึ้น", "แล้ว". What `ccmp` exists for is the tall consonants
ป ฝ ฟ ฬ, whose ascender the mark must clear via a substituted lowered form.
Without it a mark can collide with the ascender.

On the PC this is done by Uniscribe behind `ID3DXFont` (which is why
`D3DFontX.cpp` deliberately loads `d3dx9_35.dll` "which shapes Thai correctly via
Uniscribe"). Closing it means implementing the `ccmp` substitution and the
`mark`/`mkmk` attachment lookups — not hand-tuned offsets.

NOT MEASURED: how often this occurs in real game text. Extracting the string
table from the packed `Gui.rcc` with a byte scan pulls too much binary noise to
give an honest frequency, and I have not written a real `.rcc` reader for it.

SUPERSEDED (2026-08-26): the frequency question turned out not to be the point.
The real gap was GSUB `ccmp`, without which a tone mark is never positioned by
anything at all. Shaping is now implemented — see "Thai shaping implemented".

## Inventory tooltip crash — not the keyevent (2026-08-26)

**Symptom.** Sending keyevents to open the inventory/character/skill windows
killed the client. It looked like an input-handling crash; it was not.

**What it actually was.** Tombstone, symbolised against the matching BuildId:

    NS_ITEMINFO::LOAD          UIItemInfoLoader.cpp:1715  <- SIGSEGV
      inlined: std::string::c_str -> __is_long
    CInnerInterface::SHOW_ITEM_INFO   InnerInterfaceSimple.cpp:1625
    CInventoryWindow::TranslateUIMessage  InventoryWindow.cpp:175

Line 1715 col 101 is `COMMENT::BLOW[emBLOW].c_str()`. `COMMENT::BLOW` is
`std::string[EMBLOW_SIZE]` (9), and every use in that block indexes it with
`emTYPE` unchecked, so an out-of-range value reads a `std::string` out of
arbitrary memory and dereferences it. Opening the inventory showed a tooltip;
the tooltip did the rest.

Frame #0's symbol read `NS_ITEMINFO::LOAD+27706`, which is only the nearest
preceding exported symbol — the real location came from `llvm-symbolizer
--inlines`, and the inlined `c_str`/`__is_long` frames are what identified the
expression.

**Measured, in this order:**
1. Guarded the index and logged the value: `blow type -1, outside 0..8`.
   The client no longer dies, and all three windows open.
2. Suspected the LP64 record trap, so checked it instead of assuming:
   `sizeof(ITEM::SSUIT)` matches the size on disk, and a per-chunk trace of
   `SITEM::LoadFile` shows every header-carrying chunk (FILE_SBASIC..
   FILE_SGENERATE) reading exactly its stated length. **The item file is read
   correctly** — this is not a layout bug.
   (First cut of that trace also flagged chunks 7..14 and 100. Those are false
   positives: `FILE_BOX` and above carry no version/size header and hand the
   stream to a sub-loader, so `dwVer`/`dwSize` were stale. The probe now only
   checks the chunks that have a header.)
3. Logged what was asked for versus what came back:
   `asked for item 0/0, got record 0/0`.

**Where it stands.** The request is for item **0/0**, which is the value of a
*zeroed* `SITEMCUSTOM`, not a default-constructed one — `SITEMCUSTOM::SITEMCUSTOM`
initialises `sNativeID(false)`, i.e. ID_NULL/ID_NULL, and
`CItemSlot::ResetItemImage` explicitly sets `NATIVEID_NULL()`. The guard in
`InventoryWindow.cpp:174` tests `!= NATIVEID_NULL()`, which 0/0 passes. So a slot
somewhere is being filled from an `SINVENITEM` whose memory was zeroed rather
than constructed.

`GLItemMan::GetItem` then returns non-NULL because `m_ppItem` is zeroed on
allocation and entry [0][0] is genuinely populated — so a real record comes back,
and its blow type is -1.

NOT YET FOUND: which path puts a zeroed `SITEMCUSTOM` into a slot. The candidates
are `CInventoryPage::LoadItemPage` and the inventory packet parse
(`GLInventory::SETITEM_BYBUFFER`). The bounds guard makes this survivable and
self-reporting in the meantime.

FOUND (2026-08-26): neither candidate. `SINVENITEM` keeps `sItemCustom` in an
anonymous union, so `SITEMCUSTOM`'s constructor never runs and the slot is simply
uninitialised — which reads as 0/0 on Android's zero-filled pages. See "Empty
inventory slots read as item 0/0".

## %I64d printed literally (fixed 2026-08-26)

Found while checking the above: the character window showed `EXP I64d /I64d` and
`แต้มผลงาน I64d`. `%I64d` is MSVC's spelling of a 64-bit conversion; bionic does
not know `%I`, drops the conversion and prints the rest literally, so every
64-bit number in the UI was missing. There are 158 such format strings in SOURCE.

Fixed in one place instead of 158: `shim/win/fmt_msvc.h` rewrites `%I64` to `%ll`
and `%I32` to plain, preserving flags/width/precision and leaving `%%` alone.
`CString::FormatV` and `StringCchVPrintfA`/`StringCchPrintfA` route through it,
so the SOURCE call sites stay byte-identical to the PC build.

Verified by compiling the header for android-x86_64 and running it on the device,
so bionic's own printf is what was tested:

    EXP %I64d /%I64d  -> EXP %lld /%lld       %-8I64u|      -> %-8llu|
    %.2f and %I64x    -> %.2f and %llx        %%I64d literal-> %%I64d literal
    %I32d plain       -> %d plain             rendered: "EXP 164"

## Thai shaping implemented (2026-08-26)

The rasteriser laid glyphs out linearly and applied none of the font's layout
tables, so combining marks landed wherever their default outline put them. The
visible symptom was tone marks floating well above the line — `ร์` in
"เซิร์ฟเวอร์" sat a third of an em too high.

**Why that happens.** `NotoSansThai-Regular.ttf` gives a tone mark two forms and
picks between them in GSUB, then positions it in GPOS. Read out of the font:

    ccmp lookup 2  (unconditional)     93..97 -> 73..77   the LOW forms
    ccmp lookup 3  (chained context)   73..77 -> 93..97   but only when a vowel
                                                          precedes - the HIGH forms
    GPOS 'mark'    covers 54, 57-63, 72-79, 105-107       attaches to a base
    GPOS 'mkmk'    stacks a mark on a mark

The cmap always yields the high form (93..97), and GPOS `mark` does not cover
that range at all. So without `ccmp` a tone mark is never repositioned by
anything — it just floats. `ccmp` is not an optional refinement here; it is what
moves the glyph into the range GPOS can attach.

**Implemented** in `shim/win/ttf_layout.cpp`: GDEF glyph classes, GPOS lookup
types 4 (MarkBase) and 6 (MarkMark), GSUB types 1 (single), 2 (multiple) and 6
format 3 (chained context), plus coverage formats 1/2, class definitions 1/2 and
anchor formats 1-3. `d3dx_font.cpp` now shapes a run before laying it out:
cmap -> ccmp -> rasterise by glyph id -> attach marks. The glyph cache is keyed
by glyph id rather than codepoint, because after `ccmp` the glyph for a
codepoint depends on its neighbours.

Marks also stopped advancing the pen, and the measured width (`DT_CALCRECT`,
the centring width, and `GetTextExtentPoint32A`) skips them to match — a
measured width that disagrees with the drawn width shows up as text sitting off
-centre.

**Verified.**
* The table walk was checked against the real font in node before building:
  GDEF classes correct, `mark` gives ก dx=1284 / ฬ dx=1263 dy=303 (the tall
  consonant raises the mark), `mkmk` stacks at dy=500.
* Placement was simulated end to end and compared old vs new before building,
  which showed the GPOS-only change was tiny — correctly, since the real fix was
  `ccmp`.
* On screen: "< เลือกเซิร์ฟเวอร์ >" now renders with `ร์` sitting on its
  consonant instead of floating. Login, character select, in-world chat and ten
  windows all render correct Thai with no crash.

One earlier reading was wrong and worth recording: I first read the floating
marks as a regression from my own GPOS change. Cropping the same title from a
build made *before* that change showed them floating identically — the bug
predated it. Comparing against the previous build's screenshot, rather than
against expectation, is what caught it.

## Inventory 0/0: the layout theory is refuted (2026-08-26)

`GLInventory::SETITEM_BYBUFFER` reads `SINVENITEM_SAVE` as a raw struct blob
straight off the network stream, with `GASSERT(dwSize==sizeof(...))` compiled out
in release — the same shape as the item-file read, and the obvious suspect for a
32-bit/LP64 mismatch.

It is not that. A loud check now compares the server's `dwSize` against this
build's `sizeof(SINVENITEM_SAVE)` on every load, and it stays silent: **the
record sizes agree**. Combined with the earlier item-file result (every
header-carrying chunk reads its exact stated length), two separate raw-blob paths
have now been measured and cleared.

So the `0/0` is not a desync. What remains is either an inventory record the
server genuinely sends zeroed, or a field-offset difference that happens to
preserve the total size. Not chased further this session.

What is fixed regardless: the tooltip no longer crashes on it, and it reports
itself. Both size checks stay in as permanent, silent-when-healthy integrity
checks — this is a class of bug that otherwise corrupts data with no signal at
all.

## Window sweep (2026-08-26)

All 17 menu hotkeys from `RANPARAM::MenuShotcut` opened and closed, in world:
inventory, character, skill, party, quest, club, friend, map, chat macro, item
bank, item shop, run, and the five unlabelled ones. **No crashes.** Thai is
correct in every window that opened, and `%I64d` numbers now render.

Two notes from the sweep:

* A first pass reported "no window opens" for all 17. That was the measurement,
  not the game: the baseline screenshot it diffed against already had windows
  open. Re-run from a verified-clean state, ten windows opened with 8-34% of the
  frame changing.
* The Assist, Club and item-bank panels look see-through. Measured rather than
  assumed: the body of a `CBasicLineBox` is a single 1x1 texel of
  `interface_main.dds` stretched to fill, and those texels carry alpha 132-153
  out of 255. **The translucency is by design**, not a decode bug — so it has
  been left alone. Confirming it matches the PC needs a side-by-side of the same
  window on the PC client, which has not been done.

## Empty inventory slots read as item 0/0 — root cause found (2026-08-26)

The tooltip crash's real cause, traced end to end this session.

`SINVENITEM` keeps `sItemCustom` inside an **anonymous union**:

    union {
        struct { SINVENITEM_SAVE sSaveData; };
        struct { WORD wPosX; WORD wPosY; SITEMCUSTOM sItemCustom; };
    };

A union has no active member to construct, so `SITEMCUSTOM`'s constructor — the
one that sets `sNativeID(false)`, i.e. ID_NULL — **never runs**. And
`SINVENITEM`'s own constructor initialises only `wBackX`/`wBackY`.

On Windows this goes unnoticed: fresh heap holds debris, and debris does not
look like a valid item id. Android hands out **zeroed** pages, so an untouched
slot reads as item **0/0** — which is a legal id, and therefore slips past every
"is this slot empty" test in the client, all of which compare against
`NATIVEID_NULL` (0xFFFF/0xFFFF). `InventoryWindow.cpp:174` then asks for a
tooltip on an empty cell, and `NS_ITEMINFO::LOAD` indexes `COMMENT::BLOW` with a
garbage blow type and segfaults.

**Fix:** `SINVENITEM::ClearSlot()`, called from both constructors, sets the
position and both native ids to `SNATIVEID(false)`.

Note this is **not** `#ifdef RAN_MOBILE` guarded. It initialises memory that was
previously left uninitialised, which is a strict improvement on MSVC too and
keeps the two builds behaving identically; guarding it would leave the PC on
undefined behaviour. Flagged rather than done silently.

**How it was found** (four wrong theories, each killed by measurement):
1. LP64 layout mismatch on the item file — refuted, every header-carrying chunk
   reads its exact stated length.
2. LP64 mismatch on the inventory packet — refuted, `SINVENITEM_SAVE` matches
   the size the server sends.
3. `CItemSlot::SetItemImage` bailing on an unknown id — refuted, it is never
   called; the live class is reached through `CInventoryPage`, and
   `LoadItemPage` reported **0 items**, which is correct: the character's
   inventory really is empty.
4. Only then did "empty inventory + slot reads 0/0" point at the constructor.

**Verified:** hovering empty slots no longer requests a tooltip, no
`blow type -1` report, client stable. The bounds guard stays in as defence.

## The caps lie is inert — corrected (2026-08-26)

Earlier notes here said the false `PixelShaderVersion = D3DPS_VERSION(2,0)` was
gating real subsystems (DxCubeMap, DxPostProcess, DxShadowMap, DxEffectMan).
Measured, that is **wrong**, and the correction matters:

* `GetCreationParameters` reports `D3DCREATE_HARDWARE_VERTEXPROCESSING`, not
  MIXED, so `DxEffectMan::InitDeviceObjects` sets `m_bUseMIXED = FALSE`.
* That leaves `m_dwUseSwShader` at its non-zero initial value, and every
  `!dwUseSwShader && PixelShaderVersion >= ...` branch is already off.
* `m_bPixelShader`, `m_bPixelShader_1_4` and `m_bPixelShader_2` are set only
  inside the `m_bUseMIXED` block, so they stay FALSE.
* The one branch with no SwShader guard is `m_bBorder` — and
  `DxEffectMan::IsBorder()` has only commented-out callers.

So the shader-version caps have no live consumer and were left alone. What was
fixed is the one cap that is a real promise we break: `D3DPTADDRESSCAPS_BORDER`
is no longer advertised, because the sampler maps `D3DTADDRESS_BORDER` to
`GL_CLAMP_TO_EDGE` (ES has no border colour).

## Still open after this session

* **Item tooltips never render.** `SHOW_ITEM_INFO` runs and `ShowGroupTop
  (INFO_DISPLAY)` is called, and *skill* tooltips do render — so the machinery
  works and something item-specific fails. Not yet found.
* Sky-coloured geometry gaps — not reproduced at the one camera angle tried.
* Fixed-function gaps: `TCI_CAMERASPACEPOSITION` + `D3DTTFF_PROJECTED`,
  `SetClipPlane`, vertex specular.
* Panel translucency vs the PC — needs a side-by-side with MiniA.
* Gameplay past the inventory: NPC dialogue, shops, trade, quest accept, skill
  use, combat, death, zone change.

## va_list reuse: a real bug, but not the one reported (2026-08-26)

Chasing "the tyranny end notice shows 68 นาที where it should show 1", I found
that `CInnerInterface::PrintConsoleTextDlg` (and 82 other places) format the same
`va_list` two or three times with no `va_copy`:

    va_start(argList, szFormat);
    StringCbVPrintf(szBuffer,  ..., szFormat,   argList);   // consumes it
    strCombine.Format("%s", szFormat);
    StringCbVPrintf(szBuffer2, ..., strCombine, argList);   // reuses it - UB
    va_end(argList);

`szBuffer2` is what gets displayed. On 32-bit MSVC a `va_list` is a bare `char*`
passed by value, so a callee's walk leaves the caller's copy alone and this works
by accident — which is what the code was written against.

**Measured, on device, rather than assumed:**

* x86_64: the two-argument case really does break — "2 hours 30 minutes" comes
  out as "-1 hours 0 minutes". The one-argument case survives.
* **arm64 (Tab S9): nothing breaks.** Not one argument, not two, not a string
  plus an int — with the intermediate `CString::Format` call in place, exactly as
  the real function has it.

So this is a genuine latent bug that is fixed, but it is **not** the cause of the
reported 68. Both call sites of that string pass a literal `1`
(`GLGaeaClient.cpp:3533`, `GLPVPTyrannyClient.cpp:95`), so the value cannot be
wrong at the source. Still unexplained.

**Fixed anyway**, in the shim rather than at 83 call sites: `StringCchVPrintfA`
and `CString::FormatV` now work on a `va_copy` and never consume the caller's
list, restoring the by-value semantics every caller assumes.

## First arm64 verification (2026-08-26)

Everything this session had been checked only on LDPlayer x86_64. On the Tab S9
(SM_X710, arm64-v8a, 2560x1600), booted from the same APK:

* Thai shaping is correct — "< เลือกเซิร์ฟเวอร์ >" renders with ร์ sitting on
  its consonant, not floating. The `ccmp` work holds on the real device.
* Button captions are centred — the "ออก" button matches the PC reference.
* Boots clean, no fatal, no missing-face or atlas-full reports.
* The scene renders markedly better than the emulator (real GPU): foliage,
  sky and terrain all correct.

NOT verified on arm64: anything behind the login — the `%I64d` numbers, the
inventory slot fix, item tooltips. `option.ini` on the device is encrypted so
there is no saved id to read, and guessing credentials risks the server's
auto-ban. Needs the account details, or the user logged in.

## Exploded, flickering characters on the tablet — streaming ring fence (2026-08-26)

**Symptom (Tab S9 only, never on the emulator):** in world, the player character
rendered as huge stretched polygon shards radiating from its position, changing
every frame. Character select was clean.

**Cause.** The streaming vertex/index ring in `gl_render.cpp` guards its wrap
with a single fence, in the wrong order:

    if (cursor + size > capacity) {
        waitForLap();        // fence inserted at the START of this lap
        cursor = 0;
        markLap();           // fence for the next lap
    }

Wrapping reuses memory the GPU may still be reading. The draws that read this
lap were submitted *during* the lap — after the fence being waited on. So the
wait proved only that work from *before* the lap had finished, and the memcpy
then overwrote vertices a queued draw was still sourcing.

Two things hid it:
* The emulator serialises the GPU, so the race cannot express itself.
* It needs enough streaming volume to wrap. Character select streams little and
  was clean; the world streams ~717 KB/frame at 61 fps, wrapping the 16 MB ring
  a few times a second.

**Fix:** fence *after* the lap's last draw, then wait, then reset.

    markLap();
    waitForLap();
    cursor = 0;

One stall per wrap. At ~22 frames per lap that is not measurable.

**Verified on the Tab S9:** character renders correctly in world, and four
frames sampled two seconds apart differ by 1.7-3.3% — idle animation, not the
per-frame churn of the corruption. This was also what the user was describing as
"skin texture overlapping" and "flickering": one bug, not two.

## The dark GUI: gamma ramp is a no-op (diagnosed, not yet fixed)

`GammaControl::Apply` builds a 3x256 ramp from the user's gamma, contrast and
overbright settings and applies it with **GDI** `SetDeviceGammaRamp`. The shim
has:

    inline BOOL SetDeviceGammaRamp(HDC, LPVOID) { return FALSE; }

So every brightness/contrast setting the player chooses does nothing, and the
whole frame — GUI included — renders darker than the PC, which is exactly the
report. (The D3D `IDirect3DDevice9::SetGammaRamp` is a no-op too, but this
client uses the GDI one.)

Fix not yet written. The faithful approach is to capture the ramp and apply it
as the last step of the uber fragment shader via a 256-entry LUT texture, since
every pixel the client draws goes through that shader — which makes it
equivalent to a display ramp without needing an off-screen pass.

## State-block recording leaked onto the live device (2026-08-26)

The cause of the "overlapping texture", the mismatched ground, and much of the
darkness — one bug behind all three.

D3D9 semantics: between `BeginStateBlock()` and `EndStateBlock()`, `SetRenderState`
and friends are **captured into the block and do not touch the device**. The shim
captured *and* applied:

    if (m_recording) m_recording->m_rs.push_back({State, Value});
    m_renderState[State] = Value;          // <- applied as well. Wrong.

So every state block the engine builds at startup permanently leaked its contents
onto the live device, and nothing ever took them back. `DxRenderStates::Init`
alone (`DxRenderStates.cpp:419`) builds its shadow blocks with:

    D3DRS_DEPTHBIAS         -0.0002
    D3DRS_ZWRITEENABLE      FALSE
    D3DRS_LIGHTING          FALSE
    D3DRS_ALPHABLENDENABLE  TRUE
    D3DRS_FOGCOLOR          0
    stage 0 D3DTSS_COLOROP  D3DTOP_SELECTARG1

Every one of those was left applied to the whole scene. In particular:

* **DEPTHBIAS -0.0002** becomes a **-3355 unit** polygon offset. The bias is in
  depth units and `glPolygonOffset` counts smallest-resolvable steps, so the
  conversion multiplies by 2^24 — correct arithmetic applied to a value that
  should never have reached the device. Everything was drawn pulled hard toward
  the camera, so surfaces punched through each other: the overlapping textures.
* **COLOROP = SELECTARG1** on stage 0 selects one argument instead of modulating
  the texture, which is why patches of ground rendered as flat vertex-lit colour
  with no texture — the "ground that mismatch".
* **LIGHTING FALSE / FOGCOLOR 0 / ALPHABLENDENABLE TRUE** account for much of the
  overall darkness.

This is also why implementing `D3DRS_DEPTHBIAS` earlier appeared to fix the
overlap and then appeared to regress: before that work the state was ignored
entirely, so the leak was harmless; implementing it correctly is what gave the
leaked value teeth.

**Fix:** `SetRenderState`, `SetTextureStageState` and `SetSamplerState` now
return immediately after capturing when `m_recording` is set — no device state,
no batch flush, no epoch bump, because nothing actually changes.

**Verified** on the emulator, login background, same crop before and after: the
flat olive ground is gone and the grass texture reaches the bottom of the frame;
the whole scene is markedly brighter and the GUI panel legible.
(`scratchpad/ldgnd.png` before, `scratchpad/sbgnd.png` after.)

---

## Touch controls: the skill arc, and two movement bugs

### Reusing the client's slots instead of drawing new ones

The first cut of the touch pad drew its own skill buttons — ten circles and a
pair of page arrows — and pressed the matching keys. That was wrong twice over:
the drawn circles were blank, so nothing showed which skill was where or whether
it was on cooldown, and the client's own quick-skill tray was still on screen
down the left edge, so there were two sets of slots.

`CSkillTrayTab::MobileArrangeArc` (guarded, `RAN_MOBILE`) moves the real tray
instead. Its ten slots go onto two quarter arcs around the attack button, five
inner and five outer; the overlay keeps only the attack button and reports its
position as fractions of the surface so the client can lay the arcs out in its
own coordinate space.

Because they are still the client's controls they keep their icons, cooldown
sweeps, tooltips and drag-to-assign, and `CBasicSkillTray::TranslateUIMessage`
already runs `ReqSkillRunSet` on `UIMSG_LB_UP` — the same call the number keys
make. Nothing new had to understand skills.

Paging came free as well. Only the current page's tab button is visible at a
time and clicking it advances to the next, so all four stack in one spot beside
the arc and act as both indicator and switch. The page arrows were deleted.

### The stick was dropping a click marker per step

`GLCharacter::ActionMoveTo` fires `NewClickEff` whenever `RANPARAM::bClickEffect`
is set. The stick reaches its destination by issuing a GOTO several times a
second, so walking laid a trail of "you clicked here" markers across the ground —
which is what it looked like: the screen being tapped over and over.

`GLCharacter::SuppressClickEff` is set around the stick's own calls only. Real
clicks still show the marker.

### The stick stopped short of edges and slopes

Two causes, both in how the destination was probed.

`ActionMoveTo` casts a vertical ray from `vFromPt` down to `vTargetPt` to find
the ground under the destination, and returns FALSE — no movement at all — if it
misses. The stick was passing a **±5 unit** window around the character's own
height. Any ground ahead that rose or fell more than that (a ramp, a stair, a
kerb) fell outside the ray and the character stopped for no visible reason. Now
±600.

The second cause is why it showed up worst near edges: aiming a fixed 260 units
ahead means walking toward a wall or the rim of the walkmesh eventually puts the
destination past the edge, where there is no ground at any ray height. The stick
now retries at 55%, 25% and 10% of the reach, so the character slides along the
edge instead of halting a stride short of it.

**Verified on the emulator.** Holding the stick forward walked the character
across a bridge and up a flight of steps in one unbroken move, with no click
marker anywhere on the ground; releasing it returned the idle pose within a
frame. (`out/mv_hold.png`, `out/mv_hold2.png`, `out/mv_rel.png`.)

### Naming the corner controls, and two wasted cycles

Two passes at the corner layout listed the controls by GUID and both silently
missed some. The cause is worth recording: the control census prints the XML
**keyword**, the code needs the **enum** name, they differ, and the
keyword-shaped spelling usually exists too as a neighbouring control:

    census keyword          id    enum name          keyword-as-enum
    PARTY_FINDER_BUTTON     206   FINDER_BUTTON      PARTYFINDER_BUTTON = 208
    AUCTION_ALERT           239   AUCTION_BUTTON     AUCTION_ALERT      = 240

So the wrong name compiles, FindControl returns a real control, and an invisible
one gets moved while the button you meant stays in the corner. Nothing errors.

The fix was to resolve the ids rather than guess: parse `InnerInterfaceGuid.h`
positionally (entries run from `NO_ID + 1`) and cross-reference every name
against the census id before using it. The arrangement now carries those ids in
its comments.

A recursive census dumped *after* the arrangement settled the last one: the
envelope still sitting in the corner belongs to `CItemShopIconMan`, which anchors
an icon over the head of any player with a personal shop open. It was never HUD
furniture, just another player standing there.

Both diagnostics have been removed now the layout is settled.

### Deflection picks the gait

How far the stick is pushed now chooses walk or run, the way a console stick
does: ease it over and the character walks, push it out to the ring and it runs.
The overlay already clamped the stick's magnitude to exactly 1.0 once the knob
reaches the ring, so "at the ring" is a real reachable value rather than
something the player has to feel for.

Two thresholds, not one: run above 0.98, walk below 0.90, and hold whatever the
current state is in between. A single threshold at 1.0 would flip the gait every
frame while a thumb rests on the edge, and every flip is a `SNETPC_MOVESTATE`
the server is told about.

The switch goes through `GLCharacter::ReqToggleRun` rather than setting
`EM_ACT_RUN` directly - that call also flips the game menu's run button, retimes
the pet's movement, and sends the state message. Setting the flag by hand would
desync all three.

**Verified:** at half deflection the character walks upright with a short stride;
pushed to the ring it leans forward, swings wider, and covers several times the
distance in the same five seconds. (`out/gait_walk.png`, `out/gait_run.png`.)

### The rows sit against the compass rose

Right-aligning the two rows to the minimap group's left edge left them stranded
in the middle of the screen. The group is 340 wide but the rose is only the
square at its right-hand end; the rest is empty space that the date and clock are
drawn over.

The rose is square and flush to the group's right edge, so its left edge is one
group-height in from that: `rcMap.left + rcMap.sizeX - rcMap.sizeY`. At 1280x720
the group is 940,0 340x120 and that gives 1160, which is where the rose measures.
Both rows now end hard against it. (`out/arcEtr.png`.)

### The attack button was drinking a potion

Verifying the touch controls turned up a real bug rather than confirming one.
The attack button sent `DIK_Q`, on the assumption that Q was a basic attack. It
is not: `RANPARAM::QuickSlot[0]` is `DIK_Q`, the first **item** slot on the
top-left bar. Every press was using a consumable.

RAN has no attack key at all, because attacking is clicking a target - so there
was nothing to synthesise a keypress for and the whole approach was wrong.

`GLCharacter::MobileAttackNearest` (guarded) does what a click does instead:
picks the target with `FindNearTarget(..., EMFIND_TAR_ENEMY, false)` - the same
call the bot path uses - then runs `MobReaction` or `PvPReaction` with the
`DXKEY_DOWNED|DXKEY_UP` a completed click leaves behind. When the reaction
reports the target is out of reach it hands back a destination, so the same tall
ground probe and click-marker suppression the stick uses apply here too.

`bcontinue` is passed true. On a mouse that is ctrl-click, "keep attacking this
target"; a finger cannot hold a button down while doing anything else, so one tap
engaging until the target dies is the only thing that works on a touch screen.

**Verified:** pressing it puts the character into the attack animation, and two
seconds later it is in a different frame of the swing - it engages and keeps
swinging. (`out/atk1c.png`, `out/atk2c.png`.)

### What the touch controls have actually been tested for

| control | verified | how |
|---|---|---|
| stick, movement | yes | crossed a bridge and steps unbroken |
| stick, stop on release | yes | idle pose returns within a frame |
| stick, walk vs run | yes | stride and distance differ by deflection |
| no click marker | yes | ground clean through every move frame |
| skill slot hit test | yes | slot highlights, tooltip appears |
| skill tray click path | yes | page button cycles F1-F4 and the icons change |
| attack button | yes | attack animation, continuing across frames |
| pinch zoom | **no** | needs two real fingers; `adb input` cannot synthesise it |

Firing an individual skill from a slot was inconclusive rather than verified -
slot 1 is an SP refill at full SP and slot 2 did not fire for game reasons. The
click path through the moved tray is proven by the page button, which is the same
path through the same group.

### Back used to quit the game

`AKEYCODE_BACK` was not in `scanCodeFor`, so the handler returned 0, the system
took the key, and `NativeActivity` finished the activity. Back dropped the
player out of the game instantly, mid-session, with no confirmation - and on a
tablet it is a gesture you hit by accident.

It now maps to Escape, which opens the client's own menu. Quitting is still there
as "ออกจากเกม", but it is a choice rather than a slip. **Verified:** the menu
opens and the process survives. (`out/back1c.png`.)

### One finger, two mouse buttons

Right-click does real work in RAN - it uses or equips an item from the inventory,
clears a quick slot, drives context actions - and a touch screen has no second
button. A long press now stands in for it.

The press cannot be sent on touch-down, because by the time a hold is long enough
to count, a left click has already happened. So it is deferred: a finger that
moves is a drag and presses left as soon as it passes the slop threshold, a
finger that lifts early presses left then releases, and a finger that stays put
presses right at 450ms. The pointer still moves on touch-down, so hover and
tooltips are unaffected.

**Verified:** a long press on quick slot 2 cleared it, which is
`ReqSkillQuickReSet` on `UIMSG_RB_UP`. (`out/rc0c.png`, `out/rc1c.png`.)

### Clicks had to become frame-safe first, and that took three attempts

The UI only turns a press into a click when it sees the button down on one poll
and up on a later one, so both halves have to be visible to a poll. Deferring the
press made that fragile, and the first two attempts each broke the login.

1. **A latch that held the release until the press had been read.** Correct for
   one click, wrong for two: a second press cleared the first's pending release,
   so a double click collapsed into a single long press and picking a server from
   the list stopped working.

2. **An ordered queue, drained inside `GetDeviceState`.** That ties input
   delivery to the client happening to call one particular API. The outer stages
   read the mouse a different way, so the queue never drained there and the
   server list stopped responding to clicks at all.

3. **The same queue, pumped once per frame from the main loop** -
   `RanInput_PumpButtons`. Independent of how any stage reads its input, and
   every down and every up is visible to at least one frame, in order. A double
   click takes four frames, about 130ms at 30fps, well inside the client's
   double-click window.

This also fixes a pre-existing problem rather than just enabling the new one: a
fast tap could always deliver both halves between two polls and be lost. That is
why `adb shell input tap` on a skill slot did nothing and the slots looked
broken, when the hit test had been right the whole time.

**Still missing: camera rotation.** Long-press-drag delivers a right drag, but
that does not rotate the view - the camera was unchanged across the gesture
(`out/rot0c.png`, `out/rot1c.png`), so whatever rotates the RAN camera is not
right-drag and has not been found yet.

## Round controls, a repeating sweep, and auto-target

### The corner sweep had to stop being one-shot

The quest icon kept sitting under the attack button however many names were added
to the move list, because it does not exist when the list is applied. Half the
corner furniture is like that - the quest alarm, the pet and vehicle status
boxes, the booster bar are all created or shown once you are in the world.

`MobileArrangeInterface` now runs on a one-second timer instead of once, and a
bounded rect sweep adopts anything small still sitting in the bottom-right
corner. Bounded matters: an earlier unbounded version swallowed the 231-wide
booster bar and shoved the whole icon row into the middle of the screen, so the
sweep now ignores anything wider than 64. Adopted ids are remembered, because
after the move the control is no longer in the corner for the next sweep to find.

Two related fixes fell out of the same pass:

* **The row is bottom-aligned, not centred.** The two notification buttons are 59
  tall against 35 for the rest, and they draw their icon at the *bottom* of the
  box with the space above reserved for a banner. Centring the boxes left those
  two icons visibly low; aligning the boxes by their bottom edge lines the
  artwork up.
* **Hidden controls no longer reserve space.** Laying one out anyway left a hole
  in the row - which is what put the gap between the quest icon and the rest.

### Round skill slots

The slots are still the client's own square controls; the overlay draws an opaque
rim over each one, thick enough to cover the corners of the square underneath,
which leaves a circular window onto the icon. It has to be an overdraw rather
than a mask because the overlay renders after the client - there is no way to get
anything behind the icon.

Two things went wrong on the way:

* `SetTexture(NULL)` looked like the way to drop the square frame. A control with
  no texture does not draw nothing, it draws an *untextured quad*, so every slot
  became a flat white disc. The frames are back and the rim covers them.
* The rim is wider than the slot, so the arc had to open up to match - sized off
  the slot alone, the rims overlapped. The spacing that matters is the rim's.

The rim also starts slightly inside the slot's half-width. Starting it exactly at
the edge left the square's four sides tangent to the circle and still visible.

Page arrows now sit outboard of the attack button and the tray's own page button
is hidden - a thumb already resting on attack can reach them, which a label off
the far end of the arc could not.

### Auto-target and PK

Two toggles up the right edge, mutually exclusive, lit in their own colour when
on - PK red, because it is the one you do not want left on by accident. Each
re-engages on a half-second timer rather than every frame.

**`FindNearTarget`'s plain overload cannot be used for this, and the attack
button had been getting away with it by luck.** That overload searches around
`m_vBotPos` and measures distance from `m_vBotPos` - the anchor the bot parks
itself on. For a player who is not botting that is a stale point, so the search
happens somewhere the player is not. The skill overload gets it right and uses
`m_vPos`; `MobileFindNearestMob` is that branch without the running-skill
machinery, and `MobileFindNearestPvP` is the same for players, keeping the
`IsPK_TAR` check that decides who may legally be attacked.

**Verified:** auto-target closed on a mob, attacked it, produced damage numbers
and left loot on the ground (`out/auto5c.png`, `out/tsel0c.png`); PK lights red
and switches auto-target off (`out/pk2c.png`); the quest icon is in the row and
all eight icons share a baseline (`out/c2tr.png`); the slots are round and no
longer overlap (`out/c3br.png`).

**Not working: the target name and health panel.** The six `TARGETINFO_*` and
`CROW_TARGET_INFO*` controls are moved to the top centre, but no panel has yet
been seen there after selecting a target - so either those are not the controls
that display it, or something else gates them. Unresolved.

---

# Current state — 2026-08-27, end of the touch-UI session

Written after a long session on the Galaxy Tab S9. Everything below is either
measured on a device or explicitly marked as unverified. Where I got something
wrong, that is recorded too, because the wrong turns cost more than the fixes.

## Verified working

Confirmed by watching the screen or reading a log, not by reasoning:

| thing | how it was confirmed |
|---|---|
| Stick movement, stop on release | crossed a bridge and steps unbroken; idle pose returns within a frame |
| Walk vs run by stick deflection | stride and distance differ; measured over 5s |
| No click marker while steering | ground clean across every movement frame |
| Attack button | attack animation, continuing across frames |
| Target selection sticks | three presses, same mob, killed it; two others untouched |
| Auto-target selects without fighting | mobs adjacent, character idle |
| PK toggle | lights red, switches auto-target off |
| Skill fires once per press | `PRESS` then one `CAST` in the log, 8s with no repeat |
| Pick-up button | box vanished from the ground; later found in the bag |
| Round skill slots, page arrows | on screen, no overlap |
| Corner icons in one bottom-aligned row | eight icons, quest icon included |
| Rows sit against the compass rose | measured `rcMap.left + sizeX - sizeY` = 1160 |
| Back opens the ESC menu | menu appears, process survives |
| Long press = right click | cleared a quick slot (`ReqSkillQuickReSet`) |
| Windows draw over the touch controls | inventory covers the skill ring |
| Close buttons close | tapped the X on อุปกรณ์สวมใส่ |
| Money amount prints | "เก็บเงินได้ '8' เหรียญ" — was "Id" |
| Fullscreen | status bar gone, game's own bar at the top edge |
| Device keyboard rises and stays up | `mInputShown=true` on both ID and Pass |
| Typing reaches the edit box | sent `x x 2 2`, field read `xx22` |

## Broken, with the evidence

### Item and money drops render as blank white quads

Seen on the tablet, in the world, two drops in frame — "ขนม…" and
"น้ำยาเพิ่ม MP (เล็ก)" — both drawn as **white untextured boxes**, with a black
blob above the character. White-untextured is the same signature as a control
drawing with no texture bound, so the suspicion is that the drop object's texture
never reaches the draw. Not yet traced.

### The loading screen never starts

`RanLoad` logged **nothing** while genuinely in the world.
`DxGlobalStage::ChangeStage` calls `StartThreadLOAD` unconditionally, and the
shim's `_beginthreadex` is a real `pthread_create`. The instrumentation added
this session sits *inside* `LoadingThread()`, which is too late to tell apart:

* `ChangeStage` is not on the path that entered the world, or
* `pthread_create` fails and the `E_FAIL` return is ignored.

**Next step:** log in `StartThreadLOAD` itself and on the `pthread_create`
result. One run answers it.

### Camera rotate / zoom "overlap"

Reported, not yet reproduced by me. One real cause was found and fixed - the
overlay left blending enabled while `RanGLR_InvalidateStateCache` memsets the
cache to "blend off", so the next `setBlend(false)` compared 0 with 0 and never
issued the `glDisable`, leaving opaque geometry blending. Whether that was the
whole of it is **unconfirmed**.

### Tapping outside an edit box does not close the keyboard

Cause found, not fixed. `EndEdit` is only called when switching between edit
boxes (`CUIEditBoxMan::StartEDIT`) or on OK/Cancel. Nothing ends editing when a
tap lands outside every box. Invisible on PC; on mobile the keyboard stays up.
Needs a mobile-only "tap outside the active box ends editing" rule.

### ANIPROBE instrumentation is still in the shipping build

Roughly 20 log lines a second from `DxSkinAniControl`, `SAnimation` and
`DxSkinAniManThread` - nine `#ifdef RAN_MOBILE` blocks left over from the
animation work. Each is rate-limited to one line a second, but there are several
per skeleton. Should come out.

## Fixed this session, with the mechanism

These are worth keeping because the mechanism was the surprising part:

* **`ShowCursor` returning a constant hung the game.** `CCursor::SetShowCursor`
  loops `while (nShow > -1) nShow = ShowCursor(FALSE);` against Win32's cursor
  *counter*. The shim returned 0 always, so it spun at 100% until Android killed
  the app. It only bit on camera rotation, because the middle-drag branch is the
  one place that asks for the cursor to be **hidden**.
* **The overlay corrupted the client's VAOs.** `glVertexAttribPointer` records
  into whichever VAO is bound; the overlay had none of its own, so it pointed the
  client's VAO at its two-float buffer. Harmless while the overlay drew last -
  moving it under the interface armed it. Then, with a VAO but no
  `glBindBuffer`, `glBufferSubData` wrote into the client's bound buffer,
  because **the `GL_ARRAY_BUFFER` binding point is not VAO state**.
* **`%Id` is an MSVC format too.** The rewriter only handled `%I64`, and its
  guard tested `strstr(fmt, "I64")`, so `PICKUP_MONEY` was never rewritten.
  See [[msvc-format-specifiers]].
* **`m_sRunSkill` is never cleared after a cast**, so a level-triggered check
  re-fired the skill every frame. It is edge-triggered off `ReqSkillRunSet` now.
* **`FindNearTarget`'s plain overload searches around `m_vBotPos`**, the bot's
  parking anchor - useless for a player who is not botting. Replaced with
  `MobileFindNearestMob` / `MobileFindNearestPvP`, anchored on `m_vPos`.
* **`STARGETID` carries a position** that goes stale; both `MobReaction` and
  `SkillReaction` measure range from it, so every press aimed at where the target
  had been. Refreshed each tick.
* **The shim's `CIMEEdit` had no input path at all** - `g_imeText` was only ever
  written by the client seeding a field. The on-screen keyboard was the only way
  text ever got in, so removing it made login impossible until
  `RanIME_InsertUtf8` / `RanIME_Backspace` were added.
* **`ANativeActivity_showSoftInput` does nothing under NativeActivity** - there
  is no View to focus. Goes through `InputMethodManager` by JNI now. The first
  working version used `toggleSoftInputFromWindow`, which *toggles*: moving from
  ID to Pass dismissed it.

## Still not started

* Thai text input. The keycode table covers ASCII only; Thai needs the composing
  IME, which is the real remainder of that job.
* Camera lock button (follow the character's facing). Free look works.
* Item tooltips do not render.
* Projected shadow texcoords.
* Gameplay sweep past the inventory: NPC dialogue, shops, trade, quest turn-in,
  death, zone change.
* The `68 นาที` number.
* Nothing committed since the GitHub push.

## Process notes — where the time actually went

Most of this session went into the test harness rather than the game, and much of
that was self-inflicted. Recording it so it is not repeated:

* **I broke `login-tab.sh` myself.** It tapped the coordinates of the client's
  on-screen keyboard; removing that keyboard meant those taps hit the *Android*
  keyboard and typed "gguu" into the ID field. It types with key events now, and
  dismisses the IME with Back before pressing OK.
* **`logcat -c` does not reliably clear every buffer**, so waits matched stale
  lines from the previous run and the script tapped character-select before it
  existed. `--pid` looked like the fix and is rejected on this device
  ("pid out of range"); what actually works is confirming the process is dead
  before clearing.
* **`am force-stop` does not always take**, so the log was cleared *after* boot
  and the boot marker never reappeared.
* **I misread my own logs.** I grepped with `-avE "FRAME "` and then counted
  `FRAME sections`, concluded the client had never reached the world, and spent
  several cycles on the login flow while the game was sitting in the world the
  whole time.
* **A patch script corrupted `android_main.cpp`.** `'$'` in a replacement string
  is `$'` - a JavaScript `String.replace` special pattern meaning "everything
  after the match" - which spliced the whole file tail into the middle. Caught in
  the build errors and repaired. Escape `$` or use a function replacement.

The pattern behind all of these: I acted on a plausible theory before measuring.
The fixes that landed cleanly are the ones where I read the mechanism first -
`SetShowCursor`, the VAO binding, `m_sRunSkill`, `m_vBotPos`. See
[[no-guessing-verify-facts]] and [[port-methodology-master-pc-source-first]].

---

# 2026-08-29 — camera separation, and the white drops

Everything here was measured on the LDPlayer emulator (x86_64, 2560x1440) with
the shipping APK, not reasoned about.

## Item and money drops render as blank white quads — FIXED

**Cause: a use-after-free in the shim's `.x` loader.**

`D3DXMATERIAL::pTextureFilename` pointed into the parsed `XFile`'s string
storage, and `loadFromBytes` did `delete file` before returning — so every
texture name was dangling the moment the caller got it:

```cpp
HRESULT hr = meshFromNode ( mesh, ... , ppMaterials, ... );
delete file;                 // "the mesh copied what it needs" — it had not
return hr;
```

Whether it mattered came down to the allocator. Read the freed block soon
enough and the old bytes were still there and the texture loaded; read it once
the block had been reused and `strlen` found a `0`. `DxSimMesh::Create` then
copied that **empty, non-null** string, `LoadTexture` failed, and
`DxSimMesh::RenderItem` — which unlike `Render` has no no-texture guard — drew
the subset with no texture bound. The shader's `tex = vec4(1.0)` fallback
modulated against a white material: a flat white box.

That intermittency is why it looked like a data problem. It is not: the data
is fine. `money_1.x` carries `coin_tex_a.dds`, and `coin_tex_a.dds` is a clean
128x128 DXT1 with 8 mips.

**Fix:** the names now live *inside* the returned material buffer, laid out
after the `D3DXMATERIAL` array — which is what real D3DX does, and why callers
may hold those pointers for as long as they hold the buffer.

**How it was found.** Three steps, each ruling something out:

1. Built the shim's own `xfile_parse.cpp` into a standalone Android binary and
   ran it against the real `money_1.x`. It printed `coin_tex_a.dds`. The parser
   was not the problem.
2. Swept the whole corpus on the device with the same binary — 941 files in
   `data/object` (1598 materials) and 191 in `data/skinobject` (7242). Result:
   `empty=0`, `shortData=0`. 514 and 6249 materials respectively have no
   `TextureFilename` node at all, which is legitimate for effect meshes. **No
   file anywhere produces an empty name.** So an empty name at runtime had to be
   created at runtime.
3. Made the runtime probe name the mesh instead of deduping by texture name —
   the old probe collapsed every failure into one useless `[]` line. One run
   then said it outright:
   `untextured draw: mesh=[Money_1.X] subset=0 of 1 tex=[]`
   The same file that scanned clean offline. That gap is only explicable by
   lifetime, and `delete file` was two calls up.

**Verified:** killed mobs until money and items dropped. Coin pile and treasure
chest render with their real textures; `RanTex` silent across the whole run.
Pick-up then cleared them, and the chat read `เก็บ ขนมปัง ได้`,
`เก็บเงินได้ '6' เหรียญ`, `เก็บ น้ำยาเพิ่ม MP (เล็ก) ได้`.

## Zooming also rotated the camera — FIXED

A pinch is two fingers moving. The gesture layer read that movement as a drag,
and a drag with no control under it presses the **middle** button, which is the
client's camera-rotate binding (`DxViewPort::FrameMoveMAX`, `dwMOUSEKEY_M &
DXKEY_DRAG`). So every zoom turned the view at the same time.

`RanTouch_IsPinching()` now reports the pinch, and the gesture layer refuses to
start a drag during one — and releases a drag already in flight when the second
finger lands, or the rotation continues through the whole gesture.

**Verified with numbers, not pixels.** A temporary probe logged the camera's
own inputs and yaw per frame. Across a full pinch:

```
dx=7 dy=0 dz=120  M=0x1  vRot=(0.0000,0.0000) zoom=3.4560  yaw 1.571 -> 1.571  (d=0.0000)
...
dx=7 dy=0 dz=240  M=0x1  vRot=(0.0000,0.0000) zoom=14.7718 yaw 1.571 -> 1.571  (d=0.0000)
```

Zoom ran 3.4 to 14.8; yaw did not move at all. And the converse, a one-finger
drag:

```
dx=49  dy=0 dz=0  M=0x8  vRot=(0.0000,0.1539) zoom=0.0000  yaw  1.571 -> 1.417
dx=195 dy=0 dz=0  M=0x8  vRot=(0.0000,1.0668) zoom=0.0000  yaw  0.310 -> -0.756
```

Rotates (`M=0x8` is `DXKEY_DRAG`), and `zoom` stays exactly `0.0000`. The two
gestures are now cleanly separated in both directions. The probe has been
removed.

Note for anyone re-testing this: **the compass rose is not a yaw indicator.**
It is perfectly static frame to frame (measured: 0.00 difference over 3s with
no input), which makes it look like a good one, but it also changes on zoom.
Reading it as yaw says "the zoom still rotates" when the yaw number says it
does not. Measure the yaw.

## The loading screen — it was always working

Captured it this session: full art, the map name `< สถาบัน SG >`, the HINT
badge, the spinner and the copyright line, between character select and the
world. `RanLoad` traces the whole path — `ChangeStage entered, to=2`,
`StartThreadLOAD called tex=[loading_054.dds]`, `thread handle = 0x...`,
`context acquired, rendering`. Nothing to fix.

## Test harness

`login-emu.sh` was still tapping the coordinates of the client's own on-screen
keyboard, which no longer exists — the same breakage `login-tab.sh` had. It
types with key events now. Its start-button coordinate was also wrong
(`2473,500`; the button is at `2413,707`), and it waited on a log marker
(`FRAME sections`) that no longer exists, so it "timed out" while sitting
happily at character select.

Two throwaway tools proved useful enough to keep in mind: an Android binary
built straight from `shim/d3d/xfile_parse.cpp` for reading real `.x` files
offline, and `rectdiff`, which compares a rectangle between two raw
`screencap` dumps. Both live in the scratchpad.

## Camera lock button — ADDED

There is nothing to port here: RAN’s camera is free-look only, turned by
dragging the middle button, and it never follows the character. So the lock is a
mobile addition — but expressed in the client’s own terms rather than beside
them. It feeds `CameraRotation` exactly the way a drag does, so zoom, the
collision pull-in and the pitch limits all keep working untouched.

A seventh touch button joins the toggle stack up the right edge, blue so it
reads apart from green auto-target and red PK. While it is on, each frame takes
the signed angle from the camera’s heading to the character’s — through a cross
product, so it lands in (-pi,pi] with no wrap-around case to get wrong — and
eases the camera along it. The easing is frame-rate independent
(`1 - exp(-6*dt)`), because this runs anywhere from 8 to 60 fps on the devices
in hand.

**Verified.** With the lock off the follow code never runs at all (no trace
lines). With it on:

```
camlock: camYaw=1.571 charYaw=0.844 delta=-0.727 step=0.4593
camlock: camYaw=0.844 charYaw=0.844 delta=-0.000 step=0.0001
camlock: camYaw=-0.050 charYaw=-0.052 delta=-0.003 step=0.0014
camlock: camYaw=-0.052 charYaw=-0.052 delta=-0.000 step=0.0000
```

It converges onto the character’s heading, holds there, and re-acquires when the
character turns again. The probe has been removed.

## Full sweep on the shipping APK

One pass with everything on: camera lock, auto-target, 40 attacks, five
pick-ups, a pinch each way and a drag rotate. Result: 0 untextured draws, 0
`FATAL`/`signal 11`, `glErr=0x0000`, 31 fps, process alive. Chat read
`เก็บเงินได้ '8' เหรียญ` and `เก็บ ขนมปัง ได้`.

## Item tooltips — they were never broken

The standing note said item tooltips do not render. They do. Hovering a bread
stack in the inventory brings up the full panel in correct Thai — `ข้อมูลสำคัญ`,
`ชื่อ:ขนมปัง`, `จำนวน:4/999`, `ต้องมีเลเวล:1`, `HP Recovery:60`, the
can-sell / can-drop / can-store list, and the CTRL+Mouse-R chat-link hint.

It persists correctly too: `UIMSG_MOUSEIN` is purely positional
(`CUIControl::MouseUpdate` is a rect test), and the shim leaves the pointer
where the finger left it, so after a tap the tooltip stays up on its own. It was
still on screen after six seconds with nothing touching the display.

What made it look broken was the measurement, twice over. A press held long
enough to take a screenshot crosses the 450 ms long-press threshold, and the
right-click that follows replaces the tooltip — so every attempt to photograph
it destroyed it. And the diagnostic left in `SHOW_ITEM_INFO` was capped at
eight lines, which the press alone exhausted; its silence afterwards read as
"not called" when it was only "not logged". Uncapping it showed the call running
every frame while idle. Both probes are now removed.

## Backspace did nothing in a text box that already had text — FIXED

Reported from the tablet: characters could not be deleted in an edit box.

`CIMEEdit` kept the caret in a file-static `g_imeCaret` in the shim. But
`IMEEdit.h` defines the getter **inline** — `int GetInsertPos() { return
m_xCaretPos; }` — so the client read a member the shim never wrote. It was
always 0.

That 0 did not stay harmless. `CUIEditBox::FrameMove` reads `GetInsertPos()`
into `m_xInsertPos`, and `BeginEdit` pushes it straight back:

```cpp
SET_STRING_BUFFER ( m_strText );   // shim: text set, caret = end
DXInputString::GetInstance().OnInput ();
SetInsertPos();                    // pushes m_xInsertPos (0) back -> caret = 0
```

So focusing a field that already held text pinned the caret to the front, where
backspace correctly has nothing before it to delete and typed characters go in
at the start. An **empty** field worked perfectly, which is exactly why every
earlier test passed — the login boxes were always typed into from empty.

Two changes, both needed:

* The caret now lives in `m_xCaretPos`, the member the inline getter exposes,
  so reads and writes agree. The `RanIME_*` free functions reach it through the
  public accessors on the `CIMEEdit` that last touched the buffer.
* `BeginEdit` puts the caret at the end of the existing text under
  `RAN_MOBILE`. On a desktop the caret starting at 0 is harmless because
  clicking into the text positions it; with a finger and a soft keyboard there is
  no such gesture, so it has to start where a keyboard user expects.

**Verified on the emulator against the exact failing case:** typed `abcd` into
the ID field, moved focus to Pass, tapped back into the now-filled ID field, and
pressed backspace twice. Field read `ab`. Before the fix that deleted nothing.

## Touch UI pass — five items from the tablet

**The item tray collapse arrow is gone.** It was the small arrow alone against
the left edge. Finding it took a control census logged from
`MobileArrangeInterface`: at logical 1280x720 it sits inside id=3
`LEFTTOP_CONTROL_GROUP` (rect `(0,41) 41x415`), whose children are the potion
tray, the level display and `QUICK_POTION_TRAY_OPEN_BUTTON` — the arrow.
`CUILeftTopGroup::Update` now keeps it hidden and the tray open under
`RAN_MOBILE`. Collapsing a tray buys screen back for a mouse user; on a touch
screen it is one more thing to mis-tap, hiding something that has to stay
reachable.

The same census also showed `QUICK_SKILL_TRAY_OPEN_BUTTON` still listed as
visible after `SetVisibleSingle(FALSE)` — `MobileCollectIn` skips anything
invisible, so that flag is plainly not what `IsVisible()` reads. It goes
through `HideGroup` now, which is what the client uses for that button itself.

**The stick is smaller.** `g_stick.radius` is `g_unit * 0.72f` instead of
`g_unit`. The ring is only a hint of where the thumb rests — it re-centres
under the finger anyway — so a big one just covered the world without steering
any better. Its centre did not move, so it stays level with the attack button.

**Both thumb clusters and the chat are off the bottom edge.** The last strip of
the screen belongs to the system: the gesture handle sits there even in
immersive mode, and the band above it is a system gesture inset, so touches are
taken for back/home before the app ever sees them. The chat tabs are the bottom
row of that window, which is exactly what could not be tapped. `layout()` now
subtracts a `g_height * 0.04f` bottom-safe margin, and the chat is placed
`fH * 0.05f` clear of the edge.

**The controls are drawn in the game's own idiom.** RAN's windows and buttons
are a dark, slightly blue charcoal panel behind a thin bright rim, with a darker
line outside it. Every control is built from exactly that now, at whatever
radius it happens to be — one `frame()` helper — so the stick, its knob, the
attack button, the mode toggles, the page arrows and the skill slot rims read as
one set, and as the same set as the MENU button and the window frames beside
them. The glyphs moved into a `glyph()` helper so the lit and unlit paths
cannot drift into drawing different marks, and a lit toggle keeps its frame and
only changes what fills it.

**Verified together on the emulator in one run:** arrow gone from the left edge,
stick visibly smaller, chat clear of the bottom, every control wearing the new
frame.

### Startup loading screen — wired, not yet confirmed

There is no way to draw one from inside the game. `m_pd3dDevice` is still NULL
right through `DxGlobalStage::OneTimeSceneInit`, and the lobby stage is entered
by assigning `m_emThisStage = EM_STAGE_LOBY` directly rather than going through
`ChangeStage`, so `StartThreadLOAD` never runs at startup — confirmed by
`RanLoad` logging nothing at all across a whole boot. The black period is
before any device exists.

So it is an Android window background instead. `loading_002.dds` — the image
`DxGlobalStage` already names for the lobby stage — was decoded to PNG with a
small DXT1 decoder written for the job, and is now
`res/drawable-nodpi/splash.png` behind a `RanSplash` theme; `build-apk.sh`
gained an `aapt2 compile` step for it. The resource is in the APK and the
manifest resolves the theme, but I could not see it on the emulator: the window
is covered there within half a second. It should show on the tablet, where
startup is slower. **Unconfirmed — needs a look on the device.**

## Touch controls restyled to the Claude palette

The engine-matched grey-metal look was rejected. The controls now use Claude's
design language: warm neutrals with a single terracotta accent, spent only where
it carries meaning rather than as decoration.

| control | treatment |
|---|---|
| Attack | solid terracotta `#D97757` — the primary action, the only control wearing the accent by default |
| Stick | warm-black well, cream knob; the knob takes the accent while it is actually being moved |
| Skill slots | warm-black rim with a cream ring |
| Page arrows, pick-up | warm-black discs, cream glyphs |
| Auto-target lit | terracotta |
| PK lit | deep clay `#AA5135`, the one colour that reads as "careful" |
| Camera lock lit | muted slate `#5C7A99` — a view setting, not a combat one |

### The light palette did not survive contact with the game

The first pass used Claude's *light* surfaces: cream fills, warm-grey rims, dark
ink glyphs. Correct on a page, wrong over game footage. On the pale market
pavement the buttons washed out almost entirely — the mode toggles were barely
findable in the screenshot, and a control you cannot see is worse than an ugly
one.

The fix was to take the dark half of the same palette rather than abandon it:
warm-black `#262624` surfaces at 0.66 alpha, with cream promoted from fill to
rim and ink. Same identity, same accent, same hierarchy — but it holds against a
bright street and a dark interior alike, which is the only test that matters
when the background is arbitrary.

Both states were confirmed in one frame: bright pavement and dark brick in the
same screenshot, with auto-target and camera lock switched on so a lit toggle
and an unlit one could be compared side by side.

## Gameplay sweep — first results

Taken on the emulator in one session, with the restyled controls in place.

| thing | result |
|---|---|
| Zone change through a portal | works — market to the SG institute interior, geometry and lighting correct |
| NPC dialogue | renders and responds; the OK button drove the zone change |
| NPC hover tooltip | renders (`รถจิ๋ว [Npc Type]`) |
| Area messages on entry | arrive in chat (tax rate lines) |
| Untextured draws across the whole sweep | 0 |
| Crashes | 0 |

**One thing to look at next:** on zone entry a second, largely empty dark panel
appears across the top centre carrying the area message
(`อัตราภาษีของพื้นที่นี้คือ 5.00`). It is not obviously wrong — it may be the
region notice banner behaving normally — but it is big, mostly empty, and sits
where nothing else does. It was not present before the zone change. Worth
identifying before deciding whether it needs moving like the rest of the corner
furniture.

Still unswept: shops, trade, quest turn-in, death.

## Tablet session — everything below was verified on the Tab S9 itself

The lesson of this round: the emulator is 2560x1440 and the tablet 2560x1600,
and they are **not** the same layout. A fix confirmed on the emulator and
reported as done was still visibly broken on the device. Nothing here is claimed
without a capture or a log line from the tablet.

### Tapping chat killed the game — FIXED

`SIGABRT` in `onAppCmd`, an ART abort, i.e. a JNI error:

```
F com.ran.native: runtime.cc:761] "Signal Catcher" ...
  native: #13 ... ((anonymous namespace)::onAppCmd+432)
F libc: Fatal signal 6 (SIGABRT), code -1 (SI_QUEUE)
```

`goFullscreen` calls `setSystemUiVisibility`, which throws on this device - a
View method off the UI thread. The newly added `WindowInsetsController` block
then called `GetObjectClass` **with that exception still pending**, and ART
aborts the process the moment any JNI function is entered in that state. It
fired on every focus change, which is exactly what raising the soft keyboard
does - so tapping chat killed the game.

Clearing the pending exception between the two blocks fixes it. Verified: open
the chat input, type, send, tap the box - process alive throughout, zero
`Fatal signal`.

### The bars are gone — FIXED

The legacy `setSystemUiVisibility` path never worked here (see above: it
throws). `Window.setDecorFitsSystemWindows(false)` plus
`WindowInsetsController.hide(systemBars())` and
`setSystemBarsBehavior(BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE)` does. The gesture
pill is absent from every capture after the fix. Exceptions are logged now
instead of silently swallowed - swallowing them is why this failed invisibly for
so long.

### The left-edge arrow — FIXED, and it was never a tray collapse

It is `MINIPARTY_OPEN` (id 10), at logical `(0,440) 41x16`. Every earlier
attempt hid the skill tray and potion tray arrows, which is why it survived them
all: it was neither.

Finding it needed a census that could see **child** controls.
`CInnerInterface::MobileCollectIn` walks only the top-level container, and the
arrow is a group child, so it was invisible to every sweep built on it.
`CUIControl::MobileCollectTree` (virtual, overridden in `CUIGroup`) walks the
whole tree; one run on the tablet named the id outright.

Also worth keeping: `SetVisibleSingle(FALSE)` does **not** change what
`IsVisible()` returns - a census taken immediately after that call still listed
the control as visible. Guarding a hide on `IsVisible()` therefore makes it a
no-op. Both hides are unconditional now, every sweep.

### The boot screen — ADDED, then made to match, then made to animate

Three separate problems, in order:

1. **Nothing was drawn at all.** No loading screen exists at startup by design:
   `m_pd3dDevice` is NULL right through `DxGlobalStage::OneTimeSceneInit`, and
   the lobby stage is entered by assigning `m_emThisStage` directly rather than
   through `ChangeStage`, so `StartThreadLOAD` never runs - `RanLoad` logs
   nothing across a whole boot. An Android window background does not work
   either: with a NativeActivity the surface is created and painted black
   immediately, and a capture 0.7s after launch was black with the theme
   correctly linked and the drawable packaged. It is drawn directly on the GL
   surface now, before `RanApp_Boot`.
2. **It looked like a different screen.** It draws the same pieces in the same
   1024x768 virtual layout as `NLOADINGTHREAD`: `ld_top`, the lobby art,
   `ld_under`, the HINT badge and the `ld_back` ring.
3. **The spinner did not move.** `loading_st.dds` is four 105x105 frames along a
   512x128 sheet, indexed exactly as the client does it
   (`left = (step %% 4) * 105`). The first attempt drew all four at once because
   a shader edit had silently failed to apply - `String.replace` does not error
   when the pattern does not match, so the `uUV` uniform never existed and the
   quad sampled the whole sheet.

   It advances on real progress rather than a timer, which is what
   `LOADINGSTEP::SETSTEP` does for the in-game screen. The first attempt ticked
   only between the coarse steps in `RanApp_Boot`, which all finish in the first
   second - measured: the spinner region was pixel-identical between 2s and 5s.
   Ticks inside `DxGlobalStage::OneTimeSceneInit`, where the time actually goes,
   took it from 5 frames to 9 across a 12s boot, with the spinner region
   measurably changing (13.3%% of pixels between 4s and 6s).

### Chat follows the keyboard — ADDED

`windowSoftInputMode=adjustNothing`, deliberately: letting Android resize the
window churns the surface, and the client is not built to be resized mid-frame.
So the chat asks how tall the keyboard is and moves itself, every frame rather
than on the one-second sweep, or it would lurch up a second late.

The height comes back as a **fraction of the window**, not pixels. The first
attempt returned device pixels and scaled by `RanGL_Height()` - which reports
the client's *logical* size, not the panel's - so the two disagreed by the UI
scale factor and the chat flew off the top of the screen. A ratio has no units
to get wrong.

Verified both ways on the tablet: keyboard up, the chat sits directly above it
with its tabs and input line visible; keyboard down, it returns to the bottom.

## Particle effects: RAND_MAX is not 32767 everywhere

`RANDOM_01` in `DxVertexFVF.h` was `((FLOAT)rand())*0.000031f`. That literal is
`1/32767` — MSVC's `RAND_MAX` — and the author's own derivation is still sitting
one line above it, commented out. bionic's `RAND_MAX` is `2^31-1`, so on Android
the expression returned up to ~66000 rather than 0..1.

Every particle built from it came out thousands of times too large: sizes of
20000-85000 units instead of ~2, so a single billboard spanned the whole view.
Drawn additively with a near-black colour, that is the full-screen magenta wash.
Lifetimes were scaled the same way, so `m_fRate = m_fTime/m_fLife` stayed at 0 —
the alpha ramp never rose and particles never expired.

Found by bisecting the frame with `/sdcard/ran/drawlimit` down to draw #118, then
logging quad spans at the point each of the five effect classes writes its
vertices, which named `DxEffCharParticle`. The fog colour, the sky dome, DXT1
decoding and the buffer-upload path were each eliminated by measurement first,
and every one of them was a wrong guess.

Fixed under `RAN_MOBILE` by dividing by the `RAND_MAX` actually in force. The
Tab S9 went from 15-19 fps to ~55: those quads were overdrawing the whole screen
every frame.

## Drawing at panel resolution

How large to lay the GUI out, and how many pixels to draw it with, are two
questions that used to share one answer: the frame buffer was shrunk to 1280x800
and stretched by the display, making everything on screen a 2x nearest-neighbour
blow-up. That was the blur.

Now the buffer is the full panel and `RanGL_UIScale` carries the ratio — the
viewport and scissor paths already multiplied by it, and touch already divides by
`InputScale`, the same number. The client still lays out at 1280x800, so the GUI
stays finger-sized. Measured after the particle fix: 16-19 ms a frame at
2560x1600, so it costs nothing that was not already being wasted.

## Skill press during an attack

`MobileCastRunSkill` cleared the press flag before testing `IsACTION(GLAT_ATTACK)`,
so a skill pressed mid-swing was eaten with nothing to retry it. Tapping attack
repeatedly holds `GLAT_ATTACK` almost continuously, so skills stopped coming out
at all with nothing on cooldown. The press now opens a 0.75 s window, mid-swing
counts as busy rather than refused, and the attack button stands aside while a
skill is queued. Not yet confirmed on device — the test character has no skills
in its quick slots.

## Data on the tablet

`push-data.sh minimal` had left `data/map` at 12 of 450 files and `sounds` at
zero, which is why most maps did not load. Both are now pushed in full (3.06 GB
and 266 MB).

`adb push` needs `MSYS2_ARG_CONV_EXCL='*'` set, or the destination `/sdcard/...`
is rewritten to a Windows path and the push reports success while moving nothing.
That is the opposite of `build-apk.sh`, which breaks when the variable *is* set.

The `.enm` skin misses in the log are harmless: those files are not in the PC
client either.

## Four moons: the second texture coordinate set was never read

`moon.dds` holds four phases in a 2x2 grid. `DxSkyManDayNight` binds the same
texture to stages 0 and 1, and picks tonight's phase by writing quadrant UVs into
`vTex02` — coordinate **set 1**. Stage 0 samples set 0 for colour and stage 1
passes that colour straight through (`SELECTARG2`/`CURRENT`), contributing only
alpha: `MODULATE(TEXTURE, DIFFUSE)` sampled with set 1. That alpha is the mask
that cuts the sheet down to one phase.

The shim only ever read the first coordinate set off a vertex — there was no
second UV attribute at all — so the mask was the whole sheet and all four moons
showed at once.

Added `aUV2`/`vUV2` (attribute 5, fed from the second set when the FVF declares
one, in both the attribute-format and classic layout paths) and a stage-1 mode 4:
alpha comes from the stage 0 texture sampled at set 1. Matched narrowly in
`d3d9_impl` on one texture bound to both stages with exactly that op pair, so it
cannot catch anything else.

Also fixed while in there: `RanGLR_SetStage1` mapped only modes 1 and 2, so mode
3 — the camera-space reflection addressing — silently became 0 and never reached
the shader branch that implements it.

Not yet confirmed on device: the moon only draws at night (`m_fAlpha_Night`), and
in-game time was 07:20.

## Sharpness is a setting

> Later measurement: resolution is NOT what costs frames on the Tab S9 - at
> renderscale 2 it still runs 31-33 fps against ~30 at full res. The setting is
> a taste control only; there is no speed to buy by turning it down. See "What
> the frame rate is actually made of" below.

Full-panel rendering makes geometry and text sharper but magnifies art authored
at 1024x768 further, which can read as softer. Rather than pick for the player,
`/sdcard/ran/renderscale` chooses: 1 draws at the full panel (default), 2 draws
at half and lets the display stretch it, which is what the build did before. The
GUI is laid out at the same size either way, so only sharpness changes.

Verified by launching and reading the boot line, which needs no login:

    renderscale 1 -> drawing 2560x1600, laid out 1280x800 (UI scale 2)
    renderscale 2 -> drawing 1280x800, laid out 1280x800 (UI scale 1)

Measured against the worry that the scene might be rendering into an off-screen
target and being magnified back up regardless: about a quarter of the frame's
draws go into render targets, but the largest is 512x512 (character composition),
not the scene. So drawing at panel resolution does reach the 3D.

## Text: glyphs were rasterised at the size they are laid out, not drawn

The client asks for text in its own logical pixels. With the frame at the full
panel every glyph quad is magnified by `RanGL_UIScale` on the way to the screen,
so rasterising at the logical size and magnifying is what made all text soft.

`d3dx_font.cpp` now rasterises each glyph at `scale * RanGL_UIScale()` and draws
it into a quad of the same logical size as before, which puts the bitmap at
roughly 1:1 with panel pixels. Only the bitmap changed: `advance` still comes
from `TtfFace::Advance` at the logical scale rather than from the oversized
bitmap, because that number is what every label in the interface is measured
with and moving it would move the whole GUI. `w/h/bearing` became floats so the
division does not quantise glyph boxes to whole logical pixels.

The atlas grows with the factor (1024 -> 2048 at 2x) or it fills and later
glyphs draw blank; the client only creates one font object, so that is cheap.

Measured on the server-select title, same text both ways: edge energy 2.48 with
the old pipeline against 3.84 with the new one, and the difference is obvious at
3x zoom. Verified at the login screen, which needs no login.

## Icons: bilinear magnification is what made them mushy

Icons are authored small — an item icon is around 32 texels — and are drawn two
to three times that size on the panel. With plain bilinear sampling almost every
output pixel of them is a blend between two texels, which is exactly what "the
icons look blurry" means. Nothing was being downscaled: every GUI texture was
checked against its file and all upload at full resolution.

The fix is a sharper reconstruction rather than a different resolution. The
interpolation ramp is squeezed into roughly one output pixel, so a texel edge
still ramps where it genuinely falls between output pixels and is flat
everywhere else. Nearest sampling would also be crisp, but it puts hard
stair-steps back — which is the complaint this started from.

Applied in two places, because icons reach the screen by two different routes:

* the client's interface path, in the main fragment shader, gated on
  `uPreTransformed` so world geometry (as often minified as magnified) is left
  alone. Texture sizes are recorded at upload in `g_texDims` and fed to the
  shader per draw, memoised on the texture name.
* the touch overlay's own icon shader, which draws the arc's skill icons and
  never went through the renderer's shader at all. It derives the magnification
  from the actual disc radius and UV span rather than assuming a factor.

`/sdcard/ran/nouisharp` turns the client-path filter off live for comparison.

Two shader traps on the way in, both caught by reading the log rather than the
screen: `uPreTransformed` had to be declared in the fragment stage too, and then
qualified `highp` — the vertex stage defaults an int to highp and the fragment
stage to mediump, and a uniform shared by both stages must agree or the program
will not link.

Measuring this needs care. Total variation across an edge is invariant to how
wide the blur is, so mean edge energy showed nothing; the difference is obvious
at 4x zoom on the item tray, where the slot borders go from soft grey ramps to
clean lines.

## Frame rate in a crowd: characters were being drawn three times

Measured on the emulator with the census the shim prints every 300 frames, in a
populated town:

    per frame: opaque 41 | alpha 22 | skinned 335 | ui 289
      of which off-screen: 140 skinned a frame

**Character drawing is the frame, and 40% of it was off-screen** — every
character rendered again into 512x512 targets. Two passes were doing it:

* **The water reflection.** `DxEnvironment::RenderRefelctChar` is the one place
  every character, pet and summon reflects through. It is also wrong here: the
  pass uses `SetClipPlane` to cut the reflection at the water surface and this
  shim has no clip planes, so what it drew was never clipped to the water. Now
  skipped; `/sdcard/ran/reflectchars` puts it back. Worth ~11 draws a frame -
  the player and pets, since only those reflect.

* **The shadow buffer**, which is the real cost. `DxShadowMap::
  RenderShadowCharMob` is the chokepoint for the player, other players, mobs,
  pets and summons alike, and each caster is a second full pass over the
  character. Now only the first few casters of a frame get a shadow. The client
  renders the player before the crowd, so the player keeps its shadow and the
  crowd gives theirs up.

The budget is read from `/sdcard/ran/shadowcount` (default 6, 0 disables), and
it behaves proportionally — measured at about ten draws per caster:

    cap=1 -> 11 off-screen skinned a frame
    cap=2 -> 14
    cap=4 -> 30
    cap=6 -> 58
    uncapped, same scene, no cap -> grows with every character on screen

In a crowd of thirty that is roughly 300 draws against 58. With character
shadows off entirely the emulator went from 74.0 ms to 60.6 ms a frame in a
scene that was not even busy.

Two caveats. The emulator's GPU is not the tablet's, so the *draw counts* here
transfer and the millisecond figures do not — this needs confirming on the Tab
S9. And `RAN_TIME_DRAWS` still has to be defined at build time before the
"submitting draws" line in the budget means anything, so it is not yet known
whether the remaining cost is submission or fill.

## Text input follows the keyboard layout

Key events were mapped through a hard-coded US-ASCII table, so only the
characters an account name is made of could ever be typed. They now go through
`KeyCharacterMap.get` for the device's actual layout first, and the codepoint is
encoded as UTF-8 for `RanIME_InsertUtf8`, which the edit boxes already take. The
table stays as the fallback for when the platform says nothing.

That covers any layout that sends key events. It is **not** all of Thai input: a
keyboard that composes — which most Thai IMEs do — commits through an
`InputConnection`, and a plain `NativeActivity` has none to commit to. That
needs a Java Activity of our own and a dex step in the build, which this port
does not have (`android:hasCode="false"`, no .java anywhere).

Regression-checked by logging in: the account name and password still type.

## Security: what the APK was shipping

* **`android:debuggable="true"` was in the shipped manifest.** Anyone with the
  APK could `run-as` the package, read everything it stores and attach a
  debugger. Removed; `DEBUGGABLE=1 ./build-apk.sh` puts it back on a temporary
  copy when a debugger is actually wanted. Verified: `run-as` now answers
  `package not debuggable`.
* **`allowBackup` was unset**, so it defaulted to on and `adb backup` could pull
  the app's private data off an unrooted device. Now `false`.
* **Unbounded accumulation in the receive buffer.** `CRcvMsgBuffer::addRcvMsg`
  bounded the single packet at `MAX_PACKET_SIZE` (2048) but never checked the
  running total against `m_pRcvBuffer`, which is 16384. Eight arrivals that have
  not been consumed fill it and the ninth `memcpy` writes past the allocation —
  reachable whenever the client stalls while the server keeps sending. Now
  bounded, dropping the packet the way the caller already handles.

  This one is **not** `RAN_MOBILE`-guarded, unlike the rest of the SOURCE
  changes. It changes MSVC behaviour only in the case that is currently a heap
  overflow, and leaving that in the PC build to preserve byte-identical
  behaviour seemed the wrong trade. Say if you want it guarded.

Noted, not changed:

* The whole data tree and the logs live under `/sdcard`, readable by any app
  with storage access. That is inherent to shipping several GB outside the APK.
* The `/sdcard/ran/*` diagnostic switches let any app with storage access change
  how the client renders. Harmless in itself, but they should be compiled out of
  a build meant for other people.
* 43 raw `strcpy`/`sprintf` calls remain in the client logic against 711 safe
  `StringCch*` ones. None were traced to a network-controlled source in this
  pass; that trace is still to do.
* The game protocol itself is unencrypted, which is how the original works.


## What the frame rate is actually made of — measured on the Tab S9

Two things I had been reporting were wrong, and both are corrected here.

**My own frame-budget line was reading half the truth.** It timed the interval
between `Present` calls and divided by 300 — but the client was calling `Present`
*twice* per rendered frame, so it reported ~18 ms while the game was really
taking ~37 ms. Wall-clock from the client's own counter is what settles it:
300 rendered frames in 11.03 s = **27 fps**, matching the on-screen readout.
Anything earlier in this document quoting a "frame budget" figure is per-swap,
not per-frame.

The double swap came from `CD3DApplication::Present`, which on PC presents the
four regions *around* the GUI rectangle so the GUI area is not copied again.
There are no partial presents here — every one is a full `eglSwapBuffers`, and
on a tiled GPU each resolves and flushes the tile buffer. Now one swap a frame
under `RAN_MOBILE`. Worth 27 -> 30 fps: real, but not the main cost.

**Resolution is not the bottleneck, so the sharpness/speed trade offered earlier
does not exist.** At `renderscale=2`, a quarter of the pixels, the tablet still
runs 31-33 fps against ~30 at full resolution. This is CPU-bound, not fill-bound.
Keep full resolution; it is not costing frames.

Where the time really goes, from the client's own profiler:

    FRAME 30.9 fps | 32.3 ms = update 2.0 + render 18.1 + present 12.1
    FRAME sections: world 12.5ms  w:mobitem 9.6ms  interface 2.7ms
                    w:land 1.2ms  w:chars 0.8ms  world-eff 0.7ms
    FRAME gl calls: 3750/frame = uniform 1536, texture 192, attrib 1240,
                    draw 708, buffer 57
    FRAME draws: 708 per frame, 14 us each

`w:mobitem` — `CLandManClient::Render_MobItem`, which draws every mob, other
player and dropped item — is 9.6 ms of the 12.5 ms of world time. `w:chars`, the
local character, is 0.8 ms. That section *is* the "many mobs and players" case.

And 708 draws at 14 us each is 9.9 ms, which accounts for essentially all of it.
So the cost is **draw submission**, not pixels: about 2.5 GL state calls per draw
(1536 uniform + 1240 attrib for 708 draws) on a driver where each one is not
free. That makes reducing the *number* of draws the right lever, which is what
the shadow cap does, and it is why cutting resolution changes nothing.

Frame times cluster at 30-33 ms, which is two vsync periods: the frame misses
the 16.6 ms budget and drops to half rate. Getting under it means roughly halving
per-frame submission work.

Next, in order of likely return:

* **Batch character pieces.** Every equipment piece and bone-combination
  attribute group is its own `DrawIndexedPrimitive`. Merging groups that share a
  material is the single biggest reduction available in `Render_MobItem`.
* **Cull by distance in `Render_MobItem`** before submitting, not after. A mob
  across the map still costs its draws today.
* **The interface is 399 UI draws a frame** in a busy scene (2.7 ms). The shim
  already batches UI quads; worth checking why so many survive batching.
* Per-draw state: 2.5 GL calls per draw is the multiplier on all of the above.


## The frame rate: measured cause, and why 100+ entities breaks it

Per-entity costs on the Tab S9, from the client's own profiler with counters
added to `Render_MobItem`:

    mi:mob-list 16/frame      mobs the client knows about
    mi:mob-seen 13/frame      mobs that survive the frustum test
    mi:mob-draw 11.9 ms       drawing those 13
    mi:mob-namecast 3.0 ms    the camera-to-entity raycasts for name display
    mi:pc-seen 0/frame        no other players present in this test

    796 draws a frame at ~20 us each

So **one visible character costs about 0.92 ms to draw and 0.23 ms for its name
raycast**, and takes roughly 40 draw calls. That is the number that matters,
because it multiplies:

    200 entities x 0.92 ms  = 184 ms a frame -> about 5 fps
    200 entities x 40 draws = 8000 draws     -> 160 ms of submission alone

A hundred mobs and a hundred players will not fit in a frame as this stands.
That is arithmetic from measured per-entity cost, not a guess.

### What is already correct, and was checked rather than assumed

* **Frustum culling works.** `GLCrowClient::Render` returns early on
  `IsVisibleDetect` and `IsCollisionVolume` before touching the device. The
  outer loop in `Render_MobItem` calls `Render` unconditionally, which looks
  wrong but is not - the cull is inside.
* **Resolution is not the cost.** At `renderscale=2`, a quarter of the pixels,
  the tablet still runs 31-33 fps against ~30. This is submission-bound.
* **`USE_SKINMESH_LOD` is not the answer.** The engine has a character LOD flag,
  commented out in `DxSkinDefine.h`, with distance and "more than ten already
  drawn at high detail" rules already written. But its only four read sites are
  `if (g_dwLOD == 0) ++g_dwHIGHDRAW_NUM;` - it counts, and never selects a
  cheaper mesh. Turning it on would change nothing.

### The fix, and why it has not been done yet

The cost is ~40 draw calls per character, submitted at ~20 us each. Everything
else is downstream of that. Three routes, in order of expected return:

1. **Merge attribute groups that share a material.** A skinned mesh is drawn one
   `DrawIndexedPrimitive` per bone-combination attribute group
   (`DxSkinMesh9_NORMAL.cpp`), and per equipment piece on top. Groups that share
   a material and fit one bone palette can be one draw. This is where the 40
   comes from and where it can most honestly be reduced.
2. **Finish the LOD the engine started.** The selection logic exists; what is
   missing is a cheaper thing to select. A distant character drawn as body only,
   without separate equipment pieces, would cut most of its draws.
3. **Drop the name raycast for distant entities.** 0.23 ms each, purely to
   decide whether a name is occluded. It is the cheapest win and needs no mesh
   work: skip it beyond a distance, or spread it across frames.

This has not been implemented. The measurement was the work of this session and
the refactor in (1) is not something to start without room to verify it - a
half-finished merge of attribute groups would be worse than the current state.

## The crash on entering a map: a bone list read past its own array (fixed 2026-08-30)

Reported three times: entering some maps killed the client. Every tombstone
landed on the same instruction,

    DxSkinMesh9_NORMAL::DrawMeshContainer+452
      ldr x2, [x9, x8, lsl #3]        ; ppBoneMatrixPtrs[iMatrixIndex]

with `x8` - the bone id - holding `0x74786554`, which is `"Text"`. A bone id is a
small index into `ppBoneMatrixPtrs`; that value is a fragment of some other
allocation, so the first two attempts chased the wrong thing:

1. A null `ppBoneMatrixPtrs[i]` (real, guarded, not this crash).
2. A dangling mesh container - a `DxCharPart` borrows `m_pmcMesh` from a shared
   `DxSkinPiece`, and `DeletePiece` frees the piece under everyone else. A live
   container registry did **not** stop the crash, because the piece is deleted
   and reloaded straight back into the same address: the pointer looked alive
   because a new container sat exactly where the old one had.

What settled it was refusing to guess a third time. A check in
`DrawMeshContainer` compared every id in the bone combination against
`pSkinInfo->GetNumBones()` and named the mesh when one did not fit:

    bad bone combination: mesh[Plane01[Mesh]] group 1/5 infl 16 bones 36 id0 0xd

The container is intact - name readable, group count sane - so nothing had been
freed. The bone list itself was wrong, and it is the shim that builds it.
`shim/d3d/d3dx_hierarchy.cpp` prunes a face that names more bones than the
palette holds:

    if (need.size() > kMaxInfluences) {          // 4
        ...
        for (size_t i = 0; i < kMaxPalette; ++i) // 16
            need.push_back(strength[i].second);

The test used the per-vertex influence limit (4) and the copy used the palette
size (16), so every face naming five to fifteen bones went down this path and
then read up to eleven entries past the end of `strength` - whatever followed it
on the heap, written into the bone combination table as bone ids. It faults only
when one of those values lands outside a mapped page, which is why it read as a
map-change bug: a map change is simply when new meshes load.

The fix is the constant and a clamp: prune against `kMaxPalette`, copy
`min(kMaxPalette, strength.size())`, and never write a group's bone ids past its
own row of the table. The guard stays in as a cheap net, and still logs.

Verified on the Tab S9 (xx11, GameMaster Lv150, the prison map, about forty mobs
on screen). Before the fix the guard fired on `Plane01[Mesh]` every frame and the
client died within a minute of entering; after it, two world entries, a four
minute soak and a round trip out to server select produced zero
`bad bone combination` lines and zero entries in `logcat -b crash`.

Two ownership fixes made while chasing this are kept, because both are real
even though neither was the crash:

* `DxSkinPieceContainer::DeletePiece` (and the delete inside `ReleasePiece`) now
  tell every `DxCharPart` and `DxAttBoneData` that borrowed the piece to drop it,
  instead of leaving them pointing at freed memory.
* `CreateMeshContainer` / `DestroyMeshContainer` keep a live-container set that
  `DxCharPart::Render` checks before drawing.

## Frame rate in a crowd: 27 fps -> 48 fps (2026-08-30)

Measured on the Tab S9 at 2560x1600, in the prison map with about forty mobs on
screen and roughly twenty of them visible. Every step below was measured on its
own, and the frame is unchanged pixel for pixel.

    start                          27 fps   35.7 ms   swap 14.3
    dynamic buffers via the ring   37 fps   26.3 ms   swap 12.6
    swap preserved only in loading 39 fps   25.6 ms   swap 11.9
    lighting and fog per vertex    39 fps   25.0 ms   swap 11.3
    specialised shaders            45 fps   21.7 ms   swap  8.5
    pose once a frame              48 fps   20.8 ms   swap  8.5

**The dynamic vertex buffers were stalling.** `DxDynamicVB` is a rolling pool -
it appends with `D3DLOCK_NOOVERWRITE` and starts over with `D3DLOCK_DISCARD` -
and the shim honoured neither, patching a buffer the GPU was still reading. That
cost 10 ms a frame for 295 KB, which is not a copy, it is a stall. The slices now
go into the persistently mapped streaming ring and the draw is pointed at them:
0.1 ms. Two things were tried first and are worse, so do not re-try them: sending
the whole buffer each time (15 MB a frame), and an unsynchronised
`glMapBufferRange`, which costs the same 120 us a call as `glBufferSubData` on
this driver.

**Preserving the swap is only for the loading screen.** The game frame redraws
every pixel; preservation makes a tiled GPU reload the whole colour buffer into
tile memory first, 16 MB at this resolution. It now follows the context handover,
so the loading screen still gets what it needs.

**Lighting and fog belong in the vertex shader.** D3D fixed function computes
both per vertex, so this also matches the PC client - per pixel it was eight
lights, a `pow` and a `distance` for every one of four million pixels.

**One shader per state, not one shader for every state.** This was the big one,
and the measurement that found it is worth keeping: `/sdcard/ran/plainfs` makes
the fragment shader return straight after the texture fetch, and that took the
swap from 13.4 ms to 8.1 ms - while `/sdcard/ran/fsprobe`, which pins any single
feature to its cheap path, changed nothing at all. A long shader costs even when
its branches are not taken, because fewer waves fit on the GPU and there is less
work to hide memory latency behind. So the state that changes the shader's shape
now picks a program compiled with those uniforms as constants. Twenty-two cover
the game frame.

**A character was posed twice a frame.** Every character is rendered once for its
shadow and once for itself, and each pass recomputed the skeleton - 74 us a
character, measured. Worse, `UpdateTime` advances the animation clock, so the
animation was being stepped twice as well. It is now done once a frame per
transform.

Ruled out along the way, so they need not be re-checked: the EGL config is not
multisampled (it is logged at startup now); per-section GPU timer queries
attribute nothing on a tiled GPU, because the fragment work all happens at the
flush; and character shadows, capped at six casters, are worth about 1 ms.

### What is left, and what it would take

The frame is now 20.8 ms: about 8.5 ms of GPU and about 12 ms of CPU. It is no
longer dominated by one thing.

* **60 fps** needs roughly 4 ms off each side. On the GPU that means less fill -
  the honest lever is rendering the world at less than the panel's 2560x1600
  while keeping the interface at full size, which is invisible where it matters
  and would roughly halve the fill. On the CPU it means the client's own scene
  walk (`world` 7.0 ms, `w:mobitem` 3.7 ms at twenty mobs).
* **120 fps** would also need the surface to run at 120 Hz; it is presently
  handed 60, and the panel supports 120.
* **The crowd case still has to be proven.** Twenty visible characters is not a
  hundred, let alone the three hundred an event brings. What the per-character
  numbers say is that posing is the cost that scales, and it is now half what it
  was; the next measurement to take is a real crowd, not another quiet map.

## The loading screen flicker was a second Present (fixed 2026-08-30)

Reported as "the loading page is flickering and does not disappear". It is not
the loading screen at all: the game presents the frame twice, and the second
swap puts up whatever buffer comes next in the chain - which still held the
loading screen, for as long as it took to cycle out.

Finding it took measurement rather than reading, because every intuition about
it was wrong:

* Screenshots taken from the host looked like the world and the loading screen
  alternating, but stills cannot tell a real alternation from a capture
  artefact. Sampling one pixel out of `screencap`'s raw output, twenty-four
  frames in a row, showed it was real: `34 50 58` (the loading art) and
  `38 37 29` (the ground) alternating irregularly, half a minute after the map
  had finished loading.
* It was not the preserved swap. Forced off for the whole run, the alternation
  stayed.
* It was not two threads presenting. The loading thread hands the context over
  cleanly - it presents frames 10118 to 10134, the game presents from 10135 -
  and only one load phase ever starts.
* It was not empty frames: every presented frame had drawn something, and the
  frame report showed a full ~350 draws each time.

Logging every present with its thread and the frame's draw count said it in one
line:

    PRESENT 13191 tid=14877 draws=349
    PRESENT 13192 tid=14877 draws=349   <- 1 ms later, nothing new drawn

`CD3DApplication::Render3DEnvironment` calls `Render()` and then `Present()`,
and `RanMobileApp::Render` - the override - was also presenting at its end. Two
swaps for every frame drawn. Removing the one in `Render` is the fix.

It was also the single most expensive thing in the frame, because each swap
resolves and flushes the whole tile buffer:

    before   45 fps   21.7 ms   swap 8.5 ms
    after    86 fps   11.5 ms   swap 1.3 ms

Verified on the Tab S9 in the same crowded prison map: twenty raw-frame samples
after the fix contain no loading-screen pixel at all, and the frame report reads
84-87 fps at 2560x1600 with about thirty mobs on screen.

## The crashes were a stray write, found with trapping bounds checks (2026-08-30)

Three crashes in unrelated places - `RanTexture::Release`, `DxSkinAniMan::DoInterimClean`,
and the font glyph cache - all faulted on a pointer of the same shape:

    fault addr 0xb400007000000000
    fault addr 0x0000007000000008

A live heap pointer here looks like `0xb400007X_XXXXXXXX`. These have the top
half intact and the low half zeroed, which is not a random value: it is a
four-byte zero written **onto** a pointer. One stray write, three victims.

### Getting a tool onto the device

AddressSanitizer was the obvious instrument and it does not work here.
Its runtime has to be loaded before the first allocation, which on a non-rooted
device means `wrap.sh` inside a debuggable APK - and this tablet never runs it
(a marker written from the script never appeared). Loaded late, as a dependency
of libran.so, ASan SIGILLs inside its own `AsanInitInternal`. `setprop
wrap.com.ran.native` is refused on a user build.

What did work: **trapping bounds checks**, `BOUNDS=1 ./build.sh`. Those need no
runtime and no wrap script - the compiler plants a trap at the offending access,
so the tombstone points straight at the line. They only see arrays whose size
the compiler knows, which is exactly the shape of the bug being hunted.

Four traps fired, one after another, all before the game even reached the world:

1. **`CRijndael::Initialize`** - `m_Ke` is declared `[MAX_ROUNDS]` and the key
   schedule fills and reads `m_Ke[0]` to `m_Ke[m_iROUNDS]` inclusive, which is 14
   for this client's 32-byte key. The last round key was written one row past the
   array, onto `m_Kd[0]`. Contained inside the object, but a 32-byte overrun on
   every encrypted file the client opens.

2. **`SlangFilter::addSlang`** - `sizeof` used as a character count on a
   `wchar_t` array. `wchar_t` is four bytes here and two on Windows, so
   `sizeof(buf)` is 1028 for a 257-character buffer: `_snwprintf` was told it
   could write 1027 characters, and the terminator went to `buf[1027]` - a
   four-byte zero written 770 characters past a stack buffer, for every word in
   the slang list at startup.

3. **`CPartyFinderSlot`** - `m_pClassImg[GLCI_NUM_6CLASS]` (12) filled to
   `GLCI_NUM_7CLASS` (14): two pointers written past the array, over the party
   data behind it, for every slot and on every device reset.

4. **`CInventoryPage::ResetAllItemSlotRender`** - and this is the one.
   `m_pItemSlotArray` holds `EM_INVENSIZE_Y` slots (eleven, since the row count
   was bumped from ten) and both callers still say `ResetAllItemSlotRender(10,20)`,
   a literal left from when the count was the twenty of the *dummy* array. The
   loop walked nine entries past the end, read whatever was there as a
   `CItemSlot*` and called `SetVisibleSingle(FALSE)` on it - a virtual call
   through a wild pointer, which then writes a four-byte zero at an arbitrary
   address. That is the corruption, and that is its signature.

   It is a family, not one site: ten classes carry their own `m_pItemSlotArray`
   with their own callers passing 20 and 50. All 41 call sites are now clamped
   inside the walkers, so no caller can do it again.

A fifth came out of the release build afterwards, in the `.x` loader: a string
member is stored as a pointer inside the node's byte blob, and `stringMember`
read one at an assumed offset. A node packed any other way - a different
exporter, an extra leading array - had eight bytes of float data handed to
`strlen`. The parser now records which offsets hold string pointers and the
reader refuses any other, so a layout it does not recognise loses a name instead
of crashing.

### Verified

Under the bounds build: startup, world entry, the whole HUD button row, the item
shop, storage, and a return to server select and back - no traps.

On the release build: three world entries and a two-minute soak, no entries in
`logcat -b crash`, 105-120 fps.

## The iPhone booted, ran at 60 fps, and drew nothing (2026-09-09)

With the mode filter cleared the client reached `=== boot complete ===` on the
phone and held a steady 60 fps — submitting **not one draw**:

    FRAME 60.1 fps | 4.1 ms = update 0.1 + render 3.6 + present 0.4
    draws=0 (ui=0 textured=0) verts=0
    per frame: opaque 0 (0 verts) | alpha 0 (0) | skinned 0 (0) | ui 0

A black screen at full frame rate. `Render()` was running and costing 3.4 ms of
engine CPU while producing nothing.

**Two leads were wrong and both were dropped by reading the source rather than
arguing from the log.** Zero textures created is a *consequence* of zero draws —
`texture N = ...` is logged on upload, and upload only happens when a texture is
bound for a draw. The absent `RanLight` line is the same: capped at 40, emitted
during draw setup. Neither was independent evidence; both restate `draws=0`.

**The cause was one missing separator.**

    E RanOpen: rb .../Application Support/ranData/Map/Map.rcc -> FAILED
    I RanApp:  engine data: loose files
    E RanLand: log_in.wld: 0 frames, 0 leaf nodes
    W RanEngine: file not found by DxSkinCharData::LoadFile: o_m1.chf

`ranData`. The engine joins its data root without a separator —

    ran_app.cpp:276   std::string(g_appPath) + "Data/Map/Map.rcc"

— because the contract is that the root **ends** in one, and Android has always
met it: `pickDataRoot` does `snprintf(chosen, n, "%s/", candidate)`.
`RanIOS_DataRoot` returned an `NSURL` path, which never carries a trailing
slash. So the 574 MB archive never opened, the client fell back to loose files,
and **there are none** — the manifest ships `data/map/Map.rcc` and zero loose
`.wld`, zero `.chf`, because they all live inside it. An empty scene submits no
draws.

Paths built with an explicit separator — `RANPARAM::LOAD`'s `ranparam.ini` —
were unaffected, which is why the boot looked healthy right up to the end.

The patcher's `RootDir` standardises the slash back off: it joins with
`stringByAppendingPathComponent` and `SafeDest` compares path prefixes, where a
root ending in `/` would test for `//` and reject every path.

Found by diffing the two platforms' logs line by line — Android reports
`log_in.wld: 2 frames, 1 leaf nodes`, iOS `0 frames, 0 leaf nodes` — and then
reading how each platform builds its root. Android is untouched: the change is
in iOS-only code.

## The client runs on an iPhone, and stops 11 pixels short (2026-09-09)

After the patcher was fixed the phone downloaded all 4.7 GB, brought up GL,
drew its own splash, loaded param.ini / option.ini / Config.ini, indexed
Gui.rcc and loaded the game text — and then:

    I RanD3D: device created 0x0
    I RAN: [MessageBox] Could not find any compatible Direct3D devices.
    E RanApp: CD3DApplication::Create failed 0x82000003

**`d3dapp.cpp` drops any display mode under 800x600:**

    // Filter out low-resolution modes
    if( DisplayMode.Width < 800 || DisplayMode.Height < 600 ) continue;

An iPhone 15 in landscape is 2556x1179, and at UI scale 2 that is a logical
**1278x589**. 589 is under the floor, so the only mode the shim reports was
thrown away, the adapter ended up with no devices, and the client refused to
start on a phone that had just drawn its own splash screen. A 19.5:9 display is
simply shorter than a PC-era filter expects; Android's 1280x720 has always
cleared it by 120 pixels.

**Established by measurement, not reading.** Both platforms were instrumented at
`GetAdapterModeCount`, `EnumAdapterModes` and `CheckDeviceType`, and the two
sequences are identical line for line — same counts across four formats, same
two caps probes (which is all `device created 0x0` ever was), same 4+4
`CheckDeviceType` — differing in exactly one number:

    Android:  EnumAdapterModes -> 1280x720   -> device created 1280x720
    iOS:      EnumAdapterModes -> 1278x589   -> no compatible devices

**The fix reports two modes.** Mode 0 is the client size raised to the floor.
Mode 1 is exactly 800x600, and that one is for a second bug found while reading:
when no mode matches the configured resolution the engine falls back to
searching for 800x600, and **if that finds nothing it leaves `dwCurrentMode` at
-1 and then reads `modes[-1]`** in `Initialize3DEnvironment`. Android has been
doing exactly that since the port began and getting away with it. Neither size
is what gets drawn — in the windowed path the back buffer comes from
`m_rcWindowClient` and only the depth-stencil format is taken from the mode.

Verified on Android before it went anywhere near the phone: `EnumAdapterModes[0]
-> 1280x720`, `[1] -> 800x600`, device still created 1280x720, **boot
complete**, server-select page pixel-identical, 60 fps, 1.5 ms submitting draws.

**Also fixed: boot no longer retries forever.** A failed `RanApp_Boot` was
retried every frame, so the first run made **1,461 attempts and a 31,000-line
log**, re-running the splash each time — which is why the screen looked like it
was still loading rather than broken.

## The first run on a real iPhone (2026-09-09)

**iPhone 15, iOS 26.6.2** (build 23G90), signed with a free Apple ID through
Sideloadly and installed as `com.ran.launcher.FALC84Z7MP` — Sideloadly appends
the team id, so nothing may assume the bundle id.

The app launched, drew a black screen with an empty progress bar, and stopped.
No crash report. `ran.log` created and left at **exactly 0 bytes**. Nothing in
the system log. Three instruments, all silent, and the first job was working out
which of them was lying.

**Two of the three were lying, and both were mine.**

1. **iOS routes an app's stderr nowhere.** `RanPlat_Log` writes there on every
   platform that is not Android, so on a sideloaded build every line was
   discarded. Now also emitted through `os_log` with `%{public}s` — os_log
   redacts `%s` to `<private>` by default, which would have left the lines
   visible and their contents not.

2. **The ATS exception did the opposite of what it looked like.** It was written
   as `NSExceptionDomains -> "143.14.11.244" -> NSExceptionAllowsInsecureHTTPLoads`,
   and ATS exception keys must be **domain names**: an IP address is not a valid
   key, and a domain listed without usable subkeys gets FULL enforcement rather
   than none. The plain-HTTP patch fetch was blocked. `NSAllowsArbitraryLoads`
   instead; the real fix is a hostname with HTTPS, at which point the block goes.

**And then the actual cause,** found with
`pymobiledevice3 developer dvt launch --stream`, which pipes the process's own
stderr and was the only channel that worked:

    I RanPatch: patch base http://143.14.11.244:1521/launcher_mobile/
    I RanOpen:  wb /var/mobile/Containers/Data/Application/...

`RanOpen` is `ran_fopen`. `windows.h:1433` has
`#define fopen(p, m) ran_fopen((p), (m))` so the client's file opens reach the
case-insensitive resolver — and `ran_plat.cpp` is compiled with the engine's
`StdAfx.h` force-included, so it inherited the macro. **The log-to-file `fopen`
added the day before became `ran_fopen`, which logs, through `RanPlat_Log`,
which was already holding `g_logLock`.** A non-recursive mutex taken twice, on
the first line the client ever wrote. It froze the patcher thread; the UI thread
carried on drawing the empty page, which is why the process stayed alive and
nothing crashed.

`#undef fopen`, plus a thread-local re-entry guard so this file can never hang
the client again — a re-entrant call skips the file and still reaches stderr and
os_log. `path_resolve.cpp` carries a comment about this exact recursion.
Android never saw it: the file tee is compiled out there.

**What the round established beyond the bugs:** the debugging loop works.
Screenshots, process list, crash reports, app-container file access and streamed
stderr all run from Windows over USB, and `dvt launch --stream` is the channel
to reach for first.

## iOS compiles and links (2026-09-09)

**`ran.app` exists.** Every one of the client translation units built for
arm64 iOS, the link is clean, and the artifact unpacks to a real bundle:

    ran           13,952,792 bytes   Mach-O 64, MH_EXECUTE, arm64
    Info.plist    UIFileSharingEnabled, UILaunchScreen, MinimumOSVersion 13.0
    fonts/        NotoSansThai Regular+Bold, Roboto Regular+Bold
    AppIcon-*.png 13 sizes

with `RanApp_Boot`, `RanTouch_Init`, `CAEAGLLayer`, `AudioQueueNewOutput` and
`SecKeyVerifySignature` all present in the binary. It has never been signed or
run; what is established is that the code compiles and links for the platform,
which two days ago was a prediction about files no compiler had seen.

It took nine runs on a `macos-14` runner. What they found, in order:

1. **The job reported success on a build that died at 1%.** The exit status of
   `cmake --build … | tee build.log` is `tee`'s, which is always 0.
   `set -o pipefail`. Every finding below was invisible until this was fixed.

2. **`-fms-compatibility` cannot be used against the Apple SDK.** Measured with
   `echo __GNUC__ | clang -E -x c -`:

       <none>              __GNUC__ -> 4
       -fms-extensions     __GNUC__ -> 4
       -fms-compatibility  __GNUC__ -> __GNUC__

   It undefines `__GNUC__`, because it is pretending to be MSVC. Apple's
   `sys/_types.h` then takes its non-GNUC branch and makes `__darwin_va_list` a
   `void *`, while clang's own `stdarg.h` defines `va_list` as
   `__builtin_va_list` — so every translation unit that reaches `<stdio.h>`
   dies with a typedef redefinition. No include order fixes it.

   Turning the flag off got to 13% and then hit the MSVC-isms the flag exists
   to accept — a member named after its template parameter, a `goto` across an
   initialisation — with 1,150 files still to go. So the flag stays and the SDK
   is simply given `__GNUC__=4` back. `__EXCEPTIONS` had to come with it: this
   boost predates clang, so with `__GNUC__` set it selects
   `config/compiler/gcc.hpp`, which turns `BOOST_NO_EXCEPTIONS` on unless
   `__EXCEPTIONS` is defined — and MSVC mode does not define it, so
   `BOOST_CATCH(x)` collapsed to `else if(false)` and `StringFormat.h` lost the
   exception it names.

3. **Two files that exist here and in no checkout.** `ogg/config_types.h` is
   generated, and libogg's own `.gitignore` excludes it — this build only ever
   worked because a copy had been left on one machine. CMake writes it into the
   build tree now. And all 13 `AppIcon-*.png` were caught by a blanket `*.png`
   rule meant for device screenshots, so **the first successful bundle had no
   icon at all**; found by unpacking the artifact and counting. Both are the
   same shape: something present locally, absent everywhere else, silent.

4. **Darwin makes the byte-order calls macros.** `#define htons(x)
   __DARWIN_OSSwapInt16(x)`, and the client writes `::htons(nPort)`. A
   qualified name cannot be a macro invocation target, so the expansion is a
   syntax error. bionic declares them as functions, which is why Android never
   saw it. Undefined and re-declared as inline functions in the shim.

5. **Objective-C owns `BOOL`, and the frameworks must be imported first.** Win32
   `BOOL` is `int`; `objc.h` makes it `bool`. And `windows.h` does
   `#define interface struct` for the COM declarations, which costs every Apple
   header parsed afterwards its `@interface`. Both only bite
   `image_decode_ios.mm`, the one file that needs both worlds.

6. **`os.execute` does not compile**: `system()` is `__API_UNAVAILABLE(ios)`.

7. **Five symbols had the wrong linkage.** The build reached 100% and the link
   failed on `RanTouch_Init`, `_Frame`, `_PointerDown`, `_PointerMove`,
   `_PointerUp` — declared with C++ linkage in `touch_ui.h` while
   `ran_ios_main.mm` declares them inside `extern "C"`. Android never noticed
   because `android_main.cpp` includes the header and agreed with it either way.

8. **`sha1.cpp` wanted `<byteswap.h>`**, a consequence of giving `__GNUC__`
   back. All it needs from it is `BYTE_ORDER`; on Apple that is
   `<machine/endian.h>`.

**Android was rebuilt after every single change: 0 errors, 0 failed TUs
throughout.** The flag scoping was checked by counting command lines rather than
trusting it — 1,265 carried `-fms-compatibility-version` before the C and
Objective-C++ scoping, 1,196 after, which is exactly the 69 C translation units.

**How the SOURCE repository reaches the runner.** It cannot be pushed:
`mobile-port/effects-resolution-and-text` carries 49 Visual Studio IntelliSense
files — a 186 MB `Browse.VC.db` and a dozen 174 MB `.ipch` — and GitHub refuses
any blob over 100 MB. `ci/ios-source` is that same tree squashed onto `main`
without them, and `MOBILE/tools/sync-ci-source.sh` re-syncs it. **Run that after
every SOURCE change or the runner compiles the old file.** Doing it by hand went
wrong the first time in the obvious way: checking the port tree out over the CI
branch restores its `.gitignore` too, which does not ignore `.vs/`.

**The patch gate is open (2026-09-09).** Version 413 is live and verified from
here: `manifest.json` is byte-identical to the local one at minApk 1 / **minIos
1** / 23,368 files, `manifest.sig` verifies against the pinned P-256 key, the
APK blob is present at exactly the 342,620,229 bytes it claims, and three
spot-checked data blobs are there at the right sizes. LDPlayer went 412 -> 413
and booted straight through, so the Android client consumes it too.

**What remains before it runs on a phone: signing.** Free and manual — Sideloadly
on this machine signs the `.ipa` with an Apple ID, and the signature lasts 7
days. $99/year buys TestFlight and a year-long signature; nothing about the
build needs it.

**There is a debugging loop, and it is most of the Android one.**
`MOBILE/tools/ios-device.sh`, over pymobiledevice3 11.12 (Python 3.12 installed
here for it):

    adb logcat              ->  ios-device.sh log        (syslog live)
    adb pull /sdcard/ran/x  ->  ios-device.sh pull       (the app's Documents)
    adb push /sdcard/ran/x  ->  ios-device.sh flag NAME
    adb shell screencap     ->  ios-device.sh shot
    /data/tombstones        ->  ios-device.sh crash      (symbolised reports)

The flag and log paths work **only** because `Info.plist` sets
`UIFileSharingEnabled` and the diagnostic root was moved to Documents on
2026-09-08 - without that there would be no way to set `audiolog` or read
`ran.log` on a device at all. What is genuinely missing is `adb shell` (iOS has
no equivalent) and `adb install` (an `.ipa` must be signed first). The USB
transport needs Apple Devices installed for its usbmuxd service.

## The iOS pass before the first compile (2026-09-08)

An iPhone arrived, so the iOS files were read once more against the SDK before
spending a CI run on them. Six defects, found by reading rather than by
compiling, and every one of them would have cost a round trip:

1. **ARC was never enabled.** `-fobjc-arc` appeared nowhere in the build, and
   all six `.mm` files are written for it: `__weak`, `__bridge`, strong
   properties, blocks capturing `weakSelf`. Without it `__bridge` is an
   error and a file-scope `__weak` is another. Not one file calls
   `retain`/`release`, so ARC is the only setting that compiles rather than a
   preference. Added as `$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>`, which
   keeps it off every C++ command line.

2. **The engine's precompiled header was force-included into Objective-C++.**
   `target_compile_options(ranshim PRIVATE -include .../Lib_Engine/StdAfx.h)`
   had no language guard, so the Win32 emulation's `BOOL` and `interface`
   would have gone in ahead of `#import <UIKit/UIKit.h>`. Now `CXX` only.
   `image_decode_ios.mm` includes `windows.h` itself, in the order it wants;
   the other three want nothing from it.

3. **GL was initialised in `viewDidLoad`,** which runs before the view is laid
   out in its window. The layer still carries `UIScreen.bounds`, which reports
   the current interface orientation and at launch can be portrait — and
   `RanApp_Boot` takes the logical size **once**. The whole client would have
   been laid out against a portrait panel. Moved to
   `viewDidLayoutSubviews`, guarded by a flag.

4. **`RanGL_SurfaceChanged` was defined, never declared, never called.** It is
   the rotation path. Declared in `gl_context.h` and wired to every layout
   after the first.

5. **Every decoded PNG and JPEG would have been upside down.** A
   `CGBitmapContext` has its origin at the bottom left, so
   `CGContextDrawImage` puts the image's top row at the end of the buffer.
   Every other decoder in the shim returns rows top-down. Added the
   translate-and-flip.

6. **`UILaunchScreen` held `<key>UIColorName</key><string></string>`.** That
   key names a colour in an asset catalogue and there is no catalogue here; an
   empty string names nothing and is a plist an installer can reject. Now an
   empty `<dict/>`.

And one gap that is not a defect but would have made the phone untestable:

**There was no way to reach a diagnostic flag or a log on iOS.** On Android
every instrument the port has — `audiolog`, `audiodump`, `drawlimit`,
`nulldraw`, `renderscale`, `presentlog`, `shadowcount` — is a file under
`/sdcard/ran` that adb touches from outside, and the log is logcat. An iOS
container is reachable from outside in exactly one place, `Documents`, and
only when the bundle asks. So:

* `RanIOS_DiagRoot` moved from `Application Support/ran-diag` to
  `Documents/ran`. The **data** root stays in Application Support — the 4.7 GB
  argument is untouched; what lands in Documents is kilobytes of text and
  whatever a dump is asked for.
* `Info.plist` gained `UIFileSharingEnabled` and
  `LSSupportsOpeningDocumentsInPlace`. A store build drops both.
* `RanPlat_Log` now tees to `<diag root>/ran.log` on every platform that is
  not Android — truncated per run, capped at 8 MB, flushed per line because the
  interesting log is the one from the run that crashed. stderr on iOS goes to
  the system log, which needs Xcode or Console.app to read, i.e. a Mac.

Both roads out of the device — iTunes/Finder file sharing over USB, and
Files.app on the phone — now work from Windows.

### The second pass: what the link and the flags would have done

Reading the entry point is not enough — the iOS target compiles a different
file list and a different flag set, and both were wrong in one place each.

**The link was checked symbol by symbol** and is sound. The iOS target builds
`platform/ios/*.mm` plus `platform/android/ran_app.cpp`, so every symbol
`ran_ios_main.mm` declares had to have a home in `shim/` or in `ran_app.cpp`:
all 24 do. `RanSplash_*` is `shim/platform/splash.cpp`, `RanTouch_*` is
`touch_ui.cpp`, `RanInput_*` is `dinput_mobile.cpp`, `RanIME_*` is
`shell_mobile.cpp`, `RanGLR_Init` is `gl_render.cpp`. Nothing the iOS entry
point calls lives in an Android-only file.

**Android-only code is properly walled off.** Only five files in the shim
reach for an Android header, and every one is behind `#ifdef __ANDROID__` or a
`__APPLE__` branch. `ran_app.cpp` — the file the iOS target borrows — contains
no Android reference at all. `SOURCE/` is clean too: the one file that ever
included `<android/log.h>` is `SkillTrayTab.cpp`, already guarded.

**The MSVC-compatibility flags were being applied to Objective-C++.**
`-fms-extensions -fms-compatibility -fdelayed-template-parsing
-fms-compatibility-version=19.30` are global, and they exist for the client,
which is MSVC code. Not one `.mm` file is. `-fms-compatibility` changes name
lookup in ways the Apple framework headers were never compiled against, and
`#import <UIKit/UIKit.h>` would have been the first thing to meet it. Now
scoped with `$<NOT:$<COMPILE_LANGUAGE:OBJCXX>>`, which is a no-op on Android
where OBJCXX is not an enabled language. What deliberately stays on for every
language is `-fsigned-char`, `-fwrapv` and the `WIN32`/`_WINDOWS` defines:
`image_decode_ios.mm` includes the shim's own `windows.h` and shares its types.

**`gl_platform.h` used `#import` for the OpenGLES headers**, and that header is
reached from plain C++ translation units where `#import` is a clang extension
that warns. They are ordinary C headers with their own guards, so `#include`.

**The Android build was rebuilt after every change: 0 errors, 0 failed TUs, 29
StdAfx force-includes intact, 1,265 command lines still carrying
`-fms-compatibility-version`, and no `-fobjc-arc` anywhere in its ninja file.**
None of this is verified on a compiler that has seen Objective-C; it is eight
defects fewer for the first run to find.

## The engine keeps its own error log, and nobody had read it (2026-09-09)

Noticed while verifying something else: the client opens
`files/Logs/ErrorLog/log.<date>.txt` 255 times a boot. It has been writing
**5,740 lines every run** for the whole port and no session has ever looked at
it — all the instrumentation built here goes to logcat, and this file is the
engine's own channel, written by `CDebugSet::ToLogFile`.

Almost all of it is benign, and that was worth establishing before chasing any
of it. The bulk is

    2,632  item ran option setting file load fail : w4_sample.bin
      938  item ran option setting file load fail : a3_sample.bin
      919  ERROR : SGENITEM::LOADFILE(), <name>.genitem

which is the client asking for `data/glogicserver/` — **server** data. The PC
client's own error log (`Ran/Logs/ErrorLog/`) has the same families in the same
proportions, 9,002 lines to our 5,740, so a client without server data logging
thousands of these is normal and not a port defect.

**One lead, and its confound, both recorded rather than acted on.** Comparing by
item key rather than by message text (the two logs are in different codepages,
so a byte-level diff lies), mobile reports `GETAPPLYNUM() == 0` for **45 item
keys the PC log does not, and none the other way**. `GETAPPLYNUM` returns
`sDrugOp.wCureVolume` for a list of item types, so a zero there is either real
data or a stream desync in `SITEM::LoadFile` - which would be the same family as
the `.cps` pointer-in-a-record bug found earlier.

It cannot be attributed yet, and the reason is worth writing down: the PC log is
from 2026-08-21, and `Ran/` has no `data/glogicserver/` directory at all while
`CLIENT/` has 1,586 `.genitem` files in it. **The two clients are demonstrably
not loading the same data**, so the difference may be content vintage rather
than parsing. Settling it needs one PC run against the same item file, and that
is what to do before touching any code.

**Worth doing routinely from now on:** pull this file after a session on the
device. It is 5,740 lines that no instrument here was showing.

## The zone loading screen renders through a black checkerboard (2026-09-10)

Found while verifying login end to end. The zone loading art for
`< สถาบัน SG >` draws dark and covered in a regular per-pixel black
checkerboard. **Android and iOS both**, so it is not platform-specific.

Ground truth: `CLIENT/textures/gui/loading_054.dds` decoded independently in
node is **a clean, bright daytime plaza**. What reaches the screen is neither
clean nor bright.

Ruled out by measurement, not by argument:

* **Not the art.** The file decodes correctly outside the client.
* **Not the DXT1 decoder.** The file is DXT1 1024x512. 10.2% of its 32,768
  blocks use punch-through mode (`c0 <= c1`), and **zero of those blocks use
  index 3**, so no texel should be transparent. The shim's
  `decodeColorBlock` implements the `c0 > c1 || !dxt1Alpha` rule correctly.
* **Not the framebuffer.** The map name, the HINT badge and the Loading spinner
  drawn over the same quad are clean. Only the textured quad is speckled.
* **Not multisample coverage.** The shim has no `SAMPLE_ALPHA_TO_COVERAGE`,
  `glSampleCoverage` or `MULTISAMPLEMASK` path at all.
* **Not our own splash path.** `RanSplash` draws boot art from the same kind of
  DDS cleanly, on the iPhone included.

What is known about the drawing side: `LoadingThread.cpp:130 Render()` sets
**no render state at all** - it does `SetTexture`, `SetStreamSource`,
`SetFVF`, `DrawPrimitive` and nothing else, so it inherits whatever state the
previous frame left. It also runs on the loading thread through the EGL context
handover, which is why `drawlimit` cannot bisect it - the same blind spot noted
for the touch overlay.

**Next step:** instrument the blend, alpha-test and texture-stage state at the
moment that quad is drawn. That is the one place not yet observable, and a
leftover state from the previous frame is the remaining candidate.

## Still open

### 1. Confirm the frame-rate work on the tablet — measured, partly

Measured on the Tab S9 in a quiet spot (about five characters visible), at
2560x1600 with the shipped defaults:

    uncapped              12 off-screen skinned a frame   20.6 ms
    capped at 6           42                              21.7 ms
    no character shadows   0                              17.1 ms

So character shadows cost roughly 3.5-4.5 ms a frame for about five casters -
call it 0.7-0.9 ms each, on the real GPU. Uncapped in a crowd of thirty that is
in the region of 25 ms of shadows alone, which is the reported lag; the cap
holds it near 5 ms however many characters are on screen.

What is *not* yet shown is the crowd case on the tablet: the spot tested had
fewer than the cap, so capped and uncapped are the same there (and the small
difference above is the scene moving between samples, not the change). Take one
reading in a busy town to close this properly, and tune the cap against it.

**The draw-timing instrument was broken, and is fixed (2026-09-09).**
`RAN_TIME_DRAWS` has been on all along; the frame-budget line printed
`0.0 ms submitting draws (0%)` because two readers shared one resettable
counter. `ran_app.cpp` prints a per-frame FRAME line and `d3d9_impl.cpp` a
300-frame census, and both called `RanGLR_TakeDrawSeconds`, which resets —
whichever ran first got the time and the other got nothing. That is why the
budget line claimed draw submission was free while the FRAME line beside it
reported 2 us x 287 draws. The census now reads a monotonic total and keeps its
own delta.

Measured on LDPlayer immediately after, at the character screen:

    frame budget: 17.3 ms total, 1.4 ms submitting draws (8%)
    frame budget: 16.9 ms total, 0.9 ms submitting draws (5%)
    frame budget: 16.9 ms total, 0.8 ms submitting draws (5%)

and the FRAME line agrees at 0.7-1.2 ms. So submission is a twentieth of the
frame in a quiet scene. **The number that decides whether batching character
pieces is worth doing is the same reading on the Tab S9 in a crowd** - the
instrument works now, the measurement still needs the tablet.

The draw-count reductions are measured and proportional, but on the emulator.
Confirm on the Tab S9 with a real crowd, and tune `/sdcard/ran/shadowcount`
against it. Build once with `RAN_TIME_DRAWS` defined to find out whether what is
left is draw submission or fill, which decides whether batching character pieces
(one draw per bone-combination attribute group today) is worth doing next.

### 2. Security: the parts not yet looked at

* **DONE 2026-09-09: none of them takes a server-supplied string.** The count
  was wrong to begin with — grepping `SOURCE/` finds 91, but that includes
  files the mobile client does not compile (`s_CNetUser.cpp`, the ODBC
  sources; `Lib_Network` ships 26 of its files, not all of them). Scoped to the
  1,167 translation units the build actually names, it is **105 calls**, and
  every one was classified by its source:

  - `m_szUID` (21 bytes) and `m_szName` (33) — the player's own, fixed-size
    struct fields, into 256-byte buffers with ~117 bytes of slack.
  - item and effect names from the local `.rcc` data tables.
  - local strings: hardware ids in `NSPCID.cpp`, `inet_ntoa`, filenames,
    and the fixed `GARBAGE_DATA` table.
  - fixed literals via `strcat`.

  No inbound packet field reaches an unbounded copy. The residual exposure is
  the item-name path, because item data is a file a user can replace on
  `/sdcard` — which is the parser item below, not this one.

* **One real defect found on the way, and fixed.** `GLAgentServerMsg.cpp:3872`:

      strcat( szTempChar, "SPEED," ); ... "ATTACK SPEED," ... "ATTACK RATE,"
      szTempChar[strlen(szTempChar)-1] = ' ';    // strip the trailing comma

  An EX event whose `emType` matches none of the three bits leaves the buffer
  empty, `strlen` is 0, and it writes one byte **before** a stack array. The
  three literals also total exactly 31 bytes into a `char[32]` — no margin for
  a fourth flag. Guarded under `#ifdef RAN_MOBILE` so MSVC compiles the line it
  always has; **the PC client has the same defect**. It is the only
  `[strlen(x)-1]` in all 1,167 shipped translation units.
* **The image decoders: audited and hardened 2026-09-09.** They parse files a
  player can replace — the client reads its textures out of the data directory,
  which on Android is world-writable storage.

  Read line by line rather than fuzzed, because the arithmetic is where these
  fail. TGA and DDS are sound: every product is promoted to `size_t` before
  multiplying, every write is bounded by `written < total`, and every read
  tests `src + bpp > end`. **BMP was not.**

      UINT stride = ((w * srcBpp + 3) / 4) * 4;

  `w` is a 32-bit field straight out of the file and `w * srcBpp` is `UINT`
  arithmetic, so a declared width of 2^30 at 4 bytes a pixel wraps the stride to
  **0**. The "does the pixel data fit in the file" test on the next line then
  compares against nothing and passes, and the row loop reads wherever the
  header points it. Fixed twice over: the multiply is `size_t` now, and every
  decoder — DDS, DDS cube, TGA, BMP, PNG — rejects a dimension over 16384, which
  is the largest `GL_MAX_TEXTURE_SIZE` any device in this port reports and far
  larger than any real texture here.

  Verified by rebuilding and booting: the server-select page draws exactly as
  before, no texture lost, 60 fps, and the frame budget line unchanged at 1.5 ms.

* **The `.x` reader: audited 2026-09-09, one hole.** Its `BinaryReader` is
  careful — every accessor tests `m_at + n > m_n` before reading, and every
  count-times-width is computed in `size_t`, so nothing wraps on 64-bit. Nothing
  allocates from a file-supplied count.

  Its **MSZip decompressor** was another matter:

      std::vector<BYTE> buf(history.size() + 65536);
      for (;;) {
          if (produced == buf.size()) buf.resize(buf.size() * 2);

  an inflate loop that doubles its buffer for as long as the stream keeps
  producing. MSZip is one deflate stream per block and a block decompresses to
  at most 32 KB **by definition**, so that growth is not something a conformant
  file can ever ask for — but a crafted `.x` can, and `.x` files live in the
  data directory a player can write to. It is a decompression bomb: one block,
  inflated until the process is killed for memory.

  Capped at 64 KB, twice what the format permits, so no real file can reach it
  and one that does says so in the log instead of failing silently.

* Still to look at: the `.rcc` extractor, on the same grounds.
* Compile the `/sdcard/ran/*` switches out of a distribution build.

### 3. Confirm every function in the game works

Not started. Overlaps with the sweep below.

### 4. Thai text input: the composing IME

The layout half is done (above). What remains is a Java Activity with an
`InputConnection` so a composing keyboard has somewhere to commit to, plus
javac/d8 in `build-apk.sh` and `android:hasCode="true"`.

### 5. Gameplay sweep past the inventory

Deliberately not attempted this session: it needs many trips into the world and
the server drops a session on every reconnect.

Walk each on the tablet, in this order, logging what breaks rather than fixing
as you go:

* **NPC dialogue** — page through, take and decline a branch.
* **Shops** — buy, sell, the quantity prompt.
* **Trade** — offer, change it, both confirm, cancel midway.
* **Quest turn-in** — accept, track, complete, hand in, reward pick.
* **Death** — the prompt, resurrect in town and on the spot.
* **Zone change** — a portal and a teleport card; watches the loading screen
  hand the EGL context over and back.

### 6. Smaller things

* **Projected shadow texcoords** — `TCI_CAMERASPACEPOSITION` with
  `D3DTTFF_PROJECTED` is not implemented in the fixed-function translation.
* **`SetClipPlane` is not implemented** — which is why the character reflection
  was never clipped to the water. Implementing it would let reflections come
  back, if they are ever worth the draws.
* **The `68 นาที` number** — a duration that does not match the PC client.
* **Skill press during an attack is unverified** — the test character has no
  skills slotted. Slot one, spam attack, then press it.
* **The moon is unverified** — the four-phase fix only shows at night.

## The dead mob kept its name and health bar (2026-08-30)

Reported as "the target mob is delay to disappear the name the hp after I kill
them so the target is not change". Two separate defects looked like one:

**The drop itself was on time.** A temporary log in `MobileTargetTick` printed
what the target looked like at the moment it was dropped:

    target dropped: id 346 copy yes hp 2 dying 1

`dying 1` is `IsACTION(GLAT_DIE)` — the target is released the frame the death
action starts, which is the earliest the client knows. Note `hp 2`, not 0: the
server announces the death as an action and the last damage packet never brings
the bar to zero, so anything keyed on `HP == 0` would never have fired at all.

**Nothing hid the display.** Dropping the target only stops `SetTargetInfo`
being called each frame; the groups it had shown stay up. The client's own path
never shows this because it re-picks from the cursor every frame and the pick
simply stops matching — with a finger there is no pick to stop.

There are *two* groups, and hiding one leaves the other:

* `CROW_TARGET_INFO` — the fixed panel above the touch pad
  (`ResetTargetInfoCrow` / `…Npc` / `…Player`).
* `TARGETINFO_DISPLAY` — the name and health bar drawn over the target's own
  head (`ResetTargetInfo` / `…Npc` / `…Player`), which is the one actually seen
  sitting on a corpse.

`MobileTargetTick` now calls all six when the target stops being live.

**Measured on the Tab S9** (`out/k3.png` … `out/k5.png`): with a mob targeted the
panel reads `Lv.167 ไอ้ค้อนใหญ่ (นักโทษ) 30000/30000 (100%)` and a red bar sits
over its head; at `2/30000` both are still up; three seconds later, with the
loot on the ground, both are gone and no bar is left on the corpse.

## "Classic Name" did nothing: two defects (2026-08-30)

**The touch pad ate the window's buttons.** `RanTouch_PointerDown` claimed any
press landing on one of its own buttons without asking what else was on screen.
The client's windows are movable and several open into the lower right - the
options window does - so their buttons sit under the pad. Measured: the tick
flipped (`opt flip: classic=1`) but `GamePlayOption_OK` never ran, and dragging
the window's title bar out of the corner only flipped the pad's skill page to
`4/4`. `CUIMan::IsPointInControl(x,y)` (new, mobile-only; walks top/focus/bottom
in draw order for a visible control covering the point) is exported as
`RanUI_PointInControl` and consulted before the pad claims a press. This unblocks
every window whose buttons land under the pad, not just the options one.

**The plate never followed the option.** `CNameDisplay::SetName` is the only
place that shows or hides `m_pNameLineBox`, and it runs when a name display is
handed out - so turning the option on left every name already on screen without
a plate. Measured at render time: `box vis=0` on every display seconds after
`opt OK: classic=1`. `CNameDisplay::Render` now tracks the flag directly.

Verified on the Tab S9: after ticking Classic Name and pressing ตกลง the window
closes and every mob name gains the dark plate (`out/f2c.png`); the plate art is
black at alpha 177 in `interface_main.dds` at (315,460), which is what the PC
draws too.

### Still open: touch item interaction

The PC item model is mouse-shaped: 27 distinct item gestures, four of them
behind ALT/CTRL and so unreachable on a tablet (preview, box contents, chat
link, buy-without-confirm). Worse, carrying an item makes a stray tap on the
terrain drop it on the ground with no confirm, and a long press in the world
cancels a trade offer. Design written up in `MOBILE/ITEM-TOUCH-PLAN.md`: a per-slot
action sheet, a touch count sheet for split/buy/sell, bigger slot hit areas.
Not implemented - waiting on four decisions listed at the end of that file.

## Touch item interaction, phases 1-5 and 9 (2026-08-31)

Built to `MOBILE/ITEM-TOUCH-PLAN.md` and verified on LDPlayer (the tablet's
wireless adb drops mid-session). **LDPlayer runs `lib/x86_64/libran.so`**, so
every emulator test needs `ABI=x86_64 ./build.sh` as well - an hour went into
probes that were compiled into the arm64 library while the emulator ran the
previous day's x86_64 one.

**Phase 1 - carrying is visible and cannot lose an item.**
`GLCharacter.cpp`'s world-click drop is compiled out on mobile: a tap on the
ground no longer throws the carried item away, and a long press out in the world
no longer cancels a trade offer (both were reachable by accident). `CItemMove`
parks the carried icon at the top centre instead of following the pointer -
snap, which normally parks it on the hovered slot, is a hover effect and hid the
icon inside the bag grid, so a full hand looked like an empty one. Tapping that
icon calls the new `GLCharacter::MobilePutHeldBack`, which finds a free cell the
way an unequip does. Measured: an item stuck in `SLOT_HOLD` from an earlier
session was invisible until this landed.

**Phases 2-3 - the action sheet.** `CMobileItemSheet` (new, mobile-only) lists
what can be done to the tapped item: equip/use, move, split, preview, box
contents, chat link, enhance, drop, close. Rows come from the client's own
tests (`sSuitOp.emSuit` for wearable, `IsInvenSplitItem`, `PreviewItemCheckSimple`,
`ITEM_BOX`/`sRandomBox`), and each row calls the same `Req*` the PC calls. Bag,
worn gear, the quick tray and storage route into it; a full hand still places,
swaps and splits on release, and a drag still lifts an item.

**Phase 5 - the count sheet.** `CMobileCountSheet` replaces the number modal for
split (`-1 / +1 / 1/2 / All`, then `ReqInvenSplit`). Verified: a 600 stack split
599 off, server accepted.

**Phase 9 - the enhance window.** `CMobileEnhanceWindow`: pick target, pick
material, press อัพเกรด. It does the carrying itself - `ReqInvenTo` to fill the
hand, wait for the server, `ReqInvenDrug` on the target, then empty the hand
whatever happened - which is exactly the PC's carry-and-right-click, with no
carry state exposed to the player. The picker reuses the action sheet rather
than drawing a second list. Verified end to end with a cleanser.

Text lives in `gameword.xml` (`MOBILE_ITEM_SHEET`, `MOBILE_ENHANCE`) and
`gameintext.xml` (`MOBILE_DROP_CONFIRM`), so it translates with the rest -
which meant writing `MOBILE/tools/rcc-extract/rcc-pack.js`, since the GUI ships
inside `Gui.rcc` and the loose XML is unreachable on device.

**Still open on this piece:** shop and trade grids still use the PC path (both
already confirm, so nothing is silently destructive); mix/rebuild/transfer/
garbage work through the sheet's "move" row; the latched tooltip with a close
button is not built (the pointer stays parked on the tapped slot, so the tooltip
already behaves, and the sheet suppresses it while open). Slot hit-padding from
the plan is **dropped**: cells in these grids are adjacent, so padding one cell
only steals from its neighbour - bigger targets need a different cell layout,
which is a data change.

### The touch panels now wear the client's own skin (2026-08-31)

First pass built them out of the combo-box back and a text list, which read as
bolted on. Rebuilt against what the client already ships:

* **Item sheet** - the ESC menu's frame (`CreateBaseBoxESCMenu`) with one
  `SIZE22` text button per row, shadowed font and all, because that menu is
  exactly this menu with different rows.
* **Count sheet and enhance window** - real `CUIWindowEx` windows built with
  `CreateBaseWindowLightGray`, so they have the game's title bar, its close
  button, its frame and its buttons. Their control rects live in
  `uiinnercfg02.xml` like every other window's.

Two engine rules came out of it, both now in memory:

1. **A derived window must start its control ids at `ET_CONTROL_NEXT`.**
   `CUIWindow` numbers title/focus-title/close/body from `NO_ID+1`, so ids
   starting at 1 collide with the frame: the colliding controls never draw, and
   the container's destructor later double-frees one - a SIGSEGV in `je_free`
   from `~CBasicTextBox`, and a bogus pointer in the font path on the loading
   thread. Three of the four steppers were invisible until this was found.
2. **A window lays its children out from their LOCAL rects every time it moves**,
   so the layout has to be the local rect - a global position holds only until
   the next move.

### Touch item UX, second pass (2026-08-31)

Four changes after seeing it in use, each verified on the emulator:

* **The panel opens beside the item's own detail**, not on top of it and not
  under the thumb: `MobileOpenItemSheet` reads `INFO_DISPLAY`'s rect and puts
  the panel to its left, or its right when there is no room. The tooltip is no
  longer suppressed while the panel is up - the two are read together.
* **The panel wears the tooltip's skin.** `CreateBaseBoxVarTextBox`, the same
  frame `CBasicVarTextBox` builds itself from, so the pair share a background
  and an outline. Stretching it is `AlignSubControl` + `SetLocalPos`, not a
  ratio resize - the ratio drifted as the row count changed and left the frame
  short of its own rows.
* **Moving is hold, drag, release** - and the item follows the finger, because
  the lift now happens on the DOWN edge of the long press while the finger is
  still on the glass. Lifting on the release instead meant the icon only
  appeared once the finger had gone. `CItemMove` is back on the client's own
  path (follow the pointer, honour snap); the parked top-centre icon is gone.
* **The enhance window has real slots.** Two `CItemImage` slots show what is in
  them; dropping a carried item on one puts it there, and with the window open
  the item sheet grows an **ใส่** row that sends a bag cell straight into the
  slot it belongs in.

Also: the first row now reads **ใช้งาน** rather than สวมใส่.

### Upgrade row honesty, and Auto Potion start/stop (2026-08-31)

**The upgrade row appeared on items that cannot be upgraded.** The rule was
"any suit, or any material", which is far looser than the client's. Each
material's real target test is now mirrored from the request that enforces it:

| material | target must be |
|---|---|
| grinding stone | a suit with `sSuitOp.wReModelNum > 0` |
| cleanser | something carrying a disguise (`nidDISGUISE`) |
| disjunction | not a wrapped item |
| random-option card | a suit with `sRandomOpt.bPVPItem` |
| non-drop card | a suit that is not GM-generated |
| skill reform card | a held weapon that has a skill |
| wrapper | something not already exchangeable |
| disguise card | a suit that is not itself a disguise |

and the row is offered only when the bag actually holds a material that takes
this item (or the item is itself a material) - `CMobileEnhanceWindow::
CanEnhanceCell`. What is deliberately left to the client to say are the checks
that depend on state rather than the pair: grade caps, stone counts, class
match, cost. Verified: a potion no longer offers the row.

**Auto Potion now starts and stops.** OK/Cancel are gone on mobile: **เริ่ม**
saves the three thresholds and turns auto-potting on, **หยุด** turns it off,
and the window stays open because the next thing anyone does after stopping is
change a threshold and start again. The X still closes it. Measured: the
client's own `Auto-pots start` / `Auto-pots stop` lines appear in the console.

### Icon strip: duplicates out, mini-party back (2026-08-31)

The corner has two rows: standalone buttons on top, the collapsible MENU strip
below. Quest and item shop existed in both, so the strip's copies are gone on
mobile - `CBasicGameMenu::CreateSubControl` hides `MENU_QUEST_BUTTON` and
`MENU_ITEMSHOP_BUTTON`, slides the eight icons to their right along by 25 px
each and shortens the strip by 50, so there are no holes and the frame ends
where the icons do. The outer pair is what remains, and the quest one there
still blinks when something is waiting, which the strip's never did.

`MINIPARTY_OPEN` is no longer force-hidden: it is one of the PC client's own
icons and it now sits in the top row with the rest instead of as a stray tab
against the left edge.

Identifying which control was which took a temporary id probe in the row
layout, because the ids in the comments come from an old census and do not
match the enum. Both probes are removed. Verified on the emulator: the strip
lost exactly two icons, nothing else moved, and tapping the shifted icons opens
the window that now sits there.

### Windows would not move, and the mini-party panel now opens middle-left (2026-08-31)

Dragging a window by its title bar did nothing on touch. The cause was in the
input shim, not the UI: a pointer position is applied the instant it arrives,
while button events wait in a queue that drains one per frame. On a drag the
press was therefore delivered a frame or more after the finger had moved on, so
`CUIWindow::TranslateUIMessage` saw a press at a point outside the title and
never started the drag - and the faster the drag, the further outside it was.

Each queued button event now carries the position it happened at
(`shim/platform/dinput_mobile.cpp`). Draining one applies its position for that
frame and hands the real position back on the next pump - a straight assignment
with no delta and no `DIMOFS` event, because the motion between the two points
was already accumulated when it arrived; counting it twice would have spun the
camera. `RanInput_PointerMove` writes to whichever of the two the borrow leaves
live, and `RanInput_WarpPointer` (the client pinning the cursor) cancels a
borrow outright.

Two smaller fixes went with it: `CMobileEnhanceWindow::Update` was calling
`CUIGroup::Update` and so skipped the drag branch that lives in
`CUIWindow::Update`; and the touch layer chose its drag button from
`RanUI_MouseInControl()`, a stale hover flag, instead of a fresh
`RanUI_PointInControl` hit test at the point the finger went down - over a
window that made the drag a camera drag.

The mini-party panel is placed against the middle of the left edge on first
appearance (`DxGameStage::MobileArrangeInterface`). Its own place is the top
left, where the health, level and experience bars already are. Placement happens
once per appearance, so a panel the player drags elsewhere stays there.

Verified on the emulator (screenshots `native/out/k2.png`, `k6.png`, `k7.png`):
the inventory window and the upgrade window both follow a title drag, and the
mini-party panel opens centred on the left edge. Probes removed, both ABIs
rebuilt, `out/ran-phase3.apk` repacked.

### The action sheet: detail stays up, no move row, honest use row (2026-08-31)

Three things were wrong with the sheet a tap opens.

**The item detail vanished behind it.** `SHOW_ITEM_INFO` refuses to draw while
the pointer is inside a control, which is a hover rule - and the sheet is under
the finger that opened it, so the detail the player is deciding from was thrown
away the moment the rows appeared. `CInnerInterface::MobilePinItemInfo` is the
sheet's own hover: called every frame the sheet is open, it steps around that one
rule and pins the box at a fixed place instead of at the pointer, so tapping a row
cannot drag the detail (and with it the panel) out from under the finger. The box
only knows its size once it has been laid out, so the pin clamps against the live
rect each frame, and `CMobileItemSheet::PlaceAt` keeps the panel beside it as it
settles. `MobileUnpinItemInfo` restores the cursor gap on close, or the next
hover would draw its tooltip wherever the sheet left it.

**ย้าย is gone.** A long press already lifts the item and carries it; the row was a
second way to do what the finger does.

**ใช้งาน is offered only when something would happen.** It was gated on the suit
switch alone - and `SUIT_HEADGEAR` is zero, so every card, potion and megaphone
in the bag read as a hat and got an equip row that failed silently. The real gate
is `CHECKSLOT_ITEM`, which refuses anything that is not one of seven item types
before it looks at the suit; `IsWearable` now runs that test first. Beside it,
`IsUsable` mirrors `ReqInvenDrug`'s own type switch (a cure only counts with a
drug effect on it), and `IsStorageUsable` mirrors `ReqStorageDrug`, which takes
only a drink, a skill book and a pet skill book. One label for both branches, since
an item is either put on or used up, never both.

Verified on the emulator: sword ใช้งาน/อัพเกรด/ลิงก์ในแชท/ทิ้ง/ปิด with its detail
beside the panel (`native/out/q2.png`), megaphone แยก/ลิงก์ในแชท/ปิด with no use row
(`q1.png`), recall card ใช้งาน (`r1.png`), talisman and skill book both keeping
theirs. Both ABIs rebuilt, `out/ran-phase3.apk` repacked.

### The use row now agrees with the line above it (2026-08-31)

An item whose detail said [ไม่สามารถใช้ได้] - gear for another class, or a sword
past this character's Dex - still carried a ใช้งาน row. Type and suit were the only
tests; the requirements were not checked at all.

Both paths are gated now, each by its own test, which are not the same test:
equipping runs `ACCEPT_ITEM` (class, school, brightness, level, stats, skill) -
that is the very call whose result prints the red line in the detail, so the row
and the line can no longer disagree - and using runs `SIMPLE_CHECK_ITEM` (class,
school, level), which is what `ReqInvenDrug` and `ReqStorageDrug` check before
they do anything. A worn item's disguise id picks the two-argument form, as
`ReqInvenToWear` does.

Verified on the emulator: BaiYou Sword, detail [ไม่สามารถใช้ได้], rows อัพเกรด /
ลิงก์ในแชท / ทิ้ง / ปิด with no use row (`native/out/s1.png`); recall card and
talisman, both [สามารถใช้ได้], keep theirs (`s2.png`, `s3.png`).

### A hold on another player opens their menu (2026-08-31)

Shift + left click is how the PC opens the P2P menu on another player - trade,
whisper, party, club, friend, view gear. There is no Shift on a tablet, so that
whole menu was unreachable.

The 450 ms hold is now that gesture. In `GLCharacter::FindActionTarget`,
`bODER_P2P` - which is what makes the function return a player as
`EMACTAR_PC_P2P` (someone to deal with) rather than `EMACTAR_PC_PVP` (someone to
hit) - is set by the right mouse button on mobile, which is what the hold
produces. The state is tested with `DOWNED|UP|DUP`, not `DOWNED` alone: the frame
the menu opens on is the frame the finger lifts, and by then the button is no
longer held.

The reaction runs **ahead of** the skill branch in `PlayerUpdate`, not as another
arm after it. The same right button casts a skill, and a skill is nearly always in
hand, so as the last arm the hold never ran once - it reached the skill branch,
which has nothing to do with a P2P target, and stopped there.

One regression prevented while testing: the mobile target latch (which stores what
a click landed on so the attack button and the skill arc know what to fight) would
have stored the P2P actor, and neither `MobileAttackNearest` nor
`MobileSkillAtTarget` accepts one - so a hold on an enemy would have left the
player unable to attack or cast until they tapped again. The latch now skips
`EMACTAR_PC_P2P`.

Verified on the emulator: hold on test02 opens the six-icon P2P menu titled with
their name (`native/out/v1.png`); a plain tap still selects them, red HP bar and
all (`v2.png`). The menu's own buttons work through touch - whisper put `@test02`
into the chat box and printed the whisper hint (`w1c.png`). The magnifier (view
character) does nothing, on mobile and on the PC alike: `RequestCharacterInfo`
returns immediately unless `RANPARAM::bFeatureViewCharInfo` is set, and it
defaults FALSE and is off in this config. Both ABIs rebuilt, `out/ran-phase3.apk`
repacked.

### The touch overlay is Gunmetal now (2026-08-31)

The on-screen controls were warm cream with a terracotta accent - the one pale
warm thing on a screen full of the client's own dark steel windows, so they read
as bolted on. They are now the same gunmetal, lit from the top left, with colour
reserved for state: amber when an action is available, cyan when a system is on,
crimson for PK.

**The renderer change everything rides on: a per-vertex colour attribute.** The
overlay carried one flat colour per draw and faked a gradient by stacking up to
thirty filled fans. That was the banding on the stick, the visible polygon
corners on every disc, and most of the draw calls. With `aColor` a gradient is
one fan and a feathered edge is a ring of transparent vertices, so it is better
looking *and* cheaper. On top of that: an additive pass for blooms, a dark halo
under each control, a gloss over the face, a catchlight on the top edge, and
`drawArcFade` - an arc whose alpha ramps to nothing at both ends, which is what
removes the hard notch where the lit and shadowed halves of a bevel meet.

**The glyphs are painted, and measured rather than drawn from memory.** A sword
in steel with a lit edge and gold furniture; the same sword crossed for PK; a
wooden chest with iron straps for loot. All three were rasterised from their
references on game-icons.net and measured:

- lorc's *broadsword*: -45 degrees from vertical, length/width 2.46, guard/blade
  5.8 - about twice as slender as the one drawn by eye, which had looked like a
  toy.
- delapouite's *chest*: the lid is a flat-topped trapezoid at 36% of the height,
  not a dome at 25%; four vertical gaps at +/-0.52 and +/-0.78 are where the
  bands go; the lock plate is large and straddles the seam with the keyhole cut
  through it.

Painted art cannot also carry state colour - a crimson sword is not a sword - so
state moved outward to the chrome, and the art only ever dims.

**Also changed:** the stick's well went from a near-opaque black disc to 30% with
eight ticks and a heading wedge on the rim, so the world shows through and the
control finally shows direction; empty skill slots keep a dimmed frame instead of
being stripped to nothing, which had read as holes where buttons should be.

**Two process notes**, both of which cost several rounds before they stuck:

1. *Judge at true size.* Every glyph that looked right blown up failed at the
   size it is actually drawn - a hand became an arrow, an arrow's shaft vanished,
   a chest became a house. The preview renders every candidate twice, and only
   the small row decides anything.
2. *Measure the reference, do not copy it by eye.* Checking the chest's aspect
   ratio passed (1.29 against 1.37) while every internal proportion was wrong.
   Extracting the mask and reading coordinates took two minutes and settled what
   several rounds of taste could not.

Tooling that came out of it and is worth keeping, in the session scratchpad:
`svglib.js` (SVG path parser plus mask rasteriser), `measure.js` (principal axis
and width profile of a reference icon), `art.js` (shaded glyph preview at true
button size), `svgview.js` (rasterise reference SVGs to look at).

Verified on the emulator: `native/out/hud3_r.png`. Both ABIs rebuilt,
`out/ran-phase3.apk` repacked.

**Still open on this piece:** auto-loot on a hold of the loot button
(`GLCharacter::m_bAutoLoot` already exists and is only ever set by the
auto-pilot); the attack button dimming when nothing is targeted; the skill
ready-flash and the radial cooldown sweep; and the page arrows are still 76 px
against a 92 px minimum touch target.

### Three dead toggles: a window nobody can see was eating the presses (2026-08-31)

The auto-target, PK and camera-lock buttons did nothing. A probe in
`RanTouch_PointerDown` logging any press the client's hit test swallowed showed
it was exactly those three and nothing else:

    padeat: slot -4 at 1188,439      auto
    padeat: slot -5 at 1188,368      PK
    padeat: slot -7 at 1188,298      camera lock

A second probe inside `PointInList` named the culprit:

    padblock: id 114 rect 1080,200 200x300

Id 114 is `GENDER_CHANGE_WINDOW` - parked at (1080,200) 200x300, drawing
nothing, with its visible flag set. It sits directly over the pad's mode-toggle
column, so those three buttons had been dead since the day the hit test was
added (in this session, for the Classic Name fix - the pad used to eat presses
meant for the client's own windows, and this was the cure).

`CUIMan::IsPointInControl` filtered on `IsVisible()`, which this window passes.
It now filters on `IsNoRender()` instead: **a control the player cannot see must
not take a press away from one they can.** The loot button and the page arrows
were never affected, which is why the failure looked arbitrary.

Verified: the probe logs nothing, and all three toggles light - camera lock cyan,
PK crimson, auto correctly switching itself off when PK comes on, which is the
client's own mutual exclusion. Both probes removed.

**Also in this pass**

- **The page readout is gunmetal.** It was the client's own cream chamfered
  plate, which was right while the overlay was cream and was the one bright
  rectangle on the screen once it was not. Dark chamfer, steel rim, and the page
  number in amber because the page you are on is a state, and amber is what
  state is drawn in everywhere else on the pad. Verified stepping 1 -> 3 with
  the arrows (`native/out/pg2c.png`).
- **Attack and loot moved inboard**, from `g_unit * 1.35` to `g_unit * 1.68`
  from the right edge. The arrows and mode toggles stack *outboard* of the
  attack button, so measuring the inset to the attack button alone put it right
  of the middle of its own cluster and it read as shoved into the corner.

- **Attack and loot moved inboard again**, `g_unit * 1.68` -> `g_unit * 2.00`. The skill
  arc follows, because the client derives it from `RanTouch_GetAttackCircle`.
- **The page plate lost its "/ 4".** The total never changes, so it was a
  constant occupying a third of the plate to say nothing, and it kept the
  figure small. The page number now has the whole well and is set larger
  (`0.68` of the well height, up from `0.52`). Verified on the emulator:
  `native/out/hud7_c.png`.
- **The page plate was overlapping the attack ring**, by 2.7 logical px - 5 on
  device. The arrow column was positioned relative to `attackX`, so pulling the
  attack button inboard dragged the column in with it, and the plate is wider
  than the arrows (`0.46` of a module against `0.34`) so the plate is what
  reached back and collided. The column is now anchored to the screen edge
  (`g_width - g_unit * 0.42`), which decouples it: the action buttons can move
  without it following. Measured clear by 79 logical px, 19 px right margin.
  Screenshot `native/out/hud8_c.png`.

### How far the attack button can actually move: 1.48 modules (2026-08-31)

Pulling the attack button inboard was pushing the skill arc over the chat panel.
The client hangs the arc off this button, so the button cannot be placed on its
own - and the numbers were guessed twice before being measured.

Measured off a running build rather than assumed:

- a skill slot is **41 logical px**, not the 33 an old comment implies. Taken
  from the rendered rim: it is 26.5 px in radius and the overlay draws it at
  `1.30` of the slot half-width.
- so `fInner = 159.0`, `fOuter = 232.6`, rim `26.7` - the arc reaches
  **259 px left of the attack centre**.
- the chat panel's right edge is at **x = 862**, found by walking a brightness
  profile across row 1260 of a screenshot until the panel's step up to the world
  appears.

That gives a hard ceiling of **1.48 modules** from the right edge. Settled at
`g_unit * 1.45`, which leaves 13 px between the outermost slot and the chat.

Both intermediate values were already wrong when they shipped: 1.68 overlapped by
11 px and 1.80 by 23, which is why this only surfaced as "the skill slot overlaps
the chat" after two rounds of moving it.

**This is most of the room there is.** Going further in means narrowing the chat
panel or tightening the arc, and the arc is near its own minimum already:
neighbours need 1.6 slot widths between centres, spacing along a quarter arc is
`0.393 * fInner`, and the inner radius only just provides it.

Verified on the emulator: `native/out/edge2.png`, a crop of the boundary itself
rather than the whole screen.

## The mobile patcher (2026-08-31)

The PC client is patched by a third-party incremental updater over
`http://143.14.11.244:1521/launcher/`, which publishes 2,300 `.ken` and 194
`.eiei` files plus an encrypted `incupdate.idx`. None of that is reusable on
Android: the index format is opaque, and the payload is the *loose* file tree
while mobile reads `.rcc` packs. Mobile gets its own subtree,
`/launcher_mobile/`, published from the same host.

### What ships

`MOBILE/tools/patch/make-manifest.js` builds the payload:

    manifest.json          version, minApk, one entry per file (path/size/sha256)
    blobs/<sha256>         content-addressed, immutable, append-only

Content addressing means uploads only ever ADD, so there is no cache to
invalidate and no window where a client can fetch a half-replaced file;
rollback is republishing an older manifest. It also deduped 246 byte-identical
files for free.

**8,263 files, 1,679.6 MB** - against the 5.7 GB currently on the test device.
The difference is loose copies of already-packed content plus 352 MB of
`.bak_pre_*` files that `push-data.sh` carried across because it copies whole
directories. The generator ships from an explicit allowlist instead.

### Working out what actually ships, and one bug it caught

Verified in both directions rather than assumed:

- `skin`, `piece`, `object`, `skeleton`, `help` are in no pack (only 79 of
  skin's 2,883 files appear in SkinObject.rcc), so they ship loose - 722 MB.
- every loose file inside a packed directory *is* in its pack, so excluding
  those copies is safe. The one apparent exception,
  `_bowstring_ready - +_+.egp`, is in the pack under a mojibake name - an
  encoding mismatch in the comparison, not a gap.

That check missed something anyway: it only looked at files directly inside each
directory and never recursed. `data/glogic` has four **subdirectories** -
`quest`, `npctalk`, `level`, `activity` - plus `data/effect/char`, none of them
in any pack. **1,968 files, 54 MB**, and losing them is silent: quests and NPC
dialogue simply stop working.

The fix is structural. The allowlist was derived from `CLIENT/`, a development
tree where inclusion means nothing; `Ran/` is a *working install* and therefore
ground truth. `--verify` now walks the shipped PC client and reports anything it
has that the manifest does not:

    verify: every file under .../Ran/data is in the manifest.

`data/glogicserver` was dropped for the same reason: the shipped PC client has
no such directory.

**End to end:** with only the manifest's files present (1,738 MB) the client
boots, logs in, and renders world, characters, NPCs and mobs. The 392
"file not found" lines in logcat are probes for random-option tables like
`contri_rv.bin` that do not exist in the PC client either.

### The launcher

The APK was `hasCode="false"` - pure NativeActivity, no Java at all. It now has
a launcher Activity, `com.ran.launcher.RanLauncher`, which patches and then
starts the game. `javac` and `d8` run from the same SDK as everything else; no
Gradle. **This is also the layer the Thai composing IME needs.**

Boot flow, with the one ordering rule that makes it crash-safe:

    fetch manifest -> compare to /sdcard/ran/.patchver
                   -> reconcile against .patchindex, hashing only what moved
                   -> download to .tmp, verify sha256, rename over
                   -> rewrite .patchindex
                   -> write .patchver LAST
                   -> start NativeActivity

Killed part way through, `.patchver` still names the old version, so the next
launch reconciles again and finishes.

Measured on the emulator: a full reconcile of 8,263 files takes ~10 s, and a
second launch with the version already current takes **35 ms** to check and
**378 ms** to reach the game. Without `.patchindex` every launch would hash
1.7 GB.

Four bugs found by running it, three of them mine:

1. `package com.ran.native` does not compile - **`native` is a Java reserved
   word**. The class lives in `com.ran.launcher`; the application id is
   unchanged, because that is an Android identifier rather than a Java one.
2. `Intent(Context, Class)` builds a ComponentName immediately, so passing a
   null class throws before `setComponent` can replace it.
3. `onCreate` started the patch thread without claiming the guard `onResume`
   checks, so both ran and the game was launched twice.
4. **Android 9+ refuses cleartext HTTP**, and the patch host is plain `http://`.
   Every fetch failed and the launcher fell through to "could not reach the
   patch server" - which would have happened to every player. Fixed with a
   network security config scoped to that host rather than
   `usesCleartextTraffic` application-wide.

`/sdcard/ran/.patchbase` overrides the base URL when present, so a patch can be
tested against a local server over `adb reverse` without rebuilding the APK.

### Still open on this piece

- **The patch host is plain HTTP on a bare IP.** A patcher trusts what it
  downloads, and over cleartext an attacker on the same network can substitute
  the payload *and* the manifest, so hash checking does not help. The box
  already has OpenSSL. A hostname also matters: if `143.14.11.244` changes,
  every installed client is orphaned and needs a new APK to find the new address.
- **Back up `native/android/debug.keystore`.** It is gitignored and exists only
  on one machine, and `build-apk.sh` silently regenerates a *different* key if
  it goes missing - after which no update will install over an existing app.
- Apache 2.4.41 / OpenSSL 1.0.2s / PHP 7.1.33 on the patch host are all
  end-of-life since 2019.
- `minApk` is 1 and `versionCode` is 1; give the APK a real numbering scheme
  before relying on the out-of-date gate.

---

## Text-format `.x` files were parsed with two whole features missing (2026-09-03)

**Symptom.** `กล่อง PVP รางวัล` - the PVP reward chest NPC - did not appear at all.
Other pieces (wings, bikes, some costumes, several weapons) had the same shape of
failure. The device log gave the chain in three steps:

```
RanPiece: skin mesh s_gbox_01.x (skeleton b_gbox_01.X) failed to load
RanSkin : s_gbox_01.x: SetupBoneMatrixPointers failed 0x80004005
RanSkin : s_gbox_01.x: bone 0/1 "" not in skeleton b_gbox_01.x
```

The bone name came back **empty**, so `DxBoneCollector::FindBone` could not match
it and the whole piece was dropped.

**Cause 1 - string members were invisible.** `xfile_parse.cpp` has two parsers.
The binary one records, for every string member it writes, the byte offset at
which the pointer lands, in `XNode::stringOffsets`. The text one never did.
`stringMember()` in `d3dx_hierarchy.cpp` refuses to dereference an offset the
parser did not declare - deliberately, because a blob whose members pack
differently would otherwise hand eight bytes of float data to `strlen`. The
result was that **every string member of every text-format `.x` read as absent**:
bone names in `SkinWeights`, filenames in `TextureFilename`. 133 text-format
skins in `data/skin` were affected. One line in the text parser's string branch
fixes it.

**Cause 2 - `{ Name }` references were never resolved.** With the bones fixed the
chest rendered, but flat. A text export writes each material once at file scope
and points every user at it:

```
Material Material__5376 { ... TextureFilename { "G_Box_base.dds"; } }
...
MeshMaterialList { 1; 32; 0,0,...; { Material__5376 } }
```

Both parsers emitted those `{ Name }` children as bare `__reference` nodes and
nothing ever bound them - `XNode::reference` was a declared-but-never-assigned
field. So `MeshMaterialList` looked like it had no `Material` child at all.
`XFile_Parse` now runs a `resolveReferences()` pass once the whole file is in
(the target may not be parsed yet at the point the reference is read), and
`XNode_Deref()` in the header is what any child walk has to step through.

**Cause 3 - `DeclData` was not read.** The chest was textured after that, but
still drew as one flat colour. `s_gbox_01.x` ships **no `MeshNormals` and no
`MeshTextureCoords`**: its per-vertex normals and UVs live in a `DeclData` block,
a `D3DVERTEXELEMENT9` array followed by one packed record per vertex. Nothing in
the shim read it, so the mesh had no UVs and every pixel sampled texel 0 - which
looks exactly like a missing texture, not like missing UVs. `meshFromNode` now
falls back to `DeclData` for whichever of normals/UVs the named blocks did not
supply. **27 text-format `.x` under `data/skin` and `data/object` have `DeclData`
and no `MeshTextureCoords`.**

**The lesson worth keeping.** All three were the same shape: a text-format `.x`
feature the binary parser had and the text parser did not. Only ~130 of the
several thousand skins are text format, which is why this survived so long, and
why the symptom looked like content damage - a missing NPC, a white mesh, a flat
mesh - rather than a parser gap.

**Verified on device** (LDPlayer x86_64, 2026-09-03): the chest renders with its
`g_box_base.dds` metal-chest texture; `RanD3D: texture 3086 = g_box_base.dds
(512x512, 2 levels)` appears and no `RanXH: ... has no TextureFilename` line
does. Screenshot `MOBILE/native/out/gbox_zoom.png`.

**Still open:** `s_m_bs_leg.X` still fails to load on that device. The manifest
ships it again (it was dropped by an earlier over-broad pack-dedup rule), but the
emulator cannot reach the patch host, so its data is still the pre-fix copy.
Expected to clear on the next successful patch.

## The patch page appeared to vanish and be replaced by a loading screen (2026-09-03)

Both activities use `@style/RanSplash`, whose `android:windowBackground` was
`@drawable/splash` - the bare 1024x512 art, **without the RAN mark**. A window
background is drawn before either activity's own views are, and a drawable there
can only be *stretched* to fill: there is no CENTER_CROP for a drawable. So the
boot ran

    splash.png stretched 2:1 -> 16:9, no mark      (RanLauncher window background)
    ran_loading cover-fit + mark + status band     (RanLauncher's own views)
    splash.png stretched again, mark gone          (RanActivity window background)
    ran_loading cover-fit + mark                   (the native GL splash)

The mark disappearing and the photo jumping crop, twice, is what read as "the
patch page disappears and then a loading page shows".

`MOBILE/tools/rcc-extract/compose-splash.js` now composes `splash.png` as the
cover-fit result *plus* the mark at 16:9, so stretching it to fill is a no-op on
a 16:9 panel and a couple of percent on 16:10. All four frames are then the same
picture.

**Measured, not asserted.** A throwaway build with a 2.5 s sleep before
`setContentView` exposed the window-background frame on its own for capture
(`wbg_1.png`); on a normal boot the frames after the launcher's page are
byte-identical to it (`md5 12a9009a...`, v_03 through v_05). The activity could
not simply be started directly to capture it - `am start` on RanActivity is
refused, `not exported from uid 10074`, which is the export hardening working.

Note for anyone repeating this: there is no ffmpeg on this machine and
`screenrecord` has no raw-frame output, so `screencap` bursts are the only frame
source, and they are far too slow to catch a sub-second transition by luck. Slow
the app down and capture deliberately instead.

### Correction: the flash was a black frame, not a mismatched picture

The composed `splash.png` above was the right thing to do but it was **not** the
reported bug, and claiming it was fixed on one sampled boot was wrong - a
`screencap` burst samples about every 400 ms and cannot see an 80 ms event.

Captured properly (raw `screencap` in a device-side loop at 640x360, ~10 fps,
90 frames per boot), the boot is:

    launcher page   mean 126 / 12% dark
    BLACK           mean   0 / 100% dark      <- one frame, every boot
    boot art        mean 189 /  0% dark

and logcat gives its length:

    +0 ms    START RanActivity
    +20 ms   Displayed RanActivity        <- starting window handed back
    +72 ms   GLES renderer ready
    +99 ms   RanSplash: boot screen up    <- first pixels on the surface

**~80 ms of an opaque window with nothing drawn in it.** Three fixes were tried
and measured, and two of them did nothing:

- `windowBackground` on RanActivity - belongs to the starting window, which is
  exactly what has already been taken away. 5/5 boots still black.
- a full-screen `ImageView` added in `onCreate` - a NativeActivity window is
  rendered by the native side through `ANativeWindow`, not by the View
  hierarchy, so it never reaches the surface. "Displayed" stayed at +20 ms.
  5/5 boots still black.
- the platform SplashScreen API's `setOnExitAnimationListener`, to hold the
  splash until told - the Android 12+ splash screen only appears on a cold start
  from the launcher icon, never on an activity-to-activity switch inside the
  app, so the listener never fires. 5/5 boots still black.

What works is to stop the window being opaque before it has anything in it.
RanActivity's theme is `windowIsTranslucent`, so the launcher underneath shows
through for those 80 ms; RanLauncher no longer finishes at `startActivity` (nor
in `onStop`, which a translucent activity on top does not even trigger) but
waits to be dismissed. On its first present the native side calls
`RanAndroid_BootScreenUp` -> `RanActivity.ranBootScreenUp`, which puts the
window back to `PixelFormat.OPAQUE` - a translucent window would otherwise make
SurfaceFlinger blend every frame for the whole session - and dismisses the
launcher.

**Measured after:** 0 black frames in 8 boots (5 at 640x360, 3 at 1280x720),
against 5/5 black before. In-world afterwards: 34 fps at the same spot that gave
32 fps earlier the same day, so the translucent window costs nothing measurable
on the emulator. Not yet checked on the Tab S9 - it was offline.

**Unattributed:** one SIGSEGV during a login run, in
`RanTexture::LockRect` (`d3d9_impl.cpp:238`) reached from
`RanD3DXFont::glyphFor` while `CInventoryUI_TradeInven::CreateSubControl`
measured text. It did not reproduce on the next run and nothing changed in that
path, but it is not explained.

### Correction again: the translucent window was wrong, and unnecessary

The previous entry is superseded. It was arrived at by trying mechanisms and
measuring each one, instead of reading how this codebase's window and blending
actually work first. Two things in our own code rule the approach out:

- **`setBlend` uses `glBlendFunc`, not `glBlendFuncSeparate`**, and nothing masks
  the alpha channel (`shim/gl/gl_render.cpp:696`). `SRC_ALPHA/ONE_MINUS_SRC_ALPHA`
  therefore blends alpha as well as colour, so every semi-transparent panel -
  chat box, tooltips, the name plates - drives destination alpha below 1. The
  client clears with `D3DCOLOR_XRGB`, which is `D3DCOLOR_ARGB(0xff,...)`, so the
  frame *starts* opaque and is eroded from there. Against a translucent window
  those pixels composite as see-through. That would have shipped.
- **The shim cannot survive a window-surface recreation**, which is what
  `setFormat(PixelFormat.OPAQUE)` does. `RanGL_Init` opens with
  `if (g_ready) return 1;` (`shim/gl/gl_context.cpp:97`) and `APP_CMD_TERM_WINDOW`
  only sets `st->ready = false` (`platform/android/android_main.cpp:864`) - no
  `eglDestroySurface`, no re-create. A second `APP_CMD_INIT_WINDOW` does nothing
  and the stale EGLSurface stays bound to a dead window. That is the 550 ms of
  nothing measured on the Tab S9 after `setFormat`, which was misread at the
  time as an animation.

And there was nothing to cover. On the Tab S9:

    Displayed RanActivity   02:17:13.229
    boot screen up          02:17:13.241     <- 12 ms

Twelve milliseconds, under one frame. The 80 ms hole is the emulator's EGL init
(51 ms of it), not the hardware players run on.

So all of it is reverted - translucency, `windowDisablePreview`, the Java cover
view, the SplashScreen hold, `setFormat`, the deferred launcher dismissal and the
`RanAndroid_BootScreenUp` callback. What remains is the one change the evidence
supports: **the composed `splash.png`**. The build the tablet was actually
running (versionCode 18, V001) drew the old asset - the bare 2:1 art, no RAN
mark, stretched to fill - for RanActivity's starting window, so a different
picture appeared between the patch page and the boot art.

**Measured after the revert, on the Tab S9:** 4/4 boots go
home (54) -> patch page (176) -> boot art (189) -> login (81), with no dark frame
and no dip.

**Method note.** `screencap` bursts over adb sample ~every 400 ms and cannot see
an 80 ms event; one clean burst is not evidence. Capture with a device-side raw
`screencap` loop (8-10 fps) writing to `/data/local/tmp`, repeat at least three
times, and read the mean-luminance trace rather than eyeballing single frames.

### The boot screen is now the patch page itself

Requested: show the patch page, and when patching finishes go straight to the
login page - no separate loading screen. The ~2.5 s of `RanApp_Boot` cannot be
removed, only covered, and it was being covered by a bare art screen with no
band, which is the "loading page" that should not exist.

Rather than rebuild the band and its text in GL - before the client has a font,
and destined to drift from the launcher's layout - **RanLauncher rasterises its
own view hierarchy** just before `startActivity` and leaves it in
`<root>/cache/bootcover.bin` (`"RANC"`, width, height, then RGBA). `RanSplash`
draws that verbatim over the whole surface, and falls back to composing art +
mark if the file is not there (game started without the launcher, or the write
failed). Half resolution, so ~4 MB and a sub-frame write; `cache/` because the
patch manifest does not list it, so a patch never fights over the file.

The launcher also had to take the same immersive flags the game sets in
`goFullscreen`: laid out inside the navigation bar its page is 1568 px tall
against the surface's 1600, and the handover stretched it by 2%.

**Measured on the Tab S9, 3 boots:** home (54) -> patch page (179) -> boot
screen (180) -> login (81). Patch page against boot screen, pixel for pixel:
mean absolute difference 2.5 of 765, with 1.0% of pixels differing by more than
60 - all of it text antialiasing from the half-resolution capture. Before this,
the boot screen read 189 against the page's 176.

In world afterwards on the Tab S9: 120 fps, and the PVP reward chest renders
with its texture.

## The chat resize grip was unhittable, not broken (2026-09-03)

`CHAT_LEFT_BAR_TOP` - the little arrow at the chat's top-left - is what resizes
the chat: it posts `UIMSG_MOUSEIN_LEFTBAR_DRAG`, and `CBasicChat::Update` then
moves the window's top edge to follow the pointer. It is **19x15 layout units**.
The client lays out at half the panel width, so on a Tab S9 that is 38x30
physical pixels, about 18x14dp against the 48dp a finger needs - and it has to
be dragged, not tapped.

The drag was never broken. `adb shell input swipe` from the middle of the grip
expands the chat exactly as on PC, verified before changing anything.

`CUIControl::SetTouchPad` (RAN_MOBILE only) widens just the rectangle
`MouseUpdate` tests, leaving the artwork alone. It defaults to zero, so every
other control keeps the identical test.

**Both gates have to be widened.** Padding the grip alone looked like it did
nothing. The probe said why:

    from inside the old rect:  LEFT_BAR drag msg=0x01000042 mousein=1  -> 22 drag frames
    from the padded zone:      LEFT_BAR drag msg=0x01000082 mousein=0  ->  0 drag frames

The grip posted the message and `CBasicChat::TranslateUIMessage` dropped it,
because that case also requires the **LEFT_BAR group** to report `UIMSG_MOUSEIN`
and the group is the same 19 units wide. Padding both to 18 units gives a
55x51-unit target, a little over 48dp each way.

Downwards it reaches y=33 inside the left bar and the chat-state buttons there
start at y=41, so nothing else loses a tap - confirmed by tapping the chat-mode
button afterwards and watching the icon still cycle.

**Measured on LDPlayer**, collapsed mean luminance 69 against expanded 33:

    from (875,1145) right+below:  70 -> 32   expands
    from (845,1160) below:        73 -> 32   expands
    from (890,1115) right:        76 -> 33   expands
    from (875,1075) above:        70 -> 44   expands
    from (808,1115) left:         71 -> 103  no - that is off the chat window
                                             entirely and the tap goes to the world

**Not yet checked on the Tab S9** - it dropped off adb again before this could be
run there.

## Rebranded to Ran Legacy M (2026-09-03)

One square logo (1254x1254 JPEG, gold artwork flattened onto black) drives the
launcher icon, the patch page, the boot screen and the client's own login mark.

**Keying.** The source has no alpha and the logo's own metal is nearly black in
places, so a luminance threshold punches holes through the middle of it. The
black is removed by **flood-filling inwards from the border** instead - only
black connected to the edge is background. 45% of the image comes out
transparent and the internal darks survive. `tools/rcc-extract/make-icons.js`.

**Where it went.**

- `res/mipmap-{m,h,xh,xxh,xxxh}dpi/ic_launcher.png` - the legacy icon, logo
  composited back onto its black square.
- `res/mipmap-*/ic_launcher_foreground.png` + `mipmap-anydpi-v26/ic_launcher.xml`
  - adaptive, artwork inside the 66-of-108 safe zone over a black background
  layer, so a launcher can mask it to any shape without clipping.
- `res/drawable-nodpi/ran_mark.png` - the mark on the patch page. The launcher
  drew the old wide wordmark at 230dp; a square logo at 230dp is a third of the
  page, so that is now 170dp, and `compose-splash.js` matches it at 13.7% of
  the width.
- `textures/gui/ranlegacy_mark.dds` + `LOGIN_MARK` in `uioutercfg.xml`, repacked
  into `Gui.rcc` - the logo on the login and server-select screens, 150x150
  where the old wordmark was 177x96.

**Why DXT5, and why that needed writing.** There was no DDS *encoder* in the
tree, only decoders. It could not be skipped: `TextureManager` chooses how a
texture is drawn from the format it comes back as, and `D3DFMT_A8R8G8B8` lands
in the `EMTT_ALPHA_HARD` case - alpha test, no blending - which would cut the
logo's glow into a jagged edge. DXT5 is `EMTT_ALPHA_SOFT`, what every other UI
mark uses. `tools/rcc-extract/dds-encode.js` writes DXT5 with a full mip chain;
fully transparent texels are left out of the colour fit so the black they were
keyed from cannot drag the visible edge muddy.

**Name.** `android:label` was "RAN" in three places, now "Ran Legacy M". There
was no `android:icon` at all before this - the app was showing the stock Android
robot.

**Verified on LDPlayer:** the home screen shows the logo and "Ran Legacy M"; the
patch page and the boot screen carry it; the server-select screen draws it with
its glow blending against the sky, which is the DXT5 path doing its job.

**Trap for next time.** `adb push` with an absolute device path needs
`MSYS_NO_PATHCONV=1` under Git Bash. Without it the destination is rewritten to
`C:/Program Files/Git/storage/...`; the push *reports success* and the device
keeps the old file. The first Gui.rcc push did exactly that and the login screen
was still showing the old wordmark for it.

## The patch page is now the game's loading screen (2026-09-03)

New background art (`ran_old_film.dds`, 4096x2828 DXT5, the sepia class group
shot) and the two panels the client's own loading screen uses.

The proportions are not invented. `LoadingThread.cpp` lays that screen out in a
1024x768 virtual space:

    ld_top.dds   @ 0,0   1024x140   drawn 1024x128 at (0,0)
    the art                         drawn 1024x512 at (0,128)
    ld_under.dds @ 0,7   1024x140   drawn 1024x128 at (0,640)

so each band is **128/768 of the height** whatever the panel is, and the art has
the middle two thirds. The launcher page and `compose-splash.js` both build to
exactly that, which is the point of it: the player sees this screen and then,
moments later, the client's map loader draws the same one.

The bands stretch to width (`FIT_XY`) because they are a frame, not a picture -
their ends have to meet the edges of the screen - while the art is cover-cropped
into the middle.

**The logo moved into the top band.** Below it, it landed on the group's heads
and read as clutter. The band is empty by design; it is where the client puts
the map name. It is sized at 82% of the band rather than in dp, so it keeps its
margin on any panel.

**The status band lost its scrim.** It used to paint `#B4000000` behind the text;
over `ld_under` that was a second dark rectangle on a dark panel and showed as a
seam. The text and bar now sit directly on the band.

`ran_old_film.dds` lives in `MOBILE/art/`, not `CLIENT/`: it is drawn from the
APK - the launcher paints this screen *while* provisioning the data root, so
nothing it needs can come from there - and putting it in `CLIENT/` would ship
11.5 MB to every player for a file the game never reads.

`extract-launcher-art.js` no longer writes `ran_mark.png`. `make-icons.js` owns
it now that the mark is the Ran Legacy logo instead of a crop of a client sheet,
and leaving the old job in would have quietly put the RAN ONLINE wordmark back
the next time anyone ran the extractor.

## Ride/dismount button beside the chat (2026-09-03)

No keyboard on a phone, and the vehicle is otherwise only reachable by opening
the equipment window and double-clicking the slot every time.

`GLCharacter::ReqSetVehicle(bool)` is the request the PC path already uses and
`m_bVehicle` is the current state, so the button is a straight toggle of the
two. It re-checks nothing on purpose: that function already refuses when the map
forbids vehicles, the battery is flat, or the character is attacking, casting or
falling - and prints the reason itself.

The control is a child of `CBasicChat`, positioned **every frame** against the
chat's outside edge rather than laid out once, because the chat is dragged and
resized and a button left where the chat used to be is worse than no button.
Groups do not clip their children, so sitting outside the parent's rect draws
and hit-tests normally. The press also raises
`UIMSG_MOUSEIN_BLOCK_CHARACTER_MOVE` so it cannot double as a tap on the ground
behind the chat, and it takes the same `SetTouchPad` as the chat grip - 35x35
layout units is about 33dp, under the 48 a finger wants, at the screen edge
where a thumb is least precise.

The art is the game's own vehicle equip slot (`GUI_Inven_Slots.dds` @70,35),
added to `uiinnercfg01.xml` as `MOBILE_VEHICLE_BUTTON` / `_F` and repacked into
`Gui.rcc`. Only RAN_MOBILE code names those ids, so the entries are inert for
the PC build.

**Verified:** the button draws in the right place, parked on the chat's bottom
outside corner (`out/veh_z.png`).

**Not verified:** that it actually mounts. The test character has no vehicle
equipped, and with none `ReqSetVehicle(true)` returns early at
`!m_sVehicle.IsActiveValue()` *without* a message - so a tap is correctly
silent and proves nothing either way. Needs a character with a bike.

## Auto-target prefers what the character is facing (2026-09-03)

`MobileFindNearestMob` and `MobileFindNearestPvP` both took the strict nearest,
which is often not what the player means: standing between two mobs, the one
behind wins by a step and the character spins round to fight it.

Both now track **two** answers in the same pass - the nearest thing in front and
the nearest thing at all - and return the second, unchanged, when nothing is in
front. So the change can only alter which target is picked when one is being
faced; with none it is the old behaviour exactly, including all the dead-crow
and `IsPK_TAR` filtering.

Facing comes from `GLCharacter::m_vDir`, which is a persistent world heading -
it keeps its value while standing still, so the preference works when stationary
and not only while running.

Two judgement calls, both in the code as comments:

* **Flattened to XZ.** `m_vDir` is a heading, not a look direction. A mob up a
  slope or down a stairwell is still in front, and comparing the full 3D vectors
  would drop it for no reason the player can see.
* **90 degree window** (`cos(45)` either side). Wider starts choosing things off
  the shoulder that do not look aimed at; much narrower is hard to satisfy while
  moving, and then the preference never fires and it is the old behaviour with
  extra arithmetic.

A target standing on top of the character counts as faced: there is no direction
to test, and answering "not facing" would push the player at something further
away.

**Verified:** builds for both ABIs, and auto-target still selects in world -
`Lv.2 Little Vulgarian 180/180` on the panel with the bar over the mob beside
the character (`out/auto_test.png`).

**Not verified:** that it prefers the faced one over a nearer one behind. That
needs a controlled position between two candidates, which is what the player
will see immediately in normal play.

## Vehicle button, and skill icons that would not centre or respond (2026-09-03)

**The toggle did nothing.** The button called `ReqSetVehicle` directly. That
looked equivalent to pressing V and is not: `InnerInterface`'s `DIK_V` handler
refuses while a trade is open, enforces a one second cooldown through
`m_fVehicleDelay` - printing `VEHICLE_SET_FB_DELAY` when it bites - and resets
that timer afterwards. Going straight to the request skipped all three, so a
press inside the cooldown did nothing *and said nothing*. It now taps DIK_V
(0x2F) through `RanInput_KeyTap`, which is the pattern the other mobile buttons
already use: express the action as the key press the client is written to react
to, and nothing downstream has to know a finger did it.

**The icon was an empty socket.** The first cut used the vehicle equip slot out
of `GUI_Inven_Slots.dds`, which is a socket outline rather than a picture of
anything. `tools/rcc-extract/make-vehicle-button.js` now composes the game's own
motorcycle icon onto a round face in the touch-button idiom - dark chrome, a lit
rim, amber when pressed - and writes `mobile_vehicle.dds` / `_f.dds` as DXT5.

The sheet coordinates were measured, not eyeballed: `sc_bike_gui.dds` has 32x32
cells whose borders in the bike row sit at x=418/421 and 453/456, so the red
sportbike's content is x 422..452, y 213..241. The first attempt guessed 413 and
pulled in the border and half the neighbouring cell.

**Skill icons rode low, and taps missed.** The icons were centred on
`QUICK_SKILL_IMAGE` while their ring is drawn from the slot, and the two boxes
do not share a centre: `BASIC_QUICK_SKILL_TRAY_SLOT0` is **41x38** and
`QUICK_SKILL_IMAGE` is **35x35 at (3,3)**, so the image centre is 1.5 units
below the slot centre. Every icon sat low in its ring - and since the slot rect
is what receives the press, a tap aimed at the art could land outside it, which
is why tapping "did nothing". Both the ring and the icon now come from the slot.

**Verified on LDPlayer:** icons sit centred in their rings; tapping slot 1 puts
it on cooldown, raises the skill tooltip and casts (world changes by 91/pixel).
The motorcycle button draws correctly beside the chat.

**Not verified:** that the toggle mounts. The test character has no vehicle, and
`ReqSetVehicle` returns early with no message when `m_sVehicle.IsActiveValue()`
is false. It now takes exactly the PC path, so if a bike is equipped and the map
allows it, it behaves as V does on PC - including the cooldown message.

### Why the vehicle button drew but did not respond

Measured, not reasoned. The chain, probe by probe:

1. the touch reaches the overlay at **393,685** with `inClientUI=1` - the UI
   works in logical pixels, half the 786,1371 the screenshot showed, and the
   overlay correctly yields to the client;
2. the chat group dispatches `VEHICLE_BUTTON` every frame, so the control is
   registered and reachable;
3. the message was **0x80 MOUSEOUT** while the button's own logged rect was
   **370,664..412,706** - which contains 393,685.

Rect right, answer wrong. The only thing that explains both is *when* the rect
is set: `CBasicChat::Update` placed the button **after** calling
`CUIGroup::Update`, and that call is where children run their own
`MouseUpdate`. So the hit test used the rect from before the move while the draw
used the rect after it - tested in one place, painted in another. The placement
now runs above `CUIGroup::Update`.

Then it works in both directions, confirmed on screen and in the log:

    DIK_V seen: active=1 riding=1 delay=17.76 trade=0
    ReqSetVehicle(0) -> 0x00000000      (dismount)
    DIK_V seen: active=1 riding=0 delay=28.32
    ReqSetVehicle(1) -> 0x00000000      (mount, character is on the bike)

**Lesson worth keeping:** a control repositioned from its parent's `Update` has
to be moved *before* the base-class call, or its hit rect trails its art by a
frame - and if the control sits outside the parent, "a frame behind" means "a
whole button away".

**LDPlayer note:** `com.android.vending` and `com.android.ld.appstore` were
disabled with `pm disable-user` on the emulator. Both kept stealing the
foreground mid-test, and one whole round of "the button does not work" was
actually taps landing in the Play Store. Re-enable with `pm enable` if the
emulator is wanted for anything else.

### The ride button is now one of the pad's own buttons

It was a client UI control with a texture, so it could only ever look
*approximately* like the joystick, the attack ring and the mode toggles - those
are drawn by the overlay in GL, from the same palette and the same primitives.
Approximately similar is exactly what it looked like: first a socket outline,
then a 30 px item sprite stretched on a flat disc.

It is now `RANTOUCH_SLOT_VEHICLE`, an overlay button with the same face, rim and
press behaviour as auto-target and PK, and a motorcycle drawn with the same
primitives `artSword` and `artChest` use - `drawRing`, `drawCapsule`, `artPoly`,
in `kSteel` / `kBlade` / `kEdge` with `kGold` accents.

The split: **the client says where, the overlay says how big and how it looks.**
The button belongs beside the chat and the chat is dragged, so only the centre
can come from the client; taking the radius and the drawing from the overlay is
what makes it identical to its neighbours by construction instead of by eye.

Two attempts it took to make the glyph read at button size, both worth keeping:

* dark tyres on a dark face vanish - the wheels have to be the *light* part, or
  it is a blob with two smudges;
* filling the body pale is the same mistake - only the top edges are lit, and
  the shape is three pieces (a tail humping over the rear wheel, a tank, a
  fairing dropping to the front) because two pieces read as a bicycle.

The press is handled where every other pad button is, as `nKey = DIK_V`. This
also retires the client-side control, its two XML entries, its two DDS textures
and their generator - and with them the ordering trap where a control positioned
after `CUIGroup::Update` drew in one place and hit-tested in another.

**Verified on LDPlayer:** the button sits beside the chat in the pad's own
livery, and pressing it puts the character on the bike.

## Riding cost half the frame rate, and it was the vehicle's shadow (2026-09-03)

**Measured first, on the emulator, standing still in one spot.**

| | fps | frame | opaque verts | alpha verts | off-screen draws |
|---|---|---|---|---|---|
| on foot | 34 | 32.6 ms | 71,538 | 27,558 | 6 |
| mounted | 9 | 112.8 ms | 249,426 | 119,510 | 16 |
| mounted, no vehicle shadow | 13 | **54 ms** | 108,498 | 64,660 | 6 |

**Where the time went**, established before changing anything:

* submitting draws is **2% of the frame in both states**, so it is not the CPU;
* `/sdcard/ran/plainfs` - drop everything the fragment shader does after the
  texture fetch - changed nothing, so it is not shading;
* `/sdcard/ran/nulldraw` - skip the GL work of every draw - took the mounted
  frame from **113 ms to 16.7 ms**. So ~96 ms of it is the GPU chewing geometry.

The shadow pass draws the whole vehicle a second time into the shadow target,
and a vehicle is a far heavier mesh than a character. Cutting it halves the cost
of riding.

**The engine's own LOD is a stub.** `USE_SKINMESH_LOD` is commented out in
`DxSkinDefine.h`, and enabling it would achieve nothing: `g_dwLOD` is set - to 1
for shadows, and by distance - but **nothing anywhere reads it to choose a
mesh**. It is only ever set and counted into `g_dwHIGHDRAW_NUM`. Worth knowing
before anyone reaches for it again.

**Still open.** Riding is 54 ms against 32.6 on foot, so the vehicle's own
on-screen geometry still costs ~21 ms: +37k opaque and +37k alpha vertices over
being on foot. The alpha half is the suspicious part - ~37k alpha-blended
vertices for a motorbike suggests its parts are going through the blended path
rather than the opaque one, which would follow from DXT5 textures
(`TextureManager` maps DXT5 to `EMTT_ALPHA_SOFT`). Not chased yet, and not
guessed at: it needs the same measurement treatment.

**Not verified on the Tab S9** - it has been off adb throughout. The emulator's
GPU is not the tablet's, and the ratio may differ there.

### Reverted: auto-target no longer prefers what the character is facing

Tried in play and it did not feel right, so it is out. `git revert` of the
facing commit, and the diff against the version before that change is empty -
`MobileFindNearestMob` and `MobileFindNearestPvP` are byte-identical to what
they were, nearest-only, with all the dead-crow and `IsPK_TAR` filtering intact.

Verified after the revert: pressing auto-target still selects - `Lv.2 Little
Vulgarian 180/180 (100%)` on the panel - and the world runs at 36 fps.

Worth keeping in mind if it ever comes up again: the idea is sound on paper and
the implementation was cheap (two candidates tracked in one pass, falling back
to nearest when nothing is in front). What made it wrong was play feel, not
correctness, and that is not something the code can tell you.
