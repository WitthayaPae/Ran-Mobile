# Packet-layout parity report — phase 1 exit gate

**Result: PASS.** 1,286 of 1,288 network message structs are byte-identical between the
server's ABI (MSVC x86) and the mobile build (arm64). The 2 exceptions never carry client
wire data (details below).

## Why this gate exists

The live server is a 32-bit MSVC binary. Every packet is a `#pragma pack`-ed struct sent
with `nmg.dwSize = sizeof(*this)`. On arm64 `long` is 8 bytes (4 on Win32), pointers are 8,
and 64-bit members align differently — so a single wrong `sizeof` silently desyncs the
protocol. Nothing else in the port can be trusted until this matches.

## Method

1. `gen-probe.sh` scans `Lib_Client/G-Logic/GLContrl*.h` and generates `probe.cpp`, which
   takes `sizeof()` of all **1,288** message structs (a plain `#include` is not enough —
   clang only lays out records that are actually used).
2. `run.sh` compiles that probe for **aarch64-linux-android** and dumps every record layout
   (`-Xclang -fdump-record-layouts`).
3. `../../tools/layout-probe/msvcsizes.cmd` compiles the *same* struct list with the **real
   MSVC x86 toolchain** (VS 2022, `vcvars32`) and prints ground-truth sizes. This is the
   authority — not an inference about ABI rules.
4. The two lists are joined and any difference is a bug.

Re-run any time with:
```
MOBILE/native/layout/gen-probe.sh          # regenerate after adding message headers
MOBILE/native/layout/run.sh                # arm64 dump
MOBILE/tools/layout-probe/msvcsizes.cmd    # MSVC x86 ground truth  (needs VS 2022)
```

## What the first run found — and the fix

5 real mismatches:

| struct | MSVC x86 | arm64 (before) |
|---|---|---|
| SNETPC_MAPWEATHER | 908 | 1036 |
| SNETPC_MAPWHIMSICALWEATHER | 36 | 40 |
| SNETDROP_PC | 1728 | 1736 |
| GLCHARAG_DATA | 328 | 352 |
| SNET_GET_ITEMSHOP_FROMDB_FB | 81 | 105 |

Root cause of the first three: **`CTime`**. MFC lays `CTime` out with 4-byte alignment on
x86 — its `__time64_t` member does *not* force the struct to 8-byte alignment there. The
shim's `CTime` was a plain class, so on arm64 it aligned to 8 and pushed every following
member. `SONEMAPWEATHER` embeds a `CTime`, and `SNETPC_MAPWEATHER` holds an array of 32 of
them → 32 × 4 = the 128-byte error.

Fix: `#pragma pack(push, 4)` around `CTime`/`CTimeSpan` in `shim/win/mfc_compat.h`.
After that: **0 mismatches among structs that carry wire data.**

## The 2 remaining differences (not bugs)

- **`GLCHARAG_DATA`** (328 vs 352) — holds `std::map<std::string,SFRIEND>` and
  `std::vector<USER_ATTEND_INFO>`, so its size legitimately differs between 32- and 64-bit.
  It is never sent by value: the only message that references it uses
  `sizeof(GLCHARAG_DATA*)` (`GLContrlCharJoinMsg.h:200`) — a pointer in an in-process
  agent-server message, not a network packet.
- **`SNET_GET_ITEMSHOP_FROMDB_FB`** (81 vs 105) — contains `ITEMSHOP`, which holds
  `std::string`. It does set `nmg.dwSize = sizeof(...)`, but it is a server↔DB message
  (`FROMDB`) exchanged between two MSVC x86 server processes. The client neither sends nor
  parses it. **If that ever turns out to be wrong, this one is a live protocol bug** — worth
  re-checking when the login/shop path is exercised.

## Standing rule for the rest of the port

Anything added to the shim that ends up *inside* a packet struct must reproduce the MSVC x86
layout, not the "natural" arm64 one. `CTime` was the first such trap; `CTimeSpan`, and any
future `CString`/`CRect`-like type that lands in a wire struct, are the same class of risk.
Re-run this gate after every shim change that touches a type used in `GLContrl*Msg.h`.
