"""Turn generated billboard PNGs into the game's map ad textures.

    python make-ads.py            # every src/<name>.png present -> CLIENT/textures/map/<name>.dds
    python make-ads.py --out DIR  # write somewhere else (a dry run)

Each output matches the texture it replaces exactly: same size (512x256), same
pixel format (DXT1, DXT3 or A8R8G8B8 - it varies per file), and the full
10-level mip chain the originals carry. A billboard seen across a map is drawn
from the small mips almost all the time; a file without them shimmers.

Five originals have their frame painted into the texture. For those the new
picture goes inside the measured frame and the frame itself is kept, taken from
the untouched original saved in orig/ on the first run - so re-running after a
replacement still has the real frame to work from.

Every file written is checked: its byte size must equal the original's, and it
must decode back to the right dimensions. Anything else stops the run.
"""
import io, os, shutil, struct, sys
from PIL import Image

HERE   = os.path.dirname(os.path.abspath(__file__))
ROOT   = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
MAPDIR = os.path.join(ROOT, 'CLIENT', 'textures', 'map')
SRC    = os.path.join(HERE, 'src')
ORIG   = os.path.join(HERE, 'orig')

NAMES = (['ad_ppl1_%02d' % i for i in (1, 2, 3)] +
         ['ad_ppl2_%02d' % i for i in range(1, 14)] +
         ['ad_ppl3_%02d' % i for i in (1, 2, 3, 4, 5)] +
         #  the Trade Zone market signs (4:1, text only)
         ['min_su_11_04', 'min_su_11_04_redbull'])

# Inner picture areas of the framed billboards, measured on the originals
# (left, top, right, bottom; right/bottom exclusive). ad_ppl2_11 is two
# panels showing the same square picture.
FRAMES = {
    'ad_ppl2_06': [(12, 17, 500, 251)],
    'ad_ppl2_08': [(12, 17, 500, 251)],
    'ad_ppl2_10': [(12, 17, 500, 251)],
    'ad_ppl2_11': [(12, 17, 251, 251), (260, 17, 500, 251)],
    'ad_ppl2_12': [(87, 46, 428, 224)],
}

def header(path):
    b = open(path, 'rb').read(128)
    if b[:4] != b'DDS ':
        raise SystemExit('%s: not a DDS' % path)
    h, w = struct.unpack('<II', b[12:20])
    mips = struct.unpack('<I', b[28:32])[0] or 1
    pf_flags = struct.unpack('<I', b[80:84])[0]
    fourcc = b[84:88]
    if pf_flags & 0x4:
        fmt = fourcc.decode('ascii')
    elif struct.unpack('<I', b[88:92])[0] == 32:
        fmt = 'ARGB8'
    else:
        raise SystemExit('%s: unhandled pixel format' % path)
    if fmt not in ('DXT1', 'DXT3', 'ARGB8'):
        raise SystemExit('%s: unhandled format %s' % (path, fmt))
    return b, w, h, mips, fmt

def cover(img, w, h):
    """Scale to fill w x h, cropping the overflow evenly - never stretches."""
    sw, sh = img.size
    s = max(w / sw, h / sh)
    img = img.resize((max(w, round(sw * s)), max(h, round(sh * s))), Image.LANCZOS)
    x = (img.size[0] - w) // 2
    y = (img.size[1] - h) // 2
    return img.crop((x, y, x + w, y + h))

def encode_level(img, fmt):
    if fmt == 'ARGB8':
        return img.convert('RGBA').tobytes('raw', 'BGRA')
    b = io.BytesIO()
    img.convert('RGBA').save(b, 'DDS', pixel_format=fmt)
    return b.getvalue()[128:]

def build(name, out_dir):
    orig = os.path.join(ORIG, name + '.dds')
    hdr, w, h, mips, fmt = header(orig)
    base = Image.open(orig).convert('RGBA')
    art = Image.open(os.path.join(SRC, name + '.png')).convert('RGBA')

    if name in FRAMES:
        comp = base.copy()
        for (l, t, r, b) in FRAMES[name]:
            comp.paste(cover(art, r - l, b - t), (l, t))
    else:
        comp = cover(art, w, h)
    comp.putalpha(255)          # the originals are fully opaque

    payload = b''
    for i in range(mips):
        lw, lh = max(1, w >> i), max(1, h >> i)
        lvl = comp if i == 0 else comp.resize((lw, lh), Image.LANCZOS)
        payload += encode_level(lvl, fmt)

    data = hdr + payload
    want = os.path.getsize(orig)
    if len(data) != want:
        raise SystemExit('%s: wrote %d bytes, original is %d - refusing' % (name, len(data), want))

    dst = os.path.join(out_dir, name + '.dds')
    with open(dst, 'wb') as f:
        f.write(data)
    back = Image.open(dst); back.load()
    if back.size != (w, h):
        raise SystemExit('%s: decodes as %s, expected %dx%d' % (name, back.size, w, h))
    comp.convert('RGB').save(os.path.join(out_dir, name + '.preview.png'))
    return fmt

def main():
    out_dir = MAPDIR
    if '--out' in sys.argv:
        out_dir = os.path.abspath(sys.argv[sys.argv.index('--out') + 1])
        os.makedirs(out_dir, exist_ok=True)

    os.makedirs(ORIG, exist_ok=True)
    for n in NAMES:                       # keep the untouched originals once
        o = os.path.join(ORIG, n + '.dds')
        if not os.path.exists(o):
            shutil.copy2(os.path.join(MAPDIR, n + '.dds'), o)

    todo = [n for n in NAMES if os.path.exists(os.path.join(SRC, n + '.png'))]
    unknown = [f for f in os.listdir(SRC) if f.endswith('.png') and f[:-4] not in NAMES]
    if unknown:
        raise SystemExit('not a billboard name: %s' % ', '.join(unknown))
    if not todo:
        print('no PNGs in %s' % SRC); return
    for n in todo:
        print('%-12s %-5s %s' % (n, build(n, out_dir), 'framed' if n in FRAMES else ''))
    print('%d written to %s  (%d still using the old ad)' % (len(todo), out_dir, len(NAMES) - len(todo)))
    if out_dir == MAPDIR:
        for n in todo:                    # previews are for checking, not shipping
            p = os.path.join(out_dir, n + '.preview.png')
            if os.path.exists(p):
                os.replace(p, os.path.join(SRC, n + '.preview.png'))

if __name__ == '__main__':
    main()
