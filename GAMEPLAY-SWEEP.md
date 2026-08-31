# Gameplay sweep

Exercising surfaces past "the window opens". Findings are logged here as they
appear and fixed in one batch afterwards, rather than fix-rebuild-relogin per
bug — the server drops a session on every reconnect.

Run on LDPlayer against `out/ran-phase3.apk`. One login per pass.

---

## 2026-08-31 — pass 1: NPC dialogue, shop, quest list

| # | surface | result |
|---|---------|--------|
| 1 | NPC dialogue | **works** — tap the NPC, the reply window opens with its text and branch list, branches are tappable |
| 2 | Shop, open | **works** — the shop branch opens `ร้านค้า` alongside the inventory, tabs and item tooltips render |
| 3 | Shop, buy | **works, but the quantity prompt is unusable** — see F1 |
| 4 | Shop, sell | **works, but undiscoverable** — see F2 |
| 5 | Quest list | **works** — `ภารกิจ` opens with the accepted-quest list, Thai renders correctly |
| 6 | Trade | **not tested** — needs a second player on the same server |
| 7 | Quest turn-in | **not tested** — needs a completable quest |
| 8 | Death and resurrect | **not tested** |
| 9 | Zone change | **not tested** |

### F1 — buying asks for a quantity through a keyboard that does not exist

Tapping an item in the shop opens the client's number modal: *"โปรดใส่จำนวนที่คุณ
ต้องการซื้อ"* with an empty text field and OK / Cancel. Nothing brings up a soft
keyboard, and the client's own on-screen keypad was removed during the port, so
**a player cannot type a number and cannot buy anything.**

The transaction itself is fine. Injecting `3` with `adb shell input text` and
pressing OK completed the purchase: money went 100,452,739 → 100,452,583, and
three potions arrived in the bag. So only the input is missing.

This is the same problem `CMobileCountSheet` was built to solve for splitting a
stack — steppers instead of a text field. `ITEM-TOUCH-PLAN.md` records buy and
sell counts as still using "the client's own confirm path", which is true and is
exactly the gap. Route the buy prompt through the count sheet.

Severity: **blocking**. Shops are unusable.

### F2 — selling works, but nothing tells you how

With the shop open, tapping an inventory item opens the mobile action sheet with
ใช้งาน / ลิงก์ในแชท / ทิ้ง / ปิด. **There is no sell row**, even though the item's own
tooltip says `โยนร้านค้า:สามารถทำได้` — it can be sold.

Selling does work, by the carry gesture: hold the item for 450 ms to lift it,
drag onto the shop grid, release. That raises the client's own confirmation
("คุณต้องการขาย ขนมปัง หรือไม่ ?"), and confirming pays — money 100,452,583 →
100,452,588 for one bread.

So the mechanism is sound and safely gated; it is simply invisible. A player who
only ever taps will conclude they cannot sell. Add a sell row to the sheet when
a shop is open, alongside the existing เอากลับ/ใส่ contextual rows.

Severity: **high** — a core loop is unreachable without guessing.

### Note on testing this by hand

`input swipe` moves the pointer immediately, so it never crosses the 450 ms
long-press threshold and never lifts the item — the first drag-to-sell attempt
looked like a failure for that reason. The gesture has to be driven as
`motionevent DOWN`, a real pause, then `MOVE`s and `UP`.

Also: after the lift, the item leaves the inventory grid and there is no carried
icon visible while a confirmation is pending, so a mid-drag screenshot looks
exactly like item loss. It is not — the confirm dialog is on screen elsewhere.
Check the whole frame before concluding anything is gone.
