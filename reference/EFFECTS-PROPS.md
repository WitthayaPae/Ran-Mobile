# Effect PROPERTY layouts — measured, and validated against all 4,294 files

`EFFECTS-SCOPE.md` §8.2 left this open:

> The full numeric layouts of `PARTICLESYS`, `GROUND`, `BLURSYS`, … are read
> from headers but **not verified against data**. Per the project rule, those
> `sizeof`s should come from the layout probe, not from hand arithmetic, before
> anyone writes an importer. I did not add them to the probe.

They are in the probe now. This document is what came out, and — more
importantly — what happened when the result was tested against the shipped data
rather than admired.

Everything below is labelled:

- **MEASURED** — computed by MSVC (`sizeof`/`offsetof` over the real headers),
  or counted by running a decoder over all 4,294 shipped `.egp`.
- **INFERRED** — read off the C++ by a human.

```
cd MOBILE/tools/layout-probe
node gen-effectprops.js > effectprops.gen.inc   # also writes 2 sidecar files
node build-structs.js                           # compiles -> layout.json

cd ../rcc-extract
node --max-old-space-size=8192 effect-props.js            # the validation run
node --max-old-space-size=8192 effect-props.js --verbose  # every field, not just failures
```

New files: `MOBILE/tools/layout-probe/gen-effectprops.js` (+ its three generated
outputs) and `MOBILE/tools/rcc-extract/effect-props.js`. Modified:
`probe.cpp` (seven header `#include`s, the generated legacy header, and one dump block) and `effect-egp.js` (one
optional hook, plus a comment). `test.js`, `animinfo.js`, `extract-animtypes.js`,
`verify-animtypes.js` and `EFFECTS-SCOPE.md` were not touched.
`node test.js` — **275 passed, 0 failed**. `cd SOURCE && git status --porcelain`
— empty.

**I chose a new `effect-props.js`** rather than extending `effect-egp.js`.
`effect-egp.js` stays the container walker with no dependency on the probe;
`effect-props.js` is the field decoder and imports `layout.json`. The one change
to `effect-egp.js` is an opt-in `opts.bodySize` hook, off by default, which §5
below needs.

---

## 1. How the layout gets from the header to the decoder

**No offset in `effect-props.js` is written down.** The chain is:

```
SOURCE/…/DxEffect/Single/*.h        the real headers, read-only
   │
   ├─ gen-effectprops.js ──► effectprops.gen.inc      DUMP_STRUCT/DUMP_MEMBER, names only
   │                     ──► effectprops.types.json   declared TYPE SPELLINGS, names only
   │                     ──► effectprops.legacy.gen.h verbatim copies of the .cpp-only structs
   │
   └─ probe.cpp + build-structs.js ──► layout.json .effectProps
                                          every size and offset, computed by MSVC
   effect-props.js reads layout.json. It computes nothing.
```

Three details that are the difference between this working and this being
another hand-transcription:

**The structs are nested and all six are called `PROPERTY`.** `gen-structs.js`
emits unqualified names, so adding these headers to it would have collapsed six
different `PROPERTY`s into one and then failed to compile. `gen-effectprops.js`
emits `SEQUENCE_PROPERTY::PROPERTY`, which is unambiguous and still legal for
`offsetof` (the nested structs have no virtuals, so they stay standard-layout —
the *outer* `*_PROPERTY` types are polymorphic and could not be measured).

**The declared type spelling is captured too, and cross-checked.** `layout.json`
gives a size, not a type, and `float`/`int`/`DWORD`/`BOOL` are all 4 bytes.
`effectprops.types.json` records the spelling as written in the header, and
`effect-props.js` refuses to decode a field whose spelling and measured size
disagree. **MEASURED: all 254 header members agree** — `float`→4 ×153,
`int`→4 ×28, `D3DXCOLOR`→16 ×21, `D3DXVECTOR3`→12 ×16, `DWORD`→4 ×9,
`BOOL`→4 ×9, `char[256]`→256 ×13, `D3DXVECTOR2`→8 ×3, `D3DXVECTOR4`→16 ×1,
`float[4]`→16 ×1. That is what makes `m_fMaterial[4]` a 4-float array rather
than a mis-typed single float, without anyone deciding it by eye.

**MESH 0x0100–0x0103 and PARTICLESYS 0x0100–0x0106 are not in any header.**
Their `PROPERTY_10x` structs are declared at *file scope inside*
`DxEffectMeshPROP.cpp` / `DxEffectParticleSysPROP.cpp`, so nothing can
`#include` them — and they are **4,120 nodes, 12.5% of the six types' total**,
including 70% of all PARTICLESYS. `gen-effectprops.js` copies those declarations
out **verbatim** into `effectprops.legacy.gen.h`, one namespace per file
(both files name their oldest struct `PROPERTY_100`), and the probe measures
them like anything else. The text moves byte for byte and the compiler still
does all the arithmetic.

> Bug worth recording, because it is the same class as the one already in
> `README.md`: the first version of that extraction sliced the original file
> using offsets found in a **comment-stripped** copy. `stripComments()` *drops*
> line comments, so every offset after the first `//` shifted and the struct
> bodies came out starting four lines early. It now uses a length-**preserving**
> blanking pass. Comments are blanked only in the text being *scanned*; the
> text being *copied* keeps them, so nothing that is commented out in the
> original can become live in the copy.

---

## 2. MEASURED — struct sizes and node body sizes

`EFF_PROPERTY::GetSizeBase()` is `float + float + BOOL + D3DXMATRIX`
(`DxEffSinglePropGMan.cpp:136`). The probe emits it as that same sum, not as a
literal:

| | bytes |
|---|---:|
| `EFFPROP_PREFIX` — matrix + `m_bMoveObj` + 2 floats | **76** |
| `EFFPROP_PREFIX_AFFINE` — the same plus a `DXAFFINEPARTS` | **112** |

Every `*_PROPERTY::LoadFile` in `DxEffect/Single/` reads that prefix and then
blits the whole `PROPERTY` with one `ReadBuffer`. So the node body is
`prefix + sizeof(PROPERTY)`, and every number in this table is compiler-derived:

| type / version | struct | prefix | sizeof | body |
|---|---|---:|---:|---:|
| SEQUENCE 0x0102 | `SEQUENCE::PROPERTY` | 76 | 460 | **536** |
| SEQUENCE 0x0101 | `SEQUENCE::PROPERTY` | 112 | 460 | **572** |
| SEQUENCE 0x0100 | `SEQUENCE::PROPERTY_100` | 112 | 452 | **564** |
| MESH 0x0105 | `MESH::PROPERTY` | 76 | 1228 | **1304** |
| MESH 0x0104 | `MESH::PROPERTY` | 112 | 1228 | **1340** |
| MESH 0x0103 | `EFFLEGACY_MESH::PROPERTY_103` | 112 | 1224 | **1336** |
| MESH 0x0102 | `EFFLEGACY_MESH::PROPERTY_102` | 112 | 1208 | **1320** |
| MESH 0x0101 | `EFFLEGACY_MESH::PROPERTY_101` | 112 | 940 | **1052** |
| MESH 0x0100 | `EFFLEGACY_MESH::PROPERTY_100` | 112 | 936 | **1048** |
| PARTICLESYS 0x0107 | `PARTICLESYS::PROPERTY` | 76 | 1052 | **1128** |
| PARTICLESYS 0x0106 | `EFFLEGACY_PARTICLESYS::PROPERTY_106` | **76** | 1048 | **1124** |
| PARTICLESYS 0x0105 | `EFFLEGACY_PARTICLESYS::PROPERTY_105` | **76** | 792 | **868** |
| PARTICLESYS 0x0104 | `EFFLEGACY_PARTICLESYS::PROPERTY_104` | 112 | 776 | **888** |
| PARTICLESYS 0x0103 | `EFFLEGACY_PARTICLESYS::PROPERTY_102_103` | 112 | 772 | **884** |
| PARTICLESYS 0x0102 | `EFFLEGACY_PARTICLESYS::PROPERTY_102_103` | 112 | 772 | **884** |
| PARTICLESYS 0x0101 | `EFFLEGACY_PARTICLESYS::PROPERTY_101` | 112 | 760 | **872** |
| PARTICLESYS 0x0100 | `EFFLEGACY_PARTICLESYS::PROPERTY_100` | 112 | 748 | **860** |
| MOVEROTATE 0x0103 | `MOVEROTATE::PROPERTY` | 76 | 28 | **104** |
| MOVEROTATE 0x0102 | `MOVEROTATE::PROPERTY` | 112 | 28 | **140** |
| MOVEROTATE 0x0101 | `MOVEROTATE::PROPERTY_101` | 112 | 24 | **136** |
| MOVEROTATE 0x0100 | `MOVEROTATE::PROPERTY_100` | 112 | 12 | **124** |
| GROUND 0x0102 | `GROUND::PROPERTY` | 76 | 400 | **476** |
| GROUND 0x0101 | `GROUND::PROPERTY` | 112 | 400 | **512** |
| GROUND 0x0100 | `GROUND::PROPERTY_100` | 112 | 408 | **520** |
| BLURSYS 0x0102 | `BLURSYS::PROPERTY` | 76 | 320 | **396** |
| BLURSYS 0x0101 | `BLURSYS::PROPERTY` | 112 | 320 | **432** |
| BLURSYS 0x0100 | `BLURSYS::PROPERTY_100` | 112 | 324 | **436** |
| POINTLIGHT 0x0101 † | `POINTLIGHT::PROPERTY` | 76 | 68 | **144** |
| POINTLIGHT 0x0100 † | `POINTLIGHT::PROPERTY` | 112 | 68 | **180** |

† POINTLIGHT is not one of the six. It was measured because it turned out to be
the last unmeasured node type in `sk_dfly.egp` — see §5.

**Two things here are not obvious and would both be wrong if guessed.**

- **The same struct is used across versions**; only the prefix changes. SEQUENCE
  0x0101 and 0x0102 share `PROPERTY`, as do MESH 0x0104/0x0105, GROUND
  0x0101/0x0102, BLURSYS 0x0101/0x0102, MOVEROTATE 0x0102/0x0103.
- **PARTICLESYS 0x0105 and 0x0106 take the 76-byte prefix, not 112.** They
  dropped the `DXAFFINEPARTS` two versions before the type's current version,
  while every other type dropped it exactly at its `VERSION`. This is the same
  exception `effect-egp.js`'s `NO_AFFINE_OLD` already encodes, arrived at from a
  different direction, and it is 3,410 nodes — the single most expensive thing
  to get wrong in this table.

---

## 3. MEASURED — the field layouts

Offsets are **from the start of the node body** (i.e. after `[ver][size]`), for
the current version of each type. Machine-readable in
`MOBILE/tools/layout-probe/layout.json` under `effectProps`, including every
legacy version; reproduce this listing with `effect-props.js --verbose`.

### SEQUENCE 0x0102 — billboard quad + flipbook (body 536)

| off | size | type | field |
|---:|---:|---|---|
| 76 | 4 | DWORD | `m_dwFlag` |
| 80 | 12 | D3DXVECTOR3 | `m_vGVelocity` |
| 92 | 12 | D3DXVECTOR3 | `m_vGGravityStart` |
| 104 | 12 | D3DXVECTOR3 | `m_vGGravityEnd` |
| 116 | 4 | float | `m_fFlarePos` |
| 120 | 4 | float | `m_fRotateAngel` |
| 124/128 | 4 | float | `m_fRotateRate1` / `m_fRotateRate2` |
| 132…144 | 4 | float | `m_fLengthStart` / `Mid1` / `Mid2` / `End` |
| 148/152 | 4 | float | `m_fSizeRate1` / `m_fSizeRate2` |
| 156…168 | 4 | float | `m_fSizeStart` / `Mid1` / `Mid2` / `End` |
| 172/176 | 4 | float | `m_fWidthRate` / `m_fHeightRate` |
| 180/184 | 4 | float | `m_fAlphaRate1` / `m_fAlphaRate2` |
| 188…200 | 4 | float | `m_fAlphaStart` / `Mid1` / `Mid2` / `End` |
| 204/220/236 | 16 | D3DXCOLOR | `m_cColorStart` / `m_cColorVar` / `m_cColorEnd` |
| 252 | 4 | float | `m_fAniTime` |
| 256/260 | 4 | int | `m_iCol` / `m_iRow` |
| 264 | 4 | BOOL | `m_bTexRotateUse` — **dead, uninitialised** (§6) |
| 268 | 4 | float | `m_fTexRotateAngel` |
| 272 | 4 | BOOL | `m_bGIsColliding` — **dead, uninitialised** (§6) |
| 276 | 4 | int | `m_nBlend` |
| 280 | 256 | char[] | `m_szTexture` |

### MESH 0x0105 — `.x` mesh + texture + animated UVs (body 1304)

| off | size | type | field |
|---:|---:|---|---|
| 76 | 4 | DWORD | `m_dwFlag` |
| 80 | 4 | int | `m_nBlurObject` |
| 84/88 | 4 | int | `m_nCol` / `m_nRow` |
| 92 | 4 | float | `m_fAniTime` |
| 96 | 256 | char[] | `m_szMeshFile` |
| 352 | 256 | char[] | `m_szMeshFile1` — morph target, only under `USEBLENDMESH` |
| 608 | 256 | char[] | `m_szMeshFile2` — morph target, only under `USEBLENDMESH` |
| 864 | 256 | char[] | `m_szTexture` |
| 1120 | 4 | int | `m_nRotationType` |
| 1124 | 4 | int | `m_nBlend` |
| 1128 | 4 | int | `m_nPower` |
| 1132 | 4 | float | `m_fMorphRoopNum` |
| 1136/1140 | 4 | float | `m_fSizeRate1` / `2` |
| 1144…1156 | 4 | float | `m_fSizeStart` / `Mid1` / `Mid2` / `End` |
| 1160 | 12 | D3DXVECTOR3 | `m_vSizeXYZ` |
| 1172/1176 | 4 | float | `m_fAlphaRate1` / `2` |
| 1180…1192 | 4 | float | `m_fAlphaStart` / `Mid1` / `Mid2` / `End` |
| 1196 | 8 | D3DXVECTOR2 | `m_vHeight` |
| 1204 | 4 | float | `m_fRotationAngle` |
| 1208 | 4 | float | `m_fTexRotateAngel` |
| 1212 | 4 | int | `m_nTexRotateType` — **dead** (§6) |
| 1216 | 12 | D3DXVECTOR3 | `m_vTexCenter` |
| 1228 | 12 | D3DXVECTOR3 | `m_vTexVel` |
| 1240/1244 | 4 | float | `m_fTexScaleStart` / `End` |
| 1248 | **16** | float[4] | `m_fMaterial` — **dead, uninitialised** (§6) |
| 1264/1268 | 4 | float | `m_fMaterialRatio0` / `1` — **dead, uninitialised** |
| 1272/1288 | 16 | D3DXCOLOR | `m_clrStart` / `m_clrEnd` |

### PARTICLESYS 0x0107 — CPU emitter (body 1128)

| off | size | type | field |
|---:|---:|---|---|
| 76/88/100 | 12 | D3DXVECTOR3 | `m_vGVelocity` / `m_vGGravityStart` / `m_vGGravityEnd` |
| 112/120 | 8 | D3DXVECTOR2 | `m_vPlayTime` / `m_vSleepTime` (min,max) |
| 128 | 4 | DWORD | `m_dwFlag` |
| 132 | 4 | DWORD | `m_dwDummy` — **dead**, ctor sets 0 |
| 136 | 4 | int | `m_iCenterPoint` |
| 140 | 12 | D3DXVECTOR3 | `m_vRange` |
| 152/156 | 4 | float | `m_fRangeRate` / `m_fRotateAngel` |
| 160 | 4 | float | `m_fRotateLAngel` |
| 164/168 | 4 | float | `m_fRotateLRate1` / `2` |
| 172…184 | 4 | float | `m_fLengthStart` / `Mid1` / `Mid2` / `End` |
| 188 | 4 | DWORD | `m_uParticlesPerSec` |
| 192 | 256 | char[] | `m_szTexture` |
| 448 | 256 | char[] | `m_szMeshFile` |
| 704 | 256 | char[] | `m_szEffFile` — nested `.egp` to spawn |
| 960 | 12 | D3DXVECTOR3 | `m_vWindDir` |
| 972 | 4 | float | `m_fTexRotateAngel` |
| 976 | **16** | D3DXVECTOR4 | `m_vTexScale` |
| 992…1000 | 4 | float | `m_fGravityStart` / `Var` / `End` |
| 1004…1012 | 4 | float | `m_fSizeStart` / `Var` / `End` |
| 1016/1020 | 4 | float | `m_fAlphaRate1` / `2` |
| 1024…1036 | 4 | float | `m_fAlphaStart` / `Mid1` / `Mid2` / `End` |
| 1040/1056/1072 | 16 | D3DXCOLOR | `m_cColorStart` / `Var` / `End` |
| 1088/1092 | 4 | float | `m_fSpeed` / `m_fSpeedVar` |
| 1096/1100 | 4 | float | `m_fLife` / `m_fLifeVar` |
| 1104 | 4 | float | `m_fTheta` — emission cone |
| 1108/1112 | 4 | int | `m_nCol` / `m_nRow` |
| 1116 | 4 | float | `m_fAniTime` |
| 1120/1124 | 4 | int | `m_nBlend` / `m_nPower` |

### MOVEROTATE 0x0103 — transform animator (body 104)

| off | size | type | field |
|---:|---:|---|---|
| 76 | 4 | DWORD | `m_dwFlag` |
| 80 | 12 | D3DXVECTOR3 | `m_vVelocity` |
| 92/96/100 | 4 | float | `m_fRotateAngelX` / `Y` / `Z` |

The whole type is 28 bytes of payload. **EFFECTS-SCOPE.md §5 said MOVEROTATE has
no name fields; that is confirmed by the struct, and its 4,520 nodes decode with
100.00% of every field valid** — the only type in the set with a perfect score.

### GROUND 0x0102 — ground-aligned quad (body 476)

| off | size | type | field |
|---:|---:|---|---|
| 76 | 4 | DWORD | `m_dwFlag` |
| 80 | 4 | float | `m_fAniTime` |
| 84/88 | 4 | int | `m_iCol` / `m_iRow` |
| 92 | 4 | float | `m_fRotateAngel` |
| 96/100 | 4 | float | `m_fHeightRate1` / `2` |
| 104…116 | 4 | float | `m_fHeightStart` / `Mid1` / `Mid2` / `End` |
| 120/124 | 4 | float | `m_fSizeRate1` / `2` |
| 128…140 | 4 | float | `m_fSizeStart` / `Mid1` / `Mid2` / `End` |
| 144/148 | 4 | float | `m_fAlphaRate1` / `2` |
| 152…164 | 4 | float | `m_fAlphaStart` / `Mid1` / `Mid2` / `End` |
| 168/184/200 | 16 | D3DXCOLOR | `m_cColorStart` / `Var` / `End` |
| 216 | 4 | int | `m_nBlend` |
| 220 | 256 | char[] | `m_szTexture` |

### BLURSYS 0x0102 — trail ribbon (body 396)

| off | size | type | field |
|---:|---:|---|---|
| 76 | 4 | DWORD | `m_dwFlag` |
| 80 | 4 | int | `m_nBlend` |
| 84 | 4 | int | `m_nNum` — trail segment count |
| 88 | 4 | float | `m_fLife` |
| 92/96 | 4 | float | `m_fLengthStart` / `End` |
| 100/104 | 4 | float | `m_fAlphaStart` / `End` |
| 108/124 | 16 | D3DXCOLOR | `m_cColorStart` / `m_cColorEnd` |
| 140 | 256 | char[] | `m_szTexture` |

### Independent confirmation of the name fields

`effect-egp.js`'s `FIELDS` table was built two other ways — by histogramming
where filename-shaped runs start in the data (`effect-fields.js`), and by
reading the struct. **MEASURED: the probe agrees with both, exactly**, for all
six types and every version they share: SEQUENCE 280, MESH 96/352/608/864,
PARTICLESYS 192/448/704, GROUND 220, BLURSYS 140. Three independent
derivations, one answer.

---

## 4. VALIDATION — the deliverable

All numbers **MEASURED** over the 4,294 shipped `.egp` / 34,609 property nodes.

### (a) The declared node size equals the measured struct size

The strongest single check, because it is a whole-struct assertion against real
bytes that cannot pass by luck: a 4-byte error in either the prefix or the
`sizeof` fails on *every* node of that type.

**33,624 of 33,631 nodes (99.98%) declare exactly `prefix + sizeof(PROPERTY)`.**
Eighteen of the twenty-two (type, version) combinations that appear in the data
match at **100.00%**; the other four miss on a handful of nodes each.

The 7 that do not are all exactly **4 bytes short**, and they sit in 4 files —
which turn out to be 4 of the 5 files `EFFECTS-SCOPE.md` §8.3 could not explain.
See §5.

### (b) Coverage

**33,037 of 33,037 (100.00%)** of all SEQUENCE / MESH / PARTICLESYS /
MOVEROTATE / GROUND / BLURSYS nodes have a measured layout, across every
version present in the data. Before the `.cpp`-only structs were extracted this
was 87.5%, with PARTICLESYS at 23%.

### (c) Per-field validity, per type

Validity rules, per field family: floats finite and inside a plausible band;
`BOOL` is 0 or 1; enumerated ints inside the authored range; flag words with no
bit that no `#define` in the type's own header accounts for; `char[]` fields
NUL-terminated, printable, and — for the name fields — **resolving against what
actually ships**, matched the way the engine matches (case-insensitive, with an
extension-agnostic fallback, per `audit-assets.js`).

| type | nodes | field-checks | valid |
|---|---:|---:|---:|
| SEQUENCE | 8,866 | 363,383 | **99.86%** |
| MESH | 8,600 | 369,368 | 95.88% — **99.89%** excluding the two morph slots (below) |
| PARTICLESYS | 4,865 | 257,601 | **99.96%** |
| MOVEROTATE | 4,520 | 40,674 | **100.00%** |
| GROUND | 4,030 | 128,960 | **99.93%** |
| BLURSYS | 2,156 | 32,343 | **99.68%** |

Every field not listed below is at 100.00%. The ones that are not:

| field | nodes | valid | what the failures are |
|---|---:|---:|---|
| `SEQUENCE.m_szTexture` | 8,863 | 94.27% | 508 name a texture that does not ship |
| `MESH.m_szTexture` | 8,598 | 97.51% | 214 not shipped; 10 resolve only by ignoring the extension |
| `MESH.m_szMeshFile` | 8,599 | 98.67% | 114 not shipped; 18 by-stem |
| `MESH.m_szMeshFile1/2` | 8,599 | **13.8%** | the `Dolphin2.x`/`Dolphin3.x` editor defaults — see below |
| `GROUND.m_szTexture` | 4,030 | 97.79% | 89 not shipped |
| `PARTICLESYS.m_szTexture` | 4,863 | 98.77% | 60 not shipped, 12 by-stem |
| `PARTICLESYS.m_szMeshFile` | 4,863 | 99.55% | 22 not shipped |
| `PARTICLESYS.m_szEffFile` | 4,535 | 99.89% | 5 not shipped (4,169 nodes leave it empty) |
| `BLURSYS.m_szTexture` | 2,156 | 99.17% | 18 not shipped |
| `BLURSYS.m_dwFlag` | 2,153 | 96.01% | 86 set bit `0x4` — see §6 |
| `MESH.m_dwFlag` | 8,599 | 99.14% | 74 set bit `0x8` — see §6 |
| `SEQUENCE.m_dwFlag` / `GROUND.*` | | 99.99% | 1 node each |
| `GROUND.m_nBlend` | 4,030 | 99.98% | 1 node holds `1065353216` = the bit pattern of `1.0f` |

**The `not shipped` names are not decode failures.** They are the same
seasonal/event content gap `README.md` records for the texture audit: 1,039
distinct textures are named, 668 ship (64.3%); 773 distinct meshes are named,
708 ship (91.6%); 127 nested `.egp` are named, 123 ship (96.9%). Those figures
match `EFFECTS-SCOPE.md` §5's independently-derived "1,043 named, 674 ship" to
within the handful of names the legacy versions added — which is itself a
cross-check, since that count came from a different code path.

**`MESH.m_szMeshFile1/2` at 13.8% is the editor's default, not a bad offset.**
`EFFECTS-SCOPE.md` §5 already flagged that 6,950 nodes name `Dolphin2.x` /
`Dolphin3.x` in the morph slots and that neither file exists anywhere in `Ran/`.
Those slots only mean anything under `USEBLENDMESH`. **MEASURED, restricted to
the nodes where the flag is actually set: 1,810 of 1,908 slots (94.86%) resolve
to a shipped mesh** — in line with every other name field. Judging the slots
unconditionally measures the editor, not the decoder.

### (d) Every float and every string in the corpus

| | |
|---|---:|
| float values read | **1,267,239** |
| …non-finite | 132 (0.010%) |
| …of which in a field the runtime never reads *and* never initialises | **128** |
| float values read, excluding those three dead fields | 1,215,645 |
| …non-finite | **4** — all four in one file, `xv_ele_b.egp` |
| …largest magnitude | 1.34e8 (`eff_ambient_aura_005.egp`, `m_vWindDir`) |
| `char[256]` fields read | **63,705** |
| …not NUL-terminated | **0** |
| …containing a non-printable byte before the NUL | **0** |
| …carrying live bytes *after* the NUL | 15,588 (24.47%) |

That last row is the quantified version of `EFFECTS-SCOPE.md`'s warning: **a
quarter of all name fields have a dead tail after the terminator**, left by a
shorter string overwriting a longer one. A regex over the property body harvests
those tails as filenames. Reading the declared offset and stopping at the NUL
removes all of it.

With the three dead MESH fields excluded, **4 non-finite floats in 1.2 million**
is the number that says the offsets are right. They are two `D3DXCOLOR`
channels in two SEQUENCE nodes of `xv_ele_b.egp`, a file that walks to EOF
cleanly and whose texture resolves — an authored NaN, the same artefact class as
the uninitialised animation key and the terrain NaN UVs already in `README.md`.

### (e) Positive control

A check that cannot fail proves nothing, so the same decode was rerun with the
property body deliberately shifted — the exact 4-byte error the whole `+4/−4`
size rule exists to prevent:

| | name fields that still resolve |
|---|---:|
| correct offsets | 94–99.9% per field |
| **shifted +4** | **118 / 44,483 = 0.27%** |
| **shifted −4** | **0 / 27,434 = 0.00%** |

---

## 5. What this closed: 4 of the 5 unexplained files

`EFFECTS-SCOPE.md` §8.3 listed as undetermined: *"What the 4 mid-file desyncs
are (`kaifirecrack`, `sk_dfly`, `sundo`, `tegm_a`) and the one file with 2,220
trailing bytes (`mob_bb_bomb`)."*

**MEASURED.** All 7 nodes in the corpus whose declared size disagrees with the
measured struct size live in exactly those files, and every one is 4 bytes
short:

| file | node | declared | measured |
|---|---|---:|---:|
| `mob_bb_bomb.egp` | MESH 0x0105 | 1300 | 1304 |
| `sk_dfly.egp` | PARTICLESYS 0x0105, SEQUENCE 0x0102, POINTLIGHT 0x0101 | 864 / 532 / 140 | 868 / 536 / 144 |
| `sundo.egp` | PARTICLESYS 0x0105, SEQUENCE 0x0102 | 864 / 532 | 868 / 536 |
| `tegm_a.egp` | SEQUENCE 0x0102 | 532 | 536 |

That is a falsifiable prediction — if the measured size is the truth and the
declared one is the bug, re-walking with the measured size must recover those
files — so `effect-props.js` runs it rather than arguing it:

> **MEASURED: 4,268 files walk to the exact last byte using the DECLARED size;
> 4,272 using the MEASURED one.** `mob_bb_bomb`, `sk_dfly`, `sundo` and `tegm_a`
> all flip from failure to a byte-exact walk. **No file regresses.**

So the remaining unparseable set is **22 files (0.51%)**: the 20 truncated
`*_summon.egp`, `very_strong03_red.egp` (not an `.egp` at all) and
`kaifirecrack.egp` — whose single GROUND node holds a flag word of `0x41a26667`
and a `m_nBlend` of `1065353216`, i.e. float bit patterns in integer fields.
That one is genuinely corrupt data, not a layout question.

`sk_dfly.egp` is why POINTLIGHT is in the table in §2: with the six types
measured it was the only node left in that file whose size was still taken on
trust, and it was 4 bytes short like the others.

**INFERRED** as to cause: these four files were written by a build whose
`GetSizeBase()` did not count `m_bMoveObj` — the same 4-byte omission that makes
`DECAL` a special case today. The engine would not notice, because on a version
it recognises it parses structurally and ignores the declared size entirely.
**This is the third independent confirmation of `README.md`'s rule that
`[version][size]` sizes are only trustworthy on versions the engine does not
know.**

---

## 6. What an importer needs to know

### Blend modes — MEASURED distribution, domain read from the render switch

`m_nBlend` means **two different things** and mixing them up is a silent visual
bug across half the effect set:

**SEQUENCE / GROUND / BLURSYS** (`DxEffectSequence.cpp:1010`,
`DxEffectGround.cpp:689`, `DxEffectBlurSys.cpp:587`) — the switch has cases
1, 2, 3 and 5 only; anything else takes the default alpha blend.

| value | meaning | SEQUENCE | GROUND | BLURSYS |
|---|---|---:|---:|---:|
| 1 | additive (`DESTBLEND=ONE`) | 7,685 | 3,236 | 1,805 |
| 2 | additive + `MODULATE2X` | 675 | 272 | 153 |
| 3 | additive + `MODULATE4X` | 171 | 44 | 6 |
| 4 | *no case* → default alpha blend | 185 | 466 | 192 |
| 5 | opaque (`ZWRITE` on, blending off) | 147 | 11 | 0 |

**MESH / PARTICLESYS** (`DxEffectMesh.cpp:545`,
`DxEffectParticleSysDraw.cpp:144`) — a completely different, fully populated
0..6 enum over `D3DBLENDOP`:

| value | meaning | MESH | PARTICLESYS |
|---|---|---:|---:|
| 0 | ADD | 6,801 | 3,793 |
| 1 | SUBTRACT | 326 | 258 |
| 2 | REVSUBTRACT | 29 | 37 |
| 3 | MIN | 0 | 23 |
| 4 | MAX | 134 | 34 |
| 5 | classic alpha (`SRCALPHA`/`INVSRCALPHA`) | 888 | 602 |
| 6 | opaque + alpha test | 421 | 116 |

`m_nPower` (both) is `MODULATE` / `MODULATE2X` / `MODULATE4X` = 0/1/2:
MESH 6,958 / 1,159 / 98, PARTICLESYS 2,818 / 1,651 / 107.
`MESH.m_nRotationType` is 0..6 (0: 4,609 · 5: 2,930 · 4: 574 · 6: 464 · 1: 18 ·
2: 4), and `PARTICLESYS.m_iCenterPoint` is 0/1/2 (109 / 4,473 / 281).
**MEASURED: no node anywhere holds a value outside these domains**, except the
one corrupt `kaifirecrack` GROUND node.

### Flag bits actually used — MEASURED

`EFFECTS-SCOPE.md` §5 measured this for SEQUENCE and MESH; the other four are
new. The masks are extracted from the headers as literals, with comments
stripped first — because this project's most-repeated bug is a constant read
out of a commented-out line.

- **SEQUENCE** — `USEBILLBOARD` 8,199 · `USETEXROTATE` 899 · `USEANI` 743 ·
  `USEDIRECTION` 435 · `USEBILLBOARDUP` 229 · `USELIGHTING` 130 ·
  `USESEQUENCELOOP` 127 · `NOT_WORLD_RS` 17 · `USERANDOMLIFE` 17 ·
  `USEDYNAMICSCALE` 13 · `USEDEFAULTPOS` 10 · `USEGOTOCENTER` 6 ·
  `USECOLLISION` 5 · `USEROTATE` 2
- **MESH** — `USECULLNONE` 4,807 · `USEOTHERTEX` 3,825 · `USEROTATE` 3,533 ·
  `USESCALE` 2,697 · `USESIZEXYZ` 2,312 · `USEBLENDMESH` 954 · `USESEQUENCE`
  949 · `USENORMAL2` 486 · `USETEXMOVE` 481 · `USEBLUR` 464 · `USEHEIGHT_MESH`
  391 · `USEDIRECTION` 357 · `USEGROUNDTEX` 256 · `USESEQUENCELOOP` 97 ·
  `USEGOTOCENTER` 3
- **PARTICLESYS** — `USETEXTURE` 4,231 · `USERANGE` 2,677 · `USEBEZIER_P` 2,253 ·
  `USEDIRECTION` 1,627 · `USETEXSCALE` 1,606 · `USETEXROTATE` 1,202 · `USEMESH`
  594 · `USEPARENTROTATEWORLD` 523 · `USEPARENTMOVE` 499 · `USECENTER` 485 ·
  `USEGOTOCENTER` 412 · `USEHEIGHTNO` 370 · `USESEQUENCE` 313 · `USENEWEFF` 251 ·
  `USEATTRACTION` 228 · `USEBILLBOARDALL` 216 · `USEBILLBOARDUP` 181 ·
  `USECOLLISION` 168 · `USEGROUND` 140 · `USERANDOMPLAY` 126 · `USEGROUNDTEX`
  125 · `USENORMAL2` 111 · `USERANGEHIGH` 108 · `USEPARENTROTATE` 94 ·
  `USEROTATEL` 66 · `USESPHERE` 62 · `USESEQUENCELOOP` 36 · `USERANDOMDIRECT`
  35 · `USEFROMTO` 17 · `USENEWEFF_END_STOP` 14 · `USENEWEFF_END` 4 ·
  `USEGROUNDATTACH` 1
- **MOVEROTATE** — `USERANDROTATE` 331 · `USEGOTOCENTER` 279 (3,992 of 4,520 nodes set
  no flag at all)
- **GROUND** — `USEROTATE` 810 · `USEPICKING` 453 · `USEHEIGHT` 304 · `USEANI`
  167 · `USENORMAL` 34 · `USESEQUENCELOOP` 11
- **BLURSYS** — `USEABSOLUTE` 2,078 · `NOUSE_BEZIER` 1,012 · `USE_LOOP_RESET` 22
  (`USEREFRACT` never appears)

> JS trap, found and fixed here: `USENEWEFF_END` is `0x80000000` and JavaScript's
> `&` returns a **signed** 32-bit result, so `(dw & v) === v` is false for that
> bit however it is set. The first run reported the flag as never used. Both
> sides now go through `>>> 0`.

**Two flag bits are used by the data and defined by nothing** — because the
`#define` was commented out:

| type | bit | nodes | the commented-out define |
|---|---|---:|---|
| BLURSYS | `0x00000004` | 86 | `//#define USEPARENTEND` (`DxEffectBlurSys.h:15`) |
| MESH | `0x00000008` | 74 | `//#define USEMATRIAL` (`DxEffectMesh.h:12`) |

This is `README.md`'s commented-out-constant trap running in the opposite
direction: the *data* still carries a feature the *source* has retired. A live
`#define` read by eye would have made these look like known flags; the extractor
strips comments, so they show up as unknown, which is the honest answer. One
further bit is unaccounted for: `SEQUENCE 0x02000000` on a single node in
`2011_msmap_eff10.egp`.

### Fields to ignore — MEASURED (grepped over all of SOURCE)

Never read by any code in the engine, so an importer must not try to honour
them. The first three are also never initialised by the `PROPERTY` constructor,
which means **the bytes on disk are whatever was in memory**:

| field | bytes | state |
|---|---:|---|
| `MESH.m_fMaterial[4]`, `m_fMaterialRatio0`, `m_fMaterialRatio1` | 24 | uninitialised — 128 of the corpus's 132 non-finite floats and every value above 1e7 are here |
| `SEQUENCE.m_bTexRotateUse`, `m_bGIsColliding` | 8 | uninitialised — only 73.2% of nodes have both holding 0 or 1; the live equivalents are the flag bits `USETEXROTATE` and `USECOLLISION` |
| `MESH.m_nTexRotateType` | 4 | copied to the runtime object, then never used; ctor sets 0 |
| `PARTICLESYS.m_dwDummy` | 4 | ctor sets 0 |

Reading these as meaningful is the one way a *correct* layout still produces
garbage, so they are listed in `effect-props.js` as `DEAD_UNINIT` / `DEAD` and
excluded from the validity headline rather than quietly passed.

### Curve conventions — MEASURED

The `Start / Mid1 / Mid2 / End` quadruples paired with `Rate1 / Rate2` are a
4-point piecewise curve over the node's lifetime, with the two rates as
percentage breakpoints (defaults 25 and 75). Alpha is nominally 0..1 and
overwhelmingly is; the authored tail is small and deliberate — 652 SEQUENCE
nodes hold exactly 1.2, a few dozen hold 4, 5, 50 or 100. **Every out-of-band
value in the corpus is a round number**, which is the point: a float read at the
wrong offset does not come out as 1.2 and 50.

---

## 7. What I could not determine

1. **Whether the four short-size files are broken in the live client.** The
   recovery in §5 is measured against my walker, not against the shipped engine.
   The engine ignores the declared size on a known version, so it almost
   certainly loads them fine — but that is **INFERRED** from the source, not
   tested.
2. **The `SEQUENCE 0x02000000` flag bit** on one node of `2011_msmap_eff10.egp`.
   No `#define` in any of the six headers has that value, live or commented out.
3. **Semantics, as opposed to layout.** I decoded and validated the fields; I
   did not verify what most of them *do*. `m_fFlarePos`, `m_nBlurObject`,
   `m_vHeight`, `m_fMorphRoopNum`, `m_iCenterPoint`'s three modes and
   PARTICLESYS's `USEBEZIER_P` path are all named and typed here but their
   behaviour is only as good as reading the render code, which I did only for
   the blend/power switches. Nothing was rendered.
4. **The remaining seven types.** `WAVE`, `LIGHTNING`, `DECAL`, `SKINMESH`,
   `CAMERA`, `MOVETARGET` are still unmeasured (POINTLIGHT is now done as a
   side-effect). They are 4.5% of nodes. The generator takes a header per line,
   so adding them is mechanical — the reason to stop here is that the six were
   the scope, not that the rest are hard.
5. **Legacy struct field-level validation.** The legacy versions are included in
   the coverage and size checks and their fields are decoded, but the per-field
   tables in §3 are the current version of each type. Legacy versions are 4,120
   nodes and their offsets are in `layout.json`.
6. **Sound.** Every node still carries an `SMovSound` that `effect-egp.js` reads
   and this document does not touch. `EFFECTS-SCOPE.md` §8.7 remains open.
7. **`m_dwFlag`'s meaning for the 3,992 MOVEROTATE nodes that set no bit at
   all.** They decode cleanly; the flag simply is not used by most content.

## 8. Test coverage

`test.js` is owned by another agent and was not modified. When it is free, the
numbers worth pinning are: **`EFFPROP_PREFIX` 76 and `EFFPROP_PREFIX_AFFINE`
112**; the six current-version body sizes **536 / 1304 / 1128 / 104 / 476 /
396**; **33,624 of 33,631 nodes declare exactly the measured size**; **33,037 of
33,037 six-type nodes have a measured layout**; **4,272 files walk to exact EOF
with the measured size versus 4,268 with the declared one**; and the positive
control — **shifting the property start by ±4 drops name resolution from >94% to
0.27% / 0.00%**. That last one is the check that makes the rest mean something,
and it is the cheapest to run.
