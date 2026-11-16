#!/bin/bash
# Linux ENA: ethtool -L on a down interface, then ethtool -X.
# See harness_lx_channels.c.  Expected to FAIL (reproduces two findings):
#   CH_ERR_COUNT  (K: the new count sticks after a failure)
#   CH_ERR_RSS    and a NULL dereference in ena_com_indirect_table_fill_entry
#                 during the following ethtool -X (H)
set -u
SOLVER=${SOLVER:---sat-solver minisat2}
BIN=${BIN:-/home/ubuntu/cbmc-github.git/build-asm/bin}
LENA=${LENA:-/tmp/lena-oot}
KTREE=${KTREE:-/home/ubuntu/linux_6_12}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/ena-proof/lx-channels}; mkdir -p "$OUT"
. "$HERE/lx_lib.sh"
GBS="ena_ethtool ena_netdev ena_com ena_eth_com ena_xdp"
RMB="ena_com_execute_admin_command"
U="--unwind 10 $SOLVER --slice-formula --unwinding-assertions"
# Tracing and logging hooks: they record a message and touch no driver state.
NOBODY_OK='do_trace_netlink_extack|_printk|_dev_err|_dev_info|_dev_warn|netdev_err|netdev_info|netdev_warn|__dynamic_netdev_dbg|__dynamic_dev_dbg|__dynamic_pr_debug'
lx_build ch0 harness_lx_channels.c "$GBS" "$RMB"
lx_build ch1 harness_lx_channels.c "$GBS" "$RMB" VACUITY_CHECK
echo "CBMC-CMD: cbmc $OUT/ch0.gb --bounds-check --pointer-check $U" > "$OUT/ch.log"
( ulimit -v ${MEM:-60000000}; timeout ${TO:-3600} "$BIN/cbmc" "$OUT/ch0.gb" \
    --bounds-check --pointer-check $U ) >> "$OUT/ch.log" 2>&1
echo "CBMC-CMD: cbmc $OUT/ch1.gb --no-standard-checks $U" > "$OUT/ch_v.log"
( ulimit -v ${MEM:-60000000}; timeout ${TO:-3600} "$BIN/cbmc" "$OUT/ch1.gb" \
    --no-standard-checks $U ) >> "$OUT/ch_v.log" 2>&1
echo "== ethtool -L then -X"
lx_report "$OUT/ch.log" 'CH_|LX_' "$NOBODY_OK"
grep -E 'pointer NULL.*: FAILURE$' "$OUT/ch.log" | sed -E 's/\] line [0-9]+ /] /; s/^/   /' | head -5
sed -E 's/\] line [0-9]+ /] /; s/^/   /' "$OUT/ch_v.log" | grep -E '\bwitness\b' | sort -u
