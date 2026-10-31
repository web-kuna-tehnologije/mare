# Encode a BMP/PNG frame sequence into an MP4 with Blender's bundled ffmpeg (no ffmpeg on the
# machine is needed). Run headless:
#
#   blender -b --python tools/blender_encode_mp4.py -- <frames_dir> <fps> <out.mp4> [pattern]
#
# <frames_dir> holds frame_00000.bmp .. (the game's MARE_RECORD output, main.cc). The strip is
# loaded into the sequencer, the scene takes the first frame's size and the fps given, and the
# animation renders straight to H.264 in an MP4 container at a high constant quality. The
# Blender 4.4+ API calls the sequencer's strips `strips`; older builds `sequences` - both work.
import bpy, os, sys

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
if len(argv) < 3:
    print("usage: blender -b --python blender_encode_mp4.py -- <frames_dir> <fps> <out.mp4> [glob-prefix]")
    sys.exit(2)
frames_dir, fps, out = argv[0], float(argv[1]), argv[2]
prefix = argv[3] if len(argv) > 3 else "frame_"

files = sorted(f for f in os.listdir(frames_dir) if f.startswith(prefix) and f.lower().endswith((".bmp", ".png", ".tga", ".jpg")))
if not files:
    print("ENCODE FAILED: no frames under %s" % frames_dir)
    sys.exit(1)
print("encoding %d frames from %s at %g fps -> %s" % (len(files), frames_dir, fps, out))

scene = bpy.context.scene
scene.sequence_editor_create()
se = scene.sequence_editor
coll = se.strips if hasattr(se, "strips") else se.sequences   # 4.4+ renamed it; an empty collection is falsy, so test the name, not the value
strip = coll.new_image(name="trailer", filepath=os.path.join(frames_dir, files[0]), channel=1, frame_start=1)
for f in files[1:]:
    strip.elements.append(f)

# the first frame's size sets the output size
img = bpy.data.images.load(os.path.join(frames_dir, files[0]))
w, h = img.size[0], img.size[1]
bpy.data.images.remove(img)

scene.render.resolution_x = w
scene.render.resolution_y = h
scene.render.resolution_percentage = 100
scene.render.fps = int(round(fps)); scene.render.fps_base = 1.0
scene.frame_start = 1
scene.frame_end = len(files)
if hasattr(scene.render.image_settings, 'media_type'):      # Blender 4.5+/5.x: the movie formats live under media type VIDEO
    scene.render.image_settings.media_type = 'VIDEO'
scene.render.image_settings.file_format = 'FFMPEG'
scene.render.ffmpeg.format = 'MPEG4'
scene.render.ffmpeg.codec = 'H264'
scene.render.ffmpeg.constant_rate_factor = 'HIGH'
scene.render.ffmpeg.ffmpeg_preset = 'GOOD'
scene.render.ffmpeg.gopsize = int(round(fps))
scene.render.ffmpeg.audio_codec = 'NONE'
scene.render.filepath = out
# the sequencer renders without a camera; make sure nothing in the default scene is drawn
scene.render.use_sequencer = True
for o in list(bpy.data.objects):
    bpy.data.objects.remove(o)
bpy.ops.render.render(animation=True)
print("ENCODE DONE: %s (%dx%d, %d frames, %g fps)" % (out, w, h, len(files), fps))
