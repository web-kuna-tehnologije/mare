# Export a .blend to per-LOD GLB files for the asset runtime (game/src/assets). Run headless:
#
#   blender -b <source.blend> --python tools/blender_export_glb.py -- <out_dir> <Name> [tris0,tris1,tris2,tris3] [tex_px]
#
# Writes <out_dir>/<Name>_LOD0.glb .. _LOD3.glb, each holding one node named <Name>_LODn (the
# descriptor keys on the node name). Sources that already ship *_LOD0..3 objects (Meshy exports
# such as the church) are exported object by object; a single-mesh source is decimated to the
# triangle budgets given (default 80k, 24k, 7k, 2k). Textures are embedded as JPEG; only the
# base-colour input survives (the game bakes it into vertex colours and ignores the rest), and
# images are downscaled to tex_px (default 1024) since a 4k JPEG costs ~800 ms to decode for
# colours that end up per vertex anyway. Modifiers applied, +Y up.
import bpy, sys, os, time
argv = sys.argv[sys.argv.index("--") + 1:]
out_dir, name = argv[0], argv[1]
budgets = [int(x) for x in (argv[2].split(",") if len(argv) > 2 else "80000,24000,7000,2000".split(","))]
tex_px = int(argv[3]) if len(argv) > 3 else 1024
os.makedirs(out_dir, exist_ok=True)
t0 = time.time()

meshes = [o for o in bpy.data.objects if o.type == 'MESH']
print("source: %d mesh objects, %d triangles" % (len(meshes), sum(len(o.data.polygons) for o in meshes)))

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
        img.scale(max(1, int(img.size[0] * s)), max(1, int(img.size[1] * s)))

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

lod_objs = {}
for o in meshes:
    low = o.name.lower()
    for i in range(4):
        if low.endswith("_lod%d" % i): lod_objs[i] = o
if len(lod_objs) == 4:
    for i in range(4):
        o = lod_objs[i]; o.name = "%s_LOD%d" % (name, i)
        select_only(o); export_selected(os.path.join(out_dir, "%s_LOD%d.glb" % (name, i)))
        print("LOD%d: %d tris (authored)" % (i, len(o.data.polygons)))
else:
    # one source mesh (join if several): decimate copies to each budget
    if len(meshes) > 1:
        bpy.ops.object.select_all(action='DESELECT')
        for o in meshes: o.select_set(True)
        bpy.context.view_layer.objects.active = meshes[0]
        bpy.ops.object.join()
    src = bpy.context.view_layer.objects.active if len(meshes) > 1 else meshes[0]
    ntris = len(src.data.polygons)
    for i, budget in enumerate(budgets):
        copy = src.copy(); copy.data = src.data.copy(); copy.name = "%s_LOD%d" % (name, i)
        bpy.context.collection.objects.link(copy)
        ratio = min(1.0, budget / float(ntris))
        if ratio < 1.0:
            m = copy.modifiers.new("lod", 'DECIMATE'); m.ratio = ratio; m.use_collapse_triangulate = True
        select_only(copy)
        export_selected(os.path.join(out_dir, "%s_LOD%d.glb" % (name, i)))
        print("LOD%d: ratio %.4f of %d tris (budget %d)" % (i, ratio, ntris, budget))
        bpy.data.objects.remove(copy)
print("export done in %.0fs: %s" % (time.time() - t0, out_dir))
