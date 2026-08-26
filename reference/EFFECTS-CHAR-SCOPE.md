# `DxEffect/Char` — what it actually is, what feeds it, and what it costs

`EFFECTS-SCOPE.md` §7 lists `SOURCE/Lib_Engine/DxEffect/Char` at **25,880 lines**,
consuming "`EffectChar.rcc` (198 `.effskin*`)", and flags it as a **separate
subsystem, not scoped here**. §8.6 records it as unopened. This document opens it.

The headline correction is in §2: **`EffectChar.rcc` is not this subsystem's main
input, and is not close to it.** It holds 449 effect records. The shipped `.cps`
character pieces hold at least **18,083** more — 40× as many — and that is where
the work actually is.

Everything below is labelled:

- **Measured** — produced by running a parser over the real `Ran/` tree, or by
  reading a constant out of `SOURCE` with a script.
- **Inferred** — read off the C++ by a human. Useful, but not evidence.

Tools written for this (all under `MOBILE/tools/rcc-extract/`, prefix `char-`;
nothing existing was modified, `cd SOURCE && git status --porcelain` is empty):

```
node char-census.js            # EffectChar.rcc: types, versions, piece slots, walk health
node char-census.js --strings  #   + strings per effect type
node char-cps-walk.js          # the .cps piece walk: embedded DxEffChar effects
node char-cps-walk.js --strings#   + texture/mesh names per type
node char-refs.js              # which .effskin* names shipped content actually uses
```

`char-effskin.js` is the `.effskin` reader they share.

---

## 1. What `Char/` is for

**It is not a second effects engine and it is not the character renderer.** It is
the **per-piece material and attachment layer** that wraps the character mesh
draw, plus a set of genuinely attached visual effects.

**Measured (source).** A `DxEffChar` is owned by a `DxCharPart` (a live equipped
piece) or a `DxSkinPiece` (the loaded template). `DxCharPart::Render`
(`SOURCE/Lib_Engine/Meshs/DxCharPart.cpp:420-478`) does exactly this, in order:

```
for each effect with GetStateOrder() <= EMEFFSO_RENDERSTATE (6):
    pEFFECT->SettingState(...)          <- changes D3D state for the base draw
DrawMeshContainer(...)                  <- the character mesh itself, NOT in Char/
for each effect at EMEFFSO_NORMAL (14):
    pEFFECT->Render(...)                <- extra passes / attached geometry
for each state effect, in reverse:
    pEFFECT->RestoreState(...)
```

and three more entry points draw the rest by state order: `RenderGlow`
(`EMEFFSO_GLOW`, 12), `RenderEff` (`EMEFFSO_SINGLE_EFF`, 11), `RenderGhosting`
(`EMEFFSO_GHOSTING`, 13).

So the base mesh is drawn by `DxSkinMesh`, not by `Char/`. **`Char/` is a
decorator.** That single fact is what makes it stageable.

**Measured (source, `DxEffChar.h:42`).** 35 type IDs are defined; 29 can be
instantiated (`DxEffCharMan::CreateEffInstance`, `DxEffChar.cpp:132`); 6 —
`OVERLAY`, `TOTALSHADER`, `VISUALMATERIAL`, `SPLINESINGLEEFF`,
`WORLDBATTLEFLAG`, `DISABLERENDER` — are gated off by `IsEffectNotSupported`
(`DxEffChar.h:82`) and **skipped by declared size at load time**. They have no
implementation files in `Char/` at all.

The 29 implemented types split cleanly into two roles:

| Role | Types |
|---|---|
| **Material / render state** — modify how the character's own mesh draws, or draw an extra pass of it | AMBIENT, ALPHA, NOALPHA, SHOCK, MARK, TOON, SPECULAR, SPECULAR2, DOT3, NORMALMAP, REFLECTION2, TEXDIFF, MULTITEX, USERCOLOR, LEVEL, NEON |
| **Attached visual effects** — new geometry bound to bones or trace points | SINGLE, BLUR, CLONEBLUR, DUST, EMIT, GHOSTING, PARTICLE, ARROW, BONEPOSEFF, BONELISTEFF, LINE2BONE, AROUNDEFFECT, ATTRIBUTE |

Two types are worth naming now because they change the shape of the work package:

- **`SINGLE` is a bridge into the `.egp` runtime.** `DxEffCharSingle.cpp:103`
  calls `DxEffSinglePropGMan::GetInstance().LoadEffectGProp(m_szFileName)` — it
  plays a whole `.egp` effect group attached to a named trace point on the piece
  (`m_szTrace`, `m_szTraceBack`). **`Char/` and `Single/` are not independent
  work packages in the direction Char→Single.**
- **`LEVEL` is the item enchant/grind glow.** `DxEffCharLevel.cpp:813` reads
  `m_pCharPart->GetGrindLevel()` and indexes a 12-entry table of
  ambient/specular/reflect/flow/glow sub-effects. It is the `+1…+11` weapon and
  armour shine, and it is by a wide margin the most-authored type in the data.

**Measured.** Shader usage across all of `Char/`: every `SetVertexShader` /
`SetPixelShader` / `SetPixelShaderConstant` call in the directory is **commented
out**, and they all live in one file (`DxEffCharDot3.cpp`, 20 occurrences, all
dead). One type is a genuine shader path — `NORMALMAP` uses `DxMaterialHLSL` and
`DrawMeshNORMALMAP` — and it accounts for **5 blobs in the entire shipped
corpus**. Two types use a render target for a glow post-pass:
`DxEffCharLevel.cpp:1193,1294` and `DxEffCharNeon.cpp:267,282,360`, both writing
into `DxSurfaceTex`'s shared glow surfaces.

So, like `Single/`, **`Char/` is fixed-function** — with the one exception of the
`LEVEL`/`NEON` glow surfaces, which is a real (and on mobile, expensive)
difference from `Single/`.

---

## 2. What data feeds it — and `EffectChar.rcc` is the small half

There are **three** sources, not one.

### 2.1 `EffectChar.rcc` — 198 files, 449 effect records

**Measured** (`char-census.js`). `Ran/data/effect/char/EffectChar.rcc` is
**69,836 bytes on disk, 222,764 uncompressed, 198 entries**:

| Extension | Files | Loader |
|---|---:|---|
| `.effskin_a` | 185 | `DxEffCharDataArray::LoadFile` — an array of piece slots |
| `.effskin` | 12 | `DxEffCharData::LoadFile` — one piece slot |
| `.effskin_a_before` | 1 | array; one stale editor artefact (`very_strong`) |

The format is simple and needed no solving:

```
0    128  CSerialFile type string   "default" in all 198   <- written by CSerialFile, not by this format
128    4  u32 version               0 in all 198           <- ditto; the EXTENSION is the discriminator
132       body
   .effskin_a : [u32 nSlots] then nSlots x [BOOL bExist][DxEffCharData body]
   .effskin   : [DxEffCharData body]

DxEffCharData body : [u32 EMPIECECHAR][u32 nEffects] then nEffects x [u32 nBytes][blob]
blob               : [u32 TypeID][u32 version][u32 declaredSize][blitted EFFCHAR_PROPERTY_*]
```

**Measured: 198 of 198 parse and consume to the exact last byte, zero
failures.** Every `.effskin_a` declares `nSlots = 12`, which was `PIECE_SIZE`
when they were written; `PIECE_SIZE` is 25 today
(`Meshs/DxPieceDefine.h`), and the count comes from the file, so this is
forward-safe.

**Measured: `.effskin` is not encrypted.** `DxEffCharData::LoadFile` calls
`GLOGIC::openfile_basestream` with no `EMBYTECRYPT` argument, so it defaults to
`EMBYTECRYPT_NONE`. There is no version gate. Three effect *types* carry
`VERSION = 0x0200` (`SINGLE`, `SPECULAR2`, `TEXDIFF`) without triggering one —
**the 0x0200 crypt gate recorded in `CRYPT-MAP.md` lives in the loader, not in
the number**, and this format is the counter-example that proves it.

449 effect blobs across 976 piece slots. Per piece slot:

| Slot | Slots | Types present |
|---|---:|---|
| HEAD | 172 | SHOCK, LEVEL, ALPHA, NEON, REFLECTION2, AMBIENT, MULTITEX, GHOSTING, TEXDIFF, PARTICLE |
| UPBODY | 167 | SINGLE, BLUR, SHOCK, ARROW, ALPHA, NEON, AMBIENT, MULTITEX, GHOSTING, LINE2BONE |
| GLOVE | 165 | SINGLE, SHOCK, ALPHA, AMBIENT, MULTITEX, GHOSTING |
| FOOT | 163 | SINGLE, SHOCK, ALPHA, NEON, AMBIENT, MULTITEX, GHOSTING |
| LOBODY | 162 | SINGLE, BLUR, SHOCK, ALPHA, NEON, AMBIENT, MULTITEX, GHOSTING |
| HAIR | 96 | ALPHA, NEON, NOALPHA, AMBIENT, GHOSTING, USERCOLOR |
| RHAND | 49 | SINGLE, BLUR, LEVEL, ALPHA, MULTITEX, GHOSTING |
| LHAND | 2 | BLUR, ALPHA |

Only 8 of the 12 declared slots are ever used. `HEADGEAR`, `VEHICLE`, `WING` and
`NECK` never carry an `.effskin` effect.

### 2.2 `.cps` character pieces — the real input

**Measured.** Every `DxSkinPiece::LoadPiece_*` in
`SOURCE/Lib_Engine/Meshs/DxSkinPieceSaveLoad.cpp` — all seventeen of them —
ends with

```
[u32 nEffects] then nEffects x [u32 TypeID][ DxEffChar::LoadFile ]
[u32 m_dwFlag]                                          <- last field (0x0105+)
```

so **a character piece carries its own effect stack inline**. That makes
`SkinObject.rcc`'s 12,879 `.cps` the dominant consumer of `Char/`, not the 198
`.effskin`.

`char-cps-walk.js` walks the piece record for 14 of the 17 versions. The
validation is the same one used for `item.isf` and `.wld0`: because the effect
list is the **second-to-last** field, a walk that lands on the exact last byte
proves the effect list was read correctly.

**Measured: 11,331 of 12,879 `.cps` (88.0%) walk to the exact last byte.**

| `.cps` version | Files | Walked | Failed | Walker |
|---|---:|---:|---:|---|
| 0x0106 | 4,492 | 4,057 | 435 | yes |
| 0x0110 | 3,174 | 2,963 | 211 | yes |
| 0x0107 | 2,071 | 1,711 | 360 | yes |
| 0x0201 | 960 | 667 | 293 | yes |
| 0x0109 | 591 | 572 | 19 | yes |
| 0x0114 | 355 | 318 | 37 | yes |
| 0x0104 | 349 | 337 | 12 | yes |
| 0x0116 | 330 | 287 | 43 | yes |
| 0x0105 | 140 | 120 | 20 | yes |
| 0x0200 | 113 | 97 | 16 | yes |
| 0x0115 | 82 | 78 | 4 | yes |
| 0x0103 | 59 | 59 | 0 | yes |
| 0x0113 | 56 | 48 | 8 | yes |
| 0x0112 | 17 | 17 | 0 | yes |
| 0x0108 | 4 | 0 | 4 | yes |
| 0x0101 | 3 | 0 | 0 | no walker |
| **0x0117** | **8** | – | – | **no such loader exists in SOURCE** |
| garbage versions (`0x20202126`, `0x64646554`, `0x64646665`, `0xe7e7e6e1`) | 75 | – | – | corrupt |

Two of those rows are findings in their own right. **The 8 files at version
`0x0117` cannot be loaded by the shipped client either** — `DxSkinPiece::LoadPiece`'s
`switch` has no `case 0x0117`, so they hit `default:` and `return E_FAIL`. And 75
files have a version DWORD that is ASCII text or high-byte noise; they are corrupt
in the archive.

**Measured, of the 11,331 pieces that walk:**

- **8,535 (75.3%) carry at least one embedded `DxEffChar` effect**
- **18,083 effect blobs total**
- distribution: 2,796 pieces with none, 3,593 with one, 1,902 with two, 2,904
  with 3–5, 136 with 6+

18,083 is a **floor**: 1,548 pieces did not walk, and 86 more have no walker.

#### Two size traps found here, both the house pattern

Both are instances of the rule already in `ran-port.md` — *sizes are only
trustworthy on versions the engine does not know* — and both produce a
plausible-looking parse rather than an error.

**`SVERTEXINFLU`'s declared size is wrong on every record that has bones.**
`SVERTEXINFLU::SaveFile` (`Meshs/DxSkinMeshContainer9.cpp:55`) computes

```cpp
dwSize = 4 + 12 + 12 + 4 + sizeof(DWORD)*m_dwNumBone + sizeof(float)*m_dwNumBone;
if ( m_dwNumBone > 0 && m_dwNumBone != COL_ERR )
    dwSize += sizeof(DWORD)*m_dwNumBone + sizeof(float)*m_dwNumBone;   // AGAIN
```

so it **double-counts the bone arrays**. The engine never notices, because
`LoadFile` parses structurally on the known version `0x0101` and uses `dwSize`
only to skip an unknown one. Skipping trace points by `dwSize` — the obvious
implementation — desyncs the entire piece. Fixing this took the walk from
**11.1% to 60.2%**.

**The tail after the effect list is version-dependent.** Versions
`0x0101`–`0x0104` read nothing; `0x0105`+ read `m_dwFlag`; `0x0116` reads
`m_dwFlag` then an extra `DWORD` and a `std::string`. Assuming one uniform tail
shifts the EOF check by one field and makes a *correct* effect walk look broken.
Fixing this took the walk from **60.2% to 88.0%**.

**Measured, and worth recording as a negative result:** after those two fixes,
`solveDeltas` — which derives a per-`(TypeID, version)` size correction from
pieces carrying exactly one effect, where the answer is forced by
`actual = (fileLength - 4) - bodyStart` — finds **no correction is needed**. Unlike
`.egp`, where the node size was wrong by a knowable ±4, **the `DxEffChar`
declared block size is correct**. The residual 12% of failures come from six
`(type, version)` pairs whose evidence disagrees, i.e. whose bodies are genuinely
variable-length: `LEVEL 0x0105`, `USERCOLOR 0x0101`/`0x0102`, `LINE2BONE 0x0103`,
`TEXDIFF 0x0101`, `NORMALMAP 0x0102`.

### 2.3 Runtime-synthesised effects — no data at all

**Measured (source).** `DxCharPart::SetPart` (`DxCharPart.cpp:185-199`): after
cloning a piece's effects, if the part is character data and **no effect with
flag `EMECF_AMBIENT` is present, the engine creates one**:

```cpp
pEffChar = DxEffCharMan::GetInstance().AddDefaultAmbient( this, NULL );
```

with a hardcoded `D3DXCOLOR(180/255, 180/255, 180/255, 1)`. So **every character
part in the game has an `AMBIENT` effect whether the data provides one or not.**
That is the one part of `Char/` with no opt-out.

---

## 3. Is it required, or is it polish?

**Both, and the split is clean.** The base character mesh is drawn by
`DxSkinMesh::DrawMeshContainer`, called from `DxCharPart::Render` independently of
any effect. Delete all of `Char/` and characters still draw, textured and
animated.

What breaks, concretely:

| If absent | Visible result | Blobs | Verdict |
|---|---|---:|---|
| **AMBIENT** | The per-piece ambient/`TEXTUREFACTOR` tint is never applied. Since `AddDefaultAmbient` supplies one for *every* part, the whole cast loses its authored lighting tint. | 591 + 1 per part at runtime | **required** |
| **ALPHA / NOALPHA** | `SettingState` sets the blend/z states for the base draw. Pieces authored transparent (hair, wings, ghost forms) render opaque; `halfalpha.effskin_a` — the client's stealth/fade state, `GLCharacter.cpp:4878` — does nothing. | 145 | **required** |
| **SHOCK / MARK** | Hit flash and the target/marking overlay never appear. Combat loses its damage feedback. | 9 | **required for game feel** |
| **LEVEL** | Every `+N` enchanted weapon and armour piece renders as its base material. No glow, no flow, no enchant specular. | **7,015** | polish, but the most-authored thing in the subsystem |
| **MULTITEX / TEXDIFF / REFLECTION2 / SPECULAR2 / NEON / USERCOLOR / NORMALMAP** | Extra passes of the same mesh with a second texture layer are simply not drawn (`DxEffCharTexDiff::Render` calls `DrawMeshContainer` a second time with its own material set). Pieces look flat but correct. | 5,206 | polish |
| **BLUR / CLONEBLUR / GHOSTING** | Weapon swing trails and after-images vanish. | 2,922 | polish |
| **SINGLE** | Every `.egp` attached to a character bone stops playing: buff auras, class effects, status effects, summon markers. | 2,349 | **required for combat readability** |
| **ARROW / LINE2BONE / PARTICLE** | Bow-string geometry, bone-to-bone beams, per-piece particles vanish. | 20 | drop |

**Measured — the client-side call sites** (`SOURCE/Lib_Client/G-Logic/*.cpp`,
all through `DxEffcharDataMan`):

| Call site | What it puts on the character |
|---|---|
| `GLFactEffect.cpp:85,90` | skill body effects — `SEXT_DATA::GETTARGBODY01/02(emELMT)`, per element |
| `GLFactEffect.cpp:27` | status-ailment body effects — `GLCONST_CHAR::strBLOW_BODY_EFFECTS[]` (stun, stone, burn, frozen, mad, poison, curse, numb) |
| `GLCharClient.cpp:688`, `GLCharacter.cpp:741,786,1314` | the per-class passive effect — `cCONSTCLASS[].strCLASS_EFFECT` |
| `GLCharClient.cpp:1454`, `GLCharacter.cpp:4878`, `GLCrowClient.cpp:1506`, `GLMaterialClient.cpp:444` | `strHALFALPHA_EFFECT` — the transparency state |
| `GLAnySummon.cpp:238`, `GLSummonClient.cpp:149`, `GLCrowClient.cpp:424`, `GLMaterialClient.cpp:268` | summons, monsters and dropped materials |
| `Meshs/DxSkinChar.cpp:147,198,215,251` | per-item "self effect" (`DxEffSelfEffect`, also from `EffectChar.rcc`), for pieces, `.abl` attach bones and vehicles |

So `Char/` sits directly on the **combat and status-effect** path, not only on the
cosmetic one. A player under `stun` or `frozen` shows it through an `.effskin_a`
on their body; a buffed player shows it through `GETTARGBODY01`.

---

## 4. The 25,880 lines, broken down

**Measured** (`wc -l` over `SOURCE/Lib_Engine/DxEffect/Char/*.cpp *.h`). Blob
counts are `.effskin` + `.cps` combined, from §2.

### Framework — 2,323 lines

| File(s) | Lines | Role |
|---|---:|---|
| `DxEffChar.cpp/.h` | 803 | base class, type enum, `DxEffCharMan` factory, state-order comparators |
| `DxEffCharData.cpp/.h` | 720 | the `.effskin` container + `DxEffcharDataMan` put/out API |
| `DxEffCharData2.cpp` | 331 | `DxAttBone` / `DxVehicle` variants of the same put/out API |
| `DxEffSelfEffect.cpp/.h` | 469 | per-item self-effect container (same blob format) |

### Types present in shipped data — 17,311 lines

| Type | ID | Lines | Blobs | Role |
|---|---:|---:|---:|---|
| **LEVEL** | 8 | 2,043 | **7,015** | item enchant glow: 12-level table of ambient/specular/reflect/flow/glow |
| **BLUR** | 1 | 1,560 | 2,840 | weapon swing trail ribbon between two trace points |
| **SINGLE** | 0 | 1,254 | 2,349 | plays an `.egp` attached to a trace point |
| **PARTICLE** | 22 | 1,334 | 1 | per-piece particle emitter |
| **LINE2BONE** | 27 | 1,660 | 8 | textured line between two named bones |
| **ARROW** | 6 | 1,063 | 11 | bow-string / arrow geometry |
| **MULTITEX** | 17 | 971 | 2,975 | extra pass, 2 blended textures with independent UV scroll |
| **CLONEBLUR** | 2 | 942 | 31 | after-image copies of the mesh |
| **REFLECTION2** | 14 | 928 | 264 | cubemap reflection pass |
| **NEON** | 11 | 866 | 393 | glow pass through a render target |
| **SPECULAR2** | 19 | 788 | 303 | multi-pass specular |
| **TEXDIFF** | 21 | 699 | 1,094 | extra pass with a substitute texture set |
| **USERCOLOR** | 25 | 573 | 172 | player-chosen colour applied to named texture slots |
| **MARK** | 12 | 518 | 3 | targeting/mark overlay state |
| **GHOSTING** | 18 | 444 | 51 | motion after-image driven by the anim controller |
| **ALPHA** | 10 | 412 | 43 | alpha-blend state for the base draw |
| **AMBIENT** | 15 | 399 | 591 | ambient colour / `TEXTUREFACTOR` state |
| **SHOCK** | 4 | 329 | 6 | hit-flash state |
| **NOALPHA** | 13 | 277 | 102 | forces alpha off for the base draw |
| **NORMALMAP** | 26 | 251 | 5 | the only real shader path (`DxMaterialHLSL`) |

### Types with code but **zero** occurrences in shipped data — 6,246 lines

**Measured.** No `.effskin` and no walked `.cps` contains any of these:

| Type | ID | Lines | Registered in the editor? |
|---|---:|---:|---|
| BONELISTEFF | 24 | 1,500 | yes |
| BONEPOSEFF | 23 | 944 | yes |
| ATTRIBUTE | 5 | 770 | **no** (`RegistType` commented out) |
| DOT3 | 16 | 745 | **no** |
| TOON | 20 | 639 | yes |
| AROUNDEFFECT | 28 | 608 | yes |
| SPECULAR | 7 | 395 | **no** |
| DUST | 3 | 335 | **no** |
| EMIT | 9 | 310 | **no** |

**That is 24.1% of the directory that the shipped content never exercises.**

### The six unsupported IDs — 0 lines

`OVERLAY` (29), `TOTALSHADER` (30), `VISUALMATERIAL` (31), `SPLINESINGLEEFF`
(32), `WORLDBATTLEFLAG` (33), `DISABLERENDER` (34) have **no source files in
`Char/`** — they were stripped from this branch and `IsEffectNotSupported`
size-skips them. **Measured: 275 blobs of them still sit in the shipped `.cps`**
(`OVERLAY` 252, `VISUALMATERIAL` 23), and the shipped client discards every one.
A port should do the same and log nothing.

### Summary

| Bucket | Lines | Share |
|---|---:|---:|
| Framework (container, factory, put/out API) | 2,323 | 9.0% |
| Types the data actually uses | 17,311 | 66.9% |
| Types with code and **no data** | 6,246 | 24.1% |
| **Total** | **25,880** | |

---

## 5. Asset footprint

**Measured** (`char-cps-walk.js --strings`). A free string scan over the effect
blobs finds **5,006 distinct filename-shaped tokens**; **4,159 resolve exactly**
against the 58,622 filenames in `Ran/` (loose + archive contents).

Of the 847 that do not:

- **226 begin with a non-alphanumeric character** — the `char[MAX_PATH]`
  overwrite artefact `EFFECTS-SCOPE.md` §5 already documented: a shorter string
  written over a longer one leaves the old tail after the NUL, and a free scan
  picks up fragments. These are debris, not references.
- **341 are `_s` / `_n` / `_p` / `_spc` maps** — specular, normal and gloss
  companions to textures that *do* ship. Consistent with an install that only
  carries the low-detail texture set.

**This is a scan, not a parse, and it is weaker evidence than the `.egp` asset
audit.** `effect-fields.js` located the `.egp` name fields *by measurement* and
so removed exactly this class of noise; **I did not do the equivalent for
`Char/`.** Treat 4,159 as a candidate floor.

**Measured — `SINGLE` pulls in the `.egp` corpus.** 2,349 `SINGLE` blobs name
**475 candidate `.egp`**; 98 resolve exactly against `Effect.rcc` and **359 more
resolve after dropping 1–3 leading scan-debris characters**, leaving 18
unresolved. So roughly **457 distinct `.egp`** are reached *through* character
pieces. This closes a loop in `EFFECTS-SCOPE.md` §4, which measured "514 `.egp`
names found in `SkinObject.rcc`" without knowing what named them: it was
`DxEffCharSingle`.

---

## 6. Which `.effskin*` shipped content actually names

**Measured** (`char-refs.js`), scanning every archive and loose data directory,
decoding the AES layer on `data/glogic/` and trying each byte-crypt table:

| Source | Entries | New `.effskin*` names |
|---|---:|---:|
| `GLogic.rcc` | 376 | **149** |
| `SkinObject.rcc` | 16,570 | 0 |
| `Map.rcc` / `Gui.rcc` / `Effect.rcc` / `EffectChar.rcc` | 5,048 | 0 |
| loose `data/piece`, `data/skin` | 3,891 | 0 |

| | Count |
|---|---:|
| Distinct names referenced | 149 |
| …that resolve to a shipped file | **147 of 198 (74.2%)** |
| …named but **not** shipped | 2 (`framegun_fire.effskin_a`, `science_rank_gun.effskin`) |
| Shipped but never named | 51 |

**Every reference comes from four `GLogic.rcc` entries**: `skill.csv` (114 names),
`default.charclass` (9, including `strHALFALPHA_EFFECT halfalpha.effskin_a`),
`class4.classconst` and `class5.classconst` (1 each).

`default.charclass` is AES-encrypted and yields **nothing** raw — skipping
`gamecrypt.js` loses the class, half-alpha and status-effect names entirely, and
it is the difference between 140 and 149 here.

**74.2% referenced compares very favourably with `.egp`'s 33.7%.** The 51
unreferenced are dominated by `*_st.effskin_a` skill-body variants (`sba102_st`,
`sca103_st`, …) — the same legacy-content pattern the texture and `.egp` audits
found. Unlike `.egp`, the names here are **stored whole** per element
(`SEXT_DATA::strTARGBODY01[EMELEMENT_MAXNUM]`, `GLSkillExData.h:537`), so a
string scan can in principle be complete. It is still a review list, not a
deletion list.

---

## 7. Recommendation: what to build, in what order

Difficulty is **inferred** — my judgement of Unity rebuild cost. Frequency is
measured.

### Tier 0 — the container and the decorator model (≈1,500 lines equivalent)

Build the `.effskin` reader and the `.cps` effect-list reader, and the
**state-order render model**: a character part draws its mesh, wrapped by
"state" effects, followed by "extra pass" effects, with four ordered buckets.
Design an unknown `TypeID` as a **no-op that is skipped by its declared size**,
exactly as `IsEffectNotSupported` already does — the shipped client relies on
that behaviour for 275 blobs, so it is proven safe.

Everything below then becomes independently droppable, which is the whole point.

### Tier 1 — required for a character to look and read correctly

| Type | Blobs | Rebuild cost | Why |
|---|---:|---|---|
| **AMBIENT** | 591 + synthesised for every part | **Trivial** | one colour multiplier; must exist because `AddDefaultAmbient` is unconditional |
| **ALPHA** | 43 | **Trivial** | a transparent material variant |
| **NOALPHA** | 102 | **Trivial** | an opaque material variant |
| **SINGLE** | 2,349 | **Low** *given* the `.egp` runtime | bone attachment + a call into the effect player already scoped in `EFFECTS-SCOPE.md` |
| **SHOCK** | 6 | **Trivial** | a flash colour on the base material |

That is **3,091 blobs (16.7%)** and, in `Char/` terms, **2,671 lines** of the
original (AMBIENT 399 + ALPHA 412 + NOALPHA 277 + SINGLE 1,254 + SHOCK 329) on
top of the 2,323-line framework. It gets characters looking right, transparency
working, buffs and status effects visible, and hits readable.

**Sequencing note:** `SINGLE` should be built **after** the `.egp` runtime's
first six types (`EFFECTS-SCOPE.md` §6), not before — it is a consumer of them.

### Tier 2 — the visual identity of geared characters

| Type | Blobs | Rebuild cost | Why |
|---|---:|---|---|
| **LEVEL** | **7,015** | **High** | 12 preset levels × 5 sub-effects (ambient, specular, reflect, flow, glow), and the glow path needs a render target |
| **MULTITEX** | 2,975 | **Low** | second texture, independent UV scroll — one shader, one extra pass |
| **BLUR** | 2,840 | **Medium** | trail ribbon generated from two trace points' motion history; shares its shape with `Single/`'s `BLURSYS`, so build them together |
| **TEXDIFF** | 1,094 | **Low** | a second draw with a substitute texture set |

Cumulative through Tier 2: **17,015 of 18,532 blobs (91.8%)**.

`LEVEL` is the single hardest call in this document. It is 38% of all authored
effect records and it is what makes an enchanted item look enchanted — but it is
also the most expensive thing here on mobile (a full-screen glow surface plus up
to five extra passes per piece). **Recommended: build `LEVEL`'s ambient and flow
sub-effects, and fake glow with an additive rim rather than a render target** —
the same call `EFFECTS-SCOPE.md` §6 made for `POINTLIGHT`.

### Tier 3 — cheap adds, do if time allows

`NEON` (393), `SPECULAR2` (303), `REFLECTION2` (264), `USERCOLOR` (172),
`GHOSTING` (51), `CLONEBLUR` (31). Cumulative: **18,229 (98.4%)**.

`USERCOLOR` deserves a flag: it is player-chosen costume colour. It is only 172
blobs, but if the game exposes colour customisation, its absence is a
*functional* bug, not a visual one. Check the feature before dropping it.

### Drop permanently

- **The nine types with zero shipped data** — `BONELISTEFF`, `BONEPOSEFF`,
  `ATTRIBUTE`, `DOT3`, `TOON`, `AROUNDEFFECT`, `SPECULAR`, `DUST`, `EMIT`.
  **6,246 lines, 24.1% of the directory, 0 blobs.** Five of them are not even
  registered in the editor.
- **The six unsupported IDs** — already dead in the shipped client.
- **`NORMALMAP`** (5 blobs) — the only real shader path, for 0.03% of the data.
- **`ARROW`** (11), **`LINE2BONE`** (8), **`PARTICLE`** (1), **`MARK`** (3) —
  3,575 lines of source between them for 23 blobs. `MARK` is the one to
  reconsider if targeting feedback turns out to need it; the other three are the
  worst line-count-per-blob ratio in the project.

### The size of the real work package

| | Lines | Blobs covered |
|---|---:|---:|
| Nominal `Char/` | 25,880 | 18,532 |
| minus the 9 zero-data types | −6,246 | −0 |
| minus permanently dropped types (`NORMALMAP` 251, `ARROW` 1,063, `LINE2BONE` 1,660, `PARTICLE` 1,334, `MARK` 518) | −4,826 | −28 |
| **Realistic port surface** | **14,808** | **18,504 (99.85%)** |
| …of which Tier 0 + Tier 1 alone | 4,994 | 3,091 (16.7%) |

**So the honest number for `Char/` is not 25,880 — it is ~14,800 lines of
reference source, and the first useful slice is under 5,000.** That is a
different proposition from the opaque block `EFFECTS-SCOPE.md` §7 had to record.

---

## 8. What I could not determine

Stated plainly, because an unverified claim recorded as fact is worse here than
an open question.

1. **1,548 `.cps` (12%) do not walk to exact EOF**, so their effect lists are
   unmeasured. The cause is identified — six `(type, version)` pairs whose bodies
   are variable-length and whose declared size therefore cannot be used to
   advance: `LEVEL 0x0105`, `USERCOLOR 0x0101`/`0x0102`, `LINE2BONE 0x0103`,
   `TEXDIFF 0x0101`, `NORMALMAP 0x0102`. Closing it needs each of those six
   `LoadFile` bodies transcribed. **18,083 is a floor, not a total**, and the
   missing 12% is skewed toward exactly the types that are already the most
   common, so the true figure is probably nearer 20,000.
2. **Three `.cps` versions have no walker** — `0x0101` (3 files) and the 8 files
   at `0x0117`, which no loader in `SOURCE` can read either. 86 files.
3. **The asset name scan is a free regex, not a field read.** 5,006 candidates,
   4,159 resolving, 226 obvious debris. `effect-fields.js` did this properly for
   `.egp` by locating name fields by measurement; the equivalent for the 20
   `EFFCHAR_PROPERTY_*` families is not done. Every asset number in §5 is
   therefore softer than the equivalents in `EFFECTS-SCOPE.md`.
4. **No `EFFCHAR_PROPERTY_*` struct layout is verified against data.** I read the
   headers and measured blob lengths, but per the project rule those `sizeof`s
   must come from the layout probe, not hand arithmetic, before anyone writes an
   importer. `probe.cpp` is owned by other work and I did not touch it. This is
   the single biggest prerequisite for actually building the reader.
5. **Whether the 51 unreferenced `.effskin*` are dead.** `skill.csv` stores whole
   names per element so a scan *can* be complete, but I did not parse `skill.csv`
   structurally to prove it. 147 is a floor.
6. **Runtime cost.** Every number here is a count. How many `DxEffChar`
   instances are live during a real fight, how many extra mesh passes that means
   per frame, and whether `LEVEL`'s glow render target has an affordable mobile
   equivalent — none of that is measured, and it is the question that decides
   whether Tier 2 is buildable at all. It needs a running client.
7. **Whether Tier 1 alone looks acceptable.** §7's claim that a character without
   `LEVEL`, `MULTITEX`, `TEXDIFF` and `BLUR` "looks flat but correct" is a
   judgement from reading `Render` bodies. Nothing here was rendered.
8. **`DxEffCharData2.cpp` (331 lines) — the `DxAttBone` and `DxVehicle` paths.**
   Read enough to confirm they reuse the same containers and blob format, not
   enough to say whether `.abl` / `.vcf` data carries its own effect lists the
   way `.cps` does. If it does, §2.2's floor rises again.
9. **`EffKeep/` has no caller in `Lib_Client`** and loads a *different* format
   from `EffectChar.rcc` — of which no file ships. **Inferred: dead code.** Not
   part of `Char/`, but noted because `EFFECTS-SCOPE.md` §7 groups
   `EffAni`/`EffKeep`/`EffProj` as 5,355 lines of "effect playback attach
   points"; at least one of the three appears to be unreachable.

## 9. Numbers worth pinning in `test.js`

`MOBILE/tools/rcc-extract/test.js` is owned by other work and was not touched.
When it is free, the checks with the most diagnostic power are:

- **198 `.effskin*` entries; 185/12/1 by extension; 198 walk to exact EOF; 449
  effect blobs; 976 piece slots; 16 distinct types.**
- **12,879 `.cps`; 11,331 walk to exact EOF; 18,083 embedded effect blobs;
  8,535 pieces carrying at least one.**
- **`LEVEL` 7,015 / `MULTITEX` 2,975 / `BLUR` 2,840 / `SINGLE` 2,349.**
- **147 of 198 `.effskin*` referenced, from 4 `GLogic.rcc` entries.**

The exact-EOF counts are the strongest single check available for this format —
the same role they already play for `item.isf` and `.wld0`. A regression in the
`SVERTEXINFLU` double-count fix drops the `.cps` figure from 11,331 to 1,433
immediately.
