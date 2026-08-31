# Gameplay sweep

Exercising surfaces past "the window opens". Findings are logged here as they
appear and fixed in one batch afterwards, rather than fix-rebuild-relogin per
bug — the server drops a session on every reconnect.

Run on LDPlayer against `out/ran-phase3.apk`. One login per pass.

---

## 2026-08-31 / 09-01 — pass 1: NPC dialogue, shop, quest list, teleport

| # | surface | result |
|---|---------|--------|
| 1 | NPC dialogue | **works** — tap the NPC, the reply window opens with its text and branch list, branches are tappable |
| 2 | Shop, open | **works** — the shop branch opens `ร้านค้า` alongside the inventory, tabs and item tooltips render |
| 3 | Shop, buy | **works**, subject to the keyboard — see F1 |
| 4 | Shop, sell | **works** — hold to lift, drag onto the shop grid, release, confirm. Paid correctly |
| 5 | Quest list | **works** — `ภารกิจ` opens with the accepted-quest list, Thai renders correctly |
| 6 | Teleport (start-point card) | **works** — the sheet offers ใช้งาน, the card fires, position moved SG 64/9 → 61/7, world reloaded, no crash |
| 7 | Trade | **not tested** — needs a second player on the same server |
| 8 | Quest turn-in | **not tested** — needs a completable quest |
| 9 | Death and resurrect | **not tested** |
| 10 | Cross-map zone change | **not tested** — the start-point card stays on the same map |

### F1 — the buy quantity prompt, and why it stays as it is

Tapping a stackable item in the shop opens the client's number modal:
*"โปรดใส่จำนวนที่คุณต้องการซื้อ"* with a text field and OK / Cancel. On the
emulator nothing appears to type with, so buying looks impossible.

**Decision: keep the PC behaviour.** A mobile-specific stepper sheet was built
and then reverted — buy and sell now follow the PC client exactly.

The keyboard path already exists and is wired correctly. Traced end to end:

- `CModalWindow` calls `m_pEditBox->BeginEdit()` when a `MODAL_INPUT` opens.
- `CUIEditBox::BeginEdit` calls `RanIME_Show()` under `RAN_MOBILE`.
- `RanIME_Show` calls `imeCall(true)`, which goes at `InputMethodManager`
  directly with `showSoftInput(decorView, SHOW_FORCED)` — the NDK workaround,
  because a `NativeActivity` has no focusable `View` for the polite call.

Verified with a temporary probe: `editbox: BeginEdit accepted`, and
`dumpsys input_method` reports **`mInputShown=true`**. The system believes the
keyboard is up.

It does not draw, and that is an **emulator artifact**: LDPlayer's only
installed IME is `com.android.inputmethod.pinyin`, which does not render over a
fullscreen NativeActivity surface here. There is no other IME to switch to.

So **this surface cannot be verified on the emulator** and has to be checked on
the Tab S9. The evidence that it works on a real device is that the login ID and
password fields take the Android keyboard there — the note in STATUS about taps
hitting "the Android keyboard" during login is exactly that keyboard appearing.

Probes removed; `SOURCE` is clean.

### Note on testing the carry gesture by hand

`input swipe` moves the pointer immediately, so it never crosses the 450 ms
long-press threshold and never lifts the item — the first drag-to-sell attempt
looked like a failure for that reason. The gesture has to be driven as
`motionevent DOWN`, a real pause, then `MOVE`s and `UP`.

Also: after the lift the item leaves the inventory grid, and a mid-drag
screenshot looks exactly like item loss. It is not — the confirmation dialog is
on screen elsewhere. Check the whole frame before concluding anything is gone.
