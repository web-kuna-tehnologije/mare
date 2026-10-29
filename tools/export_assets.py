#!/usr/bin/env python3
"""Batch-export every .blend in the art source tree to LOD0..LOD4 GLBs for the game.

    python3 tools/export_assets.py --check                 # no Blender, no writes: list the plan
    python3 tools/export_assets.py --validate              # runs Blender, loads each .blend, writes nothing
    python3 tools/export_assets.py                         # the real export (slow: ~30-60 s per asset)
    python3 tools/export_assets.py --only church,lighthouse
    python3 tools/export_assets.py --force                 # re-export assets that already have GLBs
    python3 tools/export_assets.py --jobs 4                # parallel Blender processes

Sources live outside the repository (the .blend files are large and gitignored); the default
root is ../assets next to the checkout, i.e. the folder holding buildings/ and ships/. Each
<source>.blend becomes game/assets/<name>/<name>_LOD0..4.glb, where <name> comes from the Meshy
export filename: "Meshy_AI_Small_Wooden_House_1024162346_image-to-3d-texture.blend" ->
"small_wooden_house". After exporting, run tools/gen_asset_descriptors.py to write the
descriptors and the manifest (it leaves hand-tuned descriptors alone).

The per-blend work is tools/blender_export_glb.py; this driver finds the files, resolves names,
skips what is already exported, runs Blender headless, and reports failures with their log tail.
"""
import argparse, concurrent.futures, os, re, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
EXPORT_SCRIPT = os.path.join(HERE, "blender_export_glb.py")
NLODS = 6

# Meshy names a file <Prefix>_<Asset_Name>_<digits>_<suffix>.blend. Strip both ends; whatever is
# left, lowercased with underscores, is the asset name the descriptors and the town generator use.
NAME_RE = re.compile(r"^(?:meshy[_-]?ai[_-]?)?(.*?)(?:[_-]\d{6,})?(?:[_-]image[-_]to[-_]3d(?:[-_]texture)?)?$", re.I)

def asset_name(blend_path):
    stem = os.path.splitext(os.path.basename(blend_path))[0]
    m = NAME_RE.match(stem)
    core = (m.group(1) if m else stem).strip("_- ")
    core = re.sub(r"[^A-Za-z0-9]+", "_", core).strip("_").lower()
    return re.sub(r"_+", "_", core) or "asset"

def find_blender(explicit=None):
    cands = [explicit] if explicit else []
    cands += [shutil.which("blender")]
    cands += ["/mnt/c/Program Files/Blender Foundation/Blender %s/blender.exe" % v
              for v in ("5.2", "5.1", "5.0", "4.2", "4.1")]
    cands += ["/Applications/Blender.app/Contents/MacOS/Blender"]
    for c in cands:
        if c and os.path.exists(c): return c
    return None

def short(p):   # readable path: relative when it is inside the tree, absolute when the sources live elsewhere
    try:
        r = os.path.relpath(p)
        return p if r.startswith("..") else r
    except ValueError:
        return p

def find_blends(roots):
    out = []
    for root in roots:
        if os.path.isfile(root) and root.endswith(".blend"): out.append(root); continue
        for dirpath, _dirs, files in os.walk(root):
            for f in sorted(files):
                if f.endswith(".blend") and not f.startswith("."): out.append(os.path.join(dirpath, f))
    return sorted(out)

def already_exported(out_dir, name, nlods):
    return all(os.path.exists(os.path.join(out_dir, "%s_LOD%d.glb" % (name, i))) for i in range(nlods))

def path_for(blender, p):
    """Windows Blender under WSL cannot open /home/... paths: hand it the UNC/drive form."""
    if not blender.lower().endswith(".exe") or os.name == "nt":
        return p
    try:
        return subprocess.run(["wslpath", "-w", p], capture_output=True, text=True,
                              check=True).stdout.strip()
    except Exception:
        return p

def run_one(blender, blend, out_dir, name, budgets, tex_px, validate, timeout, rot=None):
    cmd = [blender, "-b", path_for(blender, blend), "--python", path_for(blender, EXPORT_SCRIPT),
           "--", path_for(blender, out_dir), name, budgets, str(tex_px)]
    if rot: cmd += ["--rot", rot]
    if validate: cmd.append("--validate")
    t0 = time.time()
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        log = (p.stdout or "") + (p.stderr or "")
        ok = p.returncode == 0 and ("EXPORT DONE" in log or "EXPORT VALIDATED" in log)
    except subprocess.TimeoutExpired:
        return name, False, time.time() - t0, "timed out after %d s" % timeout, 0
    if ok and not validate:
        ok = already_exported(out_dir, name, NLODS)
        if not ok: log += "\n(no error reported, but the LOD files are missing)"
    mb = sum(os.path.getsize(os.path.join(out_dir, f)) for f in os.listdir(out_dir)
             if f.startswith(name + "_LOD")) / (1024.0 * 1024.0) if os.path.isdir(out_dir) else 0
    return name, ok, time.time() - t0, log, mb

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sources", nargs="*", default=None, help="source folders or .blend files (default: ../assets)")
    ap.add_argument("--out", default=os.path.join(REPO, "game", "assets"), help="game asset root")
    ap.add_argument("--blender", default=None, help="path to the Blender executable")
    ap.add_argument("--only", default=None, help="comma-separated asset names to export")
    ap.add_argument("--skip", default=None, help="comma-separated asset names to leave out")
    ap.add_argument("--budgets", default="120000,40000,12000,4000,1200,350", help="triangle budget per LOD")
    ap.add_argument("--tex", type=int, default=1024, help="longest texture edge, px")
    ap.add_argument("--rot", default=None, help="rx,ry,rz degrees applied before export (straighten a source modelled at an angle)")
    ap.add_argument("--jobs", type=int, default=1, help="parallel Blender processes")
    ap.add_argument("--timeout", type=int, default=900, help="seconds per asset")
    ap.add_argument("--force", action="store_true", help="re-export assets that already have all LODs")
    ap.add_argument("--check", action="store_true", help="print the plan and exit (no Blender, no writes)")
    ap.add_argument("--validate", action="store_true", help="run Blender on each source but write nothing")
    args = ap.parse_args()

    sources = args.sources or [os.path.join(os.path.dirname(REPO), "assets")]
    sources = [s for s in sources if os.path.exists(s)] or [p for p in ["/mnt/c/projects/mare/assets"] if os.path.exists(p)]
    if not sources:
        print("no source folder found: pass one explicitly (the .blend tree is outside the repo)"); return 2
    if len(args.budgets.split(",")) != NLODS:
        print("--budgets needs %d values (LOD0..LOD%d)" % (NLODS, NLODS - 1)); return 2

    blends = find_blends(sources)
    only = set(args.only.split(",")) if args.only else None
    skip = set(args.skip.split(",")) if args.skip else set()
    plan, seen, dupes = [], {}, []
    for b in blends:
        n = asset_name(b)
        if only and n not in only: continue
        if n in skip: continue
        if n in seen: dupes.append((n, b, seen[n])); continue
        seen[n] = b
        out_dir = os.path.join(args.out, n)
        done = already_exported(out_dir, n, NLODS)
        if done and not args.force: continue
        plan.append((b, out_dir, n))

    print("sources: %s" % ", ".join(sources))
    print("%d .blend files, %d distinct assets, %d to export%s" %
          (len(blends), len(seen), len(plan), " (--force)" if args.force else ""))
    for n, b, first in dupes:
        print("  ! duplicate name %r: ignoring %s (already have %s)" % (n, short(b), short(first)))
    for b, _o, n in plan: print("  %-28s <- %s" % (n, short(b)))
    skipped = len(seen) - len(plan) - len(dupes)
    if skipped > 0: print("  (%d already exported; --force to redo)" % skipped)
    # Residency warning: the GPU budget in gfx/scene.c is 192 MB and a 5-LOD building is ~7-9 MB.
    est = (len(seen)) * 8
    if est > 192: print("  note: %d assets x ~8 MB is ~%d MB of GPU meshes, over the 192 MB residency budget "
                        "in gfx/scene.c - assets will stream in and out (DESIGN_LOD.md)" % (len(seen), est))
    if args.check or not plan:
        if args.check: print("--check: nothing was run")
        return 0

    blender = find_blender(args.blender)
    if not blender:
        print("Blender not found: pass --blender <path>"); return 2
    print("blender: %s" % blender)
    print("%s %d assets with %d jobs..." % ("validating" if args.validate else "exporting", len(plan), args.jobs))

    t0, ok_n, failed = time.time(), 0, []
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as ex:
        futs = [ex.submit(run_one, blender, b, o, n, args.budgets, args.tex, args.validate, args.timeout, args.rot)
                for b, o, n in plan]
        for fut in concurrent.futures.as_completed(futs):
            name, ok, secs, log, mb = fut.result()
            if ok: ok_n += 1; print("  ok   %-28s %5.0f s  %5.1f MB" % (name, secs, mb))
            else:  failed.append((name, log)); print("  FAIL %-28s %5.0f s" % (name, secs))
    print("%d ok, %d failed in %.0f s" % (ok_n, len(failed), time.time() - t0))
    for name, log in failed:
        tail = [l for l in log.splitlines() if l.strip()][-8:]
        print("--- %s\n%s" % (name, "\n".join("    " + l for l in tail)))
    if ok_n and not args.validate:
        print("\nnext: python3 tools/gen_asset_descriptors.py %s" % os.path.relpath(args.out, REPO))
    return 1 if failed else 0

if __name__ == "__main__":
    sys.exit(main())
