# Characters — how a shipped RAN character becomes a Unity prefab

Phase 3 work. PC client and servers are untouched (Option A).

## The chain

No shipped file names all the parts of a character. Four do, in sequence, and
each step was a separate gap:

```
.chf   skeleton + piece names                    -> animtypes.json (characters)
.cps   which .x holds geometry, and which mesh    -> pieces.json
.x     vertices, bones, skin tables              -> assets/meshes/*.rmesh
.cfg   which clips belong to that skeleton       -> meshclips.json
```

`gen-characters.js` joins them into `characters.json` (+ `charflat.json` for
Unity, whose `JsonUtility` cannot read a dictionary keyed by arbitrary strings).

**1,590 characters, 3,236 of 3,360 piece references resolved (96.3%), 1,565
carrying clips.**

## The four things that were wrong, and what they cost

### 1. `.cps` looked like 2,317 missing meshes. It was an indirection.

Matching piece names against extracted mesh names found **35 of 2,352**. A
`.cps` holds no geometry at all — it names another `.x` plus one mesh inside it.
Following that resolves **2,277**.

### 2. Mesh names in `.rmesh` were all empty.

A piece selects ONE mesh out of a shared `.x` (`s_13.X` carries both `13body`
and `13weapon`), but the exporter leaves the D3DX `Mesh` instance name blank on
essentially every shipped file — the name is on the parent `Frame`. With
`m.name || m.frameName`, sub-mesh resolution went **1.3% → 99.0%**.

That number is the load-bearing check in this whole document: the sub-mesh name
comes from the `.cps` and the mesh list comes from the `.x`, two files that
never reference each other, so 99% agreement is not something a wrong parse
produces.

### 3. The two name slots swap at version 0x0108.

```
<= 0x0107   [ref][geometry .x][skeleton .x][pieceType][mesh name]
>= 0x0108   [ref][skeleton .x][geometry .x][mesh name]
```

"Take the first name that exists on disk" resolved **100%** of pieces and was
wrong: on 0x0114 it returns the SKELETON, which contains no meshes. The only
reason that surfaced is that the sub-mesh lookup then had nothing to match
against — a reminder that a 100% rate on the wrong question proves nothing.

### 4. 39% of animation tracks were silently dropped.

`.ranim` keeps two track lists so the engine can drive the upper body
independently of the legs. **155,434 of 397,523 tracks (39.1%), across 2,867 of
6,498 files** are in the upper list. Baking only the lower one gives clips that
animate from the waist down; 7 clips bound nothing at all. Merging both lists
took `b_m` from 334,020 to **787,080 curves**.

## Which clips a character can play

Declared, not inferred. A clip's `.cfg` names its skeleton; a `.chf` names the
skeleton it wears; a mob is a single `.x` that IS its own skeleton.

Bone names deliberately do **not** drive this. Nearly every rig uses the 3ds Max
biped convention, so `Bip01_Spine` appears on the player skeletons, the mobs and
the pets alike — matching on names gave a median of **721** clips per mesh and
handed a mob the player's entire library. With declarations the median is **8**.

Names are used as the *verifier* instead: after pairing, the clip's tracks must
resolve against the mesh's bones, which is exactly the condition under which the
clip animates anything in Unity. Two controls guard it — `b_element` must play
`a_element_stay` (declared, never fitted), and must NOT play any `b_m` clip, so
a matcher that matched everything would fail rather than pass.

## Unity side

| Script | Does |
|---|---|
| `RanClipBaker` | `.ranim` -> `.anim`, one folder per **skeleton** (not per character — 1,590 characters share 636 skeletons) |
| `RanAnimatorBuilder` | `AnimatorController` from `EMANI_MAINTYPE`; locomotion by `Speed`, `Dead`/`Attack`/`Hit` as interrupts |
| `RanChfBuilder` | Skeleton prefab + parts, re-binding each part's skinning onto the character's own bones |

Re-binding is the substance of `RanChfBuilder`. Each part imports as its own
prefab with its own copy of the bone hierarchy, so parenting it unchanged leaves
it skinned to a skeleton nothing animates: it renders, in bind pose, forever.

`b_m` and `b_w` both build all 7 wanted states. Mob skeletons typically get
fewer, which is correct — a mob has no `AN_PLACID`.

## Four Unity-side traps, all found by running it

**Re-bind from the FILE's skin table, not the imported Transform array.** A
costume or equipment mesh skins onto the character's skeleton, which lives in a
different file, so the part importer had no Transform for those slots and left
them null. Reading `src[i].name` therefore cannot recover the name for exactly
the bones that most need rebinding. It reported **1,344 of 5,120 slots
unresolved**; reading the `.rmesh` skin table gives **12**, which matches the
0.5% miss rate measured independently on names alone.

**A skeleton `.x` carries its own geometry.** `b_w` has 16 meshes and 1,471
vertices — 3ds Max's bone display boxes. Left in, they render as a cage of blocks
around the model: `boa` came out with **23 renderers for 6 parts**, now 7. Only
stripped when parts were placed, because a mob has no pieces and IS its skeleton
file.

**A part can carry bones of its own.** `s_w_tc_whip` has `whip01..05` and
`whip_hand`, which exist nowhere in any character skeleton. Reparenting only the
renderer and destroying the rest of the part deletes them, and the whip then
skins to nulls. The subtree is moved across at its highest ancestor whose parent
resolves by name — exact rather than guessed, because the attachment point is
recorded in the part's own hierarchy.

The other two flagged prefabs are benign and worth distinguishing: `archer_mob`
and `log_in_man` wear reduced skeletons (`b_w1` 67 bones, `b_m1` 105, against
`b_w`'s 226) whose finger bones simply do not exist, so a handful of vertices
stay put. That is the shipped rig, not a porting defect.

**`GetComponent<T>() ?? AddComponent<T>()` does not work.** Unity overloads `==`
against a sentinel "fake null" but `??` uses real reference null, so the coalesce
keeps the missing component and the next call throws.

**Baking every clip produced 13 GB.** Unity serialises `.anim` as YAML text, so a
2.5 MB `.ranim` becomes a **127 MB** asset — one skeleton reached 6.4 GB. The
worst offenders are all `AN_GESTURE` emotes no state references. Restricting to
the seven action types the controller uses keeps 17% of the bytes, and forcing
binary serialization does the rest: **13 GB -> 434 MB**. Emotes are not lost,
only unbaked — the `.ranim` still ships and can be bound on demand.

## Final build

**977 prefabs, 0 failed.** 1,590 characters collapse to 976 distinct models —
same skeleton, same part list — so only one prefab is built per combination.

| | |
|---|---|
| renderers | 2,192 (1,654 skinned), 1,159,642 vertices |
| bone slots | 27,375, **6 null (0.02%)** |
| bindposes / weights | 0 meshes missing either |
| materials | 2,637, 0 null, 395 (15.0%) with no base texture |
| animators | 1 missing, **21 without a controller** |

The 395 untextured materials and the 21 controller-less characters are both
never-shipped content, not pipeline gaps: 384 of the 1,039 textures effects and
characters name do not exist in any archive, and 25 characters wear a skeleton no
`.cfg` assigns a clip to.

**621 AnimatorControllers, 3,348 clips baked.** State coverage: 4 skeletons fill
all 7 states, 238 fill 6, and 180 fill 1 — a mob with a single idle is correct,
not a failure. **Zero clips across every skeleton bound nothing**, which is the
check that would have caught a wrong clip-to-skeleton pairing.

## Result

`play_w_school_03_boa.unity`: map 88 renderers, character 7 renderers, controller
`b_w` bound, spawned on **navmesh cell 4054 of 8108** — on the mesh the server
validates against, not on the terrain surface and not at the origin.

`RanCharacterDriver` interpolates toward server-confirmed positions and derives
`Speed` from measured displacement rather than intent, so the legs match what is
on screen. A jump beyond **60 units** — the server's own rejection threshold in
`GLCharMsg.cpp:256` — is treated as a correction and snapped, not animated.

## Known gaps

- **99 piece references unresolved, 25 whose mesh is absent** (3.7% combined).
  Those 99 are 77 distinct names, and **65 of them are not in `SkinObject.rcc` at
  all** — never-shipped event costumes (`w_assassin_eventcos_*`), the same
  seasonal-art pattern the asset audit already reports. Only 12 are present and
  fail to walk.
- **`.abl` is not the same container**, despite sitting in the same archive and
  being named by eight character pieces. Including it moved "unreadable" from 75
  to 1,046 and "no walker" from 86 to 483 while resolving zero additional
  pieces — a different format under a different crypt gate. Excluded, with the
  measurement recorded in `cps-refs.js` so it is not retried blind.
- **1,529 of 13,404 pieces desync** in the full walk. Names are still read via a
  prefix-only path that skips materials and effects; it agrees with the full
  walk on the sub-mesh check at **99.3% vs 99.0%**, so those pieces are usable.
- **18 characters name a skeleton that was never extracted.**
- **`upperBody`/`lowerBody` in `SANIMCONINFO` are never set** in shipped data
  (0 of 6,161). The flags are decoded; nothing uses them.
- Clips are baked for the 4 most-used skeletons so far, not all 636.
