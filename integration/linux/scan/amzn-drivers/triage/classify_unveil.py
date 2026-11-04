#!/usr/bin/env python3
"""Classify the Velocity Labs Unveil findings for amzn-drivers by failure class
and by trigger, from the text of the overview page.

  pip install pypdf
  classify_unveil.py unveil_amzn-drivers.pdf

The classification is keyword-based over each finding's Impact column, so the
counts are approximate (+/- a few per class) and are meant for deciding where
to aim verification effort, not for bookkeeping.
"""
import json
import re
import sys
from collections import Counter, defaultdict

CLASS = [
    ("A lifetime: device DMA/CQ writes into freed memory",
     r"DMA into freed|writes into a freed|DMA into a freed|into freed (kernel|queue|CQ)|write-after-free"),
    ("B lifetime: UAF / double-free / freed-page mapping",
     r"use-?after-?free|double[- ]free|still-registered|mapping of freed|returned to the wrong allocator"),
    ("C stale / live resource after a failed path",
     r"\bstale\b|Live .*(after|on an)|rejected rollback|revoked|reused|recycl|stays registered|silently dropped"),
    ("D leak (memory / resource / IRQ / ID)", r"\bleak"),
    ("E deadlock / hang / lockup", r"deadlock|permanent hang|hung|hang\b|lockup|hangs"),
    ("F lost event / stuck / no progress / timeout",
     r"Lost (completion|event)|lost wakeup|stuck|stall|times out|never |no progress|wedge|breaks on"),
    ("G memory corruption / out-of-bounds",
     r"heap corrupt|out of bounds|out-?of-?bounds|past the|overflow|corrupt"),
    ("H NULL / invalid deref / oops", r"NULL|invalid pointer|oops|panic|fault"),
    ("I integer wrap / arithmetic", r"wrap|underflow|truncat|over-? ?advance"),
    ("J uninitialised / info exposure", r"uninitiali|info[- ]?leak|exposes|disclos"),
    ("K unrecoverable/wrong state, outage (availability)",
     r"stays down|outage|mismatch|too little|Interface|port resets|reset"),
]
TAG = r"(Linux ENA|Linux EFA|ena_com \(shared\)|FreeBSD ENA|DPDK ENA PMD)"
CONC = r"\brac(e|es|ed|ing)\b|concurrent|interleav|mid-probe|interrupts a|while .* holds|simultaneous|in flight"
ERRP = r"fail(s|ed|ure)?|reject|timeout|times out|transient|error path|rollback|aborted"


def main(path):
    from pypdf import PdfReader
    raw = "\n".join(p.extract_text() for p in PdfReader(path).pages)
    raw = raw.replace("\ufb01", "fi").replace("\ufb02", "fl")
    raw = re.sub(r"Unveil findings in amzn-drivers \(ENA and EFA\) https://\S+", " ", raw)
    raw = re.sub(r"\d+ of \d+ \d{4}-\d\d-\d\d, \d\d:\d\d", " ", raw)
    flat = re.sub(r"\s+", " ", raw)

    items = []
    for m in re.finditer(TAG, flat):
        impact = re.split(r"Opus 5|GPT-5\.6|Sonnet|proved|patch,|checked",
                          flat[m.end():m.end() + 170])[0].strip()
        if len(impact) <= 25 or re.search(r"modeled the drivers|Figure ", impact):
            continue
        ctx = flat[max(0, m.start() - 260):m.start()] + " " + impact
        cls = next((n for n, p in CLASS if re.search(p, impact, re.I)), "Z other")
        items.append({"comp": m.group(1), "impact": impact, "cls": cls,
                      "conc": bool(re.search(CONC, ctx, re.I)),
                      "err": bool(re.search(ERRP, ctx, re.I))})

    tot = len(items)
    print(f"findings parsed: {tot}")
    print(f"by component: {dict(Counter(i['comp'] for i in items))}\n")
    by = defaultdict(list)
    for i in items:
        by[i["cls"]].append(i)
    for name, _ in CLASS + [("Z other", None)]:
        v = by.get(name, [])
        if v:
            print(f"{len(v):4} ({100 * len(v) // tot:2}%)  {name}")
    conc = sum(i["conc"] for i in items)
    err = sum(i["err"] for i in items)
    nei = sum(1 for i in items if not i["conc"] and not i["err"])
    print(f"\nconcurrency-triggered: {conc} ({100 * conc // tot}%)")
    print(f"error-path-triggered:  {err} ({100 * err // tot}%)")
    print(f"neither:               {nei} ({100 * nei // tot}%)")
    json.dump(items, open("/tmp/unveil_classified.json", "w"), indent=1)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else
         "/home/ubuntu/unveil_amzn-drivers.pdf")
