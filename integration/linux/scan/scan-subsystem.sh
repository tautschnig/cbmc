#!/bin/bash
# scan-subsystem.sh — End-to-end: build DB, run all queries, auto-harness, CBMC triage.
#
# Usage: ./scan-subsystem.sh <kernel-tree> <make-target> [unwind]
# Example: ./scan-subsystem.sh /home/ubuntu/linux_next "net/nfc/" 12
#
# Emits a triage table: candidate | query | CBMC verdict | property
set -uo pipefail
TREE="${1:?Usage: $0 <kernel-tree> <make-target> [unwind]}"
TARGET="${2:?Usage: $0 <kernel-tree> <make-target> [unwind]}"
UNWIND="${3:-10}"
DIR="$(cd "$(dirname "$0")" && pwd)"
DB="/tmp/scan_db_$$"
CBMC="$(cd "$DIR/../../.." && pwd)/build/bin/cbmc"
GOTOCC="$(cd "$DIR/../../.." && pwd)/build/bin/goto-cc"
export PATH=/home/ubuntu/codeql:$PATH
export CODEQL_ALLOW_INSTALLATION_ANYWHERE=true

echo "========================================"
echo " scan-subsystem: $TARGET on $(basename "$TREE")"
echo "========================================"

# Step 1: build DB
echo "[1/4] Building CodeQL DB..."
"$DIR/build-codeql-db.sh" "$TREE" "$DB" "$TARGET" 2>&1 | grep -E "===|patched|bodies|WARNING|DONE"

# Step 2: run all queries
echo "[2/4] Running queries..."
RESULTS=""
for q in tlv_parse_loop tlv_parse_loop_helper tainted_into_fixed_dest tainted_alloc_overflow tainted_count_into_fixed_array skb_field_before_lencheck decoded_len_arith_overflow; do
  OUT="/tmp/scan_${q}_$$.bqrs"
  timeout 600 codeql query run --database="$DB" --additional-packs=/home/ubuntu/codeql/qlpacks \
    --output="$OUT" "$DIR/abc-refinement/$q.ql" >/dev/null 2>&1
  HITS=$(codeql bqrs decode --format=csv "$OUT" 2>/dev/null | \
    awk -F'","' 'NR>1{gsub(/^"/,"",$2);gsub(/"$/,"",$2);print $2}' | grep -c "${TARGET%/}" || true)
  if [ "${HITS:-0}" -gt 0 ]; then
    codeql bqrs decode --format=csv "$OUT" 2>/dev/null | \
      awk -F'","' 'NR>1{gsub(/^"/,"",$2);gsub(/"$/,"",$2);print $2}' | \
      grep "${TARGET%/}" | while read -r line; do
      RESULTS="${RESULTS}${q}|${line}\n"
    done
  fi
  rm -f "$OUT"
done

# Step 3: generate harnesses + CBMC for TLV-loop hits
echo "[3/4] CBMC triage (TLV-loop candidates)..."
printf "%-50s %-12s %s\n" "CANDIDATE" "VERDICT" "PROPERTY"
printf "%-50s %-12s %s\n" "---------" "-------" "--------"

TLV_OUT="/tmp/scan_tlv_triage_$$.bqrs"
timeout 600 codeql query run --database="$DB" --additional-packs=/home/ubuntu/codeql/qlpacks \
  --output="$TLV_OUT" "$DIR/abc-refinement/tlv_parse_loop.ql" >/dev/null 2>&1
codeql bqrs decode --format=csv "$TLV_OUT" > /tmp/scan_tlv_csv_$$ 2>/dev/null
awk -F'","' 'NR>1{gsub(/^"/,"",$2);gsub(/"$/,"",$2);print $2}' /tmp/scan_tlv_csv_$$ | \
  grep "${TARGET%/}" > /tmp/scan_tlv_hits_$$ || true
rm -f /tmp/scan_tlv_csv_$$
if [ -s /tmp/scan_tlv_hits_$$ ]; then
while IFS='|' read -r func file line rest; do
  H=$(mktemp /tmp/h_XXXX.c)
  python3 "$DIR/abc-refinement/tlv_harness_gen.py" -c "${func}|${file}|${line}|${rest}" > "$H" 2>/dev/null
  if [ ! -s "$H" ]; then
    printf "%-50s %-12s %s\n" "$func:$line" "GEN_ERR" "-"
    rm -f "$H"; continue
  fi
  GB=$(mktemp /tmp/h_XXXX.gb)
  "$GOTOCC" -o "$GB" "$H" 2>/dev/null
  VOUT=$( timeout 90 "$CBMC" "$GB" --function harness_buggy --bounds-check --pointer-check --unwind "$UNWIND" 2>&1 )
  VERDICT=$(echo "$VOUT" | grep -o "VERIFICATION [A-Z]*" || echo "TIMEOUT/ERR")
  PROP=$(echo "$VOUT" | grep "FAILURE" | head -1 | sed 's/.*] //' || echo "-")
  printf "%-50s %-12s %s\n" "$func:$line" "$VERDICT" "$PROP"
  rm -f "$H" "$GB"
done < /tmp/scan_tlv_hits_$$
fi
rm -f "$TLV_OUT" /tmp/scan_tlv_hits_$$

# Step 4: summary
echo
echo "[4/4] Summary"
echo "  DB: $DB"
echo "  Queries: tlv_parse_loop, tlv_parse_loop_helper, tainted_into_fixed_dest, tainted_alloc_overflow, tainted_count_into_fixed_array, skb_field_before_lencheck, decoded_len_arith_overflow"
echo "  CBMC unwind: $UNWIND"
echo -e "  All candidates:\n$RESULTS" | head -30
echo "========================================"

# Cleanup
rm -rf "$DB"
