#!/usr/bin/env python3
"""End-to-end measured evaluation of the CodeQL -> CBMC triage pipeline.

Runs every shape oracle over a CodeQL database and reports the candidate
FUNNEL -- how many raw oracle hits survive each successive triage filter --
then CBMC-adjudicates (shape + reach) the distilled survivors and the
known-CVE ground-truth functions.  Answers the question the "kernel
security in the age of AI" talk poses for defenders: given a flood of
candidate hits, how few machine-checked, ranked items does the pipeline
hand you, and do the real bugs survive?

Funnel stages
  raw hits          every oracle result row
  raw functions     distinct enclosing functions
  HIGH confidence    raw-taint-reachable (count/index oracle only)
  WRITE impact       OOB-write primitive (count/index oracle only)
  CBMC shape         bounds/overflow property FAILED on the (shape or REAL) harness
  CBMC reach         OOB precondition REACHABLE (cover-probe)

Usage:  pipeline_eval.py --db /tmp/broad-next-db
"""
import argparse
import collections
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import triage_loop as tl  # reuse run_oracle/parse_candidate/cbmc/cover/REAL_HARNESS

# Honest ground truth for the net/ surface in broad-next-db (linux-next,
# a POST-fix tree -- so "the shape is present; the guard status is what
# stage-2 / the verbatim+caller analysis determines").
GROUND_TRUTH = {
    "cgw_csum_crc8_pos": ("CVE-2019-3701",
        "CAN-gw OOB write; from/to/result_idx nla_memcpy'd raw; "
        "caller cgw_parse_attr is the validation site"),
    "cgw_csum_crc8_neg": ("CVE-2019-3701", "as crc8_pos (negative scan)"),
    "cgw_csum_xor_pos": ("CVE-2019-3701", "as crc8_pos (xor profile)"),
    "cgw_csum_xor_neg": ("CVE-2019-3701", "as crc8_pos (xor, negative)"),
    "rxkad_decrypt_ticket": ("rxkad ticket parse",
        "bounded-cursor flags read; OOB closed by caller guard "
        "ticket_len>=4 (rxkad.c:1167) -- function-granularity FP"),
    "crush_decode": ("ceph osdmap",
        "count->multiply; array_size/size_mul guarded, u32 cannot overflow "
        "size_t on 64-bit -- shape-model territory, BMC-intractable verbatim"),
    "ieee80211_get_ttlm": ("mac80211 T2L map",
        "bounded-cursor: read width chosen by bm_size, no length arg; "
        "UNGUARDED in caller-precondition -- genuine concern (verbatim TP)"),
    "try_rfc959": ("nf_conntrack_ftp",
        "FTP PORT parser (try_number); loop bounded by dlen, index by "
        "array_size -- CBMC-clear TRUE NEGATIVE (verbatim)"),
}


def funnel(db, query, otype, cg):
    rows = tl.run_oracle(db, query)
    cands = [tl.parse_candidate(l) for l in rows]
    funcs = {}
    for c in cands:
        funcs.setdefault(c["func"], c)  # first hit per function
    high = [c for c in funcs.values() if c["confidence"] == "HIGH"]
    high_write = [c for c in high if c["impact"] == "WRITE"]
    # precondition verdict split (the precision lever for the decoded/skb
    # oracles, whose raw-taint is baked in by construction)
    resolved = [f for f in funcs if cg.get(f, ("", ""))[1] in GUARDED]
    # genuine UNGUARDED -- but skb-pull UNGUARDED is a WEAK signal (the
    # pskb_may_pull may live higher in the rx stack than the immediate
    # caller), so it does not count as a high-confidence genuine concern.
    unguarded = [f for f in funcs
                 if cg.get(f, ("", ""))[1] == "UNGUARDED"
                 and cg.get(f, ("", ""))[0] != "skb-pull"]
    skb_pull_weak = [f for f in funcs
                     if cg.get(f, ("", "")) == ("skb-pull", "UNGUARDED")]
    # per-oracle "distilled" set a human would be handed:
    if otype == "count":
        distilled = high_write
    else:
        # decoded/skb: the genuine (precondition-UNGUARDED) concerns
        distilled = unguarded
    return {
        "raw_hits": len(rows),
        "raw_funcs": len(funcs),
        "high": high,
        "high_write": high_write,
        "resolved": resolved,
        "unguarded": unguarded,
        "skb_pull_weak": skb_pull_weak,
        "distilled": distilled,
        "by_func": funcs,
    }


import auto_real_harness as arh

# function -> full-TU goto binary, for auto-generated verbatim adjudication
# (built once with goto-cc; see verbatim-body-reach doc). Auto-harness is
# preferred over a hand-authored harness when the .gb is present and the
# function is tractable; hand-authored is the fallback for heavy deps.
GB_REGISTRY = {
    "cgw_csum_crc8_pos": ("/tmp/gw.gb", 70),
    "cgw_csum_crc8_neg": ("/tmp/gw.gb", 70),
    "cgw_csum_xor_pos": ("/tmp/gw.gb", 70),
    "cgw_csum_xor_neg": ("/tmp/gw.gb", 70),
    "rxkad_decrypt_ticket": ("/tmp/rxkad.gb", 8),
}


def adjudicate(func, kind_oracle, otype):
    """Return (src, shape_bug, shape_fix, reach_bug, reach_fix).

    Precedence: AUTO-generated verbatim harness (goto-harness on the TU
    .gb, cheapest -- no authoring) -> hand-authored verbatim harness (for
    heavy deps where auto times out) -> parameterised shape model."""
    # (1) auto-generated verbatim harness
    gbrec = GB_REGISTRY.get(func)
    if gbrec and os.path.exists(gbrec[0]):
        verdict, _ = arh.auto_verdict(gbrec[0], func, unwind=gbrec[1],
                                      timeout=90)
        if verdict in ("REAL-OOB", "CLEAN"):
            sb = "FAILED" if verdict == "REAL-OOB" else "SUCCESSFUL"
            return ("auto", sb, "-", "-", "-")
        # else (TIMEOUT / HARNESS-FAIL) fall through to hand-authored
    # (2) hand-authored verbatim harness
    real = tl.REAL_HARNESS.get(func)
    if real:
        hf = os.path.join(HERE, real["file"])
        fl, uw = real["flags"], real["unwind"]
        sb = tl.cbmc_verdict(hf, real["vuln"], fl, uw)
        sf = tl.cbmc_verdict(hf, real["fixed"], fl, uw)
        rb = tl.cover_verdict(hf, real["vuln"],
                              real.get("probe_vuln", real.get("probe")), uw)
        rf = tl.cover_verdict(hf, real["fixed"],
                              real.get("probe_fixed", real.get("probe")), uw)
        return ("REAL", sb, sf, rb, rf)
    # (3) shape model
    import subprocess, tempfile
    cand = next((c for c in CAND_CACHE if c["func"] == func), None)
    raw = cand["raw"] if cand else f"{func}|x|0|{kind_oracle}|x|arr=x[8]"
    fl, uw = tl.ORACLES[ "tainted_count_into_fixed_array.ql"
        if otype == "count" else "decoded_len_arith_overflow.ql"
        if otype == "decoded" else "skb_field_before_lencheck.ql"][1:]
    h = tempfile.mktemp(suffix=".c")
    with open(h, "w") as f:
        subprocess.run([sys.executable, os.path.join(HERE, "oracle_harness_gen.py"),
                        "--oracle", otype, "-c", raw], check=True, stdout=f)
    sb = tl.cbmc_verdict(h, "harness_buggy", fl, uw)
    sf = tl.cbmc_verdict(h, "harness_fixed", fl, uw)
    rb = tl.cover_verdict(h, "harness_buggy", "harness_buggy", uw)
    rf = tl.cover_verdict(h, "harness_fixed", "harness_fixed", uw)
    os.unlink(h)
    return ("shape", sb, sf, rb, rf)


CAND_CACHE = []


def caller_verdicts(db):
    """func -> (kind, verdict) from caller_precondition.ql."""
    m = {}
    for line in tl.run_oracle(db, "caller_precondition.ql"):
        p = line.split("|")
        kind, verdict = "", ""
        for field in p:
            if field.startswith("kind="):
                kind = field.split("=", 1)[1]
            if field.startswith("verdict="):
                verdict = field.split("=", 1)[1]
        if verdict:
            m[p[0]] = (kind, verdict)
    return m


GUARDED = ("CALLER-GUARDED", "PRODUCER-GUARDED")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--db", required=True)
    a = ap.parse_args()

    oracles = [
        ("count/index", "tainted_count_into_fixed_array.ql", "count"),
        ("decoded-len", "decoded_len_arith_overflow.ql", "decoded"),
        ("skb/cursor", "skb_field_before_lencheck.ql", "skb"),
    ]

    print(f"# pipeline_eval on {a.db}\n")
    cg = caller_verdicts(a.db)  # precondition verdicts (precision lever)
    print("## Funnel (per oracle)\n")
    print(f"{'oracle':<14}{'raw_hits':<10}{'raw_funcs':<11}"
          f"{'HIGH&WR':<9}{'precond-resolved':<18}{'genuine(UNGUARDED)':<20}"
          f"{'skb-pull-weak':<15}{'distilled':<10}")
    print("-" * 107)
    survivors = []  # (oracle_label, otype, candidate)
    funnels = {}
    for label, q, otype in oracles:
        fn = funnel(a.db, q, otype, cg)
        funnels[label] = (fn, otype)
        CAND_CACHE.extend(fn["by_func"].values())
        print(f"{label:<14}{fn['raw_hits']:<10}{fn['raw_funcs']:<11}"
              f"{(str(len(fn['high_write'])) if otype=='count' else 'n/a'):<9}"
              f"{len(fn['resolved']):<18}{len(fn['unguarded']):<20}"
              f"{len(fn['skb_pull_weak']):<15}{len(fn['distilled']):<10}")
        # adjudicate: count -> the HIGH&WRITE distilled set; all oracles ->
        # any ground-truth function present in their hits.
        if otype == "count":
            for c in fn["high_write"]:
                survivors.append((label, otype, c))
        for f in fn["by_func"]:
            if f in GROUND_TRUTH and not any(s[2]["func"] == f for s in survivors):
                survivors.append((label, otype, fn["by_func"][f]))

    print("\n## Distilled survivors -- CBMC-adjudicated (shape + reach) + caller-guard\n")
    print(f"{'function':<24}{'oracle':<13}{'src':<6}"
          f"{'shape:b/f':<13}{'reach:b/f':<13}{'caller':<18}{'ground-truth'}")
    print("-" * 112)
    n_timeout = 0
    for label, otype, c in survivors:
        src, sb, sf, rb, rf = adjudicate(c["func"], c["kind"], otype)
        gt = GROUND_TRUTH.get(c["func"], ("", ""))[0]
        cgv = cg.get(c["func"], ("", "no-bound"))[1]
        if "TIMEOUT" in (sb, sf, rb, rf):
            n_timeout += 1
        print(f"{c['func']:<24}{label:<13}{src:<6}"
              f"{(sb[:4]+'/'+sf[:4]):<13}{(rb[:5]+'/'+rf[:5]):<13}"
              f"{cgv:<18}{gt}")

    print("\n## Precondition automation (function-granularity FP filter)\n")
    print("  Survivors marked CALLER-GUARDED (caller validates the bound) or")
    print("  PRODUCER-GUARDED (the field is validated where the struct is")
    print("  filled from the wire) are FPs the automation resolves WITHOUT a")
    print("  harness.")
    ng = sum(1 for _, _, c in survivors
             if cg.get(c["func"], ("", ""))[1] in GUARDED)
    print(f"  precondition-resolved survivors: {ng} / {len(survivors)}")
    print(f"  CBMC-discharge timeouts:         {n_timeout} / {len(survivors)} "
          f"(large functions; need a hand-authored harness or perf work)")

    print("\n## Ground-truth coverage\n")
    for fnc, (cve, note) in GROUND_TRUTH.items():
        where = [lbl for lbl, (fn, ot) in funnels.items() if fnc in fn["by_func"]]
        tier = ""
        for lbl, (fn, ot) in funnels.items():
            if fnc in fn["by_func"]:
                cc = fn["by_func"][fnc]
                tier = f"{cc['confidence'] or 'n/a'}/{cc['impact'] or 'n/a'}"
        flag = ("FLAGGED " + ",".join(where) + f" [{tier}]") if where else "not flagged"
        print(f"  {fnc:<24} {cve:<18} {flag}")
        print(f"      {note}")


if __name__ == "__main__":
    main()
