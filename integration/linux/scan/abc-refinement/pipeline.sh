#!/bin/bash
# End-to-end: CodeQL stage-1 → auto-harness → CBMC stage-2
# Usage: ./pipeline.sh <codeql-db> [query.ql]
set -euo pipefail
DB="${1:?Usage: $0 <codeql-db-path> [query.ql]}"
QUERY="${2:-tainted_copy_size_structured.ql}"
DIR="$(cd "$(dirname "$0")" && pwd)"
CBMC="${DIR}/../../../../build/bin/cbmc"
export PATH=/home/ubuntu/codeql:$PATH
export CODEQL_ALLOW_INSTALLATION_ANYWHERE=true

echo "=== Stage 1: CodeQL ==="
BQRS=$(mktemp /tmp/stage1_XXXX.bqrs)
timeout 900 codeql query run --database="$DB" \
  --additional-packs=/home/ubuntu/codeql/qlpacks \
  --output="$BQRS" "$DIR/$QUERY" 2>&1 | grep -E "eval|Shutting"
CSV=$(mktemp /tmp/stage1_XXXX.csv)
codeql bqrs decode --format=csv "$BQRS" > "$CSV" 2>/dev/null

echo "=== Stage 1 candidates: ==="
# Extract the structured field (2nd CSV column, unquoted)
awk -F'","' 'NR>1{gsub(/^"/,"",$2); gsub(/"$/,"",$2); print $2}' "$CSV" | \
  grep -v "^$" | while IFS='|' read -r func file line sink param dest; do
  echo "  $func ($file:$line) sink=$sink param=$param"
done

echo
echo "=== Stage 2: CBMC refinement ==="
awk -F'","' 'NR>1{gsub(/^"/,"",$2); gsub(/"$/,"",$2); print $2}' "$CSV" | \
  grep -v "^$" | while IFS='|' read -r func file line sink param dest; do
  # Skip header/kernel-internal (focus on target driver)
  case "$file" in */vme_user/*) ;; *) continue;; esac
  HARNESS=$(mktemp /tmp/harness_${func}_XXXX.c)
  python3 "$DIR/auto_harness.py" -c "${func}|${file}|${line}|${sink}|${param}|${dest}" > "$HARNESS"
  VERDICT=$(timeout 60 "$CBMC" "$HARNESS" --function "harness_${func}" \
    --bounds-check --pointer-check 2>&1 | grep "^VERIFICATION" || echo "TIMEOUT/ERROR")
  printf "  %-25s %s\n" "$func" "$VERDICT"
  rm -f "$HARNESS"
done

rm -f "$BQRS" "$CSV"
