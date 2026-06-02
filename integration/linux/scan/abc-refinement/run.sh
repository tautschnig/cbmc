#!/usr/bin/env bash
# Two-stage CodeQL -> CBMC bounds-refinement prototype.
#
#   stage 1 (CodeQL, over-approx) proposes candidates:
#           tainted length reaches a memcpy size.
#   stage 2 (CBMC, precise) refines each candidate:
#           real OOB (with witness) vs proved-safe (FP filtered).
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
CODEQL="${CODEQL:-/home/ubuntu/codeql/codeql}"
CBMC="${CBMC:-/home/ubuntu/cbmc-github.git/build/bin/cbmc}"
DB="${DB:-/tmp/abc-db}"
export CODEQL_ALLOW_INSTALLATION_ANYWHERE=true

echo "== stage 1: CodeQL over-approximating taint (expect 2 candidates) =="
"$CODEQL" database create "$DB" --language=cpp --overwrite \
  --command="gcc -c $HERE/bounds.c -o /tmp/bounds.o" >/dev/null 2>&1
"$CODEQL" query run --database="$DB" \
  --additional-packs=/home/ubuntu/codeql/qlpacks \
  "$HERE/tainted_memcpy_size.ql" 2>/dev/null | grep -E "copy_"

echo "== stage 2: CBMC refinement (expect vuln FAIL, fixed SUCCESS) =="
for fn in harness_vuln harness_fixed; do
  v=$("$CBMC" "$HERE/bounds.c" -DCBMC_HARNESS --function "$fn" \
        --bounds-check --pointer-check 2>&1 \
        | grep -oE "VERIFICATION (SUCCESSFUL|FAILED)" || true)
  echo "  $fn: $v"
done
