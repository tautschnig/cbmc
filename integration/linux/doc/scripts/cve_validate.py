"""cve_validate.py — Validate the CBMC kernel property
catalog against historical CVEs.

For each CVE in a sample:

  1. Look up the patch (already cached at /tmp/cve-survey/
     patch_cache).
  2. Extract the modified file + (best-effort) the changed
     function name from the unified-diff hunks.
  3. Find the file in one of our checked-out LTS kernel
     trees.
  4. Map the CVE's category to the appropriate property
     module.
  5. Run scan-per-file.sh and record whether CBMC fires a
     contract violation on the vulnerable function.

The output is a CSV plus a Markdown summary so we can read
the per-CVE detection result.

Limitations (documented honestly):

  * We don't reset the kernel tree to the pre-patch
    commit — we use the LTS release as-is.  If the LTS
    branch already backported the fix, we'll see SUCCESS
    where we expected FAILED; we record this case
    separately.
  * The function-extraction from the unified-diff hunk is
    heuristic (looks at the `@@ ... fn(` context line).
  * Several CVEs may be in subsystems (drivers, fs) that
    don't compile cleanly under our scan-per-file.sh due
    to missing headers; these get an `error` verdict.
  * The CVE-to-module mapping is per-category; a single
    CVE might map to a module that doesn't apply at the
    flagged file.  When that happens the verdict is
    `vacuous` (no contract clauses checked).
"""

from __future__ import annotations

import argparse
import csv
import dataclasses
import os
import random
import re
import subprocess
import sys
from pathlib import Path

CVE_CSV = Path("/tmp/cve-survey/cves_classified_v3.csv")
PATCH_CACHE = Path("/tmp/cve-survey/patch_cache")
VULNS_ROOT = Path("/tmp/cve-survey/vulns/cve/published")

REPO_ROOT = Path(__file__).resolve().parents[4]
SCAN_DIR = REPO_ROOT / "integration" / "linux" / "scan"
SCAN_PER_FILE = SCAN_DIR / "scan-per-file.sh"

# Pull CONTRACT_FUNCTIONS from scan.py so we don't duplicate
# the mapping (and so additions to scan.py automatically
# propagate here).
sys.path.insert(0, str(SCAN_DIR))
import scan  # noqa: E402
import synthesise_harness  # noqa: E402
_CONTRACT_FNS: dict[str, list[str]] = scan.CONTRACT_FUNCTIONS
_PERFILE_MODULES = set(synthesise_harness.MODULE_GHOST_BOOTSTRAP)


# Per-module API patterns.  When a CVE's patched function
# uses one of these APIs, the corresponding per-file-capable
# module is the right fit.
_MODULE_API_PATTERNS = [
    ("cred_lifetime",      r"\b(?:get_cred|put_cred|prepare_creds|"
                           r"prepare_kernel_cred|abort_creds|"
                           r"override_creds)\b"),
    ("kref_lifetime",      r"\bkref_(?:get|put|init)\b"),
    ("refcount_lifetime",  r"\brefcount_(?:dec|inc|set|read)\b"),
    ("kobject_lifetime",   r"\bkobject_(?:get|put|init|create_and_add)\b"),
    ("device_lifetime",    r"\b(?:get_device|put_device)\b"),
    ("of_node_lifetime",   r"\bof_node_(?:get|put)\b"),
    ("inode_lifetime",     r"\b(?:iput|ihold|igrab|new_inode)\b"),
    ("dentry_lifetime",    r"\b(?:dput|dget|d_alloc|d_alloc_anon)\b"),
    ("fput_lifetime",      r"\b(?:fput|fget|get_file)\b"),
    ("sock_lifetime",      r"\b(?:sock_hold|sock_put)\b"),
    ("skb_lifetime",       r"\b(?:skb_get|kfree_skb|consume_skb)\b"),
    ("module_lifetime",    r"\b(?:try_module_get|module_put|"
                           r"__module_get)\b"),
    ("netlink_attr_validation",
                           r"\bnla_(?:get|parse|put)\w*\b"),
    ("lock_state",         r"\b(?:mutex_lock|mutex_unlock|"
                           r"spin_lock|spin_unlock)\b"),
    ("pipe_buffer",        r"\bpipe_buf_(?:release|get)\b"),
]


def _pick_module(file_path: str, function: str,
                 kernel_tree: str | None) -> str | None:
    """Choose a per-file-capable module by scanning the
    function body for matching kernel-API calls."""
    if not kernel_tree:
        return None
    src = Path(kernel_tree) / file_path
    if not src.exists():
        return None
    try:
        text = src.read_text(errors="replace")
    except OSError:
        return None
    # Function-body extraction is the same heuristic as
    # triage_filter — find `function(` then match braces.
    sys.path.insert(0, str(SCAN_DIR))
    try:
        from triage_filter import _function_body  # type: ignore
        body = _function_body(text, function) or ""
    except Exception:
        body = ""
    if not body:
        # Fall back to scanning the whole file (still useful
        # signal even if the function-extraction misses).
        body = text
    # Score each module by the count of API matches in the
    # function body.
    scores = []
    for mod, pat in _MODULE_API_PATTERNS:
        if mod not in _PERFILE_MODULES:
            continue
        n = len(re.findall(pat, body))
        if n > 0:
            scores.append((n, mod))
    if not scores:
        return None
    # Highest-count wins.
    scores.sort(reverse=True)
    return scores[0][1]

# Category → (module, [contract function args]).  When a
# category maps to 'skip' the validation runner skips it.
CATEGORY_TO_MODULE = {
    "refcount_balance": ("refcount_lifetime", []),
    "use_after_free": ("use_after_free_generic", []),
    "resource_leak": ("module_lifetime", []),
    "out_of_bounds": ("netlink_attr_validation", []),
    "null_pointer_deref": ("null_after_alloc", []),
    "race_or_toctoue": ("concurrent_pointer_publish", []),
    "integer_overflow": ("integer_overflow_in_alloc_size", []),
    "string_or_copy_bound": ("copy_from_user_size_check", []),
    "format_string": ("format_string", []),
    "permission_check": ("permission_bypass", []),
    # Cleanup-ordering covered by cancel_*_before_free family.
    "cleanup_ordering": ("cancel_work_before_free", []),
    # Information leak: covered by uninit_to_user.
    "info_leak": ("uninit_to_user", []),
    # The catch-all 'other' has no mapping.
    "other": (None, []),
    # dos_panic_warn: most are divide-by-zero or warn-on
    # paths; map to division_by_zero_check.
    "dos_panic_warn": ("division_by_zero_check", []),
}


@dataclasses.dataclass
class CveCase:
    cve: str
    category: str
    summary: str
    file_path: str | None = None
    function: str | None = None
    module: str | None = None
    kernel_tree: str | None = None
    verdict: str = "?"   # "candidate", "fp-filtered", "successful",
                          # "vacuous", "skipped", "noise", "error"
    note: str = ""


def _parse_patch(cve: str) -> tuple[str | None, str | None]:
    """Return (file_path, function_name) for a CVE.

    Looks up the CVE JSON record under VULNS_ROOT, picks
    the first programFiles entry and the first 'lessThan'
    fix-commit hash, then loads the cached patch and
    extracts the function name from the first @@ hunk
    header.
    """
    import json
    # CVE JSON layout: vulns/cve/published/<year>/CVE-YYYY-NNNNN.json
    year = cve.split("-")[1] if "-" in cve else ""
    jpath = VULNS_ROOT / year / f"{cve}.json"
    if not jpath.exists():
        return (None, None)
    try:
        doc = json.loads(jpath.read_text(errors="replace"))
    except Exception:
        return (None, None)
    cna = doc.get("containers", {}).get("cna", {})
    affected = cna.get("affected", [])
    if not affected:
        return (None, None)
    # Get the first programFiles entry as our file_path.
    file_path = None
    for a in affected:
        prog = a.get("programFiles") or []
        if prog:
            file_path = prog[0]
            break
    # Get the first lessThan that looks like a hash.
    fix_hash = None
    for a in affected:
        for v in a.get("versions", []):
            lt = v.get("lessThan", "")
            if re.match(r"^[0-9a-f]{8,}$", lt):
                fix_hash = lt
                break
        if fix_hash:
            break
    if not fix_hash:
        return (file_path, None)
    cache_file = PATCH_CACHE / f"{fix_hash[:12]}.patch"
    if not cache_file.exists():
        return (file_path, None)
    try:
        text = cache_file.read_text(errors="replace")
    except OSError:
        return (file_path, None)
    if not text:
        return (file_path, None)
    # If file_path wasn't in the CVE JSON, take it from the
    # first diff --git header.
    if not file_path:
        fm = re.search(r"^diff --git a/(\S+) ", text, re.MULTILINE)
        if fm:
            file_path = fm.group(1)
    # Function from @@ hunk context.  Look for the FIRST hunk
    # in the modified file (not the entire patch's first
    # diff, which may be a header file).
    if file_path:
        # Find diff hunks for the matching file.
        # diff --git a/file +++ section.
        rest = text
        d_pat = re.compile(
            r"^diff --git a/(\S+) ", re.MULTILINE)
        sections = []
        for m in d_pat.finditer(rest):
            sections.append((m.start(), m.group(1)))
        sections.append((len(rest), ""))
        for i in range(len(sections) - 1):
            start, fn_in_diff = sections[i]
            end = sections[i+1][0]
            if fn_in_diff == file_path:
                section = rest[start:end]
                hm = re.search(
                    r"^@@ [-0-9,+ ]+@@\s+(?:static\s+|extern\s+|const\s+|inline\s+)*"
                    r"(?:struct\s+\S+\s+\*?|\S+\s+\*?)?"
                    r"(\w+)\s*\(",
                    section, re.MULTILINE,
                )
                if hm:
                    return (file_path, hm.group(1))
    # Fallback: first @@ in entire patch.
    hm = re.search(
        r"^@@ [-0-9,+ ]+@@\s+(?:static\s+|extern\s+|const\s+|inline\s+)*"
        r"(?:struct\s+\S+\s+\*?|\S+\s+\*?)?"
        r"(\w+)\s*\(",
        text, re.MULTILINE,
    )
    fn_name = hm.group(1) if hm else None
    return (file_path, fn_name)


def _find_file_in_tree(file_path: str,
                       kernel_trees: list[str]) -> str | None:
    """Return the kernel tree where file_path exists."""
    for kt in kernel_trees:
        if (Path(kt) / file_path).exists():
            return kt
    return None


def _run_scan(case: CveCase, timeout: int = 240) -> CveCase:
    """Run scan-per-file.sh on the case and update the
    verdict."""
    if (case.module is None or case.kernel_tree is None
            or case.file_path is None or case.function is None):
        case.verdict = "skipped"
        case.note = "missing module/tree/file/function mapping"
        return case
    cmd = [
        str(SCAN_PER_FILE),
        case.module,
        case.file_path,
        case.function,
        *_CONTRACT_FNS.get(case.module, []),
    ]
    env = os.environ.copy()
    env["LINUX_TREE"] = case.kernel_tree
    env["UNWIND"] = env.get("UNWIND", "2")
    try:
        r = subprocess.run(
            cmd, env=env, timeout=timeout,
            capture_output=True, text=True, check=False)
    except subprocess.TimeoutExpired:
        case.verdict = "timeout"
        case.note = f"timeout > {timeout}s"
        return case
    out = (r.stdout or "") + (r.stderr or "")
    rc = r.returncode
    # scan-per-file.sh exit codes (cf. scan.py docstring).
    if rc == 0:
        case.verdict = "successful"
        case.note = "contract holds (likely fix backported)"
    elif rc == 10:
        case.verdict = "candidate"
        # If the triage filter says fp, we'll downgrade.
        # Ad-hoc rerun via triage_filter import:
        try:
            sys.path.insert(0, str(SCAN_DIR))
            from triage_filter import classify
            v = classify(
                f"{case.kernel_tree}/{case.file_path}",
                case.function,
            )
            if v.shape:
                case.verdict = "fp-filtered"
                case.note = f"filtered: {v.shape} ({v.reason})"
            else:
                # Genuine candidate — likely matches the CVE.
                case.note = "contract violation reported"
        except Exception as e:
            case.note = f"contract violation; filter err: {e}"
    elif rc == 11:
        case.verdict = "noise"
        case.note = "only built-in CBMC checks fired"
    elif rc == 12:
        case.verdict = "vacuous"
        case.note = "no contract clauses checked"
    elif rc == 13:
        case.verdict = "skipped"
        case.note = "known-unverifiable shape"
    elif rc in (2, 3):
        case.verdict = "error"
        # Return the last informative error line.
        last = ""
        for ln in out.splitlines()[::-1]:
            if "error:" in ln or "FAIL:" in ln or "fatal" in ln.lower():
                last = ln.strip()
                break
        case.note = last[:200] or f"exit {rc}"
    else:
        case.verdict = "error"
        case.note = f"unexpected exit {rc}"
    return case


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=30,
                    help="number of CVEs to sample")
    ap.add_argument("--timeout", type=int, default=240)
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--out-csv", default="/tmp/cve-validate-results.csv")
    ap.add_argument("--out-md", default="/tmp/cve-validate-results.md")
    ap.add_argument("--kernel-trees", default=(
        "/home/ubuntu/linux_5_10,"
        "/home/ubuntu/linux_6_1,"
        "/home/ubuntu/linux_6_6,"
        "/home/ubuntu/linux_6_12"))
    args = ap.parse_args()

    kernel_trees = args.kernel_trees.split(",")
    rng = random.Random(args.seed)

    # Stratified sample: aim for 2-4 CVEs per category that
    # has a module mapping.
    rows_by_cat: dict[str, list[dict[str, str]]] = {}
    with open(CVE_CSV, newline="") as f:
        for r in csv.DictReader(f):
            cat = r.get("category", "")
            if cat not in CATEGORY_TO_MODULE:
                continue
            if CATEGORY_TO_MODULE[cat][0] is None:
                continue
            rows_by_cat.setdefault(cat, []).append(r)

    sampled: list[CveCase] = []
    target_per_cat = max(2, args.n // max(1, len(rows_by_cat)))
    print(f"target {target_per_cat} runnable per category, "
          f"out of {len(rows_by_cat)} categories")
    for cat, rs in rows_by_cat.items():
        rng.shuffle(rs)
        accepted = 0
        for r in rs:
            if accepted >= target_per_cat:
                break
            cve = r["cve"]
            fp, fn = _parse_patch(cve)
            if not fp or not fn:
                continue
            kt = _find_file_in_tree(fp, kernel_trees)
            if not kt:
                continue
            mod = _pick_module(fp, fn, kt)
            if not mod:
                continue
            sampled.append(CveCase(
                cve=cve, category=cat,
                summary=r.get("summary", "")[:120],
                file_path=fp, function=fn,
                module=mod, kernel_tree=kt,
            ))
            accepted += 1
    rng.shuffle(sampled)
    sampled = sampled[:args.n]
    cases = sampled
    runnable = list(cases)
    print(f"sample={len(cases)} runnable={len(runnable)}")

    # Run scans.
    for i, c in enumerate(runnable, 1):
        print(f"[{i}/{len(runnable)}] {c.cve} {c.category} "
              f"{c.module} {c.file_path}:{c.function} ...",
              flush=True)
        _run_scan(c, args.timeout)
        print(f"    verdict={c.verdict}  {c.note[:80]}")

    # Mark the un-runnable.
    for c in cases:
        if c.verdict == "?":
            if not c.file_path:
                c.verdict = "skipped"
                c.note = "patch not found in cache"
            elif not c.function:
                c.verdict = "skipped"
                c.note = "function name not extractable from patch"
            elif not c.kernel_tree:
                c.verdict = "skipped"
                c.note = (f"file not in any LTS tree: {c.file_path}")

    # Write CSV.
    with open(args.out_csv, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["cve", "category", "module", "kernel_tree",
                    "file_path", "function", "verdict", "note",
                    "summary"])
        for c in cases:
            w.writerow([c.cve, c.category, c.module or "",
                        c.kernel_tree or "", c.file_path or "",
                        c.function or "", c.verdict, c.note,
                        c.summary])
    print(f"\nResults CSV: {args.out_csv}")

    # Per-category summary.
    print("\n=== Per-category detection summary ===")
    by_cat: dict[str, dict[str, int]] = {}
    for c in cases:
        by_cat.setdefault(c.category, {}).setdefault(c.verdict, 0)
        by_cat[c.category][c.verdict] = by_cat[c.category].get(c.verdict, 0) + 1
    print(f"  {'category':<22} {'cand':>5} {'fp':>5} "
          f"{'succ':>5} {'vac':>5} {'noise':>5} "
          f"{'err':>5} {'skip':>5}")
    for cat in sorted(by_cat):
        d = by_cat[cat]
        print(f"  {cat:<22} {d.get('candidate',0):5d} "
              f"{d.get('fp-filtered',0):5d} "
              f"{d.get('successful',0):5d} "
              f"{d.get('vacuous',0):5d} "
              f"{d.get('noise',0):5d} "
              f"{d.get('error',0):5d} "
              f"{d.get('skipped',0)+d.get('timeout',0):5d}")

    # Markdown table.
    with open(args.out_md, "w") as f:
        f.write("# CVE validation results\n\n")
        f.write("| CVE | category | module | verdict | "
                "file:fn | note |\n")
        f.write("|-----|----------|--------|---------|"
                "----------|------|\n")
        for c in cases:
            f.write(f"| {c.cve} | {c.category} | "
                    f"{c.module or '—'} | {c.verdict} | "
                    f"`{c.file_path or '—'}:{c.function or '—'}` | "
                    f"{c.note} |\n")
    print(f"\nMarkdown table: {args.out_md}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
