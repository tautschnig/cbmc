# Towards full coverage of the DPDK ENA PMD: plan, against the Unveil classes

This plan supersedes the "what to do next" list in
`proof-coverage-map-2026-11.md`. "Full coverage" here means that every entry
point of the driver is an operation of one induction over operation
sequences, and each preserves one shared invariant. Each operation is also
proven memory-safe, on success and on every error path, against an untrusted
device. The invariant itself must imply the safety and ownership properties
behind the Unveil classes. The plan says below which classes that cannot reach
and why.

## The operation alphabet

`ena_ethdev.c` has 4224 lines, `base/ena_com.c` 3465, `base/ena_eth_com.c` 746
and `ena_rss.c` 590. Every way control enters the driver:

| group | entry points | state touched | status |
|---|---|---|---|
| TX queue | `ena_tx_queue_setup`, `ena_queue_start` (TX), `ena_queue_stop` (TX), `ena_tx_queue_release` | `ena_ring` (TX), io SQ/CQ | setup, start and stop proven. Release has F10. |
| TX data | `eth_ena_xmit_pkts`, `ena_tx_cleanup` (also `.tx_done_cleanup`), `eth_ena_prep_pkts` | TX ring, LLQ | xmit and cleanup proven at ring 4. Prep not started. |
| RX queue | `ena_rx_queue_setup`, `ena_queue_start` (RX, `ena_populate_rx_queue`), `ena_queue_stop` (RX), `ena_rx_queue_release` | `ena_ring` (RX) | setup, start and stop proven (RX induction closed at ring 4). `ena_rx_queue_release` still has the F10 shape. |
| RX data | `eth_ena_recv_pkts` (with its refill), `ena_rx_queue_intr_enable/disable` | RX ring, device completions | recv and refill proven at ring 4, chains of 2, under `ASSUME_NO_F16`. The interrupt-enable pair is not started. |
| device lifecycle | probe (`eth_ena_dev_init`), `ena_dev_configure`, `ena_start`, `ena_stop`, `ena_close`, `ena_dev_reset`, remove (`eth_ena_dev_uninit`) | adapter state, all queues, admin queue, AENQ, timers | device init's LLQ and sgl facts proven; `ena_start`, `ena_stop` and `ena_close` proven at ring 4 (`run_dev_start.sh`, `run_dev_lifecycle.sh`), closing the lifecycle loop at device level. `ena_dev_configure`, probe, remove and `ena_dev_reset` not started. |
| async | `ena_control_path_handler` / `_poll_handler` (admin completions, AENQ: link change, notification, keep-alive), `ena_timer_wd_callback` (keep-alive, admin state, TX completion check, `trigger_reset`) | adapter, AENQ ring, per-queue timestamps | the watchdog (`run_watchdog.sh`) and the AENQ dispatch (`run_aenq.sh`) proven. The admin-completion half of the control-path handler is not started. |
| config and query | `ena_mtu_set`, RSS reta/hash update and query, `ena_stats_get`, xstats (4 functions), `ena_infos_get`, `ena_link_update` | admin queue, RSS table | not started |
| multi-process | `ena_mp_primary_handle` (requests from secondary processes) | admin queue, adapter | not started |

## The invariant hierarchy

* **Per TX queue: `INV_STARTED+` and `INV_STARTABLE` (have).** These cover
  slot ownership, the free list, the descriptor sum and LLQ state.
* **Per RX queue: `INV_RX_STARTED` and `INV_RX_STARTABLE` (have).**
  `INV_RX_STARTED` has the free list over `empty_rx_reqs`, one owner per
  posted buffer, and the accounting clause tying the ring's in-flight count
  to the SQ's used entries -- which the refill proof showed is needed, since
  without it "free slots" and "free SQ entries" are unrelated and refill
  called with the latter can overrun the free list. `INV_RX_STARTABLE` is
  what `ena_queue_start` assumes: configured, geometry, every slot free.
  Still missing: "no mbuf is handed to the application while the device can
  still write to it", which belongs with phase 7.
* **Per device (new): `INV_DEV`.** It covers:
  - the adapter state machine (init, configured, running, stopped, closed,
    with `trigger_reset`);
  - which queues are configured or started, against `nb_tx_queues` and
    `nb_rx_queues`;
  - admin-queue and AENQ ring indices;
  - the timer and alarm registration state.

  The per-queue invariants are quantified over a probe queue index, the
  pattern the slot proofs already use.

## The device model

The device is untrusted throughout, as in the start and cleanup harnesses.
Every value it supplies is nondeterministic:
- RX completions: `req_id`, descriptor count, lengths, flags;
- AENQ entries: group, syndrome, phase;
- admin completions.

Device-precondition assumptions are kept to a named list, as in the TX ledger
(F8, F13, F14), and each one is either a finding or a stated restriction.

## Phases, in order

Each phase ends the same way as the TX induction: a runner that reports every
part, vacuity witnesses that must fail, and an assumption ledger.

1. **TX lifecycle closed (this commit).** The parts are device init, setup,
   start, xmit, prepare_tx against the real code, cleanup and stop, with stop
   leading back to start. The remaining TX gaps are `eth_ena_prep_pkts`,
   offloads (`offloads == 0` is a harness restriction), host-placement mode,
   `tx_free_thresh > 0` (xmit calling cleanup) and meta caching disabled.
   Each is one more configuration of an existing harness.
2. **RX path -- done for the datapath and lifecycle** (`run_rx_induction.sh`,
   RX INDUCTION: CLOSED at ring 4). Six parts: setup, start, the
   `ena_com_rx_pkt` contract against the real code, recv, refill and
   release. B is covered (`RXS_FREE_ONCE`, `RX_OWN_ONCE`, `RXS_FREES_POSTED`);
   G and H are covered for these operations by the bounds and pointer checks.
   It did find the DPDK device-trust gap the Linux F1-F7 class predicted:
   **F16**, an unchecked device `req_id` dereferenced in `ena_rx_mbuf`, which
   Linux and FreeBSD both check.

   RX leftovers now settled: `ena_rx_queue_intr_enable/disable` gave **F17**
   (the device queue is dereferenced before the port is started), and
   `ena_rx_queue_release`'s F10 shape is proven rather than read
   (`harness_rx_errorpath.c`). Still open on RX: the `max_rx_sgl_size == 0`
   reset question, and ring 8 with chains (does not finish in 3000 s).
3. **Device lifecycle and error paths -- largely done** (class C, and the 61%
   error-path-triggered findings). `ena_stop` and `ena_close`
   (`run_dev_lifecycle.sh`, ring 4, one and two queues per direction) and
   `ena_start` (`run_dev_start.sh`, ring 4, one queue) are proven, with the
   error-path obligation `DS_ERR_NO_HALF_START` holding.

   The payoff is the composition: `LC_STOP_TX_STARTABLE` /
   `LC_STOP_RX_STARTABLE` and `DS_TX_STARTED` / `DS_RX_STARTED` are exactly
   the predicates the per-queue inductions use, so the device operations and
   the per-queue data paths now compose over **every** queue rather than the
   one a per-queue harness drives.

   This phase produced **F18** (`ena_setup_rx_intr` returns success after
   undoing its own setup) and, with the RX work, **F17**. Still open:
   `ena_dev_configure`, probe (`eth_ena_dev_init`), remove, and
   `ena_dev_reset`; and two queues per direction for `ena_start` does not
   finish in 3000 s.
4. **Async handlers -- done for the two main entry points** (classes F and K).
   - The **watchdog** (`run_watchdog.sh`): `WD_QID`, `WD_RESET_STICKY`,
     `WD_EVENT`, `WD_NO_FREE`, under `--div-by-zero-check` as well, including
     the zero-TX-queue configuration. This is the mechanism F15 relies on for
     recovery.
   - The **AENQ dispatch** (`run_aenq.sh`): `AQ_HEAD`, `AQ_PHASE`,
     `AQ_NO_RESET`, `AQ_KEEPALIVE`, over a ring of arbitrary device bytes
     with the real handler table.

   `AQ_PHASE` is the class F property for this path: flipping the phase
   without a wrap would skip live events, failing to flip on a wrap would
   re-read consumed ones.

   Still open: the admin-completion half of `ena_control_path_handler`, and
   "a reset request is eventually consumed by `ena_dev_reset`", which needs
   the reset operation itself (phase 3 leftover).
5. **Config and query operations** (class G). Untrusted admin responses
   indexed into driver arrays are the F8/F9 shape, for example the RSS
   indirection table size and the xstats counts.
6. **Leaks** (class D). Add a ghost allocation counter over `rte_zmalloc`,
   `rte_memzone` and mbuf get/put. The obligation is that across a full
   setup→start→…→stop→close sequence, live allocations equal those reachable
   from driver state. F15's overwritten mbuf is a leak this would catch
   directly.
7. **DMA after free** (class A). Add a ghost "device may access" set. A
   region enters it when it is given to the device (queue create, RX post)
   and leaves when the device is told to stop (destroy-queue admin
   completion). The obligation is that nothing in the set is freed. This
   needs the real `ena_com_destroy_io_queue` admin path, which the stop step
   currently leaves out.
8. **Concurrency** (the 21%). CBMC's thread support does not scale to this
   code, so the plan is bounded interleaving by hand: two operations whose
   shared accesses are interleaved at nondeterministic scheduling points.
   The DPDK threading model limits the pairs:
   - datapath per queue is single-threaded by contract;
   - the app control thread runs against the interrupt/alarm thread
     (`ena_control_path_handler`); both use the admin queue;
   - the timer's `trigger_reset` runs against the datapath;
   - secondary-process requests run against the primary's control path.

   Each pair is one harness. This covers named pairs, not all interleavings.
9. **Scale and other targets.**
   - Ring 8 and larger through loop contracts. The cleanup attempt is
     measured and written up in `ena-cleanup-ring8-2026-11.md`: **not
     proven**, and the remaining obstacle is that the invariant does not
     carry `INV_DESC_SUM` or `INV_FREE_LIST`, not performance. The xmit step
     has been checked at ring 8 / burst 2.
   - **The FreeBSD xmit side is done** for the submit step
     (`harness_fbsd_xmit.c`): seven obligations including `FBX_NO_UNSENT`,
     which is the F12 check FreeBSD passes and DPDK fails, and
     `FBX_DMA_ORDER`, a class A obligation DPDK cannot express. FreeBSD RX
     and its queue lifecycle are not started.
   - The Linux ENA driver, where most Unveil findings sit and which shares
     `ena_com`. Phases 1–5 transfer with new harness glue, not new
     invariants.

## What "full" cannot mean here

* **Unbounded sizes.** Each result holds at its stated ring, burst and
  segment bounds unless a loop contract replaces the unwinding. Ring 4 for
  the composed TX induction is small.
* **Exhaustive concurrency.** Phase 8 covers named pairs at bounded
  interleaving depth.
* **The device itself.** The proofs hold for every device behaviour the model
  allows, and the model is no stronger than the named device assumptions.
  The assumption ledger is part of the result.
* **Class Z** (22% of Unveil, unclassified) can be mapped only after it is
  classified.

## Class coverage when phases 1–8 are done

| class | share | covered by |
|---|---|---|
| B lifetime | 9% | ownership invariants, TX (done) and RX (2) |
| K wrong state / outage | 16% | lifecycle induction (1, 3), reset path (4) |
| F lost event / stuck | 9% | `INV_wakeup` (done), async handlers done: `WD_RESET_STICKY`, `WD_EVENT`, `AQ_PHASE`, `AQ_KEEPALIVE`. F18 is a class F finding from this phase |
| G OOB / corruption | 4% | bounds checks in every part, untrusted device (2, 4, 5) |
| H NULL deref | 7% | pointer checks in every part, error paths (3) |
| C stale after failure | 9% | error-path obligations (3). Done for start/stop/close: `DS_ERR_NO_HALF_START`, plus `LC_CLOSE_IDEMPOTENT` and `LC_DRV_STATS_ONCE` |
| D leak | 9% | ghost allocation counter (6) |
| A DMA into freed memory | 6% | ghost device-access set (7). First instance done: `FBX_DMA_ORDER` on the FreeBSD submit step, which has a real `bus_dma` layer to track |
| E deadlock | 3% | lock-order ghost. DPDK ENA takes `adapter->admin_lock` in 4 places in `ena_ethdev.c` and `ena_com` takes its own spinlocks 12 times, so this goes with 5 and 8 |
| I, J | 2% | conversion and bounds checks, uninitialised locals via goto-instrument's experimental `--uninitialized-check` |
| concurrency trigger | 21% | named pairs (8) |
