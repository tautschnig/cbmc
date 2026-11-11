# Proving a Linux device driver with CBMC: a transferable method

**Date:** 2026-11-11
**Basis:** the DPDK/Linux/FreeBSD ENA work in `integration/linux/scan/amzn-drivers/`,
which produced two closed inductions, a ring-size-independent loop proof and
findings F1–F21.
**Audience:** someone who wants to do the same for a different driver.

This is the method, not a tutorial on CBMC. It says what to build, in what
order, and — more usefully — which things look like progress but are not.

---

## 1. What this method proves, and what it cannot

The deliverable is **an induction over operation sequences**:

* **base cases** — each way the driver reaches a usable state establishes an
  invariant (`setup`, `start`);
* **steps** — each operation, from any state satisfying the invariant,
  re-establishes it (`xmit`, `recv`, `cleanup`, `refill`);
* **closure** — teardown returns the state to a base case's post-condition,
  so arbitrary sequences are covered.

Everything is relative to an **assumption ledger** which is part of the
result, not a footnote. Ours ended up with three device assumptions, three
application obligations and a list of harness restrictions, each naming the
finding that would otherwise apply.

It does **not** prove: unbounded data sizes without extra work (§7),
exhaustive concurrency (§8), anything about the device itself beyond the
modelled assumptions, or absence of bugs in code you did not drive.

---

## 2. Pick the target and get it building first

Spend the first day on the build, not on proofs.

1. **Choose a driver with a shared core.** ENA has an `ena_com` layer vendored
   into three drivers, so one proof covers three products and a finding in the
   core is three findings. Look for that shape.
2. **Compile the driver into a goto binary with its real headers.** Capture
   the exact compiler flags from the real build (`ninja -t commands`, or
   `compile_commands.json`) into a `flags.txt` and reuse them verbatim. Stub
   headers invalidate everything downstream.
3. **Expect one upstream fix.** Ours needed a cherry-pick before the
   unmodified driver would build at all. Budget for it.
4. **Include the driver's `.c` file into the harness** rather than linking it.
   Most interesting functions are `static`. Where a translation unit must be
   linked instead (pre-built objects), remember that macros you redefine in
   the harness will **not** apply to it — see §6.

## 3. Write the invariant before the first harness

The invariant is the artefact. Harnesses are scaffolding.

Put it in **one shared header** and never duplicate it. We put `INV_STARTED`,
`INV_FREE_LIST`, `INV_RX_STARTED` and the rest in `ena_inv.h`, and the single
worst class of wasted effort in this project was two harnesses using
*slightly different* versions of the same predicate, so the base case and the
step did not compose and nothing detected it.

A ring-based driver's invariant usually needs four kinds of clause:

| clause | what it says | why you find out you need it |
|---|---|---|
| structural | handles non-NULL, geometry consistent, mode pinned | first proof attempt |
| ownership | each buffer is owned by exactly one slot | the double-free obligation fails |
| free-list | the free list names exactly the free slots | the step admits a state where it reuses an occupied slot |
| accounting | descriptors outstanding equal those of in-flight packets | the space check becomes reachable from an impossible state |

**Pin the mode.** Our `INV_STARTED` originally did not mention the queue
placement mode, so the base case proved a host-mode queue and the step
consumed a device-mode one. The composition check could not see it because
nothing in the predicate mentioned the mode.

**Beware modular arithmetic in accounting clauses.** Stating "outstanding ==
sum of per-packet counts" over `uint16_t` is satisfiable by wrap-around: a sum
of 60000 and an accumulator of 5540 total 4 modulo 2^16. That passes while
leaving the sum far above the queue depth. Add the no-wrap bound in wider
arithmetic. We only discovered this when the accounting had to survive an
intermediate state (§7).

## 4. The non-negotiable hygiene checks

A green run means nothing on its own. Every result must come with all five:

1. **Vacuity witnesses must FAIL.** For each outcome the proof claims to be
   about, assert its negation and require a counterexample. "A packet can be
   accepted", "a start can fail", "the walk can wrap the ring". **21 of our
   commits record a run where every obligation passed and the result was
   vacuous.** This is the single highest-yield check in the method.
2. **No reached bodyless callee.** A function with no body becomes an
   unreachable stub under DFCC and cuts the path, so later assertions pass
   vacuously. Count `no body for callee ... FAILURE` and require zero. Prefer
   *defining* a model to removing a body; where you must redirect, use
   `--replace-calls`.
3. **No UNKNOWN properties.** Count them; they are not successes.
4. **Unwinding assertions on, and shown complete.** If the loops have constant
   bounds, no unwinding assertion is generated at all — so also show that one
   unwinding *short* fails one. Otherwise "0 failures" is meaningless.
5. **The build must not be stale.** Our AENQ runner reported
   `VERIFICATION SUCCESSFUL` from a previous binary for two iterations,
   because its compile line ended in `| grep -i error` which masked the
   compiler's exit status. Every runner should delete its outputs first and
   refuse to proceed if the compile target is missing.

Add a sixth when the property needs it: **`--div-by-zero-check` is not implied
by `--bounds-check --pointer-check`.** We nearly claimed a modulo-by-zero was
impossible without having generated the property at all.

## 5. Finding bugs: where they actually were

Our 21 findings clustered in four places. In rough order of yield per unit
effort:

1. **Values the device chooses that index driver arrays.** BAR offsets (F8,
   F9), an RX `req_id` (F16), a DMA width stored from an error return (F13).
   Method: make every device-supplied value nondeterministic and let
   `--bounds-check --pointer-check` decide. Compare against sibling drivers —
   if one checks and another does not, that is a finding with a ready-made fix.
2. **Error paths and unexpected call orders.** A release after a failed setup
   (F10), an interrupt enabled before start (F17), a function that returns
   success after undoing its own work (F18), a flag never cleared (F21).
   Method: inject allocation and admin-command failure; call each entry point
   in the states the API permits but the driver does not expect. Error paths
   were 61% of the triggers in the reference finding set we reconciled against.
3. **Invariant clauses the driver relies on but never checks.** F15 (a
   zero-length packet consuming a slot forever) came from a stub claiming
   something the real code did not do.
4. **Ordering around resource release.** F20 (DMA rings freed when the device
   never acknowledged the destroy) came from a ghost "the device may still
   access this" set.

**Check the layer above before reporting.** Several candidates died because
the framework validated the argument: the RSS *key* paths are bounded by
`rte_ethdev.c`, which is why the missing bound in the *table* path (F19) is a
real finding and not part of a pattern. Read the caller.

## 6. The traps, with their symptoms

These cost us the most time. Each is cheap to avoid once named.

| trap | symptom | what to do |
|---|---|---|
| `__CPROVER_havoc_object` on a sub-object | wild pointers, enormous formulas, nonsense traces | it havocs the *whole enclosing object*; assign nondeterministic locals field by field |
| havocking a struct with function pointers | symex explores unrelated code | never do it; set the fields you need |
| macros redefined in the harness but the TU is linked, not included | your `ENA_MEMCPY_TO_DEVICE` override silently does not apply; log macros become undefined-function dereferences | compile that `.c` into the harness instead |
| anonymous unions in the driver's structs | `--pointer-check` reports **ERROR** on byte-extract bounds | copy the union member into a typed local first; or drive the function from a harness that does not include the union-bearing TU |
| the invariant evaluated at O(n²) | the *harness*, not the driver, dominates the cost | restate it; see §7 |
| `pkill -f <pattern>` | kills your own shell | kill by PID, excluding `$$` |
| a reachability probe you forgot to place inside `main` | witnesses report on a function that never runs | check the property name in the output matches the function you meant |

## 7. Getting past small sizes

Bounded unwinding ties you to a tiny ring. Three things worked, in increasing
order of effort:

1. **Restate expensive predicates.** Our free-list clause compared every pair
   of window positions, O(n²), evaluated twice per run (assumed, then
   asserted). A bitmask form — the ids the window names, the slots actually
   free, and a popcount — is O(n), and distinctness follows because a repeated
   id sets one bit twice. **Prove the two forms equivalent** before
   substituting; we did, at two sizes, with both witnesses failing.
2. **Hand-written loop invariant, machine-checked in three parts.** CBMC loop
   contracts could not express ours, because a loop invariant must be a
   side-effect-free expression and the predicate needs loops over the ring.
   So do the induction over iterations yourself:
   * ENTRY: the caller's invariant implies the mid-loop invariant;
   * STEP: one loop-body iteration preserves it;
   * EXIT: the mid-loop invariant plus the post-loop writes gives the
     post-condition.

   Each part is one iteration's work, so none grows with the size. This took
   our cleanup step from *no verdict at ring 8* to proven at ring 32. Be
   explicit that the splice itself and the transcription of the body are not
   machine-checked.
3. **A genuinely different mid-loop predicate.** The loop advances a *local*
   cursor and writes the shared one back only at the end, so the real
   invariant is false mid-loop. Parameterise it by the local state. Expect
   this, do not be surprised by it.

**If a loop contract does not terminate, look for an inner loop.** Havocking
the state makes a loop that was bounded in the concrete program unbounded in
the havocked one. Also bound DFCC's own write-set inclusion loop.

## 8. Concurrency, honestly

CBMC's thread support does not scale to a driver of this size. What works:

* **Name the pairs the threading model actually allows.** Per-queue data paths
  are usually single-threaded by contract; the real pairs are control thread
  vs interrupt/timer thread, and anything vs teardown.
* **Interleave by hand at the granularity of individual shared accesses.**
  Our first attempt executed a read-modify-write atomically in one step, and
  **both obligations held even in the variant that should have failed** — the
  harness could not express the window it was about. Split each participant
  into its individual loads and stores.
* **Model store reordering explicitly.** CBMC is sequentially consistent by
  default, which hides exactly the question you are asking about two plain
  stores with no barrier.
* **Expect the result to be a precondition, not a yes/no.** Both our pairs
  came out as "sound exactly under this assumption", which is more useful than
  either answer alone.

## 9. Report structure that survives scrutiny

For each finding, state: the code with line numbers; the exact mechanism; the
reaching call path including what the layer above does and does not validate;
the machine-checked evidence including which obligation failed and which
vacuity witnesses failed; the fix; and the severity *with its preconditions*.

Two things materially strengthen a report:

* **A sibling comparison.** "Your other two drivers already do this" is hard
  to argue with and supplies the patch. Three of ours have it.
* **Verifying the fix.** Add the proposed check behind a switch and show the
  obligation then passes. We did this for F19 and the F14 fix.

Keep **refuted hypotheses** in the record. We withdrew four claimed causes
after measurement; writing them down stopped them being re-proposed.

## 10. Order of work

1. Build the driver unmodified into a goto binary with real headers.
2. Write the invariant in a shared header.
3. One step operation end-to-end, with all five hygiene checks, at the
   smallest size that is non-vacuous.
4. The base case that establishes the invariant, and a composition check that
   every predicate a step assumes is proven by the base case.
5. The teardown step, closing the loop.
6. A runner that reports every part, the composition check, all witnesses and
   the assumption ledger, and prints one line saying whether the induction
   closed.
7. Then breadth: the other data path, the device lifecycle, the async
   handlers, config entry points.
8. Then the ghost-state classes: allocation ledger for leaks, device-access
   set for DMA-after-free.
9. Then concurrency pairs, and depth.

Breadth finds bugs at roughly one per harness for the first dozen harnesses.
Depth makes existing claims stronger and rarely finds new bugs. Choose
deliberately, and say which you are doing.

---

## Appendix: the artefacts worth copying

* `ena_inv.h` — one shared header for every invariant, with the reason each
  clause exists written next to it.
* `run_induction.sh` — one command, every part, composition check, witness
  check, assumption ledger, `INDUCTION: CLOSED` or not.
* `run_all.sh` + `proof-sweep-*.txt` — rebuild and re-run everything; mark the
  runners that fail *by design* because they reproduce a finding, so a real
  regression is visible.
* `metrics/` — the process instrumentation described in
  `proof-process-metrics-2026-11.md`.
* A findings document with one section per finding, and the refuted
  hypotheses kept alongside.
