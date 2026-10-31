#!/usr/bin/env python3
"""Turn the Meshy sail and rigging sheets into the tiles the mesh pass reads (pure stdlib).

    python3 -I tools/ship_textures.py <src_sails_dir> <src_rigging_dir> <out_dir>

The sheets are already square PBR tiles, so unlike the sea reference sheets there is no frame
to crop: each is measured for its wrap error and only blended seamless (tools/sea_tiles.py's
shift-and-blend) when the error says it needs it. Writes, into <out_dir>:

    sail_premium_albedo.png  sail_premium_normal.png     new canvas, tight weave, bolt ropes
    sail_used_albedo.png     sail_used_normal.png        coarse hessian, frayed seams
    rope_premium.png         rope_used.png               new hemp / tarred and frayed cordage
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sea_tiles import read_png, write_png, seamless, seam_error, tiled

# which sheet is which (identified by eye; see game/docs/DESIGN_SHIPS.md 3d)
SAIL = {
    'sail_premium_albedo': 'Meshy_AI_image_3b29688b-63b2-44c4-b4a4-4b07f705a39b_0.png',
    'sail_premium_normal': 'Meshy_AI_image_486ce30c-c394-4687-83f9-e7e05a9eff39_0.png',
    'sail_used_albedo':    'Meshy_AI_image_a94d67bb-6b24-4128-827b-bd53ff0ddcf5_0.png',
    'sail_used_normal':    'Meshy_AI_image_8e1109ed-a657-49b8-b4d9-bac48caa4b88_0.png',
}
RIG = {
    'rope_premium': 'Meshy_AI_image_acbaf26a-b2c3-483e-8168-e79fec350f4c_0.png',
    'rope_used':    'Meshy_AI_image_094b5e2f-ab5e-41ba-abd5-1b044505b2da_0.png',
}

def renormalise(size, px):
    for k in range(0, size * size * 3, 3):
        nx, ny, nz = px[k] / 127.5 - 1, px[k+1] / 127.5 - 1, max(px[k+2] / 127.5 - 1, 0.05)
        l = (nx*nx + ny*ny + nz*nz) ** 0.5
        px[k] = int((nx / l + 1) * 127.5 + 0.5); px[k+1] = int((ny / l + 1) * 127.5 + 0.5); px[k+2] = int((nz / l + 1) * 127.5 + 0.5)
    return px

def emit(src, dst, is_normal, preview=False):
    w, h, px = read_png(src)
    before = seam_error(w, px)
    if before > 12.0:                       # only touch it when the wrap actually shows
        px = seamless(w, px, 0.10)
        if is_normal: px = renormalise(w, px)
        after = seam_error(w, px)
    else:
        after = before
    write_png(dst, w, h, px)
    if preview: write_png(dst.replace('.png', '_tiled.png'), w * 2, h * 2, tiled(w, px))
    print('%-26s %dx%d wrap %.1f -> %.1f' % (os.path.basename(dst), w, h, before, after))

def main(argv):
    if len(argv) < 4: print(__doc__); return 2
    sails, rig, out = argv[1], argv[2], argv[3]
    os.makedirs(out, exist_ok=True)
    for name, f in SAIL.items(): emit(os.path.join(sails, f), os.path.join(out, name + '.png'), name.endswith('normal'))
    for name, f in RIG.items():  emit(os.path.join(rig, f),   os.path.join(out, name + '.png'), False)
    return 0

if __name__ == '__main__': sys.exit(main(sys.argv))
