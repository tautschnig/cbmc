#!/usr/bin/env bash
#
# Regression driver for the aead property module.
#
# The single test is test_copyfail.c, compiled twice: once as the
# vulnerable shape of _aead_recvmsg (sg_chain(rx, _, tx); set_crypt
# with src == dst == rx), once as the fixed shape (distinct src/dst,
# no sg_chain).  Both are transformed with
# --replace-call-with-contract aead_request_set_crypt so that the
# contract's __CPROVER_requires(sgl_all_user_writable(dst)) is
# checked at the call site.
#
#   vuln  -> expect VERIFICATION FAILED (precondition fails)
#   fixed -> expect VERIFICATION SUCCESSFUL
#
# Exit code 0 iff both outcomes match expectation.

set -u

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
REPO_ROOT=$(cd -- "$SCRIPT_DIR/../../../.." &>/dev/null && pwd)
cd -- "$SCRIPT_DIR"

PP="$SCRIPT_DIR/../page_provenance"
SGL="$SCRIPT_DIR/../scatterlist"
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

build() {
  local name=$1
  shift
  "$GCC" "$PP/page_provenance.c" "$SGL/scatterlist.c" aead.c test_copyfail.c \
         "$@" -o "$tmp/$name.gb"
  "$GI" --replace-call-with-contract aead_request_set_crypt \
        "$tmp/$name.gb" "$tmp/$name.trans.gb" &>/dev/null
}

echo "=== vuln: sg_chain(rx, _, tx); set_crypt(req, rx, rx, ...) ==="
build vuln
out=$(timeout 120 "$CBMC" "$tmp/vuln.trans.gb" \
                 --unwind 32 --unwinding-assertions 2>&1)
if echo "$out" | grep -q "^VERIFICATION FAILED\$" && \
   echo "$out" | grep -qE "precondition.*aead_request_set_crypt.*FAILURE"; then
  echo "  [ok] vuln: VERIFICATION FAILED on aead_request_set_crypt precondition"
else
  echo "  [FAIL] vuln: did not see the expected FAILED precondition" >&2
  echo "$out" | grep -E "(precondition|VERIFICATION)" | sed 's/^/    /' >&2
  fail=$((fail + 1))
fi

echo
echo "=== fix: set_crypt(req, tx, rx, ...) with no sg_chain ==="
build fix -DFIXED
out=$(timeout 120 "$CBMC" "$tmp/fix.trans.gb" \
                 --unwind 32 --unwinding-assertions 2>&1)
if echo "$out" | grep -q "^VERIFICATION SUCCESSFUL\$"; then
  echo "  [ok] fix: VERIFICATION SUCCESSFUL"
else
  echo "  [FAIL] fix: did not see VERIFICATION SUCCESSFUL" >&2
  echo "$out" | tail -25 | sed 's/^/    /' >&2
  fail=$((fail + 1))
fi

if [[ $fail -eq 0 ]]; then
  echo
  echo "aead copyfail test behaved as expected on both variants."
  exit 0
fi

echo >&2
echo "$fail case(s) did not match expectation." >&2
exit 1
