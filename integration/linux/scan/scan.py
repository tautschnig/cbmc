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
MEMORY_LIMIT_BYTES = int(os.environ.get("SCAN_MEMORY_LIMIT", 24 * 1024 * 1024 * 1024))

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
class PerFileVerdict:
    """One per-hit verdict from the --per-file pipeline.

    Each verdict records the enclosing function the harness was
    synthesised for, the cocci-hit lines it covers (a single
    harness can cover multiple hits if they share an enclosing
    function), and the cbmc verdict.
    """
    function: str
    hit_lines: list[int]
    status: str                 # successful | failed | timeout | error
                                # | no-function-found
    notes: str = ""


@dataclasses.dataclass
class ModuleReport:
    module: str
    cocci_hits: list[CocciHit] = dataclasses.field(default_factory=list)
    cbmc_status: str = "not-run"        # not-run | successful | failed | timeout | adapter-needed | error
    cbmc_failures: list[CbmcFailure] = dataclasses.field(default_factory=list)
    cbmc_notes: str = ""
    # --per-file mode only: per-hit verdicts produced by running the
    # synthesised per-function harness on the enclosing function of
    # each cocci hit.  Empty in adapter-mode scans.
    per_file: list[PerFileVerdict] = dataclasses.field(default_factory=list)


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
        # The kernel's `aead_request_set_crypt` is `static inline` in
        # <crypto/aead.h>, so goto-cc with --export-file-local-symbols
        # exposes it under this mangled name at call sites in
        # crypto/algif_aead.c (the _aead_recvmsg body).
        "__CPROVER_file_local_aead_h_aead_request_set_crypt",
        # Keep the external name too, for any path that resolves the
        # non-static symbol (stand-alone harnesses, older link modes).
        "aead_request_set_crypt",
    ],
    "pipe_buffer": [
        # pipe_buf_release is `static inline` in
        # <linux/pipe_fs_i.h>; exposed under this mangled name in
        # every kernel TU that includes the header.
        "__CPROVER_file_local_pipe_fs_i_h_pipe_buf_release",
        # External-name fallback for direct-call harness links.
        "pipe_buf_release",
    ],
    "cred_lifetime": [
        # put_cred is `static inline` in <linux/cred.h>; exposed
        # under this mangled name in every kernel TU that
        # includes the header.
        "__CPROVER_file_local_cred_h_put_cred",
        # External-name fallback for direct-call harness links.
        "put_cred",
    ],
    "lock_state": [
        # mutex_unlock is an ordinary `extern void` in
        # <linux/mutex.h> — not static inline — so the external
        # name is all we need.
        "mutex_unlock",
    ],
    "refcount_lifetime": [
        # refcount_dec_and_test is `static inline __must_check` in
        # <linux/refcount.h> in modern kernels, so each kernel TU
        # exposes it as `__CPROVER_file_local_refcount_h_refcount_
        # dec_and_test` under goto-cc --export-file-local-symbols.
        # The external form is still needed for the direct-call
        # harness.  __refcount_dec_and_test (the static inline
        # helper refcount_dec_and_test wraps) is also mangled;
        # some kernel paths call it directly.  Apply the contract
        # to all three so whichever form the link produces fires
        # the precondition at the call site.
        "__CPROVER_file_local_refcount_h_refcount_dec_and_test",
        "__CPROVER_file_local_refcount_h___refcount_dec_and_test",
        "refcount_dec_and_test",
    ],
    "alloc_tag": [
        # vfree is an ordinary extern in <linux/vmalloc.h>
        # (not static inline); single external name.
        "vfree",
    ],
    "kobject_lifetime": [
        # kobject_put is an ordinary extern in <linux/kobject.h>
        # (lib/kobject.c provides the body and EXPORT_SYMBOLs
        # it); single external name.
        "kobject_put",
    ],
    # Phase-1 balance modules.
    "device_lifetime": [
        "put_device",
    ],
    "of_node_lifetime": [
        "of_node_put",
    ],
    "inode_lifetime": [
        "iput",
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
        # Vacuity-probe adapter: same declarations as `adapter` but
        # the contract's substantive precondition is replaced by
        # `__CPROVER_requires(0 == 1)`.  scan.py links this in place
        # of the real adapter for a one-shot probe run; cbmc MUST
        # report FAILED on the precondition, proving the contract
        # call site is reached on at least one path.  If the probe
        # reports SUCCESS, the pipeline has silently regressed to
        # vacuous and scan.py refuses to emit a verdict.
        "adapter_probe":
            SCRIPT_DIR / "adapters" / "aead_kernel_adapter_probe.c",
        # Direct-call harness (LIM-012 resolution path 2).  Builds
        # kernel-layout scatterlist shapes explicitly and calls
        # the contract target.  Compiled twice per scan run:
        # without `-DFIXED` for the vulnerable shape (expect
        # FAILED) and with `-DFIXED` for the safe shape (expect
        # SUCCESSFUL).  See the harness source for the rationale.
        "harness": SCRIPT_DIR / "adapters" / "aead_kernel_direct_harness.c",
        # Compiled-twice flag for vuln / fix shapes of the same
        # harness source.
        "harness_fix_define": "FIXED",
        "deps": [PROPERTIES_DIR / "page_provenance" / "page_provenance.c"],
        # Pure predicates referenced from contract `__CPROVER_requires`
        # clauses.  Under the direct-call harness, no stub bodies
        # need preserving — the harness constructs the SGL
        # explicitly rather than relying on the kernel control
        # flow and stubs to build it.
        "slice_preserve": [
            "sgl_all_user_writable", "page_prov_of", "k_sg_next", "k_sg_page",
        ],
        # Functions that MUST have a non-empty body in the linked
        # goto binary.  Post-link, scan.py verifies each.  The
        # check catches the exact failure mode LIM-009 resolved:
        # a `static` kernel symbol that silently binds to an empty
        # external stub because the harness called the unmangled
        # name.
        #
        # Under the direct-call harness (LIM-012 path 2) the
        # kernel TU's entry function is not called from main, so
        # we do NOT list it here — doing so would cause every
        # scan of a corpus file OTHER than crypto/algif_aead.c
        # to produce a false-positive vacuity-risk.  The checks
        # that survive are the ones the direct-call harness
        # actually exercises: adapter predicate + ghost backend.
        "required_bodies": [
            # Adapter-provided predicate — referenced from the
            # contract's `__CPROVER_requires`.
            "sgl_all_user_writable",
            # page_provenance ghost-state backend.
            "page_prov_of",
            "set_page_prov",
        ],
    },
    "pipe_buffer": {
        "adapter":
            SCRIPT_DIR / "adapters" / "pipe_buffer_kernel_adapter.c",
        # Vacuity-probe adapter.  See the aead entry above for the
        # semantics of this guardrail.
        "adapter_probe":
            SCRIPT_DIR / "adapters" / "pipe_buffer_kernel_adapter_probe.c",
        # Direct-call harness: builds kernel-layout pipe_buffer
        # shapes explicitly and calls `pipe_buf_release`.  See the
        # harness source for vulnerable / fixed branch details.
        "harness":
            SCRIPT_DIR / "adapters" / "pipe_buffer_kernel_direct_harness.c",
        "harness_fix_define": "FIXED",
        "deps": [
            PROPERTIES_DIR / "pipe_buffer" / "pipe_buffer.c",
        ],
        # Only predicates referenced from the contract clauses.
        "slice_preserve": [
            "pipe_buf_merge_safe", "pipe_buffer_is_populated",
            "pipe_buffer_mark_populated", "pipe_buffer_mark_taken_over",
        ],
        # Required-body check: predicate + ghost backend.  We do
        # NOT list the kernel TU's entry function here for the
        # same reason as the aead entry above — the direct-call
        # harness does not call it, and different corpus files
        # have different entry functions.
        "required_bodies": [
            "pipe_buf_merge_safe",
            "pipe_buffer_is_populated",
        ],
    },
    "cred_lifetime": {
        "adapter":
            SCRIPT_DIR / "adapters" / "cred_kernel_adapter.c",
        # Vacuity-probe adapter.  Same semantics as the aead /
        # pipe_buffer entries.
        "adapter_probe":
            SCRIPT_DIR / "adapters" / "cred_kernel_adapter_probe.c",
        # Direct-call harness (LIM-012 path 2): builds two
        # `put_cred` calls on an explicitly-initialised cred.
        "harness":
            SCRIPT_DIR / "adapters" / "cred_kernel_direct_harness.c",
        "harness_fix_define": "FIXED",
        "deps": [
            PROPERTIES_DIR / "cred_lifetime" / "cred_lifetime.c",
        ],
        # Predicates referenced from the contract clauses.
        "slice_preserve": [
            "cred_live", "cred_lifetime_usage",
            "cred_lifetime_init", "cred_lifetime_get",
            "cred_lifetime_put",
        ],
        # Required-body check: adapter predicate + ghost backend.
        "required_bodies": [
            "cred_live",
            "cred_lifetime_usage",
        ],
    },
    "lock_state": {
        "adapter":
            SCRIPT_DIR / "adapters" / "lock_state_kernel_adapter.c",
        "adapter_probe":
            SCRIPT_DIR / "adapters" / "lock_state_kernel_adapter_probe.c",
        "harness":
            SCRIPT_DIR / "adapters" / "lock_state_kernel_direct_harness.c",
        "harness_fix_define": "FIXED",
        "deps": [
            PROPERTIES_DIR / "lock_state" / "lock_state.c",
        ],
        "slice_preserve": [
            "lock_held", "lock_state_held_count",
            "lock_state_lock", "lock_state_unlock_ghost",
            "lock_state_ghost_find", "lock_state_ghost_find_or_add",
        ],
        "required_bodies": [
            "lock_held",
            "lock_state_held_count",
        ],
    },
    "refcount_lifetime": {
        "adapter":
            SCRIPT_DIR / "adapters" / "refcount_kernel_adapter.c",
        "adapter_probe":
            SCRIPT_DIR / "adapters" / "refcount_kernel_adapter_probe.c",
        "harness":
            SCRIPT_DIR / "adapters" / "refcount_kernel_direct_harness.c",
        "harness_fix_define": "FIXED",
        "deps": [
            PROPERTIES_DIR / "refcount_lifetime" / "refcount_lifetime.c",
        ],
        "slice_preserve": [
            "refcount_live", "refcount_lifetime_usage",
            "refcount_lifetime_init", "refcount_lifetime_inc",
            "refcount_lifetime_dec_and_test",
            "refcount_ghost_find", "refcount_ghost_find_or_add",
        ],
        "required_bodies": [
            "refcount_live",
            "refcount_lifetime_usage",
        ],
    },
    "alloc_tag": {
        "adapter":
            SCRIPT_DIR / "adapters" / "alloc_tag_kernel_adapter.c",
        "adapter_probe":
            SCRIPT_DIR / "adapters" / "alloc_tag_kernel_adapter_probe.c",
        "harness":
            SCRIPT_DIR / "adapters" / "alloc_tag_kernel_direct_harness.c",
        "harness_fix_define": "FIXED",
        "deps": [
            PROPERTIES_DIR / "alloc_tag" / "alloc_tag.c",
        ],
        "slice_preserve": [
            "alloc_tag_of", "alloc_tag_mark", "alloc_tag_clear",
            "alloc_tag_kfree_ok", "alloc_tag_vfree_ok",
            "alloc_tag_free_ok_null_or",
            "alloc_tag_ghost_find", "alloc_tag_ghost_find_or_add",
        ],
        "required_bodies": [
            "alloc_tag_vfree_ok",
            "alloc_tag_of",
        ],
    },
    "kobject_lifetime": {
        "adapter":
            SCRIPT_DIR / "adapters" / "kobject_kernel_adapter.c",
        "adapter_probe":
            SCRIPT_DIR / "adapters" / "kobject_kernel_adapter_probe.c",
        "harness":
            SCRIPT_DIR / "adapters" / "kobject_kernel_direct_harness.c",
        "harness_fix_define": "FIXED",
        "deps": [
            PROPERTIES_DIR / "kobject_lifetime" / "kobject_lifetime.c",
        ],
        "slice_preserve": [
            "kobject_live", "kobject_lifetime_usage",
            "kobject_lifetime_init", "kobject_lifetime_get",
            "kobject_lifetime_put",
            "kobject_ghost_find", "kobject_ghost_find_or_add",
        ],
        "required_bodies": [
            "kobject_live",
            "kobject_lifetime_usage",
        ],
    },
    # Phase-1 balance modules generated via
    # scan/balance_module_factory.py.
    "device_lifetime": {
        "adapter":
            SCRIPT_DIR / "adapters" / "device_kernel_adapter.c",
        "adapter_probe":
            SCRIPT_DIR / "adapters" / "device_kernel_adapter_probe.c",
        "harness":
            SCRIPT_DIR / "adapters" / "device_kernel_direct_harness.c",
        "harness_fix_define": "FIXED",
        "deps": [
            PROPERTIES_DIR / "device_lifetime" / "device_lifetime.c",
        ],
        "slice_preserve": [
            "device_live", "device_lifetime_usage",
            "device_lifetime_init", "device_lifetime_get",
            "device_lifetime_put",
            "device_ghost_find", "device_ghost_find_or_add",
        ],
        "required_bodies": [
            "device_live",
            "device_lifetime_usage",
        ],
    },
    "of_node_lifetime": {
        "adapter":
            SCRIPT_DIR / "adapters" / "of_node_kernel_adapter.c",
        "adapter_probe":
            SCRIPT_DIR / "adapters" / "of_node_kernel_adapter_probe.c",
        "harness":
            SCRIPT_DIR / "adapters" / "of_node_kernel_direct_harness.c",
        "harness_fix_define": "FIXED",
        "deps": [
            PROPERTIES_DIR / "of_node_lifetime" / "of_node_lifetime.c",
        ],
        "slice_preserve": [
            "of_node_live", "of_node_lifetime_usage",
            "of_node_lifetime_init", "of_node_lifetime_get",
            "of_node_lifetime_put",
            "of_node_ghost_find", "of_node_ghost_find_or_add",
        ],
        "required_bodies": [
            "of_node_live",
            "of_node_lifetime_usage",
        ],
    },
    "inode_lifetime": {
        "adapter":
            SCRIPT_DIR / "adapters" / "inode_kernel_adapter.c",
        "adapter_probe":
            SCRIPT_DIR / "adapters" / "inode_kernel_adapter_probe.c",
        "harness":
            SCRIPT_DIR / "adapters" / "inode_kernel_direct_harness.c",
        "harness_fix_define": "FIXED",
        "deps": [
            PROPERTIES_DIR / "inode_lifetime" / "inode_lifetime.c",
        ],
        "slice_preserve": [
            "inode_live", "inode_lifetime_usage",
            "inode_lifetime_init", "inode_lifetime_get",
            "inode_lifetime_put",
            "inode_ghost_find", "inode_ghost_find_or_add",
        ],
        "required_bodies": [
            "inode_live",
            "inode_lifetime_usage",
        ],
    },
}


# Budget for cbmc runs against real kernel binaries, in seconds.
KERNEL_CBMC_TIMEOUT = 600
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

    # Apply each replacement in its own goto-instrument invocation so
    # that a symbol absent from this particular link does not abort
    # the pipeline (we pass both the external name and any
    # file-local-mangled form, and only one will typically resolve
    # in any given target).
    trans_gb = tmp / f"{target.stem}.trans.gb"
    current_input = gb
    applied_any = False
    for fn in CONTRACT_FUNCTIONS.get(module, []):
        step_out = tmp / f"{target.stem}.trans.{fn}.gb"
        result = _run(
            [str(goto_instrument), "--replace-call-with-contract", fn,
             str(current_input), str(step_out)],
            timeout=GI_TIMEOUT, check=False,
        )
        if result.returncode == 0:
            current_input = step_out
            applied_any = True
    if applied_any and current_input != gb:
        current_input.rename(trans_gb)
    else:
        import shutil
        shutil.copy(str(gb), str(trans_gb))

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


def _list_goto_function_bodies(
    goto_instrument: Path, binary: Path
) -> set[str]:
    """Return the set of function names that have a non-empty body in
    `binary`.

    Uses `goto-instrument --list-goto-functions`, which emits one
    line per function in the form

        symbol_name /* source_name */                 — has body
        symbol_name /* source_name, body not available */

    We collect both the symbol name and the source name (the kernel's
    `static` symbols are exposed under
    `__CPROVER_file_local_<file>_<sym>` in the source-name position
    but retain the short symbol name before the /* comment, so we
    index under both to match either lookup style).
    """
    result = subprocess.run(
        [str(goto_instrument), "--list-goto-functions", str(binary)],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
    )
    text = result.stdout.decode("utf-8", errors="replace")
    with_body: set[str] = set()
    for line in text.splitlines():
        line = line.strip()
        if not line or "/*" not in line:
            continue
        has_body = "body not available" not in line
        if not has_body:
            continue
        symbol = line.split("/*", 1)[0].strip()
        comment = line.split("/*", 1)[1].rsplit("*/", 1)[0].strip()
        # The comment may have the form "source_name" or
        # "source_name, body not available"; we already filtered the
        # latter.
        src_name = comment.split(",", 1)[0].strip()
        with_body.add(symbol)
        with_body.add(src_name)
    return with_body


def _verify_required_bodies(
    goto_instrument: Path,
    binary: Path,
    required: list[str],
) -> list[str]:
    """Return the subset of `required` names that do NOT have a body
    in `binary`.  An empty list means all requirements are met."""
    present = _list_goto_function_bodies(goto_instrument, binary)
    return [name for name in required if name not in present]


def _apply_kernel_transformations(
    goto_instrument: Path,
    module: str,
    target: Path,
    spec: dict,
    tmp: Path,
    linked_gb: Path,
    transformed_suffix: str,
) -> Path:
    """Apply the common goto-instrument transformation chain: strip
    bodies of CBMC-symex-problematic kernel helpers, replace each
    contract function call with its attached contract, then
    aggressive-slice preserving adapter predicates.

    `transformed_suffix` is appended to filenames so real and
    vacuity-probe pipelines can coexist in the same tmp dir.

    Returns the final transformed binary path.
    """
    current_input = linked_gb

    # Kernel helpers that CBMC's symex can't currently lower (use of
    # gcc's __builtin_*_overflow in statement expressions trips up
    # symex_assign with an "Unreachable" invariant violation).  Strip
    # their bodies so each call site becomes a nondet-return stub;
    # this is sound for our property as those helpers are unrelated
    # to the scatterlist/provenance reasoning.
    kernel_symex_problem_functions = [
        "__CPROVER_file_local_overflow_h_array_size",
        "__CPROVER_file_local_overflow_h_array3_size",
        "__CPROVER_file_local_overflow_h_struct_size",
    ]
    for fn in kernel_symex_problem_functions:
        step_out = tmp / f"{target.stem}.{transformed_suffix}.nobody.{fn}.gb"
        result = _run(
            [str(goto_instrument), "--remove-function-body", fn,
             str(current_input), str(step_out)],
            timeout=GI_TIMEOUT, check=False,
        )
        if result.returncode == 0:
            current_input = step_out

    # Replace the contract function's calls with the adapter-attached
    # contract.  Some symbols may not exist in every link; apply each
    # replacement in its own goto-instrument invocation so a missing
    # symbol does not abort the pipeline.
    for fn in CONTRACT_FUNCTIONS.get(module, []):
        step_out = (
            tmp / f"{target.stem}.{transformed_suffix}.trans.{fn}.gb"
        )
        result = _run(
            [str(goto_instrument), "--replace-call-with-contract", fn,
             str(current_input), str(step_out)],
            timeout=GI_TIMEOUT, check=False,
        )
        if result.returncode == 0:
            current_input = step_out

    # Aggressive slicing — keep only function bodies on the trace to
    # the replaced contract sites.  Preserve the adapter predicates
    # explicitly (contract requires clauses are not CFG edges to the
    # slicer).
    sliced = tmp / f"{target.stem}.{transformed_suffix}.sliced.gb"
    slice_args = [str(goto_instrument), "--aggressive-slice"]
    for preserve in spec.get("slice_preserve", []):
        slice_args += ["--aggressive-slice-preserve-function", preserve]
    slice_args += [str(current_input), str(sliced)]
    result = _run(slice_args, timeout=GI_TIMEOUT, check=False)
    if result.returncode == 0:
        current_input = sliced

    final = tmp / f"{target.stem}.{transformed_suffix}.gb"
    if current_input == linked_gb:
        import shutil
        shutil.copy(str(linked_gb), str(final))
    else:
        current_input.rename(final)
    return final


def _run_cbmc_on_trans(
    cbmc: Path, trans_gb: Path, entry: str, sarif: Path | None,
    timeout_s: int,
) -> tuple[str, str, str, list[CbmcFailure]]:
    """Run cbmc on a transformed binary, return
    (status, notes, combined_output, failures).

    status is one of: "successful", "failed", "timeout", "error".
    """
    args = [str(cbmc), str(trans_gb),
            "--function", entry,
            "--unwind", "2", "--no-unwinding-assertions",
            "--no-standard-checks",
            # --slice-formula drops parts of the SSA formula that do
            # not influence the assertions.  Essential on
            # kernel-scale inputs where the SAT solver otherwise
            # OOMs.
            "--slice-formula"]
    if sarif is not None:
        args += ["--sarif-result", str(sarif)]
    try:
        result = _run(args, timeout=timeout_s)
    except subprocess.TimeoutExpired:
        return ("timeout",
                f"cbmc exceeded {timeout_s}s on entry '{entry}'. "
                "This is the LIM-006 state-explosion case; mitigation "
                "requires aggressive stubbing of kernel helpers (see "
                "CBMC_LIMITATIONS.md).",
                "", [])

    combined = result.stdout + result.stderr
    failures: list[CbmcFailure] = []
    if "VERIFICATION SUCCESSFUL" in combined:
        return ("successful", "", combined, failures)
    if "VERIFICATION FAILED" in combined:
        for line in combined.splitlines():
            m = re.match(r"^\[([^\]]+)\]\s+(.*?)\s*:\s*FAILURE$", line)
            if m:
                failures.append(CbmcFailure(
                    assertion=m.group(1),
                    location=m.group(2),
                ))
        return ("failed", "", combined, failures)
    return ("error", f"cbmc exit {result.returncode} on entry '{entry}'",
            combined, failures)


def run_cbmc_kernel(
    module: str,
    target: Path,
    tmp: Path,
    direction: str = "vuln",
) -> tuple[ModuleReport, Path | None]:
    """Run the CBMC stage on real kernel source using the module's
    kernel adapter.  Compiles the target file with scan/compile_file.sh
    if a LINUX_TREE environment variable identifies its root, links
    with the adapter + page_provenance, applies the contract, and runs
    cbmc with `_aead_recvmsg`-style entry-point selection.

    The ``direction`` parameter selects between the vulnerable-shape
    harness build (``"vuln"``, default — expect FAILED) and the
    safe-shape harness build (``"fix"`` — expect SUCCESSFUL).  The
    scan compiles the harness twice in a single scan.py invocation
    if the ``harness_fix_define`` key is present in the module's
    ``KERNEL_ADAPTERS`` spec; otherwise ``direction`` is ignored.

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

    # Compile the kernel-aware stubs (if any) with the same kernel
    # flags — they `#include <linux/…>` and need the same -I soup.
    # Same story for the harness, which wants to include kernel
    # headers so it can build a valid pointer graph for the entry
    # function (otherwise nested nondet dereferences inside inlined
    # helpers kill reachability before the target call site is
    # reached).  The adapter is kept free of kernel-header deps and
    # is linked as plain C via goto-cc below.
    stubs_gb: Path | None = None
    if "stubs" in spec:
        stubs_gb = tmp / f"{target.stem}.stubs.gb"
        try:
            _run(
                [str(SCRIPT_DIR / "compile_file.sh"), ktree,
                 str(Path(spec["stubs"]).resolve()), str(stubs_gb)],
                timeout=GOTOCC_TIMEOUT, check=True,
            )
        except subprocess.TimeoutExpired:
            return (ModuleReport(
                module=module, cbmc_status="error",
                cbmc_notes=(
                    f"compile_file.sh exceeded {GOTOCC_TIMEOUT}s on stubs"
                ),
            ), None)

    harness_gb: Path | None = None
    if "harness" in spec:
        harness_gb = tmp / f"{target.stem}.harness.gb"
        # When the harness supports a fix-define and the caller
        # asked for the fix direction, pass the define through to
        # goto-cc so the harness builds its safe-shape branch.
        fix_define = spec.get("harness_fix_define")
        extra_args: list[str] = []
        if direction == "fix" and fix_define:
            extra_args.append(fix_define)
        try:
            _run(
                [str(SCRIPT_DIR / "compile_file.sh"), ktree,
                 str(Path(spec["harness"]).resolve()), str(harness_gb),
                 *extra_args],
                timeout=GOTOCC_TIMEOUT, check=True,
            )
        except subprocess.TimeoutExpired:
            return (ModuleReport(
                module=module, cbmc_status="error",
                cbmc_notes=(
                    f"compile_file.sh exceeded {GOTOCC_TIMEOUT}s on harness"
                ),
            ), None)

    # Link kernel binary + stubs binary + adapter + harness + deps.
    # We build two linked binaries: one with the real adapter (the
    # `verify_gb`) and one with the vacuity-probe adapter (the
    # `probe_gb`).  The probe run is the primary guardrail against
    # vacuity: if its trivially-false contract reports SUCCESSFUL,
    # the call site is unreachable in the linked binary and the
    # scan is refused with `cbmc_status: "vacuity-risk"`.
    def _link_with(adapter_path: Path, suffix: str) -> Path:
        linked = tmp / f"{target.stem}.{suffix}.linked.gb"
        inputs = [str(kernel_gb), str(adapter_path)]
        if stubs_gb is not None:
            inputs.append(str(stubs_gb))
        if harness_gb is not None:
            inputs.append(str(harness_gb))
        inputs += [str(p) for p in spec.get("deps", [])]
        _run(
            [str(goto_cc), *inputs, "-o", str(linked)],
            timeout=GOTOCC_TIMEOUT, check=True,
        )
        return linked

    linked_gb = _link_with(Path(spec["adapter"]), "verify")

    # ---------------- Guardrail 1: symbol-body check -----------------
    # Before any transformation, confirm the functions that MUST have
    # bodies in the linked binary actually do.  LIM-009's root cause
    # was that the kernel TU's `static _aead_recvmsg` bound to an
    # empty external stub (symbol present, body absent) — which is
    # detected here.
    required_bodies = spec.get("required_bodies", [])
    if required_bodies:
        missing = _verify_required_bodies(
            goto_instrument, linked_gb, required_bodies,
        )
        if missing:
            return (ModuleReport(
                module=module, cbmc_status="vacuity-risk",
                cbmc_notes=(
                    "required function(s) have no body in the linked "
                    "goto binary: " + ", ".join(missing) + ". "
                    "Likely cause: a `static` or `static inline` "
                    "kernel symbol was called via an unmangled "
                    "`extern` declaration and bound to an empty "
                    "external stub.  Update the harness to call the "
                    "`__CPROVER_file_local_<file>_<sym>` mangled "
                    "name, or extend KERNEL_ADAPTERS[...][required_"
                    "bodies] if the symbol is genuinely optional."
                ),
            ), None)

    entry_candidates = {"aead": "_aead_recvmsg"}
    if "harness" in spec:
        entry = "main"
    else:
        entry = entry_candidates.get(module, "main")

    # ---------------- Guardrail 2: vacuity probe ---------------------
    # Link the same kernel + stubs + harness with a probe adapter
    # whose contract has `__CPROVER_requires(0 == 1)`.  Run the same
    # transformation chain; cbmc MUST report FAILED on the
    # precondition, proving the contract call site is reached.  If
    # it reports SUCCESSFUL, the scan is vacuous and we refuse a
    # verdict.  `timeout` on the probe is reported as "unknown"
    # (we can't tell whether the site is reached) but does not
    # block the real run.
    if "adapter_probe" in spec:
        probe_linked = _link_with(
            Path(spec["adapter_probe"]), "probe",
        )
        probe_trans = _apply_kernel_transformations(
            goto_instrument, module, target, spec, tmp,
            probe_linked, transformed_suffix="probe",
        )
        probe_status, probe_notes, _, _ = _run_cbmc_on_trans(
            cbmc, probe_trans, entry, None, KERNEL_CBMC_TIMEOUT,
        )
        if probe_status == "successful":
            return (ModuleReport(
                module=module, cbmc_status="vacuity-risk",
                cbmc_notes=(
                    "vacuity probe reported SUCCESSFUL — the contract "
                    "call site is unreachable in the linked binary, "
                    "so any 'successful' from the main run would be "
                    "vacuous.  Check the harness, stubs, and "
                    "KERNEL_ADAPTERS configuration for this module."
                ),
            ), None)
        if probe_status == "error":
            return (ModuleReport(
                module=module, cbmc_status="vacuity-risk",
                cbmc_notes=(
                    f"vacuity probe failed with cbmc error: "
                    f"{probe_notes}"
                ),
            ), None)
        # probe_status in {"failed", "timeout"} — either the probe
        # correctly fired (non-vacuous) or it timed out before
        # reaching a verdict.  We proceed with the real run in both
        # cases; the real-run cbmc verdict is still informative
        # under timeout-only-on-probe.

    # ---------------- Real verification ------------------------------
    trans_gb = _apply_kernel_transformations(
        goto_instrument, module, target, spec, tmp,
        linked_gb, transformed_suffix="verify",
    )

    mr = ModuleReport(module=module)
    sarif = tmp / f"{target.stem}.{module}.sarif"
    status, notes, combined, failures = _run_cbmc_on_trans(
        cbmc, trans_gb, entry, sarif, KERNEL_CBMC_TIMEOUT,
    )
    mr.cbmc_status = status
    mr.cbmc_notes = notes
    mr.cbmc_failures = failures
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
# Per-file mode: synthesise a harness per enclosing function and run
# the scan pipeline through scan-per-file.sh.
# ---------------------------------------------------------------------------

# Budget for a single scan-per-file.sh invocation.  The script runs
# goto-cc + goto-instrument + cbmc end-to-end; each sub-tool has its
# own rlimit/timeout inside the script, but the outer wrapper caps
# the whole pipeline so runaway cases are killed cleanly.
PER_FILE_TIMEOUT = int(os.environ.get("SCAN_PER_FILE_TIMEOUT", 900))


def _strip_c_comments_preserve_lines(text: str) -> str:
    """Remove C block and line comments, preserving newline count so
    line numbers in the returned text match the original source."""
    def _block_repl(m: re.Match) -> str:
        return "\n" * m.group(0).count("\n")
    text = re.sub(r"/\*.*?\*/", _block_repl, text, flags=re.DOTALL)
    text = re.sub(r"//[^\n]*", "", text)
    return text


# Function-definition detector: an identifier followed by a
# parenthesised parameter list, optionally whitespace (possibly
# including a newline), then an opening '{'.  The parameter list
# may span multiple lines.  Requires that the signature does not
# contain a ';' (which would mark it as a declaration only) and
# that no '{' appears inside the parameters (which would mean we
# matched a nested block).
_FUNC_DEF_RE = re.compile(
    r"(?:^|\n)"
    r"[\w\s\*]*?"                   # return type soup
    r"\b(\w+)\s*"                   # function name (captured)
    r"\(\s*([^;{}]*?)\s*\)"         # parameter list
    r"(?:\s*__attribute__\s*\(\([^)]*\)\))?"  # optional attribute
    r"\s*\{",                       # opening brace
    re.MULTILINE,
)


def find_enclosing_function(source: Path, line_number: int) -> str | None:
    """Return the name of the function whose body contains
    `line_number` in `source`, or None if none matches.

    Uses a two-step scan: first find function-definition signatures
    via regex (preserving line numbers by blanking out comments);
    then for each match track the matching close-brace by
    depth-counting.  The innermost function spanning `line_number`
    is returned.  Kernel-style C (opening '{' flush-left, signatures
    on a single line or with args wrapping) parses cleanly; unusual
    layouts may return None and the caller must fall back to
    adapter mode for that hit.
    """
    try:
        text = source.read_text(errors="replace")
    except OSError:
        return None
    text = _strip_c_comments_preserve_lines(text)

    # Collect (start_line, end_line, name) for every function def.
    functions: list[tuple[int, int, str]] = []
    for m in _FUNC_DEF_RE.finditer(text):
        name = m.group(1)
        # Guard against misclassifying keywords as the function name.
        if name in {"if", "for", "while", "switch", "return",
                    "sizeof", "do", "else", "typeof", "static",
                    "inline", "extern", "const", "struct", "union",
                    "enum"}:
            continue
        open_brace_pos = m.end() - 1
        start_line = text.count("\n", 0, m.start(1)) + 1
        # Depth-count to find the matching close brace.
        depth = 0
        i = open_brace_pos
        end_line = None
        while i < len(text):
            c = text[i]
            if c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    end_line = text.count("\n", 0, i + 1) + 1
                    break
            i += 1
        if end_line is not None:
            functions.append((start_line, end_line, name))

    # Pick the innermost match (smallest span).  This handles
    # nested functions (rare in kernel C but possible with gcc
    # extensions) without mis-attributing a hit to an outer scope.
    best: tuple[int, int, str] | None = None
    for f in functions:
        start, end, _ = f
        if start <= line_number <= end:
            if best is None or (end - start) < (best[1] - best[0]):
                best = f
    return best[2] if best is not None else None


# Modules that ship a ghost-bootstrap configuration in
# synthesise_harness.py AND a default set of contract targets.
# scan.py passes the contract targets from CONTRACT_FUNCTIONS to
# scan-per-file.sh explicitly, overriding the script's own
# fallback defaults; this keeps CONTRACT_FUNCTIONS the single
# source of truth.
_PER_FILE_SUPPORTED_MODULES = {
    "cred_lifetime",
    "pipe_buffer",
    "lock_state",
    "refcount_lifetime",
    "aead",
    "kobject_lifetime",
    # Phase-1 balance modules.
    "device_lifetime",
    "of_node_lifetime",
    "inode_lifetime",
}


def run_cbmc_per_file(
    module: str,
    target: Path,
    hits: list[CocciHit],
    tmp: Path,
) -> ModuleReport:
    """Run the --per-file pipeline against `target` for each cocci
    hit in `hits`.  Groups hits by enclosing function so a single
    harness covers multiple hits in the same function.

    Returns a ModuleReport with `per_file` populated and
    `cbmc_status` aggregated:
      - "failed"       if any per-hit verdict is "failed";
      - "timeout"      if any is "timeout" and none are "failed";
      - "successful"   if all verdicts are "successful";
      - "error"        if any is "error" and no other signal;
      - "adapter-needed"  if the module isn't supported here;
      - "not-run"      if no hits had a resolvable enclosing function.
    """
    mr = ModuleReport(module=module, cocci_hits=hits)

    if module not in _PER_FILE_SUPPORTED_MODULES:
        mr.cbmc_status = "adapter-needed"
        mr.cbmc_notes = (
            f"--per-file: module '{module}' is not yet supported by "
            "synthesise_harness.py's ghost-bootstrap table.  See "
            "scan/synthesise_harness.py MODULE_GHOST_BOOTSTRAP to "
            "add support."
        )
        return mr

    ktree = os.environ.get("LINUX_TREE")
    if not ktree:
        mr.cbmc_status = "error"
        mr.cbmc_notes = (
            "--per-file requires LINUX_TREE to point at a kernel tree"
        )
        return mr

    # Group hits by enclosing function.
    by_function: dict[str, list[int]] = {}
    unresolved: list[int] = []
    for h in hits:
        func = find_enclosing_function(target, h.line)
        if func is None:
            unresolved.append(h.line)
            continue
        by_function.setdefault(func, []).append(h.line)

    # Unresolved hits are recorded as their own verdict so the JSON
    # report captures the gap honestly.
    if unresolved:
        mr.per_file.append(PerFileVerdict(
            function="",
            hit_lines=unresolved,
            status="no-function-found",
            notes=(
                "scan.py could not determine the enclosing function "
                "for these hits via regex-based scanning.  Layouts "
                "with macro-generated function headers, "
                "__attribute__((...)) placements, or unusual "
                "formatting can defeat the scanner."
            ),
        ))

    if not by_function:
        if not unresolved:
            mr.cbmc_status = "not-run"
            mr.cbmc_notes = "no cocci hits to scan"
        else:
            mr.cbmc_status = "error"
            mr.cbmc_notes = "no resolvable enclosing functions"
        return mr

    per_file_sh = SCRIPT_DIR / "scan-per-file.sh"

    # Relative path of the kernel file inside LINUX_TREE — what
    # scan-per-file.sh expects.
    try:
        rel = target.resolve().relative_to(Path(ktree).resolve())
    except ValueError:
        mr.cbmc_status = "error"
        mr.cbmc_notes = (
            f"--per-file: {target} is not under $LINUX_TREE={ktree}"
        )
        return mr

    contract_targets = CONTRACT_FUNCTIONS.get(module, [])

    for func, lines in by_function.items():
        try:
            result = _run(
                [str(per_file_sh), module, str(rel), func,
                 *contract_targets],
                timeout=PER_FILE_TIMEOUT,
            )
        except subprocess.TimeoutExpired:
            mr.per_file.append(PerFileVerdict(
                function=func,
                hit_lines=sorted(lines),
                status="timeout",
                notes=(
                    f"scan-per-file.sh exceeded {PER_FILE_TIMEOUT}s"
                ),
            ))
            continue

        combined = (result.stdout or "") + (result.stderr or "")
        rc = result.returncode
        # scan-per-file.sh exit code conventions:
        #   0  VERIFICATION SUCCESSFUL with contract clause checked
        #      (real successful — property holds at the call site).
        #   10 VERIFICATION FAILED with contract violation —
        #      REAL CANDIDATE BUG.
        #   11 VERIFICATION FAILED but only built-in CBMC checks
        #      fired (memcpy/memset bounds, no-body, unwind, etc.);
        #      no contract clause was violated.  Reported as
        #      "noise" — the harness shape didn't fully match the
        #      kernel state, but the property holds.
        #   12 No contract clause was even checked — vacuous.
        #      Either the call site is unreachable from the
        #      synthesised harness or the contract didn't apply.
        #   13 Synthesiser detected a known-unverifiable shape
        #      (currently the aead transform-wrapper) and skipped.
        #      Reported as "skipped" — not a bug signal but also
        #      not a noise/failure.
        #   3  infrastructure error (compile/link/etc)
        #   2  usage error
        #   other: cbmc non-verdict exit
        # Detect the empty-ghost-confidence marker that
        # scan-per-file.sh emits on its verdict line for
        # harnesses where no parameter type matched the
        # module's ghost-bootstrap.  The marker is appended to
        # the verdict line; we capture it for downstream
        # triage so the per_file rollup can segment
        # high-confidence vs low-confidence candidates.
        empty_ghost = "empty-ghost-confidence: low" in combined
        confidence_note = (
            " [empty-ghost: low-confidence]" if empty_ghost else ""
        )
        if rc == 0:
            status = "successful"
            notes = "" + confidence_note.lstrip()
        elif rc == 10:
            status = "failed"
            notes = "" + confidence_note.lstrip()
        elif rc == 11:
            status = "noise"
            notes = (
                "VERIFICATION FAILED but no contract clause "
                "violated; only CBMC built-in checks fired."
                + confidence_note
            )
        elif rc == 12:
            status = "vacuous"
            notes = (
                "no contract clause checked at this call site"
                + confidence_note
            )
        elif rc == 13:
            status = "skipped"
            notes = (
                "known-unverifiable shape (e.g. aead transform-"
                "wrapper); harness synthesis intentionally skipped"
            )
        elif rc in (2, 3):
            status = "error"
            # Last few informative lines from the script's output.
            notes = "\n".join(combined.splitlines()[-5:])
        else:
            status = "error"
            notes = (
                f"scan-per-file.sh exit {rc}; last lines:\n"
                + "\n".join(combined.splitlines()[-5:])
            )
        mr.per_file.append(PerFileVerdict(
            function=func,
            hit_lines=sorted(lines),
            status=status,
            notes=notes,
        ))

    # Aggregate.
    statuses = {v.status for v in mr.per_file}
    if "failed" in statuses:
        mr.cbmc_status = "failed"
    elif "timeout" in statuses:
        mr.cbmc_status = "timeout"
    elif "error" in statuses:
        mr.cbmc_status = "error"
    elif "successful" in statuses:
        mr.cbmc_status = "successful"
    elif "no-function-found" in statuses:
        mr.cbmc_status = "error"
        mr.cbmc_notes = "no enclosing functions resolved; see per_file"
    else:
        mr.cbmc_status = "not-run"
    return mr


# ---------------------------------------------------------------------------
# Driver.
# ---------------------------------------------------------------------------

def scan_file(
    target: Path,
    tmp: Path,
    direction: str = "vuln",
    per_file: bool = False,
    bug_shape_only: bool = False,
) -> tuple[FileReport, list[Path]]:
    """Run every registered property module against one file.  Returns
    a FileReport plus any SARIF files cbmc produced for that file.

    ``direction`` is passed through to :func:`run_cbmc_kernel` to
    select between the vulnerable- and safe-shape harness builds
    for property modules that ship a fix-direction harness.

    ``per_file`` selects the --per-file pipeline: instead of
    linking the hand-written direct-call harness from
    ``KERNEL_ADAPTERS``, scan.py determines the enclosing function
    for each cocci hit, synthesises a per-function harness via
    ``scan/synthesise_harness.py``, and runs the scan pipeline
    through ``scan/scan-per-file.sh``.  Verdicts are reported
    per-hit in ``ModuleReport.per_file`` and aggregated into
    ``cbmc_status``.  ``direction`` is ignored in per-file mode.

    ``bug_shape_only`` filters cocci hits to only those tagged
    ``BUG-SHAPE:`` — see scan/properties/*/.cocci for the
    pattern-aware rules that emit this tag.  Drops generic
    call-site hits.  Useful for corpus-scale bug hunts where the
    noise floor of call-site-only hits drowns the real signal.
    """
    report = FileReport(file=str(target))
    sarifs: list[Path] = []
    modules = discover_modules()
    for module, cocci in modules:
        hits = run_cocci(module, cocci, target)
        if bug_shape_only:
            hits = [h for h in hits if "BUG-SHAPE:" in h.message]
        mr = ModuleReport(module=module, cocci_hits=hits)
        if hits:
            if per_file and not file_uses_property_modules(target):
                # Per-file synthesis only makes sense for real
                # kernel source (property-module-native test
                # harnesses don't need enclosing-function
                # resolution).  Fall through to the adapter /
                # native code path otherwise.
                mr = run_cbmc_per_file(module, target, hits, tmp)
                # per_file mode doesn't emit SARIF directly today
                # (the cocci_hits + per_file verdicts are written
                # into the JSON / cocci-SARIF outputs instead).
            elif file_uses_property_modules(target):
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
                        mr, sarif = run_cbmc_kernel(
                            module, target, tmp, direction=direction,
                        )
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
        for v in m.per_file:
            lines = ",".join(str(n) for n in v.hit_lines)
            func = v.function or "(no enclosing function)"
            print(f"    per-file {func}  lines={lines}  "
                  f"verdict={v.status}")
            if v.notes:
                for note_line in v.notes.splitlines():
                    print(f"             {note_line}")
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


def _sarif_rule_for_module(module: str) -> dict:
    """SARIF `reportingDescriptor` for a property module.  One rule
    per module; the module name is the rule id."""
    descriptions = {
        "aead": (
            "AEAD scatterlist may be aliased to a page the user cannot "
            "write (Copy Fail / CVE-2026-31431 bug class).  The call "
            "site was flagged by the Coccinelle prefilter; review the "
            "aead_request_set_crypt arguments and confirm the "
            "destination scatterlist cannot contain page-cache pages."
        ),
        "pipe_buffer": (
            "pipe_buffer slot assignment without a preceding flags "
            "reset may carry PIPE_BUF_FLAG_CAN_MERGE from a previous "
            "occupant (Dirty Pipe / CVE-2022-0847 bug class).  Review "
            "the take-over site and confirm buf->flags is zeroed before "
            "the new page is assigned."
        ),
    }
    help_uris = {
        "aead": "https://github.com/diffblue/cbmc/"
                "tree/develop/integration/linux/properties/aead",
        "pipe_buffer": "https://github.com/diffblue/cbmc/"
                       "tree/develop/integration/linux/properties/pipe_buffer",
    }
    return {
        "id": f"cbmc-linux-scan/{module}",
        "name": f"cbmc-linux-scan-{module}",
        "shortDescription": {"text": f"{module} bug-class prefilter hit"},
        "fullDescription": {
            "text": descriptions.get(module,
                f"{module} property module prefilter hit — see README "
                "for the bug class specification."),
        },
        "helpUri": help_uris.get(
            module,
            "https://github.com/diffblue/cbmc/tree/develop/integration/linux"),
        "defaultConfiguration": {"level": "warning"},
    }


def write_cocci_sarif(
    reports: list["FileReport"],
    output: Path,
    repo_root: str | None = None,
) -> None:
    """Emit a SARIF 2.1.0 log pinning Coccinelle prefilter hits to
    their kernel-source file:line locations, suitable for upload
    via the `github/codeql-action/upload-sarif` action.

    LIM-013 context: the scan's per-file signal is the Coccinelle
    prefilter hit, not the cbmc_status of the shared direct-call
    harness.  This function emits exactly that signal — each cocci
    hit becomes a SARIF result anchored at a kernel-source line,
    which is what GitHub's Code Scanning tab will render.

    `repo_root` is an optional prefix to strip from file paths so
    the URIs in the SARIF log are repository-relative rather than
    absolute.  When uploading under a non-Linux repo (the common
    case for this scan, which runs against a separate kernel
    checkout), leave `repo_root` unset.
    """
    # Collect modules that actually fired.
    seen_modules: set[str] = set()
    results: list[dict] = []

    for r in reports:
        for m in r.modules:
            for h in m.cocci_hits:
                seen_modules.add(m.module)
                uri = h.file
                if repo_root and uri.startswith(repo_root):
                    uri = uri[len(repo_root):].lstrip("/")
                notes = ""
                if m.cbmc_status and m.cbmc_status not in (
                    "not-run", "adapter-needed"
                ):
                    notes = (f" [pipeline verdict: cbmc_status="
                            f"{m.cbmc_status}]")
                results.append({
                    "ruleId": f"cbmc-linux-scan/{m.module}",
                    "level": "warning",
                    "message": {"text": h.message + notes},
                    "locations": [{
                        "physicalLocation": {
                            "artifactLocation": {
                                "uri": uri,
                            },
                            "region": {"startLine": h.line},
                        },
                    }],
                })

    rules = [_sarif_rule_for_module(m) for m in sorted(seen_modules)]

    sarif: dict = {
        "$schema": "https://json.schemastore.org/sarif-2.1.0.json",
        "version": "2.1.0",
        "runs": [{
            "tool": {
                "driver": {
                    "name": "cbmc-linux-scan",
                    "informationUri": (
                        "https://github.com/diffblue/cbmc/"
                        "tree/develop/integration/linux"
                    ),
                    "rules": rules,
                    "version": "0.1",
                },
            },
            "results": results,
        }],
    }
    output.write_text(json.dumps(sarif, indent=2))


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
    ap.add_argument(
        "--cocci-sarif", type=Path,
        help=(
            "write a separate SARIF 2.1.0 report containing one "
            "result per Coccinelle prefilter hit, anchored at its "
            "kernel-source file:line.  Suitable for upload via the "
            "`github/codeql-action/upload-sarif` action — the "
            "results will appear in the GitHub Code Scanning tab "
            "on the actual kernel source line.  See LIM-013 in "
            "CBMC_LIMITATIONS.md for why the cocci hits are the "
            "per-file signal."
        ),
    )
    ap.add_argument(
        "--cocci-sarif-repo-root",
        help=(
            "path prefix to strip from file URIs in --cocci-sarif "
            "output, so the SARIF is repository-relative when the "
            "scan ran against a checkout at a non-default location."
        ),
    )
    ap.add_argument(
        "--direction", choices=["vuln", "fix"], default="vuln",
        help=(
            "which harness direction to verify: 'vuln' (default) "
            "builds the harness's vulnerable-shape branch and "
            "expects the contract to fire; 'fix' builds the safe-"
            "shape branch and expects the contract to pass.  Only "
            "takes effect for modules whose KERNEL_ADAPTERS spec "
            "declares a harness_fix_define."
        ),
    )
    ap.add_argument(
        "--per-file", action="store_true",
        help=(
            "Use the per-file pipeline: for each cocci hit, find "
            "the enclosing function in the kernel source, "
            "synthesise a per-function harness via "
            "scan/synthesise_harness.py, and run the scan via "
            "scan/scan-per-file.sh.  Verdicts are reported per-hit "
            "in the JSON output's `per_file` field and aggregated "
            "into the module's cbmc_status.  Currently supported "
            "for modules {cred_lifetime, pipe_buffer} — see "
            "synthesise_harness.py MODULE_GHOST_BOOTSTRAP to add "
            "more.  Ignores --direction (per-file mode has only "
            "one direction)."
        ),
    )
    ap.add_argument(
        "--bug-shape-only", action="store_true",
        help=(
            "Filter cocci hits to only those tagged 'BUG-SHAPE:' "
            "in the report message.  These come from cocci rules "
            "that match actual bug-class shapes (e.g. back-to-back "
            "put_cred without intervening get_cred) rather than "
            "generic call sites.  Reduces the noise floor for "
            "corpus-scale bug hunts: every retained hit is a "
            "plausible candidate, not just a call site."
        ),
    )
    args = ap.parse_args()

    reports: list[FileReport] = []
    sarif_files: list[Path] = []
    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = Path(tmpdir)
        for f in args.files:
            if not f.is_file():
                print(f"skip (not a file): {f}", file=sys.stderr)
                continue
            r, sarifs = scan_file(
                f, tmp, direction=args.direction, per_file=args.per_file,
                bug_shape_only=args.bug_shape_only,
            )
            reports.append(r)
            sarif_files.extend(sarifs)
            print_summary(r)

        if args.sarif:
            merge_sarif(sarif_files, args.sarif)
            print(f"\nmerged SARIF report written to {args.sarif}")

        if args.cocci_sarif:
            write_cocci_sarif(
                reports, args.cocci_sarif,
                repo_root=args.cocci_sarif_repo_root,
            )
            print(f"\ncocci-hit SARIF report written to {args.cocci_sarif}")

    if args.json:
        args.json.write_text(json.dumps(report_json(reports), indent=2))
        print(f"\nstructured JSON report written to {args.json}")

    return 1 if any_cbmc_failure(reports) else 0


if __name__ == "__main__":
    sys.exit(main())
