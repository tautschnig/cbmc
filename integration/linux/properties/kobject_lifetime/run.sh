#!/usr/bin/env bash
#
# Regression driver for the kobject_lifetime property module.
#
# One test: unit exercise of the ghost store + kobject_live
# predicate.  Expect VERIFICATION SUCCESSFUL — the reference
# implementation and the predicate agree on four canonical cases:
#   (1) untracked kobject reports not-live (safe default).
#   (2) init(usage=1) then put → not live.
#   (3) get/put balanced to zero → not live; any positive usage → live.
#   (4) NULL kobject → not live.
#
# Exit code 0 iff the outcome matches expectation.

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
source "$SCRIPT_DIR/../../scan/_lib.sh"
cd -- "$SCRIPT_DIR"

fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "=== unit: kobject_lifetime ghost + kobject_live predicate ==="
run_gotocc kobject_lifetime.c test_unit.c -o "$tmp/unit.gb"
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
  echo "kobject_lifetime tests behaved as expected."
  exit 0
fi

echo >&2
echo "$fail case(s) did not match expectation." >&2
exit 1
