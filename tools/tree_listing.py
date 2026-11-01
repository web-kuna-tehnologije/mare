#!/usr/bin/env python3
"""Write docs/FOLDER_STRUCTURE.md: the repository tree and the asset store tree, as a listing.

    python3 -I tools/tree_listing.py [repo_root] [assets_root] [out.md]

Defaults: the repo this script lives in, <repo>/assets (the store, not in git), docs/FOLDER_STRUCTURE.md.
Every directory is listed with its file count and size; files are listed in full except inside
folders that hold hundreds of same-shaped files (the terrain chunks, LOD GLB sets), which are
summarised as a pattern. Pure stdlib. 0.22.14.
"""
import os, sys, re, time

SKIP_DIRS = {'.git', '__pycache__', 'build', 'ccobj', 'tmp', '.vs', 'logs', 'test_logs', 'test_diag_logs'}
SUMMARISE_OVER = 24      # a folder with more files than this is summarised by extension / pattern

def human(n):
    for u in ('B', 'KB', 'MB', 'GB', 'TB'):
        if n < 1024 or u == 'TB': return '%.0f %s' % (n, u) if u == 'B' else '%.1f %s' % (n, u)
        n /= 1024.0

def dir_stats(path):
    files, size = 0, 0
    for root, dirs, fs in os.walk(path):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for f in fs:
            try: size += os.path.getsize(os.path.join(root, f)); files += 1
            except OSError: pass
    return files, size

def pattern(names):
    """collapse foo_LOD0.glb .. foo_LOD5.glb and chunk numbers into patterns"""
    pats = {}
    for n in names:
        p = re.sub(r'\d+', '#', n)
        pats.setdefault(p, []).append(n)
    return pats

def walk(root, out, depth=0, max_depth=8, rel=''):
    try: entries = sorted(os.listdir(root))
    except OSError: return
    dirs = [e for e in entries if os.path.isdir(os.path.join(root, e)) and e not in SKIP_DIRS]
    files = [e for e in entries if os.path.isfile(os.path.join(root, e))]
    ind = '  ' * depth
    if len(files) > SUMMARISE_OVER:
        pats = pattern(files)
        for p, ns in sorted(pats.items(), key=lambda kv: -len(kv[1])):
            out.append('%s- `%s` x%d' % (ind, p, len(ns)) if len(ns) > 1 else '%s- `%s`' % (ind, ns[0]))
    else:
        for f in files:
            try: sz = os.path.getsize(os.path.join(root, f))
            except OSError: sz = 0
            out.append('%s- `%s` (%s)' % (ind, f, human(sz)))
    for d in dirs:
        full = os.path.join(root, d)
        nf, sz = dir_stats(full)
        out.append('%s- **%s/** - %d files, %s' % (ind, d, nf, human(sz)))
        if depth + 1 < max_depth:
            # the terrain package's chunk grid: one sample chunk, the rest counted
            if d == 'chunks':
                sub = sorted(e for e in os.listdir(full) if os.path.isdir(os.path.join(full, e)))
                out.append('%s  - rows `%s` .. `%s` (%d), each with the columns `0`..`26`; one chunk listed:' % (ind, sub[0], sub[-1], len(sub)) if sub else '')
                if sub:
                    cols = sorted(e for e in os.listdir(os.path.join(full, sub[0])) if os.path.isdir(os.path.join(full, sub[0], e)))
                    if cols: walk(os.path.join(full, sub[0], cols[0]), out, depth + 2, max_depth)
                continue
            walk(full, out, depth + 1, max_depth)

def main(argv):
    here = os.path.dirname(os.path.abspath(__file__))
    repo = argv[1] if len(argv) > 1 else os.path.dirname(here)
    assets = argv[2] if len(argv) > 2 else os.path.join(repo, 'assets')
    outp = argv[3] if len(argv) > 3 else os.path.join(repo, 'docs', 'FOLDER_STRUCTURE.md')
    out = ['# Folder structure', '',
           'Generated %s by `tools/tree_listing.py` (0.22.14). Directories in bold with their file count and size; folders with many same-shaped files are summarised as patterns (`#` = digits). `build/`, `tmp/`, logs and `.git` are left out.' % time.strftime('%Y-%m-%d %H:%M'),
           '', '## The repository (`%s`)' % repo, '']
    nf, sz = dir_stats(repo) if not os.path.samefile(repo, os.path.dirname(assets)) else (0, 0)
    walk_root = repo
    # the repo without the store (assets/ is listed on its own below)
    saved = SKIP_DIRS.copy(); SKIP_DIRS.add('assets')
    walk(walk_root, out)
    SKIP_DIRS.clear(); SKIP_DIRS.update(saved)
    if os.path.isdir(assets):
        nf, sz = dir_stats(assets)
        out += ['', '## The asset store (`%s`, not in git) - %d files, %s' % (assets, nf, human(sz)), '']
        walk(assets, out)
    with open(outp, 'w', encoding='utf-8') as f: f.write('\n'.join(out) + '\n')
    print('wrote', outp, len(out), 'lines')
    return 0

if __name__ == '__main__': sys.exit(main(sys.argv))
