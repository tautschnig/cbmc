#!/usr/bin/env python3
"""Breadth survey of CBMC discharge outcomes across base-set candidates.

For each HIGH count/index candidate (func, translation unit) in the census,
this:
  1. goto-cc's the TU (capturing+classifying the FRONT-END outcome -- this is
     also a broad test of the C front-end across many subsystems), and
  2. runs `cbmc --function <func> --bounds-check` (classifying the DISCHARGE
     outcome).

It aggregates the failure-mode distribution -- front-end gaps, timeouts,
errors -- and the cases where CBMC works fine.  Resumable (per-TU build
status + per-func verdict cached under /tmp/disc).

  cbmc_discharge_survey.py --run [--limit N]
  cbmc_discharge_survey.py --report
"""
import argparse
import glob
import json
import os
import re
import subprocess

TREE = "/home/ubuntu/linux_6_12"
BUILD = "/home/ubuntu/cbmc-github.git/build/bin"
GOTOCC, CBMC = f"{BUILD}/goto-cc", f"{BUILD}/cbmc"
OUT = "/tmp/disc"
UNWIND = int(os.environ.get("DISC_UNWIND", "6"))
CTIMEOUT = int(os.environ.get("DISC_CBMC_TIMEOUT", "120"))
BTIMEOUT = int(os.environ.get("DISC_BUILD_TIMEOUT", "400"))


def manifest():
    m = {}
    for f in glob.glob("/tmp/dcensus/*.json"):
        for l in json.load(open(f)).get("high", []):
            p = l.split("|")
            func, path = p[0], (p[1] if len(p) > 1 else "")
            if path.endswith(".c") and "linux_6_12/" in path:
                m.setdefault(path.split("linux_6_12/")[-1], set()).add(func)
    # widen: merge extra candidates (skb/cursor + A2) from extra.json,
    # format {relpath: [funcs]}
    extra = f"{OUT}/extra.json"
    if os.path.isfile(extra):
        for rel, funcs in json.load(open(extra)).items():
            m.setdefault(rel, set()).update(funcs)
    return {k: sorted(v) for k, v in sorted(m.items())}


def sani(rel):
    return rel.replace("/", "__")[:-2]


def sh(cmd, timeout=None):
    return subprocess.run(["bash", "-c", cmd], capture_output=True, text=True)


def build_tu(rel):
    """(status, gb_path). status: OK | NOT-CONFIGURED | FE-PARSE |
    FE-CONVERSION | BUILD-FAIL | BUILD-TIMEOUT."""
    gb, st = f"{OUT}/{sani(rel)}.gb", f"{OUT}/{sani(rel)}.build"
    if os.path.isfile(st):
        s = open(st).read().strip()
        return s, (gb if s == "OK" and os.path.isfile(gb) else "")
    obj = rel[:-2] + ".o"
    sh(f"cd {TREE} && rm -f {obj}")
    cc = sh(f"cd {TREE} && ulimit -v 16000000; timeout 200 make V=1 {obj} 2>&1 "
            f'| grep -E "gcc .*{re.escape(os.path.basename(rel))}" | head -1').stdout.strip()
    if not cc:
        open(st, "w").write("NOT-CONFIGURED")
        return "NOT-CONFIGURED", ""
    cmd = re.sub(r"^gcc\b", GOTOCC, cc, count=1).replace(f"-o {obj}", f"-o {gb}")
    cmd = re.sub(r"-Werror[=a-z-]*", "", cmd)
    r = sh(f"cd {TREE} && ulimit -v 32000000; timeout {BTIMEOUT} {cmd}")
    err = (r.stdout + r.stderr).lower()
    if os.path.isfile(gb):
        s = "OK"
    elif r.returncode == 124:
        s = "BUILD-TIMEOUT"
    elif "parsing error" in err or "syntax error" in err:
        s = "FE-PARSE"
    elif "conversion error" in err:
        s = "FE-CONVERSION"
    else:
        s = "BUILD-FAIL"
    open(st, "w").write(s)
    return s, (gb if s == "OK" else "")


def discharge(gb, func):
    """PROVED | VIOLATED | TIMEOUT | OBJECT-BITS | OOM | NO-BODY |
    ERROR:<reason>.

    NB: --bounds-check only (no --pointer-check).  Under `--function` the
    input pointers are nondet, so --pointer-check flags every nondet-input
    deref (NULL/invalid) and array fields of nondet input structs become
    "dynamic objects" of unknown size -- both produce ARTIFACT failures
    unrelated to the candidate property (see cbmc-discharge-survey: even the
    locally-guarded nfc_hci_cmd_received reports a spurious bounds failure).
    A VIOLATED here is therefore a real OOB *shape under unconstrained
    input*, NOT a confirmed bug; meaningful adjudication needs a harness that
    allocates the inputs with concrete sizes (the verbatim-slice / caller-
    precondition approach)."""
    r = sh(f"ulimit -v 48000000; timeout {CTIMEOUT} {CBMC} {gb} --function {func} "
           f"--bounds-check --object-bits 16 --unwind {UNWIND} "
           f"--partial-loops --no-unwinding-assertions 2>&1")
    out = r.stdout
    if r.returncode == 124:
        return "TIMEOUT"
    if "VERIFICATION SUCCESSFUL" in out:
        return "PROVED"
    if "VERIFICATION FAILED" in out:
        return "VIOLATED"
    if "too many addressed objects" in out:
        return "OBJECT-BITS"
    if re.search(r"out of memory|bad_alloc|std::length_error", out, re.I):
        return "OOM"
    if re.search(r"no body for|not found|no function", out, re.I):
        return "NO-BODY"
    # last non-empty, non-banner line as the error signature
    tail = [l for l in out.splitlines()
            if l.strip() and not l.startswith(("****", "CBMC version"))]
    return "ERROR:" + (tail[-1][:60] if tail else "unknown")


def run(limit):
    os.makedirs(OUT, exist_ok=True)
    man = manifest()
    n = 0
    for rel, funcs in man.items():
        if limit and n >= limit:
            break
        n += 1
        status, gb = build_tu(rel)
        print(f"[{n}] {rel:<52} build={status}", flush=True)
        if status != "OK":
            for fn in funcs:
                json.dump({"tu": rel, "func": fn, "build": status,
                           "verdict": "-"},
                          open(f"{OUT}/{sani(rel)}__{fn}.v", "w"))
            continue
        for fn in funcs:
            vf = f"{OUT}/{sani(rel)}__{fn}.v"
            if os.path.isfile(vf):
                continue
            v = discharge(gb, fn)
            json.dump({"tu": rel, "func": fn, "build": "OK", "verdict": v},
                      open(vf, "w"))
            print(f"      {fn:<40} {v}", flush=True)


def report():
    rows = [json.load(open(f)) for f in glob.glob(f"{OUT}/*.v")]
    from collections import Counter
    fe = Counter(r["build"] for r in rows)
    dv = Counter(r["verdict"] for r in rows if r["build"] == "OK")
    print(f"# CBMC discharge survey ({len(rows)} candidates)\n")
    print("## Front-end (goto-cc) outcomes")
    for k, c in fe.most_common():
        print(f"  {k:<16}{c}")
    print("\n## CBMC discharge outcomes (TUs that built)")
    for k, c in dv.most_common():
        print(f"  {k:<28}{c}")
    print("\n## per-candidate")
    for r in sorted(rows, key=lambda r: (r["build"], r["verdict"])):
        print(f"  {r['tu'].split('/')[-1]:<22}{r['func']:<38}"
              f"{r['build'] if r['build']!='OK' else r['verdict']}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--run", action="store_true")
    ap.add_argument("--report", action="store_true")
    ap.add_argument("--limit", type=int, default=0)
    a = ap.parse_args()
    if a.run:
        run(a.limit)
    if a.report or not a.run:
        report()


if __name__ == "__main__":
    main()
