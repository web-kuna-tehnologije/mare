#!/usr/bin/env python3
"""Import a tree of Meshy AI downloads into the game's asset format (LOD0..LOD4 GLBs).

    python3 tools/meshy_import.py --check                     # no Blender, no writes: list the plan
    python3 tools/meshy_import.py --root ../assets/ships      # the real import
    python3 tools/meshy_import.py --only carrack_hull,cog_masts
    python3 tools/meshy_import.py --kind terrain --root ../assets/map --name tile_l3_12_7
    python3 tools/meshy_import.py --jobs 4 --force

Meshy exports one folder per generated model holding a single `Meshy_AI_model.glb`, named after
the prompt:

    Meshy_AI_armed_cog_hull_faithful_keel_3d_20261029_image-to-3d-texture_d35aeea1/
        Meshy_AI_model.glb

The asset name is the prompt with Meshy's own decorations and the prompt-engineering qualifiers
("faithful", "bare", "keel", "counter", "lateen", bare numbers) stripped, so that folder becomes
the asset `armed_cog_hull`. Each model is decimated to the LOD budgets for its kind and written
to `game/assets/<name>/<name>_LOD0..4.glb`; LOD0 keeps the original scan (budget 0), which is
only ever drawn when a ship fills the screen in battle view.

After importing, run tools/gen_asset_descriptors.py to write the descriptors and the manifest.
The per-model work is tools/blender_export_glb.py --import; this driver finds the folders,
resolves names, skips what is already imported, runs Blender headless, and reports failures.
"""
import argparse, concurrent.futures, json, os, re, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
EXPORT_SCRIPT = os.path.join(HERE, "blender_export_glb.py")
NLODS = 5

# Triangle budgets per LOD and kind (0 = keep the source untouched).
#
# LOD 0 is the "original" level, only ever drawn when the object fills the screen in battle or
# city view. A raw Meshy scan is 0.8-1.8M triangles for a ship hull, which is 45-68 MB of
# vertex data per asset once the runtime has baked textures into vertex colours - a gigabyte
# across the Gen I fleet, for detail far below a pixel even at that range. The cap keeps LOD 0
# at six times LOD 1, which is all the close-up detail a hull has to give, and brings the whole
# fleet back to ~100 MB. Raise it here if you want the untouched scan.
BUDGETS = {
    "ship":      [150000, 24000, 7000, 2000, 600],
    "ship_part": [ 80000, 12000, 4000, 1200, 350],
    "building":  [ 80000, 24000, 7000, 2000, 600],
    "terrain":   [150000, 40000, 12000, 4000, 1200],
    "prop":      [ 40000,  8000, 2500,  800, 250],
}
TEX_PX = {"ship": 1024, "ship_part": 512, "building": 1024, "terrain": 2048, "prop": 512}

# Meshy decorates the prompt; these tokens are prompt engineering, not part of the asset name.
QUALIFIERS = {"faithful", "bare", "keel", "counter", "lateen", "gen", "v1", "v2", "final",
              "clean", "simple", "detailed", "texture", "textured"}
STOP = {"3d", "image"}          # everything from here on is Meshy's own suffix


def asset_name(folder):
    """Meshy folder name -> asset name (see the module docstring)."""
    toks = [t for t in re.split(r"[^A-Za-z0-9]+", folder) if t]
    out = []
    for i, t in enumerate(toks):
        low = t.lower()
        if i < 2 and low in ("meshy", "ai"):  continue
        if low in STOP or re.fullmatch(r"\d{6,}", low): break   # Meshy suffix starts here
        if low in QUALIFIERS or low.isdigit(): continue         # prompt engineering
        out.append(low)
    name = "_".join(out) or "asset"
    name = re.sub(r"(^|_)mast($|_)", r"\1masts\2", name)        # "cog_mast" -> "cog_masts"
    return name


def find_models(root):
    """[(asset_name, glb_path)] for every Meshy download under `root`."""
    found = []
    for entry in sorted(os.listdir(root)):
        d = os.path.join(root, entry)
        if not os.path.isdir(d):
            continue
        glbs = [f for f in sorted(os.listdir(d)) if f.lower().endswith(".glb")]
        if not glbs:
            continue
        # Meshy writes exactly one model per folder; if several, take the largest (the textured one)
        glb = max(glbs, key=lambda f: os.path.getsize(os.path.join(d, f)))
        found.append((asset_name(entry), os.path.join(d, glb)))
    return found


def find_blender(explicit=None):
    cands = [explicit] if explicit else []
    cands += ["blender", "/usr/bin/blender", "/Applications/Blender.app/Contents/MacOS/Blender"]
    cands += sorted(
        ["/mnt/c/Program Files/Blender Foundation/%s/blender.exe" % d
         for d in (os.listdir("/mnt/c/Program Files/Blender Foundation")
                   if os.path.isdir("/mnt/c/Program Files/Blender Foundation") else [])],
        reverse=True)
    for c in cands:
        if not c:
            continue
        if os.path.sep in c and not os.path.exists(c):
            continue
        try:
            subprocess.run([c, "-v"], capture_output=True, timeout=120)
            return c
        except Exception:
            continue
    return None


def path_for(blender, p):
    """Windows Blender under WSL cannot open /home/... paths: hand it the UNC/drive form."""
    if not blender.lower().endswith(".exe") or os.name == "nt":
        return p
    try:
        return subprocess.run(["wslpath", "-w", p], capture_output=True, text=True,
                              check=True).stdout.strip()
    except Exception:
        return p


def run_one(blender, name, glb, out_root, kind, validate, timeout):
    out_dir = os.path.join(out_root, name)
    budgets = ",".join(str(b) for b in BUDGETS[kind])
    cmd = [blender, "-b", "--python", path_for(blender, EXPORT_SCRIPT), "--",
           path_for(blender, out_dir), name, budgets, str(TEX_PX[kind]),
           "--import", path_for(blender, glb)]
    if validate:
        cmd.append("--validate")
    t0 = time.time()
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    ok = p.returncode == 0 and ("EXPORT DONE" in p.stdout or "EXPORT VALIDATED" in p.stdout)
    tris = ""
    m = re.search(r"source: \d+ mesh objects, (\d+) triangles", p.stdout)
    if m:
        tris = "%s tris" % m.group(1)
    if not ok:
        tail = "\n".join((p.stdout + p.stderr).strip().splitlines()[-12:])
        return name, False, time.time() - t0, tris, tail
    if not validate:                       # a sidecar so gen_asset_descriptors knows the kind
        with open(os.path.join(out_dir, "%s.meshy.json" % name), "w") as f:
            json.dump({"name": name, "kind": kind, "source": os.path.basename(os.path.dirname(glb)),
                       "lod_budgets": BUDGETS[kind], "tex_px": TEX_PX[kind]}, f, indent=2)
    return name, True, time.time() - t0, tris, ""


def organize(models, dest, move=False):
    """Copy each Meshy download to <dest>/<asset name>/<asset name>.glb.

    Meshy names every download `Meshy_AI_model.glb` inside a folder named after the prompt,
    which makes the art tree unreadable. This leaves a tree you can navigate: one folder per
    asset, the source mesh inside it under the name the game knows it by.
    """
    import shutil
    os.makedirs(dest, exist_ok=True)
    done = 0
    for name, glb in models:
        out_dir = os.path.join(dest, name)
        out = os.path.join(out_dir, name + ".glb")
        if os.path.exists(out) and os.path.getsize(out) == os.path.getsize(glb):
            print("%-26s already there" % name); done += 1; continue
        os.makedirs(out_dir, exist_ok=True)
        print("%-26s %s %6.1f MB -> %s" % (name, "move" if move else "copy",
                                           os.path.getsize(glb) / 1e6, out))
        (shutil.move if move else shutil.copy2)(glb, out)
        with open(os.path.join(out_dir, "source.txt"), "w") as f:
            f.write("%s\n" % os.path.basename(os.path.dirname(glb)))
        done += 1
    print("\n%d model(s) organised under %s" % (done, dest))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default=os.path.join(os.path.dirname(REPO), "assets", "ships"),
                    help="folder holding the Meshy download folders")
    ap.add_argument("--out", default=os.path.join(REPO, "game", "assets"))
    ap.add_argument("--kind", default="ship", choices=sorted(BUDGETS), help="LOD budgets and descriptor type")
    ap.add_argument("--name", help="force the asset name (single-folder imports, e.g. a map chunk)")
    ap.add_argument("--only", help="comma separated asset names to import")
    ap.add_argument("--organize", metavar="DEST",
                    help="copy each download to DEST/<asset name>/<asset name>.glb and exit (tidies the art tree)")
    ap.add_argument("--move", action="store_true", help="with --organize: move instead of copying")
    ap.add_argument("--check", action="store_true", help="list the plan; no Blender, no writes")
    ap.add_argument("--validate", action="store_true", help="run Blender but write nothing")
    ap.add_argument("--force", action="store_true", help="re-import assets that already have GLBs")
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--blender")
    ap.add_argument("--timeout", type=int, default=3600, help="seconds per model")
    a = ap.parse_args()

    if not os.path.isdir(a.root):
        sys.exit("no such folder: %s" % a.root)
    models = find_models(a.root)
    if a.name:
        if len(models) != 1:
            sys.exit("--name needs exactly one model under --root (found %d)" % len(models))
        models = [(a.name, models[0][1])]
    if a.only:
        want = {s.strip() for s in a.only.split(",")}
        models = [m for m in models if m[0] in want]
        missing = want - {m[0] for m in models}
        if missing:
            sys.exit("not found under %s: %s" % (a.root, ", ".join(sorted(missing))))
    if not models:
        sys.exit("no Meshy model folders found under %s" % a.root)
    if a.organize:
        return organize(models, a.organize, a.move)

    todo = []
    for name, glb in models:
        done = all(os.path.exists(os.path.join(a.out, name, "%s_LOD%d.glb" % (name, i))) for i in range(NLODS))
        mark = "skip (imported)" if done and not a.force else "import"
        print("%-26s %-15s %6.1f MB  <- %s" % (name, mark, os.path.getsize(glb) / 1e6,
                                               os.path.basename(os.path.dirname(glb))))
        if not done or a.force:
            todo.append((name, glb))
    print("\n%d model(s) to import, kind '%s', budgets %s -> %s"
          % (len(todo), a.kind, BUDGETS[a.kind], a.out))
    if a.check or not todo:
        return 0

    blender = find_blender(a.blender)
    if not blender:
        sys.exit("Blender not found: pass --blender /path/to/blender")
    print("blender: %s\n" % blender)

    t0, fails = time.time(), []
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, a.jobs)) as ex:
        futs = [ex.submit(run_one, blender, n, g, a.out, a.kind, a.validate, a.timeout) for n, g in todo]
        for f in concurrent.futures.as_completed(futs):
            name, ok, secs, tris, tail = f.result()
            print("%-26s %-5s %6.0fs  %s" % (name, "ok" if ok else "FAIL", secs, tris))
            if not ok:
                fails.append((name, tail))
    for name, tail in fails:
        print("\n--- %s failed:\n%s" % (name, tail))
    print("\n%d/%d imported in %.0fs" % (len(todo) - len(fails), len(todo), time.time() - t0))
    if not a.validate and not fails:
        print("next: python3 tools/gen_asset_descriptors.py game/assets")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
