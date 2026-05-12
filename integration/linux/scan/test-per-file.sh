#!/usr/bin/env bash
#
# Regression driver for scan-per-file.sh.
#
# Runs the per-file scan pipeline on one known real kernel
# target (Linux 5.10 fs/nfsd/auth.c:nfsd_setuser, which has two
# back-to-back put_cred calls at the end) and asserts that cbmc
# reports VERIFICATION FAILED with the cred_live precondition
# firing.
#
# This locks in the LIM-016 resolution: goto-harness's
# synthesised harness + --replace-call-with-contract +
# cred_lifetime adapter all compose without the DATA_INVARIANT
# blockage that used to stop the pipeline.
#
# Exit 0 iff the expected verdict is produced.

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)

LINUX_TREE=${LINUX_TREE:-/home/ubuntu/linux_5_10}
if [[ ! -f "$LINUX_TREE/fs/nfsd/auth.c" ]]; then
  echo "  [skip] no $LINUX_TREE/fs/nfsd/auth.c"
  exit 0
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "=== per-file scan on fs/nfsd/auth.c : nfsd_setuser ==="
set +e
LINUX_TREE="$LINUX_TREE" UNWIND=3 \
  "$SCRIPT_DIR/scan-per-file.sh" cred_lifetime \
    fs/nfsd/auth.c nfsd_setuser > "$tmp/out.log" 2>&1
rc=$?
set -e

if [[ $rc -eq 10 ]] && \
   grep -q "verdict: VERIFICATION FAILED" "$tmp/out.log" && \
   grep -q "__CPROVER_file_local_cred_h_put_cred.precondition.*FAILURE" \
     "$tmp/out.log"; then
  echo "  [ok] per-file scan returned VERIFICATION FAILED with cred_live"
  echo "       precondition firing at the expected put_cred sites"
  exit 0
else
  echo "  [FAIL] expected rc 10 + VERIFICATION FAILED + precondition.FAILURE" >&2
  echo "         actual rc=$rc" >&2
  tail -20 "$tmp/out.log" | sed 's/^/         /' >&2
  exit 1
fi
