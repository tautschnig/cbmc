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


def _pick_modules(file_path: str, function: str,
                  kernel_tree: str | None,
                  max_modules: int = 5) -> list[str]:
    """Choose ALL plausible per-file-capable modules for a
    function, ranked by API match count.  Returns up to
    max_modules ordered most-relevant-first."""
    if not kernel_tree:
        return []
    src = Path(kernel_tree) / file_path
    if not src.exists():
        return []
    try:
        text = src.read_text(errors="replace")
    except OSError:
        return []
    sys.path.insert(0, str(SCAN_DIR))
    try:
        from triage_filter import _function_body  # type: ignore
        body = _function_body(text, function) or ""
    except Exception:
        body = ""
    if not body:
        body = text
    scores = []
    for mod, pat in _MODULE_API_PATTERNS:
        if mod not in _PERFILE_MODULES:
            continue
        n = len(re.findall(pat, body))
        if n > 0:
            scores.append((n, mod))
    scores.sort(reverse=True)
    return [mod for _, mod in scores[:max_modules]]


def _pick_module(file_path: str, function: str,
                 kernel_tree: str | None) -> str | None:
    """Choose the single best per-file-capable module.
    Backwards-compatible thin wrapper around _pick_modules."""
    mods = _pick_modules(file_path, function, kernel_tree)
    return mods[0] if mods else None

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
    fix_hash: str | None = None
    verdict: str = "?"   # "candidate", "fp-filtered", "successful",
                          # "vacuous", "skipped", "noise", "error"
    note: str = ""
    # Set when running in --invert mode and the inverse-
    # patch application succeeded.  None otherwise.
    inverted_file: str | None = None


def _parse_patch(cve: str) -> tuple[str | None, str | None,
                                     str | None]:
    """Return (file_path, function_name, fix_hash) for a CVE.

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
        return (None, None, None)
    try:
        doc = json.loads(jpath.read_text(errors="replace"))
    except Exception:
        return (None, None, None)
    cna = doc.get("containers", {}).get("cna", {})
    affected = cna.get("affected", [])
    if not affected:
        return (None, None, None)
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
        return (file_path, None, None)
    cache_file = PATCH_CACHE / f"{fix_hash[:12]}.patch"
    if not cache_file.exists():
        return (file_path, None, fix_hash)
    try:
        text = cache_file.read_text(errors="replace")
    except OSError:
        return (file_path, None, fix_hash)
    if not text:
        return (file_path, None, fix_hash)
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
                    return (file_path, hm.group(1), fix_hash)
    # Fallback: first @@ in entire patch.
    hm = re.search(
        r"^@@ [-0-9,+ ]+@@\s+(?:static\s+|extern\s+|const\s+|inline\s+)*"
        r"(?:struct\s+\S+\s+\*?|\S+\s+\*?)?"
        r"(\w+)\s*\(",
        text, re.MULTILINE,
    )
    fn_name = hm.group(1) if hm else None
    return (file_path, fn_name, fix_hash)


def _find_files_in_trees(file_path: str,
                         kernel_trees: list[str]) -> list[str]:
    """Return ALL kernel trees where file_path exists,
    ordered as given."""
    return [kt for kt in kernel_trees
            if (Path(kt) / file_path).exists()]


def _find_file_in_tree(file_path: str,
                       kernel_trees: list[str]) -> str | None:
    """Return the FIRST kernel tree where file_path exists,
    or None.  Backwards-compatible single-tree lookup."""
    found = _find_files_in_trees(file_path, kernel_trees)
    return found[0] if found else None


def _find_vuln_tree(file_path: str, fix_hash: str | None,
                    kernel_trees: list[str]) -> tuple[str | None, str]:
    """Find the LTS tree where the file is in (or can be
    reverted to) pre-fix state.  Returns (tree, state) where
    state is one of:
       'already_vuln', 'reverted', 'divergent', 'no_patch'.

    Tries each tree in order; returns the first 'already_vuln'
    if any, then the first 'reverted' if any, else
    'divergent' / 'no_patch' from the last attempt."""
    if not fix_hash:
        return (kernel_trees[0] if kernel_trees else None, "no_patch")
    cache_file = PATCH_CACHE / f"{fix_hash[:12]}.patch"
    if not cache_file.exists():
        return (kernel_trees[0] if kernel_trees else None, "no_patch")
    try:
        patch_text = cache_file.read_text(errors="replace")
    except OSError:
        return (None, "no_patch")
    file_diff = _extract_file_diff(patch_text, file_path)
    if not file_diff:
        return (None, "no_patch")
    last_state = "no_patch"
    last_tree: str | None = None
    reverted_candidate: tuple[str, str] | None = None
    for kt in kernel_trees:
        src = Path(kt) / file_path
        if not src.exists():
            continue
        last_tree = kt
        # Mock CveCase to call _detect_state_and_apply.
        c = CveCase(cve="", category="", summary="",
                    file_path=file_path, function="",
                    module=None, kernel_tree=kt,
                    fix_hash=fix_hash)
        ok, state, _ = _detect_state_and_apply(c)
        if state == "already_vuln":
            return (kt, state)
        if state == "reverted" and reverted_candidate is None:
            reverted_candidate = (kt, state)
        last_state = state
    if reverted_candidate is not None:
        return reverted_candidate
    return (last_tree, last_state)


# ---------------------------------------------------------------------------
# Inverse-patch support
# ---------------------------------------------------------------------------

def _extract_file_diff(patch_text: str, file_path: str) -> str | None:
    """Return only the diff section for `file_path` from a
    multi-file patch.  Returns None if the file is not in the
    patch."""
    pat = re.compile(r"^diff --git a/(\S+) ", re.MULTILINE)
    sections = []
    for m in pat.finditer(patch_text):
        sections.append((m.start(), m.group(1)))
    sections.append((len(patch_text), ""))
    for i in range(len(sections) - 1):
        start, fn = sections[i]
        end = sections[i+1][0]
        if fn == file_path:
            return patch_text[start:end]
    return None


def _detect_state_and_apply(case: CveCase) -> tuple[bool, str, str]:
    """Determine the kernel file's state relative to the
    fix patch and produce a vuln-state (pre-fix) version.

    Returns (ok, state, msg) where state is one of:
       'already_vuln'     — file is pre-fix; no patching
                            needed.
       'reverted'         — file was post-fix; we reverted
                            it to pre-fix in case.inverted_file.
       'divergent'        — file diverges from both pre-fix
                            and post-fix; skip.
       'no_patch'         — no patch available.
    """
    import shutil
    import tempfile
    if not case.fix_hash or not case.file_path or not case.kernel_tree:
        return (False, "no_patch", "missing fix_hash / file / tree")
    cache_file = PATCH_CACHE / f"{case.fix_hash[:12]}.patch"
    if not cache_file.exists():
        return (False, "no_patch", f"patch cache miss: "
                                   f"{cache_file.name}")
    try:
        patch_text = cache_file.read_text(errors="replace")
    except OSError as e:
        return (False, "no_patch", f"unreadable patch: {e}")
    file_diff = _extract_file_diff(patch_text, case.file_path)
    if not file_diff:
        return (False, "no_patch", f"no diff for "
                                   f"{case.file_path}")
    src = Path(case.kernel_tree) / case.file_path
    tmp = Path(tempfile.mkdtemp(prefix="cve_invert_"))
    dest_dir = tmp / case.file_path
    dest_dir.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dest_dir)
    diff_file = tmp / "fix.patch"
    diff_file.write_text(file_diff)

    # Try `patch -R --dry-run` first to detect the state.
    dry = subprocess.run(
        ["patch", "-R", "--dry-run", "-p1",
         "--no-backup-if-mismatch", "-f", "-i", "fix.patch"],
        cwd=tmp, capture_output=True, text=True,
        timeout=60, check=False)
    if dry.returncode == 0:
        # File is post-fix; can revert to pre-fix.
        real = subprocess.run(
            ["patch", "-R", "-p1", "--no-backup-if-mismatch",
             "-f", "-i", "fix.patch"],
            cwd=tmp, capture_output=True, text=True,
            timeout=60, check=False)
        if real.returncode == 0:
            case.inverted_file = str(dest_dir)
            return (True, "reverted", "applied reverse patch")
        shutil.rmtree(tmp, ignore_errors=True)
        return (False, "divergent", f"reverse apply failed: "
                                    f"{real.stderr.strip()[:120]}")
    # Reverse-dry-run failed.  Check if forward-dry-run
    # succeeds — if so, the file is already pre-fix
    # (vulnerable).
    fwd = subprocess.run(
        ["patch", "--dry-run", "-p1", "--no-backup-if-mismatch",
         "-f", "-i", "fix.patch"],
        cwd=tmp, capture_output=True, text=True,
        timeout=60, check=False)
    shutil.rmtree(tmp, ignore_errors=True)
    if fwd.returncode == 0:
        # File is already pre-fix; the LTS tree's copy IS
        # the vulnerable version.  No reversion needed.
        return (True, "already_vuln",
                "file is already pre-fix (vulnerable)")
    return (False, "divergent",
            "file diverges from both pre-fix and post-fix "
            "states (likely backport variant)")


def _apply_inverse(case: CveCase) -> tuple[bool, str]:
    """Compatibility wrapper around _detect_state_and_apply.
    Returns (ok, message).  case.inverted_file is set only
    when state == 'reverted'."""
    ok, state, msg = _detect_state_and_apply(case)
    return (ok, f"{state}: {msg}")


def _restore_backup(backup: Path) -> None:
    """Restore the .pre-invert backup over the live file.

    Idempotent — does nothing if backup doesn't exist."""
    import shutil
    if not backup.exists():
        return
    live = backup.with_suffix("")
    # Drop the .pre-invert suffix to recover the original
    # file name.
    name = backup.name
    if name.endswith(".pre-invert"):
        live = backup.parent / name[:-len(".pre-invert")]
    try:
        shutil.move(str(backup), str(live))
    except OSError:
        pass


def _run_scan(case: CveCase, timeout: int = 240,
              invert: bool = False) -> CveCase:
    """Run scan-per-file.sh on the case and update the
    verdict.

    When invert=True, applies the CVE's fix patch in reverse
    to a temporary copy of the kernel file, swaps it into
    the kernel tree for the duration of the scan, and
    restores the original on the way out.  This converts a
    fix-direction kernel into a vuln-direction one for the
    one specific file."""
    if (case.module is None or case.kernel_tree is None
            or case.file_path is None or case.function is None):
        case.verdict = "skipped"
        case.note = "missing module/tree/file/function mapping"
        return case

    # If inverting, determine the file's state and (if
    # post-fix) apply the patch in reverse to a temp copy.
    backup = None
    state = "n/a"
    if invert:
        ok, state, msg = _detect_state_and_apply(case)
        if not ok:
            case.verdict = "skipped"
            case.note = f"{state}: {msg}"
            return case
        if state == "reverted":
            # Swap the inverted file into the kernel tree.
            live = Path(case.kernel_tree) / case.file_path
            backup = live.with_suffix(live.suffix + ".pre-invert")
            try:
                import shutil
                shutil.copy2(live, backup)
                shutil.copy2(case.inverted_file, live)
            except OSError as e:
                case.verdict = "error"
                case.note = f"swap failed: {e}"
                return case
        # else state == 'already_vuln' — file is already
        # in the desired state; no swap.
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
        if backup:
            _restore_backup(backup)
        return case
    finally:
        # Always restore the backup, even on success path
        # below (we'll do it after computing verdict so
        # subsequent triage_filter classify() also runs
        # against the inverted file).
        pass
    out = (r.stdout or "") + (r.stderr or "")
    rc = r.returncode
    state_tag = f" [state={state}]" if invert and state != "n/a" else ""
    # scan-per-file.sh exit codes (cf. scan.py docstring).
    if rc == 0:
        case.verdict = "successful"
        if invert and state == "already_vuln":
            case.note = (
                "contract holds even though file is "
                "vulnerable — catalog MISSED the bug"
                + state_tag)
        elif invert and state == "reverted":
            case.note = (
                "contract holds after reverting fix — "
                "catalog MISSED the bug" + state_tag)
        else:
            case.note = ("contract holds (likely fix "
                         "backported)" + state_tag)
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
                case.note = (f"filtered: {v.shape} "
                             f"({v.reason})" + state_tag)
            else:
                # Genuine candidate — likely matches the CVE.
                case.note = ("contract violation reported"
                             + state_tag)
        except Exception as e:
            case.note = (f"contract violation; filter err: "
                         f"{e}" + state_tag)
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
    if backup:
        _restore_backup(backup)
    return case


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=30,
                    help="number of CVEs to sample")
    ap.add_argument("--timeout", type=int, default=240)
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--invert", action="store_true",
                    help="apply the CVE's fix in reverse "
                         "before scanning (vuln direction)")
    ap.add_argument("--modules-per-cve", type=int, default=1,
                    help="try the top-N most-relevant "
                         "per-file modules per CVE (default 1)")
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
            fp, fn, fh = _parse_patch(cve)
            if not fp or not fn:
                continue
            # In invert mode, prefer the tree where the file
            # is already in vuln state (or can be reverted to
            # one).  In fix-direction mode, just pick the
            # first tree containing the file.
            if args.invert:
                kt, _ = _find_vuln_tree(fp, fh, kernel_trees)
            else:
                kt = _find_file_in_tree(fp, kernel_trees)
            if not kt:
                continue
            mods = _pick_modules(fp, fn, kt,
                                 max_modules=args.modules_per_cve)
            if not mods:
                continue
            for mod in mods:
                sampled.append(CveCase(
                    cve=cve, category=cat,
                    summary=r.get("summary", "")[:120],
                    file_path=fp, function=fn,
                    module=mod, kernel_tree=kt,
                    fix_hash=fh,
                ))
            accepted += 1
    rng.shuffle(sampled)
    sampled = sampled[:args.n * max(1, args.modules_per_cve)]
    cases = sampled
    runnable = list(cases)
    print(f"sample={len(cases)} runnable={len(runnable)}")

    # Run scans.
    for i, c in enumerate(runnable, 1):
        print(f"[{i}/{len(runnable)}] {c.cve} {c.category} "
              f"{c.module} {c.file_path}:{c.function} ...",
              flush=True)
        _run_scan(c, args.timeout, invert=args.invert)
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
