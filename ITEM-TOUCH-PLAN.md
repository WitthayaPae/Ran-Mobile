# Touch item interaction — design (no code yet)

Written 2026-08-30. Section 1 is read out of `SOURCE/` with file:line, not
remembered.

---

## 1. What the PC actually does

The whole item system rests on one idea: **the cursor carries an item, and the
carried item lives on the server**. `SLOT_HOLD` (`GLItemDef.h:245`) is a real
equipment slot; `GET_HOLD_ITEM()` (`GLogicEx.h:694`) is what `CItemMove` draws
under the pointer (`ItemMove.cpp:36-51, 92-156`). There is no client-side drag
buffer and no drag threshold: a "drag" is two independent clicks any distance
apart in time. Trade, rebuild, mix and transfer each have their own carried
slot (`GET_PRETRADE_ITEM`, `GET_PREHOLD_ITEM`, `GLCharData.h:1077, 1090`).

Two supporting mechanisms matter for touch:

* **Snap** (`InnerInterface.h:1052-1054`, set from `InnerInterfaceMsg.cpp:353-386`)
  parks the carried icon on whichever slot the pointer is over, instead of
  letting it follow the cursor. It is driven entirely by hover.
* **First item slot** (`InnerInterface.h:1056-1058`) makes only the topmost
  slot under the pointer respond; every slot starts with
  `if ( !IsFirstItemSlot() ) return;`.

Double click exists — `DxInputDevice.cpp:1021` synthesises `DXKEY_DUP` from a
second release within 330 ms — but **no item cell uses it**. Cells test bare
`UIMSG_LB_UP`, so the second half of a double click is silently dropped;
buttons use `CHECK_LB_UP_LIKE` (`UIControlMessage.cpp:32`), which folds
`LB_UP|LB_DUP` into one. There is **no SHIFT behaviour on items anywhere**.

### The 27 distinct item gestures

| # | gesture | effect | where |
|---|---|---|---|
| 1 | hover a slot | tooltip + highlight + icon snap | `InventoryWindow.cpp:175` |
| 2 | CTRL + hover | tooltip with the random-option block | `CharacterWindowChar.cpp:309` |
| 3 | LB down on a cell | arms move/split (`SetSplitPos`) | `InventoryWindow.cpp:289` |
| 4 | LB up, same cell | pick up / put down / swap — `ReqInvenTo` | `:273`, `GLCharactorReq.cpp:528-616` |
| 5 | LB up, other cell, stackable | **quantity modal** `MODAL_SPLIT_ITEM` | `:269`, `InnerInterfaceModalMsg.cpp:1060` |
| 6 | LB on an equipment slot | equip from hand / unequip to hand — `ReqSlotTo` | `:335` |
| 7 | RB on an inventory item | equip, else consume — `ReqInvenToWear` → `ReqInvenDrug` | `:281` |
| 8 | RB on a worn item | apply held colour card, else unequip to a free cell | `:342-355` |
| 9 | RB on a storage item | consume in place — `ReqStorageDrug` | `StorageWindow.cpp:209` |
| 10 | LB on a bank item | withdraw — `ReqChargedItemTo` | `ItemBankWindow.cpp:115` |
| 11 | LB on a shop item, hand empty | buy confirm, or buy-count modal for stacks | `MarketWindow.cpp:244-266` |
| 12 | CTRL + LB on a shop item | buy with **no** confirm | `:232` |
| 13 | LB **or** RB on the shop grid, hand full | sell confirm `MODAL_SELL_ITEM` | `:168-188` |
| 14 | CTRL + LB in inventory, shop open | quick sell | `InventoryUI.cpp:221-247` |
| 15 | LB a quick slot | hand full = bind, hand empty = use | `BasicPotionTray.cpp:112-122` |
| 16 | RB a quick slot | unbind | `:125` |
| 17 | LB the terrain while carrying | **drop to ground, no confirm** | `GLCharacter.cpp:3386` |
| 18 | RB the terrain while carrying | firecracker throw (stubbed `E_FAIL`) | `GLCharactorReq.cpp:2928` |
| 19 | LB a ground item | walk to it and pick up — `ReqFieldTo` | `GLCharacter.cpp:2509-2548` |
| 20 | **any RB press in the world** | cancels the trade pre-item | `GLCharacter.cpp:3296` |
| 21 | LB a mix/rebuild/transfer/garbage slot | commit the carried item | `ItemMixWindow.cpp:261` etc. |
| 22 | RB a mix/transfer slot | clear that slot | `ItemMixWindow.cpp:266` |
| 23 | ALT + LB | 3-D preview — `PreviewItem` | `InventoryUI.cpp:251` |
| 24 | ALT + RB | box contents — `ShowBoxContents` | `:261` |
| 25 | CTRL + RB | chat item link — the only tooltip with a close button | `:271` |
| 26 | LB double click a title bar | reset the window position | `InventoryWindow.cpp:152` |
| 27 | LB on OK/page/sort/tab buttons | `CHECK_LB_UP_LIKE`, so a double click counts once | throughout |

Constraints worth remembering: equip has a one-second `m_fWearDelay` and is
refused while attacking or casting (`GLCharactorReq2.cpp:254`); only
`CURE / RECALL / TELEPORT_CARD / PET_CARD / BUFF_CARD / LEVELUP_CARD /
KILLFEED_CARD / QITEM` can go in a quick slot (`GLCharactorReq.cpp:3331`);
`ReqFieldTo` refuses while the hand is full or any modal is open (`:426-427`);
confirms are already options — `RANPARAM::bCheckConfirmSellItem` and
`RANPARAM::bNPCShopConfirm`.

The tooltip's lifetime is the thing that matters most here. `INFO_DISPLAY` and
its siblings are **hidden every frame** at `InnerInterface.cpp:698-709` and
re-shown only while the pointer is still on the slot, so it has no dismiss
gesture at all. The one exception is the item-link tooltip
(`ITEM_INFOR_TOOLTIP_LINK`), which latches and has a real close button
(`InnerInterfaceMsg.cpp:158-170`) — that is the model a touch tooltip should
copy.

---

## 2. What the mobile build does today, and what it breaks

`MOBILE/native/platform/android/android_main.cpp:140-200, 578-676`:

* tap → left click (deferred to the lift, pressed at the touch-down point)
* hold 450 ms without moving → **right click**
* move past 30 px → drag: left over a control, middle over the world (camera)
* two fingers → pinch zoom
* no ALT, no CTRL, and **no hover** — the pointer exists only where a finger
  last touched

What that costs, in order of severity:

1. **Accidental drops.** Carrying an item makes a tap on the terrain drop it on
   the ground with no confirm and no quantity prompt (gesture 17). On a mouse
   the carried icon is visible under the cursor the whole time; on a tablet the
   hand can be full without anything under the finger, and one stray tap on the
   world throws the item away. This is the single biggest hazard in the port.
2. **Long-press does too much.** It is the only way to use or equip (7), and it
   also cancels a trade offer anywhere in the world (20) and tries the stubbed
   firecracker while carrying (18). It competes with drag and with window
   dragging.
3. **Four actions are unreachable**: preview (23), box contents (24), chat link
   (25), buy-without-confirm (12) — all behind ALT/CTRL.
4. **The tooltip needs a hover a finger cannot hold**, and snap (which parks the
   carried icon over a slot) never engages, so the carried icon sits wherever
   the finger last was.
5. **A 32-px slot is a ~4 mm target**, because the grids are drawn at the
   client's logical size.
6. **Number entry** for split and buy-count opens a text edit box that needs the
   on-screen keyboard, which covers the window it belongs to.

The new NPC shop window (`NPCShopWindow.cpp:280, 366`) is already
select-then-press-Purchase and needs nothing.

---

## 3. The design

**Principle: keep the client's model — the hand, the `Req*` calls, the modals —
and replace only the gesture layer.** No request changes, so anything the server
accepts today keeps working, and the PC build stays byte-identical behind
`#ifdef RAN_MOBILE`.

### 3.1 Gesture vocabulary

| gesture | meaning |
|---|---|
| **tap a slot** | select it: latch the tooltip and show the action sheet. Nothing moves. |
| **tap a sheet row** | perform that action |
| **tap a second slot while carrying** | place / swap / split there — the PC's second click, unchanged |
| **long-press 250 ms then move** | lift and drag within a grid; dropping on a slot places it |
| **tap outside** | dismiss the sheet; **if the hand is full, put the item back where it came from**, never drop it |
| **double tap** | unused — item cells ignore it on the PC too |

Right-click emulation stays for everything that is not an item slot, so windows,
buttons and the world do not regress.

### 3.2 Carrying is made visible and safe

* While the hand is full, a **carry bar** sits at the top of the screen: the
  item icon, its name, and a "Put back" button. It is the touch answer to "the
  icon is under my cursor".
* While the hand is full, **taps on the terrain do nothing** (drop is removed
  from the world tap). Dropping becomes an explicit "Drop" row in the sheet,
  with a confirm.
* Long-press in the world does not send a right click while a trade is open, so
  a stray hold cannot silently cancel a trade offer.

### 3.3 The item action sheet

One panel anchored beside the tapped slot: the existing tooltip content, then
labelled rows. Every action that needed a second button or a modifier becomes a
row.

```
        ┌──────────────────────────────┐
        │  Sword of X          Lv.60   │   ← existing tooltip content
        │  ATK 120-140  DUR 90/100     │
        ├──────────────────────────────┤
        │  ⚔  Equip                    │   ← default action, first row
        │  ✋  Move                     │
        │  ⧉  Split…                   │   stackable only
        │  👁  Preview                  │   was ALT+LB
        │  📦  Contents                 │   box items only, was ALT+RB
        │  🔗  Link to chat             │   was CTRL+RB
        │  🗑  Drop                     │   confirm
        └──────────────────────────────┘
```

```
actions_for (slot, item, context):
    list = []
    if context is INVENTORY:
        if wearable            : list += Equip     -> ReqInvenToWear
        else if usable         : list += Use       -> ReqInvenDrug
        list += Move                               -> ReqInvenTo (into the hand)
        if stackable           : list += Split     -> count sheet, ReqInvenSplit
        if a shop is open      : list += Sell      -> ReqNpcSaleInven (+confirm per RANPARAM)
        if storage is open     : list += Store
        if a trade is open     : list += Offer     -> ReqInvenTo (pre-item)
        list += Preview, Contents (box only), Link, Drop
    if context is WEAR      : [ Unequip -> ReqWearToInven, Preview, Link ]
    if context is QUICKSLOT : [ Use -> ReqActionQ, Clear -> ReqItemQuickReSet ]
                              (with a full hand the tap binds instead: ReqItemQuickSet)
    if context is STORAGE   : [ Withdraw -> ReqStorageTo, Use -> ReqStorageDrug,
                                Split?, Preview, Link ]
    if context is BANK      : [ Withdraw -> ReqChargedItemTo, Preview ]
    if context is SHOP      : [ Buy, Buy amount…, Preview, Contents, Link ]
    if context is MIX/REBUILD/TRANSFER/TRASH : [ Put in, Take out, Preview ]
    return list
```

The **first row is the default**, and is what a long-press-without-move performs
directly for players who want the old speed. Destructive rows always confirm;
Sell and Buy keep honouring `bCheckConfirmSellItem` / `bNPCShopConfirm` rather
than inventing a second policy.

Rows are greyed with their reason rather than hidden when the client would
refuse anyway — "Equipping…" during the one-second wear delay, "Busy" while
attacking or casting.

### 3.4 The count sheet

Replaces the keyboard modal for split, buy-count and any sell-count.

```
   ┌───────────────────────────────┐
   │  How many?            12 / 40 │
   │  ──────●──────────────────    │
   │  [ -1 ] [ +1 ] [ All ] [ ½ ]  │
   │        [ Cancel ] [ OK ]      │
   └───────────────────────────────┘
```

It ends in exactly the calls the modal ends in today (`ReqInvenSplit`,
`ReqStorageSplit`, `ReqNpcTo(..., count)`), so the server side is untouched. The
existing `EDITBOX_NUMBER` modal stays as the fallback for anything not converted.

### 3.5 The tooltip

Copy the item-link tooltip: latch it on tap, give it a close button, dismiss on
the next tap elsewhere. Nothing else can drive it, because there is no hover.

### 3.6 Bigger targets, and the ground

* Mobile-only hit-test padding on item slots (~6 logical px a side, ~9 mm) so the
  touchable area grows while the art stays exactly where the PC puts it.
* Keep tap-to-walk-and-pick for ground items, and add one "loot" button for the
  nearest item in range so crowded drops stop needing precise taps.

---

## 4. Work plan

| phase | what | where |
|---|---|---|
| 1 | Carry bar + block terrain-drop while carrying + suppress world right-click during trade | `GLCharacter.cpp` (guarded), new small control |
| 2 | The action sheet as a `CUIGroup` (skin, font, layering come free) and the rules table | new `Lib_ClientUI/Interface` file, all `#ifdef RAN_MOBILE` |
| 3 | Route slot taps in inventory / wear / quick tray to the sheet, mouse paths untouched | `InventoryWindow.cpp`, `CharacterWindowChar.cpp`, `BasicPotionTray.cpp` |
| 4 | Latched tooltip with a close button | `InnerInterfaceSimple.cpp` + `InnerInterface.cpp:698-709` |
| 5 | Count sheet, wired into split / buy / sell | new control + `InnerInterfaceModalMsg.cpp` |
| 6 | Storage, bank, shop, trade, mix, rebuild, transfer, trash contexts | their windows |
| 7 | Slot hit-test padding; loot button | `ItemSlot*`, `touch_ui.cpp` |
| 8 | Retire long-press-as-right-click **on item slots only** | `android_main.cpp` |

Phase 1 is worth doing on its own even if the rest waits: it removes the
accidental-drop hazard, which is a real loss of items today.

## 5. Decisions (answered 2026-08-30)

1. **Long-press does nothing special.** Tap and hold both open the sheet; Use
   and Equip are a row like everything else. One rule, and no item can be
   consumed by a misplaced finger. Costs a second tap per potion, which the
   quick tray already covers for the common case.
2. **Drag stays, behind a 250 ms hold-to-lift.** Press, wait, move: the icon
   lifts and follows the finger, release on a slot to place. The hold is what
   stops it fighting window dragging. "Move, then tap the target" stays as the
   equivalent path through the sheet.
3. **Drop to the ground stays, behind a confirm** naming the item. World taps
   never drop anything - the only way out is the sheet row.
4. **Loot is one button** for the nearest drop in range. Tapping a drop
   directly keeps working. No auto-loot toggle, so nothing streams pickup
   requests at the server.

---

## 6. Enhancing — yes, one new window, but only for half of it

Enhancement on the PC is two different things, and only one of them needs a new
window.

### 6.1 The half that already has windows

Remodel/rebuild (`ItemRebuild.cpp:285-398`), compound/mix
(`ItemMixWindow.cpp:230-323`), option transfer
(`ItemTransferWindow.cpp:224-345`), separate (`SeparateItem.cpp:182-254`) and
garbage (`ItemGarbage.cpp:90-165`) are already real windows with slots, an OK
button and their own confirm dialogs. They need **nothing new** — only the touch
routing from phase 6: tap a slot to put the carried item in, tap it again to
take it out, instead of left-click/right-click.

### 6.2 The half with no window at all

Everything else is "carry the material and right-click the target". There is no
UI for it whatsoever — the material sits in `SLOT_HOLD` and
`ReqInvenDrug`'s held-item branch (`GLCharactorReq.cpp:2113-2202`) dispatches on
the material's type:

| material | `emItemType` | request |
|---|---|---|
| grinding stone (the +N upgrade) | `ITEM_GRINDING` 6 | `ReqGrinding` `:738-848` |
| cleanser | `ITEM_CLEANSER` 13 | `ReqCleanser` |
| disjunction | `ITEM_DISJUNCTION` 23 | `ReqDisJunction` |
| random-option card | `ITEM_RANDOM_OPTION_CARD` 71 | `ReqRandomOptionChange` |
| non-drop card | `ITEM_NONDROP_CARD` | `ReqItemNonDropCard` |
| wrapper | `ITEM_WRAPPER` 76 | `ReqItemWrap` |
| skill reform card | `ITEM_SKILL_REFORMCARD` 90 | `ReqWeaponReform` |
| disguise | `IsDISGUISE()` | `ReqDisguise` |
| colour card | — | `InvenUseToPutOn` (onto a **worn** item) |
| mystery key, pet revive/food, vehicle oil, boosters, dual-pet skill | various | their own `Req*` |

This is the family that hurts on a tablet: it needs the carry state (which is
invisible on touch), a precise second tap on a small cell, and while it is
carried a stray tap on the terrain **drops the stone on the ground** — see
section 2. Grinding stones are the most expensive consumable in the game.

It also hides all its rules until after you try. `ReqGrinding` alone refuses
with six different messages — `GRINDING_NOITEM`, `GRINDING_MAX`,
`GRINDING_NOT_BEST`, `GRINDING_NOT_BESTITEM`, `GRINDING_NOT_NUM`,
`GRINDING_DEFCLASS` (`:760-838`) — all computed client-side, all only printed
after the attempt.

### 6.3 The proposed window

```
   ┌──────────── Enhance ─────────────┐
   │                                  │
   │    Item              Material    │
   │   ┌─────┐           ┌─────┐      │
   │   │ ⚔  │     +     │ 💎 │  ×3   │   ← tap a slot, pick from a filtered list
   │   └─────┘           └─────┘      │
   │                                  │
   │   Damage grade  4  →  5          │   ← what will change, from the same data
   │   Needs 3 stones · you have 7    │
   │                                  │
   │   [ ⚠ Can fail and reset to 0 ]  │   ← only if the game's own text says so
   │                                  │
   │        [ Cancel ]  [ Enhance ]   │   ← greyed with the reason when refused
   └──────────────────────────────────┘
```

How it behaves:

* **Nothing is ever carried by the player.** Tapping a slot opens a filtered
  list — the target slot lists only items the material can legally take, the
  material slot lists only enhancement-type items in the inventory.
* **The refusal reasons become preconditions.** The same checks
  `ReqGrinding` runs are evaluated as the two slots are filled, and the
  Enhance button is greyed with that exact game text ("Already at maximum
  grade", "Needs 3 stones") instead of printing it after a failed attempt. No
  new strings: they are the existing `ID2GAMEINTEXT` entries.
* **One confirm, then commit.** Under the hood the window does what the PC
  player does — `ReqInvenTo(material)` to put it in `SLOT_HOLD`, wait for the
  server to confirm the hand is full, then `ReqGrinding(target)` (or whichever
  request the material's type maps to). No new packet, no server change.
* **The hand is always emptied.** Whatever the result, the window puts any
  remainder back into the inventory, so no path can leave a stone stranded on
  the cursor — which is the state that made the accidental drop possible.
* One window covers every material in the table above, because they all share
  the shape "held material + target cell". The type only decides which `Req*`
  is called and which items the pickers list.

Entry points: an "Enhance" row on the item sheet (section 3.3) for any material
item, and the same row on a target item, pre-filling whichever slot was tapped.

### 6.4 Where it sits in the plan

New **phase 9**, after the sheet and the pickers exist (it reuses both). It is
not a prerequisite for anything else, but it is the piece that turns the most
expensive accident in the port — losing a grinding stone to a stray tap — into
something that cannot happen.

---

## 7. Build status (2026-08-31)

| phase | state |
|---|---|
| 1 carry indicator, drop guard, trade-cancel guard | **done, verified** |
| 2 action sheet | **done, verified** |
| 3 bag / worn / quick tray routing | **done, verified** |
| 4 latched tooltip with a close button | not built — on touch the pointer stays parked on the tapped slot, so the tooltip already persists, and the sheet suppresses it while open |
| 5 count sheet (split) | **done, verified** — buy and sell counts still use the client's own confirm path |
| 6 storage | **done**; shop, trade, mix, rebuild, transfer and garbage still use the PC path (each already confirms, and the sheet's Move row covers carry-and-place) |
| 7 loot button | already existed — `RANTOUCH_SLOT_PICKUP` sends the client's own space-to-loot |
| 7 slot hit-padding | **dropped**: grid cells are adjacent, so padding one only steals from its neighbour. Bigger targets need a different cell layout, which is a data change |
| 8 long press retired on item slots | **done** — a long press on an item opens the sheet, same as a tap |
| 9 enhance window | **done, verified** |

Verified on LDPlayer, because the tablet's wireless adb drops during a session.
The emulator runs `lib/x86_64/libran.so`, so `ABI=x86_64 ./build.sh` is part of
every test cycle; the tablet build is plain `./build.sh`.

The device also needs the repacked `Gui.rcc` (new text ids live inside it) —
`CLIENT/data/gui/Gui.rcc`, pushed to `/sdcard/ran/data/gui/`.

### Skin (2026-08-31)

All three panels use the client's own art: the item sheet is the ESC menu's
frame plus `SIZE22` text buttons; the count sheet and the enhance window are
`CUIWindowEx` light-grey windows with the game's title bar, close button and
buttons, and their rects live in `uiinnercfg02.xml` beside every other window's.
