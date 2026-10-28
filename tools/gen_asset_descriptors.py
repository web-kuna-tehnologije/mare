#!/usr/bin/env python3
# Write <name>.asset.json for every exported asset (game/assets/<name>/<name>_LOD0..4.glb, see
# tools/export_assets.py) and regenerate game/assets/manifest.json.
#
#   python3 tools/gen_asset_descriptors.py game/assets
#
# Sizes are world units for the strategic map's towns (a procedural house is ~0.3 tall, the
# church 0.55); roles are what the town generator asks for (asset_get_role). Existing
# descriptors are left alone, so hand tuning survives a re-run.
import json, os, sys
root = sys.argv[1] if len(sys.argv) > 1 else "game/assets"

# role -> (type, fit axis, size, lod distances)
HEIGHTS = {
    "poor_house": 0.26, "small_wooden_house": 0.28, "small_stone_house": 0.28, "artisan_house": 0.32,
    "two_story_townhouse": 0.36, "three_story_townhouse": 0.44, "merchant_house": 0.42, "wealthy_townhouse": 0.46,
    "shop": 0.34, "inn": 0.42, "bakery": 0.34, "blacksmith": 0.32, "market_hall": 0.42, "market_stall": 0.16,
    "town_hall": 0.55, "warehouse": 0.38, "harbor_warehouse": 0.38, "customs_house": 0.42, "church": 0.55,
    "city_gate": 0.50, "defensive_tower": 0.60, "guard_house": 0.30, "stone_wall": 0.25, "watchtower": 0.65,
    "lighthouse": 0.80, "harbor_crane": 0.40, "stone_quay": 0.12, "wooden_pier": 0.10, "windmill": 0.60,
    "fountain": 0.14, "well": 0.12, "wooden_fence": 0.08,
    # ship parts are authored at their own scale and assembled by the ship builder, not placed in towns
    "galley_hull": 2.0, "galley_mainmast": 2.2, "galley_mizzenmast": 1.6, "galley_oar": 0.9,
}
SHIP_PARTS = {"galley_hull", "galley_mainmast", "galley_mizzenmast", "galley_oar"}
PROPS = {"fountain", "well", "wooden_fence", "market_stall", "harbor_crane", "stone_quay", "wooden_pier"}
GROUND = {"cobblestone_road": 1.0, "dirt_road": 1.0, "stone_plaza": 1.2}   # fit by largest extent
# LOD switch points as projected size on a 1080p screen (the renderer selects in screen space):
# LOD 0 (80k tris) only while a house is taller than 220 px, LOD 4 (600 tris) below 12 px
LOD_PX = [0, 220, 90, 30, 12]
NLODS = len(LOD_PX)

names = []
for name in sorted(os.listdir(root)):
    d = os.path.join(root, name)
    if not os.path.isdir(d) or name == "sea": continue
    lods = [f"{name}_LOD{i}.glb" for i in range(NLODS) if os.path.exists(os.path.join(d, f"{name}_LOD{i}.glb"))]
    desc_path = os.path.join(d, f"{name}.asset.json")
    if not lods and not os.path.exists(desc_path): continue
    names.append(name)
    if os.path.exists(desc_path): continue
    if len(lods) < 2: names.pop(); print("skip", name, "(export incomplete:", len(lods), "LODs)"); continue
    if len(lods) < NLODS: print("note:", name, "has", len(lods), "of", NLODS, "LODs")
    if name in GROUND: typ, fit, size = "prop", "max", GROUND[name]
    elif name in SHIP_PARTS: typ, fit, size = "ship_part", "length", HEIGHTS.get(name, 1.0)
    else: typ, fit, size = ("prop" if name in PROPS else "building"), "height", HEIGHTS.get(name, 0.3)
    # Meshy exports face -z in Blender terms, +z after the Y-up export, same as the church
    desc = {
        "name": name, "type": typ, "role": name,
        "lods": [({"file": f, "node": f"{name}_LOD{i}", "dist": 0} if i == 0 else {"file": f, "node": f"{name}_LOD{i}", "px": LOD_PX[i]}) for i, f in enumerate(lods)],
        "scale": {"fit": fit, "size": size},
        "pivot": "base_center", "forward": "+z",
        "materials": [{"match": "*", "rough": 0.85, "metal": 0, "bake_texture": True}],
        "collision": {"type": "aabb"},
    }
    with open(desc_path, "w") as f: json.dump(desc, f, indent=2); f.write("\n")
    print("wrote", desc_path, f"({len(lods)} LODs)")
with open(os.path.join(root, "manifest.json"), "w") as f:
    json.dump({"assets": [f"{n}/{n}.asset.json" for n in names]}, f, indent=2); f.write("\n")
print("manifest:", len(names), "assets")
