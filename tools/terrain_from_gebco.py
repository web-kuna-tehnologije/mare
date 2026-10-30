#!/usr/bin/env python3
"""Cut the real GEBCO export (assets/data/export-global) into the game's streamed terrain tiles.

    python tools/terrain_from_gebco.py <export-global> <out_tiles_dir>
    (needs numpy: run with the Windows python or `blender -b --python ... --`)

The export is the whole Earth as 27x14 chunks of 513x513 float32 *metres* (negative = sea
floor), chunks/{y}/{x}/height.r32, y=0 the north row - see assets/data/how_to.txt. This tool
resamples the playable window (types.h: lon -130..46, lat -58..72) to OUT_PER_DEG samples per
degree and writes the u16 tile set world/terraintiles.c streams (height_<tx>_<ty>.bin +
height.json), replacing the old AI-map cut from tools/terrain_tiles.py.

Encoding (the contract with terraintiles.c - TT_SEA / TT_HEIGHT_SCALE / TT_DEPTH_SCALE):
    g = 0.5 + 0.5 * clamp(h_m / LAND_MAX_M, 0, 1)      land
    g = 0.5 - 0.5 * clamp(-h_m / DEPTH_MAX_M, 0, 1)    sea
so 0.5 is *exactly* sea level (the old AI map's shoreline had to be measured; this one is
defined), full-range land is LAND_MAX_M = 9000 m and full-range depth DEPTH_MAX_M = 12000 m,
matching the export's own preview range. With the game's TT_HEIGHT_SCALE 28 that puts the
Alps (4800 m) at ~14.9 world units - the relief the 0.17 world already drew - and the deepest
Mediterranean trench (~5100 m) at ~3.8 units below the surface.

Missing samples (coverage.r8 = 0) are filled from the nearest valid neighbour in the row; the
GEBCO grid is complete over the playable window, so this is belt and braces.
"""
import json, os, struct, sys

try:
    import numpy as np
except ImportError:
    sys.exit("numpy required: run with the Windows python or under blender -b --python")

CHUNK = 512                       # cells per export chunk (513 samples, edges shared)
TILE = 512                        # samples per game tile (terraintiles.h TT_TILE)
LON0, LON1 = -130.0, 46.0         # the playable window (core/types.h)
LAT0, LAT1 = -58.0, 72.0
OUT_PER_DEG = 40                  # the finest clipmap ring reads 25 texels/deg; 40 leaves margin
LAND_MAX_M, DEPTH_MAX_M = 9000.0, 12000.0


def load_window(export):
    man = json.load(open(os.path.join(export, "manifest.json")))
    cx, cy = man["chunk_count"]
    gw, gh = cx * CHUNK, cy * CHUNK                       # global cells; +1 for shared samples
    # global sample coordinates of the window (fractional: the window edges need not align)
    gx0f = (LON0 + 180.0) / 360.0 * gw
    gx1f = (LON1 + 180.0) / 360.0 * gw
    gy0f = (90.0 - LAT1) / 180.0 * gh
    gy1f = (90.0 - LAT0) / 180.0 * gh
    gx0, gx1 = int(gx0f) - 1, int(gx1f) + 2               # one sample of bilinear margin
    gy0, gy1 = int(gy0f) - 1, int(gy1f) + 2
    gx0 = max(gx0, 0); gy0 = max(gy0, 0); gx1 = min(gx1, gw); gy1 = min(gy1, gh)
    W, H = gx1 - gx0 + 1, gy1 - gy0 + 1
    src = np.zeros((H, W), np.float32)
    cov = np.zeros((H, W), np.uint8)
    loaded = missing = 0
    for ty in range(gy0 // CHUNK, gy1 // CHUNK + 1):
        for tx in range(gx0 // CHUNK, gx1 // CHUNK + 1):
            if ty >= cy or tx >= cx: continue
            d = os.path.join(export, "chunks", str(ty), str(tx))
            try:
                h = np.fromfile(os.path.join(d, "height.r32"), dtype="<f4").reshape(513, 513)
                c = np.fromfile(os.path.join(d, "coverage.r8"), dtype=np.uint8).reshape(513, 513)
            except (OSError, ValueError):
                missing += 1
                continue
            loaded += 1
            # chunk (tx,ty) holds samples [tx*CHUNK .. tx*CHUNK+512] inclusive; edges agree
            x0, y0 = tx * CHUNK, ty * CHUNK
            sx0, sy0 = max(gx0, x0), max(gy0, y0)
            sx1, sy1 = min(gx1, x0 + CHUNK), min(gy1, y0 + CHUNK)
            if sx1 < sx0 or sy1 < sy0: continue
            src[sy0 - gy0:sy1 - gy0 + 1, sx0 - gx0:sx1 - gx0 + 1] = \
                h[sy0 - y0:sy1 - y0 + 1, sx0 - x0:sx1 - x0 + 1]
            cov[sy0 - gy0:sy1 - gy0 + 1, sx0 - gx0:sx1 - gx0 + 1] = \
                c[sy0 - y0:sy1 - y0 + 1, sx0 - x0:sx1 - x0 + 1]
    bad = int((cov == 0).sum())
    if bad:
        # fill from the nearest valid sample in the same row, then by column for whole bad rows
        for r in range(src.shape[0]):
            c_ = cov[r]
            if c_.all() or not c_.any(): continue
            valid = np.flatnonzero(c_)
            src[r] = np.interp(np.arange(src.shape[1]), valid, src[r][valid])
            cov[r] = 1
        for col in range(src.shape[1]):
            c_ = cov[:, col]
            if c_.all() or not c_.any(): continue
            valid = np.flatnonzero(c_)
            src[:, col] = np.interp(np.arange(src.shape[0]), valid, src[:, col][valid])
            cov[:, col] = 1
    print("source window: %dx%d samples from %d chunks (%d absent), %d invalid filled"
          % (W, H, loaded, missing, bad))
    print("height range: %.0f .. %.0f m" % (src.min(), src.max()))
    return src, (gx0, gy0, gw, gh)


def resample(src, origin):
    gx0, gy0, gw, gh = origin
    # One binomial smoothing pass before the resample. The output grid (OUT_PER_DEG) runs a
    # few percent finer than the source (~38.4/deg), and point-bilinear at a near-1:1 ratio
    # beats against the source lattice - visible as diagonal striping on gentle shelf
    # gradients. The data is ~3 km per sample; nothing real lives above that frequency.
    sm = src.copy()
    sm[1:-1, :] = 0.25 * src[:-2, :] + 0.5 * src[1:-1, :] + 0.25 * src[2:, :]
    src = sm
    sm = src.copy()
    sm[:, 1:-1] = 0.25 * src[:, :-2] + 0.5 * src[:, 1:-1] + 0.25 * src[:, 2:]
    src = sm
    out_w = int(round((LON1 - LON0) * OUT_PER_DEG)) + 1
    out_h = int(round((LAT1 - LAT0) * OUT_PER_DEG)) + 1
    # output sample i spans lon0..lon1 across out_w-1 steps (terraintiles.c tt_index)
    lon = LON0 + (LON1 - LON0) * np.arange(out_w) / (out_w - 1)
    lat = LAT1 - (LAT1 - LAT0) * np.arange(out_h) / (out_h - 1)
    u = (lon + 180.0) / 360.0 * gw - gx0
    v = (90.0 - lat) / 180.0 * gh - gy0
    u = np.clip(u, 0, src.shape[1] - 1.001); v = np.clip(v, 0, src.shape[0] - 1.001)
    iu, iv = u.astype(np.int32), v.astype(np.int32)
    fu, fv = (u - iu).astype(np.float32), (v - iv).astype(np.float32)
    out = np.empty((out_h, out_w), np.float32)
    for r in range(out_h):                       # row-wise keeps the temporaries small
        r0, r1 = src[iv[r]], src[iv[r] + 1]
        a = r0[iu] * (1 - fu) + r0[iu + 1] * fu
        b = r1[iu] * (1 - fu) + r1[iu + 1] * fu
        out[r] = a * (1 - fv[r]) + b * fv[r]
    return out


def encode_u16(h_m):
    # Land is linear. The sea half is sqrt-compressed: the game's shallow-water language -
    # the turquoise gate, wave damping, the exp(-depth) body colour - was tuned for shelves a
    # few tenths of a unit down, and a real-metre linear encode puts the whole north Adriatic
    # (-30..-70 m) at ~0.04 units, which washes every coastal sea into the shallow colour.
    # sqrt spends the range where ships sail: -70 m decodes to ~0.7 units, -600 m to ~2,
    # the Ionian deep to ~4.8, the full -12000 m still exactly to TT_DEPTH_SCALE.
    g = np.where(h_m >= 0,
                 0.5 + 0.5 * np.clip(h_m / LAND_MAX_M, 0, 1),
                 0.5 - 0.5 * np.sqrt(np.clip(-h_m / DEPTH_MAX_M, 0, 1)))
    return np.round(g * 65535.0).astype("<u2")


def write_tiles(q, out_dir):
    h, w = q.shape
    tiles_x, tiles_y = (w + TILE - 1) // TILE, (h + TILE - 1) // TILE
    os.makedirs(out_dir, exist_ok=True)
    for ty in range(tiles_y):
        for tx in range(tiles_x):
            t = np.zeros((TILE, TILE), "<u2")
            sy, sx = min(TILE, h - ty * TILE), min(TILE, w - tx * TILE)
            t[:sy, :sx] = q[ty * TILE:ty * TILE + sy, tx * TILE:tx * TILE + sx]
            t[sy:, :] = t[sy - 1:sy, :]                    # edge-clamp the padding
            t[:, sx:] = t[:, sx - 1:sx]
            t.tofile(os.path.join(out_dir, "height_%d_%d.bin" % (tx, ty)))
    json.dump({"kind": "height", "tiles_x": tiles_x, "tiles_y": tiles_y, "tile": TILE,
               "bytes_per_sample": 2, "width": w, "height": h,
               "lon0": LON0, "lat0": LAT0, "lon1": LON1, "lat1": LAT1,
               "source": "GEBCO export-global (real Earth), tools/terrain_from_gebco.py",
               "encoding": {"sea_level": 0.5, "land_max_m": LAND_MAX_M, "depth_max_m": DEPTH_MAX_M}},
              open(os.path.join(out_dir, "height.json"), "w"), indent=1)
    print("wrote %dx%d tiles (%d) to %s" % (tiles_x, tiles_y, tiles_x * tiles_y, out_dir))


def probe(q):
    """Sanity: known places, printed as the metres the game will decode."""
    h, w = q.shape
    def at(lon, lat):
        x = int((lon - LON0) / (LON1 - LON0) * (w - 1)); y = int((LAT1 - lat) / (LAT1 - LAT0) * (h - 1))
        g = q[y, x] / 65535.0
        return (g - 0.5) * 2 * LAND_MAX_M if g >= 0.5 else -(((0.5 - g) * 2) ** 2) * DEPTH_MAX_M
    for name, lon, lat in (("Alps (Mont Blanc)", 6.86, 45.83), ("Dinaric (Durmitor)", 19.0, 43.1),
                           ("Po valley", 11.0, 45.0), ("Adriatic mid", 15.3, 42.5),
                           ("Ionian deep", 19.0, 36.5), ("Atlantic abyss", -40.0, 30.0),
                           ("North Sea", 3.0, 56.0), ("Sahara", 10.0, 25.0)):
        print("  %-20s %8.0f m" % (name, at(lon, lat)))


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    src, origin = load_window(argv[0])
    out = resample(src, origin)
    q = encode_u16(out)
    write_tiles(q, argv[1])
    probe(q)
