# Build the two seamless sea tiles from the reference sheet assets/sea/sea_texture_normals.png
# (right half: tangent-space normal map; top-left quadrant: sea surface albedo with foam).
#
#   python tools/sea_textures.py <reference.png> <out_dir>      -> sea_normal.png, sea_albedo.png, *_tiled.png previews
#
# Neither region tiles on its own. Each tile is cut as a 448 px square plus a 64 px margin of
# real neighbouring content, and the first 64 px of every row/column are cross-faded with that
# margin, so the right edge continues seamlessly into the left edge when the texture repeats
# (same for top/bottom). Normals are blended as vectors and renormalised. The result is
# resampled to 512 px. Pure PIL, no numpy (the Windows Python on the dev box has only PIL).
import sys, math
from PIL import Image, ImageFilter, ImageChops

W, M, OUT = 448, 64, 512

def smooth(t):
    t = min(max(t, 0.0), 1.0)
    return t * t * (3 - 2 * t)

def seamless(src, normal):
    """src: image at least (W+M)x(W+M). Returns a WxW tile that wraps in both axes."""
    sp = src.load()
    out = Image.new("RGB", (W, W))
    op = out.load()
    def get(x, y):
        p = sp[x, y]
        if normal:
            return (p[0] / 127.5 - 1, p[1] / 127.5 - 1, p[2] / 127.5 - 1)
        return (p[0] / 255.0, p[1] / 255.0, p[2] / 255.0)
    def lerp(a, b, t):
        return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t)
    def sample_x(x, y):
        v = get(x, y)
        if x < M:                       # left band: fade in from the content beyond the right edge
            v = lerp(get(x + W, y), v, smooth((x + 0.5) / M))
        return v
    for y in range(W):
        wy = smooth((y + 0.5) / M) if y < M else 1.0
        for x in range(W):
            v = sample_x(x, y)
            if y < M:
                v = lerp(sample_x(x, y + W), v, wy)
            if normal:
                l = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) or 1.0
                v = (v[0] / l, v[1] / l, max(v[2] / l, 0.0))
                op[x, y] = (int(round((v[0] * 0.5 + 0.5) * 255)), int(round((v[1] * 0.5 + 0.5) * 255)), int(round((v[2] * 0.5 + 0.5) * 255)))
            else:
                op[x, y] = (int(round(v[0] * 255)), int(round(v[1] * 255)), int(round(v[2] * 255)))
    out = out.resize((OUT, OUT), Image.LANCZOS)
    if normal:                          # resampling shortens the vectors a little: renormalise
        p = out.load()
        for y in range(OUT):
            for x in range(OUT):
                c = p[x, y]
                v = (c[0] / 127.5 - 1, c[1] / 127.5 - 1, c[2] / 127.5 - 1)
                l = math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]) or 1.0
                p[x, y] = (int(round((v[0] / l * 0.5 + 0.5) * 255)), int(round((v[1] / l * 0.5 + 0.5) * 255)), int(round((v[2] / l * 0.5 + 0.5) * 255)))
    return out

def tiled_preview(tile):
    t = Image.new("RGB", (tile.width * 2, tile.height * 2))
    for i in range(2):
        for j in range(2):
            t.paste(tile, (i * tile.width, j * tile.height))
    return t.resize((tile.width, tile.height), Image.BILINEAR)

ref = Image.open(sys.argv[1]).convert("RGB")
outdir = sys.argv[2].rstrip("/\\")
assert ref.size == (1024, 1024), ref.size
normal = seamless(ref.crop((512, 0, 1024, 512)), True)        # right half, top 512 rows (W+M = 512)
# the photo has a top-to-bottom brightness gradient that would read as a band when repeated:
# keep only what is above ~1/8 of the tile in frequency (subtract a wide blur, add the mean back)
alb = ref.crop((0, 0, 512, 512))
low = alb.filter(ImageFilter.GaussianBlur(48))
mean = Image.new("RGB", alb.size, tuple(int(round(v)) for v in low.resize((1, 1), Image.BOX).getpixel((0, 0))))
alb = ImageChops.add(ImageChops.subtract(alb, low, 1.0, 128), ImageChops.subtract(mean, Image.new("RGB", alb.size, (128, 128, 128)), 1.0, 128), 1.0, -128)
albedo = seamless(alb, False)                                  # top-left quadrant
normal.save(outdir + "/sea_normal.png", optimize=True)
albedo.save(outdir + "/sea_albedo.png", optimize=True)
tiled_preview(normal).save(outdir + "/sea_normal_tiled.png")
tiled_preview(albedo).save(outdir + "/sea_albedo_tiled.png")
print("wrote", outdir, "sea_normal.png sea_albedo.png (+ 2x2 tiled previews)")
