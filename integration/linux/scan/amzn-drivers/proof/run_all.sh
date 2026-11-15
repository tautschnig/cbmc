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
# EACH must be generous, and 5400 was not enough.  Several runners now sweep
# five or six configurations, each with a main and a vacuity verification, so
# the old 90-minute cap truncated them part way: prepare_tx completed 2 of its
# 6 configurations and the adjudicator correctly refused the partial run for
# having no witness in the captured output.  Run standalone with no cap, all
# six pass with every witness failing.  The default is now 4 hours; a runner
# that needs more should be run on its own.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
LOG=${LOG:-/tmp/ena-proof/all}; mkdir -p "$LOG"
one() {
  local name=$1 script=$2; shift 2
  OUT="$LOG/$name" W="$LOG/$name" timeout "${EACH:-14400}" "$HERE/$script" "$@" \
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
# llq_path's default xmit mode runs without ASSUME_NO_F14, so PREPARE_OK fails:
# that is F14.  rx_errorpath reproduces F10 (released) and F17 (intr).
# conc_reset belongs here too: under STORE_ORDER=1 -- the reordering the
# driver's missing barrier permits -- CC_REASON_SOUND fails, and that failure
# IS the unsynchronised-RMW observation the harness was written to make.  It
# was omitted, so the sweep adjudicator reported it as an unsupported claim.
EXPECT_FAIL="prepare_tx doorbell reset_flag rss_reta conc_admin conc_reset \
class_s xstats_byid conc_datapath admin_reuse probe lock_order \
cleanup_fastfree xmit_offloads setup_rx_intr fbsd_mtu fbsd_sysctl fbsd_down \
llq_path rx_errorpath"
one stop          run_stop.sh
# MC=2 so the multi-descriptor chain path is covered: at the default MC=1
# it cannot arise, and the witness for it passed, which the sweep
# adjudicator flagged as a possibly-empty proof.
MC=2 one rx_recv       run_rx_recv.sh
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

# Adjudicate the whole sweep rather than trusting the per-runner summary
# above.  That summary counted witness FAILURES, so a harness with a witness
# that PASSED, or with no witness at all, was indistinguishable from a clean
# run -- and a passing witness is exactly the signal that a proof may be
# empty.  check_sweep.py applies the check_hygiene.sh rules across the corpus.
# ---- registered 2026-11-14: the runners added since the sweep was written.
# Those omitted are staged or experimental (run_stage*, run_item1*,
# run_cleanup_lc, run_xmit_variant, run_fbsd_cleanup_perf) and have no
# obligation of their own.
# Unwound at RING=4 (about a minute).  At its default ring 8 it has never
# produced a verdict (both builds still in the SAT solver after 35 minutes);
# the ring 8/16/32 result is carried by cleanup_step's hand-written invariant.
# This run is the independent whole-call cross-check at a ring it can reach.
RING=4 one cleanup run_cleanup.sh
one cleanup_fastfree run_cleanup_fastfree.sh
one xmit_step     run_xmit_step.sh
one xmit_offloads run_xmit_step_offloads.sh
one map_segs      run_map_segs.sh
one llq_path      run_llq_path.sh
one setup_errorpath run_setup_errorpath.sh
one setup_rx_intr run_setup_rx_intr.sh
one rx_errorpath  run_rx_errorpath.sh
one class_s       run_class_s.sh
one xstats_byid   run_xstats_byid.sh
one conc_datapath run_conc_datapath.sh
one admin_reuse   run_admin_reuse.sh
one probe         run_probe.sh
one lock_order    run_lock_order.sh
one fbsd_xmit     fbsd/run_fbsd_xmit.sh
one fbsd_cleanup  fbsd/run_fbsd_cleanup.sh
one fbsd_rx       fbsd/run_fbsd_rx.sh
one fbsd_down     fbsd/run_fbsd_down.sh
one fbsd_mtu      fbsd/run_fbsd_mtu.sh
one fbsd_sysctl   fbsd/run_fbsd_sysctl.sh

# Citations rot: a harness's `file.c:N-M` claim is about a location, and
# harness_cleanup_step.c goes further and SLICES its citation into the proof.
# Check both before believing any verdict below.
echo
echo "== source citations"
"$HERE/check_citations.py" || echo "  CITATIONS DRIFTED -- re-read the harnesses that cite the changed regions"
"$HERE/gen_cleanup_body.py" --check || echo "  the sliced cleanup body is stale"

echo
echo "== adjudication"
"$HERE/check_sweep.py" "$LOG" --expect-fail "$(echo ${EXPECT_FAIL:-} | tr ' ' ',')"