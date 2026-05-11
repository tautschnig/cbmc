#!/usr/bin/env bash
#
# Regression driver for scan.py.  Two cases:
#
#   1.  Run scan.py against a property-module-native harness
#       (properties/aead/test_copyfail.c).  Expected: one module
#       ("aead") with two prefilter hits and cbmc_status == "failed";
#       scan.py should exit 1.
#
#   2.  Run scan.py against a real kernel source file
#       (crypto/algif_aead.c under $LINUX_TREE, if present).
#       After LIM-009 was resolved, the expected state on the
#       vulnerable Linux 5.10 `_aead_recvmsg` is `failed`, with the
#       `precondition.3` (sgl_all_user_writable) assertion violated
#       at line 280.
#
#   3.  Meta-regression on the vacuity guardrails: deliberately
#       break the harness to call the unmangled `_aead_recvmsg`
#       (the LIM-009 bug) and confirm scan.py reports
#       `cbmc_status: "vacuity-risk"` instead of silently
#       succeeding or failing.
#
# Exit code 0 iff all cases behave as expected.

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
source "$SCRIPT_DIR/_lib.sh"
cd -- "$SCRIPT_DIR"

SCAN="$SCRIPT_DIR/scan.py"
CVE_HARNESS="$SCRIPT_DIR/../properties/aead/test_copyfail.c"

fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "=== case 1: property-module-native harness (test_copyfail.c) ==="
set +e
"$SCAN" "$CVE_HARNESS" --json "$tmp/case1.json" > "$tmp/case1.out" 2>&1
rc=$?
set -e
if [[ $rc -eq 1 ]] && \
   grep -q '"cbmc_status": "failed"' "$tmp/case1.json" && \
   grep -q 'aead_request_set_crypt.precondition' "$tmp/case1.out"; then
  echo "  [ok] exit 1, cbmc_status=failed, precondition assertion named"
else
  echo "  [FAIL] expected exit 1 + cbmc_status=failed + precondition named" >&2
  echo "         actual rc=$rc; last 20 lines of output:" >&2
  tail -20 "$tmp/case1.out" | sed 's/^/         /' >&2
  fail=$((fail + 1))
fi

echo
echo "=== case 2: real kernel source (crypto/algif_aead.c) ==="
LINUX_TREE=${LINUX_TREE:-/home/ubuntu/linux_5_10}
KERNEL_C="$LINUX_TREE/crypto/algif_aead.c"
if [[ ! -f $KERNEL_C ]]; then
  echo "  [skip] no kernel tree at $LINUX_TREE (set LINUX_TREE to override)"
else
  set +e
  LINUX_TREE="$LINUX_TREE" "$SCAN" "$KERNEL_C" --json "$tmp/case2.json" > "$tmp/case2.out" 2>&1
  rc=$?
  set -e
  # LIM-009 is now RESOLVED: the scan drives CBMC through the full
  # `_aead_recvmsg` body and reports `cbmc_status: "failed"` on the
  # vulnerable 5.10 tree, naming the
  # `__CPROVER_file_local_aead_h_aead_request_set_crypt.precondition.3`
  # (sgl_all_user_writable) contract violation at line 280.  Anything
  # weaker is a regression.
  if [[ $rc -eq 1 ]] && \
     grep -q '"line": 280' "$tmp/case2.json" && \
     grep -q '"cbmc_status": "failed"' "$tmp/case2.json" && \
     grep -q 'precondition.3' "$tmp/case2.json"; then
    echo "  [ok] exit 1, line 280 hit, cbmc_status=failed, precondition.3 named"
  else
    echo "  [FAIL] expected rc 1 + line-280 hit + cbmc_status=failed + precondition.3" >&2
    echo "         actual rc=$rc; last 30 lines of output:" >&2
    tail -30 "$tmp/case2.out" | sed 's/^/         /' >&2
    fail=$((fail + 1))
  fi
fi

echo
echo "=== case 3: vacuity guardrail on crypto/algif_aead.c ==="
# Meta-regression: deliberately break the harness so it calls the
# unmangled _aead_recvmsg (the LIM-009 bug).  scan.py must report
# `cbmc_status: "vacuity-risk"`, not a silent success or failure.
if [[ ! -f $KERNEL_C ]]; then
  echo "  [skip] no kernel tree at $LINUX_TREE"
else
  harness="$SCRIPT_DIR/adapters/aead_kernel_harness.c"
  cp "$harness" "$tmp/harness.orig.c"
  python3 -c "
p = '$harness'
s = open(p).read()
s2 = s.replace(
    '__CPROVER_file_local_algif_aead_c__aead_recvmsg(',
    '_aead_recvmsg(')
open(p, 'w').write(s2)
"
  set +e
  LINUX_TREE="$LINUX_TREE" "$SCAN" "$KERNEL_C" --json "$tmp/case3.json" > "$tmp/case3.out" 2>&1
  rc=$?
  set -e
  cp "$tmp/harness.orig.c" "$harness"  # restore before checking
  if grep -q '"cbmc_status": "vacuity-risk"' "$tmp/case3.json"; then
    echo "  [ok] broken harness caught as vacuity-risk"
  else
    echo "  [FAIL] expected cbmc_status=vacuity-risk; guardrail did not fire" >&2
    echo "         actual rc=$rc; last 15 lines of output:" >&2
    tail -15 "$tmp/case3.out" | sed 's/^/         /' >&2
    fail=$((fail + 1))
  fi
fi

if [[ $fail -eq 0 ]]; then
  echo
  echo "scan.py regressions passed."
  exit 0
fi

echo >&2
echo "$fail case(s) did not match expectation." >&2
exit 1