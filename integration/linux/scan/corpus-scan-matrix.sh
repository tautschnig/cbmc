#!/usr/bin/env bash
#
# corpus-scan-matrix.sh — local mirror of the CI
# `corpus-scan-matrix` job.  Runs corpus-scan.sh against each of
# the four LTS kernel trees expected under $HOME and aggregates
# the per-kernel summaries into a single report.
#
# ## Usage
#
#   ./corpus-scan-matrix.sh [OUTDIR]
#
# ## Kernel tree layout
#
# Assumes the standard dev-host layout:
#
#   $HOME/linux_5_10
#   $HOME/linux_6_1
#   $HOME/linux_6_6
#   $HOME/linux_6_12
#
# Missing trees are skipped with a warning.  Present trees must
# already be configured (see scan/configure.sh).
#
# ## Tuning
#
#   CORPUS_MAX=N    cap per-kernel (default 60)
#   PARALLEL=N      fan-out inside each kernel's scan
#                   (default nproc/4, at least 1)
#
# ## Why a wrapper?
#
# The CI job runs each kernel in a parallel matrix row with its
# own runner.  Running all four kernels on one dev host would
# oversubscribe memory (each cbmc invocation can use a few GiB).
# This wrapper runs them serially so the host stays responsive
# and any crash points at a specific kernel.

set -eu

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
OUTDIR=${1:-/tmp/corpus-scan-matrix-$(date +%Y%m%d-%H%M%S)}
CORPUS_MAX=${CORPUS_MAX:-60}
# Inside-kernel parallelism: keep modest so one kernel's sweep
# doesn't hog the host.
PARALLEL_DEFAULT=$(( $(nproc) / 4 ))
if (( PARALLEL_DEFAULT < 1 )); then PARALLEL_DEFAULT=1; fi
PARALLEL=${PARALLEL:-$PARALLEL_DEFAULT}

mkdir -p "$OUTDIR"

# Keep this list in sync with the matrix in
# .github/workflows/integration-linux-regressions.yaml.
KERNELS=(
  '5.10:linux_5_10'
  '6.1:linux_6_1'
  '6.6:linux_6_6'
  '6.12:linux_6_12'
)

agg_summary="$OUTDIR/matrix-summary.txt"
: > "$agg_summary"

overall_rc=0
for row in "${KERNELS[@]}"; do
  IFS=':' read -r version dir <<< "$row"
  tree="$HOME/$dir"
  if [[ ! -d "$tree" ]]; then
    echo "  [skip] Linux $version — tree not found at $tree"
    echo >> "$agg_summary"
    echo "## Linux $version" >> "$agg_summary"
    echo "(skipped — tree not found at $tree)" >> "$agg_summary"
    continue
  fi

  kernel_outdir="$OUTDIR/$dir"
  mkdir -p "$kernel_outdir"
  summary_path="$kernel_outdir/summary.txt"

  echo
  echo "=== corpus-scan Linux $version (CORPUS_MAX=$CORPUS_MAX, PARALLEL=$PARALLEL) ==="

  set +e
  env LINUX_TREE="$tree" CORPUS_MAX="$CORPUS_MAX" PARALLEL="$PARALLEL" \
      "$SCRIPT_DIR/corpus-scan.sh" "$kernel_outdir" \
      | tee "$summary_path"
  rc=$?
  set -e

  echo >> "$agg_summary"
  echo "## Linux $version" >> "$agg_summary"
  echo '```' >> "$agg_summary"
  # Include only the summary portion (after the "=== summary ===" line).
  if grep -q "=== summary ===" "$summary_path"; then
    awk '/^=== summary ===$/,0' "$summary_path" >> "$agg_summary"
  else
    tail -40 "$summary_path" >> "$agg_summary"
  fi
  echo '```' >> "$agg_summary"

  if (( rc != 0 )); then
    echo "  [WARN] corpus-scan on $version returned rc=$rc" >&2
    overall_rc=1
  fi
done

echo
echo "=== matrix summary: $agg_summary ==="
cat "$agg_summary"

exit "$overall_rc"
