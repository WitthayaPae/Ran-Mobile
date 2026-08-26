# MOBILE — RAN EP9 mobile client work

Option A workspace: a separate mobile client against **unmodified** servers.
See `../plan.md` for the full plan.

**`SOURCE/` is frozen.** Nothing here modifies it — the layout probe only
`#include`s its headers. Verify at any time with:

```
cd SOURCE && git status --porcelain     # empty = untouched
```

That check is the real enforcement mechanism for the §2 scope boundary. If it
ever prints something, mobile work has leaked into the PC codebase.

---

## Layout

```
MOBILE/
├── client/                 Phase 1 permanent client core
│   ├── connection.js       socket, framing, LZO/batch unwrap, reconnect+backoff
│   ├── messages.js         builders/parsers, all offsets from the probe
│   ├── session.js          login -> agent -> field state machine
│   ├── world.js            nearby-entity model (enter/move/leave)
│   ├── census.js           measures what the server actually sends
│   ├── demo.js             end-to-end runner
│   └── test.js             offline tests incl. reconnect vs a local server
├── tools/rcc-extract/      Phase 2 asset pipeline
│   ├── rcc.js              zip + CCrypt XOR reader
│   ├── gamecrypt.js        AES-256-ECB layer under glogic/
│   ├── dds.js              DXT1/2/3/5 + uncompressed -> RGBA8
│   ├── tga.js              TGA (RLE, colour-mapped, grey) -> RGBA8
│   ├── png.js              PNG encoder (zlib only, no deps)
│   ├── xfile.js            DirectX .x reader (bin/txt/MSZip)
│   ├── xmesh.js            .x -> vertices/indices/UVs/normals/skin
│   ├── xanim.js            AnimContainer .bin -> bone tracks/keyframes
│   ├── wld.js              .wld container + WLD cipher + section marks
│   ├── navmesh.js          navigation mesh -> vertices/cells/links
│   ├── extract-navmesh.js  all maps -> .navmesh binaries
│   ├── fvf.js              D3D9 FVF -> vertex stride + field offsets
│   ├── octree.js           DxAABBOctree -> per-node geometry
│   ├── staticmesh.js       .wld0 container + DxSingleTexMesh + effects
│   ├── extract-terrain.js  all maps -> .terrain + .textures.json (+ OBJ)
│   ├── preview-terrain.js  one map -> PNG, to look at it
│   ├── animkeys.js         packed keyframe structs -> values
│   ├── animinfo.js         .cfg SANIMCONINFO + .chf/.abf char containers
│   ├── extract-animtypes.js clip -> EMANI_MAINTYPE/SUBTYPE map (JSON)
│   ├── verify-animtypes.js cross-checks that map against the .ranim corpus
│   ├── extract-meshes.js   all .x -> .rmesh (geometry + skeleton + skin)
│   ├── extract-anim.js     all AnimContainer .bin -> .ranim
│   ├── preview-mesh.js     one mesh -> PNG, to look at it
│   ├── mapobj.js           DxOctree scene graph -> placed objects
│   ├── extract-mapobj.js   all .wld -> .rmapobj (placement + geometry)
│   ├── build-maps.js       per-map .map.json manifest + cross-check
│   ├── itemdata.js         item.isf -> items, icon sheets, worn pieces
│   ├── audit-assets.js     asset references vs what actually ships
│   ├── stage-unity.js      stage a map + its textures into unity/RanMobile
├── unity/                  Unity importers for the extracted binaries
│   └── com.ran.mobile.assets/
│       ├── Runtime/        format readers + imported asset types
│       └── Editor/         ScriptedImporters for .terrain / .navmesh
│   ├── gen-bytecrypt.js    extracts the 43 crypt tables from SOURCE
│   ├── bytecrypt.js        per-format byte substitution
│   ├── bytecrypt.json      generated
│   ├── CRYPT-MAP.md        which table applies to which format
│   ├── extract.js          CLI: --list / --out / --cat
│   ├── census.js           file-type inventory
│   ├── dds-census.js       texture format inventory
│   ├── convert-textures.js all textures -> PNG
│   └── test.js             145 checks against the real Ran/ tree
├── tools/layout-probe/     MSVC probe -> authoritative struct layouts
│   ├── probe.cpp
│   ├── build.cmd
│   ├── gen-enums.js        names -> DUMP_ENUM(...)    [codegen]
│   ├── gen-structs.js      names -> DUMP_STRUCT/...   [codegen]
│   ├── build-structs.js    compile/exclude/retry loop [codegen]
│   ├── enums.gen.inc       generated
│   ├── structs.gen.inc     generated
│   ├── exclude.txt         generated from compiler diagnostics
│   └── layout.json         generated; consumed by the spike
└── spike/                  Phase 0 throwaway protocol client (Node.js)
    ├── protocol.js         framing, garbage layer, struct pack/unpack
    ├── tea.js              XXTEA port of minTea.cpp
    ├── lzo.js              LZO1X-1 decompressor
    ├── spike.js            connection + login flow
    └── selftest.js         offline tests, no server needed
```

---

## Never resolve a constant by reading the header

Three separate bugs this project hit were all the same bug: a constant defined
**twice** in `s_NetGlobal.h`, once live and once in a commented-out or
`#if`-disabled block, resolved by eye to the wrong one. Every failure was silent.

| Constant | Wrong (read by hand) | Live (compiler) |
|---|---|---|
| `NET_MSG_LOBBY` | 2005 | **1942** |
| `MAX_ONESERVERCHAR_NUM` | 16 | **4** |
| `NET_MSG_GCTRL` | 2892 | **3003** |

So `tools/layout-probe/gen-enums.js` extracts every enum member *name* from the
header and emits `DUMP_ENUM(...)` lines; the probe compiles them and the
**compiler** computes the values. 1,825 names across three headers, zero hand
arithmetic. Regenerate after any header change:

```
cd MOBILE/tools/layout-probe
node gen-enums.js > enums.gen.inc
build.cmd
```

The script strips comments before scanning, so a commented-out definition cannot
leak in. It never evaluates arithmetic — that is the whole point. It also only
reads enums at **namespace scope**: an `enum { VERSION = 0x0200 }` declared
inside a struct names constants that are not addressable as bare identifiers, so
emitting `DUMP_ENUM(VERSION)` for it simply fails to compile — and unlike the
struct codegen there is no self-healing exclusion loop here to absorb that.

Consequence: `spike.js` can name any message ID it sees. Two of the flows above
were only understood because an "UNKNOWN(2376)" turned into
`NET_MSG_LOBBY_CHAR_PUTON_EX`.

### The same treatment for struct layouts (Phase 1 codegen)

`plan.md` Phase 1 opens with *"codegen packet structs — do not hand-transcribe
2,700+ structs"*. That is now done for the message headers:

```
node gen-structs.js > structs.gen.inc   # names only
node build-structs.js                   # compile, self-heal, emit layout.json
```

**521 structs / 1959 members**, every size and offset computed by MSVC. Covers
`s_NetGlobal.h`, the `GLMSG` message headers and the payload types they carry,
plus the engine headers whose structs are blitted with `ReadBuffer` — animation
keys, animation clip info, character sound, skin-char containers, navmesh and
frame-mesh types; add more headers to the `HEADERS` list in `gen-structs.js`.
Base classes are followed, so inherited members (e.g. `dwGlobID` on
`SNETCROW_MOVETO`) are measured too.

One member-extraction bug is worth knowing about because it was silent:
**declarators after a comma were dropped**. `DWORD m_dwETime, m_dwETimeOrig;`
yielded only the last name, so `m_dwETime` had no measured offset at all and
would have had to be hand-computed — precisely what this exists to prevent. It
now splits on commas, but only when the declaration contains no angle brackets,
or `std::map<DWORD,float> m` would produce a phantom member named `DWORD`.

`build-structs.js` is a self-healing loop: it compiles, maps any error line back
to the `DUMP_MEMBER` that caused it, appends that entry to `exclude.txt`,
regenerates and retries. So the exclusion list is derived from the compiler too,
not hand-maintained — it converges in one or two rounds, and the exclusions are
members of nested or non-standard-layout types (`GLMSG::SINFO`, the navmesh
classes) that are not addressable at namespace scope.

Getting the G-Logic headers to compile outside the game needed the real MFC +
DirectX prelude from `Lib_Client/StdAfx.h` (`afx.h`, `afxcoll.h`, `dxstdafx.h`
for `LPDIRECT3DDEVICEQ` and `CStringArray`) plus five more engine include
directories. Without those it fails with 66 cascading errors from one missing
typedef.

Two payoffs beyond convenience:

- The hand-written `MIRROR_*` structs are now **checked against the real ones**.
  `selftest.js` asserts `GLMSG::SNETPC_GOTO` and `SNETLOBBY_CHARJOIN` match the
  mirrors exactly — so mirror drift fails offline instead of corrupting packets.
- `sizeof(SNETLOBBY_CHARJOIN)` is **1030**, exactly the spawn packet size seen on
  the wire. Independent confirmation that the layout model matches reality.

`protocol.js` resolves any struct by name via `getStruct()`, curated entries
first then the codegen table, so all 490 are usable without transcription.

## Why the layout probe exists

The mobile client must reproduce MSVC struct layout and enum values exactly.
Hand-computing them does not work, and it fails *silently*.

Concrete example this already caught. Grepping `s_NetGlobal.h` for
`NET_MSG_LOBBY` finds:

```c
#define NET_MSG_LOBBY   (NET_MSG_BASE + 1013)    // -> 2005
```

That line is **inside a `/* */` comment block**. The live definition is further
down (the "2007-04-11" block):

```c
#define NET_MSG_LOBBY   (NET_MSG_BASE + 950)     // -> 1942
```

So `THAI_NET_MSG_LOGIN` is **2082**, not 2145. A hand-written client would have
sent a valid-looking packet with the wrong message type and gotten no useful
error back. The probe makes the compiler answer this instead of a human.

Two packing regimes also coexist, which is easy to get backwards:

| Header | Packing | Example |
|---|---|---|
| `s_NetGlobal.h` | **no** `#pragma pack` — natural alignment | `THAI_NET_LOGIN_DATA` = 56 bytes (54 padded) |
| `GLContrlPcMsg.h` | `#pragma pack(1)` (line 356) | `SNETPC_GOTO` = 36 bytes, no padding |

### Running it

```
MOBILE\tools\layout-probe\build.cmd
```

Requires VS 2022 (path is set at the top of `build.cmd`). Builds **x86**, to
match the Win32 server/client build. Re-run whenever the headers change.

The `GLMSG` headers **are** included now — see the MFC/DirectX prelude note
above. The older `MIRROR_*` structs in `probe.cpp` are kept deliberately:
`selftest.js` asserts each still matches the real struct, so a mirror that
drifts from upstream fails offline rather than corrupting packets.

---

## Asset pipeline (Phase 2)

```
cd MOBILE/tools/rcc-extract
node test.js                          # 145 checks against the real Ran/ tree
node extract.js --list                # inventory
node extract.js --out ../../build/assets
node extract.js --cat Gui gameextext.xml
```

Sources from **`Ran/`**, the deploy tree — not `CLIENT/`, which is the dev tree
where roughly half of `data/` never ships.

Full extraction: **36,418 files, 2.98 GB, 33.5 s**, zero failures.

| Archive | Entries | Uncompressed |
|---|---:|---:|
| Map | 448 | 2019.7M |
| Animation | 12,660 | 658.0M |
| SkinObject | 16,570 | 96.6M |
| GLogic | 376 | 79.6M |
| Gui | 108 | 59.6M |
| Level / NpcTalk / Quest / Effect / EffectChar | 6,256 | 68.3M |

### Two encryption layers, and what each really is

**Layer 1 — CCrypt XOR** (`Lib_Engine/Crypt.cpp`). Every zip entry is passed
through one byte pass: `b = ((b - 0x06) & 0xff) ^ 0xDF` to decrypt. `EN` and
`EN2` are declared as 9183/729 but the code operates on `BYTE`, so both truncate
to 8 bits — an implementation that keeps them as ints produces garbage.

**Layer 2 — AES-256-ECB** on most `data/glogic/` files, under a 4-byte version
prefix (`08`). This one is worth reading carefully, because the source supports
two wrong conclusions:

- The class is `CRijndael` with a 32-byte key table, which reads as
  **Rijndael-256** — a 256-bit *block* cipher node:crypto cannot do. It isn't:
  `Initialize(...)` is called with five arguments (`StringMemory.cpp:98`), so
  the fifth is `keylength`, not `blockSize`, and the block stays 16 bytes.
- `sm_chain0` is passed as an IV and `Decrypt` has a CBC branch, which reads as
  **CBC**. Also not: `iMode` defaults to `ECB` (`Rijndael.h:103`).

Both were settled by building a harness linked against the real `CRijndael`
rather than by reading further. It decrypts 32 zero bytes to
`996d7bae887a7a15daed63f31acfb503` **twice** — a 16-byte repeat is only possible
with a 128-bit block. CBC then still diverged from block 2 onward, and the delta
was exactly the previous ciphertext block, i.e. no chaining. `test.js` pins that
vector and the derived key.

The key is stored XOR-obfuscated so it is not a plaintext string in the shipped
binary; `Initialize` de-obfuscates it and then applies a `^0x21, +0x31` pass.

### What actually ships

```
node census.js              # every file type in Ran/, archived and loose
node census.js --where dds  # which directories hold a type
node dds-census.js --sizes  # texture formats and dimensions
```

**60,277 files, 9.02 GB, 107 distinct extensions.** (Larger than `Ran/`'s 7.2 GB
on disk because this counts archive *contents*, uncompressed — which is what
matters for conversion planning.)

| Class | Files | Size | Notes |
|---|---:|---:|---|
| Terrain `.wld` / `.wld0` / `.wld2` | 723 | 4.44 G | **counts duplicates** — deduplicated it is 248 files / 1.93 G. `RanMapZipTemp` holds 2.41 G of stale per-pid copies |
| Textures `.dds` | 15,445 | 2.64 G | see below |
| Animation `.bin` | 6,537 | 653 M | keyframe curves, always loose |
| Meshes `.x` | 4,567 | 619 M | DirectX retained-mode |
| Audio `.wav` / `.ogg` | 863 | 254 M | |
| SkinObject `.cps` | 12,879 | 28 M | metadata |
| Effects `.egp` | 4,294 | 26 M | |

### Textures

| Format | Files | Size |
|---|---:|---:|
| DXT1 | 7,919 | 816 M |
| DXT3 | 3,304 | 705 M |
| DXT5 | 2,341 | 520 M |
| DXT2 | 155 | 16 M |
| Uncompressed RGB16/24/32(A) | 1,479 | 490 M |

Encouraging for mobile: **87% is already block-compressed** (transcodes to
ETC2/ASTC without a decode-recompress round trip), only 30 textures exceed
1024², only 76 are non-power-of-two, and 41 are cubemap/volume. Dimensions are
dominated by 256² (6,334) and 512² (4,956).

**Do not trust the file extension.** 247 files named `.dds` are not DDS —
134 TGA, 109 PNG, 2 PSD, 1 empty, 1 unrecognised. The engine loads through
D3DX, which sniffs the header, so the name was never load-bearing. `dds-census.js`
identifies by content and any converter must do the same, or it fails on 1.6%
of the texture set.

### Texture conversion

```
node convert-textures.js --dry           # measure without writing
node convert-textures.js --out DIR
```

**16,232 textures, 0 failures, 2.81 GB → 1.70 GB** in 159 s.

| Outcome | Count |
|---|---:|
| Decoded and re-encoded (DDS 15,263 + TGA 355) | 15,618 |
| Already portable (PNG) | 540 |
| Passed through (BMP 34, JPEG 36, PSD 2, other 2) | 74 |

`dds.js`, `tga.js` and `png.js` are dependency-free — the block formats are
simple, and a native module would be one more thing to build per platform. PNG
is an *intermediate*, not a shipping format: the engine importer re-encodes to
ETC2/ASTC per platform. Only the top mip is decoded, since mobile pipelines
regenerate mip chains anyway and re-deriving them after transcode beats
carrying DXT-compressed chains across.

Correctness was checked by looking at the output, not just by it not throwing —
a DXT1 atlas and an uncompressed RGB32 character texture both render correctly.
One TGA decoded to a blank white image, which turned out to be genuine: it is an
effect mask in `textures/effect/` with white RGB and a varying alpha channel.

### Meshes (`.x`) — container layer

`xfile.js` opens all three DirectX `.x` variants that ship and tokenizes the
binary form. **4,547 of 4,567 files (99.6%), 4.69M tokens.**

| Variant | Files |
|---|---:|
| `xof 0303bin ` binary | 3,225 |
| `xof 0303bzip` MSZip-compressed binary | 935 |
| `xof 0303txt ` text | 387 |

**MSZip is not zlib.** Layout is `u32 total`, then blocks of
`u16 uncompressed, u16 compressed, 'CK', raw deflate` — and each block is
deflated against the previous 32 KB of *output* as history. Two traps:

- The compressed size **includes** the 2-byte `CK`.
- zlib's `dictionary` option is the obvious way to supply the history and it
  fails on some blocks with a misleading `invalid stored block lengths`, even
  where the history is provably correct. The reliable approach is to prepend the
  history to the stream as an uncompressed **stored deflate block** and discard
  it from the output — the back-references then resolve against real stream data
  with no dictionary API involved.

The 20 files that do not open:

- **19 are an encrypted container**, not `.x`: 12-byte header of
  `u32 0x00000100` + `u32 payloadSize` + `u32 0`. That matches
  `ENCRY_VER = 0x100` / `ENCRY_HEADER_SIZE = 12` in `Lib_Engine/Crypt.h` — but
  those constants are **declared and never referenced anywhere in SOURCE**, so
  the shipped code cannot read them either. Treat as unreadable until a reader
  turns up.
- **1 is `desktop.x`**, a Windows `desktop.ini` (`[LocalizedFileNames]`) renamed.

A further **14 have damaged MSZip tails** — the stream stops being
self-consistent partway through (every preceding block declares and produces
exactly 32768 bytes, so the history is right, and no offset within ±8 recovers
it). `open(buf, {tolerant: true})` returns the part that decodes and sets
`truncated`, which beats losing the whole mesh.

### Meshes — geometry

`xmesh.js` walks the token stream into a node tree and pulls out geometry.
Binary and text share one node model, so nothing downstream branches on the
container format. Across the whole corpus:

| | |
|---|---:|
| Files parsed | 4,547 / 4,567 |
| Mesh nodes | 26,459 |
| ...of which empty placeholders | 16,754 |
| Vertices | 8,598,113 |
| Triangles | 8,917,399 |
| With UVs | 7,713 |
| With normals | 25,627 |
| Skinned (SkinWeights) | 5,344 |

Two things that look like bugs and are not: **63% of mesh nodes are empty** —
files like `0001.x` are skeleton/animation carriers whose Mesh nodes are
placeholders with zero vertices — and n-gon faces are **fan-triangulated**
rather than dropped, so the triangle count exceeds the face count.

### Byte-crypt: the third encryption layer

Every remaining encrypted data format — `.lev`, `.egp`, `.cfg`, `.wld`, `.qst`,
`.ntk`, `.crowsale`, animation `.bin` — uses the same mechanism: a per-format
**256-byte substitution table**.

```
encode: b -> ARRAY[b]
decode: b -> INVERSE[b]     (BYTECRYPT::byte_decode, ByteCrypt.cpp)
```

`gen-bytecrypt.js` parses all **43 tables** out of `ByteCryptDef*.h` and reads
the enum→array mapping from `InitArray()` rather than inferring it from names —
several do not match (`EMBYTECRYPT_LEVEL` uses `ARRAY_LEVEL_VAR1`). Every table
is verified to be a permutation of 0..255, so decode is lossless; a single
mistyped byte would silently corrupt 1/256 of every file, which is precisely
why these are extracted and not transcribed.

Encrypted data files also share a **`CSerialFile` header**: 128 bytes of type
string then a u32 version, with the encoded region starting at byte 132.
Animation `.bin` files are `AnimContainer` and ship in six versions:

| Version | Files |
|---|---:|
| 0x0104 | 2,166 |
| 0x0100 | 1,605 |
| 0x0103 | 1,498 |
| 0x0101 | 1,130 |
| 0x0102 | 121 |
| 0x0200 | 8 |

**The table name does not follow the file extension**, and most `SetEncodeType`
call sites in the tree are **commented out** — a format having a table defined
does not mean it is used. `CRYPT-MAP.md` records the mapping derived from the
live call sites, with what is confirmed against real data separated from what is
only identified from source.

The headline results:

- **`.lev` and `.wld` are not byte-encoded at all.** Every `SetEncodeType` for
  them is commented out. My earlier attempt to decode a `.lev` with the `LEVEL`
  tables failed for that reason — there was nothing to decode.
- **Animation `.bin` is version-gated** at `dwVersion >= 0x0200`
  (`SAnimationSaveLoad.cpp:401`). In shipped data that is **8 files encoded with
  `EMBYTECRYPT_BIN2`, and 6,520 not encoded at all**. Proven both ways: a
  v0x0104 body already shows bone names raw and applying a table destroys them;
  a v0x0200 body decodes under BIN2 to `Bip01_Spine1`.
- **`.egp` is version-gated too**, at the same 0x0200 threshold: 1,165 of 4,294
  files are encoded with `EMBYTECRYPT_EGP`, the rest are plaintext. Its encoded
  region starts at **136**, not 132, because it writes an extra `DWORD VERSION`
  before `SetEncodeType`.
- **The gate pattern is the norm.** All three formats checked encode only above
  version 0x0200. Read the version and branch; never assume a whole format is
  encrypted.

### Animation

`xanim.js` reads `AnimContainer` `.bin` files. **All 6,498 in `Animation.rcc`
parse with zero failures** across all six shipped versions:

| | |
|---|---:|
| Files | 6,498 |
| Bone tracks | 397,795 |
| Keyframes | 15,452,358 |
| Distinct versions | 6 (0x0100–0x0200) |

Bone names come out as clean 3ds Max naming — `Bip01`, `Bone001…`, `Dummy01` —
with zero non-ASCII, which is the real check that strides and field order are
right. A wrong stride still "parses"; it just produces garbage names and
shredded curves.

**`SMatrixKey` is 80 bytes, not 68.** It holds a `D3DXMATRIXA16`, which forces
16-byte alignment, so the matrix sits at offset 16 with 12 bytes of padding
before it. All five key strides are taken from the layout probe rather than
computed:

| Struct | Stride |
|---|---:|
| `SPositionKey` | 16 |
| `SScaleKey` | 16 |
| `SRotateKey` | 20 |
| `SQuatPosKey` | 36 |
| `SMatrixKey` | **80** |

Two structural traps beyond that:

- **Field order changes per version.** 0x0100/0x0101 put the keyframe arrays
  first and the bone name last; 0x0102/0x0103 put the bone name *first* and
  reorder the arrays.
- **`dwGarbageValue` DWORDs are interleaved** between arrays in 0x0102/0x0103,
  and between the two track lists in the 0x0103 container. They carry nothing
  but they must be read, or everything after them shifts.

Container and per-animation versions are **independent** — the container version
lives in the `CSerialFile` header at byte 128, and each animation record starts
with its own version DWORD.

### Terrain `.wld` and the navigation mesh

```
node extract-navmesh.js --list        # inventory
node extract-navmesh.js --out DIR     # one .navmesh binary per map
```

**132 maps, 121 with a navmesh, 834,870 vertices, 547,788 cells, 1 failure**,
22.1 MB written in 1.7 s. Scope is deliberately the navmesh only — terrain
*geometry* lives in the `.wld0` sidecar (`DxStaticMesh`) and needs nine
per-version parsers, which is its own job.

That split is affordable because **the navmesh payload is byte-identical across
all nine `.wld` versions**. Every loader (`LoadFile_VER108`…`VER200`) reads the
same `BOOL bExist` then the same `NavigationMesh::LoadFile`; only its *position*
moves, and that is given by a mark in the header.

#### Three traps

**Marks are header-relative, not absolute.** They are written with `GetfTell()`
and read with `SetOffSet()`, both biased by `m_DefaultOffSet` = 132
(`SerialFile.cpp:164-176`). Treating them as absolute made 57% of maps look
corrupt on the first pass.

**The `.wld` cipher is not the 43-table substitution.** It calls
`SetEncode(EMBYTECRYPT_OLD, EMENCODE_WLD)` — a different API — where the
`EMBYTECRYPT_OLD` argument is a dummy non-zero value that only exists to enter
the branch; the `EMENCODE_WLD` test wins and `byte_decode` is never reached
(`SerialFile.cpp:227-234`). The real cipher reduces to
`b = ((b - 0x10) & 0xFF) ^ 0x10` — the constants are `int`s assigned into a
`BYTE`, so only the low byte survives. Selection is by **type string**, not a
version gate: `Land.Man` is encrypted, `LAND.MAN` plain (12 of 132 maps are
encrypted). Because the cipher is stateless, seeking to a mark needs no state
fixup.

**`SLAND_FILEMARK` field order changes between its two versions** — `0x0101` is
`NAVI, WEATHER, GATE, COLL`; `0x0100` is `NAVI, GATE, COLL, WEATHER`, and every
shipped file uses `0x0100`. Both are 16 bytes, so a size check will not catch
the mistake. `NAVI` is field 0 in both, which is the only reason navmesh
extraction is version-safe.

#### Verified against the live server

The Phase 0 spawn observed on map 8 was `(109.0, −319.7, −4203.4)`. The nearest
vertex in `w_school_03`'s extracted navmesh is **9.3 units** away, and the mesh's
Y range (−330..40) brackets the spawn height. That ties the asset pipeline back
to real server behaviour rather than to itself.

Structural validation runs over every mesh — corner indices in range, links in
range, vertices finite — because a wrong stride still "parses". Zero problems
across 547,788 cells.

#### `.wld0` — terrain geometry

```
node extract-terrain.js --list        # inventory
node extract-terrain.js --out DIR     # one .terrain + .textures.json per map
```

**101 of 103 sidecars decoded, 6,885 draw batches, 38,967 nodes, 15,514,004
vertices, 10,919,544 triangles, 2,914 distinct textures**, 359 MB written in
6.5 s — down from 1,442 MB of source. Every decoded file is consumed to EOF
(two carry 9 bytes of zero padding); zero nodes skipped as unreadable.

The chain, top to bottom:

```
132-byte CSerialFile header   type "default" / "Default_Crypt"
u32 version                   0x0102 (92), 0x0100 (4), 0x0103 (3), 0x0200 (2)
u32 blockSize = 24
D3DXVECTOR3 vMax, vMin        the map AABB — see the warning below
N material buckets            [u32 count] then [key string][DxSingleTexMesh]
  DxSingleTexMesh             [u32 ver][u32 size] header, effect list
    DxAABBOctree              binary AABB tree, geometry per node
```

The two files that do not decode are not ordinary sidecars:
`es_f41_killbillzone01.wld0` is genuinely corrupt (its version reads
`0x20202122` — ASCII spaces — and its `.wld` is broken too, so the whole map is
damaged), and `square_rd.wld0` is a 144-byte truncation whose body is 12 bytes,
too short to even hold the AABB.

##### Traps

Everything here produces plausible-looking output rather than an error.

- **The bucket list depends on the version, and `0x0100` shares `Load_101` with
  `0x0101`** — so they take the *same* three-bucket list. Worse, **ALPHA moves**:
  second in `0x0102`, last in `0x0103`/`0x0200`. Guessing this parses the file
  to EOF with the wrong meshes in the wrong buckets.
- **Block sizes are not trustworthy for versions the engine recognises.** Every
  `LoadPSF` and `DxSingleTexMesh::Load` reads `[version][size]`, but on a
  *known* version path the engine ignores the size and parses structurally; it
  uses the size only to skip an *unknown* version. Old writers left sizes that
  undercount. In `srp_ground.wld0` a Diffuse `0x0101` record declares 31 bytes
  and occupies 43, and the enclosing mesh block undercounts by the same 12.
  Trusting either size lands mid-way through a texture name. **Mirror the
  engine: parse structurally, size-skip only on unknown versions.**
- **`DxSingleTexMesh` `0x0101` puts `m_dwFlag` before `m_szTexName`** — the
  reverse of `0x0102`, and it is the most common shipped version.
- **`dwBufferSize` on a mesh stops before the octree.** `Save` calls `EndBlock`
  *before* `bExist`, so skipping by it lands on `bExist`, not past the mesh.
  (`DxAABBOctree::Save` does *not* have this problem — it closes the block after
  the whole tree, so its size is authoritative.)
- **Effect field order changes between versions of the same effect.** Diffuse
  `0x0101` leads with `bSpeed`; `0x0102`+ leads with the texture name. FlowUV
  `0x0100` puts its two strings last, `0x0101` puts them second.
- **`TEXEFF_VISUALMATERIAL` `0x0102` ends with a trailing sized block** that
  `Load_100` does not have — `Save` writes `DWORD(4)`, `DWORD(0)`. Miss it and
  you are 8 bytes short at the next mesh.
- **The collision tree's two child flags are not adjacent.** The layout is
  `[max][min][dwFace][BOOL hasLeft][left…][BOOL hasRight][right…]` — the left
  subtree is written *between* them. Reading both flags together desyncs on any
  node that has a left child.
- **`dwVer < 0x10000` in the geometry blob means the value IS the FVF**, not a
  version (`OctreeLoadOLD`). Compare in the right order or mis-stride silently.
- **Vertex stride comes from the FVF DWORD**, never a hardcoded struct. Three
  ship, not the two the writer suggests: `0x112` (32 bytes), `0x152` (36), and
  **`0x142` (24) — which has no normal**, so its UV0 sits at offset 16 rather
  than 28. 33 maps use it.
- **The day/night colour table exists only when `landType == 0`.** Reading it
  unconditionally eats 16 bytes per vertex that are not there.
- **`std::string` length includes the NUL**; **`BOOL` is 4 bytes**; block sizes
  exclude the size field itself.

##### The map AABB in the header is unreliable

Do not use it. In `blue_zone1.wld0` the file's own geometry escapes its own
declared AABB by **6,602 units**; `ep2_lost_1city`, `ep2_lost_2city` and
`ep2_prison` are out by 880–1,240. The **per-node** boxes are sound — every one
of 15.5M vertices lies inside its own node's box — so compute the map bounds as
the union of node boxes.

##### Validation

Geometry is validated, not just parsed — the lesson from meshes and animation,
where a wrong stride still "parsed". Across all 22,926 geometry nodes: **0
indices out of range, 0 vertices outside their node AABB, 0 nodes over the
16-bit index limit, 0 degenerate nodes.** That check is backed by a positive
control in `test.js`: decoding `0x152` at `0x112`'s stride flags 16 of 16 nodes,
so a clean run means something.

Cross-checked against the independently decoded navmesh: for **97 of 99** maps
that have both, the navmesh fits inside the terrain bounds. `w_school_03` — the
map the Phase 0 spawn was verified against — fits with **0.3 units** of margin.
The two exceptions are `cha1_select`/`cha2_select`, character-select scenes with
a single backdrop mesh and a deliberately larger navmesh.

##### Looking at it

None of the above proves the map is *the map*. A plausible-but-wrong decode
produces clean numbers and nonsense geometry, so terrain gets the same
eyes-on check every other asset class here got:

```
node preview-terrain.js w_school_03 --out map.png    # top-down z-buffer render
node extract-terrain.js --obj w_school_03 --out DIR  # OBJ+MTL for a real viewer
```

`w_school_03` renders as a symmetric walled campus with a central courtyard,
colonnade pillars and staircases; `srp_ground` as a lakeside town with rock
formations and prop rows; `ep2_lost_2city` as a room-and-corridor dungeon. Those
three were chosen to cover the paths most likely to be wrong — the `0x0100`
three-bucket list, the effect record whose declared size undercounts, and a
large `0x0102` map.

The OBJ exporter negates X and reverses winding: the engine is left-handed and
OBJ viewers are not, so without it the map loads mirrored and still looks
plausible. V is flipped for the same reason.

### Mesh and animation export

```
node extract-meshes.js --out DIR      # one .rmesh per .x
node extract-anim.js --out DIR        # one .ranim per AnimContainer .bin
node preview-mesh.js NAME --out P.png # render one, to look at it
```

**4,467 `.rmesh`** (2,576 skinned) — 26,591 meshes, 338,279 bones, 9,183,803
vertices, 9,626,775 triangles, 592 MB. The 20 failures are the encrypted-
container files whose reader is absent from SOURCE. (Was 1,993 skinned /
8,437,143 vertices before the object-reference fix below — measured
2026-08-20.)
**6,498 `.ranim`** — 397,523 tracks, **15,452,358 keys**, 718 MB, zero failures.

Getting from "parsed" to "usable" needed three things the reader was throwing
away, each of which fails silently rather than loudly:

- **`SkinWeights` carries a `matrixOffset` after the weights** — the inverse
  bind pose. It was being dropped. Without it a mesh still loads and still
  animates; it just collapses into a puddle, because every vertex skins from an
  unbound rest position.
- **The `Frame` tree is the skeleton.** `parse()` returned only `frameCount`.
  Bones are now flattened parent-first, so world transforms need one forward
  pass and no fixup step.
- **Materials were counted, not named.** `MeshMaterialList` also holds the
  per-face material assignment, which is what splits one mesh into Unity
  submeshes instead of one draw call per material change.
- **`{ ObjectName }` is a REFERENCE, not a definition, and a naked `{` broke
  the node walk.** 3ds Max's `.x` exporter dedupes a material a mesh shares
  with an earlier one by writing `{ MaterialName }` instead of a second inline
  `Material { ... }` block. `parseNodes()`'s structural walk had no case for a
  `{` that isn't preceded by a `NAME` in that position, so it fell through to
  the "skip a stray token" catch-all; the reference's own `NAME` then got
  read as a bare-name reference *of the enclosing node*, and the reference's
  closing `}` then closed the ENCLOSING node one level early. A
  `MeshMaterialList` with 3 referenced materials cascades through three such
  premature returns, detaching everything declared after it in the same
  `Mesh` block — `MeshTextureCoords`, `XSkinMeshHeader`, every `SkinWeights`
  — from the mesh entirely. Found by chasing a real Android-device screenshot
  of `boa` whose torso and lower body (`Ran/data/skin/bir01.x` frame `"01"`,
  1230 verts skinned to 9 `Bip01_*` bones in the source; `bir02.x` frame
  `"Object01"`, 763 verts / 7 bones) were reported `skinBones=0` — RanChfBuilder
  then read them as ORPHANED RIGID parts (no bone in their own hierarchy
  matched the character's) and placed them, unposed, at their raw local
  offset relative to the character root: several units *below the floor*.
  Nothing was missing; the body was buried. Fixed by giving a naked `{` its
  own recursive parse frame (so its own `}` only closes itself) and resolving
  the name(s) inside against a by-name registry of already-parsed objects —
  which also recovers the `TextureFilename` a referenced `Material` carries,
  fixing textures alongside skinning. +583 files gained real skin data,
  +746,660 vertices, +1,247 texture references surfaced. Pinned in
  `rcc-extract/test.js` (`bir01.x`/`bir02.x` regression checks + a minimal
  synthetic repro) and `unity/typecheck/RunCheck.cs`.

#### Bone indices point at the mesh, not the file

The single most important structural decision here. Equipment and costume meshes
skin onto the **character's** skeleton, which lives in a *different file* —
`s_cos_psy_dancer.x` has 7 frames of its own but skins to `Bip01_Pelvis`,
`Bip01_Spine`, `Bip01_L_Thigh`. Resolving bone names to indices at extraction
time therefore drops every one of those influences on the floor.

The first version did exactly that, and the symptom was quiet: **886,609
vertices came out unweighted** and got rigidly bound to bone 0. After giving each
mesh its own skin-bone table that keeps the bone **name** (plus `localBone`, or
-1 when external), that number fell to **542**. Same data, same parser — the
difference was entirely in where the indirection sat.

#### The compressed quaternion is asymmetric

`SQuatPosKey` stores rotation as two DWORDs (`QUATCOMP`). `DecompressionQuaternion`
(`SAnimation.cpp:51`) reads x, y and z as `(raw - 32767) / 32768` — but **w as
`raw / 65535`, with no bias and a different divisor**. That is not a typo to be
tidied up: the compressor negates the whole quaternion whenever `w < 0`, so w is
always in 0..1 and needs no signed range. Making the four channels symmetric
produces rotations that look almost right, which is the worst kind of wrong.

The check that settles it: of **13,010,783** decompressed quaternions, all but
**two** land on the unit sphere within 2e-2. The two that miss decode to
`(-1,-1,-1,0)` from an all-zero `QUATCOMP` — an uninitialised source key, the
same class of artefact as the terrain NaN UVs.

#### Tick rate is measured, not assumed

The engine has no constant for it — `SAnimation.cpp` derives `dwUnitKey` at
runtime as the smallest gap between keys. Across the corpus, every one of
**14,259,112** inter-key gaps is an exact multiple of **160 ticks**: the 3ds Max
biped convention of 160 ticks per frame at 30 fps, i.e. **4800 ticks/second**.
The corroboration is that it yields a median clip of 1.37 s and a maximum of
29.8 s, which are sane for character animation.

Other traps worth keeping: `SMatrixKey` puts its matrix at offset **16, not 4**
(`D3DXMATRIXA16` is 16-byte aligned, so the struct is 80 bytes, not 68), and
only two channel kinds actually ship — 85,401 matrix tracks and 312,122 quatPos.
There are no position, rotate or scale tracks at all.

#### Verified

`node test.js` walks every exported file: sizes match headers, every
cross-reference resolves, bone parents precede their children, submeshes tile
each mesh exactly, bone weights sum to 1, key times are non-decreasing, and every
stored quaternion is unit length. And by eye, from the exported binaries:
`w_cos_schoollook` renders as a humanoid in T-pose with skirt and shoes,
`bike_b_w` as a motorbike with spoked wheels and handlebars.

### Map object placement — the `DxOctree` scene graph in `.wld`

```
node extract-mapobj.js --out DIR      # one .rmapobj per map
```

Terrain is the ground; this is everything standing on it. **31 of 132 maps carry
objects: 14,831 placed objects, 1,028,429 vertices, 565,104 triangles, 545
textures, 1,115,377 collision nodes**, 25 MB, zero frame-decode failures. The two
failures are the same two corrupt maps that fail everywhere else.

Only interiors and a few set-piece zones use it (`w_4school_inner*`,
`blue_zone1`, `login`); outdoor maps bake everything into the `.wld0` static
mesh instead. **Object geometry is inline, not a reference to a `.x` file**, so a
map is self-contained once terrain and objects are loaded.

The chain has no mark pointing at it — `LoadFile_VER200` reads the header, the
filemark, and goes straight in, so the offset is positional:

```
DxOctree::LoadFile        BOOL subdivided, vMax, vMin, dataAddress, dataSize
                          <payload>, then 8x [BOOL -> child]
  DxFrame::LoadFile       2 AABBs, 3 x D3DXMATRIXA16, name, meshes,
                          2 effect lists, sibling, first child
    DxMeshes::LoadFile    materials (blitted), texture names, DxOctreeMesh, next
      DxOctreeMesh        FVF, vertices, faces, attribute table, collision tree
```

`m_DataSize` is what makes this tractable: the engine's own dynamic-load path
skips each payload with it, so the tree walks without decoding a single frame,
and a frame that fails costs one node rather than the rest of the map.

#### Traps

- **The sibling and child flags are not adjacent.** `DxFrame::LoadFile` reads the
  sibling flag and parses the *entire sibling subtree inline* before reading the
  child flag: `[body][BOOL sibling][sibling…][BOOL child][child…]`. An iterative
  sibling loop — the obvious implementation — desyncs on the first frame that has
  a sibling, i.e. nearly all of them. This is the same shape as the `.wld0`
  collision tree, and fixing it took decoded frames from **1,290 to 8,304**.
- **Strings here use the opposite convention to `.wld0`.** `DxFrame` and
  `DxMeshes` read `int StrLength` then exactly that many chars, appending the NUL
  themselves — so the length **excludes** the terminator. `.wld0`'s
  `CSerialFile` strings **include** it. One byte, every following field.
- **`DxFrame::LoadEffect` is not shaped like the `.wld0` texture effects.** It
  opens with an optional NAME and closes with an optional blitted
  `DXAFFINEPARTS`, and only the middle blob is size-prefixed. Missing either end
  accounted for the last 1,161 failures.
- **`DxOctreeMesh` has no version and no size** — it must be walked exactly. Its
  vertex stride comes from `D3DXGetFVFVertexSize`, and it ends with a
  **conditional** field: `m_bUnlit` exists only when the FVF carries
  `D3DFVF_DIFFUSE`. Read it unconditionally and every non-diffuse mesh loses four
  bytes; skip it and every diffuse one gains four.
- **Attribute ranges are in FACES, not indices.** `D3DXATTRIBUTERANGE.FaceStart`
  and `FaceCount` must be multiplied by 3 to address the index buffer.

Every blitted stride came from the layout probe rather than arithmetic —
`DxFrameMesh.h` and `DxMethods.h` were added to it for this, giving
`D3DMATERIALQ` 68, `D3DEXMATERIAL` 8, `D3DXATTRIBUTERANGE` 20, `DXAFFINEPARTS`
36, and `D3DXMATRIXA16` 64 (so the fixed DxFrame prefix is 216).

Verified by eye from the exported binaries with the transforms applied:
`w_4school_inner02` renders as a symmetric school floor plan with wings and
corridors; `blue_zone1` as a harbour with hulled ships moored along piers and a
rocky coastline.

### Collision needs no extraction — measured, not assumed

`DxAABBNode::LoadFile` stores `vMax`, `vMin`, `dwFace` and two child links.
`dwFace` defaults to `AABB_NONINDEX` (`0xFFFFFFFF`) on interior nodes, and
`DxOctreeMesh::IsCollision` consumes a leaf as:

```cpp
PWORD pwIndex = pwIndexB + pAABBCur->dwFace * 3;   // DxOctreeMesh.cpp:236
```

So `dwFace` is a **triangle index into the mesh's own index buffer**. The tree
carries no geometry — it is a BVH over triangles already extracted.

The one thing that could still have mattered was coverage: a tree indexing only
*some* faces would be telling us which geometry is collidable. Measured across
the whole corpus:

| | trees | full coverage | partial | leaf index out of range |
|---|---:|---:|---:|---:|
| Terrain `.wld0` | 22,926 | **22,926** | 0 | 0 |
| Map objects `.wld` | 14,831 | **14,831** | 0 | 0 |

**100.00% of 10,919,544 terrain faces and 565,104 object faces.** Every mesh has
a tree and every tree indexes every face, so the structure holds no information
the geometry does not. The correct port is a Unity `MeshCollider` on the
imported mesh, which builds an equivalent BVH itself.

That measurement doubles as the strongest check on the collision-tree walker:
a walk that drifted would produce garbage face indices, and out-of-range is zero
across all 37,757 trees.

### Per-map manifests

```
node build-maps.js --out DIR      # one <map>.map.json per map
```

**130 manifests.** Extraction writes four unrelated files per map and nothing
said they belonged together. Each manifest names the assets, merges the terrain
and object texture lists, and records world bounds as the union of node and
placed-object boxes — **never** the source header AABB, which is stale.

The real value is the cross-check it makes permanent. Terrain, objects and
navmesh come from three independent parsers across two container formats; if all
three land in the same coordinate space, none of them is badly wrong.
**118 of 120** maps have their navmesh inside the world bounds, and the two that
do not are `cha1_select`/`cha2_select` — character-select scenes with a single
backdrop mesh and a deliberately larger navmesh. `w_school_03`, the map the
Phase 0 spawn was verified against, fits with **0.30 units** to spare.

Object bounds are recomputed **post-transform**: the stored per-object AABBs are
in local space, and a scene needs where geometry lands, not where it was
authored.

### Texture reference audit

```
node audit-assets.js                 # report
node audit-assets.js --write DIR     # + referenced/missing/unreferenced json
```

**15,194 textures ship; 7,886 are referenced.** A missing texture is silent
everywhere in this pipeline — nothing errors, the surface just renders untextured
— so the counts are pinned in `test.js`.

References come from two kinds of source and they do **not** deserve equal
confidence, so the report keeps them apart:

| Source | Refs | Missing | Confidence |
|---|---:|---:|---|
| **Parsed** — terrain, objects, meshes, item tables | 6,654 | **433** | structured fields in fully decoded formats |
| **Scanned** — character pieces, GUI, effects, glogic, loose piece/skin | +6,422 | 1,303 | regex over decoded bytes — candidates |

The parsed 433 are spread across **284 meshes and 3 maps**, not concentrated in
one broken asset, and the names are heavily seasonal/event content
(`2011growth_*`, `2013_wsg_*`, `2014_oct_*`, `nms*_mas`) — consistent with
content packs whose art was never shipped to this server.

The scanned side reaches the references geometry cannot see: item icons and GUI
art are named in **data**, not meshes. `item.isf` is 26 MB of `GLITEM` v0x200
and yields nothing raw — its texture names appear only after decoding with
`EMBYTECRYPT_ITEM`. That is a **string scan, not a parse**, chosen deliberately
over implementing `GLItemBasic::LoadFile` and its nine versions when the names
are the entire payload. It both misses names and invents them: 159 of its hits
were discarded as debris (`e.tga`, `_3.dds`, and
`outgui_character_lgaacter_lga.dds`, which is two overlapping matches). Its
findings are reported as candidates and never merged into the reliable number.

Spot-checked: `interface_main.dds` is referenced by GUI XML and present, while
`btn.dds`, `chatlib.dds` and `gui_background.png` are referenced and genuinely
absent — so the scan does find real gaps, not only noise.

Matching is forgiving in exactly the ways the engine is: case-insensitive, and
extension-agnostic on a fallback pass. That last one matters — **14 references
resolve only by ignoring the extension** (`eff08.dds` → `eff08.tga`,
`sword.jpg` → `sword.dds`, `ron0002_1.psd` → `ron0002_1.dds`). The engine sniffs
headers through D3DX rather than trusting the name, and `convert-textures.js`
does too, so these are correct rather than broken.

### Item tables — `item.isf` / `item1.isf`

`itemdata.js` parses both: **37,090 items, every record decoded, byte-exact to
EOF, zero failures.** That last part matters — reading to the final byte of 26 MB
and 23 MB is what proves the record walk is right.

It exists because item icons are addressed as `strInventoryFile` + `sICONID`,
never as a literal filename in any mesh or map, so no amount of scanning can
recover the relationship.

The container is `[132-byte header][u32 count][count × SITEM]`, encoded with
`EMBYTECRYPT_ITEM` above version 0x0200. Each record is a run of tagged blocks
ending at `0xEDEDEDED`.

**Block framing is not uniform, and that is the trap.** Types 1–6 (basic, suit,
drug, skillbook, generate, grinding) have `[version][size]` read by the
dispatcher and are skippable. Types 7+ (box, random box, pet, vehicle, rv card,
self-buff…) delegate straight to their own `LOAD`, which frames itself however
it likes. A uniform `[type][ver][size]` walk parses the first nine blocks
convincingly and then degenerates to `type 0, size 0` forever.

So only `FILE_SBASIC` — always first, and the only block naming art — is
decoded, and the record is resynchronised on the sentinel. That resync is
**verified, not trusted**: the next record must carry a plausible `SITEM`
version and lead with `FILE_SBASIC`, else the scan continues. Zero false
positives across 37,090 records.

Two things that bit:

- **The sentinel scan must be byte-wise, not DWORD-aligned.** Length-prefixed
  strings leave nothing aligned after the first one, so a 4-byte stride steps
  over most terminators. The failure is quiet — it yielded 3,341 of 18,745
  records, every one of them *valid*, having silently swallowed five records per
  step.
- **`sReqStats` is blitted with `ReadBuffer`**, so `SCHARSTATS` is a stride, not
  a field. It came from the layout probe (12 bytes); every field after it —
  including both the ones this needs — shifts if it is wrong.

#### Items name much more than textures

`strFieldFile` is a `.x` or `.egp`, and the 32 `strWearingFile*` entries are
`.cps` character pieces. Routing those into the texture set inflated the missing
count from 433 to **7,938**, and the extension-agnostic fallback then "resolved"
`sword.x` against `sword.dds` — hiding the real answer behind a wrong one. The
audit now routes references by extension and reports each kind separately:

| Kind | Referenced | Missing |
|---|---:|---:|
| `.cps` character pieces | 6,696 | 323 |
| `.abl` ability files | 889 | 18 |
| `.vcf` / `.vps` vehicle | 99 | 10 |
| `.x` models | 18 | 1 |

The `.cps` chain is the link to `Ran/textures/item` — 4,133 named files there
(only 7 are numeric, correcting an earlier guess of mine), reached through
character pieces rather than named directly by items.

#### Character pieces are the item → art link

`SkinObject.rcc` holds **12,879 `.cps`** across **17 versions** — only 960 are
current (0x0201); the bulk are 0x0106 (4,492) and 0x0110 (3,174). Implementing
fourteen loaders to reach names a scan already finds was not a good trade, so
they are scanned, with one detail that matters:

**`.cps` keeps its version as the first BODY dword**, not in the CSerialFile
header — `DxSkinPiece::LoadPiece` does `SFile >> dwVer` before anything else.
Asking the header for it returns 0, every file then looks unencrypted, and the
1,073 encoded ones (≥ 0x0200) yield nothing.

Adding them resolved **3,876 more existing textures** and confirmed the chain:
items name `.cps` pieces, which name the art in `Ran/textures/item` and
`textures/char`.

#### The unreferenced pile: 9,134 → 4,436, and still not a deletion list

Worth stating plainly, because I misread this number once already — an early
pass reported 9,134 and framed it as an install-size win against the
7.2 GB → 2–4 GB target. That was wrong, and each layer of real reference data
has eaten into it:

| After adding | Unreferenced |
|---|---:|
| geometry only | 9,134 |
| + item tables, GUI, effects | 8,327 |
| + character pieces (`.cps`) | 4,436 |
| + `.aps` attach pieces | 4,365 |
| + loose `data/piece`, `skin`, `skeleton` | **4,182** |

`data/piece` alone is 1,105 `.pis` files sitting on disk rather than in any
archive — an archive-only sweep misses them entirely. `.abl`, `.chf` and `.abf`
were checked against all 30 byte-crypt tables and reference no textures at all
(ability and animation data).

The remaining 4,182 sit mostly in `textures/map`,
`textures/item` (1,126) and `textures/char` (482).

**Quest, NPC dialogue and level data are now scanned too, and they reference no
textures at all.** `Quest.rcc` (831 `.qst`), `NpcTalk.rcc` (662 `.ntk`) and
`Level.rcc` (265 `.lev`) yield zero texture names raw *and* under every
byte-crypt table — they address items, NPCs and maps by ID. That closes a
caveat this file repeated for several passes; it is not where the remaining
textures are hiding.

What they look like instead is **legacy art variants**: `01city-ban.dds` is
referenced while `01city-billding50-12.dds` and `-15` are not — numbered
siblings from the same families, only some of which the shipped maps use.

It is still **a review list, not a diff**, for two honest reasons: a string scan
can miss a name it should have found, and RAN substitutes some character
textures at runtime per school and class. But the trend is the finding — every
time a real reference source is parsed, the supposed dead weight shrinks.

#### `Ran/data/map/RanMapZipTemp` is 2.41 GB of garbage

386 `.wld` files on disk are only **132 distinct maps**. The rest are stale
per-pid extractions: the server unpacks a map out of `Map.rcc` into
`RanMapZipTemp\<pid>\` and deletes it on exit, so every crashed or killed
process leaves a copy behind. There are **66 orphaned pid directories, 483 files,
2.41 GB**. `cha_select.wld` alone has 68 copies. Safe to delete; nothing
references them.

### Facts worth knowing before converting anything

- **Only `GLogic.rcc` uses layer 2** (359 of 376 entries). `Quest`/`Level`/
  `NpcTalk`/`Gui` are plaintext after the XOR pass, despite `SOURCE_RCC.md`
  describing glogic data as "already encrypted at rest" — packing stores those
  bytes as-is, so the total is still one layer. Verified, not assumed.
- **Text is CP949 (Korean), not UTF-8.** Decoded `.ini`/`.txt`/`.crowsale` files
  look like binary to an ASCII check but are Korean comments. Anything
  user-facing needs transcoding before it reaches a mobile client.
- **Entry names are flat and lowercased** — `CUnzipper` looks them up as
  `zipPath + bareFilename` with no separator, so no archive stores folder
  prefixes.

## Client core (Phase 1)

The spike is Phase 0 scaffolding and stays as the reference decode. `client/` is
the permanent layer built on what it proved.

```
cd MOBILE/client
node test.js            # offline, 25 checks, no server needed
node demo.js --walk 2   # full flow against the live server
```

`Session` encodes the four ordering constraints that each caused a silent
failure during Phase 0, so a caller cannot get them wrong:

1. Credentials go to the **agent**, not the login server (the login server drops
   them via `default: break` — no error, no close).
2. Wait for `NET_MSG_SND_CRYT_KEY` before sending credentials.
3. Keep **both** sockets open after the field handoff — lobby and spawn traffic
   is relayed through the agent; only gameplay input goes to the field.
4. Send `LANDIN` + `READY` on the agent socket after the spawn, or the character
   stays in `EM_ACT_WAITING` and can never act.

`Connection` adds what a mobile client actually needs: exponential backoff with
**full jitter** (so a server restart doesn't produce a thundering herd of
lockstep reconnects), automatic `NET_MSG_COMPRESS` unwrapping in both its
batching and LZO forms, and a `desync` event instead of guessing at a resync
point. The outbound guard is kept — the server still firewall-bans IPs that
send malformed packets.

`Session.syncPosition()` is worth knowing about: there is no "where am I"
message in this protocol, so it reads the server's authoritative position by
deliberately claiming a far-off `vCurPos` and reading `m_vPos` out of the
snap-back. Side effect: it forces `GLAT_IDLE`, stopping any walk in progress.

A full run — login, character list, field join, spawn, ready, two confirmed
walk steps — completes in about 9 seconds.

### World model

`world.js` tracks nearby entities. Which messages it handles was decided by
**measurement, not by reading the 1615-message enum** — `census.js` enters the
world, stands still, and reports what actually arrives:

```
node census.js 45
```

Idle in an empty area: **~885 B/s, 170 messages in 45 s**. The lifecycle is three
messages:

| Message | Meaning | Size |
|---|---|---|
| `NET_MSG_GCTRL_DROP_CROW` (3016) | entity enters view | 484 B |
| `NET_MSG_GCTRL_CROW_MOVETO` (3503) | entity moves | 48 B |
| `NET_MSG_GCTRL_DROP_OUT` (3019) | batch of entities leaving view | variable |

`DROP_CROW` at 484 bytes each dominates that budget — worth knowing before
designing for a metered mobile connection.

Two details the decode had to get right:

- Nested payload offsets are composed from two compiler-measured numbers
  (`offsetof(SNETDROP_CROW, Data)` + `offsetof(SDROP_CROW, field)`). `test.js`
  checks the composition against a **real captured packet**, not just the
  arithmetic.
- `sNativeID` is legitimately **0 for most entities** (22 of 26 sampled), mixed
  with real ids like 53/1 and 9/44. That looked like a decode bug and is not —
  verified against raw packet bytes before concluding.

`DROP_OUT` is a batch whose `cNUM` is trusted only after clamping to what
actually arrived, and movement for an entity never announced is tracked as
`inferred` rather than discarded, so the entity count stays honest.

## Spike client

```
cd MOBILE\spike
node selftest.js                                   # offline, no server
node spike.js --user USER --pass PASS               # needs a local server
```

`selftest.js` covers framing, partial/batched TCP delivery, desync detection,
the garbage layer, and struct packing — **35 checks, all passing**. Run it first
so that a failure against a real server is known to be protocol-level rather
than a framing bug.

`spike.js` refuses non-loopback hosts unless `--allow-remote` is passed, because
`cfg/` in this repo points at a live production IP.

### Protocol notes

- **Header** — `dwSize` (u32 LE) + `nType` (i32 LE), 8 bytes.
- **Garbage** — fixed 5-entry table (`RcvMsgBuffer.cpp:10`), inserted between
  header and body, counted in `dwSize`. The client picks randomly but never
  reuses either of its two stored slots, so consecutive packets always differ
  (`s_NetClient.cpp:856`). Applied only **after** leaving `NET_STATE_LOGIN`.
- **Compression** — `NET_MSG_COMPRESS` (170) wraps an LZO payload. Not yet
  implemented in the spike.
- **TEA** — field-level only (credentials, heartbeat key), not a stream cipher.
  Not needed for the login milestone; required for the Agent-stage heartbeat.

---

## Status

- [x] Layout probe building and emitting `layout.json`
- [x] Framing + garbage layer implemented, **54 offline tests passing**
- [x] Login-server handshake verified against the live server
- [x] XXTEA ported and verified byte-for-byte against real `minTea.cpp`
- [x] Server-list request/parse (`NET_MSG_REQ_GAME_SVR` → agent IP:port)
- [x] Agent handoff with garbage layer on
- [x] `NET_MSG_COMPRESS` unwrapped, both batching and LZO1X forms
- [x] **Milestone 1: `EM_LOGIN_FB_SUB_OK` from ServerAgent**
- [x] **Milestone 2: character list retrieved and selected**
- [x] **Milestone 3: field handoff, identity accepted, spawn received**
- [x] **Milestone 3b: character walks — position changes and persists**
- [x] All 1615 enum names resolved by the compiler (no more UNKNOWN types)

Reproduce with `node spike.js` (credentials from `local.json`).
`--no-join` stops at the lobby instead of entering the world.

### End-to-end, against the live production server

```
LOGIN :12004  version -> encrypt key -> server list
AGENT :12003  crypt key + random slot -> CHINA_NET_MSG_LOGIN
              -> EM_LOGIN_FB_SUB_OK
              -> NET_CHA_REQ_BA_INFO -> 1 slot [1]
              -> NET_MSG_REQ_CHA_BINFO(1) -> "GameMaster", 1168b
              -> NET_GAME_JOIN -> field handoff (ip:12002, gaeaID, slot)
FIELD :12002  NET_GAME_JOIN_FIELD_IDENTITY
              <- (via agent) NET_MSG_LOBBY_CHAR_JOIN, spawn at
                 map=8 (169.0, -319.7, -4203.4), then 20+ CHAR_PUTON_EX
AGENT         -> SNETREQ_LANDIN + SNETREQ_READY   (leaves EM_ACT_WAITING)
FIELD         -> SNETPC_GOTO  (169 -> 189 on X)
              => server position 189.0, displacement 20.0, persists
```

**Phase 0 exit criterion met:** a non-`MiniA.exe` process walks around a live
field server, on unmodified server binaries.

### Which socket carries what

Both sockets stay open from the handoff onward, and the split is not obvious:

| Direction | Socket |
|---|---|
| Lobby/account, **and the spawn burst** (`CHAR_JOIN`, `CHAR_PUTON_EX`) | **agent** |
| Gameplay input (`SNETPC_GOTO`) | **field** |
| In-field responses (`UPDATE_STATE`, `GM_MOVE2GATE_FB`) | **field** |

The spawn arriving on the *agent* socket is genuinely surprising —
`CFieldServer::SendAgent` relays it (`s_CFieldServer.cpp:678`). Logging that did
not tag the connection made field traffic look like it was missing entirely.
The spike tags every line `LOGIN`/`AGENT`/`FIELD` for exactly this reason.

### Movement

`SNETPC_GOTO` is destination-based and is validated server-side in
`GLChar::MsgGoto` (`GLCharMsg.cpp:243-311`):

- `fDist = |m_vPos - vCurPos|`; **`> 60.0f` is rejected** as out-of-sync. The
  reply is `SNET_GM_MOVE2GATE_FB` carrying the server's real `m_vPos`, sent
  directly to the mover rather than view-broadcast. **That 60-unit gate is the
  hard ceiling on client-side prediction drift** — the number §9's joystick
  tuning needs.
- On success the broadcast is `SNETPC_GOTO_BRD` via `SendMsgViewAround`
  (`:305`) — **only to players in view**. On an empty map you receive nothing,
  so silence is *not* evidence the move worked.

That rejection path doubles as a **position oracle**: deliberately claim a
`vCurPos` 5000 units away and the server tells you where it thinks you are. The
spike uses this to measure real server-side state; there is no other way to
observe it from outside.

**Working.** A single `SNETPC_GOTO` moves the character exactly the requested
distance, and the new position persists across reconnects (a later session
spawns where the previous one stopped).

### The handshake that makes movement possible

This cost most of a session, so it is worth stating plainly.

`GLChar::CreateChar` puts a freshly-joined character into **`EM_ACT_WAITING`**
(`GLChar.cpp:598`). While that bit is set, `IsValidBody()` is FALSE
(`GLChar.cpp:698`) — the character exists, receives broadcasts, and its packets
are parsed and validated, but it cannot act.

Nothing on the server prompts for this. The PC client clears it on its own, once
its map finishes loading: when its internal wait counter expires it sends two
bare-header messages and the server's `MsgReady` does
`ReSetSTATE(EM_ACT_WAITING)` (`GLCharMsg.cpp:169-174`).

```
AGENT socket (NETSEND, not NETSENDTOFIELD — despite being GCTRL-range):
  NET_MSG_GCTRL_REQ_LANDIN  3025   GLMSG::SNETREQ_LANDIN   header only
  NET_MSG_GCTRL_REQ_READY   3026   GLMSG::SNETREQ_READY    header only
```

Source: `GLCharacter.cpp:4162-4177`.

A headless client never loads a map, so it never reaches the code path that
would send these — and then sits in the world silently unable to move. **Any
reimplementation must send LANDIN + READY explicitly after the spawn.**

### Why this took so long to find

The failure was invisible from outside, and two pieces of evidence actively
pointed the wrong way:

- `NET_MSG_GCTRL_UPDATE_STATE` came back after every `SNETPC_GOTO`, which looks
  like an acknowledgement. It is not: `MsgSendUpdateState` sits at
  `GLCharMsg.cpp:308`, *outside* the `if (bSucceed)` guard, so it fires whether
  or not the move was accepted.
- The success broadcast `SNETPC_GOTO_BRD` goes out via `SendMsgViewAround`
  (`:305`) — only to players already in view. On an empty map, a *successful*
  move is indistinguishable from a rejected one.

Two dead ends worth recording so nobody repeats them. Neither was the cause:

- **Destination choice.** ~330 candidates were tried — 4 radii × 4 compass
  directions × tiled height offsets, plus finer sweeps. All failed, because the
  character could not act at all.
- **A ray straight down at the character's own feet** was used as a "must
  succeed" control. It is worthless: if it succeeds the path length is zero, so
  the displacement is 0.0 either way. It cannot distinguish anything.

The lesson matches the enum one above: when a server is silent, find the state
machine the real client drives, rather than varying the inputs you can see.

---

## The thing that blocked Milestone 1 for a whole session

**This server is configured as CHINA, not THAILAND.**

`cfg/[3]ServerAgent.cfg` sets `service_provider 3`, and `SP_CHINA = 3` while
`SP_THAILAND = 6` (`s_NetGlobal.h:355-367`). Every per-provider login handler
begins with a provider guard:

```c
void CAgentServer::ThaiMsgLogin (MSG_LIST* pMsg)
{
    if (m_nServiceProvider != SP_THAILAND || pMsg == NULL) return;   // <-- line 18
```

That return writes **no console message, sends no reply, and does not close the
connection**. It is the only branch in the whole handler that is completely
silent — every other failure path calls `CConsoleMessage` *and* sends
`NET_LOGIN_FEEDBACK_DATA`. So a provider mismatch is indistinguishable from a
dead server unless you check the cfg.

Two corroborating signals that were visible the whole time:

- `ERROR:DecreaseChannelUser Channel Number Wrong(-1)` on every disconnect.
  `SetChannel` is only reached at line 90, well past the guard, so the channel
  stayed at the `-1` that `CNetUser::Reset()` assigns.
- `Agt Msg Leak Type` never appeared, which proves the message *was* dispatched
  (`s_CAgentServerMsg.cpp:554`) and died inside the handler, not in the switch.

The fix is `CHINA_NET_MSG_LOGIN` (2055) with `CHINA_NET_LOGIN_DATA`. Set
`"service": "thai"` in `local.json` if the server cfg is ever changed back.

### Three more traps in the China login path

**1. The struct is not the Thai struct.** Field order is
random-pass → password → userid, and every string carries `RSA_ADD` (4) bytes of
padding that are *not* part of the encrypted region, so field size and TEA
length differ:

| Field | Field size | TEA length | Source |
|---|---|---|---|
| `szUserid` | 25 | 21 (`USR_ID_LENGTH+1`) | `s_NetClientMsgLogin.cpp:136` |
| `szPassword` | 25 | **20** (`USR_PASS_LENGTH`, no `+1`) | `:137` |
| `szRandomPassword` | 11 | 7 (`USR_RAND_PASS_LENGTH+1`) | `:138` |

**2. Whether the password is encrypted depends on its LENGTH.**
`minTea::encrypt(char*, int)` pads to a multiple of 4 (minimum 8) and appends a
NUL; if that total exceeds `nMaxLength` it **returns false and copies nothing**,
leaving the caller's buffer as plaintext (`minTea.cpp:186-197`). For
`szPassword`, `nMaxLength` is 20 (`USR_PASS_LENGTH`, no `+1`), so:

| Secret length | Padded (+NUL) | Result |
|---|---|---|
| ≤ 16 chars | ≤ 17 | **encrypted**, and the server's `decrypt` reverses it |
| ≥ 17 chars | 21 | **bail — sent as plaintext**, server's `decrypt` no-ops identically |

The 19-char MD5 hex that `pwMode: 'md5'` produces always lands in the second
row; a short human password lands in the first. Both round-trip correctly,
because the server's decrypt mirrors the same bail. The empty random password
also bails. `tea.js` reproduces this exactly and `client/test.js` asserts both
branches — an implementation that always encrypts passes the short-password case
and then silently fails on anything ≥ 17 characters.

**3. This server stores passwords in plaintext.** `UserCheck` passes the string
straight into the `user_verify` stored procedure
(`s_COdbcUserCheck.cpp:298-307`). Empirically:

| `--pw-mode` | Result |
|---|---|
| `md5` (what `ChinaSndLogin` does) | `EM_LOGIN_FB_SUB_INCORRECT` (5) |
| `plain` | **`EM_LOGIN_FB_SUB_OK` (0)** |

Note a full 32-char MD5 could never work here regardless: the server re-truncates
to 19 chars with `StringCchCopy(dst, USR_PASS_LENGTH, ...)`
(`s_CAgentServerMsgLogin.cpp:296`). Default is now `plain`; `--pw-mode md5`
switches back.

### Verified flow (against the live server)

```
--- ServerLogin :12004 ---------------------------------------
-->  NET_CLIENT_VERSION        110    16b
<--  NET_MSG_VERSION_INFO      110    16b   patch=1 game=1
<--  NET_MSG_SND_ENCRYPT_KEY   2102   24b   12-char value
<--  NET_MSG_SND_GAME_SVR      1552   56b   143.14.11.244:12003
<--  NET_MSG_SND_GAME_SVR_END  1562    8b

--- ServerAgent :12003  (garbage layer ON) -------------------
<--  NET_MSG_COMPRESS          170    36b   bCompress=false (batching)
       NET_MSG_SND_CRYT_KEY    140    12b   01 00 01 00
       NET_MSG_RANDOM_NUM      141    12b   random-pass slot, varies
-->  CHINA_NET_LOGIN_DATA      2055   85b   userid TEA'd, password plaintext
<--  NET_MSG_COMPRESS          170    34b   bCompress=TRUE -> LZO1X
       NET_MSG_LOGIN_FB       2050   120b   nResult=0 EM_LOGIN_FB_SUB_OK
```

**Credentials go to the Agent, not the Login server.** `CLoginServer`'s dispatch
table (`s_CLoginServerMsg.cpp:27-39`) handles only `VERSION_INFO`,
`HEARTBEAT_CLIENT_ANS`, and `REQ_GAME_SVR`. Login is dispatched by `CAgentServer`
(`s_CAgentServerMsg.cpp:63`). Sending login to :12004 hits `default: break` and
is dropped silently.

**Wait for the crypt key before sending credentials.** The Agent pushes
`SND_CRYT_KEY` and `RANDOM_NUM` unprompted in one batch; the spike sends login
only after the whole batch is processed, so the random-pass slot is known first.

**Send an empty random password.** The PC client does: `LoginPage.cpp:199`
declares `strRP` and the block that would populate it is commented out. Accounts
with no secondary password set therefore pass.

**`nResult` is not at body offset 0.** It is a `USHORT` at offset 30, after
`szDaumGID[21]` (`s_NetGlobal.h:3397`). Reading offset 0 gives a plausible-looking
zero regardless of the real verdict.

### Compression

`NET_MSG_COMPRESS` (170) has two distinct meanings:

- `bCompress=false` — a *batching* wrapper: several messages concatenated, no LZO.
- `bCompress=true` — raw **LZO1X-1**, no magic and no length prefix
  (`MinLzo.cpp:112,158`). The decompressed size is not transmitted; the receiver
  supplies a capacity, as the server does with `NET_DATA_BUFSIZE`
  (`RcvMsgBuffer.cpp:113`).

`lzo.js` implements decompression only — the client never needs to compress,
since only the server batches.

### Garbage and `dwSize`

`getOneMsg` subtracts the filler length from the header before dispatch
(`RcvMsgBuffer.cpp:226`, `pNmg2->dwSize -= nGarbageLen`). This matters because
`ChinaMsgLogin` does a strict `sizeof(CHINA_NET_LOGIN_DATA) != pNml->nmg.dwSize`
check (`:276`) — which passes only because the garbage has already been stripped
and the size rewritten. Wire `dwSize` includes the filler; the handler sees 76.

### TEA

`SOURCE/Lib_Network/minTea.cpp` is **XXTEA** (Corrected Block TEA), not classic
TEA, despite the name. The key is **not** the server-supplied one:
`CNetClient::m_Tea` is default-constructed (`s_NetClient.h:117`) and nothing ever
calls `setKey()`, so every client uses the literal `"Steven Seagal Neck Break"`
truncated to 16 bytes. The server-supplied `NET_MSG_SND_ENCRYPT_KEY` value is
encrypted *as data* with that key for heartbeats — it is not itself a key.

The JS port in `spike/tea.js` is verified against the real C++ implementation:
`build.cmd` compiles `minTea.cpp` and emits test vectors into `layout.json`, and
`selftest.js` asserts the JS output matches byte for byte. Round-tripping in JS
alone would not have caught a precedence error in the mix function.

The vectors come in two sets. `teaVectors` is fixed at `USR_ID_LENGTH+1` (21).
`teaVectorsByLen` covers the other lengths the China login uses (20 and 7) —
which is what exposed the overflow bail described above. A self-consistent JS
port that always encrypts would have passed the 21-byte vectors and silently
corrupted the password field.

### Original version handshake

Confirmed against the live `ServerLogin` at port **12004**, footprint of one
connection and one packet:

```
-->  NET_CLIENT_VERSION        type=110   size=16
<--  NET_MSG_VERSION_INFO      type=110   size=16   patch=1 game=1
<--  NET_MSG_SND_ENCRYPT_KEY   type=2102  size=24   12-char TEA key
```

Two things worth recording:

1. The server answers `NET_CLIENT_VERSION` with its **own** `NET_MSG_VERSION_INFO`
   (not `NET_MSG_VERSION_OK` as the enum names suggest).
2. It then pushes `NET_MSG_SND_ENCRYPT_KEY` **unprompted** — the client does not
   have to send `NET_MSG_REQ_RAND_KEY` first. That 12-char value is the TEA key
   (`ENCRYPT_KEY` = 12) for credentials and heartbeats.

This proves header layout, byte order, `dwSize` accounting, and enum resolution
are all correct end to end. Run it yourself with:

```
node spike.js --host <HOST> --port 12004 --allow-remote --version-only
```

### To finish Milestone 1

An account is needed. Create `MOBILE/spike/local.json` (git-ignored):

```json
{ "host": "<HOST>", "port": 12004, "user": "<ACCOUNT>", "pass": "<PASSWORD>",
  "allowRemote": true }
```

then `node spike.js`. Credentials stay out of the shell history and out of any
transcript. A throwaway account is strongly preferable to a real one — failed
logins are recorded in `RanLog`.

## Unity import (Phase 3)

Engine is **Unity URP** — the assumption on record, matching the
`RANMobileAlpha0.0.1a` reference. It has never been confirmed, and it is the one
decision that now gates real work; see §7 of `plan.md`.

`MOBILE/unity/com.ran.mobile.assets` is a Unity package with `ScriptedImporter`s
for the two binaries the extraction pipeline produces. Drop a `.terrain` (with
its `.textures.json` beside it) or a `.navmesh` into `Assets/` and it imports.

**Coordinates pass through unchanged.** The engine is left-handed and Y-up —
`D3DXMatrixLookAtLH`, `PerspectiveFovLH`, `OrthoLH` throughout, with no
right-handed variant anywhere in `Lib_Engine` — which is exactly Unity's
convention. So positions and winding are copied verbatim. **Only V is flipped**,
because D3D puts the texture origin top-left and Unity bottom-left. Contrast the
OBJ exporter, which *does* negate X and reverse winding, because OBJ viewers are
right-handed; getting that backwards yields a mirrored map that still looks
plausible.

Two deliberate choices worth knowing:

- **One mesh per material batch, not per octree node.** The node is the PC
  client's culling unit, but a mid-tier Android GPU cares far more about draw
  calls than about overdraw on static geometry — `w_school_03` is 366 nodes and
  88 materials. The node boxes are still carried onto the prefab
  (`RanTerrainNodes`) so per-node culling can be added later without
  re-importing. A merged batch can exceed 65,535 vertices even though a node
  cannot, so merged meshes switch to 32-bit indices.
- **The navmesh is imported as data, not as a Unity NavMesh.** The server
  validates every movement target against this exact mesh, so a client pathing
  on a Unity-baked surface would differ at the edges — precisely where pathing
  matters — and have its moves rejected server-side.

### Navigation and culling (Phase 3, runtime side)

Two pieces of the renderer are pure logic, so they are written and **executed**
against real data without Unity:

**`RanNavPath`** — A* over navmesh cells plus a funnel pass. Not Unity's NavMesh:
the server validates every movement target against *this* mesh
(`GLCharSkillMsg_SetTarget.cpp`), so a client pathing on a baked surface would
disagree exactly at the edges, which is where it matters. The open set is a
binary heap rather than a sorted list — shipped maps reach 20,220 cells and a
linear scan per expansion turns an interactive query into a visible stall.

Checked against `w_school_03` (the map the Phase 0 spawn was verified against):
every sampled cell centre locates its own cell, paths are found between distant
cells, endpoints land where asked, and **the funnel never lengthens the corridor
it straightened** — which is the whole point of running it. An off-mesh goal
clamps to the nearest cell rather than failing, because a player tapping past a
wall expects to walk up to the wall.

**`RanSkinning`** — bone world transforms, skin matrices, weighted vertex sum,
run over all **2,576 skinned meshes / 78,446 bind poses** (measured
2026-08-20, after the `xmesh.js` object-reference fix — see "Mesh and
animation export" above; was 1,993 / 61,742).

The interesting part is what it does **not** assert. The textbook invariant —
"in the bind pose, `boneWorld * bindPose` is the identity, so skinning returns
every vertex unchanged" — is **false for this data**. Measured across all four
composition orders, the worst identity error is 17–30 units, and the products
are not even constant between bones. A mesh's `.x` frame hierarchy is not the
space its vertices were authored in, so the round trip does not close without
the mesh's own parent-frame transform, which extraction does not keep. Asserting
it would have been a test that fails on correct code.

What is asserted holds across every shipped slot and still catches a dropped
`matrixOffset` or a broken transpose:

- every bind pose is **affine and finite** (0 failures of 61,742)
- skinned vertices are finite, and no vertex loses all its weight
- **skinning stays a convex combination** — weights sum to 1, so the result
  cannot escape the bounding box of the per-bone results. This is precisely what
  a wrong matrix product violates.

2,285 slots (3.7%) have a near-zero determinant. Those are collapsed, unused
bones in the source art, so the check records the count rather than failing.

**`RanTerrainLod`** — detail selection from the same node boxes. Distance is to
the box *surface*, not its centre: a large node whose near edge is underfoot
otherwise reads as far away, drops a level, and pops as the camera crosses in.
Checked on `w_school_03` for the failure that matters — detail never increases
with distance, and a camera moving in 25-unit steps never skips a level.
`ScreenSpaceSize` is included because fixed distance thresholds are wrong the
moment field of view or resolution changes, and a mobile port ships to both.

**`RanFrustumCuller`** — Gribb-Hartmann plane extraction plus a positive-vertex
box test, against the terrain node boxes the importer preserves. Planes are
normalised on extraction: unnormalised ones work for a sign test and silently
lie to any later distance test.

The test is built around the failure that matters. Culling something visible is
a hole in the world; drawing something hidden costs a few triangles. So the box
test is conservative by construction, and the checks assert an
all-encompassing frustum culls **nothing** before asserting a tight one culls
most.

### Characters and animation import too

```
node stage-unity.js --meshes --skinned
Unity.exe ... -executeMethod Ran.Mobile.Assets.Editor.RanCharacterBuilder.ReportBinding
Unity.exe ... -executeMethod Ran.Mobile.Assets.Editor.RanCharacterBuilder.BuildCharacter -ranMesh s_m_dw_wlb
```

**1,993 skinned meshes and 6,498 clips import**, 940 MB staged. (That skinned
count was measured before the `xmesh.js` object-reference fix — see "Mesh and
animation export" above — and was an undercount: 583 of the files this
command would have skipped as "rigid" carry real skin data in the source `.x`
and re-running today staples 2,576. Re-run `--meshes --skinned` and reconcile
against `Assets/Ran/Characters/` before treating that figure as current.)

Which clip belongs to which character **is not recorded anywhere**. A `.ranim`
names bones and nothing else — RAN plays one clip across every skeleton with
matching names — so the pairing has to be established by intersecting bone
names, never guessed from filenames.

Measured: **2,693 of 4,800 sampled mesh+clip pairs share bone names**, and
`s_m_dw_wlb` binds **273 clips**, of which `a_gum_new_nanpi` matches **94 of 94
tracks**. That is the end-to-end proof the two halves agree: bone names survived
`.x` extraction into `.rmesh` AND `AnimContainer` extraction into `.ranim`,
independently, and still line up.

Binding requires more than half a clip's tracks to land, plus at least five. A
single coincidentally-shared bone name would otherwise register as a "clip" that
twitches one finger.

#### Clips become real Unity AnimationClips

```
Unity.exe ... -executeMethod Ran.Mobile.Assets.Editor.RanCharacterBuilder.BuildClips \
  -ranMesh s_m_dw_wlb -ranClipLimit 20
```

`RanAnimationBinder` existed for a long time without ever running. It does now:
**20 AnimationClips written for `s_m_dw_wlb`, 8,220 curves, longest 10.00 s** —
about 411 curves per clip, which is ~41 bones x 10 channels (position xyz,
rotation xyzw, scale xyz).

Checked for the failure that matters rather than assuming success: a clip whose
bone paths do not resolve saves as a **valid but inert** asset and plays as a
static pose. So the builder counts curve bindings and rejects any clip that
produced none. Inspecting `a.anim` directly, the rotation keys vary smoothly
frame to frame and stay unit length — real motion, not a held pose.

158 bone references across those 20 clips bind to nothing, which is expected:
clips are shared across skeletons, so any given character lacks some of the
bones a clip mentions.

One number to revisit before shipping: `a.anim` is **11.9 MB**. Curves are
written uncompressed with a key per source keyframe, and 6,498 clips at that
size is not a mobile build. Compression is a Phase 3 optimisation, not a
correctness problem.

#### Where the clip-to-action mapping lives — `.cfg`, not `.chf`

```
node extract-animtypes.js --list                        inventory + distribution
node extract-animtypes.js --out ../../assets/animtypes.json
node extract-animtypes.js --clip a_m_00_walk            one record
node verify-animtypes.js [--verbose]                    the cross-checks
```

An Animator needs to know which clip is WALK, which is RUN, which is ATTACK.
That is `EMANI_MAINTYPE` (`DxAniKeys.h`: `AN_PLACID`, `AN_WALK`, `AN_RUN`,
`AN_ATTACK`, `AN_DIE`, …).

**It is in the animation `.cfg`, one per clip, next to the `.bin` in
`Animation.rcc`.** 6,191 ship — 6,161 distinct stems, plus 30 in
`SkinObject.rcc` that duplicate stems already there — type string `SANIMCONINFO`,
and they are
what `SANIMCONINFO::LoadFile` reads. **6,037 of the 6,498 shipped clips (92.9%)
now carry a type**, and every check below agrees with data decoded by a
different parser from a different container.

Three homes were ruled out, and one of them was ruled out the hard way:

- **Not in the `.bin`.** `SAnimContainer::Load_0200` reads only counts and
  track lists. `m_MainType` exists on the in-memory container but is never read
  from that file.
- **Not derivable from clip names.** `a_13_run` and `a_elementboss_shock` hint
  at actions; `a_gum_new_nanpi` does not. (Names turn out to be a good
  *validation* signal — see the semantic check below — but they are not the
  source.)
- **Not in `.chf`/`.abf` either, and this is the trap.** Decoding
  `2017_Victors_Wings.abf` with `EMBYTECRYPT_CONTAINER` really does yield the
  exact clip name `a_2017_victors_wings`, which reads like the mapping. It is
  not: `DxSkinCharData::LOAD_0200` and every older `LOAD_*` read that list as
  bare **file names**, nothing more. The engine then calls
  `DxSkinAniControl::LoadAnimation(name)` -> `DxSkinAniMan::LoadAnimContainer`,
  which swaps `.x` for `.cfg` and loads **that** (`DxSkinAniMan.cpp:562-565`);
  `ClassifyAnimation` buckets by the `m_MainType` the `.cfg` supplied. So
  `.chf` answers *which clips a character owns* and `.cfg` answers *what each
  clip is*. Both are needed, neither is sufficient, and the plausible-looking
  clip names inside a `.chf` are the reason it is easy to stop one step short.

`animinfo.js` parses both; `extract-animtypes.js` emits the JSON map;
`verify-animtypes.js` is the cross-check, and it is the point.

##### The key is the file stem, not `m_szName`

The engine addresses a clip by **filename**, never by the name stored inside the
record — `<stem>.cfg`, `<stem>.bin` and the exported `<stem>.ranim` are one
clip. `m_szName` agrees with the stem **6,098 times out of 6,161 (98.98%)**,
which is close enough to look like the key and is wrong: the 63 exceptions are
variant copies (`a_m_e_048_o_m3.cfg` stores `a_m_e_048.x`) plus one authoring
slip (`a_m_17_die.cfg` stores `a_m_01_die.x`). Keying on `m_szName` would
collapse seven distinct `a_m_e_048_o_m*` clips into one and mislabel a death
animation. It is kept as `recordName` for traceability and nothing else.

##### Traps

- **`.chf` and `.abf` are different formats.** They share the encryption gate,
  the "version is the first BODY dword" quirk and most of the field order, so
  one parser looks like it works for both. `.chf` is `DxSkinCharData` with nine
  body versions across three field orders; `.abf` is `DxAttBoneData` (the 2017
  ABL system) with exactly two loaders. Running every `.abf` through the `.chf`
  reader fails outright on **315 of 509** — the good outcome. The bad one is
  `0x0200`, where the two bodies are identical except for a **trailing `BOOL`**
  the `.abf` writer adds, so a single-parser version parses it and is quietly
  4 bytes short of EOF.
- **`SANIMCONINFO` field order changes twice.** 0x0105-0x0107 read
  `flag, sTime, eTime, eTimeOrig, unit`. **0x0108 moves `m_UNITTIME` ahead of
  the three times AND swaps the strike and div blocks, and moves the sound blob
  to the very end**, after the effect list and the ani-scale table. Read the
  new layout in the old order and every record still parses; it just reports
  the wrong duration and the wrong type. That is the positive control in
  `verify-animtypes.js`, and it fires on 1,216 of 1,216.
- **0x0111+ replaced the two fixed 128-byte name arrays with four
  length-prefixed strings** (cfg, x, bin, skeleton). `m_szName` takes the `.x`
  name; the other three are dropped by the engine.
- **`SANIMCONINFO_101` is 304 bytes, not 302.** Its last member ends at 302 and
  the struct is padded to 304, and `LoadFile_0101` blits `sizeof()`. Same class
  of mistake as `SMatrixKey` being 80 rather than 68 — the probe answers it.
- **The effect record writes its type id twice.** `SANIMCONINFO::SaveFile`
  writes the id, then the effect's own `SaveFile` writes it again with version
  and size, so the record is `[typeID][typeID][ver][size][size bytes]`. `size`
  is `sizeof(m_Property)` in all four writers, so the payload skips cleanly on
  every version — which is exactly what the engine does on an unknown one.
- **Five versions never reach EOF, and that is correct.**
  `SAnimationInfoSaveLoad.cpp` labels the 0x0108-0x0110 and 0x0111-0x0115
  loaders *"official version partial read only"* — the shipped writer emits
  trailing data the engine's own reader never touches. 0x0102-0x0108 and
  0x0111 consume to the byte; 0x0109/0x0110/0x0112/0x0114/0x0115 do not. Same
  for `.chf` `LOAD_0107_0108`, which says so in a comment.

##### Verified — measured, from three directions

Section numbers are `verify-animtypes.js` output.

| Check | Result |
|---|---|
| `.cfg` parsed | **6,191 / 6,191**, 0 failures, 0 structurally invalid |
| `.chf` + `.abf` parsed | **1,608 / 1,608**, 0 failures |
| `.cfg` stems that are a real `.ranim` | **6,037 / 6,161 = 97.99%** |
| shipped `.ranim` clips that get a type | **6,037 / 6,498 = 92.91%** |
| `m_dwETimeOrig` == `.ranim durationTicks` | **5,970 / 6,037 = 98.89%** |
| `m_UNITTIME` == 160 ticks | **6,031 / 6,037 = 99.90%** |
| clips named by a `.chf`/`.abf` that resolve | **5,241 / 5,413 = 96.82%** |

The duration row is the one that settles it. `durationTicks` is the highest key
time `xanim.js` found walking the bone tracks in the `.bin`; `m_dwETimeOrig` is
a DWORD at a probe-derived offset in a different file in a different container.
They agree on 5,970 clips. And `m_UNITTIME` is 160 — the ticks-per-frame the
README derived independently by measuring 14.3M inter-key gaps. Neither number
was available to this parser.

The **461 clips with no `.cfg` are not a decode failure.**
`LoadAnimContainer` tolerates a missing one by design
(`DxSkinAniMan.cpp:574-601`): it synthesises a default `SANIMCONINFO`
(`AN_GUARD_N` / `AN_SUB_NONE`) and writes the file out on the spot. They are
reported as absent rather than filled in with a guess.

##### And it means the right thing

Range checks cannot catch a field read one DWORD early — it would still be in
range and still produce a plausible histogram. Clip names are 3ds Max asset
names, entirely outside the binary, so they are evidence from a fourth
direction:

| Name contains | Expected | Measured |
|---|---|---:|
| `_att0` | `AN_ATTACK` | 620 / 631 = **98.3%** |
| `_stemina` | `AN_GUARD_L` | 158 / 163 = **96.9%** |
| `_stay` | `AN_GUARD_N` | 604 / 638 = **94.7%** |
| `_strock` | `AN_SHOCK` | 64 / 69 = **92.8%** |
| `_run` | `AN_RUN` | 486 / 531 = **91.5%** |
| `_die` | `AN_DIE` | 390 / 437 = **89.2%** |
| `_walk` | `AN_WALK` | 459 / 524 = **87.6%** |

And the loop flag splits the way physics requires: **ACF_LOOP is set on 96.3%
of AN_WALK clips and 3.9% of AN_DIE clips.** That is `m_dwFlag` at the right
offset *and* `ACF_LOOP` resolved to the right bit; either being wrong would not
separate the two categories.

The distribution over 6,161 clips, dominated by exactly what a fighting game
needs:

| | | | | | |
|---|---:|---|---:|---|---:|
| AN_ATTACK | 1,124 | AN_GUARD_N | 927 | AN_WALK | 509 |
| AN_RUN | 493 | AN_DIE | 408 | AN_GESTURE | 384 |
| AN_SHOCK | 368 | AN_SHOCK_MIX | 349 | AN_GUARD_L | 316 |

Five main types ship no clips at all: `AN_GATHERING`, `AN_FLIGHT`, `AN_CAR_B`,
`AN_CAR_C`, `AN_CAR_D`.

`o_m.chf` — the male player character — names **566 clips, 100% of them
typed**, spread across 25 main types. That is a complete Animator input for a
real character, not a sample.

##### What the probe had to answer

`SAnimationInfo.h`, `CharacterSound.h`, `DxAniBoneScale.h` and
`DxSkinCharData.h` were added to the layout probe. Nothing here is
hand-computed:

| Struct | Size | Why it is a stride |
|---|---:|---|
| `SANIMCONINFO_101` | **304** | blitted whole by `LoadFile_0101` (payload ends at 302) |
| `SANIMCONINFO_102` | 564 | blitted whole |
| `SANIMCONINFO_103` | 732 | blitted whole |
| `SANIMCONINFO_104` | 840 | blitted whole |
| `SANIMSTRIKE` | 12 | `ReadBuffer(..., sizeof x 9)` in every 0x0104+ loader |
| `SChaSoundData_102` | 260 | blitted |
| `SChaSoundData_103` | 428 | blitted |
| `SChaSoundData` | 388 | blitted |
| `SBONESCALE_100` | 40 | blitted in `.chf` `LOAD_0103`/`LOAD_0104` |

Two codegen fixes came out of this and apply to everything the probe measures:

- **`gen-structs.js` lost every member declared after a comma.**
  `DWORD m_dwETime, m_dwETimeOrig;` yielded only `m_dwETimeOrig`, so
  `m_dwETime` — the clip's end time — had no measured offset at all. It now
  splits declarators, but only when the declaration has no angle brackets, or
  `std::map<DWORD,float> m` would produce a phantom member called `DWORD`.
  +7 members, no new exclusions.
- **`gen-enums.js` now takes a list of headers and only reads enums at
  namespace scope.** `DxAniKeys.h` and `SAnimationInfo.h` were added for
  `EMANI_MAINTYPE`/`EMANI_SUBTYPE`/`ACF_*` (1,825 members now). The scope filter
  is load-bearing: both new headers declare `enum { VERSION = 0x0200 }` *inside*
  a struct, and unlike the struct codegen there is no self-healing exclusion
  loop here to catch the resulting compile error. Verified to remove nothing
  that was previously emitted.

`EMANI_MAINTYPE` also needed care on the JS side: it overlays **four
independent numbering schemes** on one C++ enum — character, ABL, vehicle, and
the generic `AN_SUB_nn` ladder — so `AN_GUARD_N`, `AN_ABL_STAY` and
`AN_VEHICLE_STAY` are all 0. A value -> name map is ambiguous until the scheme
is chosen. `animinfo.js` names the character block by hand and takes every
value from the compiler: naming is safe, valuing is not.

##### Not verified

- The `.chf`/`.abf` **body-piece list and bounds** are parsed but nothing
  cross-checks them; only the animation list and skeleton name are corroborated
  (by resolving against `.ranim` and against the skeleton names in the `.cfg`).
- The **trailing bytes** on the five partial-read `.cfg` versions and on `.chf`
  `0x0107`/`0x0108` are not identified. The engine does not read them and
  neither does this, so the fields are unknown rather than known-empty.
- `m_dwSTime`/`m_dwETime` are read but only `m_dwETimeOrig` is corroborated
  against the `.bin`. Measured: `m_dwETimeOrig - m_dwETime` is exactly **160**
  (one frame) on 4,532 clips and **0** on 1,519, with a long thin tail of larger
  multiples of 160 — consistent with `m_dwETime` being a trimmed/exclusive play
  end and `m_dwETimeOrig` the authored length. **That interpretation is
  inferred, not measured**; the exporter uses `m_dwETimeOrig`, which is the one
  the `.bin` agrees with.
- The 30 `.cfg` in `SkinObject.rcc` all duplicate stems already in
  `Animation.rcc`; they are parsed but add no clips, and no check compares the
  two copies.

#### Constant-channel collapse: 11.9 MB -> 3.8 MB per clip, lossless

The first generated clip was 11.9 MB, and 6,498 of those is not a mobile
build. Rather than reach for lossy keyframe reduction, measure first — across
the whole corpus of 397,523 tracks:

| Channel | Constant for the whole track |
|---|---:|
| Scale | 311,629 of 312,122 — **99.8%** |
| Position | 269,198 — 86% |
| Rotation | 110,469 — 35% |

**73.8% of all channels never change.** Writing them as full curves is what
made clips enormous. `RanAnimationBinder` now collapses any channel whose keys
all equal the first into a single key.

This is lossless — Unity samples a one-key curve as that constant — and it is
not the same as dropping the channel. A dropped channel leaves the bone at
whatever the previously-played clip left it at; one key pins it.

Result on `a.anim`: **11,931,781 -> 3,774,845 bytes, 68% smaller**, with 132 of
its 192 curves collapsed and the other 60 keeping full motion (up to 226 keys).
Verified by inspecting the output, not inferred from the size drop.

Further compression would now have to be lossy (key decimation, quantisation),
so it is a separate decision with a quality cost, not a free win like this one.

### The world imports into Unity

```
Unity.exe -batchmode -quit -projectPath MOBILE/unity/RanMobile   -executeMethod Ran.Mobile.Assets.Editor.RanMapAssembler.AssembleAll
Unity.exe ... -executeMethod Ran.Mobile.Assets.Editor.RanSceneBuilder.BuildAndRender -ranMap w_school_03
Unity.exe ... -executeMethod Ran.Mobile.Assets.Editor.RanDiagnostics.ReportMaterials -ranMap w_school_03
```

**130 scenes built, 0 failed, 21,716 renderers total.** Each carries the map
prefab, a directional light, and a camera framed to the map bounds. Exactly one
map — `character_slt_new` — has no renderers, and that is correct: it ships a
navmesh and nothing else.

**Materials verified by data**, not by eye: `RanDiagnostics` reports 87 of 88
renderers on `w_school_03` carrying `Universal Render Pipeline/Lit` with a
bound base texture and white base colour, and **6,819 of 6,861 textures resolve
across all 130 maps** — the 42 misses being the never-shipped `nms*_mas` art the
audit already flags. The 88th batch is the one with an empty texture name, which
the extractor found in the source.

#### Two URP traps, in order

**Materials bake their shader at import.** The first import ran before any URP
pipeline asset existed, so `Shader.Find("Universal Render Pipeline/Lit")`
returned null and everything fell back to built-in `Standard` — which renders
**magenta** under URP. Adding the pipeline asset afterwards does not repair
those materials; they must be reimported, which means bumping the
`ScriptedImporter` version. `RanUrpMaterial` now warns loudly on that fallback
instead of silently producing a magenta world.

**Headless capture under URP does not work via `Camera.Render()`.** That is a
built-in-pipeline API; URP renders through its own intermediate targets and a
blit it does not drive. The symptom is specific: the clear colour is correct
while all geometry resolves to a single saturated primary, and which primary
changes between identical runs. Diagnosed rather than assumed — the material
report above is what ruled out the asset side. A supported capture needs
`SubmitRenderRequest` (Unity 2022+) or a play-mode frame. **To look at a map,
open the generated scene in the editor.**

### How the world imports

```
Unity.exe -batchmode -quit -nographics -projectPath MOBILE/unity/RanMobile \
  -executeMethod Ran.Mobile.Assets.Editor.RanMapAssembler.AssembleAll
```

From a **cold** import (no `Library/`): 101 terrain, 121 navmesh and 31
map-object assets imported, then **130 map prefabs built with 0 missing terrain,
0 missing objects, 0 missing navmesh, and 0 errors**. `w_school_03.prefab` has 92
GameObjects and **88 MeshRenderers** — exactly its 88 material batches;
`blue_zone1.prefab` is 4 MB for its 1,454 placed objects.

#### A ScriptedImporter cannot reference another importer's output

This is the bug that only a real Unity run could find, and the stub harness
never could.

The obvious design — have `RanMapImporter` load the terrain/navmesh assets and
parent them under the map root — compiles, type-checks, and **fails on a cold
import**: Unity has not necessarily produced those artifacts yet.
`AssetImportContext.DependsOnArtifact` looks like the fix and is not; measured
after adding it, **61 of 130 maps still had no terrain and 65 no navmesh**. It
registers a dependency for *reimport* purposes, not an ordering guarantee.

What works is removing the race instead of narrowing it. `RanMapImporter` now
records only asset **paths** into `RanMapInfo`, and `RanMapAssembler` links
everything in a post-import pass, when the import queue has drained and every
artifact is guaranteed to exist. Importers stay independent, which is the rule
the Unity asset pipeline actually enforces.

Two related things worth remembering: a second import pass "fixes" the warnings
purely because nothing re-imports, so **only a cold run tells the truth**; and
`touch` will not force a reimport because Unity hashes content — bump the
`ScriptedImporter` version instead, which is correct after changing importer
logic anyway.

### The Unity project (Phase 3 — staged, then imported)

```
node convert-textures.js --out ../../assets/textures   # 16,232 -> PNG
node stage-unity.js --map w_school_03                  # into unity/RanMobile
```

`MOBILE/unity/RanMobile` is a real project authored as files, not through the
editor: `ProjectVersion.txt` pinned to **2021.3.45f2**, a `manifest.json` with
URP 12.1.15 and a `file:` reference to `com.ran.mobile.assets`, and
`w_school_03` staged with all **87 of its textures resolved, 0 missing**.

Unity 2021.3.45f2 and Unity Hub are installed (winget), the Personal licence is
activated, and the project imports clean.

#### Two real bugs found by staging, before Unity ever ran

- **`ScriptedImporter` matches only the LAST extension.** `w_school_03.map.json`
  registers as `json`, so `RanMapImporter`'s declared `mapjson` would never have
  fired — and declaring `json` instead would hijack every JSON asset in the
  project. Manifests are staged as `.mapjson`.
- **AssetDatabase paths are case-sensitive, even on Windows.** Manifests name
  `terrain/`, `mapobj/`, `navmesh/`; staging into a prettier `Terrain/` would
  make `RanMapImporter` silently resolve nothing. Staging now uses each part's
  own `file` field verbatim.

**All 130 maps are staged**: 130 manifests, 202 terrain files, 31 map-object
files, 121 navmeshes, 3,011 distinct textures, 1,085 MB. 74 texture references
go unresolved — the same seasonal `nms*_mas` art the audit already flags as
never shipped.

#### URP materials: setting `_Surface` is not enough

`RanUrpMaterial` exists because the obvious code is wrong in a way that renders.
Setting `_Surface = 1` is only what the *inspector* reads back; URP's renderer
keys off the blend factors, the ZWrite flag, the render queue and a set of
keywords, all of which `LitShaderGUI` writes together when a human flips the
dropdown. An importer that sets `_Surface` and stops produces a material that
**reports transparent and renders opaque**.

It also splits the alpha buckets, which the first version did not: bucket 1 is
hard ALPHA — foliage and fences — and wants alpha-**test**, so it still writes
depth and sorts against terrain. Buckets 2–4 are the SOFT alpha lists (glass,
light shafts) and want real blending. Treating them alike makes every fence
sort by draw order.

Every property goes through a `HasProperty` guard, because these names have
moved between URP versions and one missing property must not throw mid-import
and abort a whole map. Base texture is written to **both** `_BaseMap` and
`_MainTex`: `mainTexture` alone relies on URP tagging `_BaseMap` as
`[MainTexture]`, which is true in URP 10+ but is exactly the kind of
version-dependent assumption worth not making.

#### Texture staging has to flatten, and flattening collides

References carry no directory — the engine resolves by basename — while
`convert-textures.js` mirrors the source tree. Of 16,072 converted PNGs there
are only **15,014 distinct basenames, so 1,058 collide**. `stage-unity.js`
resolves deterministically, preferring `textures/` over `data/` over `cache/`,
and reports the count so a wrong pick is visible rather than silent. The engine
has the same ambiguity and resolves it by search order.

### The C# is compiled and the readers are executed

`MOBILE/unity/typecheck` has two projects, because they answer different
questions:

```
dotnet build MOBILE/unity/typecheck/TypeCheck.csproj   # does it compile for Unity?
dotnet run  --project MOBILE/unity/typecheck/RunCheck.csproj -- MOBILE/assets
```

- **`TypeCheck.csproj`** compiles the whole package — readers and importers —
  against stub assemblies whose signatures mirror Unity's, targeting
  **`netstandard2.1`**. That target is the point: it is Unity 2021.3's actual
  profile, and building against `net8.0` instead would happily accept APIs Unity
  does not have.
- **`RunCheck.csproj`** *executes* the readers over the real exported assets.
  The Unity value types (`Vector3`, `Matrix4x4`, `Quaternion`, `Bounds`) are
  real implementations rather than inert stubs, so the decode paths genuinely
  run. **36 checks pass**: the readers independently reproduce every total the
  Node exporters wrote — 15,514,004 terrain vertices, 9,183,803 mesh vertices
  (measured 2026-08-20 after the `xmesh.js` object-reference fix; was
  8,437,143 — see "Mesh and animation export" above), 15,452,358 animation
  keys, 14,831 placed objects — and re-verify the
  invariants (node-local indices, vertices inside their node AABB, weights
  summing to 1, unit quaternions, attribute ranges in faces).

Targeting Unity's real profile immediately earned its keep: **`Encoding.Latin1`
does not exist in `netstandard2.1`** — it arrived in .NET 5 — so all three
readers would have failed to compile inside Unity. They now decode latin-1 by
hand in `RanBinary`, which is a widening cast per byte and depends on no runtime
encoding table at all.

The matrix transpose gets a dedicated check, because a wrong one still produces
a perfectly valid matrix and would simply put every bone and object in the wrong
place: a known row-major D3D matrix is built by hand and the reader must land
its translation in Unity's **fourth column**.

**What this still does not prove.** The Editor importers compile but have never
run — `ScriptedImporter`, `AssetDatabase` and material setup need a live Unity.
A URP shader property name that changed between versions, or an import-time
Unity API quirk, would not be caught here.

Alongside that, every byte offset the C# hardcodes is *also* pinned from the Node
side in `test.js` ("export format spec"), so the two sides have to agree
independently. That spec check caught a real bug: 8 of 185,041 UVs in
`w_school_03` are `NaN`, all inside the 24 untextured meshes where the editor
left uninitialised memory nothing would ever sample. Harmless in the source; in
Unity a single NaN poisons a mesh's bounds and silently kills culling for the
whole batch. The exporter now flattens them, verified zero across all 101 files
/ 15.5M vertices.

### Note on cfg/

`cfg/` contains plaintext ODBC credentials (`sa` and password). Worth handling
before this tree is shared or pushed anywhere.
