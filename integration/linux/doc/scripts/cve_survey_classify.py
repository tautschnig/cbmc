#!/usr/bin/env python3
"""
classify_cves.py - classify Linux kernel CVEs by bug-shape, mapped to
proposed CBMC property modules.

Pulls CVE records from the kernel.org vulns repo (already cloned to
/tmp/cve-survey/vulns) and classifies each by description + affected
file paths.  Outputs:

  1. Frequency table of bug-shape categories.
  2. Per-category sample (5 CVE IDs + one-liner descriptions).
  3. Mapping of bug-shape -> proposed property module.
  4. Top kernel subsystems by CVE count.
  5. CSV of (CVE, year, primary_category, primary_subsystem, summary).
"""
import json
import os
import re
import sys
import csv
from collections import Counter, defaultdict
from pathlib import Path

VULNS = Path("/tmp/cve-survey/vulns/cve/published")
YEARS = ["2023", "2024", "2025", "2026"]


# Each category: (label, list of regex/keyword patterns matched against
# the lower-cased description).  First match wins; categories ordered
# from most-specific to most-general.
CATEGORIES = [
    # === Refcount / lifetime balance shapes (existing-pipeline tractable) ===
    ("refcount_balance",
     [r"\brefcount[_ ]",
      r"\bget[_ ]\w+/put[_ ]",
      r"\bsock_put\b", r"\bsock_hold\b",
      r"\bdput\b", r"\bdget\b",
      r"\biput\b.*\biput\b",  # double iput
      r"\bfput\b.*\bleak", r"\bfput.*missing",
      r"\bget_device\b", r"\bput_device\b",
      r"\btry_module_get\b", r"\bmodule_put\b",
      r"\bkobject_(put|get)\b",
      r"\bbio_(get|put)\b",
      r"\b(ihold|igrab)\b",
      r"\bskb_(get|put)\b.*leak",
      r"\bdst_(hold|release)\b",
      r"\bclass_(get|put)\b",
      r"\bget_file\b",
      r"\bnetdev_hold\b",
      r"\bnf_conntrack_(get|put)\b",
      r"\b(get_pid|put_pid)\b",
      ]),
    # === Use-after-free, generic ===
    ("use_after_free",
     [r"\buse[- ]after[- ]free\b",
      r"\buaf\b",
      r"\bkasan: ?slab[- ]use[- ]after[- ]free\b",
      r"\baccess freed\b",
      ]),
    # === Double-free / double-put / double-unlock ===
    ("double_free_or_unlock",
     [r"\bdouble[- ]free\b",
      r"\bdouble[- ]put\b",
      r"\bdouble[- ]unlock\b",
      r"\bunbalanced (lock|put|get|ref)",
      r"\bfreed twice\b",
      ]),
    # === NULL deref ===
    ("null_pointer_deref",
     [r"\bnull[- ]pointer dereference\b",
      r"\bnull[- ]ptr[- ]deref\b",
      r"\bnull[- ]deref\b",
      r"\bdereference (?:of |a )?null\b",
      ]),
    # === OOB read/write ===
    ("out_of_bounds",
     [r"\bout[- ]of[- ]bounds\b",
      r"\bout of bounds\b",
      r"\bOOB\b",
      r"\bbuffer over[- ]?(read|run|flow)\b",
      r"\barray[- ]index[- ]out\b",
      r"\bslab[- ]out[- ]of[- ]bounds\b",
      r"\barray-bounds\b",
      r"\bstack overflow\b",
      ]),
    # === Integer overflow ===
    ("integer_overflow",
     [r"\binteger overflow\b",
      r"\binteger wraparound\b",
      r"\boverflow check\b",
      r"\bsize_(t|of) (?:overflow|wrap)\b",
      r"\bcheck_(?:add|mul|sub)_overflow\b",
      r"\bunderflow\b",
      ]),
    # === Memory leak (resource leak that's NOT refcount balance) ===
    ("resource_leak",
     [r"\bmemory leak\b",
      r"\bmem leak\b",
      r"\bleaks? \w+\b",  # generic "leaks foo"
      r"\bleak (in|of|when)\b",
      r"\bnot freed\b",
      r"\bforget(s|ting)? to (?:free|put|release|kfree)\b",
      r"\bmissing (?:kfree|free|put|release|fdput|iput)\b",
      ]),
    # === Cleanup-ordering / lifecycle (cancel-before-free, etc.) ===
    ("cleanup_ordering",
     [r"\bcancel_(delayed_)?work_sync\b",
      r"\bdel_timer_sync\b",
      r"\bcancel_work_sync\b",
      r"\bdestroy_workqueue\b",
      r"\bbefore (?:freeing|kfree|free)\b",
      r"\bunwind on error\b",
      r"\berror[- ]?path (?:leak|free|cleanup)\b",
      r"\binit/cleanup\b",
      r"\brace.{0,40}cleanup\b",
      r"\bcleanup race\b",
      ]),
    # === Race / TOCTOU / concurrency ===
    ("race_or_toctoue",
     [r"\brace condition\b",
      r"\brace (?:between|when|on|in)\b",
      r"\btoctou\b",
      r"\bdata race\b",
      r"\bconcurrent (?:access|modification)\b",
      r"\bracy\b",
      ]),
    # === RCU / read-side critical section ===
    ("rcu_misuse",
     [r"\brcu[- ]read[- ](?:un)?lock\b",
      r"\brcu_dereference\b",
      r"\bsleep(?:able|ing)? in (?:rcu|atomic)\b",
      r"\bsuspicious rcu\b",
      r"\brcu_(dereference|head|callback)\b",
      r"\bsynchronize_rcu\b",
      ]),
    # === Lock discipline (deadlock, atomic violations) ===
    ("lock_discipline",
     [r"\bdeadlock\b",
      r"\babba\b",  # AB-BA
      r"\block[- ]?dep warning\b",
      r"\bsleeping while atomic\b",
      r"\bspin_lock_irqsave\b.*missing",
      r"\bbh_disable\b.*atomic",
      r"\bunlock without lock\b",
      r"\block held\b.*\battempt\b",
      ]),
    # === BPF verifier or runtime ===
    ("bpf_verifier_or_runtime",
     [r"\bbpf verifier\b",
      r"\bebpf\b",
      r"\bbpf prog\b",
      r"\bkernel/bpf/\b",
      r"\bbpf_jit\b",
      ]),
    # === Crypto API ===
    ("crypto_api",
     [r"\bcrypto[_ ]\w+\b",
      r"\baead[_ ]\w+\b",
      r"\baeskey\b",
      r"\bgcm_\w+\b",
      r"\bkey expire\b",
      r"\bkeyctl\b",
      ]),
    # === Format string / printk class ===
    ("format_string",
     [r"\bformat string\b",
      r"\b%s.*user\b.*format\b",
      ]),
    # === Uninit memory / info leak ===
    ("uninit_or_info_leak",
     [r"\buninitiali[sz]ed (?:memory|read|access|use|value)\b",
      r"\binformation leak\b",
      r"\binfoleak\b",
      r"\bkasan: ?slab[- ]use[- ]uninit\b",
      r"\bleaks? .{0,30}\b(stack|kernel|memory)\b.{0,30} (to|into) (user|userspace)\b",
      ]),
    # === Permission / capability bypass ===
    ("permission_bypass",
     [r"\bcapability check\b",
      r"\bcap_\w+ check\b",
      r"\bprivilege escalation\b",
      r"\bsmack\b.*bypass",
      r"\bselinux\b.*bypass",
      r"\bpermission check\b",
      r"\bpath traversal\b",
      ]),
    # === Buffer / string-handling: bound omissions ===
    ("string_or_copy_bound",
     [r"\bstrncpy\b",
      r"\bstrn?cpy_from_user\b",
      r"\bcopy_(from|to)_user\b",
      r"\bmemcpy\b.*size",
      r"\bstrlen\b.*overflow",
      ]),
    # === DOS / panic without memory corruption ===
    ("dos_panic_warn",
     [r"\bdivision by zero\b",
      r"\bdiv[- ]by[- ]zero\b",
      r"\bbug_on\(\)? trigger",
      r"\bwarn(_on)?\(\)? trigger",
      r"\bsoft lockup\b",
      r"\bhard lockup\b",
      r"\bhung task\b",
      r"\bkernel panic\b",
      r"\bdenial[- ]of[- ]service\b",
      ]),
]


SUBSYSTEM_KEYS = [
    ("crypto/", "crypto"),
    ("drivers/net/", "drivers/net"),
    ("drivers/gpu/", "drivers/gpu"),
    ("drivers/usb/", "drivers/usb"),
    ("drivers/scsi/", "drivers/scsi"),
    ("drivers/block/", "drivers/block"),
    ("drivers/", "drivers/other"),
    ("fs/", "fs"),
    ("net/", "net"),
    ("kernel/bpf/", "kernel/bpf"),
    ("kernel/", "kernel"),
    ("mm/", "mm"),
    ("block/", "block"),
    ("sound/", "sound"),
    ("security/", "security"),
    ("arch/", "arch"),
    ("io_uring/", "io_uring"),
    ("ipc/", "ipc"),
    ("lib/", "lib"),
    ("virt/", "virt"),
    ("kvm/", "kvm"),
]


def classify_text(text: str) -> str:
    text_lower = text.lower()
    for label, patterns in CATEGORIES:
        for pat in patterns:
            if re.search(pat, text_lower):
                return label
    return "other"


def subsystem_of(file_paths: list[str]) -> str:
    for fp in file_paths:
        for prefix, label in SUBSYSTEM_KEYS:
            if fp.startswith(prefix):
                return label
    return "unknown"


def main() -> int:
    rows: list[dict] = []
    for year in YEARS:
        ydir = VULNS / year
        if not ydir.exists():
            continue
        for jpath in sorted(ydir.glob("CVE-*.json")):
            try:
                doc = json.loads(jpath.read_text())
            except Exception:
                continue
            cve_id = doc.get("cveMetadata", {}).get("cveId") or jpath.stem
            cna = doc.get("containers", {}).get("cna", {})
            descs = cna.get("descriptions", [])
            desc_text = ""
            for d in descs:
                if d.get("lang") == "en":
                    desc_text = d.get("value", "")
                    break
            if not desc_text:
                continue
            # Extract affected file paths.
            affected = cna.get("affected", [])
            file_paths: list[str] = []
            for a in affected:
                file_paths.extend(a.get("programFiles", []))
            category = classify_text(desc_text)
            subsystem = subsystem_of(file_paths)
            # First non-headline line is usually the bug summary.
            lines = [l.strip() for l in desc_text.splitlines() if l.strip()]
            summary = lines[1] if len(lines) > 1 else lines[0]
            summary = summary[:200]
            rows.append({
                "cve": cve_id,
                "year": year,
                "category": category,
                "subsystem": subsystem,
                "summary": summary,
            })

    # 1. Frequency table.
    cat_counter = Counter(r["category"] for r in rows)
    print(f"=== Total CVEs ({YEARS[0]}-{YEARS[-1]}): {len(rows)} ===\n")
    print("=== By bug-shape category ===")
    for cat, n in cat_counter.most_common():
        pct = 100.0 * n / len(rows)
        print(f"  {cat:30s}  {n:5d}  ({pct:5.1f}%)")

    # 2. Per-category samples (5 CVEs each, sorted by year desc).
    print("\n=== Per-category samples (most recent 5) ===")
    by_cat: dict[str, list[dict]] = defaultdict(list)
    for r in rows:
        by_cat[r["category"]].append(r)
    for cat, _ in cat_counter.most_common():
        print(f"\n--- {cat} ---")
        sample = sorted(by_cat[cat], key=lambda r: r["cve"], reverse=True)[:5]
        for r in sample:
            print(f"  {r['cve']:20s} {r['subsystem']:18s} {r['summary'][:120]}")

    # 3. Subsystem table.
    print("\n=== By kernel subsystem (top 15) ===")
    sub_counter = Counter(r["subsystem"] for r in rows)
    for sub, n in sub_counter.most_common(15):
        pct = 100.0 * n / len(rows)
        print(f"  {sub:25s}  {n:5d}  ({pct:5.1f}%)")

    # 4. Crosstab: category x subsystem (top 5 combinations per category).
    print("\n=== Top 3 subsystems per category ===")
    for cat, _ in cat_counter.most_common(15):
        sub_in_cat = Counter(r["subsystem"] for r in by_cat[cat])
        top3 = sub_in_cat.most_common(3)
        print(f"  {cat:30s}  " + ", ".join(
            f"{s}={n}" for s, n in top3))

    # 5. CSV dump.
    csv_path = Path("/tmp/cve-survey/cves_classified.csv")
    with csv_path.open("w", newline="") as f:
        writer = csv.DictWriter(
            f,
            fieldnames=["cve", "year", "category", "subsystem", "summary"],
        )
        writer.writeheader()
        writer.writerows(rows)
    print(f"\nFull CSV written to {csv_path} ({len(rows)} rows)")

    return 0


if __name__ == "__main__":
    sys.exit(main())
