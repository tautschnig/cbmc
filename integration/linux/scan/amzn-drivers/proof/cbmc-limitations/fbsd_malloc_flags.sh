#!/bin/bash
# FreeBSD malloc(9) flags are ignored by the CPROVER malloc model.
set -u
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
for opt in "" "--no-malloc-may-fail"; do
  echo "== cbmc --pointer-check $opt =="
  ( ulimit -v 8000000; timeout 300 "$BIN/cbmc" "$HERE/fbsd_malloc_flags.c" \
      --pointer-check $opt 2>&1 ) \
    | grep -E "old definition|pointer NULL in \*a|zeroed|^VERIFICATION" \
    | sed 's/^/  /'
done
echo
echo "Expected, and the problem: with the default --malloc-may-fail, the"
echo "M_WAITOK allocation may be NULL ('pointer NULL in *a'), which malloc(9)"
echo "rules out; with --no-malloc-may-fail, 'M_ZERO, variable flags: zeroed'"
echo "still fails because the model ignores M_ZERO."
