#!/usr/bin/env python3
"""Cut a huge terrain PNG into per-chunk tiles the game can stream on demand.

    python3 tools/terrain_tiles.py --preview assets/terrain/world-heightmap.png prev.png
    python3 tools/terrain_tiles.py assets/terrain/world-heightmap.png  assets/terrain/tiles height
    python3 tools/terrain_tiles.py assets/terrain/normal_map_terrain_2.png assets/terrain/tiles normal

A 32768x32768 16-bit heightmap is 2.1 GB once decoded and its normal map 3.2 GB: neither can be
held in memory, let alone uploaded. Both are therefore cut, once, offline, into square tiles
that the renderer pages in and out as the camera moves (world/terrainmap.h).

The source is read as a *stream*: PNG rows arrive in order, so only one band of TILE rows is
ever held (33 MB for a 32768-wide 16-bit image). Nothing here needs numpy or an image library -
these maps are written with filter type 0 (none), which is what makes a plain reader possible;
any other filter is reported and refused rather than silently mis-decoded.

Writes <out>/<kind>_<tx>_<ty>.bin - raw little-endian samples, no header - plus <out>/<kind>.json
with the grid shape, so the loader needs no PNG decoder at all and a tile is one pread.
"""
import json, os, struct, sys, zlib
try:
    import numpy as np          # Blender ships it; run the cut under blender -b --python
except ImportError:
    np = None

TILE = 512          # samples per tile edge

# The source maps are global equirectangular: longitude -180..180 across, latitude +90..-90
# down. Only the playable window is worth cutting, and only at the resolution the detail
# window can actually sample - the game's finest clipmap ring is 25 texels per degree, so
# halving a 91-px-per-degree source still leaves nearly twice what anything reads.
SRC_LON0, SRC_LON1 = -180.0, 180.0
SRC_LAT0, SRC_LAT1 = -90.0, 90.0


def png_stream(path):
    """Yield (row_index, bytes) for every row, holding only one chunk at a time."""
    f = open(path, "rb")
    if f.read(8)[1:4] != b"PNG":
        raise SystemExit("%s is not a PNG" % path)
    w = h = bd = ct = None
    stride = bpp = 0
    d = zlib.decompressobj()
    acc = bytearray()
    row = 0
    while True:
        head = f.read(8)
        if len(head) < 8:
            break
        ln, typ = struct.unpack(">I", head[:4])[0], head[4:]
        if typ == b"IHDR":
            payload = f.read(ln); f.read(4)
            w, h, bd, ct = struct.unpack(">IIBB", payload[:10])
            if ct not in (0, 2):
                raise SystemExit("colour type %d not supported (want grey or RGB)" % ct)
            bpp = (bd // 8) if ct == 0 else (3 * bd // 8)
            stride = w * bpp
            yield ("header", (w, h, bd, ct, bpp))
        elif typ == b"IDAT":
            acc += d.decompress(f.read(ln)); f.read(4)
            while len(acc) >= stride + 1:
                filt = acc[0]
                if filt != 0:
                    raise SystemExit("row %d uses PNG filter %d; this reader handles filter 0 only"
                                     % (row, filt))
                yield ("row", (row, bytes(acc[1:stride + 1])))
                del acc[:stride + 1]
                row += 1
        elif typ == b"IEND":
            break
        else:
            f.read(ln); f.read(4)
    f.close()


def preview(src, dst, size=512):
    """A small thumbnail, so the map's extent can be checked by eye before anything is cut."""
    it = png_stream(src)
    kind, (w, h, bd, ct, bpp) = next(it)
    assert kind == "header"
    step = max(1, h // size)
    cols = min(size, w)
    xs = [min(w - 1, int(i * w / cols)) for i in range(cols)]
    out_rows = []
    for kind, payload in it:
        if kind != "row":
            continue
        r, data = payload
        if r % step:
            continue
        if bd == 16 and ct == 0:
            px = bytes(data[x * 2] for x in xs)                       # high byte of each sample
        elif ct == 2:
            px = bytes(data[x * bpp + c] for x in xs for c in (0,))   # red channel
        else:
            px = bytes(data[x] for x in xs)
        out_rows.append(px)
        if len(out_rows) >= size:
            break
    hh = len(out_rows)
    raw = b"".join(b"\x00" + r for r in out_rows)

    def chunk(t, b):
        c = struct.pack(">I", len(b)) + t + b
        return c + struct.pack(">I", zlib.crc32(t + b) & 0xFFFFFFFF)
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", cols, hh, 8, 0, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 6))
           + chunk(b"IEND", b""))
    open(dst, "wb").write(png)
    print("preview %dx%d -> %s (source %dx%d, %d-bit, colour type %d)" % (cols, hh, dst, w, h, bd, ct))


def cut(src, out_dir, kind, win, down):
    if np is None:
        raise SystemExit("the cut needs numpy: run it under blender -b --python %s -- ..." % __file__)
    os.makedirs(out_dir, exist_ok=True)
    it = png_stream(src)
    _, (w, h, bd, ct, bpp) = next(it)
    lon0, lat0, lon1, lat1 = win
    # source pixel window for the playable extent
    px0 = int((lon0 - SRC_LON0) / (SRC_LON1 - SRC_LON0) * w)
    px1 = int((lon1 - SRC_LON0) / (SRC_LON1 - SRC_LON0) * w)
    py0 = int((SRC_LAT1 - lat1) / (SRC_LAT1 - SRC_LAT0) * h)
    py1 = int((SRC_LAT1 - lat0) / (SRC_LAT1 - SRC_LAT0) * h)
    ow, oh = (px1 - px0) // down, (py1 - py0) // down
    tx_n, ty_n = (ow + TILE - 1) // TILE, (oh + TILE - 1) // TILE
    out_bpp = 2 if (bd == 16 and ct == 0) else 3
    print("source %dx%d -> window px x %d..%d y %d..%d, /%d -> %dx%d, %dx%d tiles of %d"
          % (w, h, px0, px1, py0, py1, down, ow, oh, tx_n, ty_n, TILE), flush=True)
    dt = ">u2" if out_bpp == 2 else np.uint8
    band = np.zeros((TILE, tx_n * TILE, 1 if out_bpp == 2 else 3),
                    dtype=np.uint16 if out_bpp == 2 else np.uint8)
    rows_in_band = 0; ty = 0; written = 0
    for what, payload in it:
        if what != "row":
            continue
        r, data = payload
        if r < py0 or r >= py1 or (r - py0) % down:
            continue
        if out_bpp == 2:
            a = np.frombuffer(data, dtype=">u2")[px0:px1:down].astype(np.uint16)
            if a.size < tx_n * TILE: a = np.pad(a, (0, tx_n * TILE - a.size))
            band[rows_in_band, :, 0] = a[:tx_n * TILE]
        else:
            a = np.frombuffer(data, dtype=np.uint8).reshape(-1, 3)[px0:px1:down]
            if a.shape[0] < tx_n * TILE: a = np.pad(a, ((0, tx_n * TILE - a.shape[0]), (0, 0)))
            band[rows_in_band] = a[:tx_n * TILE]
        rows_in_band += 1
        if rows_in_band == TILE:
            for tx in range(tx_n):
                t = band[:, tx * TILE:(tx + 1) * TILE]
                open(os.path.join(out_dir, "%s_%d_%d.bin" % (kind, tx, ty)), "wb").write(t.tobytes())
                written += 1
            band[:] = 0; rows_in_band = 0; ty += 1
            if ty % 4 == 0: print("  %d of %d tile rows" % (ty, ty_n), flush=True)
        if ty >= ty_n: break
    if rows_in_band:
        for tx in range(tx_n):
            t = band[:, tx * TILE:(tx + 1) * TILE]
            open(os.path.join(out_dir, "%s_%d_%d.bin" % (kind, tx, ty)), "wb").write(t.tobytes())
            written += 1
        ty += 1
    meta = {"kind": kind, "source": os.path.basename(src),
            "lon0": lon0, "lat0": lat0, "lon1": lon1, "lat1": lat1,
            "width": tx_n * TILE, "height": ty * TILE, "tile": TILE,
            "tiles_x": tx_n, "tiles_y": ty, "bytes_per_sample": out_bpp,
            "bit_depth": bd, "colour_type": ct, "downsample": down}
    with open(os.path.join(out_dir, "%s.json" % kind), "w") as f:
        json.dump(meta, f, indent=2)
    print("wrote %d tiles and %s.json" % (written, kind), flush=True)


def main(argv):
    if "--" in argv: argv = argv[argv.index("--"):]       # running under blender
    argv = [a for a in argv if a != "--"]
    if "--preview" in argv:
        i = argv.index("--preview")
        return preview(argv[i + 1], argv[i + 2], int(argv[i + 3]) if len(argv) > i + 3 else 512)
    win = (-130.0, -58.0, 46.0, 72.0)                     # the playable extent (core/types.h)
    down = 2
    if "--window" in argv:
        i = argv.index("--window"); win = tuple(float(v) for v in argv[i + 1].split(",")); del argv[i:i + 2]
    if "--down" in argv:
        i = argv.index("--down"); down = int(argv[i + 1]); del argv[i:i + 2]
    args = [a for a in argv if not a.startswith("-")]
    if len(args) < 3:
        print(__doc__); return 2
    return cut(args[0], args[1], args[2], win, down)


if __name__ == "__main__":
    sys.exit(main(sys.argv) or 0)
