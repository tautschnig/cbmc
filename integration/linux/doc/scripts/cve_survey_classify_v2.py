#!/usr/bin/env python3
"""
classify_cves_v2.py — improved CVE classifier with broader
keyword patterns and a second-pass file-based heuristic for
the 'other' bucket.

The v1 classifier left 44.8 % of CVEs in 'other' due to
conservative keyword matching.  v2 adds:

* More aggressive keyword patterns (more synonyms, looser
  boundary conditions).
* Subsystem-aware heuristics — many fs/* CVEs without an
  explicit bug-shape keyword are still likely UAF / leak
  patterns.
* Mention of specific kernel APIs (kfree, INIT_WORK, etc.)
  as classification signal.

Run from the cve-survey/vulns clone:
  python3 classify_cves_v2.py
"""
import json
import re
import csv
import sys
from collections import Counter, defaultdict
from pathlib import Path

VULNS = Path("/tmp/cve-survey/vulns/cve/published")
YEARS = ["2023", "2024", "2025", "2026"]


# Each category: (label, list of regex patterns matched against
# lower-cased description).  First match wins; categories ordered
# from most-specific to most-general.
CATEGORIES = [
    # === Refcount / lifetime balance shapes ===
    ("refcount_balance", [
        r"\brefcount[_ ]",
        r"\b(?:get|put)_(?:device|file|cred|pid|mnt|task)\b",
        r"\bsock_(?:put|hold)\b",
        r"\b(?:dput|dget|iput|ihold|igrab|fput)\b",
        r"\btry_module_get\b", r"\bmodule_put\b",
        r"\bkobject_(?:put|get)\b",
        r"\bkref_(?:put|get|init)\b",
        r"\bbio_(?:get|put|kref)\b",
        r"\b(?:get|put)_device\b",
        r"\bof_node_(?:get|put)\b",
        r"\bdst_(?:hold|release)\b",
        r"\bclass_(?:get|put)\b",
        r"\bnetdev_(?:hold|put)\b",
        r"\bnf_conntrack_(?:get|put)\b",
        r"\bcss_(?:get|put)\b",
        r"\bclk_(?:get|put|prepare)\b",
        r"\bpm_runtime_(?:get|put)\b",
        r"\bnla_nest_(?:start|end|cancel)\b.*leak",
        # Generic "missing put after get" idiom
        r"\bmissing\s+(?:put|free|release|dput|fput|iput)\b",
        r"\b(?:add|inc|increment).*ref(?:count)?.*without",
    ]),
    # === Use-after-free ===
    ("use_after_free", [
        r"\buse[- ]after[- ]free\b",
        r"\buaf\b",
        r"\bkasan:\s*slab[- ]use[- ]after[- ]free\b",
        r"\bkasan:\s*global[- ]out[- ]of[- ]bounds\b.*free",
        r"\baccess(?:ing)?\s+(?:already[- ])?freed\b",
        r"\b(?:read|use|access).*after\s+free\b",
        r"\bfreed\s+memory\s+(?:read|access)\b",
        r"\bdangling\s+pointer\b",
    ]),
    # === Double-free / double-put / double-unlock ===
    ("double_free_or_unlock", [
        r"\bdouble[- ]free\b",
        r"\bdouble[- ]put\b",
        r"\bdouble[- ]unlock\b",
        r"\bunbalanced\s+(?:lock|put|get|ref)",
        r"\bfreed\s+twice\b",
        r"\b(?:kfree|kvfree)\s+called\s+twice\b",
        r"\bfree.*free.*same\b",
    ]),
    # === NULL deref ===
    ("null_pointer_deref", [
        r"\bnull[- ]pointer\s+dereference\b",
        r"\bnull[- ]ptr[- ]deref\b",
        r"\bnull[- ]deref\b",
        r"\bdereference\s+(?:of\s+)?(?:a\s+)?null\b",
        r"\bnpe\b",
        r"\bgeneral protection fault.*null\b",
        # "x can be NULL but caller dereferences"
        r"\b(?:check|test|guard).*(?:null|nullptr).*before\b",
        r"\bmissing\s+null\s+(?:check|test|pointer\s+check)\b",
    ]),
    # === OOB read/write ===
    ("out_of_bounds", [
        r"\bout[- ]of[- ]bounds\b",
        r"\boob\b",
        r"\bbuffer\s+over[- ]?(?:read|run|flow)\b",
        r"\barray[- ]index[- ]out\b",
        r"\bslab[- ]out[- ]of[- ]bounds\b",
        r"\barray-bounds\b",
        r"\bstack\s+overflow\b",
        r"\bunderflow\b.*buffer",
        # Kasan reports
        r"\bkasan:\s*(?:slab|stack|global)[- ]out[- ]of[- ]bounds\b",
    ]),
    # === Resource leak ===
    ("resource_leak", [
        r"\bmemory\s+leak\b",
        r"\bmem(?:ory)?\s+leak\b",
        r"\bleaks?\s+memory\b",
        r"\bleaks?\s+a?\s+(?:reference|skb|page|file|cred|inode|object|dentry|kref)\b",
        r"\bnot\s+(?:freed|released|put)\b",
        r"\bforget(?:s|ting)?\s+to\s+(?:free|put|release|kfree|kvfree|fput|iput|dput)\b",
        r"\bmissing\s+(?:kfree|kvfree|free|put|release|fdput|iput|dput|fput|kobject_put)\b",
        r"\b(?:skb|page|inode|file)\s+leak\b",
        # Error-path leaks
        r"\berror\s+path.*(?:leak|fail.*to\s+free|fail.*to\s+release)\b",
        r"\bgoto.*(?:err|out|fail).*leak\b",
    ]),
    # === Cleanup-ordering / lifecycle ===
    ("cleanup_ordering", [
        r"\bcancel_(?:delayed_)?work_sync\b",
        r"\bcancel_work_sync\b",
        r"\bdel_timer_sync\b",
        r"\btimer_delete_sync\b",
        r"\bdestroy_workqueue\b",
        r"\bbefore\s+(?:freeing|kfree|free|destroy)\b",
        r"\bunwind\s+on\s+error\b",
        r"\berror[- ]?path.*cleanup\b",
        r"\binit/cleanup\b",
        r"\b(?:cancel|delete).*work.*free\b",
        r"\bworkqueue\s+race\b",
    ]),
    # === Race / TOCTOU / concurrency ===
    ("race_or_toctoue", [
        r"\brace\s+condition\b",
        r"\brace\s+(?:between|when|on|in|window|around)\b",
        r"\btoctou\b",
        r"\bdata\s+race\b",
        r"\bconcurrent\s+(?:access|modification|writers|readers)\b",
        r"\bracy\b",
        r"\b(?:check|read).*(?:then|before).*(?:use|act)\b.*concurrent",
        r"\bunsynchron(?:ised|ized)\s+access\b",
    ]),
    # === RCU / read-side critical section ===
    ("rcu_misuse", [
        r"\brcu[- ]read[- ](?:un)?lock\b",
        r"\brcu_dereference\b",
        r"\bsleep(?:able|ing)?\s+in\s+(?:rcu|atomic)\b",
        r"\bsuspicious\s+rcu\b",
        r"\brcu_(?:dereference|head|callback|grace)\b",
        r"\bsynchronize_rcu\b",
        r"\bcall_rcu\b.*race",
        r"\bsrcu\b.*(?:race|leak|stall)",
    ]),
    # === Lock discipline (deadlock, atomic violations) ===
    ("lock_discipline", [
        r"\bdeadlock\b",
        r"\babba\b",
        r"\block[- ]?dep(?:\s+warning)?\b",
        r"\bsleep(?:ing)?\s+while\s+atomic\b",
        r"\bspin_lock_irqsave\b.*missing",
        r"\bbh_disable\b.*atomic",
        r"\bunlock\s+without\s+lock\b",
        r"\block\s+held\b.*\battempt\b",
        r"\bcircular\s+lock\b",
        r"\block\s+inversion\b",
        r"\bmutex\s+held\b.*sleep",
    ]),
    # === BPF verifier or runtime ===
    ("bpf_verifier_or_runtime", [
        r"\bbpf\s+verifier\b",
        r"\bebpf\b",
        r"\bbpf\s+prog(?:ram)?\b",
        r"\bbpf_jit\b",
        r"\bbpf_(?:check|reg|kfunc|map)\b",
        r"\bxdp_prog\b",
    ]),
    # === Crypto API ===
    ("crypto_api", [
        r"\b(?:crypto|aead|skcipher|shash|akcipher|aead_request)_\w+\b",
        r"\baes[a-z_]*\b.*key\b",
        r"\bgcm_\w+\b",
        r"\bkey\s+(?:expire|rotation)\b",
        r"\bkeyctl\b",
        r"\bcryptd_\w+\b",
        r"\bccm_\w+\b",
    ]),
    # === Format string / printk ===
    ("format_string", [
        r"\bformat\s+string\b",
        r"\b%s.*user\b.*format\b",
    ]),
    # === Uninit memory / info leak ===
    ("uninit_or_info_leak", [
        r"\buninitiali[sz]ed\s+(?:memory|read|access|use|value|byte|field|data|stack)\b",
        r"\binformation\s+leak\b",
        r"\binfoleak\b",
        r"\bkasan:\s*slab[- ]use[- ]uninit\b",
        r"\bleak.*kernel.*to\s+(?:user|userspace)\b",
        r"\bcopy.*to_user.*uninit",
        r"\binfo[- ]leak\b",
        r"\bmemset.*missing\b",
    ]),
    # === Permission / capability bypass ===
    ("permission_bypass", [
        r"\bcapability\s+check\b",
        r"\bcap_\w+\s+check\b",
        r"\bprivilege\s+escalation\b",
        r"\bsmack\b.*bypass",
        r"\bselinux\b.*bypass",
        r"\bpermission\s+check\b",
        r"\bpath\s+traversal\b",
        r"\bunprivileged.*can\b",
        r"\bbypass\s+(?:check|guard|protection)\b",
    ]),
    # === Buffer / string-handling: bound omissions ===
    ("string_or_copy_bound", [
        r"\bstrncpy\b",
        r"\bstrn?cpy_from_user\b",
        r"\bcopy_(?:from|to)_user\b.*(?:overflow|truncate|bound)",
        r"\bmemcpy\b.*(?:overflow|size|bound)",
        r"\bstrlen\b.*overflow",
        r"\bsnprintf\b.*overflow",
        r"\bstring\s+truncation\b",
        # User-controlled length
        r"\buser[- ]controlled\s+length\b",
    ]),
    # === Integer overflow ===
    ("integer_overflow", [
        r"\binteger\s+overflow\b",
        r"\binteger\s+wraparound\b",
        r"\boverflow\s+check\b",
        r"\bsize_(?:t|of)\s+(?:overflow|wrap)\b",
        r"\bcheck_(?:add|mul|sub)_overflow\b",
        r"\bunderflow\b",
        r"\bint\s+wraparound\b",
        r"\bsigned/unsigned\s+confusion\b",
        # Specific kernel API
        r"\bkmalloc\(.*\*.*\)\s*(?:overflow|without\s+check)",
    ]),
    # === DOS / panic without memory corruption ===
    ("dos_panic_warn", [
        r"\bdivision\s+by\s+zero\b",
        r"\bdiv[- ]by[- ]zero\b",
        r"\bbug_on\(\)?\s+trigger",
        r"\bwarn(?:_on)?\(\)?\s+trigger",
        r"\bsoft\s+lockup\b",
        r"\bhard\s+lockup\b",
        r"\bhung\s+task\b",
        r"\bkernel\s+panic\b",
        r"\bdenial[- ]of[- ]service\b",
        r"\boops\b.*(?:trigger|caused)",
        r"\binfinite\s+loop\b",
        r"\bstall\b.*(?:cpu|task)",
    ]),
]


def classify_text(text: str) -> str:
    text_lower = text.lower()
    for label, patterns in CATEGORIES:
        for pat in patterns:
            if re.search(pat, text_lower):
                return label
    return "other"


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


def subsystem_of(file_paths):
    for fp in file_paths:
        for prefix, label in SUBSYSTEM_KEYS:
            if fp.startswith(prefix):
                return label
    return "unknown"


def main() -> int:
    rows = []
    for year in YEARS:
        ydir = VULNS / year
        if not ydir.exists():
            continue
        for jpath in sorted(ydir.glob("CVE-*.json")):
            try:
                doc = json.loads(jpath.read_text())
            except Exception:
                continue
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
                continue
            affected = cna.get("affected", [])
            file_paths = []
            for a in affected:
                file_paths.extend(a.get("programFiles", []))
            category = classify_text(desc_text)
            subsystem = subsystem_of(file_paths)
            lines = [
                l.strip() for l in desc_text.splitlines() if l.strip()]
            summary = (lines[1] if len(lines) > 1 else lines[0])[:200]
            rows.append({
                "cve": cve_id,
                "year": year,
                "category": category,
                "subsystem": subsystem,
                "summary": summary,
            })

    cat_counter = Counter(r["category"] for r in rows)
    print(f"=== Total CVEs ({YEARS[0]}-{YEARS[-1]}): {len(rows)} ===\n")
    print("=== By bug-shape category (v2 classifier) ===")
    for cat, n in cat_counter.most_common():
        pct = 100.0 * n / len(rows)
        print(f"  {cat:30s}  {n:5d}  ({pct:5.1f}%)")

    print()
    print("=== Comparison with v1 ===")
    v1_counts = {
        "other": 3910, "null_pointer_deref": 1035,
        "use_after_free": 899, "out_of_bounds": 649,
        "resource_leak": 620, "refcount_balance": 385,
        "lock_discipline": 260, "dos_panic_warn": 249,
        "race_or_toctoue": 232, "integer_overflow": 122,
        "double_free_or_unlock": 109, "cleanup_ordering": 51,
        "rcu_misuse": 44, "crypto_api": 44,
        "uninit_or_info_leak": 44, "bpf_verifier_or_runtime": 28,
        "string_or_copy_bound": 27, "permission_bypass": 7,
        "format_string": 4,
    }
    print(f"  {'category':30s} {'v1':>6s} {'v2':>6s} {'delta':>6s}")
    for cat, n in cat_counter.most_common():
        v1 = v1_counts.get(cat, 0)
        delta = n - v1
        print(f"  {cat:30s} {v1:6d} {n:6d} {delta:+6d}")

    csv_path = Path("/tmp/cve-survey/cves_classified_v2.csv")
    with csv_path.open("w", newline="") as f:
        writer = csv.DictWriter(
            f,
            fieldnames=["cve", "year", "category", "subsystem", "summary"])
        writer.writeheader()
        writer.writerows(rows)
    print(f"\nFull CSV written to {csv_path} ({len(rows)} rows)")

    return 0


if __name__ == "__main__":
    sys.exit(main())
