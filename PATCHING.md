# Patching the mobile client

How an update reaches a player's tablet, and what to do to publish one.

Everything here has been run end to end against the live store; the numbers are
from that, not from the design.

---

## The two halves

An update is one of two things, and they travel by completely different routes.

| What changed | Ships as | How the player gets it |
|---|---|---|
| Client **data** — `.rcc` packs, `.ntk`, quests, effects, `config.ini` | the patch payload | the launcher downloads it on next start |
| **Code** — anything in `SOURCE/` or `MOBILE/native/` | `libran.so` / `classes.dex`, i.e. the APK | the launcher offers it and Android installs it — see below |

Both travel through the same store. The APK goes in as a blob and the launcher
installs it, with Android showing its own confirmation — see **Shipping code**
below. `minApk` still exists to refuse a client that is too old to talk to the
server at all.

So: a shader fix, a UI fix, a crash fix — a new APK, offered by the launcher. A
rebalanced drop table, a new map, changed NPC text — the payload. Either way you
publish the same directory.

---

## What to do when something changes

Find what you changed. Every recipe ends the same way, and that ending is always
these two lines:

    double-click MAKE-PATCH.bat
    upload MOBILE\native\out\launcher_mobile\  to  http://<host>/launcher_mobile/

| You changed | Do this | Player gets it |
|---|---|---|
| a `.rcc` pack (GUI, quests, effects…) | repack, drop in `CLIENT\`, publish | next launch |
| a loose data file (quest, npctalk, map, skin…) | copy into `CLIENT\`, publish | next launch |
| `config.ini` / `param.ini` | replace the encrypted file in `CLIENT\`, publish | next launch |
| `option.ini` | nothing — see below | never (by design) |
| C++ in `SOURCE\` or `MOBILE\native\` | publish | next launch, as an APK install |
| launcher Java, `AndroidManifest.xml`, resources | publish | next launch, as an APK install |
| the patch server's address | **new APK by hand** — see below | only by re-installing |

---

### A `.rcc` pack — GUI, strings, quests, effects

The client reads the packs, never the loose files beside them: the zip path is
armed unconditionally, so a loose XML edit is invisible on the device. The edit
has to go back into the archive.

    node MOBILE\tools\rcc-extract\rcc-pack.js ^
         CLIENT\data\gui\Gui.rcc  CLIENT\data\gui\Gui.rcc.new ^
         basicgamemenu.xml=path\to\edited.xml

then replace `Gui.rcc` with `Gui.rcc.new` and publish. Entry names are flat,
bare and lowercase. `rcc-pack.js` re-encrypts only the entries you replace and
copies the rest through as stored bytes, so nothing gains or loses an XOR pass.

### A loose data file

Quests, NPC dialogue, maps, skins, effects, help. Copy it into the matching
place under `CLIENT\` and publish. The manifest picks up whole directories, so a
new file is included without touching any list.

**But a loose file that is already inside an archive will not ship**, and that
is deliberate: the client reads the archive, so sending both costs the player
the download twice. This is checked by comparing the bytes, not just the name,
so a file that differs from its namesake in a pack *is* shipped. In practice it
means editing a `.qst`, `.ntk` or `.lev` in place does nothing — those live in
`Quest.rcc`, `NpcTalk.rcc` and `Level.rcc`, and the edit has to go back into the
archive with `rcc-pack.js`, exactly as for the GUI. `PAYLOAD.txt` is how you
confirm what actually went.

### `config.ini` or `param.ini`

These are Rijndael-encrypted, and the client **refuses a plaintext one** —
`CStringFile::Open` returns FALSE if the leading version int is not a key it
knows, rather than falling back to reading it as text. So you cannot edit them
in Notepad.

Produce the new file the way you always have (the PC-side editor that writes
them), drop it into `CLIENT\`, and publish. Read one back to check before you
publish — the tooling decodes, even though it cannot encode:

    node -e "const g=require('./MOBILE/tools/rcc-extract/gamecrypt.js'),f=require('fs');console.log(g.decode(f.readFileSync('CLIENT/config.ini')).toString())"

`config.ini` is where the `[GAME_FEATURE]` flags live. `param.ini` is where the
**game** server address lives — which is not the patch server address; see below.

### `option.ini`

Do not try to ship one. It is seeded: installed only when a player has none, and
never overwritten afterwards, because it is where their own settings live. A
change here reaches new installs only. If you need to force a setting on
everybody, it has to be a code change or a `config.ini` flag.

### C++, or anything under `MOBILE\native\`

Just publish. `MAKE-PATCH.bat` compiles both ABIs, sees the library is newer
than the APK, bumps `versionCode` and the `V001` label, repackages
`RanMobile.apk` and puts it in the store. The launcher offers it and
Android installs it after the player confirms.

The same applies to launcher Java, `AndroidManifest.xml` and resources — they
are all inside the APK, and the staleness check watches the whole `android\`
tree, not just the libraries.

**One exception, once.** A player whose installed APK predates the self-updater
has no code to offer them anything, so it ignores the manifest's `apk` block
entirely. That group needs one APK by hand — `RanMobile.apk`. Everyone after
that is a patch away.

### The patch server's own address

`BASE_DEFAULT` in `RanLauncher.java` is compiled into the APK. Moving the patch
host is therefore the one change that **cannot be patched**: a client pointed at
the old address will never see the new one. Change it, publish, and hand out the
APK the same way as the first time.

Also add the new host to `android\res\xml\network_security_config.xml` if it is
plain HTTP — cleartext is allowed per host, and a missing entry fails as "could
not reach the patch server".

For testing only, `.patchbase` in the data root overrides the address without a
rebuild.

### Nothing changed

Publishing is safe to repeat. If nothing moved, the version number does not
either, and it says so:

    nothing changed since version 377 - no upload needed

---

## What is in the payload

`MOBILE/native/out/PAYLOAD.txt` is rewritten on every publish and lists every
file a player receives - size, path, and an `S` on the one seeded entry:

    # Everything the patcher ships - store version 386
    # 23294 files, 4679.4 MB payload
    # plus RanMobile.apk  versionCode 19 "V002"  321.6 MB
    #
    # size(bytes)  flag  path   (flag: S = seeded, installed only when absent)

             8     cVer.bin
         31188     comment.ini
          1156     config.ini
     307440325     data/animation/Animation.rcc

Read that rather than `manifest.json`, which is 3.7 MB of one-line JSON. It
cannot go stale: it is written by `make-manifest.js` itself, and the `out/`
sweep keeps it.

Roughly, by weight:

| | files | size |
|---|---|---|
| `textures/` — item, map, char, mob, gui, effect, shadow, bike | 15,832 | 2.7 GB |
| `data/map/Map.rcc` | 1 | 548 MB |
| `data/skin/` | 2,809 | 520 MB |
| `data/animation/Animation.rcc` | 1 | 293 MB |
| `sounds/` — sfx and bgm | 864 | 254 MB |
| `data/piece/`, `data/object/`, `data/skeleton/`, `data/help/` | 3,402 | 147 MB |
| `Gui.rcc`, `SkinObject.rcc`, `Effect.rcc`, `GLogic.rcc` | 4 | 102 MB |
| `Level.rcc`, `NpcTalk.rcc`, `Quest.rcc`, `EffectChar.rcc` | 4 | 13 MB |
| `config.ini`, `param.ini`, `comment.ini`, `option.ini`, `cVer.bin` | 5 | 34 KB |

Nine `.rcc` archives, and **none of their loose sources**. `quest/`, `npctalk/`,
`level/` and `effect/char/` each carry their own archive, and the loose `.qst`,
`.ntk`, `.lev` and `.effskin_a` beside them in `CLIENT/` are dev-side inputs the
PC client has never shipped. They are dropped by the duplicate check, which
confirms the bytes match before dropping anything - 2,034 of 2,040 name matches
were identical, and the six that were not are shipped.

Two directions are checked, and both matter:

* `--verify` walks the whole reference client and fails if anything it ships is
  missing from the manifest. That is what caught `textures/` (2.8 GB) being
  absent for the entire port.
* The payload is compared against `Ran/` the other way too. What ships and the
  reference client does not have is 96 `.enm` and one `.mxf` - costume entries
  newer than that install, so content rather than leftovers.

`CLIENT/` carries client **and** server data, so a file being there is never a
reason to ship it. `data/glogicserver/`, 63 loose `.ini`, `cache/` (the font
cache the client writes itself), `cFileList.bin` and `Launcher.URS` all stay
behind.

---

## The server layout

Serve one directory. Nothing else goes in it.

    launcher_mobile/            ->  http://<host>:<port>/launcher_mobile/
    manifest.json               1.2 MB   version, minApk, one entry per file
    manifest.sig                97 B     signature over manifest.json - REQUIRED
    blobs/                      1.7 GB   8017 files, named by SHA-256, no extensions

The client only ever builds three URLs:

    GET  <base>manifest.json
    GET  <base>manifest.sig
    GET  <base>blobs/<sha256>          (resumed with Range on a dropped connection)

**`manifest.json` is not a map of your server.** Its `path` field is where a file
lands *on the device*:

    { "path": "data/glogic/GLogic.rcc", ... }
        ->  /sdcard/Android/data/com.ran.native/files/data/glogic/GLogic.rcc

Never edit it to "point at" anything. The server address lives in one place only:
`BASE_DEFAULT` in `RanLauncher.java`.

### Serving requirements

* **Blobs must be served byte-exact.** No transcoding, and no on-the-fly gzip —
  they are already compressed, and it breaks Range resume.
* **`Accept-Ranges: bytes` must work.** `data/animation/Animation.rcc` is 307 MB;
  without resume a dropped connection restarts it from zero.
* `application/octet-stream` for `blobs/`. Directory listing can stay off — the
  client never lists, it only fetches names it already knows.
* Cleartext HTTP is permitted **only** for the hosts in
  `native/android/res/xml/network_security_config.xml` (currently
  `143.14.11.244`, `127.0.0.1`, `localhost`). A new host must be added there, or
  Android 9+ refuses the connection.

---

## One click: MAKE-PATCH.bat

Double-click `MAKE-PATCH.bat` in the repo root. It runs
`MOBILE/tools/patch/build-and-publish.js`, which does the whole sequence in the
one order that works:

    [1/3] building libran.so
      arm64-v8a   ok
      x86_64      ok

    [2/3] out\x86_64\libran.so is newer than the APK
          versionCode 8 -> 9
          packaging   out/RanMobile.apk  321.2 MB   ABIs: arm64-v8a x86_64

    [3/3] building the payload
          ...
          version  : 372  (bumped from 371)   minApk: 1
          apk      : versionCode 9 "0.5-self-update", 321.2 MB

Then upload `launcher_mobile/`. That is the whole job, for a code change and a
data change alike.

**It only repackages when it has to.** Step 2 compares the APK's timestamp
against everything the APK contains - both `libran.so` files and the whole
`android/` tree, so a launcher-only or resource-only edit counts too. When
nothing is newer it says so and leaves the APK and its `versionCode` alone:

    [2/3] no code change - the published APK is current, versionCode untouched

That matters beyond tidiness. Bumping `versionCode` for a data-only patch would
offer every player a 321 MB reinstall of a binary identical to the one they are
running.

### The APK's name, and the two version numbers

The file has one name every release: **`RanMobile.apk`**. It is handed to
people directly, so it says what it is.

Two numbers live in `MOBILE/native/android/AndroidManifest.xml` and they are not
the same thing:

| | what it is | who sees it |
|---|---|---|
| `android:versionCode` | an integer that only ever goes up | nobody — it is what Android and the launcher compare |
| `android:versionName` | the release label, `V001` | the player's app info, and the launcher's "version 16 (V001)" line |

`MAKE-PATCH.bat` moves both when it repackages: `versionCode` 15 -> 16 and
`V001` -> `V002`. The filename never moves, so a download link to it does not
have to be reissued, and nobody has to work out which of several files is
current.

`versionCode` can never repeat or go down — Android refuses to install over a
higher one — which is why it is a plain counter and not derived from the label.
Set `versionName` by hand to anything that is not `V<digits>` (`1.2-beta`, say)
and the script stops renumbering it and just uses it.

**out/ and the store are cleaned before publishing.** Three kinds of leftover go:

* **Everything in `native/out/` that is not needed.** After a run that directory
  holds exactly seven things: `launcher_mobile/`, `RanMobile.apk` and its
  `.idsig`, `PAYLOAD.txt`, the two ABI build trees, and `ref/`. `out/apk` is
  build-apk.sh's staging area - it wipes and recreates it on every run anyway -
  and screenshots, logs and files pulled off a device are swept with it.

  The two ABI directories stay, and that is deliberate. They are ninja's build
  trees: delete them and the next run recompiles the whole client, which also
  makes `libran.so` newer than the APK - so it would bump `versionCode` and hand
  every player a 320 MB reinstall of a binary that did not change.

* **Stray files.** Anything in `launcher_mobile/` that is not `blobs/`,
  `manifest.json` or `manifest.sig`. Nothing puts files there, so whatever turns
  up is left over - an old archive, a half-finished upload - and it would be
  published with the rest. `manifest.json` is spared deliberately:
  `make-manifest.js` reads it to work out the next version number, and deleting
  it early would silently reset the store to version 1.

* **Stale blobs**, via `--prune`: the previous content of any file that has since
  changed, and every superseded APK, which is 320 MB apiece. Pruning runs
  *after* the manifest is written, because "stale" means "not named by the
  manifest we just built" - the new one has to exist before anything can be
  judged against it.

    [3/3] building the payload
          removed leftover.rar
          swept 1 stray item(s) out of the store
          ...
    pruned   : 1 blob(s), 321.2 MB - rollback to any manifest naming them is no
               longer possible

That last line is the trade, and it is real: republishing an older manifest works
only while the blobs it names still exist. If you want that safety net, pass
`--keep-stale` and prune by hand when you are sure.

**This cleans your machine, not the server.** The upload rule is still "never
delete-on-sync", so the host keeps every blob it has ever been given. That is
deliberate - a client part-way through an update is still asking for blobs from
the manifest it started with. To reclaim space there, delete server-side blobs
by hand, and only ones no manifest you might serve again names.

**`versionCode` is bumped for you**, in
`MOBILE/native/android/AndroidManifest.xml`, and the file is edited in place -
so the number in git always matches what was published. `versionName` is left
alone; set it by hand when a release deserves a name.

**A compile error stops everything.** `build.sh` prints
`--- errors: N  failed: M` whatever its pipeline exits with, so that line is
what gets believed; a non-zero count prints the offending lines and nothing is
published.

It needs `bash` - Git for Windows is enough, and it looks in the usual install
paths. Set `RAN_BASH` to a `bash.exe` if yours lives somewhere unusual.

---

## Publishing a data update

**Double-click `MAKE-PATCH.bat` in the development root.** It runs the build
below, works the version out for itself, and prints what to upload. Deploying
`launcher_mobile/` is the only step left by hand.

It refuses rather than half-working: no Node on PATH, or the file moved out of
the root, and it says so and stops. If the build fails it says
**do not upload anything** - nothing on the server has changed at that point, so
players are unaffected.

What it runs, if you prefer a terminal:

    1.  edit the file in  CLIENT/
    2.  node MOBILE/tools/patch/make-manifest.js --verify
    3.  deploy  MOBILE/native/out/launcher_mobile/

That is the whole routine. **Do not pass `--version`** — it is derived.

### Reading what it prints

Nothing changed:

    version  : 366  (unchanged - nothing to publish)   minApk: 1
      nothing changed since version 366 - no upload needed

Something did:

    blobs    : 0 linked, 1 copied, 8262 already present
    version  : 367  (bumped from 366)   minApk: 1
      changed 1
          config.ini

The `copied` count is your upload list — that many new blobs, plus the manifest.
`added` / `changed` / `removed` name the files, so you can see at a glance whether
the build did what you meant.

`--verify` walks the shipped PC client in `Ran/` and reports anything the manifest
does not cover. Worth keeping on: it is what caught the 1,968 files
(`data/glogic/{quest,npctalk,level,activity}`, `data/effect/char`) missing from
the first allowlist.

### Uploading

* **`blobs/` first, `manifest.json` last.** The manifest is the index; if it
  lands first, a client patching in that window asks for blobs that do not exist
  yet. You get the right order free from any alphabetical sync — `blobs` sorts
  before `manifest.json`.
* **Never enable delete-on-sync** (`rsync --delete` and friends). The store is
  append-only on purpose: republishing an older manifest is how you roll back,
  and that only works while the blobs it names are still on the host.
* **`manifest.sig` must go up with `manifest.json`.** They are checked as a
  pair, so a client that catches one without the other fails - safely, but it
  fails. Upload them together, after the blobs.
* Re-uploading the whole folder is fine, just wasteful. Any tool that skips
  identical files moves only the new blobs.

---

## Files the player owns: seeding

One file in the ship list is written by the *client*, not by you: `option.ini`,
where a player's graphics, sound and gameplay settings live. The launcher
replaces any file whose hash does not match the manifest, so shipping it the
normal way reset everyone's settings on every patch — including options they had
just set.

It is marked in `SHIP` instead:

    { file: 'option.ini', seed: true },

which puts `"seed": true` on that entry in the manifest. The launcher then:

* **installs it when it is absent** — a fresh install still starts on sane
  defaults, the way the PC client ships an `option.ini`;
* **never touches it again**, whatever it contains. Content is not compared; any
  existing file is the player's.

Seeded entries are also left out of `.patchindex`, since their local hash is
expected not to match and an entry that never validates is only noise.

The flag counts as a content change for versioning, so flipping `seed` on a file
bumps the version like an edit would — otherwise the new rule would sit in a
manifest no client ever fetches.

Verified end to end against a local store, version 366 -> 367:

| device state before | result |
|---|---|
| `option.ini` present, 916 B, differs from the manifest's 932 B | untouched — same md5, same mtime; **zero blobs downloaded** |
| `option.ini` deleted | `GET blobs/80d955…` — reinstalled at 932 B |

Add `seed: true` to anything else the client writes into the data root. Right now
`option.ini` is the only one: `config.ini`, `param.ini` and `comment.ini` are
read-only content.

---

## Shipping code: the launcher installs the APK

Native code cannot ride the payload. Since Android 10 an app targeting API 29 or
above may not `dlopen` a library out of its own writable storage — W^X — and
this one targets 34. So there is no equivalent of dropping a new `MiniA.exe`
into the patch: a code fix reaches a player only as a new APK.

The launcher installs it. `make-manifest.js` picks up
`MOBILE/native/out/RanMobile.apk` (override with `--apk`, disable with
`--no-apk`), puts it in the store as a blob like everything else, and adds one
block to the manifest:

    "apk": { "versionCode": 7, "versionName": "0.5-self-update",
             "size": 336806734, "sha256": "bcbd3b19…" }

`versionCode` and `versionName` are read from
`MOBILE/native/android/AndroidManifest.xml` — the same file the APK was built
from, so they cannot drift from what is inside it. **Bump `versionCode` before
building, or the launcher will not offer the new binary**: it only ever offers a
*strictly newer* one.

On the client, before any data is fetched (data can depend on code, never the
other way round):

1. Nothing happens unless `apk.versionCode` is greater than the installed one.
2. `blobs/<sha256>` is streamed straight into a `PackageInstaller` session and
   hashed on the way through.
3. Hash mismatch, short body or oversize body abandons the session.
4. Otherwise it commits, and Android shows its own "update this app?" prompt.
   The player taps once. This cannot be silent without device-owner privileges.
5. Declined, dismissed, or not permitted: the launcher says so and carries on
   into the data patch on the installed binary. An update is never a wall.

The player has to allow "install unknown apps" for the launcher once. If they
have not, the launcher says so and opens that Settings screen, then plays on.

### The two ways to publish a binary nobody receives

Both are silent, both are easy, so `make-manifest.js` refuses to build instead
of warning:

* **versionCode not bumped.** The launcher only offers a *strictly newer* one,
  so a new binary under the old number is skipped by every client.

      Error: this APK is a different build from the published one, but its
      versionCode is 7, not newer than 7. ... Bump android:versionCode in
      MOBILE/native/android/AndroidManifest.xml and rebuild the APK.

* **Code rebuilt, APK not repackaged.** If either `out/<abi>/libran.so` is newer
  than the APK, the APK does not contain the fix.

      Error: out/arm64-v8a/libran.so is newer than RanMobile.apk, so the APK
      does not contain the current code. Run build-apk.sh again.

So the order for a code change is fixed. **MAKE-PATCH.bat does all of it** (see
above); these are the steps it runs, and what make-manifest.js enforces if you
ever run them by hand:

    1. edit the code
    2. bump android:versionCode in MOBILE/native/android/AndroidManifest.xml
    3. cd MOBILE/native && ./build.sh && ABI=x86_64 ./build.sh
    4. NAME=RanMobile ABIS="arm64-v8a x86_64" ./build-apk.sh
    5. node MOBILE/tools/patch/make-manifest.js
    6. upload launcher_mobile/

A data-only change is steps 5 and 6 alone. MAKE-PATCH.bat is 1-5 in one click,
and works out for itself whether 2-4 are needed.

### Why this is safe over plain HTTP

* The hash comes out of `manifest.json`, which is verified against a key
  compiled into the APK **before it is parsed**. The bytes are authenticated,
  not merely un-corrupted — the property the data blobs already had.
* Content-addressed: the manifest never names a path for the APK, so there is no
  traversal surface and nothing new to validate.
* The bytes never exist as a file. They go into the installer session as they
  arrive, so there is no window in which this app — or any other — could swap
  them between the check and the install.
* Only forward. An old manifest stays validly signed forever; without the
  `versionCode` test a replay could walk a player back to a version whose bugs
  are known.
* Android's signature check is the second anchor, and the one that cannot be
  talked around: an APK signed with a different key from the installed app is
  refused outright. The manifest signature says *the publisher meant this*; the
  platform signature says *this is the same app*.
* The install-result broadcast is addressed to this package explicitly, so no
  other app listening on the action can see it.

### Verified end to end

Against a local store over `adb reverse`, on the emulator:

| | result |
|---|---|
| v4 installed, manifest offers v5 | downloaded 154.1 MB, hash matched, prompt shown, **installed — versionCode 5** |
| manifest offers v6, one byte of the served blob flipped | `checksum failed for the apk`, session abandoned, **still versionCode 5**, launcher continued into the data patch |
| same blob restored | downloaded, prompt shown, **installed — versionCode 6** |

The middle row is the one that matters: the tampered binary was refused, and the
refusal did not lock the player out of the game.

### The signing key

`MOBILE/native/android/debug.keystore` is the app's permanent identity, and
`build-apk.sh` silently generates a fresh one if the file is missing. Android
refuses an update signed with a different key. **Lose that file and no player can
ever upgrade again** — they would have to uninstall, which wipes
`Android/data/com.ran.native` and costs them the full 1.7 GB re-download. It is
gitignored. Back it up off this machine, alongside
`MOBILE/tools/patch/keys/manifest-signing-key.pem`.

---

## Why the version number matters more than it looks

`RanLauncher.java:220`:

    if (localVersion == version) { say("Up to date", ...); return; }

A client whose local number matches returns immediately **without inspecting a
single file**. Publish new blobs and a manifest that still says 366, and every
existing install ignores them forever. Only a fresh install (no `.patchver`)
would pick them up.

This is why the version is derived from content rather than typed. One forgotten
argument used to mean an update nobody received.

`--version N` still overrides, for deliberately republishing an old manifest.

---

## What the client does

1. `GET manifest.json`. If `minApk` exceeds the installed APK's `versionCode`,
   it stops and says so — it cannot fetch an APK.
2. If `.patchver` matches `version`, stop. Up to date.
3. Otherwise walk all 8,263 entries. `.patchindex` holds `path -> size:mtime:sha`,
   so unchanged files are trusted without rehashing 1.7 GB. Anything whose size
   or index entry disagrees is hashed to be sure.
4. Download each mismatch as `blobs/<sha>` into `<dest>.tmp`, verify the SHA,
   then rename over the target. A failed hash never becomes a real file.
5. Write `.patchver` **last**, so an interrupted patch resumes instead of
   claiming success.

Measured: ~10 s for a full reconcile, ~35 ms for the fast path.

### Where the data lives

    /sdcard/Android/data/com.ran.native/files/     the data root, and .patchver,
                                                  .patchindex, .patchbase
    /sdcard/ran/                                  debug switches only

The data used to sit in `/sdcard/ran`, which is shared storage - readable and
writable by any app holding a storage permission. The client's C++ loaders are
not hardened against hostile input, so another app editing a `.rcc` in place was
a route into this process, and `.patchbase` sitting there let any app redirect
the patcher. The app's own external directory is unreachable by other apps on
Android 11 and later, needs no permission for us to use, and is still visible
over `adb`.

An existing install migrates itself on first run. Renaming would be instant and
is tried first, but Android refuses a rename from shared storage into
`Android/data/<package>` whatever permissions are held - measured with
`MANAGE_EXTERNAL_STORAGE` granted, 0 of 33 entries moved - so it falls back to
copying, which takes a few minutes for this data. It happens once. `config.ini`
is copied last and marks the tree complete, so an interrupted migration starts
again rather than leaving half a data tree. The old files are deleted only after
the new tree is known good, and if anything fails the old root stays in charge -
the native loader still accepts it.

The debug switches (`nohud`, `nulldraw`, `drawlimit`, ...) stay in `/sdcard/ran`.
They are control files, not data, and they are meant to be compiled out of a
release build anyway.

---

### If the store moves on the server

Three cases, and only one of them is free.

**The path changes, same host** — `/launcher_mobile/` becomes `/patchstore/`.
Serve a `301` from the old path and every client follows it. Tested end to end:
the manifest, the signature and the blob downloads all came through the
redirect, including a forced re-download of a deleted file.

    301 /launcher_mobile/manifest.json   ->  /patchstore/manifest.json
    301 /launcher_mobile/blobs/68d46f9e… ->  /patchstore/blobs/68d46f9e…
    Updated  |  version 381

No APK, no hand-out. Keep the redirect up for as long as any client might still
be pointed at the old path — which, since the address is compiled in, is
forever.

**The host or IP changes.** This one is not free. Two things are baked into the
APK: `BASE_DEFAULT` in `RanLauncher.java`, and the cleartext allowlist in
`res/xml/network_security_config.xml`, which names `143.14.11.244` explicitly.
A redirect to a host that is not in that list is refused by the platform, not by
us — the launcher reports "could not reach the patch server". So a move needs a
new APK, handed out the old way, which is exactly the position the self-updater
exists to avoid.

**Moving to HTTPS.** `HttpURLConnection` does not follow redirects across
protocols, so an `http` client cannot be walked to `https` by redirect. Treat
this as needing a new APK too. (Documented Java behaviour; not tested here.)

#### Worth doing before you ever need it

Point `BASE_DEFAULT` at a **hostname you control** rather than a bare IP, and
allow that domain instead of the address. After that every future move — new
box, new IP, new hosting company — is a DNS change and never needs another
hand-out. It costs one APK now, while you are already handing one out.

Better still, put it behind HTTPS at the same time and delete the cleartext
exception entirely. The manifest signature already makes plain HTTP safe against
tampering, but TLS also stops anyone on the path seeing what a player downloads.

`.patchbase` in the data root overrides the address without a rebuild, but the
data root is app-private now, so that is a development tool and not something a
player can be talked through.

## Pointing at a different server

For a permanent change, edit `BASE_DEFAULT` in
`native/android/java/com/ran/launcher/RanLauncher.java`, add the host to
`network_security_config.xml`, and rebuild the APK.

**To test without rebuilding**, write the URL into `/sdcard/ran/.patchbase` on one
device — it overrides `BASE_DEFAULT` at runtime. Include the trailing slash.

    adb shell "echo -n 'http://10.0.0.5:8080/launcher_mobile/' \
        > /sdcard/Android/data/com.ran.native/files/.patchbase"

---

## The manifest is signed, and that is what makes this safe

Every blob is checked against a SHA-256 that comes **out of the manifest**. So
whoever writes the manifest decides what lands on the device. That check catches
corruption; on its own it stops no attacker at all, because the manifest arrives
over the same plain HTTP connection as everything else.

So the manifest is signed with a P-256 key, and the client refuses one it cannot
verify against the public half compiled into the APK. An attacker who cannot
sign cannot publish - whatever they do to the network, and even if they take the
patch host itself.

It **fails closed**. A missing, malformed or wrong signature is a hard stop:

    manifest signature does not verify - refusing this update
    no manifest signature on the server

Deleting `manifest.sig` does not disable the check, it stops the update. Verified
on the emulator against a manifest with one byte changed and against a store with
no signature at all.

The client also refuses to go **backwards**. A signature cannot stop an old
manifest you signed yourself from being replayed, and an attacker who can answer
for the host could otherwise pin clients to a version whose bugs they know. To
publish old content deliberately, republish it under a **higher** number.

### The signing key

    MOBILE/tools/patch/keys/manifest-signing-key.pem      private - gitignored
    MOBILE/tools/patch/keys/manifest-signing-key.pub.b64  public - compiled into the APK

**Back the private key up the way you back up the release keystore.** Lose it and
you cannot publish another update without shipping a new APK to every player,
because the key they check against is baked into the one they have.

Do not commit it, and do not put it on the patch host - the host never needs it.
Signing happens on your machine, at build time. If it is missing the build says
so and refuses to pretend:

    ******  NO SIGNING KEY  ******
    ...is missing, so manifest.sig cannot be written. Every client will
    refuse this payload.

### What is still not protected

* **The transport is still plain HTTP.** Signing means nobody can *change* what
  you publish, but anyone on the path can still see it. There is nothing secret
  in the payload, so this is a privacy question, not an integrity one. HTTPS is
  still worth doing.
* **A file tampered with between patches** is only caught when its manifest entry
  is next verified, and the version check returns early when the client is up to
  date. The data root is no longer reachable by other apps, so this now needs
  `adb` or root rather than any installed app.
* **`.patchbase` can still redirect the patcher**, and that is now doubly
  uninteresting: it lives in the private root, and a redirected server would
  still have to serve a manifest signed with your key.
* **`MANAGE_EXTERNAL_STORAGE` is still requested.** It is no longer needed for the
  data - only to migrate an old install and to read the debug switches. It can be
  dropped once both of those are gone.

---

## Store maintenance

All opt-in; the default build never touches existing blobs.

| Flag | What it does | When |
|---|---|---|
| `--verify` | reports files in `Ran/` the manifest misses | every build |
| `--fsck` | re-hashes every blob against its name, deletes any that fail | after a crash, a bad copy, or suspicion |
| `--unshare` | rewrites blobs that still share an inode with `CLIENT/` | once, on a store built before this was fixed |
| `--prune` | deletes blobs no longer named by the manifest | only when you accept losing those rollback points |
| `--link` | hardlink instead of copy, saving 1.7 GB | throwaway stores only — see the trap below |

Without `--prune` and `--unshare` the script still *reports* both conditions, so
you know they are there without being forced to act.

---

## Where the script looks

`make-manifest.js` finds the development root by walking up from itself until it
reaches a directory containing `MOBILE/` and either `CLIENT/` or `Ran/`. It keys
on the trees, not on the folder's name or depth, so the checkout can live
anywhere — `D:\Games\DEV EP9`, a network share, a differently-named clone — with
no edit. If it cannot find one it refuses rather than building against the wrong
tree:

    cannot find the development root above ...\tools\patch
    expected an ancestor directory containing MOBILE/ and CLIENT/ or Ran/

---

## Traps, all of them hit for real

**The store used to alias `CLIENT/`.** Blobs were hardlinked to save 1.7 GB,
which made a blob and its source the same inode. Editing a client file in place
rewrote the blob holding its *previous* content, leaving it named for bytes it no
longer held. Nothing reported it: the next build hashed the source, saw new
content, wrote a new blob, and the old one sat there corrupt until a client asked
for it, failed the SHA check, and retried forever. Reproduced by appending one
byte to `CLIENT/config.ini`, which turned blob `56b39b16ce9f...` from 1156 bytes
into 1157. Blobs are copied now. `--link` brings the hazard back; use it only
where the store is disposable.

**The payload is built from `CLIENT/`, not `Ran/`.** `Ran/` is the reference
`--verify` compares against, and is frozen. Editing `Ran/` changes nothing that
ships.

**8,263 entries but 8,017 blobs is correct.** Content addressing means identical
files share one blob — 246 duplicates here.

**Twenty paths would need URL escaping** (spaces, and one Thai `- สำเนา`). This
costs nothing today because the client fetches by hash, never by path. It is a
real reason not to "just serve the source tree".

**The blob store is a real 1.7 GB now**, on top of `CLIENT/`. That is the price of
the store being immutable, and it is the right trade.

---

## Shipping a code change

Double-click `MAKE-PATCH.bat`, then upload `launcher_mobile/`. It compiles both
ABIs, bumps the version, repackages `native/out/RanMobile.apk` and
puts it in the store; the launcher offers it and Android installs it. See
**One click** and **Shipping code** above.

By hand, if you ever need to:

    cd MOBILE/native
    ./build.sh && ABI=x86_64 ./build.sh
    # bump android:versionCode in android/AndroidManifest.xml
    NAME=RanMobile ABIS="arm64-v8a x86_64" ./build-apk.sh
    node ../tools/patch/make-manifest.js

`make-manifest.js` refuses to publish if the `versionCode` did not move or the
APK is older than the libraries, so a missed step is an error rather than a
patch that quietly reaches nobody.

`versionName` is yours to set; nothing depends on it.

Raise the manifest's `minApk` **only** to refuse a client too old to talk to the
server at all — a stale packet layout, say. It is a hard stop: the launcher shows
"the server needs app version N" and goes no further. It is not the mechanism for
delivering an update; the `apk` block is.

### The one exception

A player whose installed APK predates the self-updater cannot be updated by it.
That build has no `offerApk`, so it ignores the manifest's `apk` block entirely.
Those players need one APK by hand; everyone after that is a patch away.
