#!/usr/bin/env bash
#
# Regression driver for the of_node_lifetime property module.
#
# One test: unit exercise of the ghost store + of_node_live
# predicate.  Expect VERIFICATION SUCCESSFUL.

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
source "$SCRIPT_DIR/../../scan/_lib.sh"
cd -- "$SCRIPT_DIR"

fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "=== unit: of_node_lifetime ghost + of_node_live predicate ==="
run_gotocc of_node_lifetime.c test_unit.c -o "$tmp/unit.gb"
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
  echo "of_node_lifetime tests behaved as expected."
  exit 0
fi

echo >&2
echo "$fail case(s) did not match expectation." >&2
exit 1
