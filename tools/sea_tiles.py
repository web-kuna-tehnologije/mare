#!/usr/bin/env python3
"""Turn the Meshy sea sheets (assets/sea/sea__new) into the three seamless tiles the sea pass
reads - pure stdlib (zlib), no PIL.

    python3 -I tools/sea_tiles.py <normal.png> <displacement.png> <albedo.png> <out_dir> [--size 1024]

Each sheet is cropped to its inner square (Meshy frames the tile with a dark or grey border:
the crop is found by scanning for the first row/column whose contrast rises above the frame),
resized to --size with a box filter, and made seamless by blending towards a half-period
rolled copy of itself near the edges (14% of the width; the rolled copy wraps by construction,
nothing is mirrored). Writes
sea_normal.png (RGB), sea_displacement.png (RGB, grey), sea_albedo.png (RGB), plus
*_tiled.png 2x2 previews.
"""
import struct, sys, zlib

def read_png(path):
    d = open(path, 'rb').read()
    assert d[:8] == b'\x89PNG\r\n\x1a\n', path
    pos, chunks, w = 8, [], None
    while pos < len(d):
        n, = struct.unpack('>I', d[pos:pos+4]); t = d[pos+4:pos+8]; body = d[pos+8:pos+8+n]; pos += 12 + n
        if t == b'IHDR': w, h, bd, ct = struct.unpack('>IIBB', body[:10]); assert bd == 8, 'need 8-bit'
        elif t == b'IDAT': chunks.append(body)
        elif t == b'IEND': break
    bpp = {2: 3, 6: 4, 0: 1, 4: 2}[ct]
    raw = zlib.decompress(b''.join(chunks)); stride = w * bpp
    out = bytearray(w * h * 3); prev = bytearray(stride); p = 0
    for y in range(h):
        f = raw[p]; line = bytearray(raw[p+1:p+1+stride]); p += 1 + stride
        if f == 1:
            for i in range(bpp, stride): line[i] = (line[i] + line[i-bpp]) & 255
        elif f == 2:
            for i in range(stride): line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride): line[i] = (line[i] + ((line[i-bpp] if i >= bpp else 0) + prev[i]) // 2) & 255
        elif f == 4:
            for i in range(stride):
                a = line[i-bpp] if i >= bpp else 0; b = prev[i]; c = prev[i-bpp] if i >= bpp else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2*c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        o = y * w * 3
        if bpp >= 3:
            for x in range(w): out[o+x*3:o+x*3+3] = line[x*bpp:x*bpp+3]
        else:
            for x in range(w): v = line[x*bpp]; out[o+x*3] = out[o+x*3+1] = out[o+x*3+2] = v
    return w, h, out

def write_png(path, w, h, px):
    raw = b''.join(b'\x00' + bytes(px[y*w*3:(y+1)*w*3]) for y in range(h))
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))

def row_contrast(w, px, y):
    o = y * w * 3; vals = [px[o + x*3] + px[o + x*3 + 1] + px[o + x*3 + 2] for x in range(0, w, 4)]
    return max(vals) - min(vals)
def col_contrast(w, h, px, x):
    vals = [px[(y*w + x)*3] + px[(y*w + x)*3 + 1] + px[(y*w + x)*3 + 2] for y in range(0, h, 4)]
    return max(vals) - min(vals)

def find_crop(w, h, px):
    # the frame is flat (contrast under ~24 over 3 channels); the tile is not. Walk in from each side.
    thr = 30
    y0 = 0
    while y0 < h // 3 and row_contrast(w, px, y0) < thr: y0 += 1
    y1 = h - 1
    while y1 > 2*h // 3 and row_contrast(w, px, y1) < thr: y1 -= 1
    x0 = 0
    while x0 < w // 3 and col_contrast(w, h, px, x0) < thr: x0 += 1
    x1 = w - 1
    while x1 > 2*w // 3 and col_contrast(w, h, px, x1) < thr: x1 -= 1
    # a few pixels in from the frame's soft edge, and square
    pad = 6; x0 += pad; y0 += pad; x1 -= pad; y1 -= pad
    s = min(x1 - x0, y1 - y0)
    cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
    return cx - s // 2, cy - s // 2, s

def crop_resize(w, h, px, x0, y0, s, size):
    out = bytearray(size * size * 3)
    for j in range(size):
        sy0 = y0 + j * s // size; sy1 = max(sy0 + 1, y0 + (j + 1) * s // size)
        for i in range(size):
            sx0 = x0 + i * s // size; sx1 = max(sx0 + 1, x0 + (i + 1) * s // size)
            r = g = b = n = 0
            for y in range(sy0, sy1):
                o = (y * w + sx0) * 3
                for x in range(sx0, sx1):
                    r += px[o]; g += px[o+1]; b += px[o+2]; o += 3; n += 1
            k = (j * size + i) * 3; out[k] = r // n; out[k+1] = g // n; out[k+2] = b // n
    return out

def seamless(size, px, frac=0.14):
    # Shift-and-blend: the tile rolled by half its size wraps continuously by construction (its
    # edges are the original's centre), so near the edges the output is the rolled copy and in
    # the middle the original, with a smooth weight in between. Nothing is mirrored, and the
    # seam error at the wrap is exactly the original's interior continuity.
    band = max(1, int(size * frac)); half = size // 2; out = bytearray(size * size * 3)
    def w1(i):
        d = min(i, size - 1 - i) / band
        if d >= 1.0: return 1.0
        return d * d * (3 - 2 * d)
    wy = [w1(j) for j in range(size)]; wx = [w1(i) for i in range(size)]
    for j in range(size):
        sj = (j + half) % size
        for i in range(size):
            w = wx[i] * wy[j]; si = (i + half) % size
            a = (j * size + i) * 3; b = (sj * size + si) * 3
            if w >= 1.0: out[a:a+3] = px[a:a+3]
            else:
                for c in range(3): out[a + c] = int(px[a + c] * w + px[b + c] * (1 - w) + 0.5)
    return out

def seam_error(size, px):
    e = 0
    for y in range(size):
        a = (y * size) * 3; b = (y * size + size - 1) * 3
        e += abs(px[a] - px[b]) + abs(px[a+1] - px[b+1]) + abs(px[a+2] - px[b+2])
    for x in range(size):
        a = x * 3; b = ((size - 1) * size + x) * 3
        e += abs(px[a] - px[b]) + abs(px[a+1] - px[b+1]) + abs(px[a+2] - px[b+2])
    return e / (2 * size * 3)

def tiled(size, px):
    out = bytearray(size * 2 * size * 2 * 3)
    for y in range(size * 2):
        sy = y % size
        for x in range(size * 2):
            sx = x % size; a = (sy * size + sx) * 3; b = (y * size * 2 + x) * 3; out[b:b+3] = px[a:a+3]
    return out

def main(argv):
    if len(argv) < 5: print(__doc__); return 2
    size = 1024
    if '--size' in argv: size = int(argv[argv.index('--size') + 1])
    names = ['sea_normal', 'sea_displacement', 'sea_albedo']
    outdir = argv[4]
    for src, name in zip(argv[1:4], names):
        w, h, px = read_png(src)
        x0, y0, s = find_crop(w, h, px)
        tile = crop_resize(w, h, px, x0, y0, s, size)
        before = seam_error(size, tile)
        tile = seamless(size, tile)
        if name == 'sea_normal':   # a tangent normal must stay unit-ish after the blend: renormalise
            for k in range(0, size * size * 3, 3):
                nx, ny, nz = tile[k] / 127.5 - 1, tile[k+1] / 127.5 - 1, max(tile[k+2] / 127.5 - 1, 0.05)
                l = (nx*nx + ny*ny + nz*nz) ** 0.5
                tile[k] = int((nx / l + 1) * 127.5 + 0.5); tile[k+1] = int((ny / l + 1) * 127.5 + 0.5); tile[k+2] = int((nz / l + 1) * 127.5 + 0.5)
        after = seam_error(size, tile)
        write_png(f'{outdir}/{name}.png', size, size, tile)
        write_png(f'{outdir}/{name}_tiled.png', size * 2, size * 2, tiled(size, tile))
        print(f'{name}: crop ({x0},{y0}) {s}px of {w}x{h} -> {size}px, wrap error {before:.1f} -> {after:.1f}')
    return 0

if __name__ == '__main__': sys.exit(main(sys.argv))
