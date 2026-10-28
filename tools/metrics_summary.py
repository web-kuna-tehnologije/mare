#!/usr/bin/env python3
"""Summarise a dev-mode metrics CSV (logs/mare_metrics.csv) as a markdown table.

    python3 tools/metrics_summary.py logs/mare_metrics.csv [--cols a,b,c] [--after SECONDS] [--before SECONDS]

Prints count, median, average, 99th percentile and worst per column. The CSV has one row per
0.5 s; comment lines ('# columns changed') and the header lines that follow them are skipped,
so a run in which scopes or metrics registered late still reads. Default columns are the ones
the ROADMAP's before/after captures compare (frame time, CPU, GPU, draws, memory, job lanes,
LOD pops, save cost). `--after` drops the start-up seconds (the first frame costs ~90 ms of GL
warm-up on every run and is not steady state).
"""
import sys, csv, statistics

DEFAULT_COLS = ["frame_ms", "frame.cpu_ms", "gpu.total_ms", "gfx.draw_calls", "gfx.instances",
                "mem.vram_mb", "mem.ram_mb", "jobs.upload_ms", "jobs.queued", "jobs.busy_pct",
                "jobs.wait_ms", "lod.pops", "lod.asset_tris", "asset.loading",
                "save.snapshot_ms", "io.save_ms", "save.count"]

def load(path):
    rows, header = [], None
    with open(path, newline="") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            cells = next(csv.reader([line]))
            if cells[0] == "run_s":          # a (re-emitted) header
                header = cells
                continue
            if header is None:
                continue
            row = {}
            for k, v in zip(header, cells):
                try: row[k] = float(v)
                except ValueError: pass
            rows.append(row)
    return rows

def pct(vals, p):
    if not vals: return float("nan")
    s = sorted(vals); i = min(len(s) - 1, int(round(p * (len(s) - 1))))
    return s[i]

def main(argv):
    if len(argv) < 2 or argv[1] in ("-h", "--help"):
        print(__doc__); return 2
    path, cols, after, before = argv[1], DEFAULT_COLS, 0.0, float("inf")
    i = 2
    while i < len(argv):
        if argv[i] == "--cols": cols = argv[i + 1].split(","); i += 2
        elif argv[i] == "--after": after = float(argv[i + 1]); i += 2
        elif argv[i] == "--before": before = float(argv[i + 1]); i += 2
        else: print("unknown option", argv[i]); return 2
    rows = [r for r in load(path) if after <= r.get("run_s", 0) <= before]
    if not rows:
        print("no rows in", path); return 1
    print(f"{path}: {len(rows)} samples, run {rows[0]['run_s']:.1f}..{rows[-1]['run_s']:.1f} s, sim day {rows[0].get('sim_day', 0):.1f}..{rows[-1].get('sim_day', 0):.1f}")
    print()
    print("| column | n | median | avg | p99 | max |")
    print("|---|---|---|---|---|---|")
    for c in cols:
        vals = [r[c] for r in rows if c in r]
        if not vals:
            print(f"| {c} | 0 | - | - | - | - |"); continue
        print(f"| {c} | {len(vals)} | {statistics.median(vals):.2f} | {statistics.fmean(vals):.2f} | {pct(vals, 0.99):.2f} | {max(vals):.2f} |")
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))
