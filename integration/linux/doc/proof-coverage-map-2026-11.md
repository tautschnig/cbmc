# What the induction proof covers, against the Unveil finding classes

Written to answer one question honestly: if the goal is to prove the classes
those 179 findings occupy, how much is actually done?

The short answer: **two operations of one queue type are proven, and they
cover the core of one class out of ten.** The infrastructure that took the
longest to build -- an inductive invariant, an ownership ghost map, a vacuity
witness, and a device-untrusted model -- now generalises cheaply, but the
breadth is not there yet, and most of the remaining work is in classes we have
not started.

## Proven today

| artefact | what it establishes | evidence |
|---|---|---|
| xmit step (`harness_tx_callees.c`) | one `eth_ena_xmit_pkts` from an arbitrary `INV_STARTED` state preserves `INV_STARTED`, `INV_SLOT`, `INV_wakeup`, `OWN_UNIQUE`, `OWN_TRANSFER`, and `sent <= nb_pkts` | ring 8 / segs 2 / burst 1: 0 of 2388, vacuity witness FAILURE, 0 UNREACHABLE and 0 UNKNOWN under `--report-unreachable-properties`, 22 loop-contract properties SUCCESS including `Check step was unwound` |
| cleanup step (`harness_cleanup.c`) | one `ena_tx_cleanup` from an arbitrary `INV_STARTED` state preserves the invariant and the free discipline: `FREE_ONCE`, `OWN_NO_DANGLING` | ring 4: 0 of 7002, vacuity witness FAILURE, 0 UNREACHABLE, 0 UNKNOWN, loop fully unwound |
| setup base case (`harness_setup_basecase.c`) | `ena_tx_queue_setup` establishes `INV_CONFIGURED`, including under allocation failure | 8595 properties, no unmodelled callee |

What this is **not**: there is still no end-to-end induction, because
`ena_queue_start` establishing `INV_STARTED` is written but not discharged, so
the base case and the step do not yet meet.

## Class by class

| | class | share | status |
|---|---|---|---|
| B | lifetime: UAF, double-free | 17 (9%) | **core covered** for the TX path: `FREE_ONCE` is the double-free clause and `OWN_NO_DANGLING` the use-after-free clause, both proven. Only for TX, only for the two operations. |
| K | unrecoverable/wrong state, outage | 29 (16%) | **partial.** `INV_STARTED` preservation *is* "the queue is never left unusable", but only across xmit and cleanup. The lifecycle operations where the Unveil findings actually sit (start, stop, reset, remove, XDP set) are not covered. |
| F | lost event, stuck queue, no progress | 17 (9%) | **partial.** `INV_wakeup` -- no packet left pushed without a doorbell -- is exactly a lost-doorbell stuck-queue property, proven for xmit. Nothing for the timer, AENQ or reset paths. |
| G | memory corruption / out-of-bounds | 9 (4%) | **partial.** `--bounds-check --pointer-check` are on in every run, so the TX path is free of OOB at the proven geometry. Our own F1-F9 findings are this class but were found by taint analysis and CBMC slices, not by this proof. |
| H | NULL / invalid dereference | 13 (7%) | **partial**, same basis: pointer checks on, and the non-NULL clauses of `INV_STARTED`, for the TX path only. |
| C | stale or still-live resource after a failed path | 18 (9%) | **not covered.** One error-path obligation is written (`a failed start leaves no usable producer state`) and not yet discharged. |
| D | leak (memory, resource, IRQ, ID) | 17 (9%) | **not covered.** Needs a ghost allocation counter: every acquire paired with a release on every exit path. |
| A | device DMA or CQ writes into freed memory | 11 (6%) | **not covered.** Needs the device modelled as writing to regions after the driver frees them, i.e. a ghost "still mapped for device" set. Our device-untrusted model is the right starting point. |
| E | deadlock / hang / lockup | 7 (3%) | **not covered.** Needs a ghost lock-order invariant; a deadlock is a violation of it, so this is still safety, not liveness. |
| I, J | integer wrap; uninitialised / info exposure | 3 (2%) | **partial** via conversion and bounds checks. |
| Z | other | 40 (22%) | unclassified. |

By trigger, which the reconciliation called the more useful cut:

* **61% error-path-triggered.** The infrastructure exists -- the setup base
  case already injects allocation failure through `alloc_*_fail` flags, and
  the start harness models an untrusted device that can refuse every admin
  command -- but exactly one error-path obligation has been written and it is
  not discharged. This is the largest gap relative to effort required.
* **21% concurrency-triggered.** Zero coverage, and nothing in the current
  approach addresses it.
* 24% neither.

## What to do next, in value order

1. **Discharge the error-path obligations** (class C, and the 61% trigger).
   The pattern is one obligation per operation: *if the operation fails, the
   state is unchanged from before the call*. That is a strictly stronger and
   more useful statement than the invariant alone, it reuses the harnesses
   that exist, and it targets the single largest slice.
2. **Close the induction** (`ena_queue_start` -> `INV_STARTED`), without
   which none of the per-operation results compose.
3. **Extend the operation alphabet** to `ena_queue_stop`, `ena_down`, `ena_up`
   and the reset path (class K, 16%). Each is one more harness against the
   same invariant, which is now shared in `ena_inv.h`.
4. **Ghost allocation counter** for leaks (class D, 9%).
5. **Ghost device-ownership set** for DMA-into-freed (class A, 6%), building
   on the untrusted-device model already in the cleanup and start harnesses.
6. **Ghost lock-order invariant** for deadlock (class E, 3%).
7. **Bounded concurrency** for the overlapping pairs the reconciliation names
   (xmit vs `ena_down`, IRQ vs lock holder, devlink vs reset) -- the 21%.

Items 1-3 are reachable with the machinery that now exists. Items 5-7 each
need a new ghost structure, and item 7 needs a concurrency model we do not
have.

## A caution carried forward

Every "proven" row above is conditional on the geometry stated in its evidence
column, and the geometries are small: ring 8 and one packet for xmit, ring 4
for cleanup. Widening them is task #4 and is not free -- cleanup at ring 8 did
not finish in 2400 s. Nothing here should be read as "the ENA TX path is
proven correct"; it is "these obligations hold for these operations at these
sizes, and the proof is demonstrably non-vacuous at each point".
