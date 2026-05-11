#!/usr/bin/env bash
#
# Regression driver for the pipe_buffer property module.
#
# One test for now: unit exercise of the ghost store + predicate.
#   Expect VERIFICATION SUCCESSFUL.  The reference implementation
#   and the predicate agree on the four canonical cases:
#     (1) flags=0 is safe.
#     (2) populated + CAN_MERGE is safe.
#     (3) taken-over + CAN_MERGE is UNSAFE (Dirty Pipe signal).
#     (4) taken-over + flags cleared is safe (fix direction).
#
# Exit code 0 iff the outcome matches expectation.

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
source "$SCRIPT_DIR/../../scan/_lib.sh"
cd -- "$SCRIPT_DIR"

fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "=== unit: pipe_buffer ghost + merge-safe predicate ==="
run_gotocc pipe_buffer.c test_unit.c -o "$tmp/unit.gb"
out=$(run_cbmc "$tmp/unit.gb" --unwind 32 --unwinding-assertions 2>&1)
if echo "$out" | grep -q "^VERIFICATION SUCCESSFUL\$"; then
  echo "  [ok] unit: VERIFICATION SUCCESSFUL"
else
  echo "  [FAIL] unit: did not see VERIFICATION SUCCESSFUL" >&2
  echo "$out" | tail -20 | sed 's/^/    /' >&2
  fail=$((fail + 1))
fi

if [[ $fail -eq 0 ]]; then
  echo
  echo "pipe_buffer tests behaved as expected."
  exit 0
fi

echo >&2
echo "$fail case(s) did not match expectation." >&2
exit 1
