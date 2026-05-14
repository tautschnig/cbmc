#!/usr/bin/env bash
#
# Regression driver for scan.py's --per-file mode.
#
# Validates that the integrated per-file pipeline (enclosing-
# function extraction + scan-per-file.sh invocation + per-hit
# verdict aggregation) produces the same signal as running
# scan-per-file.sh directly, but through scan.py's standard
# interface: one CLI invocation per target file, JSON output,
# aggregated cbmc_status.
#
# Cases:
#
#   1.  fs/nfsd/auth.c (cred_lifetime): two put_cred hits in
#       nfsd_setuser.  Expected: cbmc_status=failed, per_file
#       lists one verdict for nfsd_setuser covering lines 85
#       and 86, status=failed.  scan.py exits 1.
#
#   2.  fs/coredump.c (cred_lifetime): one put_cred hit in
#       do_coredump.  Expected: cbmc_status=failed, per_file
#       lists one verdict for do_coredump, status=failed.
#       scan.py exits 1.
#
# Exit 0 iff all cases behave as expected.

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
source "$SCRIPT_DIR/_lib.sh"
cd -- "$SCRIPT_DIR"

SCAN="$SCRIPT_DIR/scan.py"
LINUX_TREE=${LINUX_TREE:-/home/ubuntu/linux_5_10}

fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "=== case 1: --per-file on fs/nfsd/auth.c (cred_lifetime) ==="
AUTH_C="$LINUX_TREE/fs/nfsd/auth.c"
if [[ ! -f "$AUTH_C" ]]; then
  echo "  [skip] no $AUTH_C"
else
  set +e
  LINUX_TREE="$LINUX_TREE" UNWIND=3 \
    "$SCAN" --per-file "$AUTH_C" \
    --json "$tmp/case1.json" > "$tmp/case1.out" 2>&1
  rc=$?
  set -e
  # Expect: cbmc_status=failed, nfsd_setuser covering lines 85 and 86.
  if [[ $rc -eq 1 ]] && \
     grep -q '"cbmc_status": "failed"' "$tmp/case1.json" && \
     python3 -c "
import json, sys
d = json.load(open('$tmp/case1.json'))
for f in d['files']:
  for m in f['modules']:
    if m['module'] != 'cred_lifetime':
      continue
    pf = m.get('per_file', [])
    if len(pf) != 1:
      sys.exit('expected exactly one per_file verdict')
    v = pf[0]
    if v['function'] != 'nfsd_setuser':
      sys.exit(f\"expected function=nfsd_setuser, got {v['function']}\")
    if v['status'] != 'failed':
      sys.exit(f\"expected status=failed, got {v['status']}\")
    if sorted(v['hit_lines']) != [85, 86]:
      sys.exit(f\"expected hit_lines=[85, 86], got {v['hit_lines']}\")
    sys.exit(0)
sys.exit('no cred_lifetime module report found')
"
  then
    echo "  [ok] exit 1, cbmc_status=failed, nfsd_setuser verdict=failed covering lines 85,86"
  else
    echo "  [FAIL] expected rc 1 + cbmc_status=failed + per-file match" >&2
    echo "         actual rc=$rc; last 20 lines of output:" >&2
    tail -20 "$tmp/case1.out" | sed 's/^/         /' >&2
    fail=$((fail + 1))
  fi
fi

echo
echo "=== case 2: --per-file on kernel/ptrace.c (cred_lifetime) ==="
PTRACE_C="$LINUX_TREE/kernel/ptrace.c"
if [[ ! -f "$PTRACE_C" ]]; then
  echo "  [skip] no $PTRACE_C"
else
  set +e
  LINUX_TREE="$LINUX_TREE" UNWIND=3 \
    "$SCAN" --per-file "$PTRACE_C" \
    --json "$tmp/case2.json" > "$tmp/case2.out" 2>&1
  rc=$?
  set -e
  if [[ $rc -eq 1 ]] && \
     grep -q '"cbmc_status": "failed"' "$tmp/case2.json" && \
     python3 -c "
import json, sys
d = json.load(open('$tmp/case2.json'))
for f in d['files']:
  for m in f['modules']:
    if m['module'] != 'cred_lifetime':
      continue
    pf = m.get('per_file', [])
    match = [v for v in pf
             if v['function'] == '__ptrace_unlink'
             and v['status'] == 'failed'
             and 129 in v['hit_lines']]
    if not match:
      sys.exit(f'expected __ptrace_unlink failed verdict with line 129; got {pf}')
    sys.exit(0)
sys.exit('no cred_lifetime module report found')
"
  then
    echo "  [ok] exit 1, cbmc_status=failed, __ptrace_unlink verdict=failed covering line 129"
  else
    echo "  [FAIL] expected rc 1 + cbmc_status=failed + __ptrace_unlink failed verdict" >&2
    echo "         actual rc=$rc; last 20 lines of output:" >&2
    tail -20 "$tmp/case2.out" | sed 's/^/         /' >&2
    fail=$((fail + 1))
  fi
fi

echo
echo "=== case 3: --per-file on kernel/bpf/dispatcher.c (lock_state + refcount) ==="
# Exercises the two additional per-file-supported modules
# (lock_state and refcount_lifetime) on a single file that
# fires both prefilters.  Expected: both modules produce a
# per-file verdict of `failed` via the synthesised harness.
DISP_C="$LINUX_TREE/kernel/bpf/dispatcher.c"
if [[ ! -f "$DISP_C" ]]; then
  echo "  [skip] no $DISP_C"
else
  set +e
  LINUX_TREE="$LINUX_TREE" UNWIND=3 \
    "$SCAN" --per-file "$DISP_C" \
    --json "$tmp/case3.json" > "$tmp/case3.out" 2>&1
  rc=$?
  set -e
  if [[ $rc -eq 1 ]] && \
     python3 -c "
import json, sys
d = json.load(open('$tmp/case3.json'))
found = {}
for f in d['files']:
  for m in f['modules']:
    if m['module'] not in ('lock_state', 'refcount_lifetime'):
      continue
    pf = m.get('per_file', [])
    if any(v['status'] == 'failed' for v in pf):
      found[m['module']] = True
if 'lock_state' not in found:
  sys.exit('no failed lock_state per-file verdict')
if 'refcount_lifetime' not in found:
  sys.exit('no failed refcount_lifetime per-file verdict')
sys.exit(0)
"
  then
    echo "  [ok] exit 1, lock_state + refcount_lifetime per-file verdicts both failed"
  else
    echo "  [FAIL] expected rc 1 + both modules verdict=failed" >&2
    echo "         actual rc=$rc; last 20 lines of output:" >&2
    tail -20 "$tmp/case3.out" | sed 's/^/         /' >&2
    fail=$((fail + 1))
  fi
fi

echo
echo "=== case 4: --per-file on crypto/echainiv.c (aead) ==="
# aead per-file uses a custom multi-statement bootstrap to build
# a 1-element SGL, mark its page as PAGE_USER_WRITABLE, and
# assign req->dst.  This tests the special-case path in
# synthesise_harness.py's MODULE_GHOST_BOOTSTRAP['aead'].
ECHAINIV_C="$LINUX_TREE/crypto/echainiv.c"
if [[ ! -f "$ECHAINIV_C" ]]; then
  echo "  [skip] no $ECHAINIV_C"
else
  set +e
  LINUX_TREE="$LINUX_TREE" UNWIND=3 \
    "$SCAN" --per-file "$ECHAINIV_C" \
    --json "$tmp/case4.json" > "$tmp/case4.out" 2>&1
  rc=$?
  set -e
  if python3 -c "
import json, sys
d = json.load(open('$tmp/case4.json'))
for f in d['files']:
  for m in f['modules']:
    if m['module'] != 'aead':
      continue
    pf = m.get('per_file', [])
    if any(v['status'] == 'failed' for v in pf):
      sys.exit(0)
sys.exit('no failed aead per-file verdict')
"
  then
    echo "  [ok] aead per-file produces a failed verdict on echainiv_encrypt"
  else
    echo "  [FAIL] expected at least one failed aead per-file verdict" >&2
    echo "         actual rc=$rc; last 20 lines of output:" >&2
    tail -20 "$tmp/case4.out" | sed 's/^/         /' >&2
    fail=$((fail + 1))
  fi
fi

if [[ $fail -eq 0 ]]; then
  echo
  echo "scan.py --per-file regressions passed."
  exit 0
fi

echo >&2
echo "$fail case(s) did not match expectation." >&2
exit 1
