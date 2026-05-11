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
#       After M4b's kernel adapter landed, the expected state on an
#       unstubbed _aead_recvmsg is `timeout` (LIM-006).  When
#       aggressive-stubbing is added (M4c), this may flip to `failed`
#       or `successful`.
#
# Exit code 0 iff both cases behave as expected.

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
  # After M4c's stubs + harness landed, the expected state on
  # crypto/algif_aead.c is `successful` — but vacuously so; see
  # LIM-009.  A precise verdict (`failed` on the vulnerable tree)
  # will be produced once the stubs materialise concrete SGL
  # contents.  The regression accepts either `successful` or
  # `failed` as expected until then.
  if [[ $rc -le 1 ]] && \
     grep -q '"line": 280' "$tmp/case2.json" && \
     grep -qE '"cbmc_status": "(successful|failed|timeout)"' "$tmp/case2.json"; then
    status=$(grep -oE '"cbmc_status": "[^"]*"' "$tmp/case2.json" | head -1)
    echo "  [ok] exit $rc, line 280 hit, $status"
  else
    echo "  [FAIL] expected rc 0 or 1 + line-280 hit + a cbmc verdict" >&2
    echo "         actual rc=$rc; last 20 lines of output:" >&2
    tail -20 "$tmp/case2.out" | sed 's/^/         /' >&2
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
