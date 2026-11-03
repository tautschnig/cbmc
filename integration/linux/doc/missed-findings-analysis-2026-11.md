# Why our pipeline missed the Velocity Labs amzn-drivers findings

**Date:** 2026-11-03
**Their run:** "Unveil on amzn-drivers", Soonho Kong, Velocity Labs / Agentic
AI, 27 Oct 2026, at the same commit we analysed (`9d5ca40`).  179 issues,
plus a large number of use-after-free reports.
**Two reports read in full:**

1. `eth_ena_xmit_pkts prefetches tx_pkts with the ring mask and reads past
   the caller's array` (DPDK ENA PMD, GPT-5.6 Sol run)
2. `efa_create_user_cq dereferences dev->eqs through an unchecked
   comp_vector when the device has no completion EQs` (Linux EFA, Opus 5
   run)

Both are real.  I verified each against the source and reproduced the first
one's defect independently.  Our pipeline reported neither, and the reasons
are structural rather than incidental.

## Both findings verified

### Report 1 (DPDK ENA TX prefetch)

Confirmed in the source at `9d5ca40`:

* `eth_ena_xmit_pkts` loops `sent_idx < nb_pkts` and prefetches
  `tx_pkts[ENA_IDX_ADD_MASKED(sent_idx, 4, tx_ring->size_mask)]`
  (`userspace/dpdk/ena/ena_ethdev.c:3292-3298`).
* `ENA_IDX_ADD_MASKED(idx, n, mask)` is `((idx) + (n)) & (mask)`
  (`ena_ethdev.h:66`); `size_mask = nb_desc - 1` (`ena_ethdev.c:1583`) with
  `ENA_MIN_RING_DESC` 128.
* The RX refill loop in the same file does it correctly:
  `if (likely((i + 4) < count)) rte_prefetch0(mbufs[i + 4]);`
  (`ena_ethdev.c:1802-1807`).

Independent check: `scan/amzn-drivers/cbmc/g1_dpdk_tx_prefetch.c` asserts the
prefetch index stays inside the caller's `nb_pkts` entries.  CBMC refutes it
immediately (`nb_pkts=8`, index 4..11 on an 8-entry array; the array-bounds
check fails too).  Their ASAN bundle is the stronger artifact -- it runs the
real extracted function and gets `heap-buffer-overflow` at `ena_ethdev.c:3296`
-- but the defect is confirmed either way.

### Report 2 (EFA `comp_vector`)

Confirmed, and it is **worse in mainline than in the out-of-tree driver**:

* `efa_create_eqs`: `neqs = min(max_eq, num_irq_vectors -
  EFA_COMP_EQS_VEC_BASE)`, then `dev->eqs = kzalloc_objs(*dev->eqs, neqs)`
  and `if (!dev->eqs) return -ENOMEM`.  With `neqs == 0` this is
  `kcalloc(0)`, which returns `ZERO_SIZE_PTR` (0x10), so the NULL check
  passes (out-of-tree `efa_main.c:385-391`, mainline `efa_main.c:330-336`).
* Probe then advertises `dev->ibdev.num_comp_vectors = dev->neqs ?: 1`
  (out-of-tree `efa_main.c:621`, mainline `efa_main.c:441`).  That `?: 1` is
  the crux: with zero EQs the driver still tells uverbs it has one
  completion vector.
* `ib_uverbs` validates only `comp_vector < num_comp_vectors`
  (`uverbs_std_types_cq.c:117`), so `comp_vector = 0` is accepted.
* The CQ path does `cq->eq = efa_vec2eq(dev, attr->comp_vector)` and reads
  `cq->eq->eeq.eqn`, where `efa_vec2eq` is `return &dev->eqs[vec];`.
  **Out of tree** the kernel-verbs path checks `comp_vector >= dev->neqs`
  (`efa_verbs.c:1852`) but the user path at `efa_verbs.c:2039` does not.
  **In mainline** only the user path exists (`efa_verbs.c:1205`) and there is
  no check anywhere.

So an unprivileged process with `/dev/infiniband/uverbsN` can oops the kernel
with one CREATE_CQ on a device that reports no event-queue attribute or got a
single MSI-X vector.  This should go through Amazon's internal security
channel together with our F1-F3/F6, not into a public patch.

## Why we missed them

### Gap 1: we never built a database for DPDK (coverage)

For FreeBSD and DPDK I only compared source by hand; no CodeQL database was
ever built, so every DPDK issue was outside the collector's reach.  That is
the shallowest reason and the easiest to fix -- I have now built one
(`/tmp/dpdk-ena-db`, DPDK v24.11 with the amzn tree overlaid, 108 functions
extracted from `ena_ethdev.c`, `eth_ena_xmit_pkts` present).

Coverage was necessary but **not sufficient**: with the database in hand I
ran all eight asset finders plus `device_tainted_sink.ql` and
`tainted_into_fixed_dest.ql` / `tainted_memcpy_size.ql` over it.  Total hits
on `eth_ena_xmit_pkts` or the prefetch line: **zero**.  Same for report 2 --
all ten queries on the EFA database report nothing matching
`efa_create_user_cq`, `comp_vector` or `efa_vec2eq`.

### Gap 2: our collector asks a different question

Every finder is of the form *"can an untrusted value reach a dangerous
sink?"*.  These two bugs are not of that form.

* **Report 1 needs no untrusted input at all.** The index is built from the
  driver's own ring mask and its own loop counter.  The bug fires on every
  short burst from any caller.  There is no taint source to find, so no
  amount of source modelling would surface it.
* **Report 2's value is user-controlled but the defect is not its
  magnitude.** `comp_vector = 0` is a perfectly ordinary value; it is
  dangerous only because the array it indexes has zero elements. The
  question is an agreement between two quantities (`num_comp_vectors`
  advertised vs `neqs` allocated), not the range of an input.

Both are **contract/invariant mismatches**: two length-like quantities are in
scope and the code bounds against the wrong one.  Our collector cannot
express that predicate at all.

### Gap 3: the sink model only covers fixed-size arrays

`tainted_count_into_fixed_array.ql` requires an `ArrayType` with a
compile-time size.  Both bugs index something else:

* `tx_pkts` is a `struct rte_mbuf **` parameter whose length is *another
  parameter* (`nb_pkts`);
* `dev->eqs` is a heap pointer whose length is a *sibling field*
  (`dev->neqs`).

`device_tainted_sink.ql` does have an `index-ptr` sink kind -- which is
exactly why it found our F4 and F5 -- but it demands a device-written
source, which neither bug has.

### Gap 4: our advisories actively suppress report 1's shape

We treat `adv_mask=MASKED` as a mitigation, on the assumption that a masked
index is in range.  Here the mask **is** the bug: `& size_mask` bounds the
index against the *ring*, not against the *object being indexed*.  A mask is
a mitigation only when the modulus is the size of the accessed object.  Even
if a finder had produced this candidate, our ranking would have pushed it
down as locally bounded.

### Gap 5: no lifetime asset, so zero recall on the use-after-free class

We have 23 queries and not one of them is about lifetime: the asset list is
A1-mem (count/index, decoded-len, skb/cursor), A1-UB, A2 infoleak, A3 CFI,
A4 availability, A5 authorization.  There is a design note
(`doc/cross-function-uaf-design-2026-05.md`) but no implementation.  Their
large batch of use-after-free reports is a class we cannot find by
construction, not one we searched and missed.  Honest recall there is 0/N.

### Gap 6: kernel allocation idioms are not modelled

`kcalloc(0)` returning `ZERO_SIZE_PTR` (so `if (!p)` passes) is the enabling
step of report 2.  "Allocation whose count can be zero, NULL-checked, then
indexed" is a mechanical pattern we do not look for, and it generalises well
beyond EFA.

### Gap 7: architecture -- CBMC only discharges, it never discovers

Our CBMC slice refuted report 1 in under a second *once pointed at it*.  But
CBMC runs only as stage 2 on candidates CodeQL proposed, so anything the
collector cannot express is unreachable no matter how strong the discharge
step is.  Their approach appears to go function-by-function and check it
against a contract, which finds unconditional violations with no taint
hypothesis.  That ordering difference explains most of the disjointness.

## What it takes to close Gap 2/3/4 (with evidence)

I prototyped the missing predicate as
`scan/abc-refinement/mask_object_bound_mismatch.ql`: an index that is
bounded by a *ring* modulus (`& size_mask`, `% ring_size`, a `*_mask` field)
but subscripts an object whose length is a *different* quantity in scope -- a
caller-supplied pointer parameter, or a heap field with a sibling count.

Results, no taint hypothesis anywhere in it:

| database | rows |
|---|---|
| DPDK ENA PMD | **1** -- `eth_ena_xmit_pkts\|3296\|obj=tx_pkts\|masked_by=size_mask\|actual_bound=nb_pkts` |
| amzn Linux ENA | 0 |
| amzn Linux EFA | 0 |
| drivers/net/ethernet/intel | 0 |
| drivers/net/ethernet/broadcom | 0 |
| drivers/nvme | 0 |

It finds their bug exactly, with no other hits.  Two refinements were needed
to get there, both instructive:

* The obvious "is the index compared against the bound?" advisory has to be
  left out. The enclosing loop *is* bounded correctly (`sent_idx < nb_pkts`),
  so asking whether the bound appears in any comparison suppresses precisely
  the bug. The signal is the mask on the subscript; the correct form of this
  code (the RX refill loop) indexes directly under a guard and is not
  reported.
* Pairing an array field with any count-ish sibling produced six false
  positives on EFA (`qp_table` indexed with `qp_table_mask`, which is the
  *matching* modulus). The discriminator is whether the modulus is named
  after the object being indexed: `qp_table` / `qp_table_mask` agree, while
  `tx_pkts` / `size_mask` do not.

## On the word "proof"

Worth being precise, in both directions.

* **Our CBMC results refute; they do not prove.** Every slice under
  `scan/amzn-drivers/cbmc/` is a hand-written model of a few lines of driver
  logic. A `VERIFICATION FAILED` with a concrete witness is strong evidence
  that the defect is real, which is all we claim. None of it is a proof about
  the driver, and `VERIFICATION SUCCESSFUL` on a slice (e.g. F2 with
  `MAX_SKB_FRAGS` 18) says only that the modelled property holds in the
  model, under its assumptions and unwind bound.
* **Their report says "Fix proved" and "977 theorems, 0 failed verification
  conditions", "its invariants proved (0 sorry)".** Read literally that is a
  proof about *their model* of the function, not about the C that ships. The
  `0 sorry` phrasing suggests a Lean development. That is a stronger artifact
  than a bounded slice, but the model-to-code gap is the same kind of gap as
  ours, and it is worth asking them exactly what the model abstracts before
  repeating the word "proved" about the driver itself.

Our own dynamic results are the one place we have something closer to
ground truth: the KASAN and ASAN reports in
`scan/amzn-drivers/dynamic/logs/` are observed executions of real driver
code, not models.

## Scalability note

`tainted_count_into_fixed_array.ql` does not complete on the DPDK ENA
database: it timed out at 900 s in the sweep and again at 3000 s with an
80 GB cap (`RC=124`), having produced no results.  So even for the asset we
cover best, this database would have contributed nothing.  Worth profiling
before the next wide run.

## Follow-ups, in order

1. Report the EFA `comp_vector` oops through the internal security channel.
   It is unprivileged-reachable and present in mainline with no check at all;
   it is more serious than anything in our own amzn-drivers list.
2. Build DPDK and FreeBSD databases as a standing part of the corpus.
3. Implement a lifetime/use-after-free asset (Gap 5) -- the largest single
   hole, and the one their run exercises most.
4. Promote `mask_object_bound_mismatch.ql` from prototype: add the
   `%ring_size%`/`q_depth` variants, run it corpus-wide, and measure.
5. Add the "zero-length allocation then indexed" pattern (Gap 6).
6. Revisit the mask advisory everywhere: only treat a mask as mitigating when
   the modulus provably matches the indexed object's size.
7. Profile the count/index finder for the timeout above.
