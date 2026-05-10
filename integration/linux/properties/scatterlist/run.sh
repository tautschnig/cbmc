#!/usr/bin/env bash
#
# Regression driver for the scatterlist property module.
#
# unit:    exercises sg_init_table / sg_set_page / sg_chain / sg_next /
#          sgl_all_user_writable directly; predicate must return true
#          for user-writable SGL, false for in-place page-cache, false
#          for a chain into page-cache (Copy Fail shape).
#
# replace: --replace-call-with-contract on a hypothetical kernel API
#          whose contract is __CPROVER_requires(sgl_all_user_writable(dst))
#          must let a good caller through and fail a bad one.
#
# enforce: --dfcc + --enforce-contract proves the reference
#          implementations of sg_init_table, sg_set_page, sg_chain,
#          sg_unmark_end match their frame conditions.
#
# Exit code 0 iff all outcomes match expectation.

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
source "$SCRIPT_DIR/../../scan/_lib.sh"
cd -- "$SCRIPT_DIR"

PP="$SCRIPT_DIR/../page_provenance"

fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "=== unit: sgl_all_user_writable on linear + chained SGLs ==="
run_gotocc "$PP/page_provenance.c" scatterlist.c test_unit.c -o "$tmp/unit.gb"
out=$(CBMC_TIMEOUT=120 run_cbmc "$tmp/unit.gb" \
                 --unwind 32 --unwinding-assertions 2>&1)
if echo "$out" | grep -q "^VERIFICATION SUCCESSFUL\$"; then
  echo "  [ok] unit: VERIFICATION SUCCESSFUL"
else
  echo "  [FAIL] unit: did not see VERIFICATION SUCCESSFUL" >&2
  echo "$out" | tail -25 | sed 's/^/    /' >&2
  fail=$((fail + 1))
fi

echo
echo "=== replace: --replace-call-with-contract write_sgl ==="
run_gotocc "$PP/page_provenance.c" scatterlist.c test_replace.c \
           -o "$tmp/replace.gb"
run_gi --replace-call-with-contract write_sgl \
       "$tmp/replace.gb" "$tmp/replace.trans.gb" &>/dev/null
out=$(CBMC_TIMEOUT=120 run_cbmc "$tmp/replace.trans.gb" \
                 --unwind 32 --unwinding-assertions 2>&1)
if echo "$out" | grep -q "^VERIFICATION FAILED\$" && \
   echo "$out" | grep -qE "precondition.*write_sgl.*caller_bad.*FAILURE"; then
  echo "  [ok] replace: VERIFICATION FAILED on caller_bad's precondition"
else
  echo "  [FAIL] replace: did not see the expected FAILED precondition" >&2
  echo "$out" | grep -E "(precondition|VERIFICATION)" | sed 's/^/    /' >&2
  fail=$((fail + 1))
fi

echo
echo "=== enforce: --dfcc + --enforce-contract (one per function) ==="
for fn in sg_init_table sg_set_page sg_chain sg_unmark_end; do
  up=$(echo "$fn" | tr '[:lower:]' '[:upper:]')
  run_gotocc -DENFORCE_$up -DPAGE_PROV_TABLE_SIZE=2 \
             "$PP/page_provenance.c" scatterlist.c test_enforce.c \
             -o "$tmp/enforce_$fn.gb"
  run_gi --dfcc main --enforce-contract "$fn" \
         "$tmp/enforce_$fn.gb" "$tmp/enforce_$fn.trans.gb" &>/dev/null
  out=$(CBMC_TIMEOUT=180 run_cbmc "$tmp/enforce_$fn.trans.gb" --unwind 8 \
                                   --unwinding-assertions 2>&1)
  if echo "$out" | grep -q "^VERIFICATION SUCCESSFUL\$"; then
    echo "  [ok] enforce $fn: VERIFICATION SUCCESSFUL"
  else
    echo "  [FAIL] enforce $fn: did not see VERIFICATION SUCCESSFUL" >&2
    echo "$out" | grep -E "FAILURE|VERIFICATION" | head -5 | sed 's/^/    /' >&2
    fail=$((fail + 1))
  fi
done

if [[ $fail -eq 0 ]]; then
  echo
  echo "All scatterlist tests behaved as expected."
  exit 0
fi

echo >&2
echo "$fail case(s) did not match expectation." >&2
exit 1
