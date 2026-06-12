#!/usr/bin/env python3
"""Auto-slicer: synthesise a minimal CBMC slice from a count/index finder hit.

The discharge ladder (cbmc-discharge-survey) showed the verbatim SLICE is the
only approach that scales to definitive verdicts.  This auto-generates that
slice from the finder's structured output -- no source parsing needed beyond
the array size, the count/index name, and the kind:

  count-loop-write:  unsigned char arr[N]; for (i=0;i<count;i++) arr[i]=0;
  direct-index:      unsigned char arr[N]; arr[idx]=0;  (or read)

with the count/index left fully NONDET (a sound over-approximation: the real
count can only be MORE constrained).  Each slice is checked twice:
  * verbatim (count unbounded)         -> expect VIOLATED  (the OOB shape)
  * with PRECOND (count<=N / idx<N)    -> expect PROVED     (bound is enough)

A (VIOLATED, PROVED) pair is the definitive characterization: "OOB reachable
iff the count/index is unbounded; SAFE iff bounded by the array size" --
reducing triage to the targeted question "does a (local or non-local)
validator bound it by N?".  Robustly-safe shapes (e.g. count type already
< N) show (PROVED, PROVED).

  auto_slice.py            # over the census HIGH count/index candidates
  auto_slice.py --emit DIR # also write the .c slices for inspection
"""
import argparse
import glob
import json
import os
import re
import subprocess

CBMC = "/home/ubuntu/cbmc-github.git/build/bin/cbmc"


def candidates():
    seen, out = set(), []
    for f in glob.glob("/tmp/dcensus/*.json"):
        for l in json.load(open(f)).get("high", []):
            p = l.split("|")
            func = p[0]
            kind = next((x for x in p if x in
                         ("count-loop-write", "direct-index")), "")
            m = re.search(r"arr=[^|]*\[(\d+)\]", l)
            if not (kind and m):
                continue
            n = int(m.group(1))
            key = (func, kind, n)
            if key in seen or n <= 0 or n > 4096:
                continue
            seen.add(key)
            adv = "|".join(x for x in p if x.startswith("adv_"))
            out.append({"func": func, "kind": kind, "n": n, "adv": adv})
    return out


def slice_src(kind, n):
    if kind == "count-loop-write":
        # sound loop abstraction: the body writes arr[i] for an ARBITRARY
        # iteration i < count; checking one havoc'd in-range i covers the
        # whole loop and is N-independent (no unwinding).
        return (f"unsigned char arr[{n}];\n"
                "unsigned long count, i;\n"
                "#ifdef PRECOND\n"
                f"__CPROVER_assume(count <= {n});\n"
                "#endif\n"
                "__CPROVER_assume(i < count);\n"
                "arr[i] = 0;\n")
    return (f"unsigned char arr[{n}];\n"
            "unsigned long idx;\n"
            "#ifdef PRECOND\n"
            f"__CPROVER_assume(idx < {n});\n"
            "#endif\n"
            "arr[idx] = 0;\n")


def run_cbmc(src, precond, unwind):
    f = f"/tmp/_slice_{os.getpid()}.c"
    open(f, "w").write("int main(void){\n" + src + "return 0;\n}\n")
    flags = "-DPRECOND" if precond else ""
    ua = "--unwinding-assertions" if precond else "--partial-loops"
    r = subprocess.run(
        ["bash", "-c",
         f"ulimit -v 8000000; timeout 60 {CBMC} {f} {flags} --bounds-check "
         f"--unwind {unwind} {ua} 2>&1"],
        capture_output=True, text=True)
    os.unlink(f)
    if r.returncode == 124:
        return "TIMEOUT"
    if "VERIFICATION SUCCESSFUL" in r.stdout:
        return "PROVED"
    if "VERIFICATION FAILED" in r.stdout:
        return "VIOLATED"
    return "ERROR"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--emit")
    a = ap.parse_args()
    if a.emit:
        os.makedirs(a.emit, exist_ok=True)
    cands = candidates()
    print(f"# auto-slice discharge of {len(cands)} count/index candidates\n")
    print(f"{'func':<34}{'kind':<18}{'N':<6}{'verbatim':<10}{'precond':<9}"
          f"{'-> conclusion'}")
    print("-" * 96)
    from collections import Counter
    agg = Counter()
    for c in cands:
        unwind = 2  # slices are loop-free
        src = slice_src(c["kind"], c["n"])
        if a.emit:
            open(f"{a.emit}/{c['func']}_{c['kind']}.c", "w").write(
                "int main(void){\n" + src + "return 0;\n}\n")
        vb = run_cbmc(src, False, unwind)
        pc = run_cbmc(src, True, unwind)
        if (vb, pc) == ("VIOLATED", "PROVED"):
            concl = "OOB-shape; SAFE iff count<=N (needs validator)"
        elif (vb, pc) == ("PROVED", "PROVED"):
            concl = "robustly safe"
        else:
            concl = f"{vb}/{pc}"
        agg[concl] += 1
        print(f"{c['func']:<34}{c['kind']:<18}{c['n']:<6}{vb:<10}{pc:<9}{concl}\n        {c['adv']}")
    print("\n## conclusion distribution")
    for k, v in agg.most_common():
        print(f"  {v:>3}  {k}")


if __name__ == "__main__":
    main()
