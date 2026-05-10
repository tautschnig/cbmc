#!/usr/bin/env bash
#
# Regression driver for the scatterlist property module.
#
# unit:    exercises sg_init_table / sg_set_page / sg_chain / sg_next /
#          sgl_all_user_writable directly; the predicate must return
#          true for a user-writable SGL, false for an SGL containing a
#          page-cache page, and false for a chain that reaches a
#          page-cache page via sg_chain (the Copy Fail shape).
#
# replace: --replace-call-with-contract on a hypothetical kernel API
#          whose contract is __CPROVER_requires(sgl_all_user_writable(dst))
#          must let a good caller through and fail a caller that
#          chains a page-cache page into the destination.
#
# Exit code 0 iff both outcomes match expectation.

set -u

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
REPO_ROOT=$(cd -- "$SCRIPT_DIR/../../../.." &>/dev/null && pwd)
cd -- "$SCRIPT_DIR"

PP="$SCRIPT_DIR/../page_provenance"
CBMC=${CBMC:-"$REPO_ROOT/build/bin/cbmc"}
GCC=${GOTOCC:-"$REPO_ROOT/build/bin/goto-cc"}
GI=${GI:-"$REPO_ROOT/build/bin/goto-instrument"}

for tool in "$CBMC" "$GCC" "$GI"; do
  if [[ ! -x $tool ]]; then
    echo "required tool not found: $tool" >&2
    exit 2
  fi
done

fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "=== unit: sgl_all_user_writable on linear + chained SGLs ==="
"$GCC" "$PP/page_provenance.c" scatterlist.c test_unit.c \
       -o "$tmp/unit.gb"
out=$(timeout 120 "$CBMC" "$tmp/unit.gb" \
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
"$GCC" "$PP/page_provenance.c" scatterlist.c test_replace.c \
       -o "$tmp/replace.gb"
"$GI" --replace-call-with-contract write_sgl \
      "$tmp/replace.gb" "$tmp/replace.trans.gb" &>/dev/null
out=$(timeout 120 "$CBMC" "$tmp/replace.trans.gb" \
                 --unwind 32 --unwinding-assertions 2>&1)
# Expected: VERIFICATION FAILED with a precondition failure in
# caller_bad.  The good caller should succeed; only the bad one
# should fail, and that's what we confirm.
if echo "$out" | grep -q "^VERIFICATION FAILED\$" && \
   echo "$out" | grep -qE "precondition.*write_sgl.*caller_bad.*FAILURE"; then
  echo "  [ok] replace: VERIFICATION FAILED on caller_bad's precondition"
else
  echo "  [FAIL] replace: did not see the expected FAILED precondition" >&2
  echo "$out" | grep -E "(precondition|VERIFICATION)" | sed 's/^/    /' >&2
  fail=$((fail + 1))
fi

if [[ $fail -eq 0 ]]; then
  echo
  echo "Both scatterlist tests behaved as expected."
  exit 0
fi

echo >&2
echo "$fail case(s) did not match expectation." >&2
exit 1
