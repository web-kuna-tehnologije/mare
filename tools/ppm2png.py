#!/usr/bin/env python3
"""Convert the binary PPM screenshots MARE_SCREENSHOT writes into PNG (pure stdlib).

    python3 tools/ppm2png.py shot.ppm [shot.png] [--half]

MARE_SCREENSHOT=path.ppm captures the tonemapped scene before the UI; PPM keeps the capture
path in the game free of an image library, and this turns it into something a browser or a
pull request can show. --half halves each dimension (2x2 box filter) for smaller attachments.
"""
import struct, sys, zlib

def read_ppm(path):
    d = open(path, 'rb').read()
    if not d.startswith(b'P6'): raise SystemExit("%s is not a binary PPM" % path)
    fields, pos = [], 2
    while len(fields) < 3:
        while pos < len(d) and d[pos:pos+1].isspace(): pos += 1
        if d[pos:pos+1] == b'#':
            while d[pos:pos+1] not in (b'\n', b''): pos += 1
            continue
        start = pos
        while pos < len(d) and not d[pos:pos+1].isspace(): pos += 1
        fields.append(int(d[start:pos]))
    pos += 1
    w, h, _maxv = fields
    return w, h, d[pos:pos + w*h*3]

def half(w, h, px):
    w2, h2 = w // 2, h // 2
    out = bytearray(w2 * h2 * 3)
    for y in range(h2):
        r0, r1 = (2*y)*w*3, (2*y+1)*w*3
        for x in range(w2):
            a, b = r0 + 6*x, r1 + 6*x
            for c in range(3):
                out[(y*w2+x)*3+c] = (px[a+c] + px[a+3+c] + px[b+c] + px[b+3+c]) // 4
    return w2, h2, bytes(out)

def write_png(path, w, h, px):
    raw = b''.join(b'\0' + px[y*w*3:(y+1)*w*3] for y in range(h))
    def chunk(t, data):
        c = t + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c) & 0xFFFFFFFF)
    png = (b'\x89PNG\r\n\x1a\n'
           + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(raw, 6))
           + chunk(b'IEND', b''))
    open(path, 'wb').write(png)

def main(argv):
    if len(argv) < 2: print(__doc__); return 2
    src = argv[1]
    dst = argv[2] if len(argv) > 2 and not argv[2].startswith('--') else src.rsplit('.', 1)[0] + '.png'
    w, h, px = read_ppm(src)
    if '--half' in argv: w, h, px = half(w, h, px)
    write_png(dst, w, h, px)
    print("%s -> %s (%dx%d)" % (src, dst, w, h))
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv))
