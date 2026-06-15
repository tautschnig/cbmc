#!/usr/bin/env python3
"""Differential validation of the CBMC COBOL frontend against GnuCOBOL.

For each program in ``programs/*.cob`` (a deterministic program that computes
a result into a field and prints it as ``[<field>]`` on one line):

  1. compile and run it with GnuCOBOL (cobc) to obtain the *oracle* -- the
     exact bytes GnuCOBOL produces for the result field;
  2. build a CBMC variant in which the ``DISPLAY "[" <field> "]"`` line is
     replaced by ``CALL "__CPROVER_assert" USING <field> = '<oracle>'`` and
     run it through CBMC;
  3. CBMC reporting VERIFICATION SUCCESSFUL means the frontend agrees with
     GnuCOBOL for that program; a FAILURE is a real divergence (a frontend
     soundness or precision bug).

The two tools are independent implementations of the IBM/ANSI COBOL
semantics, so agreement is strong evidence the frontend's modelling is
faithful. Run with the path to cbmc, e.g.:

    ./diff_check.py --cbmc ../../build/bin/cbmc
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PROGRAMS = os.path.join(HERE, "programs")
MARKER = re.compile(r'^(\s*)DISPLAY\s+"\["\s+(\S+)\s+"\]"\s*\.\s*$')

# Memory (KiB) and wall-clock (s) caps for each cbmc invocation.
CBMC_VM_KIB = 8388608
CBMC_TIMEOUT = 120


def run(cmd, **kw):
    return subprocess.run(
        cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kw)


def gnucobol_oracle(src):
    """Compile+run src with cobc; return (field, oracle) or raise."""
    with tempfile.TemporaryDirectory() as d:
        exe = os.path.join(d, "prog")
        c = run(["cobc", "-x", "-o", exe, src])
        if c.returncode != 0:
            raise RuntimeError("cobc failed: " + c.stderr.decode(errors="replace"))
        r = run([exe], timeout=30)
        out = r.stdout.decode(errors="replace")
        m = re.search(r"\[(.*)\]", out)
        if not m:
            raise RuntimeError("no [..] marker in GnuCOBOL output: " + repr(out))
        return m.group(1)


def cbmc_variant(src, oracle):
    """Return the CBMC source with the DISPLAY marker replaced by an assert."""
    lines = open(src).read().splitlines()
    field = None
    for i, ln in enumerate(lines):
        m = MARKER.match(ln)
        if m:
            indent, field = m.group(1), m.group(2)
            lit = "'" + oracle.replace("'", "''") + "'"
            lines[i] = (
                f'{indent}CALL "__CPROVER_assert" USING {field} = {lit}.')
            break
    if field is None:
        raise RuntimeError("no DISPLAY \"[\" field \"]\" marker line")
    return "\n".join(lines) + "\n"


def cbmc_check(cbmc, source_text):
    with tempfile.NamedTemporaryFile(
        "w", suffix=".cob", delete=False) as f:
        f.write(source_text)
        path = f.name
    try:
        wrapped = (
            f"ulimit -v {CBMC_VM_KIB}; "
            f"timeout {CBMC_TIMEOUT} {cbmc} {path} 2>&1")
        r = run(["bash", "-c", wrapped])
        out = r.stdout.decode(errors="replace")
        if "VERIFICATION SUCCESSFUL" in out:
            return "AGREE", out
        if "VERIFICATION FAILED" in out:
            return "DIVERGE", out
        return "ERROR", out
    finally:
        os.unlink(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--cbmc",
        default=os.path.join(HERE, "..", "..", "build", "bin", "cbmc"))
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    srcs = sorted(
        os.path.join(PROGRAMS, f)
        for f in os.listdir(PROGRAMS)
        if f.endswith(".cob"))
    agree = diverge = error = 0
    for src in srcs:
        name = os.path.basename(src)
        try:
            oracle = gnucobol_oracle(src)
            variant = cbmc_variant(src, oracle)
            status, out = cbmc_check(args.cbmc, variant)
        except Exception as e:  # noqa: BLE001
            status, out, oracle = "ERROR", str(e), "?"
        tag = {"AGREE": "ok", "DIVERGE": "DIVERGE", "ERROR": "error"}[status]
        print(f"  {name:<22} oracle=[{oracle}]  {tag}")
        if status == "AGREE":
            agree += 1
        elif status == "DIVERGE":
            diverge += 1
            print("    --- CBMC vs GnuCOBOL DIVERGENCE ---")
            for l in out.splitlines():
                if "assertion" in l or "VERIFICATION" in l:
                    print("    " + l)
        else:
            error += 1
            if args.verbose:
                print("    " + out.replace("\n", "\n    "))
    print(f"\n{agree} agree, {diverge} diverge, {error} error "
          f"(of {len(srcs)})")
    return 1 if (diverge or error) else 0


if __name__ == "__main__":
    sys.exit(main())
