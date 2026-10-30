#!/usr/bin/env python3
# Write <name>.asset.json for every exported asset (game/assets/<name>/<name>_LOD0..4.glb, see
# tools/export_assets.py) and regenerate game/assets/manifest.json.
#
#   python3 tools/gen_asset_descriptors.py game/assets
#
# Sizes are world units for the strategic map's towns (a procedural house is ~0.3 tall, the
# church 0.55); roles are what the town generator asks for (asset_get_role). Existing
# descriptors are left alone, so hand tuning survives a re-run.
import json, os, re, sys
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
    "galley_hull": 2.0, "galley_mainmast": 2.2, "galley_mizzenmast": 1.6, "galley_oar": 0.26,
}
SHIP_PARTS = {"galley_hull", "galley_mainmast", "galley_mizzenmast", "galley_oar"}
# Parts exported already pointing along +x (the ship's heading-0 axis) must not be rotated
# again: the galley oar is straightened at export time with --rot (see BLENDER_LOD.md).
SHIP_FWD_X = {"galley_oar"}
MAST_PART_RE = re.compile(r"^(.*_masts)_m\d+$")
# Every generated hull and rig came out of Meshy bow-to-the-left: the fine end and the bowsprit
# sit at -x, where the game puts the bow at +x (heading 0). Verified by rendering the plan view
# of all eight hulls and the side view of all eight rigs; "-x" is the 180-degree turn that
# fixes them, and hull and rig agree, so they turn together.

# Gen I ships imported from Meshy (tools/meshy_import.py). Sizes come from the design sheets in
# the art source tree (assets/ships/design): "length (overall)" for a hull, the scale-comparison
# height for a mast assembly, converted with SHIP_M_PER_UNIT - the same factor gfx/shipyard.c
# uses, chosen so a coastal trader lands on the ~0.95 world units the procedural sloop occupied.
SHIP_M_PER_UNIT = 0.045
SHIP_HULLS_M = {        # length overall, metres (design sheet midpoint)
    "coastal_trader_hull": 21, "cog_hull": 25, "fusta_hull": 25, "galiot_hull": 28,
    "armed_cog_hull": 32, "hulk_hull": 35, "carrack_hull": 38, "war_carrack_hull": 47,
}
SHIP_MASTS_M = {        # mast assembly height, metres (sheet height minus freeboard)
    "coastal_trader_masts": 20, "cog_masts": 25, "fusta_masts": 20, "mediterranean_masts": 24,
    "armed_cog_masts": 28, "hulk_masts": 33, "carrack_masts": 37, "war_carrack_masts": 45,
}
# The per-type mast library (assets/ships/new_masts, imported by tools/blender_prepare_part.py
# + blender_export_glb.py): one asset per mast *type*, shared across classes - gfx/shipyard.c
# stands them at each class's stations. Heights are the explanation.txt rig spec against the
# design-sheet hulls. The lateen yard (lantina) is its own asset so it can swing with the wind;
# it keeps its authored pivot (the slung point is modelled at the origin). The bowsprit is a
# fore-and-aft spar: fitted by length, pivot kept (modelled about its own centre).
NEW_MASTS_M = {
    "mast_cog_single": 25, "mast_cog_two": 30, "mast_carrack": 37, "mast_great_carrack": 45,
    "mast_mizzen": 22, "mast_lateen_tall": 24, "lateen_yard": 26,
    # the same sources imported whole (meshy_import.py naming) by a parallel session: keep
    # them correctly typed whichever session regenerates the descriptors first
    "cog_yard_mast": 25, "cog_two_yards_mast": 30, "carrack_mast": 37, "great_carrack_mast": 45,
    "mizzen_mast": 22, "tall_lateen_mast": 24, "separated_lateen_yard": 26,
}
BOWSPRIT_M = {"bowsprit": 15}
# The square yards (0.19): the generic square_yard and the per-mast sets split off the mast
# library by tools/blender_prepare_part.py (--pivot bbox). All lie along model x and must stand
# *across* the ship: forward +z turns them 90 degrees, and they are normalised to a unit span
# across (fit "width" 1.0) about their middle, so gfx/shipyard.cc scales each to the head of
# the sail it carries and braces it about the mast with the canvas.
ACROSS = {"square_yard": 1.0, "yard_cog_single": 1.0, "yard_cog_course": 1.0, "yard_cog_top": 1.0,
          "yard_carrack_course": 1.0, "yard_carrack_top": 1.0,
          "yard_great_course": 1.0, "yard_great_main": 1.0, "yard_great_top": 1.0}
KEEP_PIVOT = {"lateen_yard", "bowsprit"}

# A ship is only ever at LOD 0 when it fills the screen in battle view: the imported LOD 0 is
# the original scan (hundreds of thousands of triangles), so it hands over far sooner than a
# building does.
# Six levels now. LOD 0 is the 400k original and is additionally gated to the battle and
# city tiers in gfx/scene.c, so the strategic map can never reach for it however large a
# ship happens to project.
SHIP_LOD_PX = [0, 700, 260, 90, 30, 10]
# A map chunk is tens of world units across, so it is a large object on screen for a long time:
# it hands over later than a ship and keeps a usable silhouette at the far end.
TILE_LOD_PX = [0, 1200, 640, 300, 130, 50]
PROPS = {"fountain", "well", "wooden_fence", "market_stall", "harbor_crane", "stone_quay", "wooden_pier"}
GROUND = {"cobblestone_road": 1.0, "dirt_road": 1.0, "stone_plaza": 1.2}   # fit by largest extent
# LOD switch points as projected size on a 1080p screen (the renderer selects in screen space):
# LOD 0 (80k tris) only while a house is taller than 220 px, LOD 4 (600 tris) below 12 px
LOD_PX = [0, 320, 150, 60, 22, 9]
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
    if len(lods) < 2 and name not in ACROSS: names.pop(); print("skip", name, "(export incomplete:", len(lods), "LODs)"); continue
    if len(lods) < 2: print("note:", name, "has a single LOD (the sail's own spar stands in at distance, gfx/shipyard.cc)")
    if len(lods) < NLODS: print("note:", name, "has", len(lods), "of", NLODS, "LODs")
    px = LOD_PX
    tile = re.match(r"^tile_l(\d)_(\d+)_(\d+)$", name)
    if tile:
        # A streamed map chunk (game/docs/DESIGN_STREAMING.md): the mesh has to cover its tile
        # exactly, so it is fitted to the tile's world size - 8 degrees for a tactical chunk
        # (L3), 16 for a coastal tile (L2), 4 world units per degree. Pivot at the centre with
        # sea level at y = 0; nudge pivot_offset[1] if the shoreline sits high or low.
        deg = (512 >> int(tile.group(1))) / 8.0        # STREAM_L0_CELLS >> level, CELLS_PER_DEG
        typ, fit, size, px = "terrain", "max", round(deg * 4.0, 2), TILE_LOD_PX
    elif name in GROUND: typ, fit, size = "prop", "max", GROUND[name]
    elif name in SHIP_HULLS_M:
        typ, fit, size, px = "ship", "length", round(SHIP_HULLS_M[name] * SHIP_M_PER_UNIT, 3), SHIP_LOD_PX
    elif name in NEW_MASTS_M:
        typ, fit, size, px = "ship_part", "height", round(NEW_MASTS_M[name] * SHIP_M_PER_UNIT, 3), SHIP_LOD_PX
    elif name in BOWSPRIT_M:
        typ, fit, size, px = "ship_part", "length", round(BOWSPRIT_M[name] * SHIP_M_PER_UNIT, 3), SHIP_LOD_PX
    elif name in ACROSS:
        typ, fit, size, px = "ship_part", "width", ACROSS[name], SHIP_LOD_PX
    elif name in SHIP_MASTS_M or MAST_PART_RE.match(name):
        # A split rig's parts (tools/blender_split_masts.py writes <assembly>_m0.._m2) are
        # fitted to their assembly's height; gfx/shipyard.c then scales each by the class's
        # fore/main/mizzen proportion, so that stays a number you can tune without re-exporting.
        base = name if name in SHIP_MASTS_M else MAST_PART_RE.match(name).group(1)
        typ, fit, size, px = "ship_part", "height", round(SHIP_MASTS_M.get(base, 24) * SHIP_M_PER_UNIT, 3), SHIP_LOD_PX
    elif name in SHIP_PARTS: typ, fit, size = "ship_part", "length", HEIGHTS.get(name, 1.0)
    else: typ, fit, size = ("prop" if name in PROPS else "building"), "height", HEIGHTS.get(name, 0.3)
    # Buildings come out of Meshy facing -z in Blender terms, +z after the Y-up export (same as
    # the church). The ship scans are generated from side-view references and come out lying
    # along x, which is already heading 0, so they must not be rotated.
    forward = "+x" if name in SHIP_FWD_X else "-x" if (name in SHIP_HULLS_M or name in SHIP_MASTS_M or MAST_PART_RE.match(name)
                                                       or name in NEW_MASTS_M or name in BOWSPRIT_M) else "+z"
    # (square_yard falls through to "+z": its long axis must land athwartships)
    desc = {
        "name": name, "type": typ, "role": name,
        "lods": [({"file": f, "node": f"{name}_LOD{i}", "dist": 0} if i == 0 else {"file": f, "node": f"{name}_LOD{i}", "px": px[i]}) for i, f in enumerate(lods)],
        "scale": {"fit": fit, "size": size},
        # A map chunk keeps its own origin: it is modelled with sea level at y = 0, and the
        # streamer places it at the tile centre at y = 0, so re-pivoting would sink or float it.
        "pivot": "keep" if (tile or name in KEEP_PIVOT) else "center" if name in ACROSS else "base_center", "forward": forward,
        "materials": [{"match": "*", "rough": 0.85, "metal": 0, "bake_texture": True}],
        "collision": {"type": "aabb"},
    }
    with open(desc_path, "w") as f: json.dump(desc, f, indent=2); f.write("\n")
    print("wrote", desc_path, f"({len(lods)} LODs)")
with open(os.path.join(root, "manifest.json"), "w") as f:
    json.dump({"assets": [f"{n}/{n}.asset.json" for n in names]}, f, indent=2); f.write("\n")
print("manifest:", len(names), "assets")
