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

## 6a. Stages 1 and 2: done

**Stage 1 -- callee contracts discharged.**
`scan/amzn-drivers/proof/harness_tx_callees.c`, `run_stage1_callees.sh`.
The real bodies of `eth_ena_xmit_pkts`, `ena_xmit_mbuf`, `ena_tx_map_mbuf`
and `ena_tx_mbuf_prepare` are now verified rather than assumed; the
assumption boundary moved out to the DPDK mbuf/mempool API and the
`ena_com` layer. **1229 properties pass**, no real failures.

Getting there required four invariant clauses that are not obvious from
reading the code -- each was found by a proof failure, which is the
argument for doing this at all:

1. `io_sq->db_addr != NULL` -- the doorbell register must be mapped;
   `ena_com_write_sq_doorbell` writes through it unconditionally.
2. `llq_info.descs_per_entry >= 1` -- `ena_com_sq_have_enough_space` and
   `ena_com_is_doorbell_needed` **divide** by it. `ena_com_config_dev_mode`
   sets it to `desc_list_entry_size / 16` with the entry size in
   {128,192,256}, so it is >= 8 in practice; the only guard on the entry
   size is a multiple-of-8 check, so a configuration with
   `llq_ring_entry_size_value < 16` would divide by zero. Worth flagging to
   the driver owners as a fragility, not a bug.
3. `(u16)(tail - next_to_comp) <= q_depth - 1` -- the SQ accounting bound.
   `ena_com_free_q_entries` returns `q_depth - 1 - used` as a *signed int*,
   so without this clause it goes negative and makes the `ena_tx_cleanup`
   branch of `eth_ena_xmit_pkts` reachable from a state the driver never
   reaches.
4. `empty_tx_reqs[i] < ring_size`, and every `tx_buffer_info` slot either
   free or referencing a live mbuf -- the `req_id` indirection.

Plus one harness lesson worth recording: `__CPROVER_havoc_object(&a[i])`
havocs the **whole enclosing object**, so havocking inside a loop that
builds a linked structure wipes the pointers written on earlier iterations.

**Stage 2 -- burst bound removed.**
`harness_tx_unbounded.c`, `tx_loop_contract.json`,
`run_stage2_unbounded.sh`. `nb_pkts` is unconstrained over the whole
`uint16_t` range and the TX loop is discharged by a loop invariant supplied
**out of band** via `goto-instrument --loop-contracts-file`, so no driver
source is annotated. CBMC is invoked with **no `--unwind` at all**;
`loop_invariant_base`, `loop_invariant_step` and `loop_assigns` all pass,
and 336 properties verify.

Two things worth noting. `--add-library` must run *before* `--dfcc`, or the
contracts library has no model for `free`. And the contract is load-bearing,
not decoration: the control run on the same program without it fails the
unwinding assertion at the TX loop (`[eth_ena_xmit_pkts.unwind.0] ...
FAILURE`), i.e. the loop genuinely is not bounded by any small constant.

So of the six assumptions in section 6, (1) is discharged for the TX path
and (3) is gone. What remains for this path: the DPDK mbuf API and
`ena_com` stubs, single-threadedness, and -- under DFCC -- that an
abstracted callee is reported as `undefined function should be unreachable`
rather than silently assumed, which is the tool being honest about an
abstraction we chose.

## 6b. Stage 3: the base case

`scan/amzn-drivers/proof/harness_setup_basecase.c`, `run_stage3_basecase.sh`.

**VERIFICATION SUCCESSFUL: 8595 properties, 0 failures, and no unmodelled
callees at all** -- the only run in this series where the assumption list for
the environment is empty. The obligation that matters:

```
[main.assertion.1] base case: ena_tx_queue_setup establishes INV_CONFIGURED: SUCCESS
```

This closes the induction for the configured state: the step proofs assume
`INV`, and setup is now shown to establish it rather than assumed to.

The lifecycle forced `INV` to be split, which matches the API automaton of
section 2 and is worth stating as part of the specification:

* `INV_CONFIGURED` -- geometry (`size_mask == ring_size - 1`, power-of-two
  ring), indices zeroed, the three allocations non-NULL, every
  `empty_tx_reqs` slot a valid `tx_buffer_info` index, every slot free
  (`rte_zmalloc_socket` zeroes). Established by `ena_tx_queue_setup`.
* `INV_STARTED` = `INV_CONFIGURED` + the `ena_com` clauses (`db_addr`
  mapped, `q_depth == ring_size`, `descs_per_entry >= 1`, the SQ accounting
  bound). Established by `ena_queue_start`, which is the next base-case
  increment.

Two results that are specification findings rather than proof mechanics:

* **`tx_free_thresh <= ring_size` is NOT established by setup, so it does not
  belong in `INV`.** When `tx_conf->tx_free_thresh != 0` the driver takes the
  application's value unchecked, and `rte_eth_tx_queue_setup` does not
  validate it either. The consequence is benign -- `available_desc <
  tx_free_thresh` is then always true, so `ena_tx_cleanup` runs on every
  burst -- but it is a performance cliff an application can trigger, and my
  first `INV` asserted something false about the driver.
* **The per-slot clauses are proved for an arbitrary index**, not by a loop:
  `g_probe_idx` is a nondeterministic global fixed before the call and used
  in both the invariant and the post-condition. Proving the single instance
  for a nondeterministic index *is* universal quantification over the index,
  and it avoids `__CPROVER_forall`, which CBMC silently ignores when it comes
  from a loop-contracts file (`warning: ignoring forall`).

Scope to be honest about: the ring size is concrete (8) because the init loop
is unrolled. Generalising over the ring size needs the loop-contract route,
which is blocked by the next item.

## 6c. A CBMC limitation worth chasing

`struct ena_ring` holds `empty_tx_reqs` and `tx_buffer_info` in two
**anonymous unions** (`ena_ethdev.h:158-167`). Accesses through them compile
to `byte_extract_little_endian(txq->$anon0, ...)`, and in the stage-3 setting
that produced property status **ERROR** -- neither proved nor refuted -- in
two configurations:

* under DFCC with a loop contract on the init loop, where the *driver's own*
  `txq->empty_tx_reqs[i] = i` reported ERROR, and the loop-contract
  obligations (`loop_invariant_step`, `loop_decreases`, `assigns`) came back
  ERROR with it;
* without DFCC, when the allocator stub returned `malloc`'d memory.

Pointing the allocator at static pools made all of it go away, which is why
stage 3 does that.

This is a **symptom, not a reduced reproducer**, and it should not be filed as
a bug until it is one. Two reductions are checked in under
`proof/cbmc-limitations/`, and both currently **pass**: a union-held pointer
to `malloc`'d memory dereferenced directly, and the fuller shape (union inside
a struct inside an array, pointer from an allocator-like function, symbolic
index, init loop). So the trigger is something these do not yet capture --
candidates are the allocation size, the number of unwindings, or the
interaction with DFCC's write-set instrumentation. Worth reducing properly,
because "property status ERROR" on real driver code is exactly the kind of
silent gap this whole exercise is meant to eliminate.

## 6d. Lifecycle failure injection: first results

Two factual results from starting the re-prioritised plan, both worth
recording because one of them corrects a guess of mine.

* **The TX error path is already covered.** `ena_com_prepare_tx` lives in a
  separate translation unit, so it is bodyless and returns a nondeterministic
  value; the stage-1 proof therefore already explores the
  `if (unlikely(rc))` failure branch in `ena_xmit_mbuf`, including
  `ena_trigger_reset`. Failure injection was free here, which is the argument
  for doing it everywhere.
* **A double-destroy I predicted does not exist.** Reading
  `ena_queue_start`/`ena_queue_start_all`, the RX populate failure path
  destroys the IO queue and leaves `ring->configured == 1`, and a later
  `ena_queue_stop_all` tests only `configured` -- which looks like a second
  `ena_com_destroy_io_queue` on the same queue. It is not a bug:
  `ena_com_io_queue_free` NULLs `cdesc_addr.virt_addr`,
  `desc_addr.virt_addr` and `bounce_buf_ctrl.base_buffer` after freeing each,
  so the second call is a no-op. (`ena_queue_start_all`'s `while (i--)` also
  correctly skips the queue that failed.) Hand-reasoning produced a
  plausible-looking bug that the code already defends against -- which is
  precisely why the proof, not the reasoning, is the deliverable.
* **Still open:** on the `ena_com_get_io_handlers` failure path
  `ena_create_io_queue` destroys the queue but `ring->ena_com_io_sq` and
  `ring->ena_com_io_cq` keep whatever that call left in them, while
  `ring->configured` stays 1. Whether any later path reads them is the next
  obligation, and it needs the ownership map rather than more reading.

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

**Re-prioritised 2026-11-04.** The Velocity Labs run (179 findings at the
same commit) shows this ordering was wrong: 61% of their findings are
error-path-triggered, 21% concurrency-triggered, and lifetime dominates the
classes, while bounds violations are 7%.  See
`unveil-reconciliation-2026-11.md`; the order below is superseded by section 6
of that document, which puts failure injection, the ownership ghost map and
the full lifecycle automaton ahead of RX and cleanup.

Staging, cheapest first:

1. **Done:** TX path memory safety + API contract, all sequences, real code.
2. **Done (section 6a):** callee contracts discharged for the TX path; the
   burst bound removed with a loop invariant. Still to do here: link
   `ena_com` so the ring accounting in `INV` is proved rather than assumed.
3. **Done (section 6b):** the base case -- `ena_tx_queue_setup` establishes
   `INV_CONFIGURED`. Next: `ena_queue_start` establishes `INV_STARTED`, then
   the RX path and `tx_cleanup` under the same schema.
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

## 8. Liveness: reachable with ranking functions, and mostly not liveness at all

My earlier claim that "13% of their findings are liveness, which a bounded
model checker cannot state" was too pessimistic. Two corrections, in
increasing order of how much they buy.

### 8.1 A ranking function reduces liveness to safety, exactly as induction reduces unboundedness

The same move that removed the bound on the number of operations removes the
bound on time. For termination, a **variant** (ranking function) `V(s)` into
a well-founded order with `V(s') < V(s)` on every step is a *safety* property
per step, and well-foundedness supplies the global conclusion. For general
liveness there is the standard liveness-to-safety reduction of Biere, Artho
and Schuppan (FMICS 2002) -- record a saved state and a progress flag, and a
liveness violation becomes a reachable "the loop closed without progress"
state -- and for liveness under fairness the termination-based treatment of
Cook, Gotsman, Podelski, Rybalchenko and Vardi (POPL 2007). Podelski and
Rybalchenko's transition invariants and the Terminator line of work are the
ranking-function synthesis side of the same story.

**We are already doing this.** The loop contracts in
`tx_loop_contract.json` and `setup_loop_contract.json` each carry a
`decreases` clause -- `nb_pkts - sent_idx` and `txq->ring_size - i`. Those
are ranking functions, and CBMC discharges them as ordinary per-step safety
obligations (`loop_decreases` properties). So the mechanism is not
speculative here; it is in the two proofs that already pass.

**Write the invariants and variants by hand and let CBMC check them.** CBMC
ships `goto-synthesizer`, but synthesis is not expected to carry this
workload and we should not plan around it. The evidence from stages 1-3 is
that hand-writing is both tractable and the more useful half of the exercise:
every clause we needed was identified *by a proof failure pointing at it* and
then written in a few minutes (`db_addr != NULL`, `descs_per_entry >= 1`,
the SQ accounting bound, the `req_id` indirection), and both ranking
functions were obvious on inspection (`nb_pkts - sent_idx`,
`txq->ring_size - i`). The failures do the search; a person writes the
clause. That division of labour also produces the artefact worth reviewing
with the driver owners -- an explicit statement of what a valid queue is --
which a synthesised invariant would not.

For a driver the shape is a **conditional** liveness statement, because
unconditional liveness is simply false -- a dead device never completes
anything. The obligation is:

> assuming the device eventually completes a posted descriptor (a fairness
> assumption on the environment, stated explicitly), the driver eventually
> frees the buffer and advances the consumer index.

and it is discharged by a variant over the driver's own state: on every step
in which the goal is unmet, `(descriptors outstanding, polls remaining)`
strictly decreases. The admin path is the easy case -- `ena_com_wait_for_comp`
already has a timeout, so the property is plain loop termination with a
variant, which is what `decreases` does.

### 8.2 Most of the "lost event / stuck queue" class is a safety invariant in disguise

This is the bigger correction. A lost wakeup is not a statement about the
future; it is a *reachable state* in which work exists and nobody is going to
look at it. Written as an invariant:

```
INV_wakeup:  work_pending  ==>  (interrupt armed  \/  poll scheduled)
```

Any interleaving that disarms the notification after the producer's last
check violates `INV_wakeup` in the state reached, with no temporal operator
anywhere. Same for the doorbell handshake (`pkts_without_db` set but no
doorbell write and no cleanup scheduled), and the same for deadlock, which is
a safety property over a ghost lock-order variable: "never acquire a lock
ranked below one already held". Their "hard lockup of a CPU when a completion
MSI-X interrupts a `cq->lock` holder" is exactly that shape.

So the triage of their classes E (7) and F (17) is:

* **deadlock / lockup** -> safety, via a ghost lock-order invariant;
* **lost event / stuck queue** -> safety, via the `INV_wakeup` handshake
  invariant above;
* **genuine unbounded-delay progress** (a completion that is merely slow, not
  lost) -> conditional liveness with a ranking function, per 8.1.

With those three in place the expressible share of their findings goes from
the ~80% in the reconciliation note to roughly 93%. The honest residual is
not "liveness" as a class but the cases where the fairness assumption on the
device is doing the real work -- there the proof is only as good as that
assumption, and it should be written at the top of the harness where a
reviewer will see it.

### 8.2a INV_wakeup demonstrated on the ENA TX doorbell handshake

Done, and it cost one assertion. ENA's pending-work flag on the TX path is
`pkts_without_db`: the TX loop sets it for every accepted packet, and only a
doorbell write clears it (`ena_ethdev.c:3300-3306`, plus the mid-loop LLQ
doorbell in `ena_xmit_mbuf`). The handshake obligation on return is therefore
that nothing is left pushed-but-not-rung:

```c
__CPROVER_assert(!ring.pkts_without_db,
                 "INV_wakeup: no packet left pushed without a doorbell");
```

It **holds**, in both configurations we have:

| configuration | result |
|---|---|
| real `ena_xmit_mbuf`/`ena_tx_map_mbuf` bodies, burst <= 2 (stage 1) | SUCCESS, 1230 properties, 0 real failures |
| callees abstracted, burst **unbounded** via the loop invariant (stage 2) | SUCCESS alongside `sent <= nb_pkts` and `INV` |

The two configurations are complementary and neither alone is enough: the
first has the mid-loop LLQ doorbell write in scope but bounds the burst, the
second covers every burst length but abstracts the callee that writes that
doorbell. Together they are reasonable evidence for the discipline; a single
proof with the real body and an unbounded burst needs the loop's `assigns`
clause widened to the ring footprint, which is the next increment.

The point for the plan is the cost: a lost-wakeup class that we had written
off as needing temporal logic turned into one assertion over a flag the
driver already maintains.

### 8.4 Ownership map, part 1: uniqueness and transfer (done)

The class we could not express at all -- use-after-free, double-free and lost
buffers, 25% of the Unveil findings -- starts with two clauses on the TX
path, both hand-written and both now proved against the real
`ena_xmit_mbuf`/`ena_tx_map_mbuf` bodies:

* **`OWN_UNIQUE`** -- no mbuf is owned by two `tx_buffer_info` slots, so an
  accepted packet can be neither freed twice nor silently dropped by being
  overwritten in place. Stated over *two* arbitrary slot indices fixed before
  the call, so proving the single pair is universal quantification over all
  pairs (the same trick as the single probe index in stage 3).
* **`OWN_TRANSFER`** -- an accepted packet is recorded in exactly the slot the
  free list named (`empty_tx_reqs[next_to_use]` captured before the call), so
  ownership moves from application to driver at a known place rather than
  being assumed.

Result with the real bodies: **1295 properties, 0 real failures**, with
`OWN_UNIQUE` and `OWN_TRANSFER` joining `sent <= nb_pkts`, `INV` preservation
and `INV_wakeup`.

**`OWN_UNIQUE` failed first, and the counterexample was the point.** From an
arbitrary `INV`-state, a slot may already hold the very mbuf the application
is about to submit; `eth_ena_xmit_pkts` then records it a second time and two
slots own one buffer, which on cleanup is a double free. That is not a driver
bug -- the `rte_eth_tx_burst` contract says the application must not hand back
an mbuf that is still in flight -- but it is an **obligation the application
carries that nothing in the driver detects**, and it had to be written down as
an explicit precondition before the proof would go through. Worth raising with
the driver owners: a buggy or malicious application re-submitting an in-flight
mbuf gets a silent double-free, and this is exactly the shape of the
Unveil class-B findings.

Next clauses for the map, in order: the free-discipline (`rte_pktmbuf_free`
only on a driver-owned mbuf, which needs the cleanup harness so the frees are
in scope), then DMA ownership (no CPU read of a buffer the device owns).

### 8.5a Why the widened-assigns proof hung -- and the wrong answer I gave first

**Corrected.** My first analysis of this blamed a quadratic cost in DFCC's
write-set check and proposed reporting the encoding upstream. That was wrong.
The full correction, with the controls that disprove it, is
`dfcc-assigns-performance-2026-11.md`; the short version:

* **The real cause is an unbounded inner loop.** Running the hung proof with
  `--verbosity 9` answers it immediately: 198 of 218 unwinding messages are
  `ena_tx_map_mbuf.0`, the `while ((mbuf = mbuf->next) != NULL)` segment walk
  at `ena_ethdev.c:3108`. The widened outer-loop contract havocs the ring, so
  the chain that uncontracted inner loop walks is unconstrained, and with no
  `--unwind` symex follows it forever. It was never a performance curve; it
  was non-termination.
* **DFCC was not at fault.** The superlinear growth I measured is present with
  no DFCC, no contract and no loop at all: one dereference of a pointer that
  may designate any of K objects costs 0.13 / 0.69 / 4.6 / 36.3 s for
  K = 32 / 64 / 128 / 256 (`deref_k_targets.sh`), and the SSA is only linear
  in K, so the time goes into intermediate expressions during symex.
* **The ENA harness was never in that regime anyway.** Measured with
  `goto-instrument --show-value-sets`, the largest points-to set anywhere in
  the TX path is **2 objects**. I had inferred "K is large" from 83
  static-lifetime symbols in the program, which is not the same quantity.
* The 178 KB `object_whole(adapter)` target, which I had called a multiplier,
  changes the formula by 513 variables out of 5.2 M.

Two methodological lessons worth keeping. When a proof hangs, ask the tool
what it is doing (`--verbosity 9`) before theorising about why. And a
synthetic benchmark only transfers to the real case if you *measure* that the
real case is in the same regime -- I inferred it instead.

### 8.5 Vacuity audit: two earlier claims retracted

Attacking the ring-size limit meant scaling the geometry, and that exposed
something worse than a performance limit. **Two results I reported were
vacuous.** Both are retracted here; the tooling that catches this is now in
the harness.

**The reachability gate.** `ena_xmit_mbuf` requires
`ena_com_free_q_entries(io_sq) >= nb_segs + 2`, and
`free = q_depth - 1 - used <= ring_size - 1`. So a k-segment packet is only
ever accepted when **`ring_size >= k + 3`**:

| ring_size | max exercisable segments |
|---|---|
| 2 | none -- no packet is ever accepted |
| 4 | 1 |
| 8 | 5 |
| 32 | 29 |

Measured, not just derived: `cbmc --cover location` on the ring-2 binary
reports **0 of 105** coverage goals in `ena_tx_map_mbuf` satisfied, and 0 of
13 on the chain-walk lines. At ring 8 it is 29/105 and 3/13.

**Retraction 1 -- the segment-range table in 8.5b is meaningless.** Those runs
(`MAX_SEGS` 6 / 12 / 17, all "VERIFICATION SUCCESSFUL, 5/5 obligations") were
at `RING=2`, where no packet is accepted at all. The segment parameter was
irrelevant because execution never passed the space check. "Covers the full
legal segment range" was wrong; it covered nothing past that check.

**Retraction 2 -- "item 1 is done" was wrong.** The claim was that one proof
held with the real callee bodies *and* an unbounded burst. It was run at
`RING=2`, so `ena_tx_map_mbuf`'s body, though linked in, never executed. The
proof genuinely covered `eth_ena_xmit_pkts` and `ena_xmit_mbuf` up to the
space check and nothing beyond.

**And the unbounded-burst configuration is vacuous for the accept path at any
ring size.** With the wide loop contract applied, the harness *proves*
`sent == 0`, and `sent == 1` is refutable -- so no packet is ever accepted:

```
assert(sent == 0)   SUCCESS      <- nothing is accepted
assert(sent == 1)   FAILURE
```

The loop invariant (`sent_idx <= nb_pkts` plus frame conditions) is too weak
to carry accept-path information across the contract, so every obligation
about an accepted packet -- `OWN_TRANSFER`'s `sent >= 1` branch, the
interesting case of `OWN_UNIQUE`, the force of `INV_wakeup` -- is empty in
that configuration. The bounded-burst configurations without a loop contract
do not have this problem, because the real loop executes; they are the ones
whose results stand, subject to the ring >= segs+3 rule above.

**What now has evidence, stated precisely.** Real-body, bounded-burst,
non-DFCC runs at `RING=4, SEGS=3`: the five obligations hold for
single-segment packets (ring 4 admits k=1), which is narrow but not vacuous.
Everything claimed beyond that is withdrawn pending re-measurement.

**The durable fix: a vacuity witness in the harness.** `VACUITY_CHECK=1`
compiles the same harness with `assert(sent == 0)`, which **must FAIL** for
the proof to be about anything; a SUCCESS means the accept path is
unreachable. `run_item1_realbodies_unbounded.sh` builds and runs both
variants and prints the witness, and warns when `RING < SEGS + 3`. The
harness carries a `#warning` for the same condition.

The lesson is the one from 8.5a in a sharper form. There I theorised instead
of asking the tool what it was doing; here I read `VERIFICATION SUCCESSFUL`
as "the property holds on this code" when it meant "the property holds on the
paths that execute, and almost none do". A proof needs a reachability
obligation next to every safety obligation, or it can be satisfied by an
empty program. Coverage is not a nicety here; it is part of the claim.

### 8.6 Closing the vacuity trap: the accept path, and what it costs

Re-establishing the baseline honestly, as 8.5 said was needed. Two bodyless
callees were keeping the accept path unreachable, and fixing them turned the
obligations from "all pass" to "most fail" -- which is the point.

**Why the accept path was dead.** Not a weak invariant, which is what I had
assumed. Measurement first: with `nb_pkts == 1` proved, `sent == 0` proved,
`sent == 1` refuted and `sent >= nb_pkts` refuted, the loop was exiting only
through `ena_xmit_mbuf`'s error `break`. The cause is that under DFCC a
**bodyless callee becomes an unreachable stub**, so every path through one is
cut, and two sat on the success path:

* `ena_com_prepare_tx` -- lives in `ena_eth_com.c`, which is not compiled in.
  Now stubbed in the harness with the contract the driver relies on: on
  success it consumes at most the `num_bufs + 2` descriptors
  `ena_xmit_mbuf` checked space for (`ena_ethdev.c:3140`) and advances the SQ
  producer by that much. Staying inside that bound is exactly what preserves
  the SQ accounting clause of `INV`.
* `__builtin_ia32_rdtsc` -- reached by `tx_info->timestamp =
  rte_get_timer_cycles()` on the success path (`ena_ethdev.c:3187`). It cannot
  be stubbed with `-D` (that collides with gcc's own `__rdtsc` in
  `ia32intrin.h`) nor from inside the harness TU, so `rdtsc_stub.c` defines it
  and is linked in separately. `goto-cc` accepts defining the builtin.

With both in place the vacuity witness finally **FAILS**, i.e. a packet can be
accepted and the obligations are about something.

**And then four of six fail.**

| obligation | before (vacuous) | now (accept path live) |
|---|---|---|
| `sent <= nb_pkts` | SUCCESS | SUCCESS |
| `INV_wakeup` | SUCCESS | SUCCESS |
| `INV` preserved | SUCCESS | **FAILURE** |
| `INV_SLOT` preserved | SUCCESS | **FAILURE** |
| `OWN_UNIQUE` | SUCCESS | **FAILURE** |
| `OWN_TRANSFER` | SUCCESS | **FAILURE** |

**The cause, measured not guessed.** The failing properties are
`INV_COM.pointer_dereference.* sq->next_to_comp / sq->q_depth: FAILURE` -- the
post-loop invariant *dereferences* `ring.ena_com_io_sq`, and the assigns
clause `__CPROVER_object_whole(tx_ring)` havocs the whole ring object
**including its pointer fields** (`ena_com_io_sq`, `ena_com_io_cq`,
`tx_buffer_info`, `adapter`, `push_buf_intermediate_buf`). After the loop
those pointers are arbitrary, so any invariant that follows them is
unprovable. Strengthening the invariant with the geometry and SQ-accounting
clauses was not enough and the step obligations come back `UNKNOWN`.

**Two consequences worth carrying forward.**

1. A havoc'd frame needs *pointer-stability* clauses in the invariant --
   `tx_ring->ena_com_io_sq == __CPROVER_loop_entry(tx_ring->ena_com_io_sq)`
   and so on for every pointer the invariant dereferences. Frame conditions
   and invariants are not independent: whatever the frame destroys, the
   invariant must restore.
2. This retrospectively justifies the precise-assigns work (actions 2 and 3)
   on grounds stronger than hygiene. Had the clause named only the fields the
   loop actually writes -- `pkts_without_db`, `next_to_use`, `tx_stats` --
   the ring's pointer fields would never have been havoc'd and the invariant
   would have survived for free. `object_whole` on an aggregate that contains
   pointers is actively hostile to proving anything about what those pointers
   reach.

So the next step is a *precise* assigns clause plus pointer-stability
invariant clauses, not a bigger invariant over a whole-object havoc. The
state of evidence meanwhile: `sent <= nb_pkts` and `INV_wakeup` hold on a
live accept path at ring 8, segments 2, burst 1; the other four are open.

### 8.7 DFCC does warn about a bodyless callee -- but the warning is drowned

Raised as a question: if DFCC turns a bodyless callee into an unreachable
stub, shouldn't that come with a property failure, as plain CBMC's
`no body for callee` does? **It does.** The problem is not a missing
diagnostic; it is that the diagnostic sits next to a pile of meaningless
passes.

From the source: both
`contracts/dynamic-frames/dfcc.cpp:491` and `dfcc_instrument.cpp:440` select
the `"assert-false-assume-false"` body generator, which emits
`assert(false); assume(false);`
(`generate_function_bodies.cpp:108-129`). The `assert(false)` is the
property; the `assume(false)` cuts the path.

Reduced to ten lines (`cbmc-limitations/dfcc_bodyless_callee_cuts_path.c`
and its `.sh`): one call to a declared-but-undefined function, followed by a
**deliberately false** `assert(0)`.

| | bodyless-call property | downstream `assert(0)` |
|---|---|---|
| plain CBMC | `no body for callee undefined_fn`: FAILURE | **FAILURE** (checked) |
| with DFCC | `undefined function should be unreachable`: FAILURE | **SUCCESS** |

So under DFCC a literal `assert(0)` is reported as SUCCESS. Three things make
this easy to walk into, and I walked into all three:

1. **The ratio.** One failure, next to arbitrarily many vacuous SUCCESSes.
   In the ENA harness two bodyless callees (`ena_com_prepare_tx`,
   `__builtin_ia32_rdtsc`) silently emptied every accept-path obligation
   while 1482 properties reported SUCCESS.
2. **The wording.** "undefined function should be unreachable" reads as a
   claim about the program -- *this code should not be reachable* -- rather
   than as a statement about the model: *I have no body, so I cut the path,
   and nothing after this point was checked*. Section 6 of this document
   previously described exactly this message as "the tool being honest about
   an abstraction we chose", which is how I came to ignore it.
3. **The asymmetry with plain CBMC.** The same goto binary verified with and
   without `--dfcc` differs in coverage enormously, and the only visible
   difference is which of two similarly-worded properties appears.

Mitigations we now apply, in order of reliability: an independent
**reachability witness** (`VACUITY_CHECK`, section 8.5) which does not depend
on reading the property list correctly; treating any
`undefined function should be unreachable` failure as a **hard stop** rather
than a known-abstraction note; and `--cover location` on the functions the
proof claims to exercise. An attempted CBMC-side mitigation was withdrawn --
see "Attempted mitigation, withdrawn" in `dfcc-issues-2026-11.md`; the
reachability witness and coverage are what we actually rely on.

Whether CBMC should change is for the maintainers, but the options look like:
word the message to say the path was cut, and/or suppress or mark as
`UNKNOWN` the properties that became unreachable only because of a generated
stub. The reproducer is self-contained and runs on an unmodified CBMC, so
unlike the earlier performance report this one is ready to file as it stands.

### 8.8 Status after the vacuity fix: the envelope is currently unestablished

Honest status, and it is worse than section 8.6 implied. With the accept path
live and `--report-unreachable-properties` in use, **no configuration
currently establishes the obligation set**:

| configuration | outcome |
|---|---|
| DFCC, field-precise assigns, ring 8, segs 2 | `INV`, `INV_SLOT`, `OWN_UNIQUE`, `OWN_TRANSFER` FAIL; `INV_wakeup` **UNREACHABLE** |
| non-DFCC, real bodies, ring 8, segs 2 | times out (1800 s) |
| non-DFCC, real bodies, ring 8, segs 5 | every obligation **ERROR** (solver undecided) |
| non-DFCC, real bodies, ring 4, segs 1 | did not finish in 1800 s with the current harness |

The field-precise assigns clause (`tx_loop_contract_fields.json`) did what it
was supposed to -- it names only the fields the loop writes, so the ring's
pointer fields are no longer havoc'd -- but it did not rescue the obligations,
because of the next item. And the new UNREACHABLE status immediately earned
its keep: `INV_wakeup` had been reporting SUCCESS in that configuration and is
in fact never reached.

Note the geometry rule from 8.5 bites hard here: `ring >= segs + 3` means a
ring of 4 admits only single-segment packets, so the cheap configurations are
also the least interesting ones, and the interesting ones do not finish.

### 8.9 A second DFCC obstacle, reduced

`cbmc-limitations/dfcc_callee_param_in_loop_assigns.{c,sh}`, with a passing
control alongside it (`dfcc_callee_locals_pass.c`):

```
callee writes its locals, no loop in callee      -> SUCCESS
callee has a loop and assigns its own PARAMETER
  in the loop condition                          -> FAILURE
     [callee.assigns.1] Check that m is assignable: FAILURE
     [callee.assigns.2] Check that acc is assignable: SUCCESS
```

A callee's parameter belongs to the callee's frame, not the caller's, yet DFCC
requires the **caller's** loop assigns clause to cover it -- and the caller
cannot express that, because callee locals are not nameable from the caller's
contract. A plain local written in the loop body is fine; it is specifically
the parameter assigned in the loop condition.

This is exactly `ena_tx_map_mbuf`'s shape:
`while ((mbuf = mbuf->next) != NULL)` assigns its own parameter. So proving
the TX path with the real `ena_tx_map_mbuf` body *under an outer loop
contract* is blocked on a CBMC bug, not on specification work. Unlike the
performance claim I withdrew, this is reduced to a self-contained reproducer
with a passing control, so it is reportable as it stands.

**What that means for the plan.** The two complementary configurations of
8.2a are not a stepping stone to one combined proof; they are the best
available until this is fixed:

* real callee bodies, **bounded** burst, no loop contract -- no DFCC
  involvement, so no obstacle, but it needs a geometry that both admits
  packets (`ring >= segs + 3`) and finishes;
* **unbounded** burst via the loop contract, callees abstracted -- no callee
  parameters in the region, so no obstacle, but the accept path is then only
  as real as the stubs.

The immediate next step is therefore not more invariant work. It is to find a
geometry in the first configuration that both admits packets and completes,
and to report the two DFCC issues.

### 8.3 What this changes in the plan

Add to the revised order in `unveil-reconciliation-2026-11.md`:

* after the ownership ghost map, add the **`INV_wakeup` handshake invariant**
  and the **ghost lock-order invariant**; both are cheap (a few ghost
  variables) and between them cover classes E and F;
* keep ranking functions for the genuine progress properties, reusing the
  `decreases` mechanism already in the loop contracts;
* state the device-fairness assumption explicitly in any harness that needs
  it, and never let it be implicit.

## 9. Honest framing of the word "proof"

What the pilot establishes: *for all* states satisfying `INV`, *for all*
burst lengths up to 6, *for all* device behaviours, and *for all* numbers of
preceding operations, `eth_ena_xmit_pkts` in the fixed driver commits no
memory-safety violation and preserves `INV` -- relative to the six
assumptions in section 6. That is a genuine proof of a precisely stated
theorem, and it is strictly more than "no bug found up to depth k". It is
not a proof that the ENA driver is correct, and we should not say that until
sections 6.1-6.6 are discharged and the specification covers classes 2-9.

## Next obstacle after the issue-B fix: the invariant, not the tool

With `e1fd15a9ef2` (issue-B fix) cherry-picked, the real-callee-bodies
configuration at ring 8 / segs 2 went from 25 failures to 6. Every step was a
missing invariant clause, not a CBMC defect. Worth recording because the
diagnosis was wrong twice before it was right.

**`next_to_use` was in the frame but not in the invariant.** The seven
`assigns` failures on writes through `tx_info` / `ena_buf` had nothing to do
with the anonymous union. The counterexample says it plainly:

```
next_to_use = 32768        <- in the assigns clause, so havoc'd
req_id      = 15525        <- empty_tx_reqs[32768], read out of bounds
tx_info     = &tx_buffer_info + 4719600
```

The loop contract havocs `tx_ring->next_to_use`, the body then computes
`req_id = empty_tx_reqs[next_to_use]` from an unconstrained index, and the
derived `tx_info` lands far outside the CAR, so the frame check correctly
fails. Adding `next_to_use <= size_mask` to the invariant fixed `INV` and all
seven. **The rule, again: whatever the frame destroys, the invariant must
restore.** Minimal cases for derived pointers at a symbolic index, with and
without an anonymous union, both pass -- see `cbmc-limitations/car_deriv.c`
and `car_union.c`.

Adding per-slot and ownership clauses at the probe indices then made
`INV_SLOT`, `OWN_UNIQUE` and `OWN_TRANSFER` follow, the last needing the
entry-to-slot link `sent_idx >= 1 ==> tx_buffer_info[empty_tx_reqs[
loop_entry(next_to_use)]].mbuf == tx_pkts[0]`, which the loop abstraction
otherwise loses. One of those clauses was first written with
`num_of_bufs <= 19`; `ENA_PKT_MAX_BUFS` is **17**, and 19 is the unrelated F2
frag-count value. The weaker bound let a slot satisfy the invariant and fail
the obligation.

### Not proven -- the invariant is not inductive

All six obligations report SUCCESS **and that does not mean they are proven.**
Two `loop_invariant_step` clauses FAIL, so the loop contract is not a valid
abstraction and the obligations rest on an invariant the body does not
preserve. The honest status is: the obligations *follow from* the invariant,
and the invariant is *not established*.

The reason is substantive rather than mechanical. To preserve `OWN_UNIQUE`
across one iteration, the body writes `tx_pkts[sent_idx]` into a free slot,
which requires that this mbuf is **not already owned by another slot**. The
harness assumes that of the initial state, but the step case starts from an
arbitrary invariant-satisfying state where the assumption is gone, and no
clause states it. It cannot be maintained by the driver alone: it is a
hypothesis about the **caller**, and it is exactly the third driver-contract
finding (re-submitting an in-flight mbuf is undetected and silently
double-frees). Closing it means adding the disjointness of in-flight mbufs to
the invariant as an explicit application obligation.

### Also, correcting an earlier claim

This configuration is **not** an unbounded burst. `nb_pkts` is capped at
`MAX_BURST` because `tx_pkts` is `malloc(nb_pkts * ...)` and each entry must
be a concretely initialised well-formed mbuf. Unboundedness in the burst
belongs to stage 2 (`harness_tx_unbounded.c`), where the callees are
abstracted. The two cannot yet be had together.

### A genuine driver finding, reproduced inside the proof

`eth_ena_xmit_pkts.pointer_dereference` fails on
`tx_pkts[(sent_idx + 4) & tx_ring->size_mask]`: the prefetch masks the index
with the **ring** mask, not the burst length, so whenever
`nb_pkts < ring_size` the driver reads past the end of the caller's array.
This is the `g1_dpdk_tx_prefetch` finding, now confirmed from the proof side
rather than from taint analysis. It is a read, so the impact is limited, but
it is out of bounds and can fault at a page boundary.

### The invariant is now inductive -- with one hole

Adding the in-flight disjointness clause closed the step case. Two clauses
were needed, and the reason each is needed is worth stating because neither is
a mechanical fix.

**In-flight disjointness at the probe slots.** `OWN_UNIQUE` can only break if
the body writes `tx_pkts[sent_idx]` into `req_id` while the *other* probe slot
already holds that same mbuf. So the step needs
`tx_buffer_info[g_pN].mbuf != tx_pkts[sent_idx]` for both probes. The harness
already assumed this, but only at `g_p1` / `g_p2`, and an assumption at a
nondeterministic index cannot be instantiated at the slot the driver actually
writes -- the harness says so itself at the `INV_SLOT` definition. It has to
be carried by the invariant to survive the havoc.

**The producer index is pinned before the first accept.** `OWN_TRANSFER` names
the slot `empty_tx_reqs[loop_entry(next_to_use)]`. In the step case the havoc
untethers `next_to_use` from its loop-entry value, so the body may take a
different slot. The missing fact is
`sent_idx >= 1 || next_to_use == loop_entry(next_to_use)`: before any packet is
accepted the producer index has not moved.

**Result at ring 8 / segs 2 / burst 1:** all 22 loop-contract properties
(2 base, 11 step, 2 assigns, 1 decreases, inner-loop unwinding) **SUCCESS**,
all six obligations SUCCESS, the vacuity witness still FAILURE, and
**zero UNKNOWN and zero ERROR** in 2282 properties.

**The hole, stated plainly.** `rte_pktmbuf_read` is `static inline` in
`rte_mbuf.h`; the harness translation unit carries the symbol without a body,
and the call from `ena_xmit_mbuf`'s LLQ push path resolves to that bodyless
copy. Its `undefined function should be unreachable` property **FAILS**, which
means the path is reached, so DFCC cuts it with `assume(false)` and the
obligations are **not checked on the LLQ-push sub-path**. By issue A that cut
is silent. Taking the function's address did not force emission. So the
current claim is: the obligations hold on the paths that do not push through
`rte_pktmbuf_read`, and the LLQ-push sub-path is unverified. Closing it needs
a linked definition in a separate translation unit, like `rdtsc_stub.c`.

Note also that `rte_pktmbuf_free` and `rte_pktmbuf_free_bulk` are bodyless
with their property reported **SUCCESS**, i.e. never reached -- which on the
xmit path is expected, but is exactly the reading issue A makes unreliable, so
it rests on the vacuity witness rather than on that SUCCESS.

### Why the earlier runs showed UNKNOWN everywhere

Before the stubs were added, 1280 of 2282 properties were UNKNOWN, including
every `loop_invariant_step`. That was not undecidability: checked one at a
time with `--property`, the step clauses came back SUCCESS. The failures
elsewhere in the body cut the paths that the step assertions sit on. Stock
CBMC prints **SUCCESS** for a statically discharged or genuinely unreachable
assertion (verified with a three-case probe), so UNKNOWN here meant
"undecided because the path was cut", and a run with failures in it silently
degrades the loop-contract verdicts. Practical consequence for this proof:
**a loop-contract obligation is only meaningful in a run whose other
properties all pass**, which is now the case bar the one hole above.

## Connecting the induction: status of the ena_queue_start base case

The gap was real and is now explicit. The data-path step assumes
`INV_STARTED`; `ena_tx_queue_setup` establishes only `INV_CONFIGURED`. A
proven base case and a proven step that do not compose prove nothing about
operation sequences, so this is the load-bearing piece.

**Landed.** The predicates now live in one place, `ena_inv.h`
(`INV_DEV_LLQ`, `INV_SQ_ACCOUNTING`, `INV_COM`, `INV_GEOMETRY`,
`INV_CONFIGURED`, `INV_STARTED`), and `harness_tx_callees.c` includes it
instead of carrying its own copies. The step now literally assumes and
asserts the same `INV_STARTED` the base case will have to establish, so the
two halves cannot drift apart again -- which is exactly how the gap arose.
The TX step was re-verified after the refactor: VERIFICATION SUCCESSFUL,
0 of 2388, vacuity witness still FAILURE, zero UNKNOWN.

**Written but not yet discharged.** `harness_start_basecase.c` sets up the
post-state of setup, assumes `INV_CONFIGURED` and `INV_DEV_LLQ`, calls
`ena_queue_start`, and asserts `INV_STARTED` on success plus an error-path
obligation that a failed start leaves no usable producer state. It compiles
and symex completes, but the SAT conversion does not finish within 1800 s.

Two cost problems were found and one is solved:

1. **Solved: control-plane proofs must not havoc structures holding function
   pointers.** `__CPROVER_havoc_object(&dev)` makes `dev_ops`,
   `tx_pkt_prepare` and `tx_pkt_burst` arbitrary, and CBMC's function-pointer
   removal then admits the whole dev_ops table as a call target, so symex
   wanders into `eth_ena_prep_pkts`, `eth_ena_xmit_pkts` and
   `__rte_raw_cksum` -- thousands of unwindings of code queue start never
   calls. Zero-initialised statics plus targeted havoc of just the fields the
   proof quantifies over fixes it, and is more honest about what is being
   generalised. Removing the datapath and statistics bodies confirms they are
   off the path: no `no body for callee` is reported for any of them.

2. **Open: the ena_com queue arrays.** `ena_com_create_io_queue` memsets
   `io_sq_queues[qid]` and `io_cq_queues[qid]` out of
   `ENA_TOTAL_NUM_QUEUES`-sized arrays inside a 182144-byte
   `struct ena_adapter`, and the DMA allocation underneath is unmodelled.
   That is what the solver is chewing on. The next thing to try is a contract
   for `ena_com_create_io_queue` and `ena_com_get_io_handlers` -- the stage-1
   callee-contract pattern -- which localises the remaining gap to two named
   ena_com post-conditions instead of an open-ended cost problem.

**Finding, recorded separately as F8/F9.** The admin-command boundary is
modelled soundly: `ena_com_execute_admin_command` returns an arbitrary status
with an arbitrary completion payload, because the completion is written by an
untrusted device. Under that model `db_addr` is an arbitrary pointer, since
the driver derives it as `reg_bar + cmd_completion.sq_doorbell_offset` without
any bound check. The harness therefore needs a named
`ASSUME_DEVICE_HONEST_DOORBELL` assumption to make `INV_STARTED` provable at
all, and that assumption is the finding.

## The second operation: ena_tx_cleanup

With only xmit, "all operation sequences" is a sequence of one operation type,
which is not a sequence. Cleanup is also the only place the TX path releases
an mbuf, so double-free, use-after-free and leaked descriptors are decided
here -- the classes the taint pipeline cannot express and 25% of the Unveil
findings.

`harness_cleanup.c` takes an arbitrary `INV_STARTED` state with arbitrary slot
occupancy, performs one `ena_tx_cleanup`, and discharges five obligations.
The cleanup loop is bounded by `cleanup_budget <= size_mask`, i.e. by the ring
size, which is concrete, so it is discharged by unwinding with
`--unwinding-assertions` and needs **no loop contract** -- which keeps this
proof clear of DFCC issues B and C entirely.

**Result at ring 4, one distinct mbuf per slot:** VERIFICATION SUCCESSFUL,
0 of 7002 failed, and under the strictest available setting (the
`cut-path-reporting` binary with `--report-unreachable-properties`) **zero
UNREACHABLE and zero UNKNOWN**, with the vacuity witness still FAILURE and no
unmodelled callee reached. `ena_tx_cleanup.unwind.0` is SUCCESS, so the loop
is fully unwound rather than bounded away.

| obligation | meaning |
|---|---|
| `INV_STARTED` preserved | the queue is still usable by any later operation |
| `INV_SLOT` preserved | free-list entries stay in range, descriptor counts bounded |
| `OWN_UNIQUE` | no mbuf owned by two slots after cleanup |
| `OWN_NO_DANGLING` | **no slot still owns a released mbuf** -- the use-after-free clause |
| `FREE_ONCE` | **no mbuf is released twice** -- checked inside the free model, at the only point where a double free is observable |

Device trust is modelled soundly: `req_id` comes from the completion queue the
device writes, so `ena_com_tx_comp_req_id_get` returns an arbitrary `req_id`
or "no more completions". The proof therefore covers a device that replays,
reorders or invents completions. Worth recording that the driver does guard
this properly -- `validate_tx_req_id` checks both `req_id < ring_size` **and**
`tx_info->mbuf != NULL` -- so unlike the EFA kverbs paths in F4/F5 this is a
guard that exists. Without the second half of that check, the `fast_free`
branch would dereference `mbuf->nb_segs` on a replayed completion.

### INV_DESC_SUM, discovered by proof failure

`INV_STARTED` initially failed. The counterexample: `tail == next_to_comp ==
32752` (nothing outstanding) while the slots claimed `total_tx_descs == 16`,
so `ena_com_comp_ack`, which is just `next_to_comp += elem`, drove
`next_to_comp` to 32768 and made the apparent in-flight count 65520 against a
`q_depth` of 8.

The missing invariant is that the descriptors outstanding on the SQ are
exactly the descriptors of the packets still in flight:

```
(uint16_t)(tail - next_to_comp) == sum of tx_descs over occupied slots
```

Our "arbitrary `INV_STARTED` state" was admitting states the driver can never
reach. This is a **sum**, so unlike every other clause so far it cannot be
stated by probing one nondeterministic index; the ring size is concrete in
this harness, so it is computed directly. Carrying it into `ena_inv.h` for the
xmit step is the obvious next move, and would need the same treatment there.

### Two harness defects worth recording

Both produced failures that looked like driver bugs and were not.

1. **A pigeonhole contradiction.** Slots chose mbufs freely from a pool of 2
   while the ring had 8 slots, so ownership uniqueness was impossible by
   construction and the harness was refuting its own setup. Each occupied slot
   now owns its own mbuf, which is also the real situation and the same
   application obligation the xmit step has to assume.
2. **A pool-size override.** The runner passed `-DMBUF_POOL=2` over the
   header default, so `&pool_mbuf[i]` ran past the pool for `i >= 2`. The
   runner now defaults the pool to the ring size.

Also fixed: the runner used `set -eu` with a `grep` for the verdict, so a
timeout exited silently with no output at all. It now says so.

### Cost

Ring 4 completes in a few minutes. Ring 8 did not finish in 2400 s on the
first attempt, because cleanup iterates the ring acking descriptors and the
`INV_DESC_SUM` computation adds a second pass, so cost grows with ring size
faster than the xmit step does.

## Widening the geometry (task #4)

Each point below is a separate run of the full pipeline, and each required the
vacuity witness to FAIL, so none of them is a proof about an empty path.

| ring | segs | burst | verdict |
|---|---|---|---|
| 8 | 2 | 1 | VERIFICATION SUCCESSFUL, 0 of 2388 |
| 16 | 2 | 1 | VERIFICATION SUCCESSFUL, 0 of 2388 |
| 8 | 3 | 1 | VERIFICATION SUCCESSFUL, 0 of 2388 |
| 8 | 2 | **2** | **VERIFICATION FAILED, 2 of 2388** |

### Why the property count does not change with the geometry

It looked wrong, and it is not. Comparing the two runs at segs 2 and segs 3,
the property *name* sets are identical (2388 each, zero differing). That is
because CBMC assigns a property id per goto **instruction**, not per
unwinding, so unrolling a loop more times adds SSA steps to existing
properties rather than new properties. The models do differ -- the goto
binaries are 1147567 and 1147575 bytes -- and the burst-2 row below proves
behavioural sensitivity to the geometry. Worth recording because an invariant
property count is exactly the kind of thing that should prompt a check: an
earlier run in this session reported a stale log as a fresh verdict, and the
runner now deletes its log before each run and reports a missing verdict
instead of exiting silently.

### Burst 2 fails, and it is the finding rather than a harness artefact

The two failing properties are `loop_invariant_step.9` and `.10`, which are
the in-flight disjointness clauses added for the two probe slots:
`tx_buffer_info[g_pN].mbuf != tx_pkts[sent_idx]`.

The cause is that `make_wf_mbuf` always returns `&segs[0]`, so with
`MAX_BURST = 2` the harness hands `eth_ena_xmit_pkts` **the same mbuf twice in
one burst**. The driver accepts both, records the same pointer in two
`tx_buffer_info` slots, and `OWN_UNIQUE` is genuinely violated -- which on
cleanup is a double free of one mbuf.

That is driver-contract finding 2 restated, and this is the first time the
proof *exhibits* it rather than assuming it away: at burst 1 the clause is
vacuously true because there is no second packet, which is exactly why it
passed before. Nothing in `eth_ena_xmit_pkts` detects the duplicate, so the
only thing standing between an application bug and a double free is the
application.

To prove the burst >= 2 case *under* the application obligation, the harness
needs one distinct mbuf chain per burst entry -- `make_wf_mbuf` taking a slot
index into a per-packet pool -- plus the pairwise distinctness assumption
stated over the burst, which is cheap because `MAX_BURST` is small and
concrete. That is the next concrete step, and it is a harness change, not a
specification problem.

### Cleanup at ring 8

Still does not finish: two attempts at 2400 s both ended in the solver, the
second reported honestly by the runner as `NO VERDICT` rather than silently.
Cleanup cost grows with ring size faster than the xmit step because it
iterates the ring acking descriptors and `INV_DESC_SUM` adds a second pass
over the slots. Ring 4 remains the proven point for cleanup.

## Two attempts that did not land, and what they established

### A loop contract for the DPDK cleanup is not needed

Tried, and withdrawn as the wrong tool. `cleanup_budget` is
`(free_pkt_cnt == 0) ? size_mask : free_pkt_cnt`, so the loop looks unbounded
in the caller's argument, but it also exits as soon as
`ena_com_tx_comp_req_id_get` reports no further completion, and
`validate_tx_req_id` rejects a slot whose `mbuf` is NULL. Since the body
clears each slot it cleans, the loop cannot run more than `ring_size` times.
At ring 4 the unwinding assertion `ena_tx_cleanup.unwind.0` is **SUCCESS**, so
the existing unwinding proof is already complete for that geometry and a
contract adds nothing.

The contract (`cleanup_loop_contract.json`) applies cleanly but the run times
out at 2400 s, and ring 8 still times out under plain unwinding even with
`--sat-solver cadical`, which the runner had never used. So the cleanup proof
stands at ring 4, complete, and widening it is a cost problem rather than a
soundness one. This is the opposite of the FreeBSD case, where the bound is
`ENA_TX_BUDGET` (128) irrespective of ring size and the contract was the only
route.

### The induction: the ena_com cost is gone, the wall moved

`ena_queue_start` establishing `INV_STARTED` is still **not discharged**, but
two things changed.

`ena_com_create_io_queue` and `ena_com_get_io_handlers` are now replaced by
**callee models in the harness** stating the weakest post-conditions
`INV_STARTED` needs: the SQ and CQ sized by the requested queue size, the
doorbell inside the register BAR, nothing in flight, and a valid LLQ geometry.
Each may fail, because the device may refuse any admin command. That does what
it was meant to do -- the remaining gap is now exactly two named statements
that must be verified against the real `ena_com` code, rather than an
open-ended cost problem. They are assumed, not proven, and the harness says so
at the point of definition.

The RX side was also removed (`ena_populate_rx_queue`,
`rte_pktmbuf_alloc_bulk`, the mempool dequeue path): symex had been exploring
it even though the harness starts a **TX** ring, driven by the function-pointer
expansion at `rte_mempool_ops_dequeue_bulk`. Nothing reports
`no body for callee`, so those bodies really are off the path.

With both changes symex completes -- 48,845 steps -- where before it did not
finish. The SAT conversion then does not finish, at ring 8 or ring 4, with or
without the standard checks. So the wall is now the propositional conversion
for this harness specifically, not the ena_com admin path.

The next thing to measure, rather than guess: the harness holds a
182144-byte `struct ena_adapter` as a static, and `ena_create_io_queue` writes
`ring->empty_tx_reqs[i] = i` through a pointer held in one of
`struct ena_ring`'s **anonymous unions** -- the same union opacity that
produced `byte_extract` expressions and unresolvable pointers in the stage-1
work. A union-held pointer written in a loop is a plausible cost driver and is
cheap to test by pointing `empty_tx_reqs` at a plain static array. Plausible,
and untested: the record of this project is that an untested mechanism is
usually the wrong one.
