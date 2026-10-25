# Authored assets

Each asset is a directory with a `*.asset.json` descriptor and its glTF/GLB files; `manifest.json`
lists the descriptors the game registers at startup. The format is documented in
`game/src/assets/asset.h`. GLB files are not committed (`.gitignore`): export them from the
source `.blend` files under `/assets` with `tools/blender_export_glb.py` (or copy the exported
`Church_LOD*.glb` from `/assets/buildings/city/commercial_and_civic/`), e.g.

    blender -b assets/buildings/city/commercial_and_civic/Meshy_AI_Church_*.blend --python tools/blender_export_glb.py -- game/assets/church/church.glb
