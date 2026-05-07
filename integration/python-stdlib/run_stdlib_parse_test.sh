#!/usr/bin/env bash
#
# Run the CBMC Python front-end against a curated set of stdlib modules
# and compare the outcome against a committed baseline.
#
# See ./README.md for details.

set -u

here="$(cd "$(dirname "$0")" && pwd)"
CBMC="${CBMC:-$HOME/cbmc-python.git/build/bin/cbmc}"
# Default stdlib: probe the system python3 if no override is given.
if [ -z "${STDLIB:-}" ]; then
  if command -v python3 >/dev/null 2>&1; then
    STDLIB=$(python3 -c "import sysconfig; print(sysconfig.get_paths()['stdlib'])" 2>/dev/null || true)
  fi
  STDLIB="${STDLIB:-/usr/lib/python3.12}"
fi
TIMEOUT="${TIMEOUT:-30}"
MEM_KB="${MEM_KB:-2000000}"

update_baseline=0
verbose=0
for arg in "$@"; do
  case "$arg" in
    --update-baseline) update_baseline=1 ;;
    -v|--verbose)      verbose=1 ;;
    -h|--help)
      sed -n '2,10p' "$0"
      exit 0 ;;
    *)
      echo "Unknown argument: $arg" >&2
      exit 2 ;;
  esac
done

if [ ! -x "$CBMC" ]; then
  echo "Error: cbmc binary not found or not executable: $CBMC" >&2
  echo "Set CBMC=/path/to/cbmc." >&2
  exit 2
fi

if [ ! -d "$STDLIB" ]; then
  echo "Error: stdlib directory not found: $STDLIB" >&2
  echo "Set STDLIB=/path/to/python/stdlib." >&2
  exit 2
fi

modules_file="$here/modules.txt"
baseline_file="$here/baseline.csv"
results_file="$(mktemp)"
trap 'rm -f "$results_file" "$results_file.log"' EXIT

echo "module,status,first_error_location" > "$results_file"

# Status categories:
#   ok       - front-end ingested the file (exit 0 or expected "exit 6"
#              when no goal was given; no crash; no timeout).
#   crash    - invariant violation / abort in the front-end.
#   timeout  - hit the wall-clock budget.
#   error    - some other non-zero exit code without crash or timeout.
#
# The baseline records the expected status. The test fails if the
# observed status is strictly worse than the baseline (ok > error >
# timeout = crash, where 'worse' means further from 'ok').

classify() {
  local rc="$1"
  local log="$2"
  if grep -q "invariant violation" "$log" 2>/dev/null; then
    echo "crash"
  elif [ "$rc" = "124" ]; then
    echo "timeout"
  elif [ "$rc" = "0" ] || [ "$rc" = "6" ] || [ "$rc" = "10" ]; then
    # 0   : verification successful (no properties to check).
    # 6   : verification failed (properties present but unprovable).
    # 10  : verification failed + unwinding assertion failure.
    # All of these indicate the front-end completed ingestion; whether
    # verification passed is not the concern of this parse-only test.
    echo "ok"
  else
    echo "error"
  fi
}

first_error_location() {
  local log="$1"
  grep -m1 -E "File: .*(/|^)src/.*\.(h|cpp):[0-9]+" "$log" \
    | sed -E 's/.*File: //; s/ function:.*//; s|^.*/src/|src/|; s/,/;/g' \
    | head -c 120
}

total=0
while IFS= read -r line; do
  line="${line%%#*}"
  line="${line#"${line%%[![:space:]]*}"}"
  line="${line%"${line##*[![:space:]]}"}"
  [ -z "$line" ] && continue
  mod="$line"
  path="$STDLIB/$mod"
  total=$((total + 1))

  if [ ! -f "$path" ]; then
    echo "$mod,missing,(file not found: $path)" >> "$results_file"
    continue
  fi

  ( ulimit -v "$MEM_KB" 2>/dev/null
    timeout "$TIMEOUT" "$CBMC" "$path" ) > "$results_file.log" 2>&1
  rc=$?
  status=$(classify "$rc" "$results_file.log")
  loc=$(first_error_location "$results_file.log")
  echo "$mod,$status,\"$loc\"" >> "$results_file"
  [ "$verbose" -eq 1 ] && echo "  $mod: $status (rc=$rc)"
done < "$modules_file"

echo
echo "Tested $total modules."

if [ "$update_baseline" -eq 1 ]; then
  cp "$results_file" "$baseline_file"
  echo "Baseline updated at $baseline_file."
  column -t -s, "$baseline_file"
  exit 0
fi

if [ ! -f "$baseline_file" ]; then
  echo "Error: baseline file not found at $baseline_file." >&2
  echo "Run with --update-baseline to create it." >&2
  exit 2
fi

# Compare observed vs baseline status only. The first_error_location
# column is informational.
status_rank() {
  case "$1" in
    ok)      echo 3 ;;
    error)   echo 2 ;;
    timeout) echo 1 ;;
    crash)   echo 0 ;;
    missing) echo 0 ;;
    *)       echo 0 ;;
  esac
}

regressions=0
improvements=0
while IFS=, read -r mod status _rest; do
  baseline_status=$(awk -F, -v m="$mod" '$1==m{print $2}' "$baseline_file")
  if [ -z "$baseline_status" ]; then
    echo "NEW    $mod: $status (not in baseline — treat as informational)"
    continue
  fi
  o=$(status_rank "$status")
  b=$(status_rank "$baseline_status")
  if [ "$o" -lt "$b" ]; then
    echo "REGR   $mod: $status (baseline: $baseline_status)"
    regressions=$((regressions + 1))
  elif [ "$o" -gt "$b" ]; then
    echo "IMPROV $mod: $status (baseline: $baseline_status)"
    improvements=$((improvements + 1))
  fi
done < <(tail -n +2 "$results_file")

echo
echo "Regressions:  $regressions"
echo "Improvements: $improvements"

if [ "$regressions" -gt 0 ]; then
  echo
  echo "Integration test FAILED: one or more modules regressed." >&2
  echo "If the regression is intentional, rerun with --update-baseline." >&2
  exit 1
fi

echo "Integration test PASSED."
exit 0
