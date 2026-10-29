#!/usr/bin/env python3
"""Measure the sea-level value of a cut height tile set against the game's land mask.

    python3 tools/terrain_calibrate.py assets/terrain

Guessing which grey means "shoreline" floods continents or drains oceans. The game already
knows what fraction of its window is land (assets/terrain/landmask.pgm, one byte per base
heightmap cell), so sea level is simply that percentile of the height samples. Prints the value
to paste into TT_SEA in game/src/world/terraintiles.c.
"""
import collections, os, struct, sys

root = sys.argv[1] if len(sys.argv) > 1 else "assets/terrain"
d = open(os.path.join(root, "landmask.pgm"), "rb").read()
parts = d.split(b"\n", 3)
w, h = map(int, parts[1].split())
px = parts[3]
land = sum(1 for b in px if b > 127)
tot = w * h
print("land mask %dx%d: %.1f%% land" % (w, h, 100.0 * land / tot))

hist = collections.Counter(); n = 0
tiles = os.path.join(root, "tiles")
for f in sorted(os.listdir(tiles)):
    if not f.startswith("height_") or not f.endswith(".bin"):
        continue
    ty = int(f[:-4].split("_")[2])
    if ty % 3:                      # a third of the rows is plenty
        continue
    b = open(os.path.join(tiles, f), "rb").read()
    a = struct.unpack("<%dH" % (len(b) // 2), b)
    for v in a[::97]:
        hist[v >> 8] += 1; n += 1
cum = 0; target = (1 - land / tot) * n
sea = 0
for k in sorted(hist):
    cum += hist[k]
    if cum >= target:
        sea = k; break
print("samples %d -> sea level byte %d, TT_SEA %.3f" % (n, sea, sea / 255.0))
