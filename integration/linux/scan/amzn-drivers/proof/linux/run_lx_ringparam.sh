#!/bin/bash
# Class S, Linux ENA: ethtool -G after a ring-size backoff.
# See harness_lx_ringparam.c.
#   no-backoff  PRIOR_BACKOFF=0: SR_TAKES_EFFECT must hold
#   backoff     PRIOR_BACKOFF=1: SR_TAKES_EFFECT expected to FAIL (the finding)
set -u
SOLVER=${SOLVER:---sat-solver minisat2}
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build-asm/bin}
LENA=${LENA:-/tmp/lena-oot}
KTREE=${KTREE:-/home/ubuntu/linux_6_12}
HERE=$(cd "$(dirname "$0")" && pwd)
SCAN=$(cd "$HERE/../../.." && pwd)
OUT=${OUT:-/tmp/ena-proof/lx-ringparam}; mkdir -p "$OUT"
[ -f "$LENA/gb/ena_ethtool.gb" ] || { echo "run build_linux_ena.sh first"; exit 1; }
# vsnprintf: CBMC's library loop over the 40-byte buffer ena_set_ringparam
# formats its "supported sizes" message into.
U="--unwind 8 --unwindset vsnprintf.0:41,vsnprintf.1:41 $SOLVER --slice-formula --unwinding-assertions"
# do_trace_netlink_extack is the tracepoint inside NL_SET_ERR_MSG_MOD: it
# records the message for tracing and touches no driver state.
NOBODY_OK='do_trace_netlink_extack'

build() { # name defines...
  local n=$1; shift
  local w="$LENA/ena/lxh_$n.c" d
  { for d in "$@"; do echo "#define $d"; done
    echo "#include \"$HERE/harness_lx_ringparam.c\""; } > "$w"
  rm -f "$OUT/$n.o" "$OUT/$n.gb"
  ( ulimit -v 16000000
    SOURCE_INCLUDE_DIR=$LENA/common/ena_com GOTOCC=$BIN/goto-cc KBUILD_MODNAME_OVERRIDE=ena \
      "$SCAN/compile_file.sh" "$KTREE" "$w" "$OUT/$n.o" > "$OUT/$n.cc.log" 2>&1 )
  [ -f "$OUT/$n.o" ] || { echo "BUILD FAILED ($n), refusing to run stale binaries"; grep -m3 -i error "$OUT/$n.cc.log"; exit 1; }
  ( ulimit -v 16000000; "$BIN/goto-cc" "$OUT/$n.o" "$LENA/gb/ena_ethtool.gb" -o "$OUT/$n.gb" ) > "$OUT/$n.ld.log" 2>&1
  [ -f "$OUT/$n.gb" ] || { echo "LINK FAILED ($n)"; tail -3 "$OUT/$n.ld.log"; exit 1; }
}

for cfg in "no-backoff:PRIOR_BACKOFF=0" "backoff:PRIOR_BACKOFF=1"; do
  n=${cfg%%:*}; d=${cfg#*:}
  build "${n}0" "PRIOR_BACKOFF ${d#*=}"
  build "${n}1" "PRIOR_BACKOFF ${d#*=}" "VACUITY_CHECK"
  echo "CBMC-CMD: cbmc $OUT/${n}0.gb --bounds-check --pointer-check $U" > "$OUT/$n.log"
  ( ulimit -v ${MEM:-60000000}; timeout ${TO:-1800} "$BIN/cbmc" "$OUT/${n}0.gb" \
      --bounds-check --pointer-check $U ) >> "$OUT/$n.log" 2>&1
  echo "CBMC-CMD: cbmc $OUT/${n}1.gb --no-standard-checks $U" > "$OUT/${n}_v.log"
  ( ulimit -v ${MEM:-60000000}; timeout ${TO:-1800} "$BIN/cbmc" "$OUT/${n}1.gb" \
      --no-standard-checks $U ) >> "$OUT/${n}_v.log" 2>&1
  echo "== $n"
  grep -qE '^VERIFICATION' "$OUT/$n.log" || { echo "   NO VERDICT"; tail -3 "$OUT/$n.log"; }
  sed -E 's/\] line [0-9]+ /] /; s/^/   /' "$OUT/$n.log" | grep -E 'SR_|^   VERIFICATION' | sort -u
  printf '   other FAILURE: %s   no-body reached: %s   UNKNOWN: %s   unwind FAIL: %s\n' \
    "$(grep -E ': FAILURE$' "$OUT/$n.log" | grep -v 'no body for callee' | grep -vcE 'SR_|witness')" \
    "$(grep -E 'no body for callee.*FAILURE' "$OUT/$n.log" | grep -vcE "$NOBODY_OK")" \
    "$(grep -cE ': UNKNOWN$' "$OUT/$n.log")" \
    "$(grep -cE 'unwind.*: FAILURE' "$OUT/$n.log")"
  sed -E 's/\] line [0-9]+ /] /; s/^/   /' "$OUT/${n}_v.log" | grep -E '\bwitness\b' | sort -u
done
