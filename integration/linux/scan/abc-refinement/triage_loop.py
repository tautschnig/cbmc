#!/usr/bin/env python3
"""Closed-loop triage driver: CodeQL oracle hit -> CBMC verdict.

Runs a confidence/impact-tiered ABC oracle over a CodeQL database, then for
each candidate auto-generates a CBMC harness (via oracle_harness_gen.py),
runs CBMC on both the buggy and fixed shapes, and prints a triage table:

    function | kind | confidence | impact | CBMC(buggy) | CBMC(fixed)

This closes the stage-1 -> stage-2 loop the project was missing: instead
of a flat list of candidates, every candidate comes out with a
machine-checked reachability verdict plus the precision (confidence) and
exploitability (impact) tiers -- the triage signals the "kernel security
in the age of AI" talk identifies as the real bottleneck.

Honest scope: the harness models the bug SHAPE parameterised by the
candidate's extracted constants (array size, kind), not the verbatim
function body.  A FAILED buggy / SUCCESSFUL fixed pair confirms the shape
is genuinely OOB-reachable and that the canonical guard fixes it; it does
NOT prove the specific call site is reachable with attacker input (that is
the real-function-harness / cover-probe job, auto_harness.py's domain).

Usage:
    triage_loop.py --db /tmp/broad-next-db \\
        --query tainted_count_into_fixed_array.ql \\
        [--min-confidence HIGH] [--limit N]
"""
import argparse
import csv
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
CBMC = os.path.normpath(os.path.join(HERE, "../../../../build/bin/cbmc"))
CODEQL = "/home/ubuntu/codeql/codeql"
PACKS = "/home/ubuntu/codeql/qlpacks"

# query filename -> (harness oracle type, cbmc flags, unwind)
ORACLES = {
    "tainted_count_into_fixed_array.ql": (
        "count", ["--bounds-check", "--pointer-check"], 10),
    "decoded_len_arith_overflow.ql": (
        "decoded", ["--unsigned-overflow-check"], 4),
    "skb_field_before_lencheck.ql": (
        "skb", ["--bounds-check", "--pointer-check"], 6),
}

CONF_RANK = {"HIGH": 2, "MEDIUM": 1, "": 0}

# Verbatim real-function harnesses: candidate function name -> a harness
# that wraps the ACTUAL kernel function body (real struct layouts, real
# arithmetic, real guards) with vuln/fixed entries and a CHECKPOINT at the
# sink.  When a candidate has one, the shape+reach verdict is computed on
# the verbatim body instead of the abstract shape model.
REAL_HARNESS = {
    "cgw_csum_crc8_pos": {
        "file": "real_cgw_csum.c",
        "vuln": "probe_vuln", "fixed": "probe_fixed",
        "probe": "cgw_csum_crc8_pos",
        "flags": ["--bounds-check", "--pointer-check"], "unwind": 4,
    },
    "nfc_llcp_parse_gb_tlv": {
        "file": "real_nfc_llcp_cover.c",
        "vuln": "probe_vuln", "fixed": "probe_fixed",
        "probe_vuln": "llcp_parse_vuln", "probe_fixed": "llcp_parse_fixed",
        "flags": ["--bounds-check", "--pointer-check"], "unwind": 8,
    },
    "rxkad_decrypt_ticket": {
        "file": "real_rxkad_ticket.c",
        "vuln": "probe_vuln", "fixed": "probe_fixed",
        "probe": "rxkad_parse_ticket",
        "flags": ["--bounds-check", "--pointer-check"], "unwind": 12,
    },
    "ieee80211_get_ttlm": {
        "file": "real_ttlm.c",
        "vuln": "probe_vuln", "fixed": "probe_fixed",
        "probe_vuln": "probe_vuln", "probe_fixed": "probe_fixed",
        "flags": ["--bounds-check", "--pointer-check"], "unwind": 4,
    },
    "try_rfc959": {
        "file": "real_ftp_number.c",
        "vuln": "probe_vuln", "fixed": "probe_fixed",
        "probe": "run",
        "flags": ["--bounds-check", "--pointer-check"], "unwind": 10,
    },
}


def run_oracle(db, query):
    """Run the CodeQL query, return the list of structured candidate lines."""
    env = dict(os.environ, PATH=f"/home/ubuntu/codeql:{os.environ['PATH']}",
               CODEQL_ALLOW_INSTALLATION_ANYWHERE="true")
    bqrs = tempfile.mktemp(suffix=".bqrs")
    csvf = tempfile.mktemp(suffix=".csv")
    # Per-subsystem scoping (Scope.qll): the finders declare the external
    # predicate `scopePrefix`, so it must always be supplied.  ABC_SCOPE_PREFIX
    # (kernel-relative path prefix, e.g. "drivers/usb/") scopes the run; unset
    # / empty -> empty relation -> inScope is universally true (unscoped).
    scopef = tempfile.mktemp(suffix=".scope.csv")
    prefix = os.environ.get("ABC_SCOPE_PREFIX", "").strip()
    with open(scopef, "w") as f:
        if prefix:
            f.write('"' + prefix + '"\n')
    subprocess.run(
        [CODEQL, "query", "run", f"--database={db}",
         f"--additional-packs={PACKS}", f"--external=scopePrefix={scopef}",
         f"--output={bqrs}",
         os.path.join(HERE, query)],
        check=True, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    with open(csvf, "w") as f:
        subprocess.run([CODEQL, "bqrs", "decode", "--format=csv", bqrs],
                       check=True, env=env, stdout=f, stderr=subprocess.DEVNULL)
    lines = []
    with open(csvf, newline="") as f:
        for row in csv.reader(f):
            if len(row) >= 2 and "|" in row[1]:
                lines.append(row[1])
    for p in (bqrs, csvf, scopef):
        try:
            os.unlink(p)
        except OSError:
            pass
    return lines


def parse_candidate(line):
    """Pull func, kind, confidence, impact out of a structured line."""
    p = line.split("|")
    d = {"raw": line, "func": p[0], "file": p[1],
         "line": p[2] if len(p) > 2 else "?",
         "kind": p[3] if len(p) > 3 else "?", "confidence": "", "impact": ""}
    for field in p:
        m = re.match(r"(confidence|impact)=(\w+)", field)
        if m:
            d[m.group(1)] = m.group(2)
    return d


def cbmc_verdict(harness, func, flags, unwind):
    try:
        out = subprocess.run(
            [CBMC, harness, "-I", HERE, "--function", func, *flags,
             "--unwind", str(unwind)],
            capture_output=True, text=True, timeout=90).stdout
    except subprocess.TimeoutExpired:
        return "TIMEOUT"
    for ln in out.splitlines():
        if ln.startswith("VERIFICATION"):
            return "FAILED" if "FAILED" in ln else "SUCCESSFUL"
    return "ERROR"


# matches a --cover cover goal line, capturing the enclosing function
COVER_RE = re.compile(
    r"\.coverage\.\d+\].*function\s+(?P<func>\w+)\s+condition\s+'.*':\s+"
    r"(?P<verdict>SATISFIED|FAILED)")


def cover_verdict(harness, func, probe_func, unwind):
    """Reachability of the OOB-precondition CHECKPOINT inside `probe_func`,
    with `func` as the CBMC entry: REACHABLE if its cover goal is
    SATISFIED, BLOCKED if FAILED, NONE if the harness carries no probe.
    CBMC instruments the whole TU, so we keep only the goal whose enclosing
    function is `probe_func` (which may be a callee of `func`)."""
    cmd = [CBMC, harness, "-I", HERE, "-DCBMC_PROBE", "--function", func,
           "--cover", "cover"]
    if unwind:
        cmd += ["--unwind", str(unwind)]
    try:
        out = subprocess.run(
            cmd, capture_output=True, text=True, timeout=120).stdout
    except subprocess.TimeoutExpired:
        return "TIMEOUT"
    for m in COVER_RE.finditer(out):
        if m.group("func") == probe_func:
            return "REACHABLE" if m.group("verdict") == "SATISFIED" else "BLOCKED"
    return "NONE"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--db", required=True)
    ap.add_argument("--query", required=True, choices=list(ORACLES))
    ap.add_argument("--min-confidence", default="", choices=["", "MEDIUM", "HIGH"])
    ap.add_argument("--limit", type=int, default=0, help="0 = no limit")
    ap.add_argument("--only", default="",
                    help="comma-separated candidate function names to keep")
    a = ap.parse_args()

    otype, flags, unwind = ORACLES[a.query]
    cands = [parse_candidate(l) for l in run_oracle(a.db, a.query)]
    # one triage row per function (the harness verdict is per-function)
    seen, deduped = set(), []
    for c in cands:
        if c["func"] not in seen:
            seen.add(c["func"])
            deduped.append(c)
    cands = deduped
    if a.only:
        keep = set(a.only.split(","))
        cands = [c for c in cands if c["func"] in keep]
    if a.min_confidence:
        floor = CONF_RANK[a.min_confidence]
        cands = [c for c in cands if CONF_RANK.get(c["confidence"], 0) >= floor]
    # highest-confidence first, then write-impact first
    cands.sort(key=lambda c: (CONF_RANK.get(c["confidence"], 0),
                              c["impact"] == "WRITE"), reverse=True)
    if a.limit:
        cands = cands[:a.limit]

    print(f"# triage_loop: {a.query} on {a.db}")
    print(f"# {len(cands)} candidate(s)"
          f"{' (>= ' + a.min_confidence + ')' if a.min_confidence else ''}\n")
    hdr = ("function", "kind", "conf", "impact", "src",
           "shape:bug", "shape:fix", "reach:bug", "reach:fix")
    print(f"{hdr[0]:<26}{hdr[1]:<18}{hdr[2]:<8}{hdr[3]:<7}{hdr[4]:<7}"
          f"{hdr[5]:<11}{hdr[6]:<11}{hdr[7]:<11}{hdr[8]:<11}")
    print("-" * 106)
    for c in cands:
        real = REAL_HARNESS.get(c["func"])
        if real:
            # adjudicate on the VERBATIM function body
            hf = os.path.join(HERE, real["file"])
            rflags, runwind = real["flags"], real["unwind"]
            buggy = cbmc_verdict(hf, real["vuln"], rflags, runwind)
            fixed = cbmc_verdict(hf, real["fixed"], rflags, runwind)
            rbuggy = cover_verdict(
                hf, real["vuln"], real.get("probe_vuln", real.get("probe")),
                runwind)
            rfixed = cover_verdict(
                hf, real["fixed"], real.get("probe_fixed", real.get("probe")),
                runwind)
            src = "REAL"
        else:
            # adjudicate on the abstract shape model
            hf = tempfile.mktemp(suffix=".c")
            with open(hf, "w") as f:
                subprocess.run(
                    [sys.executable, os.path.join(HERE, "oracle_harness_gen.py"),
                     "--oracle", otype, "-c", c["raw"]],
                    check=True, stdout=f)
            buggy = cbmc_verdict(hf, "harness_buggy", flags, unwind)
            fixed = cbmc_verdict(hf, "harness_fixed", flags, unwind)
            rbuggy = cover_verdict(hf, "harness_buggy", "harness_buggy", unwind)
            rfixed = cover_verdict(hf, "harness_fixed", "harness_fixed", unwind)
            os.unlink(hf)
            src = "shape"
        print(f"{c['func']:<26}{c['kind']:<18}{c['confidence']:<8}"
              f"{c['impact']:<7}{src:<7}{buggy:<11}{fixed:<11}"
              f"{rbuggy:<11}{rfixed:<11}")

    print("\n# src:   REAL = verbatim kernel function body; "
          "shape = parameterised shape model.")
    print("# shape: CBMC bounds/overflow verdict (FAILED=bug present).")
    print("# reach: cover-probe on the OOB precondition "
          "(REACHABLE=precondition feasible; BLOCKED=guard closes the path).")


if __name__ == "__main__":
    main()
