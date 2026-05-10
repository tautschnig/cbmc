#!/usr/bin/env bash
#
# Regression driver for the aead property module.
#
# copyfail: test_copyfail.c, compiled twice: vulnerable shape
#           (sg_chain of src into dst; set_crypt with src == dst == rx)
#           and fixed shape (distinct src/dst, no sg_chain).  Both
#           transformed with --replace-call-with-contract
#           aead_request_set_crypt.
#             vulnerable -> expect VERIFICATION FAILED  (precondition)
#             fixed      -> expect VERIFICATION SUCCESSFUL.
#
# enforce:  --dfcc + --enforce-contract proves the reference
#           implementations of aead_request_set_tfm,
#           aead_request_set_ad, aead_request_set_crypt each match
#           their frame condition.
#
# Exit code 0 iff all outcomes match expectation.

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
source "$SCRIPT_DIR/../../scan/_lib.sh"
cd -- "$SCRIPT_DIR"

PP="$SCRIPT_DIR/../page_provenance"
SGL="$SCRIPT_DIR/../scatterlist"

fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

build() {
  local name=$1
  shift
  run_gotocc "$PP/page_provenance.c" "$SGL/scatterlist.c" aead.c test_copyfail.c \
             "$@" -o "$tmp/$name.gb"
  run_gi --replace-call-with-contract aead_request_set_crypt \
         "$tmp/$name.gb" "$tmp/$name.trans.gb" &>/dev/null
}

echo "=== vuln: sg_chain(rx, _, tx); set_crypt(req, rx, rx, ...) ==="
build vuln
out=$(CBMC_TIMEOUT=120 run_cbmc "$tmp/vuln.trans.gb" \
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
out=$(CBMC_TIMEOUT=120 run_cbmc "$tmp/fix.trans.gb" \
                 --unwind 32 --unwinding-assertions 2>&1)
if echo "$out" | grep -q "^VERIFICATION SUCCESSFUL\$"; then
  echo "  [ok] fix: VERIFICATION SUCCESSFUL"
else
  echo "  [FAIL] fix: did not see VERIFICATION SUCCESSFUL" >&2
  echo "$out" | tail -25 | sed 's/^/    /' >&2
  fail=$((fail + 1))
fi

echo
echo "=== enforce: --dfcc + --enforce-contract (one per function) ==="
for fn in aead_request_set_tfm aead_request_set_ad aead_request_set_crypt; do
  up=$(echo "$fn" | tr '[:lower:]' '[:upper:]')
  run_gotocc -DENFORCE_$up -DPAGE_PROV_TABLE_SIZE=2 \
             "$PP/page_provenance.c" "$SGL/scatterlist.c" aead.c test_enforce.c \
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
  echo "All aead tests behaved as expected."
  exit 0
fi

echo >&2
echo "$fail case(s) did not match expectation." >&2
exit 1
