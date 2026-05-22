#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 OR MIT
#
# Adapted from Strata Contributors' Tools/Python/scripts/hypothesmith.sh
# (origin/tautschnig/hypothesmith branch on github.com/strata-org/Strata).
# The Strata script drives strata's pyAnalyzeLaurel pipeline; this version
# drives CBMC's Python frontend instead. The program-generator scripts
# (gen_random_python.py, gen_unrestricted.py) are unchanged from the
# upstream layout, modulo cosmetic docstring updates.
#
# =============================================================================
# python-fuzz.sh — Fuzz-test CBMC's Python front-end
# =============================================================================
#
# This script generates random Python programs and runs them through CBMC's
# Python frontend pipeline to find crashes, parse failures, and semantic
# modelling bugs. It is analogous to CBMC's existing scripts/csmith.sh,
# which uses CSmith to generate random C programs for testing CBMC's C
# frontend.
#
# ## Background
#
# The approach is inspired by:
# - CSmith (https://embed.cs.utah.edu/csmith/) for random C program generation
# - hypothesmith (https://github.com/Zac-HD/hypothesmith) for random Python
#   program generation using the Hypothesis property-based testing framework
# - Strata's tautschnig/hypothesmith branch, which originally wired
#   hypothesmith into a CSmith-style fuzz harness for Strata's Python
#   front-end. This file is a CBMC-side adaptation of that harness.
#
# ## Modes
#
# **Syntax mode** — Uses hypothesmith's `from_grammar()` strategy to generate
# syntactically valid Python programs from the Python grammar. Each program
# is fed to CBMC. Failures of interest are:
#   - CBMC crash (Invariant violated, segfault, panic).
#   - CBMC frontend parse error on syntactically valid Python (a frontend bug).
# Programs CBMC simply rejects with an "Unsupported" warning are reported
# as SKIP, not as failures.
#
# **Semantic mode** — Uses a custom generator (gen_random_python.py) to
# produce typed Python programs with assertions whose expected values are
# computed by CPython at generation time (analogous to CSmith computing a
# checksum). Each program is:
#   1. Run under CPython to confirm all assertions hold (reference).
#   2. Run under CBMC.
#   3. If CPython passes but CBMC reports VERIFICATION FAILED, that is a
#      semantic modelling bug in the Python -> goto translation pipeline.
#   4. CBMC crashes are FAIL (internal error) regardless of CPython outcome.
#
# ## Result classification
#
# Each test program gets one of these outcomes:
#   OK                — Pipeline completed successfully.
#                       (Syntax mode: CBMC produced any verdict cleanly.
#                        Semantic mode: CBMC said VERIFICATION SUCCESSFUL.)
#   SKIP (parse)      — CBMC frontend rejected the program at parse time
#                       and printed a parse / type-check error message.
#   SKIP (unsupported)— CBMC emitted a warning that the construct isn't
#                       modelled (e.g. some str-method, complex match
#                       statement, unknown attribute).
#   TIMEOUT           — Analysis exceeded the time limit. Not a failure.
#   FAIL (crash)      — CBMC hit an Invariant violation, segfault, or
#                       similar runtime trap. Always a bug.
#   FAIL (verification)— CPython ran the program correctly but CBMC says
#                        VERIFICATION FAILED. Semantic modelling bug.
#   BUG (generator)   — The generated program itself fails under CPython.
#                       Bug in the generator, not in CBMC.
#
# ## Reproducibility
#
# Every run uses a seed that controls both the Hypothesis engine (syntax
# mode) and Python's random module (semantic mode). Given the same seed,
# the same programs are generated. The seed is printed at the start and
# end of every run.
#
# ## Usage
#
#   ./python-fuzz.sh [N [SEED [MODE [--unrestricted]]]]
#
#   N              Number of programs to generate per mode (default: 10)
#   SEED           Random seed for reproducibility (default: current timestamp)
#   MODE           "syntax", "semantic", or "both" (default: both)
#   --unrestricted Include generators for Python constructs beyond what CBMC
#                  is known to support today (classes, generators,
#                  comprehensions, try/except, match, async, decorators, etc.)
#
# ## Prerequisites
#
#   - CBMC built: cmake --build build --target cbmc
#   - Python venv with hypothesmith installed; the script auto-creates one
#     at scripts/python-fuzz/.venv if it doesn't already exist.
#
# ## Environment variables
#
#   CBMC          Path to the cbmc binary (default: build/bin/cbmc relative
#                 to the repository root)
#   CBMC_TIMEOUT  Per-program CBMC timeout in seconds (default: 30)
#   CBMC_UNWIND   Loop unwind bound (default: 5)
#   CBMC_MEM_MB   Per-CBMC-invocation memory cap in MiB (default: 4096).
#                 Applied via `ulimit -v` so a runaway CBMC, CPython, or
#                 hypothesmith generator can't take down the host. Set to
#                 0 to disable the cap.
#
# ## Exit codes
#
#   0  All tests passed (or only SKIPs/TIMEOUTs)
#   1  At least one FAIL detected (source code is printed for reproduction)
#
# =============================================================================

set -euo pipefail

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# CBMC binary location
CBMC="${CBMC:-$REPO_ROOT/build/bin/cbmc}"
CBMC_TIMEOUT="${CBMC_TIMEOUT:-30}"
CBMC_UNWIND="${CBMC_UNWIND:-5}"

# Per-CBMC memory cap (in MiB). Applied via ulimit -v so any subprocess
# inheriting from this shell — CBMC, the hypothesmith generator, the
# CPython sanity-check run — is bounded. Without a cap, a pathological
# fuzz program can blow up CBMC's solver memory and OOM-kill the host
# running the harness. 4 GiB is comfortably above the typical CBMC
# steady-state on the small programs we generate; raise CBMC_MEM_MB if
# you see legitimate fuzz programs hitting the cap. Set CBMC_MEM_MB=0
# to disable the cap entirely (e.g. for diagnostic runs).
CBMC_MEM_MB="${CBMC_MEM_MB:-4096}"
if [ "$CBMC_MEM_MB" != "0" ]; then
  # ulimit -v is in KiB. The cap propagates to every process this shell
  # spawns, including the timeouted cbmc invocation and the python venv
  # that drives the generator.
  ulimit -v $((CBMC_MEM_MB * 1024)) 2>/dev/null || \
    echo "warning: failed to set ulimit -v $((CBMC_MEM_MB * 1024)) — running without memory cap" >&2
fi

# Python venv (auto-created on first run)
VENV="$SCRIPT_DIR/.venv"
PYTHON="${PYTHON:-$VENV/bin/python3}"

# Command-line arguments with defaults
N="${1:-10}"
SEED="${2:-$(date +%s)}"
MODE="${3:-both}"
UNRESTRICTED=""
if [ "${4:-}" = "--unrestricted" ]; then
  UNRESTRICTED="--unrestricted"
fi

# Per-step timeouts
PARSE_TIMEOUT=10

# ---------------------------------------------------------------------------
# Prerequisite checks / venv setup
# ---------------------------------------------------------------------------

if [ ! -x "$CBMC" ]; then
  echo "ERROR: CBMC binary not found or not executable at: $CBMC" >&2
  echo "  Build CBMC first: cmake --build build --target cbmc" >&2
  echo "  Or set CBMC=/path/to/cbmc in the environment." >&2
  exit 2
fi

if [ ! -x "$PYTHON" ]; then
  echo "Setting up Python venv at $VENV..."
  python3 -m venv "$VENV"
  "$VENV/bin/pip" install --quiet --upgrade pip
  # hypothesmith brings hypothesis as a dependency.
  "$VENV/bin/pip" install --quiet hypothesmith
  PYTHON="$VENV/bin/python3"
fi

# ---------------------------------------------------------------------------
# Working directory (cleaned up on exit)
# ---------------------------------------------------------------------------

workdir=$(mktemp -d /tmp/cbmc-pyfuzz.XXX)
trap 'rm -rf "$workdir"' EXIT

failures=0

# ---------------------------------------------------------------------------
# Helper: run CBMC on one program; classify the outcome.
# ---------------------------------------------------------------------------
# Echoes one of:
#   OK <verdict>      — CBMC completed cleanly with VERIFICATION SUCCESSFUL/FAILED
#   SKIP_PARSE        — frontend parse / type error (front-end limitation)
#   SKIP_UNSUPPORTED  — frontend warning that the construct isn't modelled
#   TIMEOUT           — per-program timeout exceeded
#   CRASH <kind>      — invariant violation, panic, signal
#
# Captures the raw output to $1.cbmc.out for reproduction.
run_cbmc_on() {
  local pyfile="$1"
  local outfile="${pyfile}.cbmc.out"
  local ec=0
  if ! timeout "$CBMC_TIMEOUT" "$CBMC" \
        --unwind "$CBMC_UNWIND" --no-unwinding-assertions \
        "$pyfile" > "$outfile" 2>&1
  then
    ec=$?
  fi

  # SIGKILL from `timeout` shows as 124 (graceful) or 137 (KILL).
  if [ $ec -eq 124 ] || [ $ec -eq 137 ]; then
    echo "TIMEOUT"
    return
  fi

  # Crash patterns. CBMC emits an "Invariant check failed" header in its
  # invariant-failure path, plus a backtrace; segfaults/SIGABRT show up
  # as exit codes >=128 with no clean tail.
  if grep -q "Invariant check failed" "$outfile" \
     || grep -q "panic" "$outfile" \
     || [ $ec -ge 128 ]; then
    local kind
    kind=$(grep -m1 "Invariant check failed\|panic" "$outfile" || echo "signal $ec")
    echo "CRASH ${kind:-unknown}"
    return
  fi

  if grep -q "VERIFICATION SUCCESSFUL" "$outfile"; then
    echo "OK SUCCESSFUL"
    return
  fi
  if grep -q "VERIFICATION FAILED" "$outfile"; then
    echo "OK FAILED"
    return
  fi

  # Frontend rejection cases: PARSING ERROR, syntax errors in main.py,
  # type-check rejections, "Unsupported" warnings followed by no verdict.
  if grep -qE "PARSING ERROR|^Unknown variable" "$outfile"; then
    echo "SKIP_PARSE"
    return
  fi
  if grep -qE "warning: ignoring|warning: unsupported|no body for callee" "$outfile"; then
    echo "SKIP_UNSUPPORTED"
    return
  fi
  echo "OTHER"
}

# ---------------------------------------------------------------------------
# Syntax mode
# ---------------------------------------------------------------------------

run_syntax_tests() {
  local gen_dir="$workdir/syntax"
  echo "=== Syntax mode: generating $N programs (seed=$SEED) ==="
  # gen_random_python.py imports hypothesmith lazily — capture its output
  # so we can debug venv issues without polluting normal runs.
  if ! timeout 120 "$PYTHON" "$SCRIPT_DIR/gen_random_python.py" \
       --mode syntax --output-dir "$gen_dir" \
       --max-examples "$N" --max-nodes 50 --seed "$SEED" \
       > "$gen_dir.log" 2>&1; then
    echo "  Generator failed (see $gen_dir.log):"
    head -30 "$gen_dir.log"
    failures=$((failures + 1))
    return
  fi

  local count=0 syn_failures=0
  for pyfile in "$gen_dir"/fuzz_syntax_*.py; do
    [ -f "$pyfile" ] || continue
    count=$((count + 1))
    local base
    base=$(basename "$pyfile" .py)
    local result
    result=$(run_cbmc_on "$pyfile")
    case "$result" in
      OK\ *)             echo "  OK: $base (${result#OK })" ;;
      SKIP_PARSE)        echo "  SKIP (parse): $base" ;;
      SKIP_UNSUPPORTED)  echo "  SKIP (unsupported): $base" ;;
      TIMEOUT)           echo "  TIMEOUT: $base" ;;
      CRASH\ *)
        echo "  FAIL (crash): $base — ${result#CRASH }"
        echo "--- Source code ($pyfile) ---"
        cat "$pyfile"
        echo "--- CBMC output ---"
        head -80 "${pyfile}.cbmc.out"
        echo "---"
        syn_failures=$((syn_failures + 1))
        ;;
      *)
        echo "  ?? $base: $result"
        ;;
    esac
  done
  failures=$((failures + syn_failures))
  echo "Syntax tests: $count processed, $syn_failures failures"
}

# ---------------------------------------------------------------------------
# Semantic mode
# ---------------------------------------------------------------------------

run_semantic_tests() {
  local gen_dir="$workdir/semantic"
  echo "=== Semantic mode: generating $N programs (seed=$SEED) ==="
  if ! "$PYTHON" "$SCRIPT_DIR/gen_random_python.py" \
       --mode semantic --output-dir "$gen_dir" \
       --max-examples "$N" --seed "$SEED" $UNRESTRICTED \
       > "$gen_dir.log" 2>&1; then
    echo "  Generator failed (see $gen_dir.log):"
    head -30 "$gen_dir.log"
    failures=$((failures + 1))
    return
  fi

  local count=0 sem_failures=0
  for pyfile in "$gen_dir"/fuzz_semantic_*.py; do
    [ -f "$pyfile" ] || continue
    count=$((count + 1))
    local base
    base=$(basename "$pyfile" .py)

    # Step 1: CPython sanity check. If CPython itself raises, the
    # generator emitted a buggy program — count it but don't count
    # against CBMC.
    if ! timeout 10 python3 "$pyfile" 2>/dev/null; then
      echo "  BUG (generator): $base — fails under CPython!"
      echo "--- Source code ---"
      cat "$pyfile"
      sem_failures=$((sem_failures + 1))
      continue
    fi

    # Step 2: run CBMC.
    local result
    result=$(run_cbmc_on "$pyfile")
    case "$result" in
      OK\ SUCCESSFUL)
        echo "  OK: $base"
        ;;
      OK\ FAILED)
        # CPython said the program is correct but CBMC found a failing
        # assertion. That is exactly the semantic-modelling bug shape
        # we are looking for.
        echo "  FAIL (verification): $base"
        echo "  CPython says all assertions hold, but CBMC reports"
        echo "  VERIFICATION FAILED. This is a semantic modelling bug"
        echo "  in CBMC's Python frontend (Python -> goto translation)."
        echo "--- Source code ($pyfile) ---"
        cat "$pyfile"
        echo "--- CBMC output ---"
        head -60 "${pyfile}.cbmc.out"
        echo "---"
        sem_failures=$((sem_failures + 1))
        ;;
      SKIP_PARSE)
        echo "  SKIP (parse): $base"
        ;;
      SKIP_UNSUPPORTED)
        echo "  SKIP (unsupported): $base"
        ;;
      TIMEOUT)
        echo "  TIMEOUT: $base"
        ;;
      CRASH\ *)
        echo "  FAIL (crash): $base — ${result#CRASH }"
        echo "--- Source code ($pyfile) ---"
        cat "$pyfile"
        echo "--- CBMC output ---"
        head -80 "${pyfile}.cbmc.out"
        echo "---"
        sem_failures=$((sem_failures + 1))
        ;;
      *)
        echo "  ?? $base: $result"
        ;;
    esac
  done
  failures=$((failures + sem_failures))
  echo "Semantic tests: $count processed, $sem_failures failures"
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

echo "CBMC Python front-end fuzz test"
echo "Seed: $SEED  N: $N  Mode: $MODE  Unrestricted: ${UNRESTRICTED:-no}"
echo "CBMC: $CBMC"
echo "CBMC --unwind: $CBMC_UNWIND   timeout: ${CBMC_TIMEOUT}s"
echo ""

case "$MODE" in
  syntax)   run_syntax_tests ;;
  semantic) run_semantic_tests ;;
  both)     run_syntax_tests; echo ""; run_semantic_tests ;;
  *)        echo "Unknown mode: $MODE" >&2; exit 1 ;;
esac

echo ""
if [ $failures -ne 0 ]; then
  echo "FAILED: $failures failure(s). Seed was: $SEED"
  echo "Reproduce: $0 $N $SEED $MODE ${UNRESTRICTED:-}"
  exit 1
fi
echo "ALL PASSED. Seed was: $SEED"
