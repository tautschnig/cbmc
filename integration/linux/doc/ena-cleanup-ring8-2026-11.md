# The DPDK ENA cleanup step beyond ring 8: a hand-written loop invariant

**Status: the ring-size bound is gone.** The cleanup step's loop is no longer
unwound at all. Its invariant is written by hand and checked in three parts,
each of which is one iteration's worth of work, so none grows with the ring.
All three hold at **ring 8, 16 and 32**; at ring 64 the entry part holds and
the step part does not finish.

This supersedes the earlier version of this note, which recorded the
loop-contract attempt as not proven. That attempt and why it failed are kept
below, because the diagnosis is what led here.

## What is proven

`proof/harness_cleanup_step.c`, `proof/run_cleanup_step.sh`.

| part | statement | ring 8 | 16 | 32 | 64 |
|---|---|---|---|---|---|
| 1 ENTRY | the caller's `INV_STARTED` + `INV_FREE_LIST` + `INV_DESC_SUM` imply `INV_CLEAN` at the loop head | OK | OK | OK | OK |
| 2 STEP | one loop-body iteration preserves `INV_CLEAN`, cursor advanced and descriptors accumulated | OK | OK | OK | no verdict |
| 3 EXIT | `INV_CLEAN` plus the two post-loop writes gives `INV_FREE_LIST` and `INV_DESC_SUM` again | OK | OK | OK | not run |

Every run: no other failing property, no reached bodyless callee, no UNKNOWN,
no failing unwinding assertion, and the vacuity witness fails -- for part 2,
that a completion really is accepted and a packet really is freed.

**What is not machine-checked**, and is stated in the harness: that the three
parts compose into the loop, and that the body reproduced in part 2 matches
`ena_ethdev.c:3229-3253`. The reproduction is line-for-line and listed in a
comment beside it, but it is a transcription, not the compiled source.

## Two invariants that had to be invented

**`INV_CLEAN`, the mid-loop invariant.** `ena_tx_cleanup` copies
`next_to_clean` into a local, advances the local per completion, and writes
the ring's field back only after the loop; the descriptor accounting is acked
once, at the end. So `INV_FREE_LIST` and `INV_DESC_SUM` are **false in the
middle of the loop** -- slots are freed but the cursors do not know it -- and
neither can be a loop invariant. `INV_CLEAN` is the version parameterised by
the local cursor and the accumulated descriptor count, and it collapses to the
two originals at entry and at exit.

**The no-wrap bound, found by a failing proof.** The first `INV_CLEAN` stated
the descriptor accounting as a `uint16_t` equality, matching `INV_DESC_SUM`.
Part 3 then failed on `INV_STARTED`. The reason is that the equality alone is
satisfiable by wrap-around: a descriptor sum of 60000 and an accumulator of
5540 sum to 4 modulo 2^16, which passes, while leaving the sum far above the
queue depth, so `INV_SQ_ACCOUNTING` breaks after the ack. `INV_CLEAN` now
carries `desc_sum + acked <= q_depth - 1` computed in 32 bits. This is a
genuine weakness in the original `INV_DESC_SUM` that only showed up once the
accounting had to survive an intermediate state.

## The cost reduction that made it possible

`INV_FREE_LIST` states the free-list property by comparing every pair of
window positions: O(n^2), evaluated twice per run (assumed, then asserted).
That was the dominant cost, not the driver loop.

`INV_FREE_LIST_FAST` states the same thing with two bitmasks over the ring --
the ids the free window names, and the slots that are actually free -- plus a
popcount. Distinctness follows without comparing pairs: a repeated id sets one
bit twice, so the popcount falls below the window size. That is O(n).

`proof/harness_freelist_equiv.c` **proves the two forms accept exactly the
same states** at ring 4 and ring 8, with both vacuity witnesses failing, so
the cheap one can be substituted wherever the expensive one was used. The mask
is 64 bits, so the form is limited to n <= 64; at ring 64 with a 32-bit mask
CBMC reported "shift distance too large", which is how the limit was found
rather than assumed.

## What the loop-contract attempt established (kept for the record)

A DFCC loop contract was tried first and did not work. The diagnosis:

* The contract **is** applied and the driver loop **is** replaced, so it was
  never a plumbing problem.
* Without bounds on `rte_pktmbuf_free_bulk.0` and DFCC's own
  `__CPROVER_contracts_write_set_check_assigns_clause_inclusion.0` there is no
  verdict at all, at ring 8 or even ring 4. This is the
  havoc-makes-an-inner-loop-unbounded pattern of
  `dfcc-assigns-performance-2026-11.md`.
* With those bounds, ring 4 gives a verdict in about 20 minutes and 10 of
  1054 properties fail, because the invariant cannot carry `INV_DESC_SUM` or
  `INV_FREE_LIST`.
* Two hypotheses were tested and refuted: that the `__CPROVER_forall` clauses
  were the cost, and that a missing `pkt_cnt <= total_tx_pkts` clause
  explained it.

**The reason a loop contract cannot work here** is now clear: a CBMC loop
invariant must be a side-effect-free expression, and `INV_CLEAN` needs loops
over the ring. Expressing it as nested quantifiers is possible in principle
but the counting clause needs a sum. Doing the induction by hand sidesteps
that entirely, at the cost of the unchecked splice noted above.

## What remains

* Ring 64 part 2, and anything above 64, which needs a wider mask or the
  pairwise form.
* The fast-free branch of the loop (`offloads & MBUF_FAST_FREE`), excluded
  here; it batches frees through `pkts_to_clean` and needs its own clause.
* The xmit loop has had the same treatment, in
  `proof/harness_xmit_step.c`, with a different outcome worth recording. Its
  burst bound is gone outright -- the step holds for any burst length -- and
  its ring reach doubled from 8 to 16, but it does not scale as cleanup did:

  | | ring 8 | ring 16 | ring 32 |
  |---|---|---|---|
  | part 1 STEP | 171 s, 1.30 M clauses | 3141 s, 2.56 M clauses | no verdict in 3000 s |
  | part 2 EXIT | 9 s | 14 s | 22 s |

  The decomposition was also *simpler* there, because `ena_xmit_mbuf` updates
  `next_to_use` itself so the mid-loop invariant is the full invariant. What
  does not go away is the cost of writing a symbolic slot index into
  `tx_buffer_info[n]` along the `ena_tx_map_mbuf` path. For cleanup the
  bottleneck was the O(n^2) predicate and restating it helped; for xmit the
  bottleneck is the driver's own data flow, and restating predicates does not
  touch it. **A decomposition removes the bound its loop imposes and nothing
  else** -- worth knowing before applying the technique elsewhere.
