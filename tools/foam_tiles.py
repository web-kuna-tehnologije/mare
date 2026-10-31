#!/usr/bin/env python3
"""Cut the sheets in assets/ships/FOAM into the foam tiles the wake and the cut-water read.

    python3 -I tools/foam_tiles.py <src_dir> <out_dir>

What is actually in that folder (0.22.9): two 1024px *sailcloth* sheets - the same woven and
frayed hessian the sail tiles came from, not photographs of foam. Their colour is useless
here, but their *structure* is exactly what broken foam needs: long streaks torn across the
weave, at two scales, with holes. So the colour is thrown away and what is kept is a
contrast-stretched high-pass of the luminance:

    foam_wake.png     the frayed sheet: torn streaks, for the churn astern and the cut-water
    foam_streak.png   the woven sheet: finer, more regular, for the lapping along the hull

Both are written as greyscale in RGB, seamless, with the mean pulled to ~0.5 so the shader's
smoothsteps bite in the same place whichever tile is bound. The sea shader multiplies them
into its own foam masks, so a tile that is too bright only ever makes the foam busier, never
whiter.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sea_tiles import read_png, write_png, seamless, seam_error, tiled

# which sheet becomes which tile (by eye; both are in the FOAM folder)
WANT = {
    'foam_wake':   'Meshy_AI_image_acbaf26a-b2c3-483e-8168-e79fec350f4c_0.png',
    'foam_streak': 'Meshy_AI_image_28b6bd5f-bde1-45f9-8052-7fc9f30f4daa_0.png',
}
SIZE = 512          # the tile the game ships: 1024 is detail the foam masks throw away anyway


def luminance(w, h, px):
    return [(px[i * 3] * 77 + px[i * 3 + 1] * 151 + px[i * 3 + 2] * 28) >> 8 for i in range(w * h)]


def downsample(w, lum, size):
    """box average w x w -> size x size (w is a multiple of size for our sheets)"""
    n = w // size
    out = [0] * (size * size)
    inv = 1.0 / (n * n)
    for y in range(size):
        base = y * n * w
        for x in range(size):
            s = 0
            off = base + x * n
            for j in range(n):
                row = off + j * w
                for i in range(n):
                    s += lum[row + i]
            out[y * size + x] = s * inv
    return out


def box_blur(size, v, r):
    """separable box blur, wrapping (the tile is periodic)"""
    k = 2 * r + 1
    tmp = [0.0] * (size * size)
    for y in range(size):
        row = y * size
        s = sum(v[row + (x % size)] for x in range(-r, r + 1))
        for x in range(size):
            tmp[row + x] = s / k
            s += v[row + ((x + r + 1) % size)] - v[row + ((x - r) % size)]
    out = [0.0] * (size * size)
    for x in range(size):
        s = sum(tmp[(y % size) * size + x] for y in range(-r, r + 1))
        for y in range(size):
            out[y * size + x] = s / k
            s += tmp[((y + r + 1) % size) * size + x] - tmp[((y - r) % size) * size + x]
    return out


def emit(src, dst, preview=False):
    w, h, px = read_png(src)
    lum = luminance(w, h, px)
    v = downsample(w, lum, SIZE) if w > SIZE else [float(x) for x in lum]
    # high pass: what is left after the sheet's own shading is taken out, which is the weave
    # and the tears - the part that reads as broken water
    lo = box_blur(SIZE, v, max(2, SIZE // 24))
    hp = [v[i] - lo[i] for i in range(SIZE * SIZE)]
    # De-stripe. These sheets are *woven cloth*: a regular grid of vertical seams and a
    # horizontal weave. Tiled over the sea that grid reads as quilting, which is what the
    # first cut of this tile did to the water. Subtracting the per-column and per-row means
    # takes out exactly the structure that is constant along an axis - the seams and the
    # weave - and leaves the tears, which are what we are after.
    colm = [0.0] * SIZE
    rowm = [0.0] * SIZE
    for y in range(SIZE):
        row = y * SIZE
        rs = 0.0
        for x in range(SIZE):
            val = hp[row + x]
            rs += val
            colm[x] += val
        rowm[y] = rs / SIZE
    colm = [c / SIZE for c in colm]
    for y in range(SIZE):
        row = y * SIZE
        ry = rowm[y]
        for x in range(SIZE):
            hp[row + x] -= colm[x] + ry
    # and a one-texel blur, so what is left is torn blobs rather than individual threads
    hp = box_blur(SIZE, hp, 1)
    # contrast stretch to the 2nd..98th percentile, so one sheet's flatness does not make a
    # flat tile; then the mean to ~0.5
    s = sorted(hp)
    lo_p, hi_p = s[int(len(s) * 0.02)], s[int(len(s) * 0.98)]
    rng = max(hi_p - lo_p, 1e-3)
    g = [min(max((x - lo_p) / rng, 0.0), 1.0) for x in hp]
    # Keep only the tears. Even de-striped, the sheet is still cloth: a fine regular weave
    # under the damage. Thresholding to the brightest quarter throws the weave away and keeps
    # the torn edges, and a two-texel blur turns those edges into the soft blobs a foam mask
    # wants. What comes out has no grid left in it at all.
    def sstep(a, b, x):
        t = min(max((x - a) / (b - a), 0.0), 1.0)
        return t * t * (3.0 - 2.0 * t)
    g = [sstep(0.58, 0.93, x) for x in g]
    g = box_blur(SIZE, g, 2)
    lo2, hi2 = min(g), max(g)
    g = [(x - lo2) / max(hi2 - lo2, 1e-6) for x in g]
    mean = sum(g) / len(g)
    # gamma that lands the mean on 0.5 (foam masks are smoothstepped around the middle)
    import math
    gamma = math.log(0.5) / math.log(max(mean, 1e-3))
    out = bytearray(SIZE * SIZE * 3)
    for i, x in enumerate(g):
        b = int(min(max(x ** gamma, 0.0), 1.0) * 255.0 + 0.5)
        out[i * 3] = out[i * 3 + 1] = out[i * 3 + 2] = b
    before = seam_error(SIZE, out)
    out = seamless(SIZE, out, 0.25)
    after = seam_error(SIZE, out)
    write_png(dst, SIZE, SIZE, out)
    if preview:
        write_png(dst.replace('.png', '_tiled.png'), SIZE * 2, SIZE * 2, tiled(SIZE, out))
    print('%-22s %dx%d  mean %.2f gamma %.2f  wrap %.1f -> %.1f'
          % (os.path.basename(dst), SIZE, SIZE, mean, gamma, before, after))


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    src, out = argv[1], argv[2]
    os.makedirs(out, exist_ok=True)
    have = set(os.listdir(src))
    for name, f in WANT.items():
        if f not in have:                       # the folder's contents may change: take what is there
            left = sorted(x for x in have if x.lower().endswith('.png'))
            if not left:
                print('no PNG in %s' % src)
                return 1
            f = left[0 if name == 'foam_wake' else -1]
            print('%s: wanted sheet missing, using %s' % (name, f))
        emit(os.path.join(src, f), os.path.join(out, name + '.png'))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
