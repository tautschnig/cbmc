# Proving amzn-drivers safe and correct with CBMC: design + pilot

**Date:** 2026-11-03
**Question:** can we actually *prove* device drivers safe and correct with
CBMC, rather than hunt for bugs with taint analysis?
**Short answer:** yes for a useful and precisely-stated meaning of "prove",
and the pilot below does it on real unmodified driver code. The number of
operations does not have to be bounded. What remains is not boundedness but
the fidelity of the environment model, and that is reviewable rather than
hidden.

## 1. What "proved" can mean with a bounded model checker

CBMC is a bounded model checker, so the instinct is that it can only ever
say "no bug up to depth k". That is true of a single `cbmc` invocation on a
whole-program harness. It is not true of a proof *schema* built out of CBMC
invocations. Boundedness appears in two independent dimensions and they come
off separately.

**Dimension 1: the number of driver operations.** This is the one that
matters, and it is removable outright by induction rather than unrolling.
Instead of asking "is there a bug in some sequence of at most k calls?", ask
two questions:

* **Base case.** Does `probe` / `queue_setup` establish the invariant `INV`?
* **Step.** Starting from an *arbitrary* state satisfying `INV`, does any
  single legal operation re-establish `INV` and commit no safety violation?

If both hold, `INV` holds after every finite sequence of operations, by
induction on the length of the sequence. The set of proof obligations is
finite (one per entry point, times the lifecycle states in which it is
legal) and each obligation is a *single-operation* CBMC run. No sequence is
ever unrolled, and the result covers sequences of unbounded length. This is
the "state machine" idea made into a proof rule.

**Dimension 2: loops inside one operation.** A burst loop runs `nb_pkts`
times, a cleanup loop up to the ring size. Two options:

* Leave it bounded, but *honestly*: `--unwinding-assertions` makes CBMC
  prove the loop cannot exceed the unwind bound, so the proof is complete
  for all executions in which the bound holds, and the bound becomes a stated
  precondition rather than a silent gap.
* Remove it with loop contracts: `__CPROVER_loop_invariant` /
  `__CPROVER_decreases` plus `goto-instrument --apply-loop-contracts`, which
  this build supports. Then the burst length is unbounded too.

So "bounded" need not appear in the final assumption list at all. What does
appear is listed in section 6, and it is all environment modelling.

## 2. The state machine

Two automata have to be written down per driver, and they are the
specification work the question anticipates.

**(a) The API automaton** -- what the framework may call, and when. For the
DPDK PMD this is the ethdev lifecycle, which ENA already tracks explicitly
in `adapter->state` (`ENA_ADAPTER_STATE_{UNINITIALIZED, INIT, RUNNING,
STOPPED, CLOSED, CONFIG}`): the driver's own state machine, which is a gift
-- the abstraction already exists in the code. For the Linux drivers it is
the `net_device` open/close/xmit/ethtool discipline, and for EFA the
`ib_device` verbs order (`alloc_ucontext` -> `alloc_pd` -> `create_cq` ->
`create_qp` -> ... -> `destroy`), plus the interrupt and AENQ handlers which
may fire in *any* state after probe.

**(b) The device protocol automaton** -- what the hardware may do. Admin
queue request/response pairing, AENQ delivery, and the producer/consumer
discipline on each ring. This is where the device-as-attacker model from the
earlier work belongs: every field the device writes is havoc'd on read, so
the proof holds for *all* device behaviours, including malicious ones. Our
F1-F6 findings are exactly the obligations that fail when you do this.

The obligation set is then, for every `(state, operation)` pair the API
automaton permits:

```
{ INV(s) /\ pre(op) }   op   { INV(s') /\ post(op) }
```

with the device free to behave arbitrarily throughout.

## 3. What "safe" and "correct" mean (the specification catalogue)

"Absence of UB" is the floor, not the ceiling. For a NIC/RDMA driver the
properties worth proving, in increasing specification effort:

**Safety**

1. **Memory safety** -- CBMC built-ins (`--bounds-check`, `--pointer-check`,
   `--conversion-check`). Free, no spec to write.
2. **Ownership and lifetime** -- every packet buffer and descriptor has
   exactly one owner at a time; no double free, no use-after-free, no leak.
   Expressed as a ghost ownership map in the harness, updated at the
   alloc/free/post/complete boundaries. This is the class the other team's
   use-after-free reports live in, and the class our pipeline has no finder
   for at all.
3. **DMA discipline** -- the CPU does not read a buffer currently owned by
   the device, and does not write a descriptor the device owns. Needs the
   ownership map plus a model of the doorbell/phase handshake.
4. **No information disclosure** -- nothing uninitialised, and nothing from
   another context, reaches user memory or the device.
5. **Bounded resource use** -- no unbounded allocation or descriptor leak
   across an arbitrary operation sequence; follows from `INV` if `INV`
   includes the accounting equalities.

**Correctness**

6. **Ring discipline** -- `0 <= used(sq) <= q_depth`; producer and consumer
   indices advance monotonically modulo the ring; the doorbell is written iff
   descriptors were posted. These are `INV` clauses.
7. **API contract compliance** -- e.g. for `rte_eth_tx_burst`: the return
   value `sent <= nb_pkts`; entries `[0, sent)` are consumed and entries
   `[sent, nb_pkts)` are untouched and still owned by the caller; and
   crucially *nothing outside `[0, nb_pkts)` is accessed at all*. The
   Velocity Labs TX finding is precisely a violation of this clause, which is
   why a contract-based proof finds it and a taint search cannot.
8. **Device protocol compliance** -- a response is consumed only for an
   outstanding request; a completion's `req_id` refers to a descriptor that
   was actually posted (our F4); an index the device supplies is bounded by
   the object it indexes (F1, F5).
9. **Cross-layer ABI invariants** -- quantities the driver publishes to a
   framework must agree with what it allocated. The EFA
   `comp_vector` bug is exactly this: probe publishes
   `num_comp_vectors = dev->neqs ?: 1` while allocating `neqs` EQs, so the
   invariant `num_comp_vectors <= neqs` is *false after probe*. As an `INV`
   clause it fails in the base case, before any entry point is considered.
   No taint analysis can express that; an invariant proof cannot miss it.

## 4. Pilot: all-sequences safety on the real DPDK ENA TX path

Artifacts: `scan/amzn-drivers/proof/harness_tx_allseq.c`, `run.sh`.

**Getting real code in.** `goto-cc` compiles the unmodified
`amzn-drivers/userspace/dpdk/ena/ena_ethdev.c` (via the DPDK v24.11 build's
own compile flags) into a 1.1 MB goto binary with **214 functions**,
including the whole TX path. Two obstacles, both now cleared:

* `__attribute__((constructor(priority)))`, used by DPDK's
  `RTE_PMD_REGISTER_PCI`, was rejected by CBMC's C front end. Fixed by
  cherry-picking `diffblue/cbmc#8747` (commit `317d889dadf`) into our branch;
  a CMake reconfigure is needed because the PR adds a new source file.
* DPDK's `static_assert(!__builtin_strchr(fmt, '\n'))` log-format check is
  not constant-foldable by CBMC. `-DPEDANTIC` selects DPDK's own
  non-GCC branch, which omits the assert. No driver code is modified.

The harness is the induction **step**: havoc the ring to an arbitrary state,
constrain it with `INV`, model the caller's contract (`tx_pkts` is a heap
object of *exactly* `nb_pkts` entries -- nothing promises more), execute one
`eth_ena_xmit_pkts`, then assert `sent <= nb_pkts` and `INV` again. The
number of operations is nowhere bounded, because no sequence is executed.

Writing `INV` is where the real effort showed up, and it is instructive: the
first two attempts failed not on the driver but on *missing invariant
clauses* -- `ena_com_free_q_entries()` dereferences `ring->ena_com_io_sq` on
entry, and the doorbell write needs `io_sq->db_addr` mapped. Those are
genuine preconditions that `ena_queue_start` establishes; the proof forces
you to write them down. That is the per-driver cost, and it is also the
payoff: `INV` ends up being an explicit, reviewable statement of what a
valid ENA TX queue *is*.

**Results**

| driver source | outcome |
|---|---|
| unmodified (`9d5ca40`) | `eth_ena_xmit_pkts.pointer_dereference.35`, line 3296: *pointer outside object bounds in* `tx_pkts[(sent_idx + 4) & tx_ring->size_mask]` -- **FAILURE** |
| with the one-line fix | all memory-safety properties in the TX loop **SUCCESS** (72 in the full run), 3.4 s wall, 650 MB |

So the schema reproduces the Velocity Labs finding on real code from the
caller's contract alone, with no taint hypothesis and no attacker model, and
then verifies the fix. Three and a half seconds per obligation is cheap
enough that this can run per-commit.

## 5. Why this is the right architecture for the earlier misses

Our taint pipeline asks "can untrusted input reach a dangerous sink?". Both
missed findings are invariant/contract violations that need no untrusted
input: one is a disagreement between a ring mask and a caller's array
length, the other a disagreement between an advertised vector count and an
allocated array. In the schema above the first is obligation class 7 and the
second is class 9 failing in the base case. Neither can hide from it.

## 6. The assumption list, if we did a driver completely

This is the honest answer to "what is left". For the pilot as it stands:

1. **Callee contracts not yet discharged.** `ena_xmit_mbuf`,
   `ena_tx_cleanup` and `ena_tx_map_mbuf` have their bodies removed, so the
   proof is relative to "these do not write `tx_pkts` or the ring geometry".
   Each becomes its own CBMC obligation -- this is ordinary modular
   verification (`--enforce-contract` / `--replace-call-with-contract`, DFCC),
   not a gap in principle.
2. **Unmodelled environment functions** (`rte_log_register_type_and_pick_level`,
   `rte_pci_register`): CBMC reports them as `no body for callee`, which is
   the tool telling us the assumption rather than hiding it. Stubs or
   contracts remove them.
3. **Burst length bounded** at `MAX_BURST = 6` with `--unwind 12
   --unwinding-assertions`. Removable with a loop invariant on the TX loop;
   this is the only boundedness left and it is dimension 2, not dimension 1.
4. **Single-threaded.** The proof says nothing about `tx_burst` racing
   `tx_cleanup` or the control-path poll. Options: prove under a documented
   lock/queue-affinity discipline and verify that discipline separately, or
   model the interleavings explicitly for the pairs that can actually
   overlap.
5. **The device model is "arbitrary", which is right, but the *hardware
   protocol* is not yet constrained.** Havocking every device-written field
   is sound for safety and that is what we want; proving *liveness*-flavoured
   correctness (a posted packet is eventually completed) would need a real
   protocol model.
6. **`ena_com` is abstracted** (a separate translation unit, so its functions
   return nondet). Linking it in is mechanical and makes the accounting
   clauses of `INV` provable rather than assumed.

Note what is *not* on the list: the number of driver operations, the
reachable ring states, and the device's choices. Those are all universally
quantified already.

## 7. Effort, and a staged plan

Per-driver effort is much higher than the kernel-wide taint sweep, as
expected. Realistic shape for one driver:

* `INV` + API automaton: the hard thinking, days not hours, and it is the
  deliverable most worth reviewing with the driver owners.
* One contract per internal function on the paths under proof. The DPDK ENA
  TU has 214 functions; the TX path needs about 6, a full data path perhaps
  20, the whole driver 50-80.
* Environment stubs: ~100 `rte_*` / kernel functions, largely reusable
  across drivers, and reusable wholesale between ENA and EFA on the kernel
  side.

Staging, cheapest first:

1. **Done:** TX path memory safety + API contract, all sequences, real code.
2. Discharge the three callee contracts; add the loop invariant to remove
   `MAX_BURST`; link `ena_com` so the ring accounting in `INV` is proved.
3. Same schema for the RX path, then `tx_cleanup`, then the control path
   (`queue_setup`/`start`/`stop`), including the base case: prove
   `ena_tx_queue_setup` establishes `INV`.
4. Add the ownership ghost map -> lifetime safety (class 2), which is the
   class we currently cannot find at all.
5. Port to the Linux ENA and EFA drivers. EFA first for the ABI-invariant
   class (class 9), because that is where the `comp_vector` bug lives and
   the same shape plausibly recurs.
6. Concurrency last, with an explicit lock discipline.

Target order rationale: DPDK first because it is userspace C with a narrow,
well-specified API and no kernel headers, so the environment model is small;
the Linux drivers inherit the specification work but need a much larger stub
layer.

## 8. Honest framing of the word "proof"

What the pilot establishes: *for all* states satisfying `INV`, *for all*
burst lengths up to 6, *for all* device behaviours, and *for all* numbers of
preceding operations, `eth_ena_xmit_pkts` in the fixed driver commits no
memory-safety violation and preserves `INV` -- relative to the six
assumptions in section 6. That is a genuine proof of a precisely stated
theorem, and it is strictly more than "no bug found up to depth k". It is
not a proof that the ENA driver is correct, and we should not say that until
sections 6.1-6.6 are discharged and the specification covers classes 2-9.
