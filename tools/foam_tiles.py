#!/usr/bin/env python3
"""Cut the foam sheets under assets/ships into the foam tiles the sea reads (pure stdlib).

    python3 -I tools/foam_tiles.py <ships_dir> <out_dir>

Reads <ships_dir>/foam/masks (the masks) and <ships_dir>/foam/photos (the photographs) and writes,
into <out_dir> (normally <assets>/sea):

    foam_wake.png    the churn astern and the cut-water      <- foam/masks, first mask
    foam_streak.png  the finer lapping along the hull         <- foam/masks, second mask
    foam_crest.png   the whitecaps and the collapsing crests  <- foam/photos, the tileable photograph
    foam_lane.png    the streaks drawn out along the track    <- foam/photos, the labelled sheet's streak panel

0.22.10 (issue7). The 0.22.9 cut worked from sailcloth sheets that had been dropped into the
folder by mistake; these are the real thing. The two masks in foam/masks are white foam rendered
over a grey *checkerboard* (an alpha image flattened onto a checker), so the checker has to
come out first: its period is found by looking for the lattice whose per-phase means vary
most, that lattice mean is subtracted, and what is left is the foam. The photograph is only
high-passed gently and normalised - it already is foam. Every tile is written as greyscale in
RGB, seamless, with its mean pulled to ~0.5 so the shader's smoothsteps bite in the same place
whichever tile is bound.
"""
import math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sea_tiles import read_png, write_png, seamless, seam_error, tiled

SIZE = 512


def luminance(w, h, px):
    return [(px[i * 3] * 77 + px[i * 3 + 1] * 151 + px[i * 3 + 2] * 28) >> 8 for i in range(w * h)]


def resample(w, h, v, size, x0=0, y0=0, cw=None, ch=None):
    """box-average a (sub)rect of a w x h float field to size x size"""
    cw = cw or w; ch = ch or h
    out = [0.0] * (size * size)
    for y in range(size):
        ya, yb = y0 + y * ch // size, y0 + max((y + 1) * ch // size, y * ch // size + 1)
        for x in range(size):
            xa, xb = x0 + x * cw // size, x0 + max((x + 1) * cw // size, x * cw // size + 1)
            s = 0.0; n = 0
            for yy in range(ya, yb):
                row = yy * w
                for xx in range(xa, xb):
                    s += v[row + xx]; n += 1
            out[y * size + x] = s / n
    return out


def box_blur(size, v, r):
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


def uncheck(w, h, lum):
    """remove a checkerboard background: find its period, subtract the lattice mean"""
    best_p, best_var, best_means = 0, 0.0, None
    for p in (8, 12, 16, 20, 24, 32, 40, 48, 64):
        sums = [0.0] * (p * p); cnt = [0] * (p * p)
        for y in range(0, h, 3):
            row = y * w; py = (y % p) * p
            for x in range(0, w, 3):
                k = py + (x % p); sums[k] += lum[row + x]; cnt[k] += 1
        means = [sums[i] / cnt[i] if cnt[i] else 0.0 for i in range(p * p)]
        m = sum(means) / len(means)
        var = sum((a - m) ** 2 for a in means) / len(means)
        if var > best_var: best_p, best_var, best_means = p, var, means
    if best_var < 4.0:                      # no checker worth removing
        return [float(x) for x in lum], 0
    p = best_p; m = sum(best_means) / len(best_means)
    out = [0.0] * (w * h)
    for y in range(h):
        row = y * w; py = (y % p) * p
        for x in range(w):
            out[row + x] = lum[row + x] - best_means[py + (x % p)] + m
    return out, p


def finish(name, dst, g, blur_r=1, preview=False):
    """contrast-stretch, gamma to a 0.5 mean, seamless, write"""
    if blur_r > 0: g = box_blur(SIZE, g, blur_r)
    s = sorted(g)
    lo, hi = s[int(len(s) * 0.01)], s[int(len(s) * 0.995)]
    rng = max(hi - lo, 1e-6)
    g = [min(max((x - lo) / rng, 0.0), 1.0) for x in g]
    mean = sum(g) / len(g)
    gamma = math.log(0.5) / math.log(max(min(mean, 0.99), 0.01))
    out = bytearray(SIZE * SIZE * 3)
    for i, x in enumerate(g):
        b = int(min(max(x ** gamma, 0.0), 1.0) * 255.0 + 0.5)
        out[i * 3] = out[i * 3 + 1] = out[i * 3 + 2] = b
    before = seam_error(SIZE, out)
    out = seamless(SIZE, out, 0.18)
    after = seam_error(SIZE, out)
    write_png(dst, SIZE, SIZE, out)
    if preview: write_png(dst.replace('.png', '_tiled.png'), SIZE * 2, SIZE * 2, tiled(SIZE, out))
    print('%-16s %dx%d  mean %.2f gamma %.2f  wrap %.1f -> %.1f' % (name, SIZE, SIZE, mean, gamma, before, after))


def mask_tile(src, dst, name):
    """white foam flattened onto a grey checker: the foam is the only thing near white"""
    w, h, px = read_png(src)
    lum = luminance(w, h, px)
    # the checker's two greys are the two tallest histogram peaks under pure white; the
    # threshold sits a little above the brighter one, so both squares go to zero together
    hist = [0] * 256
    for x in lum: hist[x] += 1
    peaks = sorted(range(236), key=lambda i: -hist[i])[:2]
    thr = max(peaks) + 10
    print('%s: checker greys %d/%d, foam above %d' % (os.path.basename(src)[:24], min(peaks), max(peaks), thr))
    v = [min(max((x - thr) / max(250 - thr, 1), 0.0), 1.0) for x in lum]
    v = resample(w, h, v, SIZE)
    finish(name, dst, v, blur_r=1)


def photo_tile(src, dst, name, rect=None):
    w, h, px = read_png(src)
    lum = [float(x) for x in luminance(w, h, px)]
    if rect: v = resample(w, h, lum, SIZE, rect[0], rect[1], rect[2], rect[3])
    else:    v = resample(w, h, lum, SIZE)
    lo = box_blur(SIZE, v, SIZE // 10)                 # a gentle high-pass: lighting out, foam in
    v = [v[i] - lo[i] * 0.85 for i in range(SIZE * SIZE)]
    finish(name, dst, v, blur_r=0)


def pngs(d):
    return sorted(os.path.join(d, f) for f in os.listdir(d) if f.lower().endswith('.png')) if os.path.isdir(d) else []


def main(argv):
    if len(argv) < 3: print(__doc__); return 2
    ships, out = argv[1], argv[2]
    os.makedirs(out, exist_ok=True)
    new = pngs(os.path.join(ships, 'foam', 'masks'))     # 0.22.13: assets/ships/foam/{masks,photos} (was foam/new and FOAM)
    old = pngs(os.path.join(ships, 'foam', 'photos'))
    if len(new) >= 1: mask_tile(new[0], os.path.join(out, 'foam_wake.png'), 'foam_wake')
    if len(new) >= 2: mask_tile(new[1], os.path.join(out, 'foam_streak.png'), 'foam_streak')
    # the photographs: the tileable one is the one with the higher mean (white foam on grey
    # water); the darker one is the labelled sheet of panels
    if old:
        info = []
        for p in old:
            w, h, px = read_png(p); n = w * h
            info.append((sum(px[0::3]) / n, p, w, h))
        info.sort()
        sheet, photo = info[0], info[-1]
        photo_tile(photo[1], os.path.join(out, 'foam_crest.png'), 'foam_crest')
        if len(info) > 1:
            # the sheet's lower-left panel is the vertical streaks: the sheet is a 3 x 2 grid with
            # a label strip over each panel, so take the lower half's left two thirds below the strip
            w, h = sheet[2], sheet[3]
            photo_tile(sheet[1], os.path.join(out, 'foam_lane.png'), 'foam_lane', rect=(0, int(h * 0.56), int(w * 0.66), int(h * 0.42)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
