#!/usr/bin/env python3
"""Check JBMC against the results of the JDKs where they differ.

For the cases where Probe's results differ between Java versions (see
compare.py --json), this writes a harness with one method per case: it
makes the call with the same arguments and asserts, for each result of a
version, that the result is not that one. JBMC must not prove such an
assertion (SUCCESS) for the result of any version that may run the
program: that would be a false proof. With --java-version N, it should
refute only the results of the other versions.

Verdicts per case and result: "excluded" (JBMC proved the assertion),
"possible" (it found the result possible), "unreachable" (JBMC proved
that the end of the case is unreachable: it excludes every behaviour),
"unknown" (unwinding assertion failed, timeout, error).

Usage:
    check_jbmc.py DIFF_JSON PROBE_OUT_TXT OUT_DIR --api API [--per-partition N]
        [--jbmc PATH] [--java-version N] [--methods REGEX] [-j N]
"""

import argparse
import collections
import concurrent.futures
import json
import os
import re
import subprocess
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
MODELS = REPO / "jbmc/lib/java-models-library/target"
# a javac that compiles for every release from 8 (JAVAC in the environment)
JAVAC = os.environ.get("JAVAC", "javac")


def unesc(s):
    """The string of Probe's esc() rendering (without its quotes)."""
    return re.sub(r"\\u([0-9a-f]{4})", lambda m: chr(int(m.group(1), 16)), s)


def jstr(s):
    out = ['"']
    for c in s:
        o = ord(c)
        if c == "\n":
            out.append("\\n")
        elif c == "\r":
            out.append("\\r")
        elif c == '"':
            out.append('\\"')
        elif c == "\\":
            out.append("\\\\")
        elif 0x20 <= o < 0x7F:
            out.append(c)
        else:
            out.append("\\u%04x" % o)
    out.append('"')
    return "".join(out)


STR_RE = r'"((?:[^"\\]|\\u[0-9a-f]{4})*)"'


def split_suffixes(r):
    """The value and the ' recv=' part of a result."""
    recv = None
    m = re.search(r" recv=sb" + STR_RE + r"$", r)
    if m:
        recv = unesc(m.group(1))
        r = r[: m.start()]
    r = re.sub(r" arg=.*$", "", r)
    return r, recv


def condition(result, ret, returns_throwable=False):
    """A Java condition that holds when the call gave `result` (Probe's
    rendering), or None where it cannot be expressed. With
    `returns_throwable`, a Throwable result is the value of the call (of a
    constructor of an exception) when it is of that class."""
    r = result.split(" => ", 1)[1] if " => " in result else result
    m = re.match(r"T([\w.$]+):(null|" + STR_RE + r")(?: cause=.*)?$", r)
    if m and returns_throwable and m.group(1) == ret:
        msg = ("r.getMessage() == null" if m.group(2) == "null"
               else f"r.getMessage() != null && "
               f"r.getMessage().equals({jstr(unesc(m.group(3)))})")
        return f"!threw && {msg}"
    if m:
        msg = ("msg == null" if m.group(2) == "null"
               else f"msg != null && msg.equals({jstr(unesc(m.group(3)))})")
        return f'threw && cls.equals("{m.group(1)}") && {msg}'
    if r.startswith("FAIL") or r == "OOM":
        return None
    r, recv = split_suffixes(r)
    conds = ["!threw"]
    if recv is not None:
        conds.append(f"recv.toString().equals({jstr(recv)})")
    if r == "void":
        pass
    elif r == "null":
        conds.append("r == null")
    elif ret in ("int", "long", "short", "byte"):
        if not re.fullmatch(r"-?\d+", r):
            return None
        conds.append(f"r == {r}{'L' if ret == 'long' else ''}")
    elif ret == "boolean":
        conds.append(f"r == {r}")
    elif ret == "char":
        conds.append(f"r == (char) 0x{r[1:]}")
    elif ret == "double":
        conds.append(f"Double.doubleToRawLongBits(r) == 0x{r[1:]}L")
    elif ret == "float":
        conds.append(f"Float.floatToRawIntBits(r) == 0x{r[1:]}")
    elif re.fullmatch(STR_RE, r):
        conds.append(f"r != null && r.toString().equals({jstr(unesc(r[1:-1]))})")
    elif r.startswith("sb") and re.fullmatch(STR_RE, r[2:]):
        conds.append(f"r != null && r.toString().equals({jstr(unesc(r[3:-1]))})")
    elif ret == "java.lang.String[]" and r.startswith("["):
        items = re.findall(STR_RE + ",", r[1:-1])
        if "".join(f'"{i}",' for i in items) != r[1:-1]:
            return None
        conds.append(f"r != null && r.length == {len(items)}")
        conds += [f"r[{i}].equals({jstr(unesc(s))})" for i, s in enumerate(items)]
    else:
        return None
    return " && ".join(conds)


def parse_key(key):
    m = re.match(r"([\w.$]+)\.([\w<>$]+)\((.*)\)$", key)
    return m.group(1), m.group(2), [p for p in m.group(3).split(",") if p]


def harness_method(name, key, kind, ret, jexprs, results):
    cls, meth, params = parse_key(key)
    if ret == "java.lang.AbstractStringBuilder":
        ret = cls
    if meth == "compareTo" and params == ["java.lang.Object"]:
        # (the bridge method of Comparable, not callable from source)
        return None
    # (Probe writes the receiver, if any, then \x01 and each argument)
    parts = jexprs.split("\x01")
    recv_expr = parts[0] if kind == "instance" else None
    args = parts[1:]
    if len(args) != len(params) or any("?" == a for a in args + [recv_expr or ""]):
        return None
    cargs = ", ".join(f"({p}) {a}" for p, a in zip(params, args))
    lines = [f"  public static void {name}() {{"]
    lines.append("    boolean threw = false; String cls = null; String msg = null;")
    if kind == "instance":
        lines.append(f"    {cls} recv = ({cls}) {recv_expr};")
        call = f"recv.{meth}({cargs})"
    elif kind == "static":
        lines.append("    Object recv = null;")
        call = f"{cls}.{meth}({cargs})"
    else:
        lines.append("    Object recv = null;")
        call = f"new {cls}({cargs})"
        ret = cls
    if ret == "void":
        lines.append("    try { " + call + "; }")
    else:
        default = {"int": "0", "long": "0", "short": "0", "byte": "0",
                   "char": "0", "boolean": "false", "double": "0",
                   "float": "0"}.get(ret, "null")
        lines.append(f"    {ret} r = {default};")
        lines.append("    try { r = " + call + "; }")
    lines.append("    catch (Throwable e) { threw = true; "
                 "cls = org.cprover.CProver.classIdentifier(e); "
                 "msg = e.getMessage(); }")
    checks = []
    throwable = kind == "ctor" and re.search(r"(Exception|Error)$", cls)
    for res, versions in results.items():
        c = condition(res, ret, bool(throwable))
        if c is None:
            return None
        checks.append((versions, c))
    return lines, checks


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("diff_json")
    ap.add_argument("probe_out")
    ap.add_argument("out_dir")
    ap.add_argument("--api", required=True, help="the API list (api.py)")
    ap.add_argument("--per-partition", type=int, default=2)
    ap.add_argument("--jbmc", default=str(REPO / "build/bin/jbmc"))
    ap.add_argument("--java-version", type=int)
    ap.add_argument("--methods", default=".")
    ap.add_argument("--flags", default="--throw-runtime-exceptions")
    ap.add_argument("--timeout", type=int, default=120)
    ap.add_argument("-j", type=int, default=6)
    ap.add_argument("--agreeing", action="store_true",
                    help="check the cases where all versions agree "
                    "(compare.py --agreeing) instead of those that differ")
    args = ap.parse_args()
    out = Path(args.out_dir)
    out.mkdir(parents=True, exist_ok=True)

    api = {}
    for line in open(args.api):
        f = line.rstrip("\n").split("|")
        api[f"{f[0]}.{f[2]}({f[3]})"] = (f[1], f[4])
    jexprs = {}
    for line in open(args.probe_out, encoding="utf-8", errors="replace"):
        line = line.rstrip("\n")
        if "\tJ" not in line:
            continue
        head, j = line.split("\tJ", 1)
        key, k, _ = head.split("|", 2)
        jexprs[(key, int(k))] = j

    diffs = json.load(open(args.diff_json))
    methods, skipped = [], collections.Counter()
    for m in diffs:
        if bool(m.get("agreeing")) != args.agreeing:
            continue
        key = m["method"]
        if not re.search(args.methods, key) or key not in api:
            continue
        kind, ret = api[key]
        seen = collections.Counter()
        for c in m["cases"]:
            part = " | ".join(sorted(c["results"].values()))
            if seen[part] >= args.per_partition:
                continue
            j = jexprs.get((key, c["case"]))
            h = j is not None and harness_method(
                f"m{len(methods)}", key, kind, ret, j, c["results"])
            if not h:
                skipped[key] += 1
                continue
            seen[part] += 1
            methods.append((key, c["case"], h))

    # one class per case, so that one compile error does not lose the rest
    jobs = []
    checks_results = [[c for _, c in h[1]] for _, _, h in methods]
    for i, (key, k, (lines, checks)) in enumerate(methods):
        d = out / f"c{i}"
        d.mkdir(exist_ok=True)
        src = ["public class H {"] + lines
        # (reachability after the call: if JBMC proves this, it excludes
        # every behaviour, and the verdicts below are vacuous; JBMC's
        # assertions stop the path where they fail, hence the branch)
        end_line = len(src) + 1
        src.append("    if (org.cprover.CProver.nondetBoolean()) assert false;")
        # (what JBMC allows, for the triage: an exception, a return, the
        # class of the exception of the JDKs, a null message)
        classes = sorted({m.group(1) for r in checks_results[i]
                          for m in [re.search(r'cls\.equals\("([\w.$]+)"\)', r)]
                          if m})
        triage = {"throws": "threw", "returns": "!threw"}
        for c in classes:
            triage["throws " + c] = f'threw && cls.equals("{c}")'
        triage["null message"] = "threw && msg == null"
        triage_lines = {}
        for name, cond in triage.items():
            triage_lines[len(src) + 1] = name
            src.append(f"    if (org.cprover.CProver.nondetBoolean()) "
                       f"assert !({cond});")
        lines_of = {}
        for versions, c in checks:
            lines_of[len(src) + 1] = versions
            src.append(f"    assert !({c});")
        src += ["  }", "}"]
        (d / "H.java").write_text("\n".join(src) + "\n")
        jobs.append((i, key, k, d, lines_of, end_line, triage_lines))

    def run(job):
        i, key, k, d, lines_of, end_line, triage_lines = job
        # the oldest release that has the method (JBMC then assumes that
        # versions before it do not run the program)
        for release in (8, 9, 11, 17, 21, 25):
            cp = subprocess.run(
                ["bash", "-c", f"cd {d} && {JAVAC} --release {release} -nowarn -cp "
                 + str(MODELS / "cprover-api.jar") + " H.java"],
                capture_output=True, text=True)
            if cp.returncode == 0:
                break
        if cp.returncode != 0:
            return i, key, k, {v: "compile-error" for v in lines_of.values()}, cp.stderr[-500:]
        too_old = {v for v in lines_of.values()
                   if max(int(x) for x in re.findall(r"\d+", v)) < release}
        vflag = f"--java-version {args.java_version}" if args.java_version else ""
        cmd = (f"cd {d} && ulimit -v 6000000 && timeout {args.timeout} "
               f"{args.jbmc} --function H.m{i} {args.flags} {vflag} "
               f"--unwind 40 --unwinding-assertions --cp .:{MODELS}/core-models.jar:"
               f"{MODELS}/cprover-api.jar H")
        cp = subprocess.run(["bash", "-c", cmd], capture_output=True, text=True)
        (d / "jbmc.out").write_text(cp.stdout + cp.stderr)
        unwind_fail = re.search(r"unwinding assertion.*: FAILURE", cp.stdout)
        verdicts = {}
        for line, name in triage_lines.items():
            m = re.search(rf"\] line {line} assertion[^\n]*: (SUCCESS|FAILURE)",
                          cp.stdout)
            verdicts["jbmc " + name] = (
                "unknown" if not m else "yes" if m.group(1) == "FAILURE" else "no")
        end = re.search(rf"\] line {end_line} assertion[^\n]*: (SUCCESS|FAILURE)",
                        cp.stdout)
        if end and end.group(1) == "SUCCESS" and not unwind_fail:
            verdicts.update({v: "unreachable" for v in lines_of.values()})
            return i, key, k, verdicts, ""
        for line, versions in lines_of.items():
            if versions in too_old:
                verdicts[versions] = "n/a"
                continue
            m = re.search(rf"\] line {line} assertion[^\n]*: (SUCCESS|FAILURE)",
                          cp.stdout)
            if not m or "VERIFICATION ERROR" in cp.stdout:
                verdicts[versions] = "unknown"
            elif m.group(1) == "FAILURE":
                verdicts[versions] = "possible"
            else:
                verdicts[versions] = "unknown" if unwind_fail else "excluded"
        return i, key, k, verdicts, ""

    results = []
    with concurrent.futures.ThreadPoolExecutor(args.j) as ex:
        for r in ex.map(run, jobs):
            results.append(r)
    with open(out / "results.json", "w") as f:
        json.dump([{"case": i, "method": key, "probe_case": k,
                    "verdicts": v, "error": e} for i, key, k, v, e in results],
                  f, indent=1)
    print(f"{len(jobs)} cases, skipped {sum(skipped.values())}")


if __name__ == "__main__":
    main()
