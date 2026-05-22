#!/usr/bin/env python3
"""
classify_cves_v3.py — patch-level classifier.

Fetches each CVE's fix-commit patch text from git.kernel.org
and classifies based on what the patch DOES (rather than the
description text alone).  Falls back to the v2 description-
based classifier when the patch can't be fetched.

Reduces the conservative 'other' bucket by attributing many
CVEs to specific bug-shape categories based on the patch's
actual edits.

Caches fetched patches to disk so re-runs are cheap.

Usage:
  python3 classify_cves_v3.py
  python3 classify_cves_v3.py --only-other  # process only
                                              # CVEs that v2
                                              # marked as 'other'
"""
import argparse
import csv
import json
import re
import sys
import urllib.request
import urllib.error
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

VULNS = Path("/tmp/cve-survey/vulns/cve/published")
YEARS = ["2023", "2024", "2025", "2026"]
PATCH_CACHE = Path("/tmp/cve-survey/patch_cache")
PATCH_CACHE.mkdir(parents=True, exist_ok=True)
V2_CSV = Path("/tmp/cve-survey/cves_classified_v2.csv")

PATCH_URL = (
    "https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/"
    "linux.git/patch/?id={hash}"
)


# === v2 categories (re-imported here so v3 can fall back) ===
# Compact version for fall-back; v2 has the full set in
# cve_survey_classify_v2.py.  We don't re-import to avoid path
# coupling.
V2_CATEGORIES = [
    ("refcount_balance", [
        r"\brefcount[_ ]",
        r"\b(?:get|put)_(?:device|file|cred|pid|mnt|task)\b",
        r"\bsock_(?:put|hold)\b",
        r"\b(?:dput|dget|iput|ihold|igrab|fput)\b",
        r"\btry_module_get\b", r"\bmodule_put\b",
        r"\bkobject_(?:put|get)\b",
        r"\bkref_(?:put|get|init)\b",
        r"\bbio_(?:get|put|kref)\b",
        r"\bof_node_(?:get|put)\b",
        r"\bdst_(?:hold|release)\b",
        r"\bclass_(?:get|put)\b",
        r"\bnetdev_(?:hold|put)\b",
        r"\bnf_conntrack_(?:get|put)\b",
        r"\bcss_(?:get|put)\b",
        r"\bclk_(?:get|put|prepare)\b",
        r"\bpm_runtime_(?:get|put)\b",
        r"\bmissing\s+(?:put|free|release|dput|fput|iput)\b",
    ]),
    ("use_after_free", [
        r"\buse[- ]after[- ]free\b", r"\buaf\b",
        r"\bkasan:\s*slab[- ]use[- ]after[- ]free\b",
        r"\baccess(?:ing)?\s+(?:already[- ])?freed\b",
        r"\b(?:read|use|access).*after\s+free\b",
        r"\bdangling\s+pointer\b",
    ]),
    ("double_free_or_unlock", [
        r"\bdouble[- ]free\b", r"\bdouble[- ]put\b",
        r"\bdouble[- ]unlock\b",
        r"\bunbalanced\s+(?:lock|put|get|ref)",
        r"\bfreed\s+twice\b",
    ]),
    ("null_pointer_deref", [
        r"\bnull[- ]pointer\s+dereference\b",
        r"\bnull[- ]ptr[- ]deref\b", r"\bnull[- ]deref\b",
        r"\bdereference\s+(?:of\s+)?(?:a\s+)?null\b",
        r"\bnpe\b",
        r"\bmissing\s+null\s+(?:check|test)\b",
    ]),
    ("out_of_bounds", [
        r"\bout[- ]of[- ]bounds\b", r"\boob\b",
        r"\bbuffer\s+over[- ]?(?:read|run|flow)\b",
        r"\barray[- ]index[- ]out\b",
        r"\bslab[- ]out[- ]of[- ]bounds\b",
        r"\barray-bounds\b",
        r"\bstack\s+overflow\b",
    ]),
    ("resource_leak", [
        r"\bmemory\s+leak\b", r"\bmem(?:ory)?\s+leak\b",
        r"\bleaks?\s+memory\b",
        r"\bleaks?\s+a?\s+(?:reference|skb|page|file|cred|inode|object|dentry|kref)\b",
        r"\bnot\s+(?:freed|released|put)\b",
        r"\bforget(?:s|ting)?\s+to\s+(?:free|put|release|kfree|kvfree|fput|iput|dput)\b",
        r"\bmissing\s+(?:kfree|kvfree|free|put|release|fdput|iput|dput|fput|kobject_put)\b",
        r"\b(?:skb|page|inode|file)\s+leak\b",
        r"\berror\s+path.*(?:leak|fail.*to\s+free|fail.*to\s+release)\b",
    ]),
    ("cleanup_ordering", [
        r"\bcancel_(?:delayed_)?work_sync\b",
        r"\bcancel_work_sync\b",
        r"\bdel_timer_sync\b", r"\btimer_delete_sync\b",
        r"\bdestroy_workqueue\b",
        r"\bbefore\s+(?:freeing|kfree|free|destroy)\b",
    ]),
    ("race_or_toctoue", [
        r"\brace\s+condition\b",
        r"\brace\s+(?:between|when|on|in|window|around)\b",
        r"\btoctou\b", r"\bdata\s+race\b",
        r"\bconcurrent\s+(?:access|modification|writers|readers)\b",
        r"\bracy\b",
    ]),
    ("rcu_misuse", [
        r"\brcu[- ]read[- ](?:un)?lock\b",
        r"\brcu_dereference\b",
        r"\bsleep(?:able|ing)?\s+in\s+(?:rcu|atomic)\b",
        r"\bsuspicious\s+rcu\b",
        r"\bsynchronize_rcu\b",
    ]),
    ("lock_discipline", [
        r"\bdeadlock\b", r"\babba\b",
        r"\block[- ]?dep\b",
        r"\bsleep(?:ing)?\s+while\s+atomic\b",
        r"\bcircular\s+lock\b",
        r"\block\s+inversion\b",
    ]),
    ("bpf_verifier_or_runtime", [
        r"\bbpf\s+verifier\b", r"\bebpf\b",
        r"\bbpf\s+prog(?:ram)?\b",
        r"\bbpf_jit\b",
    ]),
    ("crypto_api", [
        r"\b(?:crypto|aead|skcipher|shash|akcipher)_\w+\b",
        r"\baes[a-z_]*\b.*key\b",
        r"\bgcm_\w+\b",
        r"\bkeyctl\b",
    ]),
    ("uninit_or_info_leak", [
        r"\buninitiali[sz]ed\s+(?:memory|read|access|use|value|byte|field|data|stack)\b",
        r"\binformation\s+leak\b", r"\binfoleak\b",
        r"\bleak.*kernel.*to\s+(?:user|userspace)\b",
        r"\bcopy.*to_user.*uninit",
        r"\binfo[- ]leak\b",
    ]),
    ("string_or_copy_bound", [
        r"\bstrncpy\b",
        r"\bstrn?cpy_from_user\b",
        r"\bcopy_(?:from|to)_user\b.*(?:overflow|truncate|bound)",
        r"\bsnprintf\b.*overflow",
        r"\bstring\s+truncation\b",
    ]),
    ("integer_overflow", [
        r"\binteger\s+overflow\b",
        r"\binteger\s+wraparound\b",
        r"\boverflow\s+check\b",
        r"\bsize_(?:t|of)\s+(?:overflow|wrap)\b",
        r"\bcheck_(?:add|mul|sub)_overflow\b",
        r"\bunderflow\b",
    ]),
    ("dos_panic_warn", [
        r"\bdivision\s+by\s+zero\b", r"\bdiv[- ]by[- ]zero\b",
        r"\bbug_on\(\)?\s+trigger",
        r"\bwarn(?:_on)?\(\)?\s+trigger",
        r"\bsoft\s+lockup\b", r"\bhard\s+lockup\b",
        r"\bhung\s+task\b", r"\bkernel\s+panic\b",
        r"\binfinite\s+loop\b",
    ]),
    ("permission_bypass", [
        r"\bcapability\s+check\b",
        r"\bprivilege\s+escalation\b",
        r"\bpath\s+traversal\b",
        r"\bunprivileged.*can\b",
        r"\bbypass\s+(?:check|guard|protection)\b",
    ]),
    ("format_string", [r"\bformat\s+string\b"]),
]

# === v3 patch-level patterns ===
# Each pattern: (label, regex on the patch text).  Patch text
# includes diff context lines (`+`, `-`, ` `); the patterns
# look at the NET CHANGES (added or removed code).
V3_PATCH_PATTERNS = [
    # Refcount additions: `+ put_X(...)` or `+ get_X(...)` adds.
    ("refcount_balance", [
        r"\+\s*(?:get|put)_(?:device|file|cred|pid|mnt|task)\(",
        r"\+\s*(?:dput|dget|iput|ihold|igrab|fput)\(",
        r"\+\s*sock_(?:put|hold)\(",
        r"\+\s*kobject_(?:put|get)\(",
        r"\+\s*kref_(?:put|get|init)\(",
        r"\+\s*bio_(?:get|put)\(",
        r"\+\s*of_node_(?:get|put)\(",
        r"\+\s*pm_runtime_(?:get|put)\(",
        r"\+\s*nf_conntrack_(?:get|put)\(",
    ]),
    # Null check additions in the FIX patch are strong signal.
    ("null_pointer_deref", [
        r"\+\s*if\s*\(!\s*\w+\s*\)\s*\{?",
        r"\+\s*if\s*\(\s*\w+\s*==\s*NULL\s*\)",
        r"\+\s*if\s*\(IS_ERR\(",
        r"\+\s*if\s*\(IS_ERR_OR_NULL\(",
    ]),
    # kfree/kfree_skb additions in the FIX patch.
    ("resource_leak", [
        r"\+\s*kfree\(",
        r"\+\s*kvfree\(",
        r"\+\s*kfree_skb\(",
        r"\+\s*free_(?:netdev|page|pages|percpu)\(",
        r"\+\s*kmem_cache_free\(",
        r"\+\s*goto\s+(?:err|out|free|cleanup)",
    ]),
    # cancel_*_sync / del_timer_sync added before kfree.
    ("cleanup_ordering", [
        r"\+\s*cancel_(?:delayed_)?work_sync\(",
        r"\+\s*cancel_work_sync\(",
        r"\+\s*del_timer_sync\(",
        r"\+\s*timer_delete_sync\(",
        r"\+\s*destroy_workqueue\(",
        r"\+\s*flush_(?:work|workqueue)\(",
    ]),
    # mutex/spinlock pairs added → lock discipline.
    ("lock_discipline", [
        r"\+\s*mutex_(?:lock|unlock)\(",
        r"\+\s*spin_(?:un)?lock(?:_irq(?:save|restore)?|_bh)?\(",
        r"\+\s*read_(?:un)?lock\(",
        r"\+\s*write_(?:un)?lock\(",
        r"\+\s*down_(?:read|write)\(",
        r"\+\s*up_(?:read|write)\(",
    ]),
    # rcu_read_lock / synchronize_rcu / rcu_assign_pointer
    ("rcu_misuse", [
        r"\+\s*rcu_read_(?:un)?lock\(",
        r"\+\s*rcu_assign_pointer\(",
        r"\+\s*synchronize_rcu\(",
        r"\+\s*rcu_dereference\b",
        r"\+\s*smp_(?:store_release|load_acquire|wmb|rmb|mb)\(",
    ]),
    # Bounds checks added: `+ if (n >= ARRAY_SIZE(...))` or
    # `+ if (off + len > ...)` — out_of_bounds class.
    ("out_of_bounds", [
        r"\+\s*if\s*\(.*>=?\s*ARRAY_SIZE\(",
        r"\+\s*if\s*\(.*>\s*\w+_(?:MAX|LEN|SIZE)\b",
        r"\+\s*if\s*\(.*\+.*>\s*\w+\b",
        r"\+\s*nla_get_u\d+\b",  # validation around nla_get
    ]),
    # check_mul_overflow / kmalloc_array conversion → integer_overflow.
    ("integer_overflow", [
        r"\+\s*if\s*\(check_(?:add|mul|sub)_overflow\(",
        r"\+\s*kmalloc_array\(",
        r"\+\s*kcalloc\(",
        r"\+\s*size_mul\(",
        r"-\s*kmalloc\([^)]*\*[^)]*sizeof",
        r"\+\s*if\s*\(.*\s*>\s*SIZE_MAX\s*/",
    ]),
    # Memset before copy_to_user → uninit_or_info_leak.
    ("uninit_or_info_leak", [
        r"\+\s*memset\([^)]*,\s*0,",
        r"\+\s*=\s*\{\s*0\s*\}",
        r"\+\s*=\s*\{\s*\}",
    ]),
    # Capability / permission checks added.
    ("permission_bypass", [
        r"\+\s*if\s*\(!?\s*capable\(",
        r"\+\s*ns_capable\(",
        r"\+\s*if\s*\(!?\s*ns_capable_noaudit\(",
    ]),
    # Divisor non-zero checks → dos_panic_warn (subset).
    ("dos_panic_warn", [
        r"\+\s*if\s*\(.*\s*==\s*0\s*\)\s*(?:return|goto)",
        r"\+\s*if\s*\(!\s*\w+_per_sec\b",
        r"\+\s*WARN_ON\(",
        r"\+\s*WARN\(",
    ]),
    # Concurrent / TOCTOU: smp_store_release, swap_pointer, etc.
    ("race_or_toctoue", [
        r"\+\s*smp_store_release\(",
        r"\+\s*smp_load_acquire\(",
        r"\+\s*WRITE_ONCE\(",
        r"\+\s*READ_ONCE\(",
        r"\+\s*atomic_(?:try_)?cmpxchg\(",
    ]),
    # use_after_free fix shapes.
    ("use_after_free", [
        # Reorder: kfree moved AFTER something
        r"\+\s*\w+\s*=\s*NULL\s*;",
        r"\+\s*p\s*=\s*NULL\s*;",
    ]),
    # double-free/unlock additions.
    ("double_free_or_unlock", [
        r"-\s*kfree\(",  # removed kfree often = double-free fix
        r"-\s*mutex_unlock\(",
        r"-\s*spin_unlock\(",
    ]),
]


def fetch_patch(commit_hash: str) -> str | None:
    """Fetch and cache patch text for a commit hash."""
    if not commit_hash or len(commit_hash) < 8:
        return None
    cache_file = PATCH_CACHE / f"{commit_hash[:12]}.patch"
    if cache_file.exists():
        try:
            data = cache_file.read_text(errors="replace")
            if data:  # non-empty cache hit
                return data
            # Empty cache means a previous fetch failed.  Retry.
        except Exception:
            return None
    url = PATCH_URL.format(hash=commit_hash)
    # Try up to 3 times with backoff, in case of rate limiting.
    import time
    for attempt in range(3):
        try:
            req = urllib.request.Request(
                url,
                headers={"User-Agent": "cbmc-cve-survey/1.0"},
            )
            with urllib.request.urlopen(req, timeout=30) as resp:
                data = resp.read().decode(errors="replace")
            if data:
                cache_file.write_text(data)
                return data
        except urllib.error.HTTPError as e:
            if e.code == 404:
                # Real 404 — commit not found upstream.  Cache empty.
                cache_file.write_text("")
                return None
            # Rate limit or other error — back off and retry.
            time.sleep(2 ** attempt)
        except Exception:
            time.sleep(2 ** attempt)
    # All retries failed.  Don't cache so we can retry next run.
    return None


def fix_commit_for_cve(jpath: Path) -> str | None:
    """Extract the upstream-fix commit hash from a CVE record."""
    try:
        doc = json.loads(jpath.read_text())
    except Exception:
        return None
    cna = doc.get("containers", {}).get("cna", {})
    affected = cna.get("affected", [])
    # Pick the FIRST affected entry's first lessThan (the fix).
    for a in affected:
        for v in a.get("versions", []):
            if v.get("status") == "affected" and v.get("lessThan"):
                lt = v["lessThan"]
                # Skip non-hash version markers.
                if re.match(r"^[0-9a-f]{8,}$", lt):
                    return lt
    return None


def classify_v2(text: str) -> str:
    """v2 classifier — description text only."""
    text_lower = text.lower()
    for label, patterns in V2_CATEGORIES:
        for pat in patterns:
            if re.search(pat, text_lower):
                return label
    return "other"


def classify_v3(desc: str, patch: str | None) -> str:
    """v3 classifier: try patch-level first; fall back to v2."""
    # Score each category by counting how many patterns match.
    if patch:
        scores = {}
        for label, patterns in V3_PATCH_PATTERNS:
            count = 0
            for pat in patterns:
                count += len(re.findall(pat, patch))
            if count > 0:
                scores[label] = count
        if scores:
            # Return the highest-scoring category.
            return max(scores.items(), key=lambda kv: kv[1])[0]

    # Patch unavailable or no signal → fall back to v2.
    return classify_v2(desc)


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
    category = classify_v3(desc_text, patch)
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
    parser.add_argument(
        "--only-other", action="store_true",
        help="Only re-classify CVEs that v2 marked as 'other'.")
    parser.add_argument(
        "--workers", type=int, default=20,
        help="Number of concurrent fetch workers.")
    args = parser.parse_args()

    # Optionally filter to CVEs v2 marked 'other'.
    only_set: set[str] | None = None
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

    rows: list[dict] = []
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

    # Combine v3 results with the original v2 CSV (so non-other
    # rows are also present).
    if only_set is not None:
        v3_rows = {r["cve"]: r for r in rows}
        combined = []
        with V2_CSV.open() as f:
            for v2_row in csv.DictReader(f):
                if v2_row["cve"] in v3_rows:
                    v3 = v3_rows[v2_row["cve"]]
                    combined.append({
                        "cve": v3["cve"],
                        "year": v3["year"],
                        "category": v3["category"],
                        "subsystem": v2_row.get("subsystem", "unknown"),
                        "summary": v3["summary"],
                        "had_patch": v3["had_patch"],
                    })
                else:
                    v2_row["had_patch"] = "n/a"
                    combined.append(v2_row)
        rows = combined

    out_csv = Path("/tmp/cve-survey/cves_classified_v3.csv")
    with out_csv.open("w", newline="") as f:
        fieldnames = (
            list(rows[0].keys()) if rows else
            ["cve", "year", "category", "subsystem", "summary",
             "had_patch"])
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    # Print frequency table.
    from collections import Counter
    cat = Counter(r["category"] for r in rows)
    print(f"\n=== v3 classifier results ({len(rows)} CVEs) ===")
    print(f"  {'category':30s} {'count':>6s}  {'%':>6s}")
    total = len(rows) or 1
    for c, n in cat.most_common():
        print(f"  {c:30s} {n:6d}  {100.0*n/total:5.1f}%")

    print()
    print("Patch retrieval rate:")
    had = sum(1 for r in rows if r.get("had_patch") in (True, "True"))
    total_processed = len([r for r in rows
                            if r.get("had_patch") != "n/a"])
    if total_processed:
        print(f"  {had}/{total_processed} = {100.0*had/total_processed:.1f}%")

    print(f"\nFull CSV written to {out_csv}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
