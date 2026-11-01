"""Decode a video into PNG frames with Blender's sequencer (no ffmpeg needed on this machine).

    blender -b --python tools/vid_frames.py -- <video> <out_dir> [frames=12] [width=960]

Writes <out_dir>/f_<n>.png for `frames` evenly spaced frames. Used to read the issue videos
(assets/issues/*/*.mp4) in a session without a decoder.
"""
import bpy, sys, os

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
video, out = argv[0], argv[1]
nframes = int(argv[2]) if len(argv) > 2 else 12
width = int(argv[3]) if len(argv) > 3 else 960

scene = bpy.context.scene
if scene.sequence_editor is None:
    scene.sequence_editor_create()
se = scene.sequence_editor
strips = se.strips if hasattr(se, "strips") else se.sequences
strip = strips.new_movie("vid", video, channel=1, frame_start=1)
length = strip.frame_final_duration
el = strip.elements[0]
w, h = el.orig_width, el.orig_height
scene.render.resolution_x = width
scene.render.resolution_y = max(1, int(h * width / max(1, w)))
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = "PNG"
scene.render.image_settings.color_mode = "RGB"
scene.frame_start, scene.frame_end = 1, length
os.makedirs(out, exist_ok=True)
print("video %s: %d frames, %dx%d, fps %s" % (video, length, w, h, scene.render.fps))
for i in range(nframes):
    f = 1 + int(i * (length - 1) / max(1, nframes - 1))
    scene.frame_set(f)
    scene.render.filepath = os.path.join(out, "f_%03d.png" % f)
    bpy.ops.render.render(write_still=True)
    print("wrote", scene.render.filepath)
