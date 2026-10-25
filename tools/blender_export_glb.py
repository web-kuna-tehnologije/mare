# Export a .blend to GLB for the asset runtime (game/src/assets). Run headless:
#
#   blender -b <source.blend> --python tools/blender_export_glb.py -- <out.glb> [decimate_ratio <out_lod.glb>]
#
# Textures are embedded (JPEG), modifiers applied, +Y up. Meshy exports already contain
# *_LOD0..3 nodes, which the descriptor selects by node name; the optional second output
# writes an extra decimated copy for sources that ship a single mesh.
import bpy, sys
argv = sys.argv[sys.argv.index("--") + 1:]
out = argv[0]
def export(path):
    kw = dict(filepath=path, export_format='GLB', export_apply=True, export_yup=True,
              export_texcoords=True, export_normals=True, export_materials='EXPORT',
              export_animations=False, export_skins=False)
    try:
        bpy.ops.export_scene.gltf(**kw, export_image_format='JPEG')
    except TypeError:                     # older exporter without the image format option
        bpy.ops.export_scene.gltf(**kw)
print("source vertices:", sum(len(o.data.vertices) for o in bpy.data.objects if o.type == 'MESH'))
export(out)
if len(argv) >= 3:
    ratio, out_lod = float(argv[1]), argv[2]
    for o in bpy.data.objects:
        if o.type == 'MESH':
            m = o.modifiers.new("lod", 'DECIMATE'); m.ratio = ratio
    export(out_lod)
print("export done")
