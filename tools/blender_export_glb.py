# Export one .blend to per-LOD GLB files for the asset runtime (game/src/assets). Run headless:
#
#   blender -b <source.blend> --python tools/blender_export_glb.py -- <out_dir> <Name> [tris0,..] [tex_px]
#   blender -b <source.blend> --python tools/blender_export_glb.py -- --validate
#
# Writes <out_dir>/<Name>_LOD0.glb .. _LOD4.glb, each holding one node named <Name>_LODn (the
# descriptor keys on the node name). Sources that already ship *_LOD0..n objects (hand-authored
# LODs) are exported object by object; a single-mesh source is decimated to the triangle budgets
# given (default 80k, 24k, 7k, 2k, 600). Textures are embedded as JPEG; only the base-colour
# input survives (the game bakes it into vertex colours and ignores the rest), and images are
# downscaled to tex_px (default 1024) since a 4k JPEG costs ~800 ms to decode for colours that
# end up per vertex anyway. Modifiers applied, +Y up.
#
# tools/export_assets.py drives this over a whole source tree; this file handles one .blend.
# `--validate` loads the blend, prints what would be written and exits without touching disk,
# which is what the batch driver's --check uses to prove the pipeline before a real run.
import bpy, sys, os, time

LOD_BUDGETS = [80000, 24000, 7000, 2000, 600]

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
validate = "--validate" in argv
argv = [a for a in argv if a != "--validate"]
out_dir = argv[0] if len(argv) > 0 else "."
name = argv[1] if len(argv) > 1 else "asset"
budgets = [int(x) for x in argv[2].split(",")] if len(argv) > 2 else list(LOD_BUDGETS)
tex_px = int(argv[3]) if len(argv) > 3 else 1024
if not validate: os.makedirs(out_dir, exist_ok=True)
t0 = time.time()

meshes = [o for o in bpy.data.objects if o.type == 'MESH']
ntris_src = sum(len(o.data.polygons) for o in meshes)
print("source: %d mesh objects, %d triangles, %d images" % (len(meshes), ntris_src, len(bpy.data.images)))
if not meshes:
    print("EXPORT FAILED: no mesh objects in this .blend")
    sys.exit(1)

# strip every material input except Base Color so the exporter does not embed roughness/normal maps
for mat in bpy.data.materials:
    if not mat.use_nodes: continue
    nt = mat.node_tree
    for node in list(nt.nodes):
        if node.type == 'BSDF_PRINCIPLED':
            for inp in node.inputs:
                if inp.name != 'Base Color':
                    for link in list(inp.links): nt.links.remove(link)
# downscale the images that remain referenced
for img in bpy.data.images:
    if img.size[0] > tex_px or img.size[1] > tex_px:
        s = tex_px / max(img.size[0], img.size[1])
        if not validate: img.scale(max(1, int(img.size[0] * s)), max(1, int(img.size[1] * s)))
        print("  image %s %dx%d -> %d px" % (img.name, img.size[0], img.size[1], tex_px))

def export_selected(path):
    kw = dict(filepath=path, export_format='GLB', export_apply=True, export_yup=True, use_selection=True,
              export_texcoords=True, export_normals=True, export_materials='EXPORT',
              export_animations=False, export_skins=False)
    try:
        bpy.ops.export_scene.gltf(**kw, export_image_format='JPEG', export_jpeg_quality=80)
    except TypeError:                     # older exporter without the image options
        bpy.ops.export_scene.gltf(**kw)

def select_only(obj):
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj

# hand-authored LODs: objects named *_LOD0 .. *_LODn
lod_objs = {}
for o in meshes:
    low = o.name.lower()
    for i in range(len(budgets)):
        if low.endswith("_lod%d" % i): lod_objs[i] = o
authored = len(lod_objs) >= 2 and 0 in lod_objs

if authored:
    print("authored LODs: %d levels found" % len(lod_objs))
    for i in sorted(lod_objs):
        o = lod_objs[i]
        path = os.path.join(out_dir, "%s_LOD%d.glb" % (name, i))
        print("LOD%d: %d tris (authored) -> %s" % (i, len(o.data.polygons), path))
        if validate: continue
        o.name = "%s_LOD%d" % (name, i)
        select_only(o); export_selected(path)
    # fewer authored levels than budgets: decimate the coarsest one down for the rest
    src = lod_objs[max(lod_objs)]
    start = max(lod_objs) + 1
else:
    # one source mesh (join if several): decimate copies to each budget
    if len(meshes) > 1 and not validate:
        bpy.ops.object.select_all(action='DESELECT')
        for o in meshes: o.select_set(True)
        bpy.context.view_layer.objects.active = meshes[0]
        bpy.ops.object.join()
    src = bpy.context.view_layer.objects.active if (len(meshes) > 1 and not validate) else meshes[0]
    start = 0

for i in range(start, len(budgets)):
    budget = budgets[i]
    ntris = len(src.data.polygons) if not authored else ntris_src
    ratio = min(1.0, budget / float(max(ntris, 1)))
    path = os.path.join(out_dir, "%s_LOD%d.glb" % (name, i))
    print("LOD%d: ratio %.4f of %d tris (budget %d) -> %s" % (i, ratio, ntris, budget, path))
    if validate: continue
    copy = src.copy(); copy.data = src.data.copy(); copy.name = "%s_LOD%d" % (name, i)
    bpy.context.collection.objects.link(copy)
    if ratio < 1.0:
        m = copy.modifiers.new("lod", 'DECIMATE'); m.ratio = ratio; m.use_collapse_triangulate = True
    select_only(copy)
    export_selected(path)
    bpy.data.objects.remove(copy)

print("EXPORT %s in %.0fs: %s" % ("VALIDATED" if validate else "DONE", time.time() - t0, out_dir))
