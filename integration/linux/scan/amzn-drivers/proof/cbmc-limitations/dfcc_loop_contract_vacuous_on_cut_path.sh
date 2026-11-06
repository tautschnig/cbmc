#!/bin/bash
# A loop contract reports SUCCESS although a bodyless callee inside the loop
# body cut every path through it.  The contract verdict is vacuous but is
# printed indistinguishably from a real proof.
set -u
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
cd "$(mktemp -d)"
"$BIN/goto-cc" "$HERE/dfcc_loop_contract_vacuous_on_cut_path.c" -o r.gb 2>/dev/null
"$BIN/goto-instrument" --remove-function-body helper r.gb r_rm.gb >/dev/null 2>&1
"$BIN/goto-instrument" --no-malloc-may-fail --dfcc main --apply-loop-contracts \
  r_rm.gb r_lc.gb >/dev/null 2>&1
echo "== loop-contract verdicts with the body path cut =="
( ulimit -v 8000000; timeout 300 "$BIN/cbmc" r_lc.gb --no-standard-checks 2>&1 ) \
  | grep -E "loop_invariant|loop_assigns|loop_decreases|undefined function|^VERIFICATION" \
  | sed 's/^/  /'
echo
echo "Expected, and the problem: every loop-contract property SUCCESS, with"
echo "only the callee's own 'undefined function should be unreachable' failing."
