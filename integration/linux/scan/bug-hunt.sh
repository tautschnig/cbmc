#!/usr/bin/env bash
#
# bug-hunt.sh — driver for the per-file-mode corpus scan with
# strict memory caps that ensure no run can consume more than
# 25 % of host memory in aggregate.
#
# ## Why this script (vs. running corpus-scan.sh directly)
#
# corpus-scan.sh fans out scan.py invocations via xargs -P; each
# scan.py invokes goto-cc / goto-instrument / cbmc with its own
# RLIMIT_AS (defaults to 24 GiB) and RLIMIT_CPU (default 900 s).
# With PARALLEL=4 that's a 96 GiB worst-case peak — fine on a
# big EC2 instance but unsafe if the run is left unattended on
# a smaller host.  This script:
#
#   1. Computes 25 % of the host's total memory.
#   2. Constrains each cbmc invocation via SCAN_MEMORY_LIMIT
#      to a small fraction of that cap (default 8 GiB).
#   3. Wraps the whole corpus-scan run inside a
#      `systemd-run --user --scope` with MemoryMax set to the
#      25 % cap.  systemd-run kills the scope cleanly if the
#      cap is exceeded — far less destructive than a host OOM.
#   4. Bounds PARALLEL and per-file timeout based on the cap so
#      worst-case peak peak (PARALLEL × per-cbmc) stays under
#      the cap by a healthy margin.
#
# ## Usage
#
#   ./bug-hunt.sh OUTDIR LINUX_TREE [CORPUS_MAX]
#
# ## Tunables (env vars)
#
#   PER_CBMC_MEM_GB       per-cbmc RLIMIT_AS in GiB (default 8)
#   PARALLEL              fan-out (default 4)
#   PER_FILE_TIMEOUT_S    per-file wall-clock cap (default 900)
#   AGG_CAP_PCT           aggregate cgroup cap as % of host
#                         memory (default 25)
#
# Output: OUTDIR/scan/ (per-file JSON + log) and OUTDIR/summary.txt.

set -eu

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
OUTDIR=${1:-/tmp/bug-hunt-$(date +%Y%m%d-%H%M%S)}
LINUX_TREE=${2:-${LINUX_TREE:-}}
CORPUS_MAX_ARG=${3:-${CORPUS_MAX:-50}}

if [[ -z $LINUX_TREE ]]; then
  echo "usage: $0 OUTDIR LINUX_TREE [CORPUS_MAX]" >&2
  exit 2
fi

if [[ ! -d $LINUX_TREE ]]; then
  echo "LINUX_TREE not a directory: $LINUX_TREE" >&2
  exit 2
fi

PER_CBMC_MEM_GB=${PER_CBMC_MEM_GB:-8}
PARALLEL_REQ=${PARALLEL:-4}
PER_FILE_TIMEOUT_S=${PER_FILE_TIMEOUT_S:-900}
AGG_CAP_PCT=${AGG_CAP_PCT:-25}

mkdir -p "$OUTDIR"

# Compute caps from host memory.
TOTAL_KB=$(awk 'NR==2 {print $2}' < <(free -k))
CAP_KB=$((TOTAL_KB * AGG_CAP_PCT / 100))
CAP_GB=$((CAP_KB / 1024 / 1024))
PER_CBMC_KB=$((PER_CBMC_MEM_GB * 1024 * 1024))

# Sanity: cumulative per-cbmc × parallel must not exceed the
# aggregate cap.  If it does, drop parallelism.
MAX_PARALLEL=$((CAP_KB / PER_CBMC_KB))
if (( MAX_PARALLEL < 1 )); then
  echo "PER_CBMC_MEM_GB=${PER_CBMC_MEM_GB} exceeds the ${AGG_CAP_PCT}% cap (${CAP_GB} GB)" >&2
  exit 2
fi
PARALLEL=$(( PARALLEL_REQ < MAX_PARALLEL ? PARALLEL_REQ : MAX_PARALLEL ))

echo "Host total memory:   $((TOTAL_KB / 1024 / 1024)) GB" | tee "$OUTDIR/limits.txt"
echo "Aggregate cap (${AGG_CAP_PCT}%): ${CAP_GB} GB"        | tee -a "$OUTDIR/limits.txt"
echo "Per-cbmc RLIMIT_AS:  ${PER_CBMC_MEM_GB} GiB"           | tee -a "$OUTDIR/limits.txt"
echo "Effective PARALLEL:  ${PARALLEL} (requested ${PARALLEL_REQ})" | tee -a "$OUTDIR/limits.txt"
echo "Worst-case peak:     $((PER_CBMC_MEM_GB * PARALLEL)) GiB" | tee -a "$OUTDIR/limits.txt"
echo "Per-file timeout:    ${PER_FILE_TIMEOUT_S} s"            | tee -a "$OUTDIR/limits.txt"
echo "CORPUS_MAX:          ${CORPUS_MAX_ARG}"                  | tee -a "$OUTDIR/limits.txt"
echo "LINUX_TREE:          ${LINUX_TREE}"                      | tee -a "$OUTDIR/limits.txt"
echo

# Build the corpus-scan invocation that runs inside the cgroup.
SCAN_OUTDIR="$OUTDIR/scan"
mkdir -p "$SCAN_OUTDIR"

# Per-cbmc memory cap is enforced inside scan.py's preexec_fn
# (RLIMIT_AS via SCAN_MEMORY_LIMIT) and inside scan-per-file.sh
# via _lib.sh's ulimit -v.  We do NOT set ulimit on the parent
# shell here: doing so leaks the cap into the systemd-run scope
# in unexpected ways and trips _lib.sh's `ulimit -v` ('cannot
# modify limit: Operation not permitted' under nested ulimit
# scopes).  Aggregate enforcement lives in the systemd-run
# --property=MemoryMax cgroup below.
:

# Run inside a systemd-run --user --scope with MemoryMax
# enforcing the aggregate cap as a cgroup limit.  When the cap
# is exceeded systemd kills the scope cleanly with a Failed
# state instead of letting the host OOM.
CMD=(
  systemd-run --user --scope --quiet
    --property=MemoryMax="${CAP_GB}G"
    --property=MemorySwapMax=0
  env
    "LINUX_TREE=${LINUX_TREE}"
    "SCAN_MEMORY_LIMIT=$((PER_CBMC_KB * 1024))"
    "SCAN_MEMORY_LIMIT_KB=${PER_CBMC_KB}"
    "SCAN_CPU_LIMIT=${PER_FILE_TIMEOUT_S}"
    "PARALLEL=${PARALLEL}"
    "CORPUS_MAX=${CORPUS_MAX_ARG}"
    "EXTRA_SCAN_ARGS=--per-file"
    "SCAN_FILE_TIMEOUT=${PER_FILE_TIMEOUT_S}"
  "$SCRIPT_DIR/corpus-scan.sh" "$SCAN_OUTDIR"
)

echo "=== launching corpus-scan inside systemd-run scope ==="
printf '  %s' "${CMD[@]}"; echo
echo

start=$(date +%s)
"${CMD[@]}" 2>&1 | tee "$OUTDIR/run.log"
rc=${PIPESTATUS[0]}
end=$(date +%s)

echo
echo "=== corpus-scan finished after $((end - start))s with rc=$rc ==="

# Extract the summary section into its own file for easy reading.
if [[ -f "$OUTDIR/run.log" ]]; then
  awk '/^=== summary ===$/,0' "$OUTDIR/run.log" > "$OUTDIR/summary.txt"
fi

echo "Per-call-site results:    $SCAN_OUTDIR/*.json"
echo "Run log:                  $OUTDIR/run.log"
echo "Summary:                  $OUTDIR/summary.txt"
echo "Limits:                   $OUTDIR/limits.txt"
exit "$rc"
