# Effects — what the shipped data actually contains, and what a rebuild costs

`plan.md` §4 says `SOURCE/Lib_Engine/DxEffect` (112,272 lines) must be **rebuilt,
not ported**, and calls it the largest remaining technical unknown. This document
sizes that work package from the **shipped data**, not from the source tree.

Everything below is one of two things and they are labelled throughout:

- **Measured** — produced by running a parser over all 4,294 shipped `.egp`
  files, or by reading a constant out of `SOURCE` with a script.
- **Inferred** — read off the C++ by a human. Useful, but not evidence.

Tools written for this (all under `MOBILE/tools/rcc-extract/`, prefix `effect-`):

```
node effect-census.js          # versions, prop types, tree shapes, walk health
node effect-refs.js            # per-type resources + which .egp are named by content
node effect-refs.js --write DIR  #   + referenced/dangling/unreferenced json
node effect-detail.js          # SEQUENCE and MESH feature flags, from the flag words
node effect-fields.js          # locates the name fields by measurement
node effect-srcscan.js         # the 13 LoadFile version dispatches, from SOURCE
node effect-solve.js           # how the size correction below was found
node effect-diag.js NAME.egp   # trace one walk + hex dump where it stops
```

`effect-egp.js` is the reader they share. No existing file was modified; `SOURCE/`
is clean (`git status --porcelain` empty).

---

## 1. What an `.egp` is

**Measured.** All 4,294 entries in `Ran/data/effect/Effect.rcc` are `.egp`;
26.4 MB uncompressed, median 4,836 bytes, largest 177,924. `data/effect/char/
EffectChar.rcc` holds **no** `.egp` — it is 185 `.effskin_a`, 12 `.effskin`,
1 `.effskin_a_before`, a **different** subsystem (see §7).

**Measured.** The container is `EFF_PROPGROUP`
(`SOURCE/Lib_Engine/DxEffect/Single/DxEffSinglePropGManSaveLoad.cpp`):

```
0    128  CSerialFile type string  "EFF_PROPGROUP"
128    4  FILEVERSION              1 in every shipped file
132    4  VERSION                  the inner version, drives everything
136       body — EMBYTECRYPT_EGP encoded iff VERSION >= 0x0200
          group header (field order changes per version)
          [BOOL bValid][u32 TypeID][property node]
```

Inner version distribution, all 4,294 files:

| VERSION | Files | Share |
|---|---:|---:|
| 0x0105 | 1,690 | 39.4% |
| 0x0106 | 1,177 | 27.4% |
| **0x0200** | **1,165** | **27.1%** |
| 0x0104 | 146 | 3.4% |
| 0x0103 | 94 | 2.2% |
| 0x0102 | 18 | 0.4% |
| 0x0100 | 3 | 0.1% |
| 0x000d | 1 | 0.0% |

The 1,165 at 0x0200 are exactly the encrypted set already recorded in
`CRYPT-MAP.md` — an independent confirmation of that count, since it was derived
there from the crypt gate and here from the version histogram.

The `0x000d` file is `very_strong03_red.egp`, whose CSerialFile type string is
`"default"`, not `EFF_PROPGROUP`. **It is not an effect file at all** — `default`
is the `.wld0` terrain-sidecar magic. One misnamed file out of 4,294.

### The property tree

**Measured (source, by script).** All thirteen `*_PROPERTY::LoadFile` bodies in
`DxEffect/Single/` share one framing — `effect-srcscan.js` extracts the version
dispatch of each rather than trusting the eye:

```
[u32 ver][u32 size][blitted body]
[u16 MovSoundVER][u16 SoundVER][SMovSound][BOOL sib][sibling…][BOOL child][child…]
```

**The two child flags are not adjacent** — the entire sibling subtree is written
between them, the same trap already recorded for the `.wld0` collision tree and
the `.wld` `DxFrame` scene graph. This is now the *third* format in this project
with that shape; treat it as the house style.

**Measured.** Tree shape across the 4,268 files that parse:

| Nodes per file | 0 | 1 | 2 | 3–9 | 10–29 | 30+ |
|---|---:|---:|---:|---:|---:|---:|
| Files | 9 | 266 | 634 | ~1,996 | ~1,120 | ~243 |

Max depth is 0 for 1,539 files (a flat sibling chain), 1 for 1,798, 2 for 712,
and >2 for 219. One file reaches depth 12 and one holds 210 nodes.
**34,609 property nodes total.**

---

## 2. The declared node size is wrong, by a knowable amount

This is the one real format trap here, and it is the same rule as everywhere
else in this project: **`[version][size]` sizes are only trustworthy on versions
the engine does not know.**

**Measured.** Walking every file with the declared `dwSize` consumes 3,977 of
4,294 (92.6%) to the exact last byte. The other 317 desync.

**Measured, then explained.** `effect-solve.js` searched a small set of
corrections per (type, node version), keeping only walks that end on the file's
final byte, and fed unique solutions back until it converged. The result is not
a fitted table — it collapses to two independent facts:

```
delta = (+4 if the branch reads a DXAFFINEPARTS) + (-4 if the type never reads m_bMoveObj)
```

Why (**inferred**, but forced by the arithmetic):

- `GetSizeBase()` is `matrix(64) + BOOL(4) + float(4) + float(4)` = **76** today
  (`DxEffSinglePropGMan.cpp:136`). Every pre-current version instead reads a
  36-byte `DXAFFINEPARTS` after the matrix and rebuilds `m_matLocal` from it.
  The writer of that era counted `matrix + DXAFFINEPARTS + 2 floats` = 108 and
  **did not count `m_bMoveObj`**, which it wrote anyway. Those bodies are 4 bytes
  longer than declared.
- `DECAL` is the only type whose `LoadFile` never reads `m_bMoveObj`, while
  `GetSizeBase()` still counts it. Its current version is 4 bytes **shorter**.

The reason to trust that over a lookup table: **`DECAL 0x0100` is both, and the
two cancel to 0** — which is exactly what the data shows (30 nodes, delta 0),
without that case ever being fitted.

The rule is now implemented directly in `effect-egp.js` (`sizeDelta`), using the
thirteen `VERSION` constants read out of `SOURCE` rather than guessed:

| Type | VERSION | Type | VERSION |
|---|---|---|---|
| PARTICLESYS | 0x0107 | GROUND | 0x0102 |
| MESH | 0x0105 | LIGHTNING | 0x0102 |
| WAVE | 0x0104 | SEQUENCE | 0x0102 |
| MOVEROTATE | 0x0103 | CAMERA / DECAL / MOVETARGET / POINTLIGHT / SKINMESH | 0x0101 |
| BLURSYS | 0x0102 | | |

`PARTICLESYS` dropped the `DXAFFINEPARTS` two versions before its current one
(0x0105 and 0x0106 have none); every other type dropped it exactly at `VERSION`.

**Measured result: 4,268 of 4,294 files (99.4%) now walk to the exact last
byte**, deterministically, with no search.

### The 26 that still do not (0.6%)

| Cause | Files |
|---|---:|
| Truncated — file ends inside the sound record or before the two child flags | 20 |
| Genuine mid-file desync (`sound name length` implausible) | 4 |
| Stops early leaving 2,220 trailing bytes (`mob_bb_bomb.egp`) | 1 |
| Not an `.egp` (`very_strong03_red.egp`, type `"default"`) | 1 |

The 20 truncations are one family — `*_summon.egp` (cannon, garam, ian, raikan,
tukran, urga, priest, lee, hillo, cyclobs, mob_ce_02, mob_rusian) plus
`kaifirecrack`. Each is 552–564 bytes, contains a single `SKINMESH` node naming
a `.chf`, and simply stops: e.g. `urga_01_summon.egp` is 556 bytes and the
property body ends at byte 556. **Inferred:** these were written by a tool that
omitted `EFF_PROPERTY::SaveFile`'s tail, or they are truncated in the archive.
Either way the *one* node they contain is readable. Not worth chasing: 20 files,
0.47%, and all of the lowest-value type (§5).

---

## 3. The effect types, and how much of the data uses each

**Measured**, over **34,609 property nodes** — 34,563 from the 4,268 files that
walk cleanly, plus 46 recovered from files that fail partway (the walk records
each node before reading the record that kills it). Type IDs are the
`EFFSINGLE_*` defines in `DxEffectSingleType.h`; `EFFSINGLE_ROOT` (1) has no
`*_PROPERTY` class and — confirmed — never appears in a file.

| Type | ID | Nodes | Share of nodes | Files using it | Share of files |
|---|---:|---:|---:|---:|---:|
| SEQUENCE | 0x05 | 8,866 | 25.6% | 2,929 | 68.2% |
| MESH | 0x04 | 8,600 | 24.8% | 2,389 | 55.6% |
| PARTICLESYS | 0x02 | 4,865 | 14.1% | 2,237 | 52.1% |
| MOVEROTATE | 0x10 | 4,520 | 13.1% | 2,162 | 50.3% |
| GROUND | 0x03 | 4,030 | 11.6% | 1,280 | 29.8% |
| BLURSYS | 0x08 | 2,156 | 6.2% | 774 | 18.0% |
| POINTLIGHT | 0x13 | 594 | 1.7% | 556 | 12.9% |
| WAVE | 0x12 | 482 | 1.4% | 371 | 8.6% |
| LIGHTNING | 0x09 | 283 | 0.8% | 122 | 2.8% |
| DECAL | 0x06 | 90 | 0.3% | 53 | 1.2% |
| SKINMESH | 0x11 | 66 † | 0.2% | 50 † | 1.2% |
| CAMERA | 0x07 | 31 | 0.1% | 30 | 0.7% |
| MOVETARGET | 0x14 | 26 | 0.1% | 11 | 0.3% |

† 20 of SKINMESH's 66 nodes (and 20 of its 50 files) are the single-node
truncated summon effects of §2 — **verified: all 20 contain exactly one
SKINMESH node and nothing else.** Cleanly-walked SKINMESH is 46 nodes in 30
files.

The distribution is steep: **six types are 95.5% of all nodes**, and the bottom
five together are 0.7%.

**Measured.** Node versions in the data are overwhelmingly current: 8,337 of
8,866 SEQUENCE are 0x0102, 8,145 of 8,600 MESH are 0x0105, 4,445 of 4,520
MOVEROTATE are 0x0103. Legacy versions exist but are a few hundred nodes in
total. A rebuild only has to understand the current PROPERTY layout of each
type plus the +4/−4 size rule; it does **not** need thirteen version ladders.

---

## 4. Which effects are actually used by shipped content

Effect names never appear in geometry. They are addressed from data tables:
skills (`GLSkill::m_sEXT_DATA`), items (`strFieldFile`,
`GLLandManClient.cpp:946`), pets/summons (`strGEN_EFFECT`), map pieces, and a
handful of literals in the client (`"QI_expget.egp"`, `"Q.egp"`, …).

**Measured** (`effect-refs.js`). Scanning every shipped data source for
`*.egp` tokens — decoded per `CRYPT-MAP.md` where the format is version-gated:

| Source | Distinct `.egp` names found |
|---|---:|
| `GLogic.rcc` (skills, classes, NPC, sale tables) | 937 |
| `SkinObject.rcc` (`.cps` character pieces) | 514 |
| `Map.rcc` | 207 |
| other `.egp` (`m_szEffFile`, `m_szFileName`) | 132 |
| loose `data/piece` | 57 |

| | Count |
|---|---:|
| Distinct `.egp` names referenced | 1,829 |
| …that resolve to a shipped file | **1,447 (33.7% of 4,294)** |
| …named but **not** shipped | 382 |
| Shipped `.egp` never named anywhere | **2,847 (66.3%)** |

**This is a review list, not a deletion list**, for exactly the reasons
`README.md` already gives for the texture audit: a string scan can miss a name
it should have found, and `.egp` names are assembled at runtime in places
(`GETTARGZONE01(emELMT)` returns a per-element string from skill data). Treat
1,447 as a **floor** on usage, not a measurement of it.

**The reassuring part for scoping: the type mix barely moves.** Restricting to
the 1,443 named-and-parsed files (12,236 nodes) reorders the top two and changes
nothing else:

| Type | All nodes | Named-only nodes |
|---|---:|---:|
| MESH | 24.8% | 26.6% |
| SEQUENCE | 25.6% | 23.3% |
| PARTICLESYS | 14.1% | 13.6% |
| MOVEROTATE | 13.1% | 13.5% |
| GROUND | 11.6% | 11.9% |
| BLURSYS | 6.2% | 6.7% |
| WAVE | 1.4% | 1.8% |
| POINTLIGHT | 1.7% | 1.5% |
| LIGHTNING / DECAL / MOVETARGET / SKINMESH / CAMERA | 1.5% | 1.3% |

So **the priority order is the same whether you scope against everything that
ships or only against what is provably used.** That is the single most useful
result here, because it means the 66% unreferenced pile cannot change the plan.

---

## 5. What each type needs at runtime

Name fields were located by **measurement** (`effect-fields.js` histograms where
NUL-delimited filename-shaped runs start, per type and version) and
cross-checked against the PROPERTY struct in the matching header. They agree on
every entry. This matters: a free regex over the body invents names
(`re.tga`, `ing.dds`, `1.dds`) because a `char[256]` overwritten by a shorter
string keeps the old tail after the NUL. Reading the declared offset removes all
of it — e.g. it drops SEQUENCE's phantom "43 meshes" to 0, which is correct
since `SEQUENCE_PROPERTY` has no mesh field.

**Measured**, nodes carrying each kind of reference:

| Type | Nodes | texture | mesh | `.chf` | nested `.egp` | sound | no fields |
|---|---:|---:|---:|---:|---:|---:|---|
| SEQUENCE | 8,866 | 8,866 | – | – | – | 2,484 | |
| MESH | 8,600 | 8,599 | 8,599 | – | – | 505 | |
| PARTICLESYS | 4,865 | 4,862 | 4,862 | – | 366 | 73 | |
| MOVEROTATE | 4,520 | – | – | – | – | – | struct has none |
| GROUND | 4,030 | 4,029 | – | – | – | 127 | |
| BLURSYS | 2,156 | 2,156 | – | – | – | – | |
| POINTLIGHT | 594 | – | – | – | – | – | struct has none |
| WAVE | 482 | – | – | – | – | – | struct has none |
| LIGHTNING | 283 | 283 (×2 slots) | – | – | – | 15 | |
| DECAL | 90 | 90 | – | – | – | – | |
| SKINMESH | 66 | – | – | 66 | – | – | |
| CAMERA | 31 | – | – | – | – | – | struct has none |
| MOVETARGET | 26 | – | – | – | 21 | – | |

Asset footprint, **measured**:

| | All 4,294 | The 1,443 named |
|---|---:|---:|
| Distinct textures named | 1,043 (674 ship, 369 do not) | 549 |
| Distinct meshes / `.chf` named | 821 (746 ship, 75 do not) | 453 |
| Distinct sounds named | – | 202 |

Effect meshes are loose `.x` in `Ran/data/object` (941 files), not in an
archive — an archive-only sweep finds none of them. Spot-checked absences are
genuine: `sword_eff_2.*`, `wind_tail_2.*` and `masmob_eff02.*` exist nowhere in
`Ran/`, while `_eff_flare.*` and `eff09.tga` do. Same seasonal/event pattern the
texture audit already found.

### The two that matter most, in detail

**SEQUENCE — 25.6% of nodes — is a billboard quad with a flipbook.**
**Measured** over 8,327 current-version nodes:

| | |
|---|---|
| `USEBILLBOARD` | 7,675 (92.2%) |
| Flipbook grid `m_iCol × m_iRow` | **4×4 in 7,670 (92.1%)**; 3×3 306; 8×8 165; 5×5 95; 1×1 only 24 |
| Animated (`grid > 1` and `m_fAniTime > 0`) | **8,302 (99.7%)** |
| `m_nBlend` | 1 → 7,159; 2 → 671; 4 → 182; 3 → 168; 5 → 147 |
| Other flags | `USETEXROTATE` 892, `USEANI` 741, `USEDIRECTION` 423, `USEBILLBOARDUP` 229, `USELIGHTING` 130 |

**Measured (source).** `DxEffectSequence.cpp:1010` maps `m_nBlend` to fixed-
function states only: 1 = `DESTBLEND=ONE` (additive), 2 = additive +
`MODULATE2X`, 3 = additive + `MODULATE4X`, 5 = `ZWRITE` on with alpha blending
off (opaque); 4 falls through to the default alpha blend. The draw is four
`VERTEXCOLORTEX2` vertices and two triangles pushed into a shared dynamic vertex
buffer. **No shader is involved.**

So SEQUENCE is: *one texture, a `col×row` UV flipbook driven by `m_fAniTime`,
size/alpha/colour curves, one of five blend modes, camera-facing.* That is a
handful of lines of Unity per node and one shared additive/alpha material.

**MESH — 24.8% of nodes — is a `.x` mesh with a texture and animated UVs.**
**Measured** over 8,144 current-version nodes:

| | |
|---|---|
| Mesh slots filled | 3 of 3 in **all** 8,144 nodes |
| …but `USEBLENDMESH` (morph actually enabled) | **895 (11.0%)** |
| Distinct slot-0 meshes | **554** |
| Distinct morph-target meshes (slots 1–2, only where morph is on) | **236** |
| `USECULLNONE` 4,741 · `USEOTHERTEX` 3,786 · `USEROTATE` 3,328 · `USESCALE` 2,470 · `USESIZEXYZ` 2,257 · `USESEQUENCE` 946 · `USENORMAL2` 486 · `USETEXMOVE` 473 · `USEBLUR` 463 · `USEGROUNDTEX` 256 | |

The three-slots-always-filled figure is a trap worth flagging: 6,950 of 8,144
nodes name **`Dolphin2.x` and `Dolphin3.x`** in slots 1 and 2. Neither file
exists anywhere in `Ran/` — they are the editor's placeholder defaults. Slots 1
and 2 are morph targets and only mean anything under `USEBLENDMESH`. **Taking
the slots at face value would inflate the mesh requirement from 554 to ~800 and
send you hunting for two files that were never real.**

### The rest, from source (**inferred** — struct + draw path read, not measured)

- **PARTICLESYS** — CPU particle emitter: spawn rate, life, gravity, velocity
  cone, size/alpha/colour curves, a texture with its own `col×row` flipbook, an
  optional mesh per particle, and `m_szEffFile` to spawn a nested `.egp`
  (366 nodes do). Drawn as quads/strips through the shared dynamic VB.
  **Measured:** `SetVertexShader` / `SetPixelShader` / `ID3DXEffect` appear
  **nowhere** in `Single/`. The only shader mention at all is
  `DxEffectParticleSysDraw.cpp`, which includes `ShaderConstant.h` and then has
  its two `SetVertexShaderConstant` calls commented out. **The whole `.egp`
  runtime is fixed-function**, which is the single biggest reason this rebuild
  is smaller than the line count suggests.
- **MOVEROTATE** — no name fields, no geometry: a velocity plus three rotation
  rates applied to the node's subtree. A transform animator.
- **GROUND** — a ground-aligned textured quad (scorch marks, spell circles).
- **BLURSYS** — a trail/blur ribbon along the effect's motion, one texture.
- **POINTLIGHT** — a dynamic point light with power/scale curves and a colour.
- **WAVE** — a procedural triangle-strip distortion (`m_fBumpWave*` curves,
  `m_emDrawMode`). No texture field. **Measured:** nothing in `Single/`
  references a render target, `GetBackBuffer` or `StretchRect`, so this is
  geometry distortion, not a screen-space post effect.
- **LIGHTNING** — a bolt with separate inner and outer textures.
- **DECAL** — a projected decal with its own flipbook grid.
- **SKINMESH** — spawns a whole skinned character (`.chf`) as an effect.
- **CAMERA** — camera animation/shake.
- **MOVETARGET** — a homing projectile that spawns another `.egp` on arrival.

---

## 6. Ranking, and the minimum viable subset

Difficulty is **inferred** — it is my judgement of Unity rebuild cost, not a
measurement. Frequency is measured.

| Rank | Type | Nodes | Rebuild cost | Why |
|---|---|---:|---|---|
| 1 | **SEQUENCE** | 25.6% | **Low** | billboard quad + flipbook UV + 5 fixed blend modes |
| 2 | **MESH** | 24.8% | **Low–Med** | `.x` mesh (already extracted to `.rmesh`) + texture + UV scroll; morph only on 11% |
| 3 | **PARTICLESYS** | 14.1% | **Medium** | maps onto Unity's particle system, but ~45 curve/flag parameters to translate |
| 4 | **MOVEROTATE** | 13.1% | **Trivial** | a transform animator, no rendering |
| 5 | **GROUND** | 11.6% | **Low** | a textured quad on the ground plane |
| 6 | **BLURSYS** | 6.2% | **Medium** | trail geometry generated from motion history |
| 7 | POINTLIGHT | 1.7% | Low code / **high runtime cost on mobile** | real-time lights are the expensive part, not the code |
| 8 | WAVE | 1.4% | Medium | procedural distortion; needs a custom shader or gets dropped |
| 9 | LIGHTNING | 0.8% | Medium | procedural bolt geometry, 2 textures |
| 10 | DECAL | 0.3% | Low | Unity has projectors |
| 11 | SKINMESH | 0.2% | **High** | depends on the whole character/skin pipeline for 66 nodes |
| 12 | CAMERA | 0.1% | Low | but touches camera control, which is game-wide |
| 13 | MOVETARGET | 0.1% | Medium | homing projectile + spawns a nested effect |

### How much you get for how much you build

**Measured.** Files whose *every* node type is implemented, adding types in
frequency order:

| Types implemented | Files fully covered (of 4,268) | Of the 1,443 named (of 1,443) |
|---|---:|---:|
| SEQUENCE | 439 (10.3%) | 37 (2.6%) |
| + MESH | 577 (13.5%) | 85 (5.9%) |
| + PARTICLESYS | 1,053 (24.7%) | 243 (16.8%) |
| + MOVEROTATE | 2,067 (48.4%) | 653 (45.3%) |
| + GROUND | 2,730 (64.0%) | 880 (61.0%) |
| **+ BLURSYS (6)** | **3,248 (76.1%)** | **1,087 (75.3%)** |
| + POINTLIGHT (7) | 3,700 (86.7%) | 1,223 (84.8%) |
| **+ WAVE (8)** | **4,029 (94.4%)** | **1,357 (94.0%)** |
| + LIGHTNING (9) | 4,145 (97.1%) | 1,401 (97.1%) |
| + DECAL (10) | 4,198 (98.4%) | 1,430 (99.1%) |
| + SKINMESH, CAMERA, MOVETARGET (13) | 4,268 (100%) | 1,443 (100%) |

The two columns track each other to within ~2 points at every step, which is
the same finding as §4 from another angle.

### Recommendation

**Build six types. Stub the other seven as no-ops.**

> `SEQUENCE`, `MESH`, `PARTICLESYS`, `MOVEROTATE`, `GROUND`, `BLURSYS`
> — **95.5% of all 34,609 property nodes.**

The stub is the important half of the recommendation. The tree is a container:
an unimplemented node type can be **parsed, skipped and its children still
played**, because children are addressed positionally and not through the
parent's data. So with six types built and seven stubbed:

- **all 4,268 parseable effects load** — none is rejected;
- 76.1% render every node they declare;
- the remaining 23.9% render 95.5% of theirs and silently omit a light, a
  distortion, a decal or a camera shake.

That is a materially different proposition from "76% of effects work". Design
the runtime so an unknown `TypeID` is a no-op node, not a load failure, and this
falls out for free.

**Phase 2 (cheap, high value): add POINTLIGHT and WAVE** → 8 types, 98.6% of
nodes, 94.4% of files complete. POINTLIGHT is trivial code; the decision there
is a *performance* one (594 dynamic lights' worth of authored content on mobile),
so it may be right to implement it as a cheap fake — an additive glow sprite —
rather than a real light.

**Drop or defer permanently:**

- **SKINMESH** (66 nodes across 50 files; only **3 files / 7 nodes** in the named
  subset) — it drags in the entire character/skin pipeline for 0.2% of nodes.
  And **20 of the 26 unparseable files are single-node SKINMESH summon effects**
  (verified), so this type is also where the shipped data itself is weakest.
- **CAMERA** (31 nodes) — effects that move the player's camera are a design
  decision on mobile anyway.
- **MOVETARGET** (26 nodes, 11 files) — needs homing logic and nested spawning
  for 0.1% of nodes.

**Asset budget for the recommended subset (measured):** the 1,443 named effects
need **549 distinct textures**, **453 meshes/`.chf`** and **202 sounds**. The
textures are a subset of the 15,194 already converted; the meshes are loose `.x`
in `data/object`, which the existing `extract-meshes.js` already handles.

---

## 7. `.egp` is only 18% of `DxEffect` — what the other 82% is

**Measured**, by line count over `SOURCE/Lib_Engine/DxEffect` (112,272 lines):

| Area | Lines | Consumes | Status for the port |
|---|---:|---|---|
| Root `DxEffect*` — water, river, shadow, reflect, toon, fur, grass, raindrop, specular, tiling, neon, glow, nature, cloth, dot3, lightmap | 33,178 | map/scene config | environment rendering; a mobile client would replace these with engine features, not port them |
| `Char/` — `DxEffChar*` | 25,880 | `EffectChar.rcc` (198 `.effskin*`) | **separate subsystem, not scoped here** |
| **`Single/` — the `.egp` runtime** | **20,122** | **the 4,294 `.egp`** | **this document** |
| `DxShader*` + `DxLoadShader` | 9,902 | `.fx`/asm | D3D9 shaders; replaced wholesale on mobile |
| `DxPiece*` | 6,093 | `.pis`/`.cps` | map pieces / skin pieces |
| `DxMapEditMan*` | 4,044 | editor | **not shipped-client code** |
| `EffAni/` + `EffKeep/` + `EffProj/` | 5,355 | attached to characters | effect playback attach points |
| `DxStaticMesh` / `DxStaticPisMan` | 3,213 | `.wld0` | already decoded by `extract-terrain.js` |
| `DxTexEff*` / `DxTextureEff*` | 2,746 | `.wld0` texture effects | already decoded by `staticmesh.js` |
| `DxFrameAni` | 1,525 | | |

**So "rebuild DxEffect" is at least four independent work packages**, and the
`.egp` one — the one that covers every skill, spell, hit and item effect the
player sees — is **20,122 lines of source and 13 types, of which 6 carry 95.5%
of the data.** Scoping the whole 112k as one unknown overstates it considerably.

---

## 8. What I could not determine

Stated plainly, because an unverified claim recorded as fact is worse here than
an open question.

1. **Whether the 2,847 unreferenced `.egp` are dead.** The scan finds 1,447
   referenced files. Skill effect names are assembled at runtime from per-element
   accessors (`GETTARGZONE01(emELMT)`), so a literal-string scan cannot be
   complete. 1,447 is a floor. Closing this needs `GLSkill`'s `.ssf`/`.charset`
   tables parsed properly, the same way `itemdata.js` did for items.
2. **The exact PROPERTY field layout of 7 of the 13 types.** I decoded the name
   fields of all types that have them (measured, cross-checked) and the flag
   words of `SEQUENCE` and `MESH`. The full numeric layouts of `PARTICLESYS`,
   `GROUND`, `BLURSYS`, `WAVE`, `POINTLIGHT`, `LIGHTNING`, `DECAL` are read from
   headers but **not verified against data**. Per the project rule, those
   `sizeof`s should come from the layout probe, not from hand arithmetic, before
   anyone writes an importer. I did not add them to the probe.
3. **What the 4 mid-file desyncs are** (`kaifirecrack`, `sk_dfly`, `sundo`,
   `tegm_a`) and the one file with 2,220 trailing bytes (`mob_bb_bomb`). They
   are not the truncation family and not covered by the +4/−4 rule. 5 files.
4. **Whether the 20 truncated `*_summon.egp` are broken in the live client too.**
   I did not test what the shipped engine does with them.
5. **Runtime cost.** Everything here is a *count*. How many effect instances are
   live at once during real play, how many draw calls that is, and whether the
   shared dynamic vertex buffer's batching model has a Unity equivalent — none of
   that is measured. That is a profiling question and it needs a running client.
6. **`.effskin` / `EffectChar.rcc` (198 files, 25,880 lines of `Char/`).** Not
   opened. It is the character-attached effect system (aura, ghosting, blur,
   emit, marks) and is a **separate scoping job** of comparable size.
7. **Sound.** Every property node carries an `SMovSound`; 3,204 nodes name a
   `.wav`. I counted them but did not check them against `Ran/sounds`.
8. **Whether stubbing really looks acceptable.** §6's claim that a missing
   POINTLIGHT or WAVE is survivable is a judgement, not an experiment. Nothing
   here was rendered. The cheapest test is to build SEQUENCE alone and look at
   a common skill effect.

## 9. Test coverage

`MOBILE/tools/rcc-extract/test.js` is owned by another agent and was not
touched, so nothing here is pinned there yet. The numbers worth pinning when it
is free are: **4,294 `.egp`; 1,165 encoded at version 0x0200; 4,268 walk to the
exact last byte; 34,609 property nodes; 13 distinct types; SEQUENCE 8,866 /
MESH 8,600 / PARTICLESYS 4,865.** A regression in the +4/−4 size rule shows up
immediately in the exact-EOF count, which is the strongest single check
available for this format — the same role the "consumed to EOF" check already
plays for `item.isf` and `.wld0`.
