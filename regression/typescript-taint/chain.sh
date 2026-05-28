#!/usr/bin/env bash
#
# Three-step taint-analysis chain for TypeScript programs:
#   1. cbmc --export-symex-ready-goto prog.gb prog.ts
#      — produces a goto binary from the TypeScript source.
#   2. goto-analyzer --taint <spec> --write-goto-binary prog-i.gb prog.gb
#      — instruments the goto binary with taint-flow assertions
#        according to the spec; lowers get_may/set_may to plain
#        boolean assertions and SKIP so the result is verifiable
#        by CBMC.
#   3. cbmc prog-i.gb
#      — verifies the instrumented assertions; a tainted path from
#        source to sink without sanitizer manifests as an assertion
#        failure with a concrete counterexample.
#
# Usage (invoked by test.pl with the two tool paths):
#   chain.sh <cbmc> <goto-analyzer> <prog.ts>

# Note: deliberately NOT using `set -e`. goto-analyzer returns
# 10 (verification-unsafe) when taint is detected, but we still
# want to run the final cbmc step which is the actual property we
# are verifying.

cbmc="$1"
goto_analyzer="$2"
input="$3"
shift 3

spec="$(dirname "$input")/taint.json"
base="$(dirname "$input")/$(basename "$input" .ts)"

"$cbmc" --export-symex-ready-goto "$base.gb" "$input" || {
  rc=$?
  echo "ERROR: cbmc --export-symex-ready-goto failed (exit $rc)"
  exit $rc
}

"$goto_analyzer" --taint "$spec" \
  --write-goto-binary "$base-i.gb" "$base.gb"
# Ignore goto-analyzer's exit code; it returns 10 when taint is
# detected, but we want CBMC to verify the lowered assertions.

if [ ! -f "$base-i.gb" ]; then
  echo "ERROR: goto-analyzer did not produce $base-i.gb"
  exit 1
fi

"$cbmc" "$@" "$base-i.gb"
