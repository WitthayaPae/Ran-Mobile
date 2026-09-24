# In-world billboards — RAN LEGACY M

The map billboards in `CLIENT/textures/map/ad_ppl*.dds` still carry 2010-2012
Philippine sponsor ads (Gatorade, Axe, Nokia, Hyundai, Cobra, e-Games…). These
prompts replace every one with a RAN LEGACY M ad.

`airportfueltruck01_d.dds` is **not** an ad — it is the UV skin of the airport
fuel truck model. Leave it alone; repainting it scrambles the truck.

## How to generate

- **Size:** generate at **2048 × 1024** (2:1) unless the entry says square.
  The game shows them at 512 × 256, so they are scaled down 4×.
- **Text:** keep it to the headline in quotes. At 512 px and seen from across a
  map, anything smaller than a headline turns to mush. English only - image
  models draw Thai badly; if you want Thai lines, ask and they get added after.
- **Save as PNG** named after the texture (`ad_ppl2_05.png`). Hand them back and
  they get resized, framed where the original has a frame, converted to the same
  DDS format as the file they replace, and put in the patch.

**Style line — add to the end of every prompt:**

> bold esports-style game advertisement, Korean MMORPG art style, vivid saturated colors, strong rim lighting, high contrast, large clean readable headline typography, logo "RAN LEGACY M" in the corner, no real-world brands, no watermark, 2:1 wide banner composition

Game facts the prompts use (from the game data, not invented): schools
**Sacred Gate, Mystic Peak, Phoenix**; classes **Brawler, Swordsman, Archer,
Shaman, Extreme, Gunner, Assassin, Tricker**; events **Club War, Club Deathmatch, Tyranny** (School Wars and
Capture the Flag are not in the live game); mobile on **Android and iOS**.

---

### ad_ppl1_01.dds — was Gatorade "Play Better"
A young male Brawler in a modern academy uniform bursting out of a glowing smartphone screen mid-punch, shards of light flying, dark stadium background with teal spotlights. Headline left side: "PLAY ANYWHERE." Small line under it: "RAN LEGACY M — Android & iOS".

### ad_ppl1_02.dds — was Century Tuna product shot
Clean product-shot advertisement on a white-to-pale-blue background: a row of glossy fantasy potion bottles — red HP, blue MP, green SP — with water droplets and soft reflections, one bottle tilted and glowing. Serif headline left: "RESTORE. REPEAT." Small tag: "Potion Shop".

### ad_ppl1_03.dds — was "Conqueror's Path" tournament board
Futuristic dark gunmetal scoreboard panel with blue neon trim, title at the top in wide sci-fi capitals: "RANKING SEASON". Below, six empty glowing rank plates in two columns of three, labelled only "1" to "6", no names. Holographic grid floor, symmetric layout.

### ad_ppl2_01.dds — was Cobra energy drink
A male Swordsman slashing diagonally across the frame, blade trailing yellow-green lightning, dynamic low angle, energy burst background in lime and black. Italic headline right: "SHARPEN YOUR SKILLS".

### ad_ppl2_02.dds — was Axe "Even goddesses will fall"
A female Shaman in flowing robes seated on a floating stone, summoned spirit glowing violet around her, moody dark plum background, elegant thin serif text top: "JOIN THE HUNT". Mysterious, premium perfume-ad mood.

### ad_ppl2_03.dds — was Nokia Ovi "claim free items"
Two academy students, one male one female, standing confidently in a sunny campus courtyard, holding smartphones that glow with the game's interface. Headline right, stacked: "NOW ON ANDROID & iOS". Small line: "Your legacy fits in your pocket."

### ad_ppl2_04.dds — was "Destiny Boxes out now"
Several glowing golden mystery boxes with teal ribbons tumbling toward the viewer, sparkles and light rays, deep brown-gold background. Big chunky yellow headline: "MYSTERY BOXES OUT NOW!"

### ad_ppl2_05.dds — was "Domination 6" job-fair tournament
A towering armored boss monster filling the right half of the frame, glowing red eyes, a party of four heroes of different classes charging at it from the left, sparks and debris, dark stormy arena. Top headline across the full width: "HUNT THE BOSS". Under it: "Bring your party."

### ad_ppl2_06.dds — was a sushi ad (has a thin gold frame — generate the picture only; the frame is kept)
A group of five adventurers of different classes posing together under a big club banner on a rooftop at sunset, warm friendly mood. Headline top-left: "FORM YOUR CLUB".

### ad_ppl2_07.dds — was "P300 Shopcards / RAN item catalog"
Showcase of costume sets and a winged mount displayed on glowing pedestals like a luxury shop window, two models wearing the costumes at the sides, dark brown and gold background. Big yellow headline with black outline: "ITEM SHOP — NEW ARRIVALS".

### ad_ppl2_08.dds — was a Hyundai car ad (thin gold frame — picture only)
A sleek futuristic hover-motorbike parked on a mirror-wet road by the sea at dusk, perfect reflection underneath, car-commercial lighting. Small elegant headline bottom-right: "RIDE IN STYLE".

### ad_ppl2_09.dds — was Propel water "double HP"
An Archer drawing a bow, arrow of blue light, splash of water and energy around her, three distant targets each pierced dead center, crisp white and blue background. Bold italic headline top-right: "HIT EVERY TARGET".

### ad_ppl2_10.dds — was an airline "We Know Asia" (thin gold frame — picture only)
Three friendly academy instructors in formal uniforms smiling in front of a grand school gate, red paper lanterns and bamboo at the edge, warm light, Asian-airline-ad mood. Thin italic headline top-left: "WELCOME, STUDENT".

### ad_ppl2_11.dds — was "Spend it or lose it" (TWO SQUARE PANELS in one frame — generate **one 1024 × 1024 square**, it is used twice)
A glowing golden "Level Up" card held up in a gauntleted hand, beams of light and rising numbers around it, deep blue background with streaks of red. Big stacked headline: "LEVEL UP CARD". Under it: "Straight to the top."

### ad_ppl2_12.dds — was a wedding ad (ornate stone frame — picture only, slightly narrower: ~1.9 : 1)
An epic castle under siege at golden hour, armies of the three schools on the field, banners and fire arrows, a lone hero on the wall raising a sword. Classical serif headline left: "TYRANNY — CLAIM THE CITY".

### ad_ppl3_01.dds — was a crowded anime event poster
All eight classes lined up across the frame like a movie poster — Brawler, Swordsman, Archer, Shaman, Extreme, Gunner, Assassin, Tricker — dramatic red and black sunburst behind them. Center headline in a bold emblem: "8 CLASSES. ONE LEGACY."

### ad_ppl3_02.dds — was "Join the AXE Wars" competition
A female Assassin in green and black leaping forward with twin blades, swirl of green smoke, dark background with club banners clashing. Headline top: "CLUB DEATHMATCH". Under it: "Every club. One winner."

### ad_ppl3_03.dds — was Cobra energy drink "Instant Brainergy"
A Gunner in crimson armor firing twin pistols, red and yellow lightning energy, a smartphone floating beside him showing an "AUTO" button glowing. Headline right: "AUTO BATTLE". Under it: "Keep fighting, even on the go."

---

`airportfueltruck01_d.dds` — no prompt, not an ad (see top).
