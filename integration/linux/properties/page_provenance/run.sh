#!/usr/bin/env bash
#
# Regression driver for the page_provenance property module.
#
# Two independent tests:
#
#   unit: the store/read-back invariant holds in the reference
#         implementation.
#
#       goto-cc page_provenance.c test_unit.c
#       cbmc ...    -> expect SUCCESSFUL
#
#   replace: --replace-call-with-contract on an API with a
#            page_prov_of(p) requires clause propagates the obligation
#            to call sites; the "good" caller satisfies it, the "bad"
#            one does not.
#
#       goto-cc page_provenance.c test_replace.c
#       goto-instrument --replace-call-with-contract write_to_page ...
#       cbmc ...    -> expect FAILED on the bad caller's precondition
#
# Exit code 0 iff both outcomes match expectation.

set -u

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
REPO_ROOT=$(cd -- "$SCRIPT_DIR/../../../.." &>/dev/null && pwd)
cd -- "$SCRIPT_DIR"

CBMC=${CBMC:-"$REPO_ROOT/build/bin/cbmc"}
GCC=${GOTOCC:-"$REPO_ROOT/build/bin/goto-cc"}
GI=${GI:-"$REPO_ROOT/build/bin/goto-instrument"}

for tool in "$CBMC" "$GCC" "$GI"; do
  if [[ ! -x $tool ]]; then
    echo "required tool not found: $tool" >&2
    echo "set CBMC/GOTOCC/GI, or build the tree first." >&2
    exit 2
  fi
done

fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "=== unit: store / read-back invariant ==="
"$GCC" page_provenance.c test_unit.c -o "$tmp/unit.gb"
out=$(timeout 60 "$CBMC" "$tmp/unit.gb" --unwind 32 --unwinding-assertions 2>&1)
if echo "$out" | grep -q "^VERIFICATION SUCCESSFUL\$"; then
  echo "  [ok] unit: VERIFICATION SUCCESSFUL"
else
  echo "  [FAIL] unit: did not see VERIFICATION SUCCESSFUL" >&2
  echo "$out" | tail -20 | sed 's/^/    /' >&2
  fail=$((fail + 1))
fi

echo
echo "=== replace: --replace-call-with-contract write_to_page ==="
"$GCC" page_provenance.c test_replace.c -o "$tmp/replace.gb"
"$GI" --replace-call-with-contract write_to_page \
      "$tmp/replace.gb" "$tmp/replace.trans.gb" &>/dev/null
out=$(timeout 60 "$CBMC" "$tmp/replace.trans.gb" --unwind 32 \
                                                 --unwinding-assertions 2>&1)
# We expect VERIFICATION FAILED, with the failing assertion being the
# write_to_page precondition check inside caller_bad.
if echo "$out" | grep -q "^VERIFICATION FAILED\$" && \
   echo "$out" | grep -qE "precondition.*write_to_page.*caller_bad.*FAILURE"; then
  echo "  [ok] replace: VERIFICATION FAILED on caller_bad's precondition"
else
  echo "  [FAIL] replace: did not see the expected FAILED precondition" >&2
  echo "$out" | grep -E "(precondition|VERIFICATION)" | sed 's/^/    /' >&2
  fail=$((fail + 1))
fi

if [[ $fail -eq 0 ]]; then
  echo
  echo "Both page_provenance tests behaved as expected."
  exit 0
fi

echo >&2
echo "$fail case(s) did not match expectation." >&2
exit 1
