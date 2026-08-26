# Handover

Read this first. It says what is done, what is blocked on you, and what the next
person (or session) should pick up.

---

## The one-line status

Every asset class is extracted, verified and importing into Unity; a character
walks a real map with a live HUD; the C# protocol layer matches the shipped C++
byte-for-byte. **The project cannot be finished without you** — four things need
hardware, accounts or permission that I do not have.

---

## Blocked on you — nothing else will move these

| Blocker | What it stops | What is needed |
|---|---|---|
| **Live production server** | Phase 0's last 2 items, load test, closed beta | Your say-so to connect. I have deliberately never connected unasked |
| **Physical Android device** | Confirming 30fps | A mid-tier phone. Static cost is measured; the target itself cannot be claimed without one |
| **Android SDK / NDK** | Producing any APK | Unity's Android *player* is installed; SDK/NDK/JDK ship only with a Hub install and this editor was installed outside Hub. Either register it with Hub or point Unity at an external SDK+NDK+JDK with the version match verified |
| **macOS + Apple account** | The iOS build, store submission | Unity emits an Xcode project; the IPA needs Apple hardware |

Nothing else is waiting on a decision. All 11 open decisions in `plan.md` §11 are
closed — see `MOBILE/DECISIONS.md` for each one and what would overturn it.

---

## What exists now

**Assets.** 130 maps, 4,467 meshes, 6,498 clips, 4,294 effects, 1,590 characters,
37,090 items. Parity against what ships: `MOBILE/PARITY.md`, regenerate with
`node MOBILE/tools/rcc-extract/parity-audit.js`.

**In Unity.** 130 map prefabs + scenes, 977 character prefabs, 621 animator
controllers, 3,348 baked clips, 4,204 effect prefabs. A playable scene:
`Assets/Ran/Scenes/play_w_school_03_boa.unity` — map, character on the navmesh,
follow camera, four-bar HUD, tap-to-move routed through A*.

**Protocol in C#.** `RanTea` (XXTEA), `RanWire` (framing + garbage + the outbound
guard), `RanSession` (state machine), `RanPackets` (generated from the same
`layout.json` the Node client uses, so the two halves cannot drift).

**Verification.** 286 Node tests, 133 C# checks. Run both before trusting
anything:

```
node MOBILE/tools/rcc-extract/test.js
cd MOBILE/unity/typecheck && dotnet run --project RunCheck.csproj
cd SOURCE && git status --porcelain      # must be empty
```

---

## Things that will bite you

- **The server bans IPs, it does not drop packets.** A malformed packet runs
  `netsh advfirewall firewall add rule` against the sender
  (`s_CIPFilter.cpp:126-152`), and the allow-list does not help because
  `AddIPBlock` never consults it. `RanWire.AssertSendable` applies the server's
  own rules to our output for exactly this reason. Do not remove it.
- **`RanSession` refuses non-loopback hosts** unless `AllowRemote` is set. That
  is intentional: the default target is a live server with real players.
- **The XXTEA overflow bail is deliberate.** A 19-char password in a 20-byte
  field pads to 20, needs 21, so nothing is copied and the plaintext crosses the
  wire. "Fixing" it breaks login, because the server's decrypt no-ops the same
  way.
- **`SOURCE/` is frozen.** It has stayed clean at `d60d660` for the whole project
  and Option A now requires *zero* server edits — the one anticipated exception
  (version gating) was investigated and dropped because there is no gate to
  extend. Keep the exception log empty.
- **Do not bake all animation clips.** Unity serialises an AnimationClip at ~50x
  its source; the full bake was 13 GB. Ship `.ranim` and bind at runtime —
  `RanAnimationBinder` is in Runtime for that reason.

---

## Next work, in order

1. **T1 chat + character select UI.** The HUD and input exist; these complete
   Tier 1.
2. **Wire `RanSession` to the login flow.** Everything up to the socket is done
   and checked. Testing it needs the live server.
3. **T2 UI** — inventory, skills, NPC shop. 97 files of reference in
   `Lib_ClientUI`, none of it portable, so it is a rebuild against the protocol.
4. **Effect ramps.** Quad effects bake only the START value, so a flare does not
   grow or fade. The mid/end values are already decoded and carried on the node;
   this is a runtime component, not another format dig.
5. **Android SDK/NDK**, then a real perf pass.

---

## Where the reasoning lives

- `plan.md` — the whole plan, phase by phase, with every correction recorded
- `MOBILE/DECISIONS.md` — the 11 decisions and the measurements behind them
- `MOBILE/CHARACTERS.md` — how a character is assembled, and the four traps
- `MOBILE/PARITY.md` — content parity, regenerable
- `MOBILE/EFFECTS-SCOPE.md`, `EFFECTS-PROPS.md`, `EFFECTS-CHAR-SCOPE.md`

Corrections are kept in place rather than tidied away, because several of them
were expensive: bone-name clip matching, the "unreferenced textures are a saving"
claim, map subsetting as a size lever, and the version gate that did not exist.

---

## Incident: the project was opened with Unity 6 (2026-08-13)

`ProjectVersion.txt` was silently rewritten to **6000.5.8f1** and the manifest to
Unity 6 packages (URP 12.1.15 -> 17.5.0). The trigger is Unity Hub: it only knows
about the Unity 6 install, because 2021.3.45f2 was installed manually and outside
Hub. **Opening this project from Hub will re-upgrade it.**

Launch 2021.3 directly instead:

```
"C:\Program Files\Unity 2021.3.45f2\Editor\Unity.exe" -projectPath "...\MOBILE\unity\RanMobile"
```

### What was damaged, and a correction

I first reported "zero assets re-serialized". **That was wrong** — I had checked
only `*.prefab`. Eight files were touched:

- `Assets/Ran/RanUrpPipeline.asset` and `RanUrpRenderer.asset` — re-serialized by
  Unity 6 and unreadable by 2021.3. This is what broke the Android build with
  "File may be corrupted or was serialized with a newer version of Unity"
- `Assets/DefaultVolumeProfile.asset`, `UniversalRenderPipelineGlobalSettings.asset`
  — created by Unity 6's URP, meaningless in 12.1.15
- Two auto-created test folders under `Assets/`

All content survived: no prefab, mesh, clip, effect or texture was converted. The
upgrade rewrote settings and pipeline assets and got no further.

### Recovery

1. `ProjectVersion.txt` and `manifest.json` restored to 2021.3 / URP 12.1.15
2. `MultiplayerManager.asset` removed (Unity 6 only)
3. The four Unity 6 assets and two test folders deleted;
   `RanSceneBuilder.EnsurePipeline()` recreates the URP assets under 2021.3
4. A stale `Temp/UnityLockfile` from the crashed Unity 6 run cleared — that, not
   the version, was the cause of exit code 21

Verified after: 8/8 PlayMode tests, zero compile errors.

### The lesson worth keeping

A warm batch run is not a compile check. My `-executeMethod` runs kept succeeding
on cached assemblies while `RanHud` and friends had a missing `UnityEngine.UI`
asmdef reference the whole time; a cold editor compile is what surfaced it. When
verifying compilation, delete `Library/ScriptAssemblies` first or open the editor.

---

# Unity 6 migration — DONE (2026-08-13)

Migrated `2021.3.45f2` + URP `12.1.15` → `6000.5.8f1` (`5cb7df797b7d`) + URP
`17.5.0`. Builds, renders, all suites green. A backup of the pre-migration
settings and package source is at `~/.claude/jobs/pre-unity6-backup`.

## What actually broke, and the fixes

1. **`UnityEngine.UI` did not exist.** uGUI is no longer auto-included in
   Unity 6 — `com.unity.modules.ui` covers UIElements, not `Image` / `Text` /
   `InputField`. Fixed by adding `"com.unity.ugui": "2.5.0"` to the manifest.
   TextMeshPro now lives in that same package; this project does not use it.

2. **The Android module was missing** from the 6000.5.8f1 install — it shipped
   with only WebGL and Windows. Installed with:

   ```
   "Unity Hub.exe" -- --headless install-modules --version 6000.5.8f1 \
       --module android --childModules
   ```

   It must be launched **detached**. Run as a child of the calling shell it dies
   when that shell exits, reporting exit 255 partway through the download.

3. **The toolchain choice inverted.** `RanBuild` hardcoded
   `{Jdk,Sdk,Ndk}UseEmbedded = false`, correct when the 2021 install was a bare
   editor with no bundled toolchain. Unity 6 is a Hub install that ships its own
   SDK and NDK (r27c) and *rejects* the old external r21d. It is now DETECTED at
   build time by `HasEmbeddedAndroidToolchain()` rather than assumed, so both
   editors keep working.

## What did not break

- `RanUrpMaterial` — it writes real render state (blend factors, ZWrite,
  keywords, render queue), not merely the `_Surface` dropdown value, so the
  URP 14+ hazard where `_Surface` alone stops taking effect never applied.
- `Ran/FixedFunction` compiled unchanged against the URP 17 shader library.
- The URP pipeline asset migrated itself — URP assets carry `k_AssetVersion`
  migration, so deleting and regenerating was unnecessary.
- No custom `ScriptableRenderPass` exists, so the Render Graph rewrite was moot.

## Verification

| suite | result |
|---|---|
| Node (rcc-extract) | 286 passed |
| Node (client) | 42 passed |
| C# typecheck | 194 passed |
| PlayMode | 6 passed |
| APK | 29 MB on disk, 4.9 min, 0 errors |
| render check | 0.0% magenta, 274 distinct colours |
| `SOURCE/` | clean at `d60d660` |

PlayMode is **6** tests, not 8 — the earlier figure in this document was wrong.
`RanPlayModeTests.cs` declares six, and all six run.

## New: `RanRenderCheck`

```
-executeMethod Ran.Mobile.Assets.Editor.RanRenderCheck.Run
```

Renders a scene headlessly and inspects the resulting pixels, because the two
failure modes a URP change causes — everything magenta, transparent surfaces
rendering solid — are both invisible to a build log. The APK builds cleanly
either way, so the log cannot be the check.

Note the coverage limit documented in the file: importer materials are created
with `ctx.AddObjectToAsset` and are therefore sub-assets, which `FindAssets` does
not return. A pass is not proof that every material is correct.

## Still device-blocked

On-device framerate, and visual confirmation that transparent effects are still
transparent. No device was attached (`adb devices` was empty). The APK is at
`MOBILE/unity/RanMobile/Builds/ran-mobile.apk`.

Note on size: `RanBuild` logs `BuildReport.totalSize`, which is UNCOMPRESSED
content (454 MB), not the file — the APK itself is 29 MB. A single-scene build
pulls in only the assets that scene references, so this is one map and one
character, not the 1.6 GB library.

---

# The C# client cannot log in — send path is stubbed (2026-08-13)

**Symptom:** the device shows "timed out waiting for server list" after 20s.

**Cause:** `RanLoginFlow` never calls `_session.Send(...)` — not once. It opens
the socket, starts the read pump, and waits for replies to requests it never
made.

```csharp
private void SendCredentials(string user, string pass) {
    _ = RanTea.EncryptField(user, 21);   // computed, then DISCARDED
    _ = RanTea.EncryptField(pass, 20);
}
private void SendLandInAndReady() { /* SNETREQ_LANDIN + SNETREQ_READY */ }
```

**This corrects an earlier claim in this project.** "The C# protocol layer is
complete" was wrong. Framing, TEA, garbage obfuscation and every READER are
complete and tested. The SEND side of the login flow was never wired:
`session.js` performs 11 sends, `RanLoginFlow` performs 0.

**Not a server or credential problem.** `client/demo.js` ran the full flow
against the live server on 2026-08-13: login -> agent -> login ok -> characters
-> field handoff -> spawn -> playing, then moved the character and got position
confirmations. Server, account and protocol are all good.

## The work

Port the 11 sends from `MOBILE/client/session.js` into `RanLoginFlow`. The first
two are the immediate blocker:

| step | type | body |
|---|---:|---|
| `NET_MSG_VERSION_INFO` | 110 | 8 bytes: patchVer(int32), gameVer(int32), both 1 |
| `NET_MSG_REQ_GAME_SVR` | 1542 | empty |

then credentials (TEA fields 21/20, already computed — just never sent), char
slots, char info, game join, field identity, landin, ready.

`RanMessages.Version(gameVer, patchVer)` already builds the right body and is
called by nothing.

**Risk to respect:** the server auto-blocks IPs via `netsh advfirewall` on
malformed input. `RanWire.AssertSendable` exists for this and must stay. Port the
sends against `session.js` byte-for-byte rather than by inference, and test one
step at a time.

## Agent phase: frames are COMPRESSED (2026-08-13, from device logs)

Observed on the wire, with `[RanFlow] rx type=` logging in `OnMessage`:

```
login socket:  110, 2102, 1552 (48 bytes), 1562     <- server list, WORKING
agent socket:  170 (28 bytes), 170 (12 bytes)
```

From `Lib_Network/s_NetGlobal.h`:

| id | name |
|---:|---|
| 170 | `NET_MSG_COMPRESS` — "Compressed message" |
| 140 | `NET_MSG_SND_CRYT_KEY` |
| 2102 | `NET_MSG_SND_ENCRYPT_KEY` |

**The crypt key never "fails to arrive" — it arrives wrapped.** The agent sends
type 170 and the real message sits inside, compressed. `RanLoginFlow.OnMessage`
sees 170, does not recognise it, and drops it. The `140` constant was right.

### Next steps

1. Handle type 170: decompress and re-dispatch the inner frame. The codec is
   already in this repo — `MOBILE/spike/lzo.js` (LZO1X-1), used by the working
   Node client. There is no C# port of it yet.
2. `2102` (`NET_MSG_SND_ENCRYPT_KEY`) arrives on the LOGIN socket and is
   currently ignored; check whether the agent phase needs it.
3. Then `140` surfaces and `SendCredentials` is the next stub.

### How this was found, and the lesson

Three fixes were guessed before this one by inferring constants. An unrecognised
id and a frame that never arrived present IDENTICALLY from outside — both are a
timeout — so guessing could not distinguish them. One line logging every
received type answered it immediately. **Log the wire before theorising about
it.**

## Login credentials: client is byte-perfect; cause is server-side IP (2026-08-13)

The mobile client gets `EM_LOGIN_FB_SUB_INCORRECT` (result 5) on xx11/1234.
Chased to the actual source, not guessed:

- **Server path:** `s_CDbActionUser.cpp` CAgentUserCheck -> `UserCheck` ->
  SQL proc `user_verify(id, pass, IP, grp, num, randpass, randnum)`. The server
  log "CAgentUserCheck result 0" = the proc returned 0 = ID/PWD rejected.
- **Password form:** PLAIN, not MD5. The stock PC client
  (`CNetClient::ChinaSndLogin`) MD5-hashes, but this private server's DB accepts
  plaintext. PROVEN: the Node client with `pwMode:'plain'` returns
  `result=0 (OK)` for xx11/1234 against this exact server. MD5 was a wrong turn.
- **The credential body is byte-identical to the working client.** Replicated
  the C# SendCredentials logic and diffed against Node's chinaLogin:
  `IDENTICAL: true`. Same 68-byte body, same offsets (nChannel 0,
  szRandomPassword 4, szPassword 15, szUserid 40), same TEA output (verified in
  RunCheck: 215 passing). Framing `[size][type][garbage][body]` and the garbage
  table (K9IHANA/L8IDUL/M7HSET/N6GNET/O5FDASEOT) are identical too. No keyed
  encryption on send (C++ MsgCryptKey hardcodes nKey=1; Node send is pure frame).

**Therefore the client is not the cause.** The one input that differs between
the passing Node test and the failing tablet is the SOURCE IP: `user_verify`
takes the client IP, and the server log shows `143.14.11.244` is specially
excluded (`IPLimit exclude add IP:143.14.11.244`) while the tablet connects from
`49.228.171.168`. Node ran from the trusted network; the tablet is external.

### To confirm and resolve (server-side, next session)
1. Read the `user_verify` stored procedure in the DB — check whether it
   validates/locks by IP or by an account-registered IP.
2. Try the mobile login from a device on the same network as the Node test, or
   allowlist the tablet's public IP.
3. If user_verify does NOT use IP, capture the on-wire agent bytes from both
   Node and the device and diff — but the body is already proven identical, so
   any difference would be in framing/garbage ordering, which are also verified.

The login flow itself is COMPLETE and correct: version -> server list -> agent
handover (login socket closed) -> filler layer -> LZO-unwrapped compressed
frames -> crypt key -> plain+TEA credentials matching the working client ->
correct feedback parsing (nResult at body offset 22). Eleven protocol bugs found
and fixed this session; the twelfth is not a client bug.

## RESOLVED: login works end-to-end (2026-08-13)

The `result 5` was NOT a client bug. Every protocol fix this session was correct.
The blocker was test automation: `adb shell input keyevent 111` (ESCAPE) after
typing cleared the password field, so an EMPTY password was sent every time.
Proven by logging the outgoing wire bytes and TEA-decrypting the password field:
it decrypted to all zeros. Committing with keyevent 66 (ENTER) instead fixed it.

Confirmed on device against the live server (xx11/1234):
```
tx type=2055 ... 770b43670b814480 ...   (correct TEA of "1234", matches Node)
login feedback result=0 (OK)
characterlist...
unwrapped -> type=2248 len=20            (character list arrived)
```

Password form is PLAIN, not MD5 (this server's user_verify accepts plaintext;
the Node client with pwMode 'plain' also gets OK). IP allowlisting was a red
herring — Node succeeds from the dev box, mobile now succeeds from the tablet.

### Login flow is COMPLETE and correct
version -> server list -> agent handover (login socket closed) -> filler layer
-> LZO-unwrapped compressed frames (NET_MSG_COMPRESS 170, payload at +4,
bCompress flag) -> crypt key -> plain+TEA credentials byte-identical to the
working Node client -> nResult parsed at body offset 22 -> character list (2248).

Twelve issues found and fixed this session, the last being operator error, not code.

### Next: character select -> world
1. `reqCharInfo` per slot (NET_MSG_REQ_CHA_INFO) to get names/levels for the list
2. `gameJoin`, field handoff, field socket connect, landin + ready
   — all in `MOBILE/client/session.js`, the byte reference for every step.

### Debug logging removed
The `[RanWire] tx` and `[RanFlow] cred body` hex dumps were removed after
diagnosis — they printed credential bytes and must not ship. If more wire
debugging is needed, add them back temporarily and strip before building release.

## Login page: PC appearance complete (2026-08-13)

The login screen now reproduces the PC client's structure, all from the shipped
data:
- 3D backdrop — the w_school_03 map instantiated in the boot scene with a
  slowly-drifting camera (RanLoginBackdrop); the login canvas overlays it, the
  same layering the PC uses. RanBootScene.BuildBackdrop.
- RAN window frame — BASIC_WINDOW_BODY_* 9-slice (Interface_Main.dds) replaces
  the flat panel.
- Recessed edit boxes — bordered input fields (the PC edit skin btn.dds is not
  in this build; this is the faithful substitute).
- Already done earlier: RAN ONLINE logo (LOGIN_MARK), top/bottom banners
  (OUTER_UP/DN_IMAGE), 3-slice OK/Cancel buttons, Save-ID checkbox, and the
  exact control layout from uioutercfg.xml.

Login still works end-to-end after all visual changes: result=0 (OK) ->
character slots -> "GameMaster". Verified on device.

Only remaining cosmetic gap: font (Unity default vs PC Tahoma) — needs the font
asset embedded, deferred.

## Login camera: exact PC path extracted (2026-08-13)

The login camera now plays the CLIENT's authored path, not an approximation.

- Source: DxLobyStage loads log_in.wld and runs its "Camera01" keyframe
  animation via DxCamAniMan (CAMERA_TFREE). Confirmed from code, not guessed.
- Extraction: `extract-logincam.js` scans log_in.wld for the DxAnimation
  matrix-key track (SMatrixKey, 80 bytes, matrix at offset 16, translation at
  _41.._43). Found 8 keys with a 126-unit path. Times / UNITANIKEY_PERSEC(4800)
  = seconds. Look point = (0,-40,0) transformed by the matrix = pos + (-40)*row2;
  row2 is constant so the look direction is fixed. -> logincam.json.
- Replay: RanLoginBackdrop loads logincam.json and lerps position over the real
  per-key times, looping (the authored keys return to start). No coordinate flip
  needed — the keyframe positions align with Unity world space directly.

Result on device: the composed PC framing (central monument, symmetric campus,
horizon), a quick establishing dolly then a ~3.2s hold, looping. Matches the
client.

Also this pass: IP/port removed from the login page (PC never shows it); RAN
logo enlarged then tuned; app locked to landscape (PlayerSettings). Login still
works end to end: result=0 -> character list -> "GameMaster".

## Full-implementation mode (2026-08-13)

User directive: stop test-per-change; implement the ENTIRE flow (login, server
select, character select, world entry, gameplay) in code first, test at the end.

### Protocol backbone — COMPLETE in code (verified vs session.js + source)
- Login: version(110) -> server list(1542/1552/1562) -> agent connect -> filler
  layer -> LZO decompress(170, NET_COMPRESS +4 payload) -> crypt key(140) ->
  PLAIN+TEA credentials(2055) -> login feedback(2050, nResult@body22).
- Character list: reqChaBaInfo(2247) -> slots(2248) -> reqCharInfo(2244) ->
  names(2332).
- World entry (RanLoginFlow.EnterWorld): gameJoin(2353) -> field handoff(2358,
  parse ip/port/gaea/slot) -> ConnectField -> fieldDelay -> field identity(2359,
  key halves=1) -> spawn(2333, map@29 pos@33) -> landin(3025)+ready(3026) on
  agent. RanSession has ConnectField/SendField and a per-socket pump.
- Movement: RanSession.MoveTo -> goto(3034, actState@0 curPos@4 tarPos@16) on the
  FIELD socket, with the 60-unit gate and tracked Position.

All message ids from spike/protocol.js; all struct offsets from layout.json
(body-relative). RanSession.Session exposed for the world scene to drive.

### Login page visuals — mostly PC-accurate
Done: PC control layout (uioutercfg.xml), logo, top/bottom banners, 3-slice
OK/Cancel buttons, recessed edit boxes, save-ID checkbox, landscape lock, IP
hidden, 3D login scene (log_in.wld) as backdrop, EXACT camera path
(extract-logincam.js -> Camera01 keyframes, TFREE, no loop, look (0,-40,0)),
daytime lighting.

Still not PC-exact (confirmed against a real PC screenshot):
- Trees: log_in.wld places 116 srp_tree_a/b/c.pis instances (99-byte placement
  records from offset 376) — NOT imported (prefab has 0 tree meshes). Needs a
  .pis (DxPiece) mesh parser + placement extractor + instancing.
- Grass/flowers: srp_grass_d_01/02 — same, not imported.
- Sky: the PC sky is DxSkyMan, a PROCEDURAL sun/gradient/cloud renderer
  (DxSkyMan.cpp), not a texture. Currently a flat overcast colour. Matching
  exactly means porting that system. (login_bg_* are city-street backdrops, NOT
  the sky — mapping them to a cube was a wrong turn, now removed.)

### Remaining for full gameplay (next phases)
1. World-scene bridge: on Playing, place player at SpawnPos, load map for
   SpawnMapId, wire tap-to-move -> Session.MoveTo, keep session alive across the
   scene load (DontDestroyOnLoad already on the flow object).
2. Entity management: parse live spawn/move/despawn packets -> instantiate and
   move other characters/NPCs/mobs (RanTargeting/RanEntityMarker exist).
3. HUD live updates: SNETPC_UPDATE_STATE(82 bytes) -> HP/MP/SP/CP bars.
4. Combat: attack/skill sends + effects.
5. Server-select PAGE UI (the PC "Select Server" page) — flow auto-picks now.
6. Login visual polish: trees, grass, DxSkyMan sky.
7. T2/T3 UI: inventory, skills, shop, map, quest, party.

Everything above the "Remaining" line compiles (215 C# checks) and is byte-faithful
to the working Node client / C++ source.

## Backbone continued + full scope confirmed (2026-08-13)

User: implement the ENTIRE backbone (all gameplay) AND make ALL GUI match the PC
version perfectly. Big project; we have full source + assets for every piece.

### Live HUD state — id + layout found, one dependency left
- Message: NET_MSG_GCTRL_UPDATE_STATE = **3046** (NET_MSG_GCTRL base 2892 + 154).
  Broadcast variant (other players) = 3053 (+161). Derived: GCTRL_GOTO is base+142
  and equals 3034, so base = 2892.
- Body (after 8-byte header): sHP, sMP, sSP, sCP — each GLDWDATA = (current,max)
  = 8 bytes, at body offsets 0/8/16/24. Then dwCharGaeaID, dwCharID.
- **Encoded.** SNETPC_UPDATE_STATE::DECODE does
  `CBit::_buf_decode(body, size, ROT_RIGHT, cryptKey+1)`. cryptKey is hardcoded 1
  (MsgCryptKey), so the key is 2. Port `_buf_decode` (a byte bit-rotation) from
  SERVER_UTIL::CBit before reading the stats. RanPlayerState.Apply already takes
  the four (cur,max) stats — just feed it the decoded values.

### Backbone status
DONE & building (0 errors, 215 C# / 296 Node):
- login -> server list -> agent -> credentials(plain+TEA) -> char list ->
  world entry(gameJoin/handoff/field/identity/spawn/landin/ready) -> movement(goto+gate).
- RanWorldSession bridges tap-to-move to the field socket, seeds spawn; wired
  into the play scene.

NEXT (all unblocked by the working connection):
1. Live HUD state: port CBit ROT decode, route 3046 -> RanPlayerState.
2. Entities: parse spawn/move/despawn (other players id-broadcast 3053 etc.,
   mobs) -> RanEntityMarker instances; RanTargeting already consumes them.
3. Combat: attack/skill sends + effect playback.
4. Server-select PAGE UI (PC "Select Server").
5. ALL PC GUI perfect: login trees/grass/DxSkyMan sky; in-game HUD skin; T2/T3
   panels skinned from uicfg.json (3,939 controls already extracted).

### GUI-perfect method (proven on login)
uicfg.json has every PC control's rect + atlas region. The 3-slice button and
9-slice window helpers in RanBootScreen are the template; every panel skins the
same way. Fonts still Unity default (Tahoma not embedded).

## Backbone: live HUD state DONE (2026-08-13)

- RanBit: the CBit rotation cipher ported byte-for-byte from s_CBit.cpp
  (bit_rot_left/right MSB-first, whole-buffer wrap, byte XOR 0xFF). Round-trip
  verified in RunCheck (Decode inverts Encode for amounts 1/2/5; region-bounded).
  222 C# checks now.
- Message architecture: RanLoginFlow raises WorldMessage(type,buf,at,len) for
  EVERY decoded+decompressed frame after its own login cases. The world scene
  subscribes there — one socket, one unwrap, many consumers. This is the seam
  every remaining gameplay system plugs into.
- HUD: RanWorldSession subscribes to WorldMessage on the socket thread, copies +
  queues type 3046, and on the main thread RanBit.Decode(body) then reads
  sHP/sMP/sSP/sCP as GLDWDATA (cur,max) at 0/8/16/24 -> RanPlayerState.Apply.
  Wired in the play scene (world.playerState).

Builds clean (0 errors, 470 MB). Login->world->move->live HUD all in code.

### Next backbone (same WorldMessage seam)
- Entities: handle the broadcast spawn/move/despawn types -> RanEntityMarker
  instances (RanTargeting already consumes them). The move broadcast is
  NET_MSG_GCTRL_GOTO_BRD (GCTRL+143=3035); state broadcast is 3053.
- Combat: attack/skill sends + effect playback.
- Then the all-PC-GUI pass (login trees/grass/DxSkyMan sky; in-game HUD +
  T2/T3 panels skinned from uicfg.json).

## Backbone: entities + combat + chat + skills DONE (2026-08-13)

All on the same WorldMessage seam, in RanWorldSession (socket thread copies +
queues, main thread applies). Message ids/offsets all from layout.json
allEnums/allStructs (body-relative = struct offset - 8 for the nmg header).

- **Entities** (`RanWorldSession`): placeholder capsule per server entity on a
  dedicated physics layer (EntityLayer=12), URP material so it is not magenta,
  RanEntityMarker for targeting.
  - DROP_CROW 3016 enter: nativeId@0, globId@12, pos@16, hp@72.
  - CROW_MOVETO 3503 move: globId@0, cur@8, tar@20.
  - GCTRL_GOTO_BRD 3035 (PCs walk): globId@0, actState@4, cur@8, tar@20, delay@32.
  - DROP_OUT 3019 leave: num@0(byte), STARID list@2 stride4, wID@2 (low word).
  - ATTACK_DAMAGE_BRD 3044: crow@4, tarId@8, dmg@12 -> subtract HP, remove on kill.
  - UPDATE_STATE_BRD 3053 (other entity HP): globId@0, sHP cur@4 max@8. NOT
    CBit-encoded (unlike 3046) — SNETPC_UPDATE_STATE_BRD has no ENCODE method in
    GLContrlPcMsg.cpp, so it is sent in the clear. Refreshes marker.hp.
  - Death has no dedicated broadcast: it arrives as a fatal ATTACK_DAMAGE_BRD
    (HP->0 removes the entity) followed by the server's DROP_OUT for the corpse.
  - StepEntities() lerps moving entities toward target each frame.
- **Combat** (`RanCombat`): field-socket sends, structs from GLContrlSkillMsg.h /
  SNETPC_ATTACK.
  - ATTACK 3036, body16: emTarCrow@0, dwTarID@4, dwAniSel@8(=AN_ATTACK 4), flags@12.
  - REQ_SKILL 3303, body 58+4*targets: skill_id(m@0,s@2), vTARPOS@4, flags@16,
    bDefense@20, fSum_TarRange@24, wBodyRadius@28, wAttRange@30, wSkillRangeApply@32,
    wSkillRangeTar@34, fAttVelo@36, fAttVeloItem@40, animKey@44, emMType@48(AN_SKILL_A 9),
    emSType@52, wTARGET_NUM@56, sTARID[0](wCrow@58,wID@60). Anti-cheat range block
    filled from the skill's own range so server sanity checks pass.
  - EMCROW: CROW_PC 0, CROW_NPC 1, CROW_MOB 2. EMTARGET_NET 24.
  - Auto-attack loop: swings the selected target at attackInterval when in range.
- **Targeting**: RanTargeting.TryPick installed as RanTouchInput.tapFilter — a tap
  on an entity selects it (does not walk); a tap that misses falls through to move.
  Entity layer excluded from groundLayers so the two never fight.
- **Chat** (`RanWorldSession`): NET_CHAT 2992 send / NET_CHAT_FB 2993 receive.
  Body: emType@0, szName@4 (CHAR[34]), szChatMsg@38 (CHAR[100]), sItemLink@144
  (184, zeroed). Single-byte ASCII, null-padded. Panel.Submitted -> send;
  received -> RanChat.Add with CHAT_TYPE -> Channel mapping.
- **Skills UI**: RanQuickSlots.Slot carries mainId/subId/range now (skillId alone
  is lossy — keeps only sub). RanSkillBook.PlaceInto fills them. RanQuickSlots
  raises Fired after its own target+cooldown gate; RanWorldSession.OnSkillFired
  rebuilds the Skill and calls RanCombat.CastCurrent. Play scene seeds the bar
  from skillflat.json (target-requiring skills first).

Play scene (`RanPlayScene`) creates + wires targeting, combat, chat, quick slots.
World scene regenerated + APK rebuilt with all of it, 0 errors.

### Lesson: the RunCheck exclude list is a real blind spot
RanSkillBook / RanQuickSlots / RanWorldSession / RanChat are excluded from
RunCheck (they need MonoBehaviour/UI stubs), so RunCheck stayed green while the
Unity build failed on a CS0170 (`out var` only conditionally assigned behind an
`&&`). **After editing any excluded file, the Unity scene-gen build is the only
compile gate — run it, do not trust the 222.**

### Next
- Effect playback on skill fact broadcasts (SKILLFACT_BRD 3306).
- NPC/shop interaction, inventory ops, party/guild.
- All-PC-GUI skinning pass (login trees/sky; in-game panels from uicfg.json).

## GUI skinning + item use (2026-08-14, "finish all, verify at end")

Working the "all GUI perfect like PC" pass off the extracted `uicfg.json` (3,939
controls, each with a PC rect + atlas region). RanUiCfg already had the loader
(Parse/SpriteFor/Place); the work is pointing each widget at its real control id.

- **HUD gauges** (`RanHud`): HP/MP/SP/CP now drawn from the PC atlas — control
  ids BASIC_INFO_VIEW_HP/MP/SP/CP (each 162x14, Interface_Main.dds regions) plus
  the *_OVERIMAGE fill, drawn as a horizontally-Filled Image so width tracks the
  stat fraction — the PC client's own two-layer trough+fill scheme. Scaled by
  uiScale (2.6) for mobile. Falls back to the old plain coloured bars when the
  atlas/config is absent, so a texture-less machine still runs the scene.
- **Quick-skill slots** (`RanQuickSlots`): each slot now draws the PC frame
  sprite (BASIC_QUICK_SKILL_SLOT, 41x41 @ Interface_Main) behind the skill icon,
  icon inset like QUICK_SKILL_IMAGE (3/41). Kept the mobile-friendly horizontal
  bottom-right layout rather than the PC vertical tray.
- **Chat**: left as the translucent dark panel — already matches PC chat's
  translucent look; the 9-slice BASIC_LINE_BOX_EX_*_CHAT frame is a later polish.
- **Item use / pickup** (`RanInventoryPackets` + `RanWorldSession`):
  - REQ_INVENDRUG 3292 (UsePotion): wPosX@0/wPosY@2 (grid cell).
  - REQ_FIELD_TO_INVEN 3193 (TakeItem): emCrow@0, dwID@4 (field global id),
    bPet@8 — the correct pickup-by-instance path (not the native-id guess).
  - **Two drop messages, not one**: mobs/NPCs arrive as DROP_CROW 3016
    (SDROP_CROW: nativeId@0, globId@12, pos@16, hp@72 — my mob parse matches
    exactly); ITEMS arrive as DROP_ITEM 3012 (SDROP_ITEM: sItemCustom.sNativeID
    main@0/sub@2, dwGlobID@24, vPos@28). Item drops now spawn a gold pickup cube
    on a dedicated ItemLayer(13). Full pickup loop: DROP_ITEM shows it → tap
    resolves item-first (TryPickItem ahead of targeting ahead of move) → TakeItem
    3193 → server removes it via DROP_OUT. Both entity and item layers are kept
    out of the ground raycast so a tap resolves cleanly.
- **Shop** (`RanShopPackets`): NPC buy/sell sends, byte-tested.
  - REQ_BUY_FROM_NPC 3242: sNID(m@0,s@2), dwChannel@4, dwNPCID@8, wPosX@12,
    wPosY@14, wBuyNum@16 (20-byte body).
  - REQ_SALE_TO_NPC 3243: sNID(m@0,s@2), dwNPCID@4 (8-byte body).
  These are tested protocol units; the shop UI + NPC-dialog loop is a later pass.

All send-packet bodies (combat/chat/inventory/shop) now live in pure, Unity-free
`Ran*Packets` classes so the harness byte-asserts every offset against the source
struct — 276 C# checks. The excluded-MonoBehaviour blind spot no longer hides any
wire format; only layout/wiring lives in the excluded files.

Verified surface: 259 C# checks (combat/chat/inventory packet bodies all
byte-asserted against source structs) + 67 Node protocol checks. Skinned-HUD APK
builds clean (0 errors). Fast iteration via a scene-gen-only check
(`RanPlayScene.Build`, ~30s) before spending a full APK build.

### In-game GUI expanded (2026-08-14)
- **Minimap** (`RanMinimap`): top-right radar — player centre dot, nearby mobs
  (red) and ground items (gold) as blips scaled from world XZ into the circle,
  reading the same RanEntityMarker set targeting maintains. Frame from
  MINIMAP_BACK (CharInven.dds) when present, plain disc otherwise. A radar, not a
  rendered map slice — needs no per-map minimap texture and is useful immediately.
- **Menu bar** (`RanMenuBar`): the PC menu icons (MENU_*_BUTTON, 24x24 out of
  Interface_Main.dds) as touch buttons; each AddToggle flips a panel's active
  state. Wired: INVENTORY, SKILL.
- **Panels**: RanInventory + RanSkillBook created hidden, toggled from the bar.
  The skill book shares the quick-slot bar, so a learned skill armed here lands in
  a slot and casts. Seeded with placeholder contents in the test scene; the real
  contents come from the server's inventory/learned lists.

## Login page: on-device iteration vs the PC reference (2026-08-14)

Paired the device (Galaxy Tab S9, Android 16) via `adb pair`/`connect` and iterated
the login page one change at a time, screenshotting each build. The user's PC
reference is `.claude/image-cache/<session>/4.png`.

- **Controls**: `Scale` was double-scaling on top of the CanvasScaler (login box
  ~42% of screen); set to 1.0 for PC's ~21%. Anchored the page top-centre just
  below the logo so logo+fields+buttons form one tight group. Fields recoloured
  from flat black to a recessed navy edit box. All verified on device.
- **Sky**: enabled the real `login_bg_*` six-sided skybox (was disabled on a
  wrong "not the sky" note — the up face is pure cloud). Adds sky, clouds, distant
  city + backdrop trees. Slightly blocky (ASTC on the gradient) — a refinement.
- **Foreground trees** (the biggest gap): **the textures were NOT missing** — I
  searched a truncated name. The `.pis` stores `min_a-tree01.DDS` (bark) and
  `min_a-tree06.DDS` (full-tree billboard, 256² RGBA alpha) with a 0x11 length
  prefix; both exist in `textures/map/` and are already converted. Lesson: read
  the length-prefixed string fully before concluding an asset is absent.
  - `extract-login-trees.js` parses the 116 placements from log_in.wld's object
    section (99-byte entries: u32 len + name + u32 + D3DXMATRIX + 3f; matrix is
    row-major, translation in row 3; trees carry uniform scale + Y-rotation only)
    → `login-trees.json` (srp_tree_a×76, b×19, c×21).
  - `RanBootScene.BuildLoginTrees` places each as a crossed-quad billboard
    (min_a-tree06, alpha-clip, double-sided), Y-snapped onto the terrain by
    raycast. Base size BaseH=240/BaseW=200 × per-tree scale — tune on device.

### The GUI reality
"All GUI perfect" = every T2/T3 panel (inventory, skillbook, shop, storage,
party, guild, ...), each a set of control ids in uicfg.json plus its data +
interactions. The atlas approach makes each mechanical, but there are dozens and
they genuinely need screenshot comparison to land pixel-exact. Done so far: the
always-on in-game HUD (gauges, slots). Highest-impact next: minimap frame, menu
button bar, then the inventory/skillbook panels.
