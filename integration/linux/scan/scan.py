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
    result = subprocess.run(
        [spatch_bin(), "--sp-file", str(cocci_path), "--very-quiet",
         str(target)],
        capture_output=True, text=True,
    )
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


def run_cbmc_native(
    module: str,
    target: Path,
    tmp: Path,
) -> ModuleReport:
    """Run the CBMC stage on a file that uses property-module types
    directly (e.g. a CVE regression harness).  Links in all property
    modules, applies --replace-call-with-contract, runs cbmc."""
    goto_cc = tool("GOTOCC", "goto-cc")
    goto_instrument = tool("GI", "goto-instrument")
    cbmc = tool("CBMC", "cbmc")

    module_srcs = sorted(PROPERTIES_DIR.glob("*/[!t]*.c"))  # skip test_*.c
    gb = tmp / f"{target.stem}.gb"
    subprocess.run(
        [str(goto_cc), str(target), *map(str, module_srcs), "-o", str(gb)],
        check=True, capture_output=True, text=True,
    )

    contract_args: list[str] = []
    for fn in CONTRACT_FUNCTIONS.get(module, []):
        contract_args += ["--replace-call-with-contract", fn]

    if not contract_args:
        return ModuleReport(module=module, cbmc_status="not-run",
                            cbmc_notes="no contract functions declared")

    trans_gb = tmp / f"{target.stem}.trans.gb"
    subprocess.run(
        [str(goto_instrument), *contract_args, str(gb), str(trans_gb)],
        check=True, capture_output=True, text=True,
    )

    result = subprocess.run(
        [str(cbmc), str(trans_gb),
         "--unwind", "32", "--unwinding-assertions"],
        capture_output=True, text=True, timeout=180,
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

    return mr


def run_cbmc_adapter_pending(
    module: str,
) -> ModuleReport:
    """For real kernel source: the kernel adapter is M4b work, not yet
    in tree.  Report the status honestly."""
    return ModuleReport(
        module=module,
        cbmc_status="adapter-needed",
        cbmc_notes=(
            "Real kernel source has the module's setters inlined by GCC "
            "before goto-cc sees them; the CBMC stage requires a "
            "source-level adapter (-include) to intercept the setters "
            "via preprocessor macros.  This is milestone M4b; see "
            "integration/linux/scan/README.md."
        ),
    )


# ---------------------------------------------------------------------------
# Driver.
# ---------------------------------------------------------------------------

def scan_file(target: Path, tmp: Path) -> FileReport:
    report = FileReport(file=str(target))
    modules = discover_modules()
    for module, cocci in modules:
        hits = run_cocci(module, cocci, target)
        mr = ModuleReport(module=module, cocci_hits=hits)
        if hits:
            if file_uses_property_modules(target):
                try:
                    mr = run_cbmc_native(module, target, tmp)
                except subprocess.TimeoutExpired:
                    mr.cbmc_status = "timeout"
                except subprocess.CalledProcessError as e:
                    mr.cbmc_status = "error"
                    mr.cbmc_notes = f"{e.cmd[0]}: {e.returncode}"
                mr.cocci_hits = hits
            else:
                mr = run_cbmc_adapter_pending(module)
                mr.cocci_hits = hits
        report.modules.append(mr)
    return report


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


def main() -> int:
    ap = argparse.ArgumentParser(
        description="PR-scan driver for the CBMC Linux-kernel property modules"
    )
    ap.add_argument("files", nargs="+", type=Path,
                    help="kernel source paths (.c / .h) to scan")
    ap.add_argument("--json", type=Path,
                    help="write structured JSON report to this file")
    args = ap.parse_args()

    reports: list[FileReport] = []
    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = Path(tmpdir)
        for f in args.files:
            if not f.is_file():
                print(f"skip (not a file): {f}", file=sys.stderr)
                continue
            r = scan_file(f, tmp)
            reports.append(r)
            print_summary(r)

    if args.json:
        args.json.write_text(json.dumps(report_json(reports), indent=2))
        print(f"\nstructured report written to {args.json}")

    return 1 if any_cbmc_failure(reports) else 0


if __name__ == "__main__":
    sys.exit(main())
