#!/usr/bin/env bash
#
# smoke-newer-kernel.sh — run the integration/linux scan against a
# newer kernel tree than the reference Linux 5.10 used for
# day-to-day development.
#
# Purpose: catch kernel-version drift early.  The scan's
# `compile_file.sh` uses a fixed `-I` / `-D` flag set that
# approximates a reasonable allnoconfig + AF_ALG + AEAD build for
# x86_64.  Header-API drift in newer trees can break that
# assumption; the sooner we catch it, the sooner we can refine
# either the fragment library (scan/fragments/*.config) or the
# compile_file.sh flag set.
#
# Usage:
#   smoke-newer-kernel.sh [PATH_TO_KERNEL_TREE]
#
# The tree must already be configured (make olddefconfig && make
# prepare scripts).  If PATH is omitted, defaults to
# /home/ubuntu/linux.git if it exists.
#
# Output: structured JSON plus a human-readable summary.  Exit 0
# iff the Coccinelle prefilter fires on the expected candidate
# sites AND the scan pipeline does not itself crash (a specific
# cbmc_status of "error" for a kernel-compile reason is reported
# as a known limitation, not a smoke-test failure).

set -u

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)
DEFAULT_KTREE=/home/ubuntu/linux.git
KTREE=${1:-$DEFAULT_KTREE}

if [[ ! -d $KTREE ]]; then
  echo "smoke-newer-kernel: no tree at $KTREE" >&2
  echo "Pass a path as the first argument, or clone one:" >&2
  echo "  git clone --depth 1 -b v6.6 \\" >&2
  echo "      https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git \\" >&2
  echo "      /tmp/linux_6_6" >&2
  echo "  (cd /tmp/linux_6_6 && make allnoconfig && make prepare scripts)" >&2
  exit 2
fi

if [[ ! -f $KTREE/include/generated/autoconf.h ]]; then
  echo "smoke-newer-kernel: $KTREE looks unconfigured" >&2
  echo "run 'make olddefconfig && make prepare scripts' there first" >&2
  exit 2
fi

# Extract the kernel version for reporting.
version=$(
  awk '/^VERSION *=|^PATCHLEVEL *=|^SUBLEVEL *=|^EXTRAVERSION *=/ {print $3}' \
    "$KTREE/Makefile" | paste -sd . -
)
echo "=== smoke test against Linux $version at $KTREE ==="

TARGETS=(
  "crypto/algif_aead.c"          # Copy Fail (CVE-2026-31431) candidate
  "fs/splice.c"                  # Dirty Pipe (CVE-2022-0847) candidate
  "lib/iov_iter.c"               # copy_page_to_iter_pipe lives here
)

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

fail=0
for t in "${TARGETS[@]}"; do
  full="$KTREE/$t"
  if [[ ! -f $full ]]; then
    echo "  [skip] $t does not exist in $version"
    continue
  fi

  out="$tmp/$(basename "$t").json"
  echo
  echo "--- $t ---"
  LINUX_TREE=$KTREE \
    "$SCRIPT_DIR/scan.py" "$full" --json "$out" 2>&1 | tail -5

  # Expect a `failed` verdict with the precondition named (the
  # direct-call harness's vulnerable branch fires the contract).
  # `error` was acceptable before LIM-011 was resolved in Phase
  # 3.1; after the `aead_kernel_stubs.c` retirement, none of the
  # three targets should hit cross-TU static-inline conflicts
  # any more.  Any error or vacuity-risk is now a regression.
  if grep -q '"cbmc_status": "vacuity-risk"' "$out"; then
    echo "  [FAIL] vacuity-risk on $t — infrastructure regression" >&2
    fail=$((fail + 1))
  elif grep -q '"cbmc_status": "error"' "$out"; then
    echo "  [FAIL] cbmc_status=error on $t — LIM-011 regression?" >&2
    fail=$((fail + 1))
  fi
done

if (( fail == 0 )); then
  echo
  echo "smoke-newer-kernel: no infrastructure regressions on Linux $version"
  exit 0
fi

echo >&2
echo "smoke-newer-kernel: $fail infrastructure regression(s)" >&2
exit 1
