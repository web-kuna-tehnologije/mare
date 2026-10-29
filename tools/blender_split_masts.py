# Split a generated mast assembly into its individual masts, narrow them and stand each one on
# its own origin, ready for tools/meshy_import.py to turn into LODs. Run headless:
#
#   blender -b --python tools/blender_split_masts.py -- <out_dir> <name> <nmasts> [narrow] [rot] --import <model.glb>
#
# Meshy generates a whole rig as one mesh: three masts, their tops and their yards in a single
# object. The game wants them apart, so it can step a foremast, mainmast and mizzen onto the
# stations a class's design sheet gives, at their own heights.
#
# Connectivity is no use for the split - a scan separates into hundreds of fragments, not three
# masts - so the fragments are clustered along the ship's axis (1D k-means on their centroids,
# k = nmasts, seeded evenly across the span). Each cluster is joined, narrowed in x and z (the
# generated spars are chunky for their height; `narrow` defaults to 0.62 and leaves height
# alone), optionally turned about y by `rot` degrees, and moved so its foot sits at the origin
# with the mast centred - which is what lets the game place it by station alone.
#
# Writes <out_dir>/<name>_m0/<name>_m0.glb .. _m<n-1>, ordered bow to stern (+x first).
import bpy, sys, os, math, mathutils

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
src = None
if "--import" in argv:
    i = argv.index("--import"); src = argv[i + 1]; argv = argv[:i] + argv[i + 2:]
out_dir = argv[0] if len(argv) > 0 else "."
name    = argv[1] if len(argv) > 1 else "masts"
nmasts  = int(argv[2]) if len(argv) > 2 else 3
narrow  = float(argv[3]) if len(argv) > 3 else 0.62
rot_deg = float(argv[4]) if len(argv) > 4 else 0.0

bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete()
if not src or not os.path.exists(src):
    print("SPLIT FAILED: no such file %s" % src); sys.exit(1)
bpy.ops.import_scene.gltf(filepath=src)

meshes = [o for o in bpy.data.objects if o.type == 'MESH']
if not meshes:
    print("SPLIT FAILED: no geometry"); sys.exit(1)
bpy.ops.object.select_all(action='DESELECT')
for o in meshes: o.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
if len(meshes) > 1: bpy.ops.object.join()
whole = bpy.context.view_layer.objects.active
bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.mesh.separate(type='LOOSE'); bpy.ops.object.mode_set(mode='OBJECT')
parts = [o for o in bpy.data.objects if o.type == 'MESH']
print("source: %d fragments" % len(parts))

cen = {}
for o in parts:
    vs = [o.matrix_world @ v.co for v in o.data.vertices]
    if not vs: continue
    cen[o.name] = sum(v.x for v in vs) / len(vs)
if not cen:
    print("SPLIT FAILED: no vertices"); sys.exit(1)
lo, hi = min(cen.values()), max(cen.values())

# 1D k-means along the ship's axis, seeded evenly so the clusters come out bow-to-stern
k = max(1, nmasts)
c = [lo + (hi - lo) * (i + 0.5) / k for i in range(k)]
for _ in range(40):
    buckets = [[] for _ in range(k)]
    for n, x in cen.items():
        buckets[min(range(k), key=lambda j: abs(x - c[j]))].append(x)
    moved = 0.0
    for j in range(k):
        if buckets[j]:
            nc = sum(buckets[j]) / len(buckets[j]); moved = max(moved, abs(nc - c[j])); c[j] = nc
    if moved < 1e-5: break

groups = [[] for _ in range(k)]
for o in parts:
    if o.name not in cen: continue
    groups[min(range(k), key=lambda j: abs(cen[o.name] - c[j]))].append(o)
order = sorted(range(k), key=lambda j: -c[j])        # +x (bow) first

def export(obj, path):
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True); bpy.context.view_layer.objects.active = obj
    kw = dict(filepath=path, export_format='GLB', export_apply=True, export_yup=True,
              use_selection=True, export_texcoords=True, export_normals=True,
              export_materials='EXPORT', export_animations=False, export_skins=False)
    try:    bpy.ops.export_scene.gltf(**kw, export_image_format='JPEG', export_jpeg_quality=80)
    except TypeError: bpy.ops.export_scene.gltf(**kw)

written = 0
for slot, j in enumerate(order):
    g = [o for o in groups[j] if len(o.data.vertices) > 0]
    if not g:
        print("  mast %d: empty cluster, skipped" % slot); continue
    bpy.ops.object.select_all(action='DESELECT')
    for o in g: o.select_set(True)
    bpy.context.view_layer.objects.active = g[0]
    if len(g) > 1: bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    ob.name = "%s_m%d" % (name, slot)
    # narrow in x and z, keep the height; then turn if asked
    M = mathutils.Matrix.Diagonal((narrow, narrow, 1.0, 1.0))      # blender is z-up: x,y are the cross-section
    if rot_deg: M = mathutils.Euler((0, 0, math.radians(rot_deg)), 'XYZ').to_matrix().to_4x4() @ M
    ob.matrix_world = M @ ob.matrix_world
    bpy.context.view_layer.update()
    vs = [ob.matrix_world @ v.co for v in ob.data.vertices]
    cx = (max(v.x for v in vs) + min(v.x for v in vs)) * 0.5
    cy = (max(v.y for v in vs) + min(v.y for v in vs)) * 0.5
    zmin = min(v.z for v in vs)
    ob.matrix_world = mathutils.Matrix.Translation((-cx, -cy, -zmin)) @ ob.matrix_world
    bpy.context.view_layer.update()
    vs = [ob.matrix_world @ v.co for v in ob.data.vertices]
    ext = [max(v[i] for v in vs) - min(v[i] for v in vs) for i in range(3)]
    d = os.path.join(out_dir, "%s_m%d" % (name, slot)); os.makedirs(d, exist_ok=True)
    p = os.path.join(d, "%s_m%d.glb" % (name, slot))
    print("  mast %d: %d tris, ext=(%.3f,%.3f,%.3f) -> %s" % (slot, len(ob.data.polygons), ext[0], ext[1], ext[2], p))
    export(ob, p)
    written += 1

print("SPLIT DONE: %d of %d masts from %s" % (written, k, name))
