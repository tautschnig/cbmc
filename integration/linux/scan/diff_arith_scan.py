#!/usr/bin/env python3
"""Differential arithmetic-bounds candidate selector (A+B+C).

Combines the three pivots from
`why-no-new-bugs-retrospective-2026-06.md` into one front-end:

  A (when/scope) -- only look at lines ADDED in a git commit
     range, i.e. fresh code not yet swept by Coverity/Smatch/
     syzkaller.
  B (which property) -- keep only changes that introduce an
     arithmetic / bounds shape CBMC is uniquely good at:
     allocation-size multiplication, copy_from_user/get_user
     length, memdup_user/memcpy length.  These map to the
     existing integer_overflow_in_alloc_size and
     copy_from_user_size_check property modules.
  C (where) -- an optional pathspec restricts the diff to a
     less-swept subtree (e.g. drivers/staging, a vendor BSP).

Output: a focused candidate list (file, new-line, enclosing
function from the diff hunk header, shape, added text) that
tells the existing CBMC harness exactly where to point.

This script does NOT verify; it SELECTS.  Verification is the
B property modules run on the emitted (file, function) pairs.
"""
from __future__ import annotations
import argparse
import csv
import re
import subprocess
import sys

# B shapes: (name, regex on an added line).  Deliberately the
# shapes the arithmetic/bounds property modules already cover.
_SHAPES = [
    ("alloc_mul",
     re.compile(r"\b\w+\s*\*\s*sizeof\b"          # count * sizeof
                r"|\bsizeof\s*\([^()]*\)\s*\*\s*\w"  # sizeof(..) * n
                r"|\b(?:k|kv|v)\w*alloc\w*\s*\(\s*"
                r"\w+\s*\*\s*\w+\s*,")),           # alloc(a * b,
    ("copy_from_user",
     re.compile(r"\bcopy_from_user\s*\(")),
    ("get_user",
     re.compile(r"\bget_user\s*\(")),
    ("memdup_user",
     re.compile(r"\bvmemdup_user|\bmemdup_user\s*\(")),
    ("user_len_memcpy",
     re.compile(r"\bmemcpy\s*\([^;]*\blen\b")),
]

# Lines that are pure noise even if they match a shape.
_SKIP = re.compile(r"^\s*[/*]|^\s*\*|Signed-off-by|^\s*//")


def changed_added(git_dir: str, rng: str, pathspec: list[str]):
    """Yield (file, new_line_no, hunk_func, added_text) for
    every '+' line in the diff of `rng`, using unified=0 so we
    only see truly added lines, and the hunk header's section
    heading as the enclosing-function hint."""
    cmd = ["git", "--git-dir", git_dir, "diff", "--unified=0",
           "--no-color", rng, "--"] + (pathspec or [])
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        sys.exit(f"git diff failed: {p.stderr.strip()}")
    cur_file = None
    new_ln = 0
    hunk_func = ""
    hdr = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,\d+)? @@(.*)")
    for line in p.stdout.splitlines():
        if line.startswith("+++ b/"):
            cur_file = line[6:]
            continue
        m = hdr.match(line)
        if m:
            new_ln = int(m.group(1))
            sec = m.group(2).strip()
            fm = re.search(r"([A-Za-z_]\w*)\s*\(", sec)
            hunk_func = fm.group(1) if fm else sec[:40]
            continue
        if line.startswith("+") and not line.startswith("+++"):
            text = line[1:]
            if cur_file and cur_file.endswith(".c"):
                yield cur_file, new_ln, hunk_func, text
            new_ln += 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--git-dir", required=True)
    ap.add_argument("--range", required=True,
                    help="git revision range, e.g. v7.0..v7.1-rc5")
    ap.add_argument("--path", action="append", default=[],
                    help="pathspec to restrict to (C); repeatable")
    ap.add_argument("--out-csv")
    args = ap.parse_args()

    rows = []
    seen = set()
    for f, ln, fn, text in changed_added(
            args.git_dir, args.range, args.path):
        if _SKIP.search(text):
            continue
        for shape, rx in _SHAPES:
            if rx.search(text):
                key = (f, fn, shape, text.strip())
                if key in seen:
                    continue
                seen.add(key)
                rows.append({"file": f, "line": ln, "func": fn,
                             "shape": shape,
                             "added": text.strip()[:160]})
                break

    by_shape: dict[str, int] = {}
    for r in rows:
        by_shape[r["shape"]] = by_shape.get(r["shape"], 0) + 1
    print(f"candidates: {len(rows)}  "
          f"(distinct funcs: {len({r['func'] for r in rows})})")
    for s, n in sorted(by_shape.items(), key=lambda x: -x[1]):
        print(f"  {n:4}  {s}")

    if args.out_csv:
        with open(args.out_csv, "w", newline="") as fh:
            w = csv.DictWriter(
                fh, ["file", "line", "func", "shape", "added"])
            w.writeheader()
            w.writerows(rows)
        print(f"wrote {args.out_csv}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
