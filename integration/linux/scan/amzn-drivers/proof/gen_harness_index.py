#!/usr/bin/env python3
"""Generate the harness index from the harnesses and runners themselves.

Why this exists: a finding was reported (and retracted) because a harness for
the function already existed, and the decisive information -- that every one of
its configurations assumed `BUDGET_FITS_PACKET` -- was inside an `#ifdef` that
nothing summarised.  The index is the thing that would have prevented it, so
it is generated rather than written, and it fails loudly when a configuration
macro has no description.

Everything mechanical is extracted:
  * the harness header comment (what is under test)
  * obligation labels, from the "LABEL: ..." strings in __CPROVER_assert
  * the -D macros the harness responds to
  * which runners build it, and which macros each runner passes

The one thing that cannot be extracted is what a macro *assumes away*, since
that is a judgement about the driver.  Those live in harness-assumptions.txt
and this script reports any macro that lacks one.  An undescribed macro is
exactly the hazard that caused the retraction, so it is an error, not a note.
"""
import os
import re
import sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ASSUMPTIONS = os.path.join(HERE, "harness-assumptions.txt")

# Macros that are harness mechanics rather than statements about the driver.
MECHANICAL = {
    "VACUITY_CHECK", "WITNESS", "PEDANTIC", "RING", "RING_SIZE", "PART",
    "NPKT", "NPKTS", "MC", "NB", "QD", "MAXBUFS", "MAX_BUFS", "NQ",
    "CONCRETE_RING", "EXPERIMENT_NAMED_STORE",
}


def harness_files():
    out = []
    for d in (HERE, os.path.join(HERE, "fbsd")):
        if not os.path.isdir(d):
            continue
        for f in sorted(os.listdir(d)):
            if f.startswith("harness_") and f.endswith(".c"):
                out.append(os.path.join(d, f))
    return out


def runner_files():
    out = []
    for d in (HERE, os.path.join(HERE, "fbsd")):
        if not os.path.isdir(d):
            continue
        for f in sorted(os.listdir(d)):
            if f.startswith("run_") and f.endswith(".sh"):
                out.append(os.path.join(d, f))
    return out


def header_comment(text):
    """The leading block comment, as a single wrapped paragraph."""
    m = re.match(r"\s*/\*(.*?)\*/", text, re.S)
    if not m:
        return ""
    body = m.group(1)
    lines = []
    for ln in body.splitlines():
        ln = re.sub(r"^\s*\*ptr?\s?", "", ln)
        ln = re.sub(r"^\s*\*\s?", "", ln)
        lines.append(ln.rstrip())
    # first paragraph only: up to the first blank line after some text
    para = []
    for ln in lines:
        if not ln.strip():
            if para:
                break
            continue
        para.append(ln.strip())
    return " ".join(para)


def obligations(text):
    """Labels of the form "NAME: explanation" inside __CPROVER_assert."""
    labels = []
    for m in re.finditer(r'"([A-Z][A-Z0-9_]{1,30}):\s*([^"]*)"', text):
        name, desc = m.group(1), m.group(2).strip()
        if name in ("TODO", "NOTE"):
            continue
        labels.append((name, desc))
    seen, out = set(), []
    for name, desc in labels:
        if name in seen:
            continue
        seen.add(name)
        out.append((name, desc))
    return out


def witnesses(text):
    # Label conventions vary: "vacuity witness: ...", "witness: ...", and
    # "zero-length witness: ..." all appear.  Matching only the first of
    # those once undercounted harness_rx_reqid.c as having none.
    return sorted(set(re.findall(r'"([^"]*witness[^"]*)"', text)))


def macros(text):
    found = set(re.findall(r"^#\s*if(?:n?def)\s+([A-Za-z_][A-Za-z0-9_]*)",
                           text, re.M))
    found |= set(re.findall(r"defined\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)", text))
    return found


def runner_map():
    """harness basename -> {runner basename: set(macros it passes)}"""
    m = defaultdict(lambda: defaultdict(set))
    for rf in runner_files():
        txt = open(rf).read()
        rname = os.path.basename(rf)
        passed = set(re.findall(r"-D([A-Za-z_][A-Za-z0-9_]*)", txt))
        for hm in re.findall(r"(harness_[A-Za-z0-9_]+)\.c", txt):
            m[hm][rname] |= passed
    return m


def load_assumptions():
    d = {}
    if not os.path.exists(ASSUMPTIONS):
        return d
    for ln in open(ASSUMPTIONS):
        ln = ln.strip()
        if not ln or ln.startswith("#"):
            continue
        if ":" not in ln:
            continue
        k, v = ln.split(":", 1)
        d[k.strip()] = v.strip()
    return d


def main():
    assumptions = load_assumptions()
    rmap = runner_map()
    undescribed = defaultdict(list)
    rows = []

    for hf in harness_files():
        text = open(hf).read()
        base = os.path.basename(hf)[: -len(".c")]
        rel = os.path.relpath(hf, HERE)
        ms = {m for m in macros(text) if m not in MECHANICAL}
        runners = rmap.get(base, {})
        for m in sorted(ms):
            if m not in assumptions:
                undescribed[m].append(rel)
        rows.append({
            "file": rel,
            "what": header_comment(text),
            "obligations": obligations(text),
            "witnesses": witnesses(text),
            "macros": sorted(ms),
            "runners": {r: sorted(v & ms) for r, v in sorted(runners.items())},
        })

    out = []
    out.append("# Harness index\n")
    out.append("**Generated by `proof/gen_harness_index.py`. Do not edit by "
               "hand.**\n")
    out.append(
        "One row per harness: what it puts under test, the obligations it "
        "asserts, whether it carries a vacuity witness, and -- the column "
        "this index exists for -- what each configuration macro assumes "
        "away. A finding was once reported and retracted because that last "
        "column did not exist anywhere.\n")
    out.append(f"{len(rows)} harnesses.\n")

    n_no_witness = [r["file"] for r in rows if not r["witnesses"]]
    out.append("## Harnesses with no vacuity witness\n")
    if n_no_witness:
        out.append(
            "A harness with no witness can pass because the state its "
            "property describes never arises. These need one, or a note "
            "saying why not:\n")
        for f in n_no_witness:
            out.append(f"* `{f}`")
    else:
        out.append("None.")
    out.append("")

    out.append("## Index\n")
    for r in rows:
        out.append(f"### `{r['file']}`\n")
        if r["what"]:
            out.append(r["what"] + "\n")
        if r["obligations"]:
            out.append("| obligation | asserts |")
            out.append("|---|---|")
            for name, desc in r["obligations"]:
                out.append(f"| `{name}` | {desc} |")
            out.append("")
        if r["macros"]:
            out.append("| configuration | assumes |")
            out.append("|---|---|")
            for m in r["macros"]:
                out.append(f"| `-D{m}` | "
                           f"{assumptions.get(m, '**UNDESCRIBED**')} |")
            out.append("")
        if r["runners"]:
            for rn, ms in r["runners"].items():
                shown = ", ".join(f"`-D{m}`" for m in ms) or "no restricting macros"
                out.append(f"* run by `{rn}`: {shown}")
            out.append("")
        out.append(f"* vacuity witness: "
                   f"{'yes, ' + str(len(r['witnesses'])) if r['witnesses'] else '**none**'}")
        out.append("")

    # .../integration/linux/scan/amzn-drivers/proof -> .../integration/linux/doc
    dest = os.path.abspath(os.path.join(
        HERE, "..", "..", "..", "doc", "harness-index-2026-11.md"))
    with open(dest, "w") as fh:
        fh.write("\n".join(out) + "\n")
    print(f"wrote {dest} ({len(rows)} harnesses)")

    if undescribed:
        print("\nERROR: configuration macros with no entry in "
              "harness-assumptions.txt:", file=sys.stderr)
        for m, files in sorted(undescribed.items()):
            print(f"  {m}  (in {', '.join(files)})", file=sys.stderr)
        print("\nAn undescribed macro is how a finding got reported against a "
              "configuration that had already been excluded on purpose. Add a "
              "line 'MACRO: what it assumes away' and re-run.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
