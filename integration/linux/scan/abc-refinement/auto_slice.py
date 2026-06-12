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
            path = p[1] if len(p) > 1 else ""
            var = next((x.split("=", 1)[1] for x in p
                        if x.startswith(("count=", "index="))), "")
            out.append({"func": func, "kind": kind, "n": n, "adv": adv,
                        "path": path, "var": var})
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



TREE = "/home/ubuntu/linux_6_12"
_DEFINE = {}


def resolve_macro(tok):
    """ALL_CAPS macro -> integer-literal value (cached), or None."""
    if tok in _DEFINE:
        return _DEFINE[tok]
    r = subprocess.run(
        ["bash", "-c",
         f"grep -rhE '^#define[[:space:]]+{tok}[[:space:]]' {TREE}/include "
         f"{TREE}/drivers {TREE}/net {TREE}/fs 2>/dev/null | head -1"],
        capture_output=True, text=True).stdout
    m = re.search(rf"#define\s+{tok}\s+(\(?\s*0[xX][0-9a-fA-F]+|\(?\s*\d+)", r)
    _DEFINE[tok] = (m.group(1).strip("( ") if m else None)
    return _DEFINE[tok]


def derive_all_rhs(path, var):
    """All assignments to `var` in the source, each abstracted to a provable
    slice RHS.  Returns (defs, complete): defs is a list of (rhs, free_vars)
    -- one per assignment -- with member/array/call sub-expressions abstracted
    to nondet placeholders (a SOUND over-approximation) and ALL-CAPS macros
    resolved to literals; complete is False if any assignment could not be
    resolved (so the caller must NOT claim safe).  SOUNDNESS: to prove the
    candidate safe, EVERY reaching definition must prove safe -- this avoids
    the trap of matching only a benign initializer (`int idx = 0;`) and
    missing the dangerous real assignment."""
    src = path if os.path.isfile(path) else TREE + "/" + path.split(
        "linux_6_12/")[-1]
    if not os.path.isfile(src):
        return None, False
    txt = open(src, errors="ignore").read()
    rhss = re.findall(rf"\b{re.escape(var)}\s*=\s*([^;=][^;]*);", txt)
    if not rhss:
        return None, False
    # SOUNDNESS: textual `var =` defs are complete only for a local scalar
    # that is neither address-taken nor cremented / compound-assigned.
    if re.search(rf"&\s*{re.escape(var)}\b", txt) or \
       re.search(rf"(\+\+|--)\s*{re.escape(var)}\b|"
                 rf"\b{re.escape(var)}\s*(\+\+|--|[-+*/%&|^]=|<<=|>>=)", txt):
        return None, False
    defs, complete = [], True
    for rhs in rhss:
        rhs = rhs.strip()
        if re.search(r"\bsizeof\b", rhs):
            complete = False
            continue
        rhs = re.sub(r"\((?:u8|u16|u32|u64|s8|s16|s32|s64|int|unsigned|long|"
                     r"char|short|__\w+)\s*\)", "", rhs)        # strip casts
        cnt = [0]

        def _ph(_m):
            cnt[0] += 1
            return f"ph{cnt[0]}"
        prev = None
        while prev != rhs:                                      # abstract
            prev = rhs
            rhs = re.sub(r"\b\w+\s*\([^()]*\)", _ph, rhs)            # calls
            rhs = re.sub(r"[A-Za-z_]\w*(?:\s*(?:->|\.)\s*\w+)+", _ph, rhs)
            rhs = re.sub(r"[A-Za-z_]\w*\s*\[[^\[\]]*\]", _ph, rhs)   # array
        ok = True
        free = []
        for t in set(re.findall(r"[A-Za-z_]\w*", rhs)):
            if t == var:
                continue
            if t.isupper() or (t.upper() == t and "_" in t):    # macro
                val = resolve_macro(t)
                if val is None:
                    ok = False
                    break
                rhs = re.sub(rf"\b{t}\b", val, rhs)
            else:
                free.append(t)
        if ok:
            defs.append((rhs, free))
        else:
            complete = False
    return defs, complete


def derive_slice_src(d, rhs, free):
    decls = "".join(f"unsigned long {v};\n" for v in free)
    body = f"unsigned long {d['var']} = {rhs};\n"
    if d["kind"] == "count-loop-write":
        body += (f"unsigned long _i; __CPROVER_assume(_i < {d['var']});\n"
                 "arr[_i] = 0;\n")
    else:
        body += f"arr[{d['var']}] = 0;\n"
    return f"unsigned char arr[{d['n']}];\n" + decls + body


def derive_run():
    cands = candidates()
    print(f"# derivation-inlining discharge of {len(cands)} count/index "
          f"candidates (prove SAFE via the REAL local expression, no "
          f"precond; ALL reaching defs must prove)\n")
    from collections import Counter
    agg = Counter()
    for c in cands:
        if "adv_bound=local" not in c["adv"]:
            agg["skip-nonlocal-storage"] += 1
            continue
        defs, complete = derive_all_rhs(c["path"], c["var"])
        if not defs:
            agg["RHS-too-complex"] += 1
            print(f"  {c['func']:<30}{c['var']:<16}N={c['n']:<5} "
                  f"RHS-too-complex -> advisory")
            continue
        verdicts = [run_cbmc(derive_slice_src(c, rhs, free), False, 2)
                    for rhs, free in defs]
        if complete and all(v == "PROVED" for v in verdicts):
            res = "PROVED-SAFE (every reaching def bounds it)"
            agg["PROVED-SAFE"] += 1
        else:
            res = (f"not locally proven ({len(defs)} defs, "
                   f"{verdicts.count('PROVED')} proved, complete={complete})"
                   " -> advisory")
            agg["not-locally-proven"] += 1
        print(f"  {c['func']:<30}{c['var']:<16}N={c['n']:<5} {res}")
    print("\n## derivation-discharge distribution")
    for k, n in agg.most_common():
        print(f"  {n:>3}  {k}")




def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--emit")
    ap.add_argument("--derive", action="store_true")
    a = ap.parse_args()
    if a.emit:
        os.makedirs(a.emit, exist_ok=True)
    if a.derive:
        derive_run()
        return
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
