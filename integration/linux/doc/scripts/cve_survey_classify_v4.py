#!/usr/bin/env python3
"""
classify_cves_v4.py — v3 + commit subject + file heuristics.

v3 caught a lot of CVEs by reading the patch's `+` /  `-`
diff lines.  But many CVEs in the surviving 'other' bucket
have patches whose individual diff lines don't match v3's
patterns yet the COMMIT SUBJECT or affected FILES strongly
suggest the bug shape.

v4 adds two extra signal sources, scored alongside v3's
patch-line patterns:

1. **Commit-subject patterns.**  The patch's `Subject:`
   line is often very specific:
   "fix memory leak in foo_init", "prevent NULL deref in
   bar", "guard against double-free in baz".  We match a
   broad set of these phrasings.

2. **Affected-file heuristics.**  Some files have a strong
   prior (e.g. fs/binfmt_*.c bugs are usually
   resource_leak / use_after_free; net/netfilter/*.c bugs
   often involve out_of_bounds / memcorruption).

3. **Combined scoring.**  A category gets points from each
   of: v3 patch-line patterns, v4 commit-subject patterns,
   v4 file-prior weights.  The category with the highest
   total score wins.  Ties broken by category specificity.

Usage:
  python3 classify_cves_v4.py --only-other [--workers 8]
"""
import argparse
import csv
import json
import re
import sys
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

# Reuse the v3 module's primitives.
sys.path.insert(0, str(Path(__file__).parent))
from cve_survey_classify_v3 import (
    fetch_patch, fix_commit_for_cve, classify_v2,
    V3_PATCH_PATTERNS, VULNS, YEARS, V2_CSV,
)


# ---------------------------------------------------------------------
# v4 commit-subject patterns.  Match against the `Subject:` line of
# the fetched patch (everything after `Subject:` up to the next
# blank line).
# ---------------------------------------------------------------------
V4_SUBJECT_PATTERNS = [
    ("null_pointer_deref", [
        r"\bnull[- ](?:ptr|pointer)[- ]?deref\b",
        r"\bfix\s+null[- ](?:ptr|pointer)\b",
        r"\bnpe\b",
        r"\bavoid\s+null\b",
        r"\bcheck\s+(?:for\s+)?null\b",
        r"\b!?\s*ptr\s+(?:check|test|guard)\b",
    ]),
    ("resource_leak", [
        r"\b(?:mem(?:ory)?|skb|page|fd|file)\s+leak\b",
        r"\bfix\s+(?:mem(?:ory)?|skb|page|fd|file|reference)\s+leak\b",
        r"\b(?:add|insert)\s+missing\s+(?:put|free|fput|iput|dput|kfree)\b",
        r"\bfree\s+on\s+(?:err|error|fail)\b",
        r"\bbalance\s+(?:get|put)\b",
        r"\bcleanup\s+on\s+error\b",
    ]),
    ("use_after_free", [
        r"\buse[- ]after[- ]free\b",
        r"\buaf\b",
        r"\bfix\s+uaf\b",
        r"\bdangling\s+pointer\b",
        r"\bset.*null\s+after\s+(?:free|kfree)\b",
    ]),
    ("double_free_or_unlock", [
        r"\bdouble[- ](?:free|put|unlock)\b",
        r"\bfreed\s+twice\b",
        r"\bunbalanced\s+(?:lock|put|get|ref)\b",
    ]),
    ("out_of_bounds", [
        r"\bout[- ]of[- ]bounds\b",
        r"\bo+ob\b",
        r"\bbuffer\s+over[- ]?(?:read|run|flow)\b",
        r"\barray[- ]index\b",
        r"\bbounds?\s+check\b",
        r"\bvalidate\s+(?:length|size|count)\b",
    ]),
    ("integer_overflow", [
        r"\bint(?:eger)?\s+overflow\b",
        r"\boverflow\s+check\b",
        r"\bunderflow\b",
        r"\bsigned/unsigned\b",
        r"\b(?:size|len|count)\s+overflow\b",
    ]),
    ("uninit_or_info_leak", [
        r"\buninit(?:iali[sz]ed)?\s+(?:value|data|memory|byte|field|stack|read)\b",
        r"\binfo(?:rmation)?\s+leak\b",
        r"\binfoleak\b",
        r"\bzero[- ]?init(?:iali[sz]e)?\b",
        r"\bmemset.*\b0\b",
        r"\bclear\s+(?:before|prior\s+to)\s+(?:copy|put_user)\b",
    ]),
    ("race_or_toctoue", [
        r"\brace\s+condition\b",
        r"\brace\s+(?:between|when|window)\b",
        r"\btoctou\b",
        r"\bracy\b",
        r"\bdata\s+race\b",
        r"\bconcurrent\s+(?:access|modification)\b",
        r"\bsynchroni[sz]e\b",
    ]),
    ("rcu_misuse", [
        r"\brcu\b.*(?:race|leak|stall|miss|unlock|deref)\b",
        r"\bsynchronize_rcu\b",
        r"\b(?:fix|prevent|avoid).*rcu\b",
    ]),
    ("lock_discipline", [
        r"\bdeadlock\b",
        r"\block\s+(?:order|inversion|abba)\b",
        r"\bsleep(?:ing)?\s+while\s+atomic\b",
        r"\bunlock\s+(?:without\s+lock|missing)\b",
        r"\block(?:dep)?\s+warning\b",
    ]),
    ("cleanup_ordering", [
        r"\bcancel.*before\s+free\b",
        r"\bdel_timer\b",
        r"\bcancel_(?:work|delayed_work|timer)\b",
        r"\bdestroy_workqueue\b",
        r"\b(?:flush|wait)\s+pending\b",
    ]),
    ("dos_panic_warn", [
        r"\b(?:divide|division)\s+by\s+zero\b",
        r"\bdiv[- ]by[- ]zero\b",
        r"\bbug_on\b",
        r"\bwarn(?:_on)?\b.*trigger",
        r"\b(?:soft|hard)\s+lockup\b",
        r"\bhung\s+task\b",
        r"\bkernel\s+panic\b",
        r"\binfinite\s+loop\b",
        r"\bcrash\s+(?:on|when)\b",
        r"\boops\b",
    ]),
    ("permission_bypass", [
        r"\bcapability\s+check\b",
        r"\bprivilege\s+escalation\b",
        r"\bpath\s+traversal\b",
        r"\bbypass\b.*(?:check|guard|protection)",
        r"\bunprivileged\s+(?:user|caller|access)\b",
    ]),
    ("string_or_copy_bound", [
        r"\bstrn?cpy\b.*(?:overflow|bound|truncate)\b",
        r"\bcopy_(?:from|to)_user\b.*(?:overflow|bound|len|size)\b",
        r"\bsnprintf\b.*overflow\b",
        r"\bstring\s+truncation\b",
    ]),
    ("crypto_api", [
        r"\bcrypto\b.*(?:key|cipher|hash|aead)\b",
        r"\baead\b",
        r"\bgcm\b", r"\bccm\b",
        r"\bskcipher\b",
    ]),
    ("bpf_verifier_or_runtime", [
        r"\bbpf\b.*(?:verifier|prog|jit)\b",
        r"\bebpf\b",
    ]),
    ("format_string", [
        r"\bformat\s+string\b",
    ]),
    ("refcount_balance", [
        r"\brefcount\b.*(?:leak|imbalance|underflow|overflow)\b",
        r"\b(?:get|put)_(?:device|file|cred|pid|task)\b",
        r"\bsock_(?:get|put|hold)\b",
        r"\bfix\s+ref(?:erence|count)\s+leak\b",
        r"\bbalance\s+ref(?:count)?\b",
    ]),
]


# ---------------------------------------------------------------------
# v4 file-prior weights.  When the patch touches files matching
# these prefixes, give a bonus to the corresponding category.
# Weighting is light (1-2) so it tips ties, doesn't override
# strong signals.
# ---------------------------------------------------------------------
V4_FILE_PRIORS = [
    # (filename pattern, category, weight)
    (r"^net/netfilter/", "out_of_bounds", 1),
    (r"^net/netlink/", "out_of_bounds", 1),
    (r"^crypto/", "crypto_api", 2),
    (r"^kernel/bpf/", "bpf_verifier_or_runtime", 2),
    (r"^kernel/locking/", "lock_discipline", 1),
    (r"^kernel/rcu/", "rcu_misuse", 2),
    (r"^drivers/.*\.c$", "resource_leak", 1),  # most driver bugs
    (r"^fs/binfmt_", "resource_leak", 1),
    (r"^fs/.*/super\.c$", "use_after_free", 1),
    (r"^security/", "permission_bypass", 1),
    (r"^arch/", "out_of_bounds", 1),  # arch bugs often OOB
]


def extract_subject(patch: str) -> str:
    """Pull the Subject: line out of the patch text."""
    m = re.search(r"^Subject:\s*(.+?)(?=\n[A-Z][a-zA-Z-]*:)",
                  patch, re.DOTALL | re.MULTILINE)
    if m:
        return m.group(1).replace("\n", " ")
    return ""


def affected_files(patch: str) -> list[str]:
    """Pull the diff'd file paths from the patch."""
    return re.findall(r"^diff --git a/(\S+) ", patch, re.MULTILINE)


def classify_v4(desc: str, patch: str | None) -> str:
    """v4 classifier: combine v3 patch-line + v4 subject +
    file-prior signals."""
    if not patch:
        return classify_v2(desc)

    scores: dict[str, int] = defaultdict(int)

    # 1. v3 patch-line patterns (weight 2 each match).
    for label, patterns in V3_PATCH_PATTERNS:
        for pat in patterns:
            scores[label] += 2 * len(re.findall(pat, patch))

    # 2. v4 commit-subject patterns (weight 3 each — strong
    # signal because authors describe what the bug was).
    subject = extract_subject(patch).lower()
    for label, patterns in V4_SUBJECT_PATTERNS:
        for pat in patterns:
            if re.search(pat, subject):
                scores[label] += 3

    # 3. v4 file-prior weights (1-2 each match).
    files = affected_files(patch)
    for fp in files:
        for pat, label, weight in V4_FILE_PRIORS:
            if re.search(pat, fp):
                scores[label] += weight

    if not scores:
        return classify_v2(desc)

    # Highest score wins.
    return max(scores.items(), key=lambda kv: kv[1])[0]


def process_cve(jpath: Path) -> dict | None:
    try:
        doc = json.loads(jpath.read_text())
    except Exception:
        return None
    cve_id = (
        doc.get("cveMetadata", {}).get("cveId") or jpath.stem)
    cna = doc.get("containers", {}).get("cna", {})
    descs = cna.get("descriptions", [])
    desc_text = ""
    for d in descs:
        if d.get("lang") == "en":
            desc_text = d.get("value", "")
            break
    if not desc_text:
        return None
    fix = fix_commit_for_cve(jpath)
    patch = fetch_patch(fix) if fix else None
    category = classify_v4(desc_text, patch)
    lines = [l.strip() for l in desc_text.splitlines() if l.strip()]
    summary = (lines[1] if len(lines) > 1 else lines[0])[:200]
    return {
        "cve": cve_id,
        "year": jpath.parent.name,
        "category": category,
        "summary": summary,
        "had_patch": bool(patch),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--only-other", action="store_true")
    parser.add_argument("--workers", type=int, default=8)
    args = parser.parse_args()

    only_set = None
    if args.only_other:
        only_set = set()
        with V2_CSV.open() as f:
            for row in csv.DictReader(f):
                if row["category"] == "other":
                    only_set.add(row["cve"])
        print(f"Reclassifying {len(only_set)} 'other' CVEs.",
              file=sys.stderr)

    paths = []
    for year in YEARS:
        ydir = VULNS / year
        if ydir.exists():
            for jpath in sorted(ydir.glob("CVE-*.json")):
                if only_set is None or jpath.stem in only_set:
                    paths.append(jpath)

    rows = []
    with ThreadPoolExecutor(max_workers=args.workers) as ex:
        futures = {ex.submit(process_cve, p): p for p in paths}
        done = 0
        for fut in as_completed(futures):
            r = fut.result()
            if r:
                rows.append(r)
            done += 1
            if done % 200 == 0:
                print(f"  ...{done}/{len(paths)}", file=sys.stderr)

    # Combine with v2 (only-other mode) so non-'other' categorise
    # remain as v2 classified.
    if only_set is not None:
        v4_rows = {r["cve"]: r for r in rows}
        combined = []
        with V2_CSV.open() as f:
            for v2_row in csv.DictReader(f):
                if v2_row["cve"] in v4_rows:
                    v4 = v4_rows[v2_row["cve"]]
                    combined.append({
                        "cve": v4["cve"],
                        "year": v4["year"],
                        "category": v4["category"],
                        "subsystem": v2_row.get("subsystem", "unknown"),
                        "summary": v4["summary"],
                        "had_patch": v4["had_patch"],
                    })
                else:
                    v2_row["had_patch"] = "n/a"
                    combined.append(v2_row)
        rows = combined

    out_csv = Path("/tmp/cve-survey/cves_classified_v4.csv")
    with out_csv.open("w", newline="") as f:
        fieldnames = (
            list(rows[0].keys()) if rows else
            ["cve", "year", "category", "subsystem", "summary",
             "had_patch"])
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    cat = Counter(r["category"] for r in rows)
    print(f"\n=== v4 classifier results ({len(rows)} CVEs) ===")
    print(f"  {'category':30s} {'count':>6s}  {'%':>6s}")
    total = len(rows) or 1
    for c, n in cat.most_common():
        print(f"  {c:30s} {n:6d}  {100.0*n/total:5.1f}%")

    # Comparison with v3.
    v3_csv = Path("/tmp/cve-survey/cves_classified_v3.csv")
    if v3_csv.exists():
        v3_cat = Counter()
        with v3_csv.open() as f:
            for r in csv.DictReader(f):
                v3_cat[r["category"]] += 1
        print()
        print("=== v3 vs v4 comparison ===")
        print(f"  {'category':30s} {'v3':>6s} {'v4':>6s} {'delta':>6s}")
        all_cats = set(cat) | set(v3_cat)
        for c in sorted(all_cats, key=lambda x: -cat.get(x, 0)):
            v3 = v3_cat.get(c, 0)
            v4 = cat.get(c, 0)
            print(f"  {c:30s} {v3:6d} {v4:6d} {v4-v3:+6d}")

    print(f"\nFull CSV at {out_csv}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
