# What the induction proof covers, against the Unveil finding classes

Written to answer one question honestly: if the goal is to prove the classes
those 179 findings occupy, how much is actually done?

The short answer, as of 2026-11-13: **both queue types are closed under
induction, and four of ten classes are substantially covered for the paths
modelled** -- lifetime, wrong-state/outage, lost-event/stuck-queue, and leaks
-- with device-DMA-into-freed-memory arguably a fifth. Two more are partial
and one is untouched. The infrastructure that took longest to build (an
inductive invariant, an ownership ghost map, vacuity witnesses, a ghost
mapped-set and a device-untrusted model) did generalise cheaply, as predicted.

The qualifier "for the paths modelled" carries weight: this is the DPDK
driver's queue and lifecycle paths at small ring sizes, not the driver
entire. And one caveat below limits the error-path claim specifically.

*An earlier version of this summary said two operations of one queue type were
proven, covering one class of ten. That was accurate when written and stale by
the time it was read, which is why the detail below is now generated from
`doc/harness-index-2026-11.md` rather than maintained by hand.*

## Proven today

Updated 2026-11-13. The previous version of this section listed three
artefacts and said "there is still no end-to-end induction". **Both inductions
have since closed**, and the lifecycle, async and resource-ledger work landed
after it was written, so it understated coverage substantially. Regenerated
against `doc/harness-index-2026-11.md`, which is itself generated from the
harnesses.

### The two closed inductions

| artefact | what it establishes | evidence |
|---|---|---|
| **TX induction** (`run_induction.sh`) | setup, start, xmit, cleanup and stop together preserve `INV_STARTED`; every reachable TX ring state satisfies it | ring 4, 8 parts (d/b1/b2/m/s1/p/s2/s3) all `VERIFICATION SUCCESSFUL`, 16 vacuity witnesses all FAILURE (`induction-ring4.txt`) |
| **RX induction** (`run_rx_induction.sh`) | the same for the RX ring: setup, start, refill, recv, stop | ring 4, 6 parts all SUCCESSFUL, 17 witnesses all FAILURE (`rx-induction-ring4.txt`) |

`INV_STARTABLE` is what lets the two meet: start accepts either setup's or
stop's post-state, so the cycle closes rather than only the forward path.

### Depth beyond the induction ring size

| artefact | reach | how |
|---|---|---|
| cleanup step | **ring 8 and 16** over the driver's own sliced body; ring 32 parts 1 and 3 | `INV_CLEAN` mid-loop invariant plus a 3-part manual induction (ENTRY/STEP/EXIT); needed `INV_FREE_LIST_FAST` (bitmask, O(n), proved equivalent to the pairwise form by `FL_EQUIV`) and a 32-bit no-wrap bound. Body sliced by `gen_cleanup_body.py`, not transcribed -- see below. |
| xmit step | **burst bound removed entirely**, ring 8 and 16 | 2-part decomposition (STEP/EXIT) |
| xmit step with offloads | **`offloads == 0` restriction lifted**, ring 8 | `harness_xmit_step_offloads.c`; the meta-descriptor path is live and the invariants needed no strengthening. Found F24. |

### A fidelity caveat on the cleanup depth results

**Resolved 2026-11-13: the body is now sliced from the driver, not retyped.**

`harness_cleanup_step.c` still makes no *call* to `ena_tx_cleanup` -- that is
what removes the ring-size bound -- but it no longer contains a hand copy of
the loop body either. `gen_cleanup_body.py` slices
`ena_ethdev.c:3229-3253` verbatim into `cleanup_body_generated.h`, which the
harness includes after binding the driver's variable names to its own state.
The generator makes no editorial choice: nothing is rewritten and no branch is
dropped, so the fast-free branch is present too, dead under `offloads == 0`.
`gen_cleanup_body.py --check` fails when the slice goes stale.

So the invariant is now proven over the driver's own statements. What is still
not machine-checked is the *composition* -- that the three parts add up to the
loop -- which `ena-cleanup-ring8-2026-11.md` has always said.

**It costs more.** With the faithful body, ring 8 and ring 16 prove in all
three parts, as before. Ring 32 part 2 no longer finishes in the budget the
transcription needed; parts 1 and 3 still do. The extra cost comes from the
fast-free branch being present in the formula even though it is unreachable,
which is the price of removing the editorial step. Ring 32 is therefore
**re-established at parts 1 and 3 only** until that run lands.

*Attribution, corrected: `ena-cleanup-ring8-2026-11.md` already said this when
that artefact was written -- "it is a transcription, not the compiled source" --
and a commit of mine claimed it was undocumented. It was documented in the
artefact's own note; what was missing was this cross-artefact summary carrying
it, and any mechanism to detect drift. `check_citations.py` is now that
mechanism.*

The comparison is the useful part: `harness_xmit_step.c` **does** call the
driver, so the xmit depth results do not carry this caveat, and
`harness_cleanup.c` calls the real `ena_tx_cleanup` but reaches only ring 4 by
full unwinding. The driver's own cleanup loop is therefore proven at ring 4;
beyond that it is a transcription.

This also means the `offloads == 0` restriction **cannot** be lifted in the
cleanup step: the fast-free branch is not in the harness to enable. Setting
`ring.offloads` there would change nothing while looking like coverage, which
is worse than leaving it at zero. The branch is covered step-wise by
`harness_cleanup_fastfree.c` (F22) instead, which is where lifting it belongs.

Measured limit: ring 32 for xmit and ring 64 for cleanup reach no verdict in
9000 s under minisat2. The xmit bottleneck is the driver's data flow -- a
symbolic slot index into `tx_buffer_info[n]`, about 18x per ring doubling --
so this is not a solver or budget problem.

### Lifecycle, async and resource discipline

| artefact | obligations |
|---|---|
| `harness_dev_lifecycle.c` | `LC_STOP_FREE_ONCE`, `LC_STOP_STATE`, `LC_STOP_TX_STARTABLE`, `LC_STOP_RX_STARTABLE`, `LC_CLOSE_STATE`, `LC_DRV_STATS_ONCE`, `LC_CLOSE_IDEMPOTENT` |
| `harness_dev_start.c` | `DS_STATE`, `DS_TX_STARTED`, `DS_RX_STARTED`, `DS_QSTATE`, `DS_ERR_NO_HALF_START` |
| `harness_dev_configure.c` | `CFG_STATE`, `CFG_BUDGET` (discharges the watchdog's budget assumption), `CFG_AENQ_SUBSET`, `CFG_AENQ_NO_LSC`, `CFG_AENQ_FAIL` |
| `harness_watchdog.c` | `WD_QID`, `WD_RESET_STICKY`, `WD_EVENT`, `WD_NO_FREE` |
| `harness_aenq.c` | `AQ_HEAD`, `AQ_PHASE`, `AQ_NO_RESET`, `AQ_KEEPALIVE` |
| `harness_admin_cq.c` | `AC_HEAD`, `AC_PHASE`, `AC_HEADS_AGREE`, `AC_UNOCCUPIED` |
| `harness_leaks.c` | `LK_BALANCED`, `LK_NO_DOUBLE`, `LK_NO_WILD`, `LK_SETUP_FAIL` -- an acquire/release ledger over every exit path |
| `harness_dma_order.c` | `DMA_NO_FREE_WHILE_LIVE`, `DMA_ALL_RELEASED`, `DMA_NO_DOUBLE_FREE` -- a ghost "still mapped for device" set; this is what found F20 |
| `harness_conc_reset.c`, `harness_conc_admin.c` | `CC_RESET_NOT_LOST`, `CC_REASON_SOUND`, `CC2_NO_STALE_CQE`, `CC2_NO_LOST` |
| `fbsd/harness_fbsd_xmit.c` | `FBX_LLQ_COPY`, `FBX_MEMCPY`, `FBX_NO_UNSENT`, `FBX_PRODUCER`, `FBX_OWN`, `FBX_DMA_ORDER` |
| `fbsd/harness_fbsd_cleanup.c` | `FREE_ONCE`, `DMA_UNMAPPED_BEFORE_FREE`, `INV_NTC`, `OWN_NO_DANGLING` |
| `fbsd/harness_fbsd_rx.c` | `FBR_POSTED`, `FBR_NO_REUSE`, `FBR_DMA_ORDER`, `FBR_NTC` -- the RX assembly step, carrying **both F16 checks DPDK fails**: an unposted slot and a `req_id` repeated inside one chain. Ring 4 chain 2 and ring 8 chain 3, all clean, three witnesses failing. |

Sweep status: 25 runners `VERIFICATION SUCCESSFUL`, 5 marked `EXPECTED-FAIL`
because they reproduce a finding by design (`proof-sweep-2026-11.txt`).

## A caveat that limits several claims below

**Resolved 2026-11-13. Every harness now carries a vacuity witness.**

The count was also wrong: it was five, not six. `harness_rx_reqid.c` had a
witness all along, labelled `"witness: ..."` rather than
`"vacuity witness: ..."`, and the index matched only the longer form. Both
the index and the sweep adjudicator now match any label mentioning a witness.

Adding the missing witnesses immediately found a soft spot of exactly the kind
they exist to find. `harness_map_segs.c` asserts
`num_of_bufs <= ENA_PKT_MAX_BUFS` (17), and the new witness showed that at
`MAX_SEGS` of 6 and 17 the bound **cannot be approached in either
configuration** -- so the obligation held with a wide margin and would not
have caught an off-by-one at the real limit. It becomes meaningful only at
`MAX_SEGS >= 18` with an over-long chain; with a legal chain `num_of_bufs` is
bounded by `sgl_size`, which is below the limit, so the bound is unreachable
by construction. The witness is now scoped to the configuration where
reaching it is possible, and the measurement is recorded in the harness.

## Class by class

Share is of the 179 Unveil findings.

| | class | share | status |
|---|---|---|---|
| B | lifetime: UAF, double-free | 17 (9%) | **covered for both queue types and the lifecycle operations.** `FREE_ONCE` / `OWN_NO_DANGLING` for TX, `RXS_FREE_ONCE` / `RXS_NO_DANGLING` / `RX_NO_DANGLING` for RX, `LC_STOP_FREE_ONCE` across stop and close, and the same on FreeBSD. Was "core covered for the TX path only". |
| K | unrecoverable/wrong state, outage | 29 (16%) | **largely covered.** Both inductions preserve the started invariant; `DS_*`, `LC_*` and `CFG_*` cover start, stop, close and configure; `STOP_RESTARTABLE`, `RXS_RESTARTABLE` and `INV_STARTABLE` cover "the queue can be started again". Reset as a whole operation, remove, and XDP set are still open. Was "partial". |
| F | lost event, stuck queue, no progress | 17 (9%) | **largely covered.** `XS_WAKEUP` / `XS_DB_PENDING` (no packet pushed without a doorbell), plus the timer (`WD_*`), AENQ (`AQ_*`), admin completion queue (`AC_*`) and the two concurrency pairs (`CC_RESET_NOT_LOST`, `CC2_NO_LOST`). Was "nothing for the timer, AENQ or reset paths". |
| D | leak (memory, resource, IRQ, ID) | 17 (9%) | **covered for the paths modelled**, by the `harness_leaks.c` ledger: every acquire paired with a release on every exit path, including the setup-failure path. Was "not covered -- needs a ghost allocation counter"; that counter now exists. |
| A | device DMA or CQ writes into freed memory | 11 (6%) | **covered for the paths modelled**, by the `harness_dma_order.c` ghost mapped-set and FreeBSD's `DMA_UNMAPPED_BEFORE_FREE`. This is the class that found F20. Was "not covered". |
| C' | probe: `eth_ena_dev_init`'s error ladder | -- | **done, and it found F25.** `harness_leaks.c -DPROBE` runs the real `eth_ena_dev_init` with `ena_com`'s five acquire/release pairs modelled against the ledger. `PR_NO_LEAK` and `PR_LADDER_RUN` both fail: one failure exit of seven (`ena_set_queues_placement_policy`, `ena_ethdev.c:2365`) returns directly instead of jumping into the ladder, leaking the metrics buffer, the admin queue and the mmio read request. Clean run otherwise, both witnesses failing. |
| C | stale or still-live resource after a failed path | 18 (9%) | **partial, and see the caveat above.** `ERR_NO_PUBLISH`, `ERR_NOT_CONFIGURED`, `ERR_NO_LEAK`, `DS_ERR_NO_HALF_START`, `RX_RELEASE_NONNULL` and `F18_SETUP_RC` address it, and F10/F18 are this class, but three of the backing harnesses have no vacuity witness. |
| G | memory corruption / out-of-bounds | 9 (4%) | **partial.** Bounds checks are on in every run across both queue types, the lifecycle and the async paths. F19 (RSS reta overread), F22 (fast-free batch) and F24 (TSO header offset) are this class and were found by proof, not only by taint analysis -- a change from the previous assessment. |
| H | NULL / invalid dereference | 13 (7%) | **partial**, same basis, now across both queue types rather than TX alone. F10 and F16 are this class. |
| E | deadlock / hang / lockup | 7 (3%) | **covered for the admin lock.** `harness_lock_order.c` carries a ghost lock counter and three obligations: `LKO_NO_RECURSION` (a non-recursive spinlock taken twice is an immediate self-deadlock), `LKO_RELEASED` (every entry point returns with the lock free, so a held lock on return is a control-plane lockup) and `LKO_NO_STRAY_UNLOCK`. **Proven for all five entry points that take the lock** -- `ena_rss_reta_update`, `ena_rss_reta_query`, `ena_rss_hash_update`, `ena_rss_hash_conf_get` and `ena_stats_get` -- each with no other failing property, no reached bodyless callee, no UNKNOWN and both witnesses FAILING, and each adjudicated by `check_hygiene.sh`. `ena_rss_reta_update` is additionally **validated by mutation**: dropping its error-path unlock makes `LKO_RELEASED` fail and nothing else change. The driver has a single lock, so there is no lock *order* to violate; a cycle needs two. |
| I, J | integer wrap; uninitialised / info exposure | 3 (2%) | **partial** via conversion and bounds checks; F13 (negative error stored as a width) is this class. |
| Z | other | 40 (22%) | unclassified. |

**Honest summary of the change:** the previous version said one class of ten
was covered. Four classes (B, K, F, D) and arguably a fifth (A) are now
substantially covered for the paths modelled, two more (C, G/H) are partial,
and one (E) is untouched. The qualifier "for the paths modelled" is doing real
work in that sentence: coverage is of the DPDK driver's queue and lifecycle
paths at small ring sizes, not of the driver entire.

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

## Class S coverage against the `eth_dev_ops` table (2026-11-13)

Class S is the group the Unveil class list had no name for: an operation the
device **rejected** reported to the caller as **success**. The DPDK ENA driver
exposes 24 `eth_dev_ops` entry points; these are the ones that set something,
with the obligation each now carries.

| entry point | obligation | result |
|---|---|---|
| `dev_configure` | `CFG_AENQ_FAIL` (pre-existing) | holds |
| `mtu_set` | `SM_MTU_RC`, `SM_MTU_PUBLISH` | holds |
| `reta_update` | `SM_RETA_RC` | holds |
| `rss_hash_update` | `SM_HASH_RC` | holds |
| `dev_reset` | `DR_RC_SOUND` | holds |
| `dev_start` | via `F18_SETUP_RC` on `ena_setup_rx_intr` | **fails** (F18) |
| `dev_stop` | `SM_STOP_RC` | **fails** (F26) |
| `rx_queue_intr_enable` | `SM_INTR_RC` | **fails** (F17) |
| `rx_queue_intr_disable` | same shape as enable, not separately run | fails by inspection |
| `rx_queue_setup`, `tx_queue_setup` | `ERR_*` error-path obligations | hold |
| `tx_done_cleanup` | returns a packet count, not a status -- no class S obligation applies | n/a |
| FreeBSD `SIOCSIFMTU` | `SM_RC_SOUND`, `SM_NO_SILENT` | **fails** (confirms a Velocity Labs finding) |

Every run clean: no other failing property, no reached bodyless callee, no
UNKNOWN, no failing unwinding assertion, witnesses failing, each adjudicated
by `check_hygiene.sh`.

**Four of the eleven setters fail a class S obligation**, and three of those
four were already findings recorded under another class -- F17 as a
device-access fault, F18 as an error-path fault, F21 as sticky state. Naming
the class is what showed they share a shape: *the caller is told the operation
succeeded when the device, or the driver's own undo path, says otherwise.*

The one new finding is F26, `ena_stop`. The one confirmation is FreeBSD's
`SIOCSIFMTU`, which is Velocity Labs' finding and not ours.

**What this says about the method.** The obligation needed nothing invented --
two instances already existed -- and applying it across one table of entry
points turned up one new defect, re-classified three, and confirmed one of
theirs. That is a better return than any depth work in the same period, and it
is the argument for finishing the sweep on Linux ENA and EFA rather than
pushing ring sizes further.
