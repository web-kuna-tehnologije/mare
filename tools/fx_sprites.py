"""Turn the Meshy effect sheets (assets/ships/{smoke,fire,foam/masks}) into RGBA sprite atlases
the particle pipeline samples (gfx/particles.cc, fx/puff.frag). 0.22.13 (issue10).

    python fx_sprites.py <ships_dir> <out_dir>          (Windows python: needs numpy + Pillow)

Writes <out_dir>/smoke_atlas.png (2 x 2 frames), fire_atlas.png (4 x 2) and foam_atlas.png (2 x 2),
every frame 512 px. The sheets are RGB renders of an alpha image *flattened onto a checkerboard*,
so the alpha has to be recovered: the checker's two greys (L, D) and its cell size come from the
sheet's border, every pixel is classed light or dark cell, and over a window one cell wide the
means of the light-cell pixels and the dark-cell pixels give the cover, a = 1 - (C_L - C_D)/(L - D)
(a smoke of cover a over a checker shows a*(L - D) less contrast). The sprite colour is then
unmixed, C = a*S + (1-a)*B. Frames are centred on their alpha's centroid and their edge faded.
"""
import os, sys, glob
import numpy as np
from PIL import Image

SZ = 512

def box(a, r):
    """box filter of radius r on a 2-D array (summed-area table)"""
    p = np.pad(a, ((r + 1, r), (r + 1, r)), mode='edge').astype(np.float64)
    s = p.cumsum(0).cumsum(1)
    return (s[2*r+1:, 2*r+1:] - s[:-2*r-1, 2*r+1:] - s[2*r+1:, :-2*r-1] + s[:-2*r-1, :-2*r-1]) / float((2*r+1)**2)

def checker(img):
    """(cell size, offset x, offset y, L, D) from the top rows of the sheet"""
    g = img.mean(axis=2)
    row = g[2, :]
    # the two greys: the modes of the border strip
    strip = np.concatenate([g[:6, :].ravel(), g[-6:, :].ravel(), g[:, :6].ravel(), g[:, -6:].ravel()])
    hist, edges = np.histogram(strip, bins=64, range=(0, 255))
    peaks = np.argsort(hist)[::-1]
    v1 = edges[peaks[0]]; v2 = None
    for p in peaks[1:]:
        if abs(edges[p] - v1) > 8: v2 = edges[p]; break
    if v2 is None: v2 = v1 - 20
    L, D = max(v1, v2) + 2, min(v1, v2) + 2
    mid = 0.5 * (L + D)
    cls = (row > mid).astype(np.int8)
    trans = np.nonzero(np.diff(cls))[0]
    if len(trans) < 3: return 32, 0, 0, L, D
    cells = np.diff(trans)
    cell = int(np.median(cells))
    ox = int(trans[0] + 1) % cell
    col = g[:, 2]; clsy = (col > mid).astype(np.int8); ty = np.nonzero(np.diff(clsy))[0]
    oy = int(ty[0] + 1) % cell if len(ty) else 0
    # which parity is light: look at the cell holding (ox+cell/2, oy+cell/2)
    return cell, ox, oy, L, D

def unmix(img, mode='grey'):
    """RGB uint8 sheet -> (rgb float 0..1, alpha float 0..1).
    The sheets' checkerboard is only a faint pattern on a flat grey (the two greys differ by a
    level or two), so the cover is simply how far a pixel's grey stands from that background,
    normalised by the sheet's own extreme: dark smoke on a light grey, bright foam on a mid
    grey, glowing fire on a dark one. The colour is the pixel's own."""
    g = img.mean(axis=2)
    h, w = g.shape
    border = np.concatenate([g[:6, :].ravel(), g[-6:, :].ravel(), g[:, :6].ravel(), g[:, -6:].ravel()])
    bg = float(np.median(border))
    d = np.abs(g - bg)
    d = box(d, 1)
    top = float(np.percentile(d, 99.7))
    a = np.clip((d - 5.0) / max(top - 5.0, 8.0), 0, 1)
    if bg < 110:   # dark sheet: emissive, the glow is the alpha
        a = np.clip((img.max(axis=2) - bg - 4.0) / max(255.0 - bg, 1.0), 0, 1) ** 0.85
    else:
        a = a ** 0.9
    if mode == 'fire':   # flame over a light grey: its colour is what the grey is not
        chroma = img.max(axis=2).astype(np.float64) - img.min(axis=2).astype(np.float64)
        a = np.maximum(a, np.clip((box(chroma, 1) - 12.0) / 90.0, 0, 1))
        # the white-hot core is the sheet's own grey: fill it from the flame round it
        a = np.maximum(a, np.clip(box(a, 14) * 1.7 - 0.35, 0, 1))
    S = np.clip(img.astype(np.float64) / 255.0, 0, 1)
    if bg >= 110:   # unmix the grey background out of the sprite colour where the cover is partial
        S = np.clip((S - (1.0 - a)[..., None] * (bg / 255.0)) / np.maximum(a, 0.15)[..., None], 0, 1)
    return S, a

def frame(S, a, size=SZ):
    """centre the sprite on its alpha centroid, fit it to the frame, fade the edge"""
    a = np.nan_to_num(a); S = np.nan_to_num(S)
    h, w = a.shape
    ys, xs = np.mgrid[0:h, 0:w]
    m = a.sum()
    if m < 1.0:   # nothing recovered: keep the sheet centred as it is
        a = a + 1e-3; m = a.sum()
    cy, cx = (a * ys).sum() / m, (a * xs).sum() / m
    # the extent: the radius holding 99% of the alpha
    rr = np.sqrt((ys - cy) ** 2 + (xs - cx) ** 2)
    order = np.argsort(rr.ravel()); cum = np.cumsum(a.ravel()[order]) / m
    rad = rr.ravel()[order][min(np.searchsorted(cum, 0.985), len(order) - 1)] * 1.05 + 2
    rad = max(rad, 16)
    # resample a square of side 2*rad centred on (cx, cy) to size
    rgba = np.dstack([S, a[..., None]])
    im = Image.fromarray((np.clip(rgba, 0, 1) * 255).astype(np.uint8), 'RGBA')
    box_ = (int(round(cx - rad)), int(round(cy - rad)), int(round(cx + rad)), int(round(cy + rad)))
    im = im.transform((size, size), Image.EXTENT, box_, Image.BILINEAR)
    arr = np.asarray(im).astype(np.float64) / 255.0
    # a soft circular fade so the quad's edge never shows
    yy, xx = np.mgrid[0:size, 0:size]
    d = np.sqrt((yy - size / 2 + 0.5) ** 2 + (xx - size / 2 + 0.5) ** 2) / (size / 2)
    fade = np.clip((1.0 - d) / 0.12, 0, 1)
    arr[..., 3] *= fade
    return arr

def atlas(paths, cols, rows, out, flips=True, mode='grey'):
    frames = []
    for p in paths:
        img = np.asarray(Image.open(p).convert('RGB'))
        S, a = unmix(img, mode)
        try: frames.append(frame(S, a))
        except Exception as e: print('  skipped', os.path.basename(p)[:40], e); continue
        print('  %-70s cover %.3f  mean %.2f' % (os.path.basename(p)[:70], a.mean(), a.max()))
    # fill the atlas with mirrored copies when there are fewer sheets than cells
    k = 0
    while len(frames) < cols * rows:
        frames.append(frames[k][:, ::-1, :] if flips else frames[k]); k += 1
    sheet = np.zeros((rows * SZ, cols * SZ, 4))
    for i, f in enumerate(frames[:cols * rows]):
        r, c = divmod(i, cols); sheet[r*SZ:(r+1)*SZ, c*SZ:(c+1)*SZ] = f
    Image.fromarray((np.clip(sheet, 0, 1) * 255).astype(np.uint8), 'RGBA').save(out)
    # a preview composited over mid grey, to judge the alpha by eye
    prev = sheet[..., :3] * sheet[..., 3:4] + 0.45 * (1.0 - sheet[..., 3:4])
    Image.fromarray((np.clip(prev, 0, 1) * 255).astype(np.uint8), 'RGB').save(out.replace('.png', '_preview.png'))
    print('wrote', out, sheet.shape[1], 'x', sheet.shape[0], len(paths), 'sheets')

def main(argv):
    if len(argv) < 3: print(__doc__); return 2
    ships, out = argv[1], argv[2]
    os.makedirs(out, exist_ok=True)
    smoke = sorted(glob.glob(os.path.join(ships, 'smoke', '*.png')))
    fire = sorted(glob.glob(os.path.join(ships, 'fire', '*.png')))
    foam = sorted(glob.glob(os.path.join(ships, 'foam', 'masks', '*.png')))
    if smoke: atlas(smoke[:4], 2, 2, os.path.join(out, 'smoke_atlas.png'))
    if fire:  atlas(fire[:8], 4, 2, os.path.join(out, 'fire_atlas.png'), mode='fire')
    if foam:  atlas(foam[:4], 2, 2, os.path.join(out, 'foam_atlas.png'))
    return 0

if __name__ == '__main__': sys.exit(main(sys.argv))
