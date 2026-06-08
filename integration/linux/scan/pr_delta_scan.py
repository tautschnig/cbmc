#!/usr/bin/env python3
"""Differential PR-delta scan (pivot A: the discovery play).

Instead of "is this function buggy?", ask "does this PATCH introduce a
bug-shape into a function it touched?".  We:

  1. parse `git diff RANGE` to get the changed line ranges per file,
  2. map those ranges to the FUNCTIONS they touch (via func_ranges.ql),
  3. run the seven oracles over the tree's CodeQL DB,
  4. keep only hits whose (file, function) was touched by the diff,
  5. emit a "patch-touched candidate" table.

This points the existing detectors at FRESH code (the window between
"merged" and "swept by Coverity/Smatch/syzkaller"), which is where live
bugs actually are -- per why-no-new-bugs-retrospective-2026-06.md.

Usage:
  pr_delta_scan.py --tree /path/to/tree --db /path/to/codeql-db \
      --base v7.1-rc6 --head v7.1-rc7 [--paths net/ fs/] [--unwind 10]

Honest scope: a "patch-touched" hit is a CANDIDATE -- the patch modified
a function that matches a bug shape.  It is NOT proof the patch
introduced the bug (the shape may predate it, or be guarded); it is the
high-signal subset to review first.  Pair with CBMC stage-2 for the
shapes that have faithful harnesses.
"""
import argparse
import os
import re
import subprocess
import sys

ORACLES = [
    "tlv_parse_loop.ql",
    "tlv_parse_loop_helper.ql",
    "tainted_into_fixed_dest.ql",
    "tainted_alloc_overflow.ql",
    "tainted_count_into_fixed_array.ql",
    "skb_field_before_lencheck.ql",
    "decoded_len_arith_overflow.ql",
]

HERE = os.path.dirname(os.path.abspath(__file__))
QLDIR = os.path.join(HERE, "abc-refinement")
PACKS = os.environ.get("CODEQL_PACKS", "/home/ubuntu/codeql/qlpacks")


def sh(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def changed_ranges(tree, base, head, paths):
    """relpath -> list of (start,end) changed line ranges on the +/new side."""
    cmd = ["git", "-C", tree, "diff", "--unified=0", base, head, "--"]
    cmd += paths if paths else ["."]
    out = sh(cmd).stdout
    ranges = {}
    cur = None
    for ln in out.splitlines():
        if ln.startswith("+++ b/"):
            cur = ln[6:].strip()
            ranges.setdefault(cur, [])
        elif ln.startswith("@@") and cur is not None:
            # @@ -a,b +c,d @@   -> new-side hunk starts at c, length d
            m = re.search(r"\+(\d+)(?:,(\d+))?", ln)
            if m:
                start = int(m.group(1))
                length = int(m.group(2)) if m.group(2) else 1
                if length > 0:
                    ranges[cur].append((start, start + length - 1))
    return {k: v for k, v in ranges.items() if v}


def run_query(db, qlfile):
    out = "/tmp/_prq_%d.bqrs" % os.getpid()
    r = sh(["codeql", "query", "run", "--database=" + db,
            "--additional-packs=" + PACKS, "--output=" + out,
            os.path.join(QLDIR, qlfile)])
    if r.returncode != 0:
        return []
    dec = sh(["codeql", "bqrs", "decode", "--format=csv", out])
    os.path.exists(out) and os.remove(out)
    rows = []
    for line in dec.stdout.splitlines()[1:]:
        # second CSV column holds our pipe-delimited payload
        m = re.search(r'","(.*)"$', line)
        if m:
            rows.append(m.group(1))
    return rows


def parse_payload(p):
    """oracle payload: func|abspath|line|...  -> (func, abspath, line)."""
    parts = p.split("|")
    if len(parts) < 3:
        return None
    return parts[0], parts[1], parts[2]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tree", required=True)
    ap.add_argument("--db", required=True)
    ap.add_argument("--base", required=True)
    ap.add_argument("--head", required=True)
    ap.add_argument("--paths", nargs="*", default=[])
    a = ap.parse_args()

    tree = os.path.abspath(a.tree)
    os.environ.setdefault("CODEQL_ALLOW_INSTALLATION_ANYWHERE", "true")

    changed = changed_ranges(tree, a.base, a.head, a.paths)
    if not changed:
        print("no changed ranges in range/paths; nothing to scan")
        return
    print("[delta] %d changed files in scope (%s..%s)" %
          (len(changed), a.base, a.head))

    # map changed ranges -> touched functions, via func_ranges.ql
    touched = set()  # (relpath, func)
    for payload in run_query(a.db, "func_ranges.ql"):
        parts = payload.split("|")
        if len(parts) != 4:
            continue
        abspath, func, s, e = parts
        rel = abspath[len(tree) + 1:] if abspath.startswith(tree + "/") else abspath
        if rel not in changed:
            continue
        s, e = int(s), int(e)
        for (cs, ce) in changed[rel]:
            if cs <= e and ce >= s:  # overlap
                touched.add((rel, func))
                break
    print("[delta] %d functions touched by the patch" % len(touched))

    # run oracles, keep hits whose (relpath, func) was touched
    print("\n%-10s %-34s %s" % ("ORACLE", "FUNCTION", "FILE:LINE"))
    print("%-10s %-34s %s" % ("------", "--------", "---------"))
    total = 0
    for ql in ORACLES:
        tag = ql.replace(".ql", "")
        for payload in run_query(a.db, ql):
            pp = parse_payload(payload)
            if not pp:
                continue
            func, abspath, line = pp
            rel = abspath[len(tree) + 1:] if abspath.startswith(tree + "/") else abspath
            if (rel, func) in touched:
                print("%-10s %-34s %s:%s" % (tag[:10], func[:34], rel, line))
                total += 1
    print("\n[delta] %d patch-touched oracle candidates" % total)
    if total == 0:
        print("        (no bug-shape introduced into the patched functions "
              "in scope -- the common, healthy result)")


if __name__ == "__main__":
    main()
