#!/usr/bin/env bash
#
# Regression driver for the kref_lifetime property module.
#
# One test: unit exercise of the ghost store + kref_live
# predicate.  Expect VERIFICATION SUCCESSFUL.

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
source "$SCRIPT_DIR/../../scan/_lib.sh"
cd -- "$SCRIPT_DIR"

fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "=== unit: kref_lifetime ghost + kref_live predicate ==="
run_gotocc kref_lifetime.c test_unit.c -o "$tmp/unit.gb"
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
  echo "kref_lifetime tests behaved as expected."
  exit 0
fi

echo >&2
echo "$fail case(s) did not match expectation." >&2
exit 1
