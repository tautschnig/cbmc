#!/usr/bin/env python3
"""Automated function-pointer restriction for the CBMC discharge layer.

CBMC resolves an indirect call to ALL type-compatible address-taken
functions.  In the kernel that set routinely contains SPURIOUS matches: an
`dev->netdev_ops->ndo_setup_tc(...)` call in sch_mqprio.c is "resolved" to
`mqprio_init` / `mqprio_dump_class_stats` merely because their signatures
match -- and inlining those large, unrelated functions is what blows up
symex.  The real target lives in another TU (a driver's netdev_ops), i.e.
it is EXTERNAL to the goto binary.

This tool identifies, per indirect call site, three things automatically:
  * the type-compatible candidates (from goto-instrument --remove-function-
    pointers verbosity);
  * the pointer's FIELD name (from --show-goto-functions);
  * the ASSIGNMENT-based (points-to) candidates -- functions whose address
    is actually stored in a field of that name (from --show-symbol-table,
    e.g. `init=mqprio_init`).

The assignment-based set is a TIGHTER, sound over-approximation than the
type-based set.  Where it is a strict, non-empty subset, the tool narrows
the call via `goto-instrument --restrict-function-pointer-by-name
FIELD/<targets>` (dropping the spurious matches).  Where it is EMPTY the
real target is external; such calls are reported (the principled treatment
is to stub them as havoc -- a documented follow-up, since the restriction
CLI needs >=1 in-binary target).

  fp_restrict.py <gb> --report
  fp_restrict.py <gb> --apply <out.gb>
"""
import argparse
import os
import re
import subprocess

BUILD = "/home/ubuntu/cbmc-github.git/build/bin"
GI = f"{BUILD}/goto-instrument"


def _run(args):
    return subprocess.run(args, capture_output=True, text=True)


def type_candidates(gb):
    """[{func,line,cands}] -- type-compatible targets per indirect call."""
    tmp = f"/tmp/_fpr_{os.getpid()}.gb"
    out = _run([GI, "--verbosity", "10", "--remove-function-pointers",
                gb, tmp]).stdout
    try:
        os.unlink(tmp)
    except OSError:
        pass
    sites, lines = [], out.splitlines()
    for i, l in enumerate(lines):
        m = re.search(r"line (\d+) function (\S+): replacing function pointer "
                      r"by \d+ possible targets", l)
        if m and i + 1 < len(lines) and lines[i + 1].strip().startswith("targets:"):
            cands = [x.strip() for x in
                     lines[i + 1].strip()[len("targets:"):].split(",")
                     if x.strip()]
            sites.append({"line": m.group(1), "func": m.group(2),
                          "cands": cands})
    return sites


def function_names(gb):
    out = _run([GI, "--list-goto-functions", gb]).stdout
    return set(re.findall(r"^(\w[\w$]*)", out, re.M))


def field_to_funcs(gb, funcs):
    """field name -> set(funcs) whose address is stored in a `.field`."""
    out = _run([GI, "--show-symbol-table", gb]).stdout
    m = {}
    for fld, fn in re.findall(r"(\w+)\s*=\s*(?:address_of\()?([A-Za-z_]\w*)", out):
        if fn in funcs:
            m.setdefault(fld, set()).add(fn)
    return m


def call_fields(gb):
    """function name -> set(field names) used in its indirect calls."""
    out = _run([GI, "--show-goto-functions", gb]).stdout
    res, cur = {}, None
    for l in out.splitlines():
        h = re.match(r"^(\w[\w$]*) /\*", l)
        if h:
            cur = h.group(1)
        if cur and "CALL" in l and "(*" in l:
            for fld in re.findall(r"\.(\w+)\)\)?\(", l):
                res.setdefault(cur, set()).add(fld)
    return res


def analyse(gb):
    sites = type_candidates(gb)
    funcs = function_names(gb)
    f2f = field_to_funcs(gb, funcs)
    cf = call_fields(gb)
    # per-function 1-based index of each indirect call (labelling order ==
    # source order); used to name the labelled call site
    by_func = {}
    for s in sorted(sites, key=lambda s: (s["func"], int(s["line"]))):
        by_func.setdefault(s["func"], []).append(s)
    for fn, lst in by_func.items():
        for i, s in enumerate(lst, 1):
            s["idx"] = i
    for s in sites:
        flds = cf.get(s["func"], set())
        field, actual = None, []
        for fld in flds:
            inter = [c for c in s["cands"] if c in f2f.get(fld, set())]
            if inter:
                field, actual = fld, inter
                break
        if field is None and len(flds) == 1:
            field = next(iter(flds))
        s["field"] = field or "?"
        s["actual"] = actual
        if actual and len(actual) < len(s["cands"]):
            s["status"] = "NARROW"
        elif actual:
            s["status"] = "unchanged"
        else:
            s["status"] = "EXTERNAL"  # real target out-of-TU -> stub (havoc)
    return sites


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("gb")
    ap.add_argument("--report", action="store_true")
    ap.add_argument("--apply", metavar="OUT")
    a = ap.parse_args()
    sites = analyse(a.gb)

    print(f"# function-pointer analysis of {a.gb} ({len(sites)} indirect calls)\n")
    print(f"{'function':<26}{'line':<6}{'field':<18}{'status':<10}"
          f"{'type->actual'}")
    print("-" * 78)
    restr = []
    for s in sites:
        print(f"{s['func']:<26}{s['line']:<6}{s['field']:<18}{s['status']:<10}"
              f"{len(s['cands'])}->{len(s['actual'])} "
              f"{','.join(s['actual']) if s['status']=='NARROW' else ''}")
        if s["status"] == "NARROW":
            restr.append(f"{s['func']}.function_pointer_call.{s['idx']}/"
                         f"{','.join(sorted(set(s['actual'])))}")
    ext = sum(1 for s in sites if s["status"] == "EXTERNAL")
    print(f"\n  NARROW: {len(restr)}  EXTERNAL(needs stub): {ext}  "
          f"unchanged: {len(sites)-len(restr)-ext}")

    if a.apply:
        args = [GI]
        for r in sorted(set(restr)):
            args += ["--restrict-function-pointer", r]
        args += [a.gb, a.apply]
        r = _run(args)
        print(f"\napplied {len(set(restr))} narrowing restriction(s) -> "
              f"{a.apply}" + ("" if os.path.isfile(a.apply)
                              else f"\nFAILED: {r.stderr[:300]}"))


if __name__ == "__main__":
    main()
