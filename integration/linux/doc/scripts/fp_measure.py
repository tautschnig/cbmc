#!/usr/bin/env python3
"""fp_measure.py — measure the catalog's false-positive rate
on benign kernel code.

Samples random (file, function) pairs from a Linux kernel
tree, EXCLUDING any function that has ever been named in a
CVE patch.  For each sampled pair, picks the relevant
property modules using the same logic as `cve_validate.py`,
then runs the scan pipeline.  Reports the rate at which the
catalog reports `candidate` verdicts on benign code — an
upper bound on the false-positive rate (a "candidate" might
genuinely be a previously-undisclosed bug).

The output mirrors `cve_validate.py`'s CSV/Markdown format
so the same per-row-best aggregation logic applies.

Usage:
  fp_measure.py --kernel-tree /home/ubuntu/linux_5_10 \\
                --n 200 --timeout 600 \\
                --modules-per-fn 3 \\
                --cve-funcs /tmp/cve-survey/cve_funcs.pkl \\
                --out-csv /tmp/fp-measure/results.csv \\
                --out-md /tmp/fp-measure/results.md
"""

from __future__ import annotations

import argparse
import csv
import dataclasses
import os
import pickle
import random
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
SCRIPT_DIR = Path(__file__).resolve().parent
SCAN_DIR = ROOT / "integration" / "linux" / "scan"
sys.path.insert(0, str(SCRIPT_DIR))
sys.path.insert(0, str(SCAN_DIR))

import cve_validate  # type: ignore
import synthesise_harness  # type: ignore
import scan_cache  # type: ignore


# Increase this when scanner internals change in a way that
# invalidates cached results (cocci rules, contracts,
# adapters, harness synth).
SCANNER_VERSION = "1"


# Function-definition regex: matches `<type-words> name(`
# at the start of a line.  Conservative — skips static
# inlines on multi-line declarations and macro-driven
# function-like definitions.  Good enough for sampling.
#
# We require the line to START with a non-whitespace character
# (so indented call-statements like `\t\tmemcpy(...)` don't
# match) AND the type prefix to contain at least one
# alphanumeric token before the function name.
_FN_DEF_RE = re.compile(
    r"^(?:static\s+)?(?:inline\s+)?(?:noinline\s+)?"
    r"(?:const\s+|volatile\s+|unsigned\s+|signed\s+|"
    r"struct\s+|enum\s+|union\s+|extern\s+)*"
    r"[a-zA-Z_]\w*[\s\*]+"
    r"(?P<name>[a-z_][\w]*)\s*\("
)

# Skip lines that look like macros or other false matches.
_SKIP_LINE_RE = re.compile(
    r"^(?:#|/\*|//|EXPORT_SYMBOL|MODULE_|TRACE_EVENT|"
    r"DECLARE_|DEFINE_|DEVICE_ATTR|SYSCALL_DEFINE|"
    r"BUFFER_FNS|TAS_BUFFER_FNS)"
)


@dataclasses.dataclass
class FpCase:
    file_path: str
    function: str
    module: str = ""
    kernel_tree: str = ""
    verdict: str = "?"
    note: str = ""
    runtime_s: float = 0.0


def _enum_functions(file_path: Path
                    ) -> list[str]:
    """Heuristically extract function names from the file
    by scanning for definition headers.  Returns names in
    order of appearance, deduplicated."""
    try:
        text = file_path.read_text(errors="replace")
    except OSError:
        return []
    names: list[str] = []
    seen: set[str] = set()
    in_block_comment = False
    for line in text.splitlines():
        if in_block_comment:
            if "*/" in line:
                in_block_comment = False
            continue
        if "/*" in line and "*/" not in line:
            in_block_comment = True
            continue
        if _SKIP_LINE_RE.match(line):
            continue
        m = _FN_DEF_RE.match(line)
        if not m:
            continue
        name = m.group("name")
        # Skip C keywords / common non-function names.
        if name in ("if", "while", "for", "switch", "return",
                    "sizeof", "alignof", "typeof", "case",
                    "default"):
            continue
        if name not in seen:
            seen.add(name)
            names.append(name)
    return names


def _enum_files(tree: Path,
                subdirs: tuple[str, ...] | None = None
                ) -> list[Path]:
    """Walk a kernel tree, return all .c files (excluding
    samples, tools, scripts, Documentation)."""
    files: list[Path] = []
    skip_top = {"Documentation", "samples", "tools",
                "scripts", "LICENSES", "include"}
    # Architecture filter: we compile for x86_64.  Files under
    # arch/<other> won't link against our scan-compat / include
    # path setup and produce compile rc=3.  Restrict arch/ to
    # x86 + cross-arch shared (the latter live outside arch/).
    skip_arch = {
        "alpha", "arc", "arm", "arm64", "csky", "h8300",
        "hexagon", "ia64", "loongarch", "m68k", "microblaze",
        "mips", "nds32", "nios2", "openrisc", "parisc",
        "powerpc", "riscv", "s390", "sh", "sparc", "um",
        "xtensa",
    }
    if subdirs is None:
        roots = [d for d in tree.iterdir()
                 if d.is_dir() and d.name not in skip_top]
    else:
        roots = [tree / s for s in subdirs]
    for root in roots:
        if not root.is_dir():
            continue
        for f in root.rglob("*.c"):
            # Skip generated files and tests.
            if any(part in f.parts for part in
                   ("test", "selftests", "kunit",
                    "generated")):
                continue
            # Skip non-x86 architecture files.
            parts = f.parts
            if "arch" in parts:
                idx = parts.index("arch")
                if idx + 1 < len(parts):
                    if parts[idx + 1] in skip_arch:
                        continue
            files.append(f)
    return files


def _sample_pairs(tree: Path,
                  cve_funcs: set[tuple[str, str]],
                  target_n: int,
                  rng: random.Random
                  ) -> list[FpCase]:
    """Sample target_n (file, function) pairs uniformly,
    excluding CVE'd pairs and functions where module-pick
    yields no module."""
    files = _enum_files(tree)
    rng.shuffle(files)
    # Collect a large pool — module-pick filters most of
    # them, so we need plenty of headroom.  Cap at 50x to
    # bound runtime on huge trees.
    pool_cap = max(target_n * 50, 5000)
    out: list[FpCase] = []
    seen_pairs: set[tuple[str, str]] = set()
    for f in files:
        if len(out) >= pool_cap:
            break
        rel = str(f.relative_to(tree))
        names = _enum_functions(f)
        for n in names:
            key = (rel, n)
            if key in cve_funcs or key in seen_pairs:
                continue
            seen_pairs.add(key)
            out.append(FpCase(file_path=rel, function=n,
                              kernel_tree=str(tree)))
    rng.shuffle(out)
    return out


def _run_scan(c: FpCase, modules: list[str],
              timeout_s: int, kernel_tree: str,
              cache: scan_cache.ScanCache | None = None
              ) -> None:
    """Run scan-per-file once per module, record best
    verdict on the case.  When `cache` is provided and
    enabled, look up each (file_hash, function, module,
    instrument) tuple before invoking the subprocess and
    short-circuit on hit."""
    BEST_ORDER = ["candidate", "fp-filtered", "noise",
                  "low-confidence-candidate",
                  "successful", "vacuous", "timeout",
                  "error", "skipped"]
    rank = {v: i for i, v in enumerate(BEST_ORDER)}

    best_verdict = "skipped"
    best_module = ""
    best_note = "no module picked"
    t0 = time.time()
    for mod in modules:
        cfg = synthesise_harness.MODULE_GHOST_BOOTSTRAP.get(
            mod, {})
        env = os.environ.copy()
        env["LINUX_TREE"] = kernel_tree
        if (cfg.get("uses_cocci_instrumentation")
                and not env.get("INSTRUMENT")):
            env["INSTRUMENT"] = mod
        env["UNWIND"] = env.get("UNWIND", "2")
        instr_key = env.get("INSTRUMENT")
        cmd = [
            str(SCAN_DIR / "scan-per-file.sh"),
            mod, c.file_path, c.function,
        ]
        # Cache lookup short-circuits the subprocess call
        # when the (file_hash, function, module, instrument)
        # tuple was previously scanned with the same scanner
        # version.
        rc: int = -1
        stdout = ""
        stderr = ""
        runtime_s_one = 0.0
        cached = (cache.get(kernel_tree, c.file_path,
                            c.function, mod, instr_key)
                  if cache and cache.enabled else None)
        if cached is not None:
            rc = cached.rc
            stdout = cached.stdout
            stderr = cached.stderr
            runtime_s_one = cached.runtime_s
        else:
            sub_t0 = time.time()
            try:
                r = subprocess.run(
                    cmd, env=env, timeout=timeout_s,
                    cwd=str(ROOT),
                    capture_output=True, text=True,
                )
            except subprocess.TimeoutExpired:
                rc = 124
                runtime_s_one = float(timeout_s)
            else:
                rc = r.returncode
                stdout = r.stdout or ""
                stderr = r.stderr or ""
                runtime_s_one = time.time() - sub_t0
                if cache and cache.enabled:
                    cache.put(kernel_tree, c.file_path,
                              c.function, mod, instr_key,
                              rc, stdout, stderr,
                              runtime_s_one)
        # Exit-code → verdict mapping (same as before; see
        # scan-per-file.sh for code definitions).
        if rc == 124:
            v = "timeout"
            note = f"module={mod} timeout"
        elif rc == 10:
            v = "candidate"
            note = f"module={mod} contract violation"
            try:
                sys.path.insert(0, str(SCAN_DIR))
                from triage_filter import classify  # type: ignore
                tv = classify(
                    f"{c.kernel_tree}/{c.file_path}",
                    c.function,
                    module=mod,
                )
                if tv.shape:
                    v = "fp-filtered"
                    note = (f"module={mod} filtered: "
                            f"{tv.shape} ({tv.reason})")
            except Exception:
                pass
        elif rc == 14:
            v = "low-confidence-candidate"
            note = (f"module={mod} contract violation "
                    f"with empty-ghost-bootstrap")
            # Also try the triage filter on the
            # low-confidence path: an empty-ghost-bootstrap
            # candidate that matches a known FP shape
            # (caller_holds_lock, ownership_handler,
            # alloc_handed_to_consumer, etc.) is downgrade-
            # able to fp-filtered.
            try:
                sys.path.insert(0, str(SCAN_DIR))
                from triage_filter import classify  # type: ignore
                tv = classify(
                    f"{c.kernel_tree}/{c.file_path}",
                    c.function,
                    module=mod,
                )
                if tv.shape:
                    v = "fp-filtered"
                    note = (f"module={mod} filtered: "
                            f"{tv.shape} ({tv.reason}) "
                            f"[empty-ghost-bootstrap]")
            except Exception:
                pass
        elif rc == 0:
            v = "successful"
            note = f"module={mod} clean"
        elif rc == 11:
            v = "noise"
            note = f"module={mod} builtin failure"
        elif rc == 12:
            v = "vacuous"
            note = f"module={mod} no contract clauses"
        elif rc == 13:
            v = "skipped"
            note = f"module={mod} skipped"
        elif rc == 3 or rc == 2:
            v = "error"
            note = f"module={mod} compile rc={rc}"
        else:
            v = "error"
            note = f"module={mod} rc={rc}"
        if rank.get(v, 99) < rank.get(best_verdict, 99):
            best_verdict = v
            best_module = mod
            best_note = note
    c.verdict = best_verdict
    c.module = best_module
    c.note = best_note
    c.runtime_s = time.time() - t0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--kernel-tree", type=Path, required=True)
    ap.add_argument("--n", type=int, default=200)
    ap.add_argument("--timeout", type=int, default=600,
                    help="per-(case, module) timeout in seconds")
    ap.add_argument("--modules-per-fn", type=int, default=3)
    ap.add_argument("--cve-funcs", type=Path, required=True,
                    help="pickle of set of (file, function) "
                         "CVE'd pairs to exclude")
    ap.add_argument("--out-csv", type=Path, required=True)
    ap.add_argument("--out-md", type=Path, required=True)
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument(
        "--cache-dir", type=Path, default=None,
        help="if set, cache per-(file_hash, function, "
             "module, instrument) scan results under this "
             "directory.  Re-runs become near-free for "
             "unchanged cases.  Use SCAN_CACHE_DIR env var "
             "as an alternative.")
    args = ap.parse_args(argv)

    cache_dir = args.cache_dir or (
        Path(os.environ["SCAN_CACHE_DIR"])
        if os.environ.get("SCAN_CACHE_DIR") else None)
    cache = scan_cache.ScanCache(
        cache_dir, scanner_version=SCANNER_VERSION)
    if cache.enabled:
        print(f"scan cache: {cache_dir} "
              f"(scanner_version={SCANNER_VERSION})",
              flush=True)

    args.out_csv.parent.mkdir(parents=True, exist_ok=True)
    cve_funcs = pickle.loads(args.cve_funcs.read_bytes())
    print(f"excluding {len(cve_funcs)} CVE'd (file, function) "
          f"pairs", flush=True)

    rng = random.Random(args.seed)
    print(f"enumerating files in {args.kernel_tree} ...",
          flush=True)
    pool = _sample_pairs(args.kernel_tree, cve_funcs,
                         args.n, rng)
    print(f"candidate pool: {len(pool)} (file, function) "
          f"pairs", flush=True)

    # Pick modules per case, mirroring cve_validate.
    cases: list[FpCase] = []
    for c in pool:
        mods = cve_validate._pick_modules(
            c.file_path, c.function,
            str(args.kernel_tree),
            max_modules=args.modules_per_fn)
        if not mods:
            continue
        c2 = FpCase(file_path=c.file_path, function=c.function,
                    kernel_tree=c.kernel_tree)
        c2.module = ",".join(mods)  # store all picks
        cases.append(c2)
        if len(cases) >= args.n:
            break
    print(f"runnable: {len(cases)}", flush=True)

    for i, c in enumerate(cases, 1):
        mods = c.module.split(",")
        print(f"[{i}/{len(cases)}] {c.file_path}:{c.function} "
              f"mods={mods}", flush=True)
        _run_scan(c, mods, args.timeout, c.kernel_tree, cache)
        print(f"    verdict={c.verdict} ({c.runtime_s:.1f}s) "
              f"{c.note[:80]}", flush=True)

    with open(args.out_csv, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["file_path", "function", "module",
                    "verdict", "runtime_s", "note"])
        for c in cases:
            w.writerow([c.file_path, c.function, c.module,
                        c.verdict, f"{c.runtime_s:.1f}",
                        c.note])
    print(f"\nResults CSV: {args.out_csv}")

    # Summary.
    import collections
    counts = collections.Counter(c.verdict for c in cases)
    total = len(cases)
    print(f"\n=== n={total} per-function-best verdicts ===")
    for v in ["candidate", "fp-filtered", "noise",
              "low-confidence-candidate",
              "successful", "vacuous", "timeout",
              "error", "skipped"]:
        n = counts.get(v, 0)
        pct = 100.0 * n / max(1, total)
        print(f"  {v:25s} {n:4d}  ({pct:.1f}%)")
    cand = counts.get("candidate", 0)
    print(f"\n  Upper-bound FP rate: {cand}/{total} = "
          f"{100.0 * cand / max(1, total):.1f}%")
    print("  (FP because these functions are NOT in the "
          "CVE list; some may genuinely be undisclosed bugs.)")

    # Write MD.
    with open(args.out_md, "w") as f:
        f.write("# FP measurement results\n\n")
        f.write("| file:function | module | verdict | "
                "runtime | note |\n")
        f.write("|---|---|---|---:|---|\n")
        for c in cases:
            f.write(f"| `{c.file_path}:{c.function}` | "
                    f"{c.module} | {c.verdict} | "
                    f"{c.runtime_s:.1f}s | {c.note} |\n")
    print(f"Markdown table: {args.out_md}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
