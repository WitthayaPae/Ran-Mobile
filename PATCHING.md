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
| **Code** — anything in `SOURCE/` or `MOBILE/native/` | `libran.so` / `classes.dex`, i.e. the APK | you hand out a new APK |

There is no mechanism for the launcher to deliver an APK. It can only *tell* a
player theirs is too old, via `minApk`. Distribution is direct.

So: a shader fix, a UI fix, a crash fix — new APK. A rebalanced drop table, a new
map, changed NPC text — patch.

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

    ABI=arm64-v8a ./build.sh              # and ABI=x86_64 for the emulator
    NAME=ran-phase3 ./build-apk.sh        # -> native/out/ran-phase3.apk

Bump `versionCode` and `versionName` in `native/android/AndroidManifest.xml` so
the build is identifiable. Raise the manifest's `minApk` to that `versionCode`
**only** if you want old APKs refused — the launcher can then only display
"the server needs app version N", so anyone who cannot reach a new APK is stuck
at that dialog.

The data payload is unaffected by a code change. If `/launcher_mobile/` is
already uploaded, a code release means handing out one APK and nothing else.
