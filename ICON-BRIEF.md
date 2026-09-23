# Icon brief — RAN Legacy M mobile

Twenty-one menu icons and sixteen on-screen controls, to be **painted** rather
than drawn as flat pictograms.

The icons in the build today are flat vector glyphs on a grey rounded plate.
They are legible and consistent, but they look like a phone app launcher rather
than this game. The client's own art — the skill slots, the attack disc — is
painted: airbrushed gradients, saturated colour, metal and gold, soft inner
glow, heavy silhouettes. The replacements have to sit beside that art without
looking imported from another product.

Hand back a folder of PNGs named exactly as the first column of each table.
Everything after that is mine: packing the atlas, repointing the GUI XML,
repacking `Gui.rcc`, and checking each one on the device at true size.

---

## The prompt

One icon per run. Swap the last line for a row from the tables below.

```
Game UI icon for a 2000s Korean fantasy MMORPG (RAN Online), painted in the
style of that era's skill icons: soft airbrushed gradients, saturated colour,
polished metal and antique gold, a warm inner glow, and a heavy readable
silhouette. Hand-painted digital illustration, NOT flat vector, NOT material
design, NOT a line icon, NOT minimalist.

FORMAT
- Exactly 128 x 128 pixels, square.
- PNG with a real transparent background (straight, un-premultiplied alpha).
- The icon fills a rounded-square plate: the plate spans the full canvas inset
  by 4 px on every side, with a 26 px corner radius. Outside that rounded
  plate must be fully transparent - no glow, no shadow, no stray pixels.
- No text, no letters, no numbers anywhere in the image.
- No border frame around the outside beyond the plate itself.

THE PLATE (identical on every icon in the set)
- A dark brushed-steel plate, vertical gradient from #515963 at the top to
  #242930 at the bottom.
- A 2 px rim of lighter steel (#8C97A5) all the way round the rounded edge,
  even weight on all four sides.
- A soft gloss across the upper third, as if lit from above.

THE SUBJECT
- One object, centred, filling roughly 70% of the plate.
- Lit from the top-left, shadow falling bottom-right, on every icon.
- Materials: aged leather, worn brass and gold (#C9962B), blued steel,
  parchment (#E8E2D2), deep oak. Small warm highlights.
- Bold shapes. It must still be recognisable shrunk to 40 px.
- Keep all important detail inside the middle 104 x 104 px; the outer 12 px
  is margin.

SUBJECT FOR THIS ICON: <<< put one line from a table below here >>>

FOR THE ROUND ON-SCREEN CONTROLS INSTEAD (second table): drop the whole PLATE
section above, and use this in its place -
- The art is a CIRCLE centred on the square canvas, filling it edge to edge.
- Everything outside that circle is fully transparent. No square plate.
- A polished steel bezel around the rim, a darker face inside it.
- Use the canvas size given in that table, not 128, where they differ.
```

The set has to look like one hand made it. If the generator drifts between runs
— different plate, different light, different level of detail — regenerate the
odd one out rather than shipping a set that does not match.

---

## The menu icons

All 128 × 128. The label is what the player reads under the icon, so the
picture only has to agree with the word; it does not have to carry the meaning
alone.

| File | Label | Subject line |
|---|---|---|
| `inventory.png` | กระเป๋า | A worn leather adventurer's satchel, brass buckle catching the light, flap closed. |
| `character.png` | ตัวละคร | The head and shoulders of an armoured student fighter seen front on, steel pauldrons, face in shadow. |
| `skill.png` | สกิล | An open leather-bound spellbook, pages lit from within, a small gold rune floating above it. |
| `party.png` | ปาร์ตี้ | Three armoured figures standing shoulder to shoulder, the middle one a step forward. |
| `guild.png` | คลับ | A heraldic banner on a brass pole, deep crimson cloth with a gold emblem, hanging still. |
| `quest.png` | ภารกิจ | A rolled parchment scroll tied with cord, a red wax seal pressed on the front. |
| `friend.png` | เพื่อน | Two figures side by side, the nearer one with a hand raised in greeting. |
| `largemap.png` | แผนที่ | A folded parchment map with faint coastlines, a red marker pin standing in it. |
| `chatmacro.png` | คำพูด | A small parchment speech scroll with three ruled lines of writing, corner curled. |
| `itembank.png` | ของจากเว็บ | A heavy oak strongbox with brass corner fittings and a gold lock, lid shut. |
| `itemshop.png` | ไอเท็มช็อป | A merchant's open coin pouch spilling gold coins across a worn wooden counter. |
| `finder.png` | หาปาร์ตี้ | A brass magnifying glass held over a small armoured figure, lens glinting. |
| `ranking.png` | อันดับ | A three-step stone podium, the tallest step in the middle crowned with a gold laurel wreath. |
| `competition.png` | แข่งขัน | Two steel swords crossed in an X, gold crossguards, blades catching a hard highlight. |
| `bossviewer.png` | บอส | A horned demon skull seen front on, empty eye sockets glowing faint red. |
| `auction.png` | ประมูล | A wooden auction gavel resting on its block, a single gold coin beside it. |
| `itemmall.png` | ร้านค้า | A small shopfront with a striped awning over a dark doorway, lantern lit above. |
| `product.png` | ผลิตของ | A blacksmith's anvil with a hammer resting on it, orange sparks fading. |
| `escmenu.png` | ระบบ | A heavy iron door standing ajar with a gold arrow leading out through it. |
| `qbox.png` | *(corner)* | A carved stone tile with a single capital letter **Q** cut deep into it, gold leaf in the groove, lit from the top left. |
| `miniparty.png` | *(corner)* | Two small oval portrait medallions overlapping, brass rims, a thin green health bar under each. |

**The last two are different.** They sit out in the corner beside the compass
rather than in the grid, they carry no label at all, and they are drawn at about
70 px instead of 112 — so they need a simpler, heavier shape than the rest and
no fine detail that dies at that size.

**The Q is deliberate.** The prompt forbids text everywhere else because image
generators mangle letterforms, but this icon *is* the letter: it is how the game
has always marked the quest box and players look for it. Ask for one clean
capital Q and regenerate if the shape comes back malformed. It is the single
place in the set worth checking twice.

---

## The on-screen controls

Different shape. These are the touch controls the mobile client adds, and they
are **round**: circular art centred on a square canvas, fully transparent
outside the circle, no rounded-square plate.

| File | Canvas | Subject |
|---|---|---|
| `atk.png` | 256 × 256 | A domed brown leather disc in a polished steel bezel, a gold-hilted sword laid across it point-up. The one control that should look expensive. |
| `atk_ring.png` | 256 × 256 | A thin broken gold ring, dashes evenly spaced, sitting just outside the attack disc — the cooldown collar. Transparent centre. |
| `skillframe.png` | 128 × 128 | An empty steel bezel with a dark well inside it, for the game's own skill picture to sit in. The well must be transparent. |
| `auto.png` | 128 × 128 | Steel disc, a crosshair over a small silhouetted target. |
| `auto_on.png` | 128 × 128 | The same, lit: gold rim, warm inner glow behind the mark. |
| `pk.png` | 128 × 128 | Steel disc, two crossed daggers. |
| `pk_on.png` | 128 × 128 | The same, lit red: crimson rim and glow, blades catching it. |
| `camlock.png` | 128 × 128 | Steel disc, an open padlock beside a small eye. |
| `camlock_on.png` | 128 × 128 | The same with the padlock shut, gold rim and glow. |
| `pickup.png` | 128 × 128 | Steel disc, an open gauntleted hand reaching down over a small gold coin. |
| `vehicle.png` | 128 × 128 | Steel disc, a saddled mount seen from the side, or a motorbike silhouette — whichever reads heavier at 64 px. |
| `menu.png` | 128 × 128 | Steel disc, four small squares in a 2 × 2 grid, slightly raised as if pressed into the metal. |
| `page_up.png` | 128 × 128 | Steel disc, a solid triangle pointing up. |
| `page_down.png` | 128 × 128 | Steel disc, a solid triangle pointing down. |
| `stick_base.png` | 256 × 256 | A wide shallow ring of dark worn metal, the seat the thumbstick moves in. Transparent centre. |
| `stick_knob.png` | 128 × 128 | A domed steel knob with a soft highlight on the upper left, the part under the thumb. |

The three toggles need both states, and they must be the same object — same
bezel, same mark, same size — so only the light changes when it switches. If the
two versions differ in shape, the button appears to jump when tapped.

These are worth doing **after** the menu icons. They need a change on my side as
well: the overlay draws them from shapes today, and I have to give it a sheet to
sample instead. It already does exactly that for the skill pictures, so it is a
known road rather than a new one.

---

## Two overlays

Separate from the icons, and drawn *over* them — so the middle must be fully
transparent or they hide the icon they are decorating. Same 128 × 128 canvas and
the same rounded-plate geometry as the menu icons.

| File | Subject |
|---|---|
| `press.png` | A pale steel ring on the plate's outline, shown for the instant a finger is down. |
| `alert.png` | A warm gold ring with a soft outward bloom, blinked on and off while an event is open. |

The game's own event blink is already a gold ring, so `alert.png` is replacing
like with like.

---

## If the generator cannot do transparency

Send them on a flat magenta `#FF00FF` background and say so — the background is
cut out when the atlas is packed.

## What happens to the files

* Packed into `mobile_icons.dds`, a 1024 × 512 atlas, uncompressed A8R8G8B8 —
  the format every other GUI sheet in this client uses.
* The GUI XML controls are repointed at the new cells and `Gui.rcc` repacked.
* Each one checked on the device at true size, and at the size the press and
  event rings sit over it.

---

## The chat button (added 2026-09-23)

One more round control, for the button that folds the chat box away and brings
it back. Same table as the on-screen controls above: round art on a square
canvas, transparent outside the circle.

| File | Canvas | Subject |
|---|---|---|
| `chat.png` | 128 × 128 | Steel disc, a parchment speech bubble with three short ruled lines of writing inside it, tail pointing down-left. |

Until the painted version lands the overlay draws its own bubble, so the button
works today and only gets better-looking when `chat.png` is packed into
`mobile_hud.dds` (append it to `CELLS` in `tools/icon-art/hud-pack.js` and add
`kCellChat` to the enum in `touch_ui.cpp`, in the same order).
