# The DPDK ENA cleanup step at ring 8: what a loop contract did and did not buy

**Status: NOT PROVEN at ring 8.** The cleanup step remains proven at ring 4 by
bounded unwinding (part `s2` of `run_induction.sh`, which reports
`INDUCTION: CLOSED`). This note records what the loop-contract attempt
established, so the next attempt does not repeat it.

## What was tried

`cleanup_loop_contract.json.in` is a ring-agnostic replacement for the old
`cleanup_loop_contract.json`, which was hard-coded to four slots and whose
`assigns` clause was missing the fields `validate_tx_req_id`'s failure path
writes (`tx_stats`, `adapter->trigger_reset`, `adapter->reset_reason`). The
new one adds those, and states the array-bound fact the loop needs:
`pkt_cnt <= mbuf_cnt < 256`, which is what keeps `pkts_to_clean[pkt_cnt++]`
in bounds given `nb_segs >= 1`.

`run_cleanup_lc.sh` applies it with `--dfcc main --apply-loop-contracts`.

## What the measurements show

**The contract is applied and the driver loop is replaced.** `goto-instrument`
reports `Instrumenting 'ena_tx_cleanup'`, and `--show-loops` on the result no
longer lists `ena_tx_cleanup.0`. So the failure below is not a plumbing
problem.

**Without bounds on two other loops there is no verdict at all**, at ring 8 or
even at ring 4 (no verdict in 1500-6000 s; one ring-8 run was killed at a
150 GB address-space cap). The two loops are:

| loop | why it is unbounded here |
|---|---|
| `rte_pktmbuf_free_bulk.0` | called inside the havocked loop body when `mbuf_cnt >= 256`, so `pkt_cnt` is bounded only by 256 unless the invariant says better |
| `__CPROVER_contracts_write_set_check_assigns_clause_inclusion.0` | DFCC's own write-set inclusion check, introduced by the instrumentation |

This is the pattern `dfcc-assigns-performance-2026-11.md` already documents: a
loop contract havocs state, and an inner loop that was bounded in the concrete
program becomes unbounded in the havocked one. Adding
`--unwindset rte_pktmbuf_free_bulk.0:6,__CPROVER_contracts_write_set_check_assigns_clause_inclusion.0:24`
produces a verdict in about 20 minutes at ring 4.

**Two hypotheses were tested and refuted:**

| hypothesis | test | result |
|---|---|---|
| the `__CPROVER_forall` clauses in the invariant are the cost | re-ran with only the eight scalar clauses | still no verdict at ring 4 |
| the missing `pkt_cnt <= total_tx_pkts` clause alone explains it | restored that clause | still no verdict without the loop bounds above |

## Why it still fails once a verdict appears

With every loop bounded, ring 4 gives `VERIFICATION FAILED`, 10 of 1054
properties: `INV_STARTED`, `INV_DESC_SUM`, `INV_FREE_LIST`, `INV_SLOT`,
`OWN_NO_DANGLING` and `FREE_ONCE` all fail. That is the real remaining work,
and it is a specification problem, not a performance one: **the invariant is
too weak to re-establish the post-state obligations.** The contract havocs the
whole ring, so everything the harness asserts afterwards must be carried by
the invariant, and `INV_DESC_SUM` (a sum over occupied slots) and
`INV_FREE_LIST` (a permutation property over a window of the free list) are
not in it. `OWN_UNIQUE` is the one obligation that does pass.

## What the next attempt should do

1. Put the full `INV_DESC_SUM` and `INV_FREE_LIST` relations into the loop
   invariant, quantified over the ring, and check each is inductive on its own
   before combining them. The old contract's note that "three step clauses are
   not inductive" is still the live problem.
2. Keep the two loop bounds above; they are not optional.
3. Expect to need the moving-window relation: the loop advances the local
   `next_to_clean` and writes `empty_tx_reqs[next_to_clean]`, so the free-list
   window moves, and the invariant must describe the window at an arbitrary
   point in the loop rather than only at entry and exit.

## Honest summary

The loop contract removed the ring-size dependence of the *driver* loop, which
was the point, but it did not make the step provable at ring 8: the cost moved
to DFCC's own instrumentation and to the harness's whole-ring invariant
evaluation, and the invariant does not yet imply the obligations. Ring 8 for
this step is open.
