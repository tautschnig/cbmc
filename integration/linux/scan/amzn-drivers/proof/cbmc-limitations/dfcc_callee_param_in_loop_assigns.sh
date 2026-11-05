#!/bin/bash
# A callee's own PARAMETER, assigned in the callee's loop condition, is
# reported as not assignable against the CALLER's loop assigns clause.
#
#   dfcc_callee_locals_pass.c        callee writes its locals, no loop -> SUCCESS
#   dfcc_callee_param_in_loop_assigns.c  callee has a loop and assigns its
#                                    parameter in the loop condition -> FAILURE
#
# Expected:
#   [callee.assigns.1] Check that m is assignable: FAILURE   <-- m is the
#                                    callee's parameter, not the caller's frame
#   [callee.assigns.2] Check that acc is assignable: SUCCESS <-- plain local ok
#
# This is what blocks proving the ENA TX path with the real ena_tx_map_mbuf
# body under an outer loop contract: ena_tx_map_mbuf walks the mbuf chain with
# `while ((mbuf = mbuf->next) != NULL)`, assigning its own parameter, and DFCC
# demands that the caller's assigns clause cover it -- which the caller cannot
# express, since callee locals are not nameable from the caller's contract.
set -eu
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
cd "$T"
for f in dfcc_callee_locals_pass dfcc_callee_param_in_loop_assigns; do
  "$BIN/goto-cc" "$HERE/$f.c" -o a.gb 2>/dev/null
  "$BIN/goto-instrument" --no-malloc-may-fail --dfcc main --apply-loop-contracts \
      a.gb a_lc.gb >/dev/null 2>&1
  echo "== $f"
  ( ulimit -v 16000000
    timeout 300 "$BIN/cbmc" a_lc.gb --no-standard-checks \
      --unwindset callee.0:6 --unwinding-assertions 2>&1 |
      grep -E "callee\.assigns|^VERIFICATION" | sed 's/^/  /' ) || true
done
