#!/usr/bin/env bash
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
source "$SCRIPT_DIR/../../scan/_lib.sh"
cd -- "$SCRIPT_DIR"

fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "=== unit: null_after_alloc ghost + null_check_done ==="
run_gotocc null_after_alloc.c test_unit.c -o "$tmp/unit.gb"
out=$(run_cbmc "$tmp/unit.gb" --unwind 32 --unwinding-assertions 2>&1)
if echo "$out" | grep -q "^VERIFICATION SUCCESSFUL\$"; then
  echo "  [ok] unit: VERIFICATION SUCCESSFUL"
else
  echo "  [FAIL] unit:" >&2
  echo "$out" | tail -20 | sed 's/^/    /' >&2
  fail=$((fail + 1))
fi

if [[ $fail -eq 0 ]]; then
  echo
  echo "null_after_alloc tests behaved as expected."
  exit 0
fi

exit 1
