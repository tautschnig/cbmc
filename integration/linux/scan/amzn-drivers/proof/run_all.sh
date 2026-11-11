#!/bin/bash
# Re-run every self-contained proof from scratch and report one line each.
# Does not include the two induction runners (run_induction.sh,
# run_rx_induction.sh), which have their own reports.
#
# Two runners report VERIFICATION FAILED BY DESIGN, because their first
# configuration reproduces a finding rather than proving a property:
#   prepare_tx  `real` fails PTX_OK, which is F14
#   doorbell    `original` and two more fail, which is F14
# Both are marked EXPECTED-FAIL below.  Every other runner must be
# SUCCESSFUL, with nobody=0, unknown=0 and its witnesses failing.
#
# EACH must be generous: several runners do a main and a vacuity
# verification, each of which can take ~20 minutes, and a tight timeout cuts
# the script off before it prints the witnesses.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
LOG=${LOG:-/tmp/ena-proof/all}; mkdir -p "$LOG"
one() {
  local name=$1 script=$2; shift 2
  OUT="$LOG/$name" W="$LOG/$name" timeout "${EACH:-5400}" "$HERE/$script" "$@" \
    > "$LOG/$name.txt" 2>&1
  local v other nobody unk
  v=$(grep -oE 'VERIFICATION [A-Z]+' "$LOG/$name.txt" | head -1)
  other=$(grep -cE 'BUILD FAILED|NO VERDICT' "$LOG/$name.txt")
  nobody=$(grep -oE 'no-body reached: [0-9]+' "$LOG/$name.txt" | grep -oE '[0-9]+' | head -1)
  unk=$(grep -oE 'UNKNOWN: [0-9]+' "$LOG/$name.txt" | grep -oE '[0-9]+' | head -1)
  printf '%-18s %-24s obligations=%-3s witnesses_fail=%-3s nobody=%-3s unknown=%-3s %s\n' \
    "$name" "${v:-NO VERDICT}" \
    "$(grep -cE ': SUCCESS$' "$LOG/$name.txt")" \
    "$(grep -E 'vacuity witness|zero-length witness' "$LOG/$name.txt" | grep -c ': FAILURE$')" \
    "${nobody:-?}" "${unk:-?}" \
    "$(case " ${EXPECT_FAIL:-} " in *" $name "*) echo 'EXPECTED-FAIL (reproduces F14)';; *) [ "$other" = 0 ] && [ "${v:-}" = "VERIFICATION SUCCESSFUL" ] && echo || echo CHECK;; esac)"
}
# reset_flag, rss_reta and conc_admin also fail by design: each reproduces a
# finding (F21, F19) or an observation in its first configuration.
EXPECT_FAIL="prepare_tx doorbell reset_flag rss_reta conc_admin"
one stop          run_stop.sh
one rx_recv       run_rx_recv.sh
one rx_refill     run_rx_refill.sh
one rx_setup      run_rx_setup.sh
one rx_start      run_rx_start.sh
one rx_stop       run_rx_stop.sh
one rx_com        run_rx_com.sh
one prepare_tx    run_prepare_tx.sh
one doorbell      run_doorbell_budget.sh
one dev_lifecycle run_dev_lifecycle.sh
one dev_start     run_dev_start.sh
one watchdog      run_watchdog.sh
one aenq          run_aenq.sh
one admin_cq      run_admin_cq.sh
one xstats        run_xstats.sh
one leaks         run_leaks.sh
one dma_order     run_dma_order.sh
one conc_reset    run_conc_reset.sh
one conc_admin    run_conc_admin.sh
one dev_configure run_dev_configure.sh
one reset_flag    run_reset_flag.sh
one rss_reta      run_rss_reta.sh
one freelist_eq   run_freelist_equiv.sh
one cleanup_step  run_cleanup_step.sh
