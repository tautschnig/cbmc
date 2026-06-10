#!/usr/bin/env python3
"""Multi-asset threat-model evaluation.

Runs every asset's threat-template finder over a CodeQL DB and tabulates,
per asset, the raw candidate count and (where the finder emits a mitigation
verdict) the GENUINE (unmitigated) subset -- the top-down complement of the
bottom-up pipeline_eval.  With --module it scopes to one file/path prefix,
producing the full cross-asset threat model of a single (fresh) module.

Usage:
  threat_model_eval.py --db /tmp/broad-next-db [--module net/l2tp/]
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import triage_loop as tl

# (asset label, finder query, mitigation field, "mitigated" token)
ASSETS = [
    ("A1-mem count/index", "tainted_count_into_fixed_array.ql", None, None),
    ("A1-mem decoded-len", "decoded_len_arith_overflow.ql", None, None),
    ("A1-mem skb/cursor", "skb_field_before_lencheck.ql", None, None),
    ("A1-UB div/shift", "tainted_ub_arith.ql", "mitigation", "MITIGATED"),
    ("A2 confidentiality", "infoleak_uninit_to_user.ql", "mitigation", "MITIGATED"),
    ("A3 integrity/CFI", "a3_tainted_fnptr.ql", None, None),
    ("A4 availability", "av_unbounded.ql", "mitigation", "MITIGATED"),
    ("A5 authorization", "auth_missing_capable.ql", "verdict", "CALLER-GUARDED"),
]


def field(line, key):
    for f in line.split("|"):
        if f.startswith(key + "="):
            return f.split("=", 1)[1]
    return ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--db", required=True)
    ap.add_argument("--module", default="", help="path prefix filter (fresh module)")
    a = ap.parse_args()

    scope = (" | module=" + a.module) if a.module else ""
    print(f"# threat_model_eval on {a.db}{scope}\n")
    print(f"{'asset':<22}{'raw':<6}{'mitigated':<11}{'genuine':<9}")
    print("-" * 48)
    detail = {}
    for label, q, mfield, mtok in ASSETS:
        lines = tl.run_oracle(a.db, q)
        if a.module:
            lines = [l for l in lines if a.module in l]
        raw = len(lines)
        if mfield:
            mit = sum(1 for l in lines if field(l, mfield) == mtok)
            gen = raw - mit
            print(f"{label:<22}{raw:<6}{mit:<11}{gen:<9}")
        else:
            print(f"{label:<22}{raw:<6}{'n/a':<11}{'n/a':<9}")
        detail[label] = lines

    if a.module:
        print(f"\n## Cross-asset threat model of {a.module} (genuine/raw candidates)\n")
        for label, q, mfield, mtok in ASSETS:
            lines = detail[label]
            shown = [l for l in lines
                     if not mfield or field(l, mfield) != mtok]
            if not shown:
                continue
            print(f"### {label}")
            seen = set()
            for l in shown:
                fn = l.split("|")[0]
                if fn in seen:
                    continue
                seen.add(fn)
                tag = ("/" + field(l, mfield)) if mfield else ""
                print(f"  {fn}{tag}")
            print()


if __name__ == "__main__":
    main()
