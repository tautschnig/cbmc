#!/bin/bash
# DFCC replaces a bodyless callee with `assert(false); assume(false);`
# (src/goto-instrument/contracts/dynamic-frames/dfcc.cpp:491 and
# dfcc_instrument.cpp:440, both selecting "assert-false-assume-false").
#
# It DOES raise a property failure, so the user is told something.  But the
# assume(false) cuts the path, so every property after the call becomes
# vacuously SUCCESS -- including a deliberately false assert(0).  Plain CBMC
# behaves differently: it reports "no body for callee" and carries on with a
# nondeterministic return, so downstream properties stay meaningful.
#
# Expected output:
#   plain CBMC : no body for callee ... FAILURE   AND downstream ... FAILURE
#   with DFCC  : undefined function should be unreachable ... FAILURE
#                downstream ... SUCCESS      <-- assert(0) reported as SUCCESS
#
# The practical hazard is the ratio: one failure you may read as a known
# abstraction, next to arbitrarily many SUCCESSes that mean nothing.  A proof
# needs an independent reachability witness to notice (see VACUITY_CHECK in
# ../harness_tx_callees.c).
set -eu
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
cd "$T"
"$BIN/goto-cc" "$HERE/dfcc_bodyless_callee_cuts_path.c" -o nb.gb 2>/dev/null

echo "=== plain CBMC (no DFCC) ==="
( ulimit -v 16000000; timeout 120 "$BIN/cbmc" nb.gb 2>&1 |
    grep -E "no body|downstream|^VERIFICATION" ) || true
echo
echo "=== with DFCC ==="
"$BIN/goto-instrument" --dfcc main nb.gb nb_dfcc.gb >/dev/null 2>&1
( ulimit -v 16000000; timeout 120 "$BIN/cbmc" nb_dfcc.gb 2>&1 |
    grep -E "unreachable|downstream|^VERIFICATION" ) || true
