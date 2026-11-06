#!/bin/bash
# Where the time goes in the FreeBSD ena_tx_cleanup loop-contract proof at
# ring size 4.  Needs the build of README-fbsd-build.md in $FB (ena.o,
# ena_com.o, ena_eth_com.o, flags.txt) and a goto-instrument with the DFCC
# fixes on branch fbsd-dfcc-findings.  Takes about 15 minutes and up to
# 52 GB of memory.
set -u
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build/bin}
FB=${FB:-/tmp/fbsd-build}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(mktemp -d)
cd "$FB"
"$BIN/goto-cc" $(cat flags.txt) -DRING_SIZE=4 -c "$HERE/harness_fbsd_cleanup.c" \
  -o "$OUT/hc.o"
"$BIN/goto-cc" "$OUT/hc.o" ena.o ena_com.o ena_eth_com.o -o "$OUT/fbc.gb"
cd "$OUT"
( ulimit -v 16777216; timeout 900 "$BIN/goto-instrument" --dfcc main \
    --apply-loop-contracts \
    --loop-contracts-file "$HERE/fbsd_cleanup_loop_contract.json" \
    fbc.gb fbc_lc.gb > instrument.log 2>&1 ) || {
  echo "goto-instrument failed, see $OUT/instrument.log"; exit 1; }
# --stop-on-fail: the first, and dominant, solver call only
( ulimit -v 67108864; timeout 1800 "$BIN/cbmc" fbc_lc.gb --unwind 5 \
    --unwinding-assertions --sat-solver cadical --stop-on-fail --verbosity 8 \
    --write-solver-stats-to hardness.json 2>&1 ) \
  | grep -E "Runtime|size of program|VCC|variables|^VERIFICATION" | tee cbmc.log
( ulimit -v 100000000; timeout 1500 python3 "$HERE/solver_hardness_summary.py" \
    hardness.json )
echo "artefacts in $OUT"
