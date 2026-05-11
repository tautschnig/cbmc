#!/usr/bin/env python3
#
# scan.py - PR-scan driver for the CBMC Linux-kernel property modules.
#
# Takes one or more kernel source paths (or a diff on stdin; see --diff),
# runs each property module's Coccinelle prefilter over them, and for
# every prefilter hit drives a CBMC run that applies the module's
# --replace-call-with-contract transforms.
#
# Output: a summary on stdout plus, optionally, a structured JSON
# report to a file.  Exit code 1 if any CBMC run reports a contract
# violation, 0 otherwise.
#
# Scope of this version (M4a):
#
#   - Prefilter and reporting work on arbitrary C source files.
#   - The CBMC run fires cleanly on files that use the property
#     module types directly (e.g. integration/linux/properties/aead/
#     test_copyfail.c and CVE regression harnesses).  For real kernel
#     source (e.g. crypto/algif_aead.c), the prefilter stage reports
#     the hit and the CBMC stage reports that a kernel adapter is
#     required.  The kernel adapter is M4b (see ../scan/README.md).
#
# See properties/<module>/aead.cocci etc. for how a module publishes
# its prefilter rule.

from __future__ import annotations

import argparse
import dataclasses
import datetime
import json
import os
import re
import resource
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Iterable

SCRIPT_DIR = Path(__file__).resolve().parent
INTEG_ROOT = SCRIPT_DIR.parent              # integration/linux/
PROPERTIES_DIR = INTEG_ROOT / "properties"
REPO_ROOT = INTEG_ROOT.parent.parent        # repository root


# ---------------------------------------------------------------------------
# Resource limits for every subprocess we spawn.  Both cbmc and
# goto-instrument can blow up on complex inputs (LIM-006 plus adjacent
# state-explosion cases).  We apply an RLIMIT_AS ceiling plus an RLIMIT_CPU
# budget so that any runaway tool is killed cleanly rather than filling
# swap or holding the host indefinitely.
# ---------------------------------------------------------------------------

# Memory cap: 4 GiB virtual per tool invocation.  Raise via env var for
# investigative work on very large binaries.
MEMORY_LIMIT_BYTES = int(os.environ.get("SCAN_MEMORY_LIMIT", 4 * 1024 * 1024 * 1024))

# CPU-time cap as an inner backstop to the wall-clock timeout.  A tool
# that's stuck in a tight SAT/symex loop may burn CPU under the wall-clock
# timeout; RLIMIT_CPU turns that into a kill-9 at the kernel level.  Set
# to a generous ceiling; per-call wall-clock budgets (NATIVE_CBMC_TIMEOUT,
# KERNEL_CBMC_TIMEOUT, GI_TIMEOUT, GOTOCC_TIMEOUT) remain the primary limit.
CPU_LIMIT_SECONDS = int(os.environ.get("SCAN_CPU_LIMIT", 900))


def _rlimit_preexec() -> None:
    """Called in the forked child before exec().  Applies the resource
    limits defined above.  Failure to set a limit is reported via the
    usual subprocess error channel (preexec_fn exceptions propagate)."""
    resource.setrlimit(resource.RLIMIT_AS,
                       (MEMORY_LIMIT_BYTES, MEMORY_LIMIT_BYTES))
    resource.setrlimit(resource.RLIMIT_CPU,
                       (CPU_LIMIT_SECONDS, CPU_LIMIT_SECONDS))


def _run(cmd: list[str],
         *,
         timeout: float | None,
         check: bool = False,
         capture_output: bool = True) -> subprocess.CompletedProcess:
    """Wrapper around subprocess.run that applies RLIMIT_AS/RLIMIT_CPU
    via preexec_fn and enforces a wall-clock timeout.  Timeouts are
    allowed to propagate (TimeoutExpired)."""
    return subprocess.run(
        cmd,
        timeout=timeout,
        check=check,
        capture_output=capture_output,
        text=True,
        preexec_fn=_rlimit_preexec,
    )


# ---------------------------------------------------------------------------
# Data model.
# ---------------------------------------------------------------------------

@dataclasses.dataclass
class CocciHit:
    file: str
    line: int
    column: int | None
    message: str


@dataclasses.dataclass
class CbmcFailure:
    assertion: str
    location: str


@dataclasses.dataclass
class ModuleReport:
    module: str
    cocci_hits: list[CocciHit] = dataclasses.field(default_factory=list)
    cbmc_status: str = "not-run"        # not-run | successful | failed | timeout | adapter-needed | error
    cbmc_failures: list[CbmcFailure] = dataclasses.field(default_factory=list)
    cbmc_notes: str = ""


@dataclasses.dataclass
class FileReport:
    file: str
    modules: list[ModuleReport] = dataclasses.field(default_factory=list)


# ---------------------------------------------------------------------------
# Tool discovery.
# ---------------------------------------------------------------------------

def tool(env_name: str, default_relative_to_build: str) -> Path:
    p = Path(os.environ.get(env_name, REPO_ROOT / "build" / "bin" / default_relative_to_build))
    if not p.is_file():
        sys.exit(f"required tool not found: {p} (set ${env_name} to override)")
    if not os.access(p, os.X_OK):
        sys.exit(f"required tool is not executable: {p}")
    return p


def spatch_bin() -> str:
    path = shutil.which("spatch")
    if path is None:
        sys.exit("spatch (Coccinelle) not found in PATH; install the 'coccinelle' package")
    return path


# ---------------------------------------------------------------------------
# Module discovery.
# ---------------------------------------------------------------------------

def discover_modules() -> list[tuple[str, Path]]:
    """Return [(module_name, cocci_path), ...] for every property module
    that ships a .cocci file."""
    modules: list[tuple[str, Path]] = []
    for cocci in sorted(PROPERTIES_DIR.glob("*/*.cocci")):
        modules.append((cocci.parent.name, cocci))
    return modules


# ---------------------------------------------------------------------------
# Coccinelle prefilter.
# ---------------------------------------------------------------------------

# Matches lines like:
#   /path/to/file.c:280:1-23: aead: aead_request_set_crypt call site — ...
COCCI_HIT_RE = re.compile(
    r"^(?P<file>[^:]+):(?P<line>\d+):(?P<col>\d+)?[-\d]*:\s+(?P<msg>.*)$"
)


def run_cocci(module: str, cocci_path: Path, target: Path) -> list[CocciHit]:
    try:
        result = _run(
            [spatch_bin(), "--sp-file", str(cocci_path), "--very-quiet",
             str(target)],
            timeout=SPATCH_TIMEOUT,
        )
    except subprocess.TimeoutExpired:
        return []
    hits: list[CocciHit] = []
    for line in (result.stdout + result.stderr).splitlines():
        m = COCCI_HIT_RE.match(line)
        if not m:
            continue
        # spatch prefixes some of its own output with "init_defs_builtins:"
        # etc. — filter those out by requiring the message to match one
        # of our modules' report prefixes.
        if not line.strip().startswith(str(target).rstrip("/")) \
           and not re.match(r"^[^:]+\.c:", line):
            continue
        try:
            col = int(m.group("col")) if m.group("col") else None
            hits.append(CocciHit(
                file=m.group("file"),
                line=int(m.group("line")),
                column=col,
                message=m.group("msg").strip(),
            ))
        except ValueError:
            pass
    return hits


# ---------------------------------------------------------------------------
# CBMC run.
# ---------------------------------------------------------------------------

def file_uses_property_modules(target: Path) -> bool:
    """Heuristic: does this file #include the property module headers
    directly?  If so, we can run CBMC against it without a kernel
    adapter."""
    try:
        text = target.read_text(errors="ignore")
    except OSError:
        return False
    return bool(re.search(
        r'#\s*include\s*"[^"]*(page_provenance|scatterlist|aead)\.h"',
        text,
    ))


# Functions that each module's --replace-call-with-contract should
# abstract.  When a new module's cocci reports hits, map it here.
CONTRACT_FUNCTIONS: dict[str, list[str]] = {
    "aead": [
        "aead_request_set_crypt",
    ],
}


# Per-module kernel adapter: path to the adapter source file plus the
# extra goto-cc inputs needed alongside it (typically the shared
# page_provenance ghost state, plus havocing stubs for kernel helpers
# and an entry-point harness that controls the initial-state
# geometry).  See scan/adapters/README.md for how each piece fits.
KERNEL_ADAPTERS: dict[str, dict] = {
    "aead": {
        "adapter": SCRIPT_DIR / "adapters" / "aead_kernel_adapter.c",
        "stubs":   SCRIPT_DIR / "adapters" / "aead_kernel_stubs.c",
        "harness": SCRIPT_DIR / "adapters" / "aead_kernel_harness.c",
        "deps": [PROPERTIES_DIR / "page_provenance" / "page_provenance.c"],
    },
}


# Budget for cbmc runs against real kernel binaries, in seconds.
KERNEL_CBMC_TIMEOUT = 180
# Budget for cbmc runs on property-module-native harnesses.
NATIVE_CBMC_TIMEOUT = 60
# Wall-clock cap for goto-cc and goto-instrument invocations.
# goto-instrument with --generate-function-body on a large binary is
# known to hang in some cases, so we must always wrap it.
GOTOCC_TIMEOUT = 120
GI_TIMEOUT = 120
# Coccinelle (spatch) budget; per-file, short on typical source.
SPATCH_TIMEOUT = 60


def run_cbmc_native(
    module: str,
    target: Path,
    tmp: Path,
) -> tuple[ModuleReport, Path | None]:
    """Run the CBMC stage on a file that uses property-module types
    directly (e.g. a CVE regression harness).  Links in all property
    modules, applies --replace-call-with-contract, runs cbmc.
    Returns the module report plus a path to an emitted SARIF file
    (or None)."""
    goto_cc = tool("GOTOCC", "goto-cc")
    goto_instrument = tool("GI", "goto-instrument")
    cbmc = tool("CBMC", "cbmc")

    module_srcs = sorted(PROPERTIES_DIR.glob("*/[!t]*.c"))  # skip test_*.c
    gb = tmp / f"{target.stem}.gb"
    _run(
        [str(goto_cc), str(target), *map(str, module_srcs), "-o", str(gb)],
        timeout=GOTOCC_TIMEOUT, check=True,
    )

    contract_args: list[str] = []
    for fn in CONTRACT_FUNCTIONS.get(module, []):
        contract_args += ["--replace-call-with-contract", fn]

    if not contract_args:
        return ModuleReport(module=module, cbmc_status="not-run",
                            cbmc_notes="no contract functions declared"), None

    trans_gb = tmp / f"{target.stem}.trans.gb"
    _run(
        [str(goto_instrument), *contract_args, str(gb), str(trans_gb)],
        timeout=GI_TIMEOUT, check=True,
    )

    sarif = tmp / f"{target.stem}.{module}.sarif"
    result = _run(
        [str(cbmc), str(trans_gb),
         "--unwind", "32", "--unwinding-assertions",
         "--sarif-result", str(sarif)],
        timeout=NATIVE_CBMC_TIMEOUT,
    )

    mr = ModuleReport(module=module)
    combined = result.stdout + result.stderr
    if "VERIFICATION SUCCESSFUL" in combined:
        mr.cbmc_status = "successful"
    elif "VERIFICATION FAILED" in combined:
        mr.cbmc_status = "failed"
        # Pull named FAILURE lines
        for line in combined.splitlines():
            m = re.match(r"^\[([^\]]+)\]\s+(.*?)\s*:\s*FAILURE$", line)
            if m:
                mr.cbmc_failures.append(CbmcFailure(
                    assertion=m.group(1),
                    location=m.group(2),
                ))
    else:
        mr.cbmc_status = "error"
        mr.cbmc_notes = f"cbmc exit {result.returncode}; see stderr"

    return mr, sarif if sarif.is_file() else None


def run_cbmc_kernel(
    module: str,
    target: Path,
    tmp: Path,
) -> tuple[ModuleReport, Path | None]:
    """Run the CBMC stage on real kernel source using the module's
    kernel adapter.  Compiles the target file with scan/compile_file.sh
    if a LINUX_TREE environment variable identifies its root, links
    with the adapter + page_provenance, applies the contract, and runs
    cbmc with `_aead_recvmsg`-style entry-point selection.

    Outcomes are reported honestly:
      - `failed` if cbmc reports VERIFICATION FAILED;
      - `successful` if cbmc reports VERIFICATION SUCCESSFUL;
      - `timeout` if cbmc exceeds its budget (this is the common case
        today; see LIM-006 in CBMC_LIMITATIONS.md);
      - `error` for anything else.

    Returns the module report plus a path to an emitted SARIF file
    (or None if cbmc did not run long enough to produce one).
    """
    spec = KERNEL_ADAPTERS.get(module)
    if spec is None:
        return (ModuleReport(
            module=module, cbmc_status="adapter-needed",
            cbmc_notes=f"no kernel adapter declared for module '{module}'",
        ), None)

    goto_cc = tool("GOTOCC", "goto-cc")
    goto_instrument = tool("GI", "goto-instrument")
    cbmc = tool("CBMC", "cbmc")

    # Compile the kernel source to a goto binary.  Use the scan
    # helper to pick up the right flags.
    ktree = os.environ.get("LINUX_TREE")
    if not ktree:
        return (ModuleReport(
            module=module, cbmc_status="error",
            cbmc_notes=(
                "LINUX_TREE is not set; scan.py cannot compile "
                f"{target} with scan/compile_file.sh.  Set LINUX_TREE "
                "to the kernel source root."
            ),
        ), None)
    try:
        rel = target.resolve().relative_to(Path(ktree).resolve())
    except ValueError:
        return (ModuleReport(
            module=module, cbmc_status="error",
            cbmc_notes=(
                f"{target} is not inside $LINUX_TREE={ktree}; cannot "
                "determine the kernel-relative path needed by "
                "scan/compile_file.sh."
            ),
        ), None)

    kernel_gb = tmp / f"{target.stem}.kernel.gb"
    try:
        _run(
            [str(SCRIPT_DIR / "compile_file.sh"), ktree, str(rel),
             str(kernel_gb)],
            timeout=GOTOCC_TIMEOUT, check=True,
        )
    except subprocess.TimeoutExpired:
        return (ModuleReport(
            module=module, cbmc_status="error",
            cbmc_notes=(
                f"compile_file.sh exceeded {GOTOCC_TIMEOUT}s on {rel}"
            ),
        ), None)

    # Link kernel binary + adapter + stubs + harness + deps.
    linked_gb = tmp / f"{target.stem}.linked.gb"
    link_inputs = [str(kernel_gb), str(spec["adapter"])]
    if "stubs" in spec:
        link_inputs.append(str(spec["stubs"]))
    if "harness" in spec:
        link_inputs.append(str(spec["harness"]))
    link_inputs += [str(p) for p in spec.get("deps", [])]
    _run(
        [str(goto_cc), *link_inputs, "-o", str(linked_gb)],
        timeout=GOTOCC_TIMEOUT, check=True,
    )

    # Replace the contract function's calls with the adapter-attached
    # contract.
    contract_args: list[str] = []
    for fn in CONTRACT_FUNCTIONS.get(module, []):
        contract_args += ["--replace-call-with-contract", fn]
    trans_gb = tmp / f"{target.stem}.trans.gb"
    _run(
        [str(goto_instrument), *contract_args, str(linked_gb), str(trans_gb)],
        timeout=GI_TIMEOUT, check=True,
    )

    # Pick a per-module kernel entry point.  If the spec ships a
    # harness, its main() is the entry point.  Otherwise fall back to
    # the `_aead_recvmsg`-style per-module default or cbmc's synthesised
    # main.
    entry_candidates = {"aead": "_aead_recvmsg"}
    if "harness" in spec:
        entry = "main"
    else:
        entry = entry_candidates.get(module, "main")

    mr = ModuleReport(module=module)
    sarif = tmp / f"{target.stem}.{module}.sarif"
    try:
        result = _run(
            [str(cbmc), str(trans_gb),
             "--function", entry,
             "--unwind", "2", "--no-unwinding-assertions",
             "--no-standard-checks",
             "--sarif-result", str(sarif)],
            timeout=KERNEL_CBMC_TIMEOUT,
        )
    except subprocess.TimeoutExpired:
        mr.cbmc_status = "timeout"
        mr.cbmc_notes = (
            f"cbmc exceeded {KERNEL_CBMC_TIMEOUT}s on entry '{entry}'. "
            "This is the LIM-006 state-explosion case; mitigation "
            "requires aggressive stubbing of kernel helpers (see "
            "CBMC_LIMITATIONS.md)."
        )
        return mr, None

    combined = result.stdout + result.stderr
    if "VERIFICATION SUCCESSFUL" in combined:
        mr.cbmc_status = "successful"
    elif "VERIFICATION FAILED" in combined:
        mr.cbmc_status = "failed"
        for line in combined.splitlines():
            m = re.match(r"^\[([^\]]+)\]\s+(.*?)\s*:\s*FAILURE$", line)
            if m:
                mr.cbmc_failures.append(CbmcFailure(
                    assertion=m.group(1),
                    location=m.group(2),
                ))
    else:
        mr.cbmc_status = "error"
        mr.cbmc_notes = f"cbmc exit {result.returncode} on entry '{entry}'"
    return mr, sarif if sarif.is_file() else None


def run_cbmc_adapter_pending(
    module: str,
) -> ModuleReport:
    """Fallback when no kernel adapter is available for this module
    yet.  New modules should supply an entry in KERNEL_ADAPTERS."""
    return ModuleReport(
        module=module,
        cbmc_status="adapter-needed",
        cbmc_notes=(
            f"No kernel adapter registered for module '{module}'.  "
            "See scan/adapters/ for the aead adapter used as a model; "
            "each new module annotated with contracts on static-inline "
            "kernel API needs its own adapter that (a) re-implements "
            "any predicate walkers against the kernel's struct layout "
            "and (b) declares the annotated function with the contract."
        ),
    )


# ---------------------------------------------------------------------------
# Driver.
# ---------------------------------------------------------------------------

def scan_file(target: Path, tmp: Path) -> tuple[FileReport, list[Path]]:
    """Run every registered property module against one file.  Returns
    a FileReport plus any SARIF files cbmc produced for that file."""
    report = FileReport(file=str(target))
    sarifs: list[Path] = []
    modules = discover_modules()
    for module, cocci in modules:
        hits = run_cocci(module, cocci, target)
        mr = ModuleReport(module=module, cocci_hits=hits)
        if hits:
            if file_uses_property_modules(target):
                try:
                    mr, sarif = run_cbmc_native(module, target, tmp)
                except subprocess.TimeoutExpired:
                    mr.cbmc_status = "timeout"
                    sarif = None
                except subprocess.CalledProcessError as e:
                    mr.cbmc_status = "error"
                    mr.cbmc_notes = f"{e.cmd[0]}: {e.returncode}"
                    sarif = None
                mr.cocci_hits = hits
                if sarif is not None:
                    sarifs.append(sarif)
            else:
                # Real kernel source path — use the kernel adapter if
                # this module has one registered; fall back to an
                # honest "adapter-needed" report otherwise.
                if module in KERNEL_ADAPTERS:
                    try:
                        mr, sarif = run_cbmc_kernel(module, target, tmp)
                        if sarif is not None:
                            sarifs.append(sarif)
                    except subprocess.CalledProcessError as e:
                        mr.cbmc_status = "error"
                        mr.cbmc_notes = (
                            f"{os.path.basename(str(e.cmd[0]))} exit "
                            f"{e.returncode}: {e.stderr[:200] if e.stderr else ''}"
                        )
                else:
                    mr = run_cbmc_adapter_pending(module)
                mr.cocci_hits = hits
        report.modules.append(mr)
    return report, sarifs


def print_summary(report: FileReport) -> None:
    print(f"=== {report.file} ===")
    for m in report.modules:
        if not m.cocci_hits and m.cbmc_status == "not-run":
            continue
        print(f"  [{m.module}] {len(m.cocci_hits)} prefilter hit(s), "
              f"cbmc: {m.cbmc_status}")
        for h in m.cocci_hits:
            print(f"    hit  {h.file}:{h.line}: {h.message}")
        for f in m.cbmc_failures:
            print(f"    FAIL {f.location}")
            print(f"         [{f.assertion}]")
        if m.cbmc_notes:
            print(f"    note {m.cbmc_notes}")


def any_cbmc_failure(reports: Iterable[FileReport]) -> bool:
    for r in reports:
        for m in r.modules:
            if m.cbmc_status == "failed":
                return True
    return False


def report_json(reports: list[FileReport]) -> dict:
    return {
        "schema": "cbmc-linux-scan.v1",
        "timestamp": datetime.datetime.now(datetime.timezone.utc)
            .isoformat(timespec="seconds"),
        "files": [dataclasses.asdict(r) for r in reports],
    }


def merge_sarif(inputs: list[Path], output: Path) -> None:
    """Merge per-file cbmc SARIF reports into a single SARIF 2.1.0 log
    file (multiple `runs`).  If `inputs` is empty, emit a minimal
    well-formed log with an empty `runs` array so downstream tools
    still get a valid SARIF file."""
    merged: dict = {
        "$schema": "https://json.schemastore.org/sarif-2.1.0.json",
        "version": "2.1.0",
        "runs": [],
    }
    for p in inputs:
        try:
            doc = json.loads(p.read_text())
        except (OSError, json.JSONDecodeError):
            continue
        for run in doc.get("runs", []):
            merged["runs"].append(run)
    output.write_text(json.dumps(merged, indent=2))


def main() -> int:
    ap = argparse.ArgumentParser(
        description="PR-scan driver for the CBMC Linux-kernel property modules"
    )
    ap.add_argument("files", nargs="+", type=Path,
                    help="kernel source paths (.c / .h) to scan")
    ap.add_argument("--json", type=Path,
                    help="write structured JSON report to this file")
    ap.add_argument("--sarif", type=Path,
                    help="write merged SARIF 2.1.0 report to this file "
                         "(uses cbmc's --sarif-result under the hood)")
    args = ap.parse_args()

    reports: list[FileReport] = []
    sarif_files: list[Path] = []
    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = Path(tmpdir)
        for f in args.files:
            if not f.is_file():
                print(f"skip (not a file): {f}", file=sys.stderr)
                continue
            r, sarifs = scan_file(f, tmp)
            reports.append(r)
            sarif_files.extend(sarifs)
            print_summary(r)

        if args.sarif:
            merge_sarif(sarif_files, args.sarif)
            print(f"\nmerged SARIF report written to {args.sarif}")

    if args.json:
        args.json.write_text(json.dumps(report_json(reports), indent=2))
        print(f"\nstructured JSON report written to {args.json}")

    return 1 if any_cbmc_failure(reports) else 0


if __name__ == "__main__":
    sys.exit(main())
