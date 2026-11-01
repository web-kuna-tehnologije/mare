#!/usr/bin/env python3
"""Pack the terrain package's river layer into a compact binary the game reads (0.22.14, todo 91).

    python3 -I tools/rivers_pack.py <dataset_root> [min_upstream_m=25e6] [lon0 lon1 lat0 lat1]

Reads chunks/{y}/{x}/waterways.geojsonl (newline-delimited GeoJSON LineStrings in lon/lat with
properties.from_upstream_m) and writes chunks/{y}/{x}/rivers.bin beside it:

    'MRV1' u32 count, then per feature: f32 from_upstream_m, u16 npts, npts x (f32 lon, f32 lat)

Features below the upstream threshold are dropped, as are chunks outside the game's world box.
Half a gigabyte of JSON becomes a few megabytes of floats; world/rivers.cc reads these instead
of generating a drainage network when they are present.
"""
import os, sys, json, struct, time

def main(argv):
    root = argv[1]; thr = float(argv[2]) if len(argv) > 2 else 25e6
    box = [float(v) for v in argv[3:7]] if len(argv) > 6 else [-130.0, 46.0, -58.0, 72.0]
    man = json.load(open(os.path.join(root, 'manifest.json'), encoding='utf-8'))
    west, south, east, north = man['bbox']; nx, ny = man['chunk_count']; C = man['cells_per_chunk']
    dx, dy = man['pixel_step_degrees']
    t0 = time.time(); total_in = total_out = chunks = 0
    for y in range(ny):
        lat_n = north - y * C * dy; lat_s = lat_n - C * dy
        if lat_s > box[3] or lat_n < box[2]: continue
        for x in range(nx):
            lon_w = west + x * C * dx; lon_e = lon_w + C * dx
            if lon_e < box[0] or lon_w > box[1]: continue
            src = os.path.join(root, 'chunks', str(y), str(x), 'waterways.geojsonl')
            if not os.path.exists(src): continue
            feats = []
            with open(src, encoding='utf-8') as f:
                for line in f:
                    if not line.strip(): continue
                    total_in += 1
                    i = line.find('"from_upstream_m":')
                    if i < 0: continue
                    j = line.find(',', i); up = float(line[i + 18:j])
                    if up < thr: continue
                    g = json.loads(line)['geometry']
                    if g['type'] != 'LineString': continue
                    pts = g['coordinates']
                    if len(pts) < 2 or len(pts) > 65535: continue
                    feats.append((up, pts))
            dst = os.path.join(root, 'chunks', str(y), str(x), 'rivers.bin')
            with open(dst, 'wb') as o:
                o.write(b'MRV1'); o.write(struct.pack('<I', len(feats)))
                for up, pts in feats:
                    o.write(struct.pack('<fH', up, len(pts)))
                    o.write(struct.pack('<%df' % (2 * len(pts)), *[c for p in pts for c in p[:2]]))
            total_out += len(feats); chunks += 1
            print('chunk %d/%d: %d of %d features kept' % (y, x, len(feats), total_in), flush=True)
    print('%d chunks, %d features kept of %d, %.0f s' % (chunks, total_out, total_in, time.time() - t0))
    return 0

if __name__ == '__main__': sys.exit(main(sys.argv))
