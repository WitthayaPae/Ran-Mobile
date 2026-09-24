"""Replace the logo an image model drew with the real RAN LEGACY M emblem.

The drawn logo is removed by inpainting (LaMa, pip: simple-lama-inpainting,
torch CPU, opencv-python-headless) - the scene is repainted through it - and
the real emblem is placed where it was.

    python put-emblem.py <in_dir> <out_dir>

Each generated ad carries a made-up "RAN LEGACY M" wordmark. For every ad the
box below marks where that fake logo sits (in 640-px-wide preview
coordinates, measured by eye on the actual images, then widened to take in
the spikes above and below). Inside the box the picture is replaced by a
heavily blurred copy of itself with a soft dark vignette - background, not a
hole - and the real emblem (launcher's ran_mark.png, transparent) is drawn
over it, centred and sized to the box height.
"""
import os, sys
import numpy as np
from PIL import Image, ImageFilter, ImageDraw, ImageChops

HERE = os.path.dirname(os.path.abspath(__file__))
MARK = os.path.join(HERE, '..', '..', 'native', 'android', 'res', 'drawable-nodpi', 'ran_mark.png')

# (x0, y0, x1, y1) in 640-wide preview space; grow = (left, top, right, bottom)
# as fractions of the box.
BOXES = {
    'ad_ppl1_01': ((35, 215, 225, 285), (0.05, 0.15, 0.05, 0.02)),
    'ad_ppl1_02': ((480, 12, 615, 77),  None),
    'ad_ppl1_03': ((225, 49, 390, 119), (0.06, 0.42, 0.06, 0.30)),   # school banners either side
    'ad_ppl2_01': ((470, 29, 610, 94),  None),
    'ad_ppl2_02': ((485, 166, 610, 221), None),
    'ad_ppl2_03': ((435, 26, 575, 96),  None),
    'ad_ppl2_04': ((400, 22, 540, 97),  None),
    'ad_ppl2_05': ((545, 12, 635, 57),  (0.02, 0.42, 0.22, 0.30)),   # headline ends right at its left edge
    'ad_ppl2_06': ((505, 14, 620, 69),  None),
    'ad_ppl2_07': ((85, 14, 190, 69),   None),
    'ad_ppl2_08': ((495, 11, 630, 81),  None),
    'ad_ppl2_09': ((25, 11, 130, 71),   None),
    'ad_ppl2_10': ((505, 17, 638, 77),  None),
    'ad_ppl2_11': ((455, 27, 625, 102), None),
    'ad_ppl2_12': ((525, 14, 635, 69),  None),
    'ad_ppl3_01': ((530, 14, 635, 69),  (0.04, 0.42, 0.22, 0.30)),   # SHAMAN banner on the left
    'ad_ppl3_02': ((495, 226, 630, 291), None),
    'ad_ppl3_03': ((10, 11, 135, 71),   None),
}
DEFAULT_GROW = (0.22, 0.42, 0.22, 0.30)   # the spiked logos reach well past the text

_lama = None
def inpaint(img, mask):
    """LaMa ("resolution-robust large mask inpainting"): the model behind most
    remove-object tools. It repaints the hole from the scene around it, so
    the logo disappears instead of being covered."""
    global _lama
    if _lama is None:
        from simple_lama_inpainting import SimpleLama
        _lama = SimpleLama()
    out = _lama(img, mask)
    return out.crop((0, 0) + img.size)          # LaMa pads to a multiple of 8

def fix(src, dst, name, mark):
    im = Image.open(src).convert('RGB')
    W, H = im.size
    s = W / 640.0
    (x0, y0, x1, y1), grow = BOXES[name]
    gl, gt, gr, gb = grow or DEFAULT_GROW
    bw, bh = x1 - x0, y1 - y0
    X0 = max(0, int((x0 - gl * bw) * s)); Y0 = max(0, int((y0 - gt * bh) * s))
    X1 = min(W, int((x1 + gr * bw) * s)); Y1 = min(H, int((y1 + gb * bh) * s))

    # 1. remove the drawn logo: inpaint a crop around it (context on every
    #    side gives the model something to continue), then paste it back
    mx, my = (X1 - X0), (Y1 - Y0)
    cx0, cy0 = max(0, X0 - mx), max(0, Y0 - my)
    cx1, cy1 = min(W, X1 + mx), min(H, Y1 + my)
    crop = im.crop((cx0, cy0, cx1, cy1))
    mask = Image.new('L', crop.size, 0)
    r = int(0.25 * min(X1 - X0, Y1 - Y0))
    ImageDraw.Draw(mask).rounded_rectangle((X0 - cx0, Y0 - cy0, X1 - cx0, Y1 - cy0), radius=r, fill=255)
    mask = mask.filter(ImageFilter.MaxFilter(9))             # a little past the glow
    filled = inpaint(crop, mask)
    soft = mask.filter(ImageFilter.GaussianBlur(3))
    im.paste(Image.composite(filled, crop, soft), (cx0, cy0))

    # 2. the real emblem where the logo was, the same height it had, with a
    #    soft shadow so it sits on the art instead of floating
    d = int((Y1 - Y0) * 0.98)
    m = mark.resize((d, d), Image.LANCZOS)
    cx, cy = (X0 + X1) // 2, (Y0 + Y1) // 2
    ox, oy = max(0, min(W - d, cx - d // 2)), max(0, min(H - d, cy - d // 2))
    out = im.convert('RGBA')
    sh = Image.new('RGBA', m.size, (0, 0, 0, 0))
    sh.putalpha(m.getchannel('A').point(lambda v: int(v * 0.55)))
    sh = sh.filter(ImageFilter.GaussianBlur(max(3, d // 30)))
    out.alpha_composite(sh, (min(W - d, ox + d // 60), min(H - d, oy + d // 40)))
    out.alpha_composite(m, (ox, oy))
    out.convert('RGB').save(dst)
    return (X0, Y0, X1, Y1), d

def main():
    src_dir, dst_dir = sys.argv[1], sys.argv[2]
    os.makedirs(dst_dir, exist_ok=True)
    mark = Image.open(MARK).convert('RGBA')
    for name in BOXES:
        src = os.path.join(src_dir, name + '.png')
        if not os.path.exists(src):
            print('%-12s missing' % name); continue
        box, d = fix(src, os.path.join(dst_dir, name + '.png'), name, mark)
        print('%-12s covered %s emblem %dpx' % (name, box, d))

if __name__ == '__main__':
    main()
