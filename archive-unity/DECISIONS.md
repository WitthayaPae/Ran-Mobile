# Decisions

The open questions from `plan.md` §11, resolved. The owner answered **platforms**
(both iOS and Android, Android first if it is hard) and delegated the rest.

Each entry says what was decided, what it was decided *on*, and what would
overturn it. Where I decided something the owner might reasonably have decided
differently, that is marked.

---

## 1. Engine — **Unity 2021.3 LTS + URP**. Ratified, not re-opened.

I should be straight about the process here: this was never formally chosen. I
assumed it and built on it, and by the time it surfaced as an open question it
was load-bearing across ~30 C# files and every importer. So this is a
ratification of a decision already made in practice, not a fresh comparison.

That said, it holds up on the merits:

- The engine is **left-handed Y-up** (`LookAtLH`/`PerspectiveFovLH`/`OrthoLH`
  throughout `Lib_Engine`, no right-handed variant anywhere), which is Unity's
  own convention. Coordinates pass through unchanged — only V is flipped for the
  texture origin. Godot is right-handed Y-up and would need a handedness flip on
  every mesh, matrix, animation curve and navmesh cell, each an opportunity for a
  sign error that renders *almost* correctly.
- ASTC and ETC2 import pipelines are mature on both target platforms.
- The 3ds Max biped rig maps onto `SkinnedMeshRenderer` + `Animator` directly.

**Would overturn it:** a licensing change, or a hard requirement for a runtime
Unity does not ship on. Cost to reverse is now measured in weeks, not days.

## 2. Platforms — **both, Android first** (owner's decision)

Android is the Thai market's floor and the harder constraint; iOS follows.
This drives §3 below.

## 3. Texture format — **ASTC 6x6 for both platforms**, ETC2 only as a fallback

Priced from actual pixel counts, not file sizes — compressed texture cost is a
function of pixels, and PNG byte size says nothing about it. **16,072 textures,
2,881 megapixels.**

| Format | bpp | All 16,072 | Referenced only |
|---|---|---|---|
| ETC2 RGBA | 8 | 2.68 GB | 0.55 GB |
| ASTC 4x4 | 8 | 2.68 GB | 0.55 GB |
| **ASTC 6x6** | **3.56** | **1.19 GB** | **0.24 GB** |
| ASTC 8x8 | 2 | 0.67 GB | 0.14 GB |

ASTC is the one format that serves both targets: iOS since the A8 (2014),
Android since GLES 3.1 / Vulkan, which is effectively every device that can run
this game at 30fps anyway. Shipping ETC2 as well would nearly double the texture
payload to serve devices that cannot run the client regardless.

6x6 over 8x8 because 58 textures exceed 1024² and the UI is icon-dense; 8x8 stays
available per-folder if the budget tightens. Only **122 of 16,072 are
non-power-of-two**, so block compression applies almost everywhere without a
resize pass.

**Would overturn it:** a decision to support GLES 3.0-only devices, which would
need an ETC2 variant as a separate asset pack — not a second copy in the base
install.

## 4. v1 content scope — **ship all 130 maps.** Subsetting is not the lever.

This is the decision the measurement changed most. `plan.md` §8 called map
subsetting the #2 size lever, second only to textures. It is not:

**All 130 maps, deduplicated, are 0.69 GB** — 407 MB of geometry plus 299 MB of
unique textures. And they share heavily: of 3,011 distinct map textures, **1,639
are used by more than one map**, so cutting maps frees far less than their listed
size. The top 10 maps are only 42% of the geometry; the curve is flat, not
long-tailed.

Cutting half the maps would save roughly 150 MB and cost half the game's content.
That is a bad trade at any install target.

**What the real lever is:** 20%. Of 2,881 megapixels, only **588 are referenced**
by any map, character or effect — 5,292 distinct texture names of 16,072 files.
Shipping the referenced set at ASTC 6x6 with mips is **0.32 GB** instead of
1.58 GB.

**The caution that goes with it, because I got this wrong before.** I once
reported 9,134 unreferenced textures as an install-size saving and that was
wrong. The reference set is a **floor, not a census**: some character textures are
substituted at runtime per school and class, and a string scan can miss a name
that is composed rather than written. So the rule is *ship referenced, stream the
rest* — the unreferenced files go into on-demand asset packs, not the bin. A
texture that turns out to be needed then costs a download, not a missing model.

## 5. Animation shipping — **ship `.ranim`, bind at runtime.** Not baked clips.

| Approach | Size |
|---|---|
| Baked `.anim` assets, all clips | 13 GB |
| Baked `.anim`, controller action types only, binary serialised | 2.3 GB |
| **`.ranim`, bound at runtime** | **623 MB** (317 MB for action types only) |

Unity serialises an AnimationClip at roughly 50x the source: a 2.5 MB `.ranim`
becomes a 127 MB `.anim`. Baking is the right call in the *editor*, where the
Animator needs concrete clips at author time, and the wrong call for *shipping*.

This is implementable rather than aspirational: `RanAnimationBinder.Bind` uses
`clip.SetCurve`, which is runtime-safe. Only the menu wrapper touches
`AssetDatabase`. The split is the work item.

## 6. Install budget — **~1.5 GB, against a 2–4 GB target**

| | |
|---|---|
| Textures, referenced, ASTC 6x6 + mips | 0.32 GB |
| Animation, `.ranim`, all reachable clips | 0.61 GB |
| World — terrain, objects, navmesh, effects | 0.41 GB |
| Meshes, 2,288 referenced `.rmesh` | 0.14 GB |
| **Total** | **~1.5 GB** |

Under the AAB model that is a base install plus asset packs, comfortably inside
Play's limits. Dropping animation to the action-type subset takes it to ~1.2 GB
if the base install needs to be smaller still.

**`plan.md` §11 lists install size as a High risk. On these numbers it is
Medium** — and the reason is not compression, it is that 80% of the texture
budget was never referenced by anything.

## 7. Version gating — **no server change. Option A stays intact.**

I first recommended "one `NET_CLIENT_VERSION` value and its gate". That
recommendation rested on a premise that turned out to be false, and the owner's
decision after the correction was to build nothing.

**There is no gate to extend.** Verified three ways:

- `CLoginServer::MsgVersion` never reads its input. It declares a fresh
  `NET_CLIENT_VERSION` and uses only `pMsg->dwClient`; the incoming payload is
  discarded.
- `nGameProgramVer` / `nPatchProgramVer` appear **zero** times across
  `ServerLogin`, `ServerAgent`, `ServerField` and `ServerSession`.
- Every server-side use of `m_nVersion` is an assignment into an *outgoing*
  message.

`NET_MSG_VERSION_INFO` is the client asking *"what version are you?"* and the
server answering with its version plus the encryption key. The client does send
its own version via `CNetClient::SndVersion`; the server throws it away.

So a real gate means **new rejection logic on the login path, keyed on a
client-supplied integer**. A wrong constant locks out every PC player at once.
That is a different risk class from the "one constant" I originally described.

**Decision: do not build it.** Staged rollout can be handled at the store level
and telemetry at the account level. Option A's entire value is that the server
stays untouched, and this was the only change threatening that.

**Consequence to accept honestly:** the server cannot distinguish a mobile client
from a PC one. For v1 nothing depends on that. If something later does, this
reopens — and the work is a login-path change, not a constant.

## 8. Controls — **tap-to-move primary, virtual joystick as an option**

Decided on the Phase 0 measurement rather than preference: the server rejects any
move where `|server_pos − vCurPos| > 60.0f` (`GLCharMsg.cpp:256`) and snaps the
client back. A joystick generates continuous local prediction and lives against
that ceiling; tap-to-move sends a destination and lets the server confirm, which
is what the protocol is shaped for — and it is what the PC client does.

Joystick ships as a setting for players who want it, tuned inside the 60-unit
budget.

## 9. PvP segregation — **mixed, with input method recorded**

Segregating splits an already-sized population and creates two balance targets.
Recording input method per character costs nothing and makes the question
answerable with data later, which is better than guessing now in either
direction. **Reversible**; segregation stays available if the data says so.

## 10. Auto-hunt — **do not ship in v1**

The client-side bot exists and is nearly free to expose, and it is market-standard
in this genre. It is still a no for v1: it compounds the botting risk on a server
that already auto-bans by IP, and it is far easier to add later than to withdraw
once players expect it. **Explicitly deferred, not rejected** — this is a
commercial call the owner may well overturn.

## 11. Team size — **assume 1–2 engineers, not the 2–4 in §6**

Everything built so far has been built by one. The phase *durations* in §6 should
be read as roughly doubled; the phase *order* is unaffected.

---

## What still needs the owner

**Nothing.** §7 was the last open item and the answer is to build nothing, so
every server and the PC client stay frozen exactly as Option A requires.

---

## Appendix: build tooling status

**Android player: installed.** `AndroidPlayer` (2.0 GB) is present in the editor
after fetching the official `UnitySetup-Android-Support-for-Editor-2021.3.45f2`
installer for changeset `88f88f591b2e`.

**SDK / NDK / OpenJDK: NOT installed, and not fetchable the same way.** Unity
bundles them with a *Hub* install; this editor was installed outside Hub, so Hub
refuses (`No editor found for version "2021.3.45f2"`) and the CDN returns 404 for
the standalone SDK/NDK/JDK installer names. Getting an APK therefore needs either
an external Android SDK + NDK + JDK pointed at via `-androidSdkPath` /
`-androidNdkPath` / `-jdkPath`, with the version match to Unity 2021.3 verified
rather than assumed, or the editor re-registered with Hub.

Consequence: **no APK yet**, so the mobile perf pass cannot be measured on-device.
Static measurement — draw calls, renderer counts, texture memory, batching — is
available now and does not need a build.

**iOS: unreachable from here regardless.** Unity emits an Xcode project; turning
it into an IPA needs macOS.

---

## Appendix: the Android toolchain, assembled by hand

Unity Hub could not install Android support for this editor — it only knows the
Unity 6 install, because 2021.3.45f2 was installed manually outside Hub. The
toolchain was therefore assembled directly, at `C:\RanToolchain`:

| Component | Version | Why exactly this |
|---|---|---|
| JDK | 11.0.25 (Microsoft) | Unity 2021.3 requires 11. Newer breaks it |
| Android SDK | platform 31, build-tools 32.0.0 | What 2021.3 targets |
| NDK | **r21d (21.3.6528147)** | Unity 2021.3 pins this exact revision |
| command-line tools | **7.0 (8512546)** | The current tools need Java 17; this runs on 11 |

Three traps, each of which cost a round trip:

1. **The current `sdkmanager` requires Java 17** while Unity requires Java 11 —
   `UnsupportedClassVersionError: class file version 61.0 ... only recognizes up
   to 55.0`. Solved by using the older command-line tools rather than installing
   a second JDK.
2. **Licence acceptance cannot be piped reliably.** `echo y | sdkmanager
   --licenses` reports "7 of 7 SDK package licenses not accepted" and installs
   nothing. Writing the SHA1 acceptance files into `$SDK/licenses/` directly is
   what `--licenses` does anyway, and it is deterministic.
3. **NDK r21e is NOT close enough.** Unity checks the revision in
   `source.properties` and rejects 21.4.7075529 with a bare "Android NDK not
   found", which reads like a path problem and is a version problem.

Build command:

```
Unity.exe -batchmode -quit -projectPath <proj> -buildTarget Android \
  -executeMethod Ran.Mobile.Assets.Editor.RanBuild.Android \
  -androidSdkPath "C:\RanToolchain\sdk" \
  -androidNdkPath "C:\RanToolchain\android-ndk-r21d" \
  -jdkPath "C:\RanToolchain\jdk\jdk-11.0.25+9"
```

The failure messages are worth reading positionally: JDK, then SDK, then NDK —
each one only appears once the previous is satisfied, so a changing error is
progress rather than a new problem.
