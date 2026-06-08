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
echo "[3/4] CBMC triage (auto-harnessed candidates)..."
printf "%-46s %-10s %-12s %s\n" "CANDIDATE" "ORACLE" "VERDICT" "PROPERTY"
printf "%-46s %-10s %-12s %s\n" "---------" "------" "-------" "--------"

# triage_oracle <query> <gentag> <cbmc-flags...>
#   gentag: tlv | count | decoded | skb  (selects the harness generator)
triage_oracle() {
  local q="$1" gentag="$2"; shift 2
  local flags="$*"
  local out="/tmp/scan_${gentag}_$$.bqrs"
  timeout 600 codeql query run --database="$DB" \
    --additional-packs=/home/ubuntu/codeql/qlpacks \
    --output="$out" "$DIR/abc-refinement/$q" >/dev/null 2>&1 || return 0
  codeql bqrs decode --format=csv "$out" > "/tmp/scan_${gentag}_csv_$$" 2>/dev/null
  awk -F'","' 'NR>1{gsub(/^"/,"",$2);gsub(/"$/,"",$2);print $2}' \
    "/tmp/scan_${gentag}_csv_$$" | grep "${TARGET%/}" \
    > "/tmp/scan_${gentag}_hits_$$" || true
  rm -f "/tmp/scan_${gentag}_csv_$$"
  [ -s "/tmp/scan_${gentag}_hits_$$" ] || { rm -f "$out"; return 0; }
  while IFS= read -r cand; do
    local func file line H GB VOUT VERDICT PROP
    func=$(printf '%s' "$cand" | cut -d'|' -f1)
    file=$(printf '%s' "$cand" | cut -d'|' -f2)
    line=$(printf '%s' "$cand" | cut -d'|' -f3)
    H=$(mktemp /tmp/h_XXXX.c)
    case "$gentag" in
      tlv) python3 "$DIR/abc-refinement/tlv_harness_gen.py" -c "$cand" > "$H" 2>/dev/null ;;
      *)   python3 "$DIR/abc-refinement/oracle_harness_gen.py" --oracle "$gentag" -c "$cand" > "$H" 2>/dev/null ;;
    esac
    if [ ! -s "$H" ]; then
      printf "%-46s %-10s %-12s %s\n" "$func:$line" "$gentag" "GEN_ERR" "-"
      rm -f "$H"; continue
    fi
    GB=$(mktemp /tmp/h_XXXX.gb)
    "$GOTOCC" -o "$GB" "$H" 2>/dev/null
    VOUT=$(timeout 90 "$CBMC" "$GB" --function harness_buggy $flags --unwind "$UNWIND" 2>&1)
    VERDICT=$(echo "$VOUT" | grep -o "VERIFICATION [A-Z]*" || echo "TIMEOUT/ERR")
    PROP=$(echo "$VOUT" | grep "FAILURE" | head -1 | sed 's/.*] //' || echo "-")
    printf "%-46s %-10s %-12s %s\n" "$func:$line" "$gentag" "$VERDICT" "$PROP"
    rm -f "$H" "$GB"
  done < "/tmp/scan_${gentag}_hits_$$"
  rm -f "$out" "/tmp/scan_${gentag}_hits_$$"
}

triage_oracle tlv_parse_loop.ql             tlv     --bounds-check --pointer-check
triage_oracle tainted_count_into_fixed_array.ql count --bounds-check --pointer-check
triage_oracle decoded_len_arith_overflow.ql decoded --unsigned-overflow-check
triage_oracle skb_field_before_lencheck.ql  skb     --bounds-check --pointer-check

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
