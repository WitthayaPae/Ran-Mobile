# Which byte-crypt table applies to which file

Derived from the **live** (uncommented) `SetEncodeType` call sites in
`SOURCE/Lib_Engine`. This matters because the table name does **not** reliably
follow the file extension, and because most call sites in the tree are
commented out — a format having a table defined does not mean it is used.

Find the live ones with:

```
grep -rn "SetEncodeType" --include=*.cpp Lib_Engine Lib_Client \
  | grep -vE "^\S+:[0-9]+:\s*//"
```

## Confirmed against real data

| Format | Table | Encoded region | Evidence |
|---|---|---|---|
| Animation `.bin`, version **< 0x0200** | *(none)* | — | bone names readable in the raw body; applying any table destroys them |
| Animation `.bin`, version **>= 0x0200** | `EMBYTECRYPT_BIN2` | from byte 132 | decodes to `Bip01_Spine1`, a standard biped bone |
| `.egp`, inner version **< 0x0200** | *(none)* | — | names readable raw (3,129 files) |
| `.egp`, inner version **>= 0x0200** | `EMBYTECRYPT_EGP` | from byte **136** | names appear only after decoding (1,165 files) |
| `.lev` (Level) | *(none)* | — | every `SetEncodeType` for it is commented out |
| `.wld` (terrain) | *(none)* | — | same — `DxLandManSaveLoad.cpp:78`, `DxStaticMesh.cpp:668,691` |
| Animation `.cfg`, header version **< 0x0200** | *(none)* | — | all 6,191 shipped files are 0x0102–0x0115, i.e. below the gate; they parse raw and the clip names come out clean |
| Animation `.cfg`, header version **>= 0x0200** | `EMBYTECRYPT_CFG` | from byte 132 | implemented from `SAnimationInfoSaveLoad.cpp:45`; **no shipped file reaches it**, so the branch is unexercised |
| `.chf` / `.abf`, body version **< 0x0200** | *(none)* | — | 1,378 files; names readable raw |
| `.chf` / `.abf`, body version **>= 0x0200** | `EMBYTECRYPT_CONTAINER` | from byte **136** | 230 files; animation and piece names appear only after decoding |

**The version-gate pattern is the norm, not the exception.** All five formats
checked so far follow it: encode only when the file's own version reaches
`VERSION_ENCODE`, which is `0x0200` in every case. Older files in the same
archive are plaintext. Always read the version and branch — never assume a
whole format is encoded.

**A per-byte substitution is stateless**, so a wrong start offset cannot shift
or corrupt the stream downstream of it. Getting the offset right only matters
for the plaintext fields *before* the encoded region — for `.egp`, decoding from
132 instead of 136 leaves the body byte-identical but mangles the version DWORD.

The animation gate is `dwVersion >= VERSION_ENCODE` where `VERSION_ENCODE` is
`0x0200` (`SAnimationInfo.h:227`), at `SAnimationSaveLoad.cpp:401`. In the
shipped data that means **8 files are encoded and 6,520 are not**.

There is a second branch at `SAnimationSaveLoad.cpp:382` that applies
`EMBYTECRYPT_BIN` regardless of version — but only when the file *path* contains
the literal `v2-1`. No shipped archive path does, so it never fires here.

## Identified from live call sites, not yet verified against data

| Format | Table | Call site |
|---|---|---|
| Skin pieces (`.cps`) | `EMBYTECRYPT_PIECE` | `DxSkinPieceSaveLoad.cpp:47,1832` |

`.chf`/`.abf` and `.cfg` moved to the confirmed table above.

Three things that generalise from confirming them:

- **The version is not always in the CSerialFile header.** `.cfg` keeps it there
  (byte 128) and `.chf`/`.abf` do not — their header says type `"default"`,
  version `0`, and the real version is the first BODY dword at 132, exactly like
  `.cps`. Reading the header version gives 0, every file then looks unencrypted,
  and the 230 encoded ones yield nothing.
- **The encoded region starts after the field the loader already read.**
  `DxSkinCharData::LoadFile` reads `dwVer` and *then* calls `SetEncodeType`, so
  the version dword stays plaintext and the body begins at **136**, not 132 —
  the same off-by-one as `.egp`.
- **A gate can be defined and never reached.** Every shipped `.cfg` is version
  0x0102–0x0115, all below `VERSION_ENCODE`, so `EMBYTECRYPT_CFG` never fires on
  real data. The branch is implemented from the source and is **unexercised**;
  do not record it as verified.

## `.wld` / `.wld0` use a different mechanism entirely

Not the substitution tables. `SetEncode(EMBYTECRYPT_OLD, EMENCODE_WLD)` passes a
dummy non-zero `EMBYTECRYPT_OLD` purely to enter the branch; the `EMENCODE_WLD`
test then wins and `byte_decode` is never called (`SerialFile.cpp:227-234`).
`EMBYTECRYPT_WLD` (4) and `EMBYTECRYPT_WLD2` (23) exist in the table set and are
**never used** — every `SetEncodeType(EMBYTECRYPT_WLD)` call site is commented
out.

`CWLDCrypt::Decryption_WLD` and `Decryption_WLD0` are **byte-identical** on the
live path (`WLDCrypt.cpp:32-88`), so one implementation covers maps and
sidecars:

```
decrypt: b = ((b - 0x10) & 0xFF) ^ 0x10
encrypt: b = ((b ^ 0x10) + 0x10) & 0xFF
```

The constants read `WLD_XOR_DATA = 0x99701AE` and `0x92617BE`, but they are
`int`s assigned into a `BYTE`, so only `0xAE` and `0xBE` survive and their XOR
is `0x10`.

Selection is by **type string**, not a version gate:

| Extension | Plain | Encrypted |
|---|---|---|
| `.wld` | `LAND.MAN` | `Land.Man` |
| `.wld0` | `default` | `Default_Crypt` |

**Test for the encrypted spellings explicitly, never by exclusion.**
`login_2.wld0` ships plaintext under the `.wld` spelling `LAND.MAN`; treating
"not the expected plain name" as encrypted corrupts it.

Only bytes from 132 onward are enciphered — the header is written with raw
`fwrite`, bypassing the codec, which is what makes the magic readable and usable
as the flag.

## The general shape

Encrypted data files share a `CSerialFile` header:

```
0    128 bytes   file type string, NUL-padded   ("AnimContainer", "EFF_PROPGROUP", "glmap", …)
128    4 bytes   u32 version
132              body — encoded only if the loader called SetEncodeType
```

The header itself is always plaintext: it is read *before* `SetEncodeType`, which
is what makes the type and version usable to pick the table in the first place.
