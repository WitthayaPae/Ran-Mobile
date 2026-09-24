"""Replace the logo an image model drew with the real RAN LEGACY M emblem.

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
    'ad_ppl1_03': ((225, 49, 390, 119), None),
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
    'ad_ppl3_01': ((530, 14, 635, 69),  None),
    'ad_ppl3_02': ((495, 226, 630, 291), None),
    'ad_ppl3_03': ((10, 11, 135, 71),   None),
}
DEFAULT_GROW = (0.22, 0.42, 0.22, 0.30)   # the spiked logos reach well past the text

def fix(src, dst, name, mark):
    im = Image.open(src).convert('RGB')
    W, H = im.size
    s = W / 640.0
    (x0, y0, x1, y1), grow = BOXES[name]
    gl, gt, gr, gb = grow or DEFAULT_GROW
    bw, bh = x1 - x0, y1 - y0
    X0 = max(0, int((x0 - gl * bw) * s)); Y0 = max(0, int((y0 - gt * bh) * s))
    X1 = min(W, int((x1 + gr * bw) * s)); Y1 = min(H, int((y1 + gb * bh) * s))

    # 1. cover: fill the box from the pixels AROUND it, never from the fake
    #    logo itself - a normalised blur (blur of the picture with the box
    #    cut out, divided by the blur of the cut-out mask). Blurring the logo
    #    in place left a ghost of its letters and spikes.
    a = np.asarray(im, dtype=np.float32)
    keep = np.ones((H, W), np.float32)
    keep[Y0:Y1, X0:X1] = 0.0
    r = max(24, int(0.09 * W))
    def blur(x):        # x in 0..255; PIL blurs 8-bit planes only
        return np.asarray(Image.fromarray(np.clip(x, 0, 255).astype(np.uint8), 'L')
                          .filter(ImageFilter.GaussianBlur(r)), dtype=np.float32)
    num = np.stack([blur(a[..., c] * keep) for c in range(3)], -1)
    den = blur(keep * 255.0)[..., None] / 255.0 + 1e-4
    fill = np.clip(num / den, 0, 255)
    #    an oval, solid in the middle and fading well past the box edge -
    #    a rectangle, however feathered, reads as a pasted label
    yy, xx = np.mgrid[0:H, 0:W]
    cx0, cy0 = (X0 + X1) / 2.0, (Y0 + Y1) / 2.0
    rx, ry = (X1 - X0) / 2.0 * 1.08, (Y1 - Y0) / 2.0 * 1.12
    dist = np.sqrt(((xx - cx0) / rx) ** 2 + ((yy - cy0) / ry) ** 2)
    m = np.clip((1.18 - dist) / 0.38, 0, 1)
    m = (m * m * (3 - 2 * m))[..., None]                 # smoothstep
    fill = fill * (1.0 - 0.30 * np.clip(1.0 - dist, 0, 1)[..., None])   # dark glow
    im = Image.fromarray((a * (1 - m) + fill * m).astype(np.uint8))

    # 2. the emblem, as tall as the covered area, centred on it
    d = int((Y1 - Y0) * 1.10)
    m = mark.resize((d, d), Image.LANCZOS)
    cx, cy = (X0 + X1) // 2, (Y0 + Y1) // 2
    ox, oy = max(0, min(W - d, cx - d // 2)), max(0, min(H - d, cy - d // 2))
    out = im.convert('RGBA')
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
