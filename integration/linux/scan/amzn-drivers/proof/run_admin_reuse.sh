#!/bin/bash
# Class R: a response attributed to the wrong request.
#
# Two class Z findings are this shape -- "Wrong admin response returned after
# abort and command-ID reuse" (shared ena_com) and "An admin command returns
# another command's response when the device duplicates a completion" (EFA).
#
# A command's only identity is its slot index: submit takes curr_cmd_id and
# advances it modulo the depth (ena_com.c:245-267), the completion handler
# looks up by that id alone (ena_com.c:459-487), and abort leaves the slot
# occupied with user_cqe still set (ena_com.c:2237-2254).  So a late
# completion for an abandoned command is delivered to whichever command now
# holds the id.
#
#   as-is   AR_NO_STALE_STATUS and AR_RIGHT_REQUEST must FAIL
#   fixed   -DWITH_GENERATION_CHECK models a per-slot generation; both hold.
#           No protocol field carries a generation, so this is a model of what
#           a fix must achieve, not a patch -- the real fix is to not reuse an
#           id while a completion may still arrive for it.
set -u
SOLVER=${SOLVER:---sat-solver minisat2}
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/ena-proof/areuse}; mkdir -p "$OUT"
U="--unwind 20 $SOLVER --slice-formula --unwinding-assertions"
for cfg in "as-is:" "fixed:-DWITH_GENERATION_CHECK" "vacuity:-DVACUITY_CHECK"; do
  n=${cfg%%:*}; d=${cfg#*:}
  rm -f "$OUT/$n.gb"
  ( cd /tmp/dpdk/build-ql && ulimit -v 32000000 &&
    "$BIN/goto-cc" $(cat /tmp/ena-proof/flags.txt) -DPEDANTIC $d \
      "$HERE/harness_admin_reuse.c" -o "$OUT/$n.gb" 2>&1 | grep -i " error" | head -3 )
  [ -f "$OUT/$n.gb" ] || { echo "$n: BUILD FAILED, refusing to run stale binaries"; exit 1; }
  CK="--bounds-check --pointer-check"; [ "$n" = vacuity ] && CK="--no-standard-checks"
  echo "CBMC-CMD: cbmc $OUT/$n.gb $CK $U" > "$OUT/$n.log"
  ( ulimit -v ${MEM:-60000000}; timeout ${TO:-2400} "$BIN/cbmc" "$OUT/$n.gb" $CK $U ) >> "$OUT/$n.log" 2>&1
  echo "== $n"
  sed -E 's/\] line [0-9]+ /] /; s/^/   /' "$OUT/$n.log" | grep -E 'AR_|witness|^   VERIFICATION' | sort -u
  printf '   other FAILURE: %s   no-body reached: %s   UNKNOWN: %s   unwind FAIL: %s\n' \
    "$(grep -E ': FAILURE$' "$OUT/$n.log" | grep -vcE 'AR_|witness')" \
    "$(grep -cE 'no body for callee.*FAILURE' "$OUT/$n.log")" \
    "$(grep -cE ': UNKNOWN$' "$OUT/$n.log")" \
    "$(grep -cE 'unwind.*: FAILURE' "$OUT/$n.log")"
done
