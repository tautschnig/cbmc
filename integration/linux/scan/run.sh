#!/usr/bin/env bash
#
# Regression driver for scan.py.  Four cases:
#
#   1.  Run scan.py against a property-module-native harness
#       (properties/aead/test_copyfail.c).  Expected: one module
#       ("aead") with two prefilter hits and cbmc_status == "failed";
#       scan.py should exit 1.
#
#   2.  Run scan.py against a real kernel source file
#       (crypto/algif_aead.c under $LINUX_TREE, if present) with
#       the default `--direction=vuln`.  Under the LIM-012 path-2
#       direct-call harness, expected: `cbmc_status == "failed"`,
#       precondition.3 (sgl_all_user_writable) violated in the
#       harness's vulnerable-shape branch; scan.py exits 1.
#
#   3.  Meta-regression on the vacuity guardrails: deliberately
#       break the harness by deleting the `aead_request_set_crypt`
#       call site and confirm scan.py reports
#       `cbmc_status: "vacuity-risk"` instead of silently
#       succeeding or failing.
#
#   4.  Fix-direction regression (LIM-010): run scan.py with
#       `--direction=fix` against the same real kernel source.
#       Expected: `cbmc_status == "successful"` and scan.py
#       exits 0.  This closes LIM-010 by demonstrating the
#       contract accepts a safe SGL shape (the direct-call
#       harness's fix branch) on the same pipeline the
#       vulnerable direction fires on.
#
#   5.  Real-kernel Dirty Pipe regression (CVE-2022-0847): run
#       scan.py against `lib/iov_iter.c` in both directions.
#       Vuln: `cbmc_status == "failed"`, precondition.2 named.
#       Fix:  `cbmc_status == "successful"`.  Demonstrates the
#       pipe_buffer kernel adapter works end-to-end.
#
#   6.  Real-kernel cred_lifetime regression (CVE-2026-23297
#       class): run scan.py against `fs/coredump.c` in both
#       directions.
#       Vuln: `cbmc_status == "failed"` with put_cred precondition
#       fired.  Fix: `cbmc_status == "successful"`.  Demonstrates
#       the third property module (cred_lifetime) works end-to-end
#       on unmodified kernel source.
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
  # LIM-009 RESOLVED + LIM-012 path 2: the scan drives CBMC via
  # the direct-call harness (scan/adapters/aead_kernel_direct_
  # harness.c) and reports `cbmc_status: "failed"` when the
  # vulnerable-shape branch is taken, naming the
  # `aead_request_set_crypt.precondition.3` (sgl_all_user_writable)
  # contract violation at the harness's call site.  The kernel
  # source is still compiled and linked so the required-bodies
  # guardrail continues to catch LIM-009-style linkage failures.
  # Anything weaker is a regression.
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
# Meta-regression: deliberately break the direct-call harness by
# deleting the `aead_request_set_crypt` call site.  scan.py must
# report `cbmc_status: "vacuity-risk"` because the vacuity probe
# will observe the trivially-false contract as SUCCESSFUL (no
# call site to fire on).
if [[ ! -f $KERNEL_C ]]; then
  echo "  [skip] no kernel tree at $LINUX_TREE"
else
  harness="$SCRIPT_DIR/adapters/aead_kernel_direct_harness.c"
  cp "$harness" "$tmp/harness.orig.c"
  python3 -c "
import re
p = '$harness'
s = open(p).read()
# Delete only the call sites (not the prototype declaration).
# The calls are the lines that start with the function name at
# the statement level — match lines beginning with 2 spaces and
# the function name to avoid eating the 'void aead_request_set_crypt('
# declaration a few lines above.
s2 = re.sub(
    r'^  aead_request_set_crypt\([^;]*;',
    '  (void)0;',
    s,
    flags=re.MULTILINE)
assert s2 != s, 'harness already contained no call site to delete'
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

echo
echo "=== case 4: fix-direction regression on crypto/algif_aead.c ==="
# LIM-010 RESOLVED (LIM-012 path 2): with `--direction=fix` the
# scan builds the direct-call harness's safe-shape branch and
# expects the contract to accept it, yielding
# `cbmc_status: "successful"` and exit 0.
if [[ ! -f $KERNEL_C ]]; then
  echo "  [skip] no kernel tree at $LINUX_TREE"
else
  set +e
  LINUX_TREE="$LINUX_TREE" "$SCAN" "$KERNEL_C" --direction=fix \
    --json "$tmp/case4.json" > "$tmp/case4.out" 2>&1
  rc=$?
  set -e
  if [[ $rc -eq 0 ]] && \
     grep -q '"cbmc_status": "successful"' "$tmp/case4.json"; then
    echo "  [ok] exit 0, cbmc_status=successful on fix direction"
  else
    echo "  [FAIL] expected rc 0 + cbmc_status=successful" >&2
    echo "         actual rc=$rc; last 20 lines of output:" >&2
    tail -20 "$tmp/case4.out" | sed 's/^/         /' >&2
    fail=$((fail + 1))
  fi
fi

echo
echo "=== case 5: real-kernel Dirty Pipe on lib/iov_iter.c ==="
# M5b: pipe_buffer kernel adapter end-to-end.  Runs the scan in
# both directions against Linux 5.10 `lib/iov_iter.c`:
#   - default --direction=vuln: prefilter fires on
#     copy_page_to_iter_pipe's take-over sites (lines 409/411/545/546);
#     CBMC on the direct-call harness reports
#     `cbmc_status: "failed"` with `pipe_buf_release.precondition.2`
#     named (pipe_buf_merge_safe(buf) == 1 violated).
#   - --direction=fix: same kernel source, harness built with
#     -DFIXED, cbmc_status: "successful".
PIPE_KERNEL_C="$LINUX_TREE/lib/iov_iter.c"
if [[ ! -f $PIPE_KERNEL_C ]]; then
  echo "  [skip] no $PIPE_KERNEL_C"
else
  # 5a: vulnerable direction
  set +e
  LINUX_TREE="$LINUX_TREE" "$SCAN" "$PIPE_KERNEL_C" \
    --json "$tmp/case5a.json" > "$tmp/case5a.out" 2>&1
  rc=$?
  set -e
  if [[ $rc -eq 1 ]] && \
     grep -q '"cbmc_status": "failed"' "$tmp/case5a.json" && \
     grep -q 'pipe_buf_release.precondition' "$tmp/case5a.json"; then
    echo "  [ok] 5a (vuln): exit 1, cbmc_status=failed, precondition named"
  else
    echo "  [FAIL] 5a expected rc 1 + cbmc_status=failed + precondition named" >&2
    echo "         actual rc=$rc; last 20 lines of output:" >&2
    tail -20 "$tmp/case5a.out" | sed 's/^/         /' >&2
    fail=$((fail + 1))
  fi

  # 5b: fix direction
  set +e
  LINUX_TREE="$LINUX_TREE" "$SCAN" "$PIPE_KERNEL_C" --direction=fix \
    --json "$tmp/case5b.json" > "$tmp/case5b.out" 2>&1
  rc=$?
  set -e
  if [[ $rc -eq 0 ]] && \
     grep -q '"cbmc_status": "successful"' "$tmp/case5b.json"; then
    echo "  [ok] 5b (fix):  exit 0, cbmc_status=successful"
  else
    echo "  [FAIL] 5b expected rc 0 + cbmc_status=successful" >&2
    echo "         actual rc=$rc; last 20 lines of output:" >&2
    tail -20 "$tmp/case5b.out" | sed 's/^/         /' >&2
    fail=$((fail + 1))
  fi
fi

echo
echo "=== case 6: real-kernel cred_lifetime on fs/coredump.c ==="
# Third property module end-to-end.  fs/coredump.c matches the
# cred_lifetime prefilter on its `put_cred(cred)` call inside the
# error path of do_coredump's prepare-creds sequence.
#   - default --direction=vuln: cbmc_status=failed; the
#     put_cred.precondition fires at the harness's second
#     put_cred call.
#   - --direction=fix: cbmc_status=successful.
CRED_KERNEL_C="$LINUX_TREE/fs/coredump.c"
if [[ ! -f $CRED_KERNEL_C ]]; then
  echo "  [skip] no $CRED_KERNEL_C"
else
  # 6a: vulnerable direction.
  set +e
  LINUX_TREE="$LINUX_TREE" "$SCAN" "$CRED_KERNEL_C" \
    --json "$tmp/case6a.json" > "$tmp/case6a.out" 2>&1
  rc=$?
  set -e
  if [[ $rc -eq 1 ]] && \
     grep -q '"cbmc_status": "failed"' "$tmp/case6a.json" && \
     grep -q 'put_cred.precondition' "$tmp/case6a.json"; then
    echo "  [ok] 6a (vuln): exit 1, cbmc_status=failed, put_cred precondition named"
  else
    echo "  [FAIL] 6a expected rc 1 + cbmc_status=failed + put_cred precondition" >&2
    echo "         actual rc=$rc; last 20 lines of output:" >&2
    tail -20 "$tmp/case6a.out" | sed 's/^/         /' >&2
    fail=$((fail + 1))
  fi

  # 6b: fix direction.
  set +e
  LINUX_TREE="$LINUX_TREE" "$SCAN" "$CRED_KERNEL_C" --direction=fix \
    --json "$tmp/case6b.json" > "$tmp/case6b.out" 2>&1
  rc=$?
  set -e
  if [[ $rc -eq 0 ]] && \
     grep -q '"cbmc_status": "successful"' "$tmp/case6b.json"; then
    echo "  [ok] 6b (fix):  exit 0, cbmc_status=successful"
  else
    echo "  [FAIL] 6b expected rc 0 + cbmc_status=successful" >&2
    echo "         actual rc=$rc; last 20 lines of output:" >&2
    tail -20 "$tmp/case6b.out" | sed 's/^/         /' >&2
    fail=$((fail + 1))
  fi
fi

echo
echo "=== case 7: real-kernel lock_state on kernel/bpf/dispatcher.c ==="
# Fourth property module end-to-end.  kernel/bpf/dispatcher.c
# matches the lock_state prefilter on mutex_unlock call sites in
# its bpf_dispatcher_update paths.
LOCK_KERNEL_C="$LINUX_TREE/kernel/bpf/dispatcher.c"
if [[ ! -f $LOCK_KERNEL_C ]]; then
  echo "  [skip] no $LOCK_KERNEL_C"
else
  # 7a: vulnerable direction.
  set +e
  LINUX_TREE="$LINUX_TREE" "$SCAN" "$LOCK_KERNEL_C" \
    --json "$tmp/case7a.json" > "$tmp/case7a.out" 2>&1
  rc=$?
  set -e
  if [[ $rc -eq 1 ]] && \
     grep -q '"cbmc_status": "failed"' "$tmp/case7a.json" && \
     grep -q 'mutex_unlock.precondition' "$tmp/case7a.json"; then
    echo "  [ok] 7a (vuln): exit 1, cbmc_status=failed, mutex_unlock precondition named"
  else
    echo "  [FAIL] 7a expected rc 1 + cbmc_status=failed + mutex_unlock precondition" >&2
    echo "         actual rc=$rc; last 20 lines of output:" >&2
    tail -20 "$tmp/case7a.out" | sed 's/^/         /' >&2
    fail=$((fail + 1))
  fi

  # 7b: fix direction.
  set +e
  LINUX_TREE="$LINUX_TREE" "$SCAN" "$LOCK_KERNEL_C" --direction=fix \
    --json "$tmp/case7b.json" > "$tmp/case7b.out" 2>&1
  rc=$?
  set -e
  if [[ $rc -eq 0 ]] && \
     grep -q '"cbmc_status": "successful"' "$tmp/case7b.json"; then
    echo "  [ok] 7b (fix):  exit 0, cbmc_status=successful"
  else
    echo "  [FAIL] 7b expected rc 0 + cbmc_status=successful" >&2
    echo "         actual rc=$rc; last 20 lines of output:" >&2
    tail -20 "$tmp/case7b.out" | sed 's/^/         /' >&2
    fail=$((fail + 1))
  fi
fi

echo
echo "=== case 8: real-kernel refcount_lifetime on kernel/fork.c ==="
# Fifth property module end-to-end.  kernel/fork.c matches the
# refcount_lifetime prefilter on its `refcount_dec_and_test`
# calls (three hits at lines 437, 722, 1526 on 5.10).
#   - default --direction=vuln: cbmc_status=failed; the
#     refcount_dec_and_test.precondition.4 fires at the second
#     dec in the harness's vulnerable-shape branch.
#   - --direction=fix: cbmc_status=successful.
RC_KERNEL_C="$LINUX_TREE/kernel/fork.c"
if [[ ! -f $RC_KERNEL_C ]]; then
  echo "  [skip] no $RC_KERNEL_C"
else
  # 8a: vulnerable direction.  The scan may also fire cred_lifetime
  # and lock_state on kernel/fork.c (it has put_cred and spin_unlock
  # calls too) and any of those counts as a valid `failed` on its
  # own module.  The refcount_lifetime-specific check below
  # requires the module to be present in the report and to have
  # fired its dec_and_test precondition.
  set +e
  LINUX_TREE="$LINUX_TREE" "$SCAN" "$RC_KERNEL_C" \
    --json "$tmp/case8a.json" > "$tmp/case8a.out" 2>&1
  rc=$?
  set -e
  if [[ $rc -eq 1 ]] && \
     python3 -c "
import json, sys
d = json.load(open('$tmp/case8a.json'))
for f in d['files']:
    for m in f['modules']:
        if m['module'] != 'refcount_lifetime':
            continue
        if m.get('cbmc_status') != 'failed':
            sys.exit(f\"expected refcount_lifetime cbmc_status=failed, got {m.get('cbmc_status')}\")
        failures = m.get('cbmc_failures') or []
        if not any('refcount_dec_and_test.precondition' in fa.get('assertion','')
                   for fa in failures):
            sys.exit(f'expected refcount_dec_and_test.precondition in failures; got {failures}')
        sys.exit(0)
sys.exit('no refcount_lifetime module report')
" >/dev/null 2>&1; then
    echo "  [ok] 8a (vuln): exit 1, cbmc_status=failed, refcount_dec_and_test precondition named"
  else
    echo "  [FAIL] 8a expected rc 1 + cbmc_status=failed + refcount_dec_and_test precondition" >&2
    echo "         actual rc=$rc; last 20 lines of output:" >&2
    tail -20 "$tmp/case8a.out" | sed 's/^/         /' >&2
    fail=$((fail + 1))
  fi

  # 8b: fix direction.  cbmc_status=successful on refcount_lifetime.
  # We check the refcount_lifetime module specifically (other
  # modules may still fail on the same file in fix direction
  # if they don't ship a fix-direction harness yet).
  set +e
  LINUX_TREE="$LINUX_TREE" "$SCAN" "$RC_KERNEL_C" --direction=fix \
    --json "$tmp/case8b.json" > "$tmp/case8b.out" 2>&1
  rc=$?
  set -e
  if python3 -c "
import json, sys
d = json.load(open('$tmp/case8b.json'))
for f in d['files']:
    for m in f['modules']:
        if m['module'] != 'refcount_lifetime':
            continue
        if m.get('cbmc_status') != 'successful':
            sys.exit(f\"expected refcount_lifetime successful, got {m.get('cbmc_status')}\")
        sys.exit(0)
sys.exit('no refcount_lifetime module report')
" >/dev/null 2>&1; then
    echo "  [ok] 8b (fix):  refcount_lifetime cbmc_status=successful"
  else
    echo "  [FAIL] 8b expected refcount_lifetime cbmc_status=successful" >&2
    echo "         last 20 lines of output:" >&2
    tail -20 "$tmp/case8b.out" | sed 's/^/         /' >&2
    fail=$((fail + 1))
  fi
fi

echo
echo "=== case 9: real-kernel alloc_tag on kernel/fork.c ==="
# Sixth property module end-to-end.  kernel/fork.c's
# free_thread_stack calls vfree(vm_stack->addr) on a pointer
# that, in vulnerable-shape harness, is tagged as
# ALLOC_TAG_KMALLOC (so vfree fires the precondition).  In the
# fix shape (-DFIXED) the pointer is tagged ALLOC_TAG_VMALLOC.
ALLOC_KERNEL_C="$LINUX_TREE/kernel/fork.c"
if [[ ! -f $ALLOC_KERNEL_C ]]; then
  echo "  [skip] no $ALLOC_KERNEL_C"
else
  # 9a: vulnerable direction.
  set +e
  LINUX_TREE="$LINUX_TREE" "$SCAN" "$ALLOC_KERNEL_C" \
    --json "$tmp/case9a.json" > "$tmp/case9a.out" 2>&1
  rc=$?
  set -e
  if [[ $rc -eq 1 ]] && \
     python3 -c "
import json, sys
d = json.load(open('$tmp/case9a.json'))
for f in d['files']:
    for m in f['modules']:
        if m['module'] != 'alloc_tag':
            continue
        if m.get('cbmc_status') != 'failed':
            sys.exit(f\"expected alloc_tag cbmc_status=failed, got {m.get('cbmc_status')}\")
        fails = m.get('cbmc_failures') or []
        if not any('vfree.precondition' in fa.get('assertion','') for fa in fails):
            sys.exit(f'expected vfree.precondition in failures; got {fails}')
        sys.exit(0)
sys.exit('no alloc_tag module report')
" >/dev/null 2>&1; then
    echo "  [ok] 9a (vuln): exit 1, cbmc_status=failed, vfree precondition named"
  else
    echo "  [FAIL] 9a expected rc 1 + cbmc_status=failed + vfree precondition" >&2
    echo "         actual rc=$rc; last 15 lines of output:" >&2
    tail -15 "$tmp/case9a.out" | sed 's/^/         /' >&2
    fail=$((fail + 1))
  fi

  # 9b: fix direction.
  set +e
  LINUX_TREE="$LINUX_TREE" "$SCAN" "$ALLOC_KERNEL_C" --direction=fix \
    --json "$tmp/case9b.json" > "$tmp/case9b.out" 2>&1
  rc=$?
  set -e
  if python3 -c "
import json, sys
d = json.load(open('$tmp/case9b.json'))
for f in d['files']:
    for m in f['modules']:
        if m['module'] != 'alloc_tag': continue
        if m.get('cbmc_status') != 'successful':
            sys.exit(f\"expected alloc_tag successful, got {m.get('cbmc_status')}\")
        sys.exit(0)
sys.exit('no alloc_tag module report')
" >/dev/null 2>&1; then
    echo "  [ok] 9b (fix):  alloc_tag cbmc_status=successful"
  else
    echo "  [FAIL] 9b expected alloc_tag cbmc_status=successful" >&2
    echo "         last 15 lines of output:" >&2
    tail -15 "$tmp/case9b.out" | sed 's/^/         /' >&2
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