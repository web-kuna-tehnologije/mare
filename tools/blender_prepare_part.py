# Prepare one part asset from a Meshy *part-segmentation* GLB (assets/ships/new_masts): keep
# the listed parts, join them, stand the result on its origin and write a clean GLB for
# tools/blender_export_glb.py to turn into LODs. Run headless:
#
#   blender -b --python tools/blender_prepare_part.py -- <src.glb> <dst.glb> \
#       [--keep 0,1,2] [--drop 3,4] [--narrow 0.8] [--pivot foot|center]
#
# Unlike the old tools/blender_split_masts.py (1D k-means over a single welded scan), the new
# mast library arrives already segmented into nodes (model_part0..N, assets/ships/new_masts/
# explanation.txt); what remains is selecting them - e.g. a lateen mast *without* its yard, the
# yard being its own swinging asset (gfx/shipyard.c) - and normalising the stance:
#   foot   (masts)  the pole's x/z centre moves to the origin, the lowest vertex to y = 0
#   center (yards)  keep the authored pivot (the slung point is modelled at the origin)
import bpy, sys, os, re

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
def opt(flag, default=None):
    if flag in argv:
        i = argv.index(flag); v = argv[i + 1]; del argv[i:i + 2]; return v
    return default
keep   = opt("--keep"); drop = opt("--drop")
narrow = float(opt("--narrow", "1.0"))
pivot  = opt("--pivot", "foot")
src, dst = argv[0], argv[1]

bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete()
bpy.ops.import_scene.gltf(filepath=src)
objs = [o for o in bpy.data.objects if o.type == 'MESH']

def part_index(o):
    m = re.search(r'(\d+)$', o.name)
    return int(m.group(1)) if m else 0
sel = set(range(len(objs)))
if keep: sel = {int(x) for x in keep.split(",")}
if drop: sel -= {int(x) for x in drop.split(",")}
kept = [o for o in objs if part_index(o) in sel]
if not kept:
    print("PREPARE FAILED: nothing kept"); sys.exit(1)

# the pole (the kept part with the tallest y extent) defines the x/z centre a mast stands on
def yext(o):
    ys = [(o.matrix_world @ v.co).z for v in o.data.vertices]   # glTF y-up imports as Blender z-up
    return (max(ys) - min(ys)) if ys else 0
pole = max(kept, key=yext)

bpy.ops.object.select_all(action='DESELECT')
for o in objs:
    if o not in kept:
        o.select_set(True)
bpy.ops.object.delete()
for o in kept: o.select_set(True)
bpy.context.view_layer.objects.active = kept[0]
if len(kept) > 1: bpy.ops.object.join()
obj = bpy.context.view_layer.objects.active
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

if pivot == "foot":
    # pole centre in x/y (Blender ground plane), feet to z = 0
    xs = [v.co for v in obj.data.vertices]
    minz = min(v.z for v in xs)
    px = [v for v in obj.data.vertices]      # pole bounds: reuse the whole joined mesh if one part
    # pole x/y centre from the original pole object's bounds (it was joined; recompute from
    # the slender column: vertices in the top half are pole/topmast, not yards)
    top = [v.co for v in obj.data.vertices if v.co.z > minz + 0.75 * (max(c.z for c in xs) - minz)]
    ref = top if len(top) > 8 else xs
    cx = sum(v.x for v in ref) / len(ref); cy = sum(v.y for v in ref) / len(ref)
    for v in obj.data.vertices:
        v.co.x -= cx; v.co.y -= cy; v.co.z -= minz
if narrow != 1.0:
    # thin the spars in plan only (x/y in Blender terms); height is authored
    for v in obj.data.vertices:
        v.co.x *= narrow; v.co.y *= narrow

obj.name = os.path.splitext(os.path.basename(dst))[0]
os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
bpy.ops.export_scene.gltf(filepath=dst, export_format='GLB', use_selection=False)
d = obj.dimensions
print("PREPARED %s: %d verts, dimensions %.3f x %.3f x %.3f (blender xyz)" %
      (dst, len(obj.data.vertices), d.x, d.y, d.z))
