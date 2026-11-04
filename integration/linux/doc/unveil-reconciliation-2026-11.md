# Reconciling our amzn-drivers work with the 179 Unveil findings

**Date:** 2026-11-04
**Their run:** Velocity Labs "Unveil", Soonho Kong, 27 Oct 2026, at the same
commit we analysed (`9d5ca40`). 179 confirmed findings: HIGH 51, MEDIUM 100,
LOW 28. By component: Linux ENA 52, Linux EFA 51, FreeBSD ENA 32, shared
`ena_com` 30, DPDK ENA PMD 14.
**Their method, in their words:** they "modeled the drivers' **concurrent
state machines** in Lean 4 with Veil, model-checked them, and wrote a test
against the real driver source for each counterexample". No maintainer has
reviewed the findings yet.

**The question:** are we missing a lot, doing something wrong, or have we not
looked at the affected drivers? Answer: **mostly the third, with a real
scope problem in what we chose to prove, and no evidence that the method is
wrong.** The numbers below say where the findings actually are.

## 1. What the 179 findings are about

Classified by keyword over each finding's Impact column
(`scan/amzn-drivers/triage/classify_unveil.py`; the component split it
recovers -- 52/30/51/32/16 -- matches their own table, so the parse is sound,
but the class assignment is keyword-based and worth +/- a few per row).

| | class | share |
|---|---|---|
| K | unrecoverable/wrong state, interface outage | 29 (16%) |
| C | stale or still-live resource after a failed path | 18 (9%) |
| B | lifetime: UAF, double-free, freed-page mapping | 17 (9%) |
| D | leak (memory, resource, IRQ, ID) | 17 (9%) |
| F | lost event, stuck queue, no progress, timeout | 17 (9%) |
| H | NULL / invalid dereference / oops | 13 (7%) |
| A | lifetime: **device DMA or CQ writes into freed memory** | 11 (6%) |
| G | memory corruption / out-of-bounds | 9 (4%) |
| E | deadlock / hang / lockup | 7 (3%) |
| I, J | integer wrap; uninitialised / info exposure | 3 (2%) |
| Z | other | 40 (22%) |

And by **trigger**, which is the more useful cut:

* **61% are error-path-triggered** -- something fails first (an allocation, a
  device command, a reset, a probe), and the driver is then left in a broken
  state. Their #1 is the shape: `ena_xdp_set` assigns the new XDP program,
  calls `ena_up`, and on failure does `return rc` without undoing the
  assignment (`ena_xdp.c:406-408`).
* **21% are concurrency-triggered** -- an explicit race or interleaving.
* 24% are neither: plain sequential paths with no failure.

So the distribution is dominated by **lifetime and state-after-failure**, not
by the bounds violations our pipeline was built to find. Classes G+I+J, the
heart of our taint work, are **12 of 181 findings (7%)**.

## 2. Are we doing something wrong?

No evidence of that, and two pieces of evidence against it.

* Our seven device-trust findings (F1-F7) are in a class their list barely
  touches: a device-supplied *index or length* used unchecked as a bound
  (EFA doorbell BAR index, ENA RSS `key_parts`, RX frag count, kverbs
  `req_id`/`sub_cq_idx`, RX completion length). Their nearest neighbour,
  "kernel memory write 16 GiB out of bounds when a kernel QP's `qp_num` is
  reused", is plausibly the same root cause as our F4 reached from a
  different direction. Different axes, small overlap -- which is what you
  would expect from a device-adversary model versus a concurrent-lifecycle
  model.
* The proof results stand on their own terms: stage 3 verifies 8595
  properties with an empty environment-assumption list, and the stage-2
  control shows the loop contract is load-bearing. Nothing there is
  contradicted by their findings.

## 3. Are we missing a lot? Yes -- and the scope is the reason

Two scope facts, both uncomfortable and both quantifiable.

**Our proof touches two entry points.** Across every harness we have written,
the driver functions ever called are `ena_tx_queue_setup` and
`eth_ena_xmit_pkts`. That is the happy path of one operation in one lifecycle
state, single-threaded. Their findings concentrate in `ena_up`/`ena_down`,
reset and restore, PCI remove, suspend, devlink reload, XDP and AF_XDP
attach/detach, PTP, netmap, and the admin-queue bring-up -- none of which we
have modelled.

**We never built anything for FreeBSD ENA at all:** 32 findings (18%) in a
component where we have no CodeQL database and no proof, only a source
reading. For DPDK we proved the TX path only; for Linux ENA and EFA our
databases covered the files but our finders model none of classes A-F.

So "haven't looked at the affected drivers" is literally true for 18% of the
findings and effectively true for most of the rest.

## 4. Why their method finds these and ours does not yet

Three structural reasons, not accidents:

1. **Failure outcomes.** Their state machine makes every step's failure a
   nondeterministic transition, so the model explores "allocation failed",
   "device rejected the command", "completion never arrived" by construction.
   Our harnesses mostly assume success. The exception proves the point: in
   stage 3 I injected allocation failure (`alloc_tbi_fail` and friends), and
   it cost three lines and immediately covered the error paths of
   `ena_tx_queue_setup`.
2. **Lifetime is a property of the whole sequence**, not of one operation.
   "Device DMA into freed memory" needs an ownership map tracked across
   create/destroy/reset, exactly class 2-3 of our own specification catalogue
   -- designed in the design doc, not yet implemented.
3. **Concurrency and liveness.** We do no concurrency at all, and "lost
   completion", "stuck queue" and "hung `ibv_get_cq_event`" are liveness
   properties. A bounded model checker does not do liveness directly; this is
   the one place where their approach is a better fit, not merely
   better-aimed.

## 5. What it takes to close the gap, in yield order

Estimated coverage of their 181 findings if each extension were in place.
These are upper bounds on *expressibility* -- being able to state the
property, not a promise to prove it:

| extension | classes | would address |
|---|---|---|
| **Failure injection in every stub and callee** (cheap, demonstrated) | drives 61% of all findings | prerequisite for the rest |
| **Ownership ghost map** (alloc/free/post/complete + DMA ownership) | A, B, D | 45 (25%) |
| **API automaton extended to teardown/reset/remove** | C, K | 47 (26%) |
| **Bounded concurrency** for the pairs that actually overlap (xmit vs `ena_down`, IRQ vs lock holder, devlink vs reset) | E + the 21% race-triggered | ~39 (21%) |
| Already in scope today | G, I, J, parts of H | ~25 (14%) |
| **`INV_wakeup` handshake invariant + ghost lock-order invariant** | most of E, F | 24 (13%) |
| **Ranking functions** for genuine unbounded-delay progress | residual of E, F | small |

Taken together these bring the expressible share from roughly 14% to about
93%.

**Correction to an earlier claim.** I previously wrote that classes E and F
are liveness and therefore outside CBMC. That was wrong twice over. A ranking
function reduces liveness to a per-step safety obligation exactly as induction
reduces unbounded sequences -- and we are already doing it, since the
`decreases` clauses in `tx_loop_contract.json` and `setup_loop_contract.json`
are ranking functions that CBMC discharges as `loop_decreases` properties.
More importantly, most of class F is not liveness at all: a lost wakeup is a
*reachable state* in which work is pending and nobody will look at it, i.e. a
violation of `work_pending ==> (interrupt armed \/ poll scheduled)`, and a
deadlock is a violation of a ghost lock-order invariant. Both are safety.
Section 8 of `cbmc-driver-proof-design-2026-11.md` has the detail and the
references (Biere/Artho/Schuppan's liveness-to-safety reduction;
Cook/Gotsman/Podelski/Rybalchenko/Vardi for liveness under fairness;
Podelski/Rybalchenko transition invariants for the synthesis side).

The honest residual is no longer a class of properties but a single
*assumption*: conditional liveness needs a fairness assumption on the device
("a posted descriptor is eventually completed"), without which the property
is simply false. That assumption should sit at the top of any harness that
relies on it.

## 6. Revised plan

The staged order in `cbmc-driver-proof-design-2026-11.md` is still right but
was mis-prioritised: it put RX and cleanup ahead of failure injection,
ownership and teardown, which is backwards relative to where the bugs are.
Revised:

1. **Failure injection everywhere.** Make every environment stub and every
   abstracted callee fail nondeterministically, and re-run stages 1-3. This
   is the cheapest change with the largest reach, and it will likely break
   the current proofs in informative ways (the error paths are exactly what
   is unverified).
2. **Ownership ghost map** -> classes A, B, D. Buys the use-after-free class
   we cannot find at all today, including the device-DMA-into-freed-memory
   variant that is specific to drivers.
3. **Extend the API automaton to the full lifecycle** -- `ena_up`/`ena_down`,
   reset/restore, remove, suspend -- with `INV` required to hold in every
   state and across every transition, including the failing ones. Classes C
   and K fall out of this plus (1).
4. **Then** RX and `tx_cleanup` (the original stage 4), which are now lower
   priority than they looked.
5. **Bounded concurrency** for the specific overlapping pairs.
6. **FreeBSD ENA**: at minimum build the CodeQL database and run the existing
   finders; 32 findings sit in a component we have never analysed.
7. Add the `INV_wakeup` handshake invariant and the ghost lock-order
   invariant (cheap, a few ghost variables, between them cover classes E and
   F), and use ranking functions for the genuine progress properties, reusing
   the `decreases` mechanism already in the loop contracts. State the
   device-fairness assumption explicitly wherever conditional liveness is
   claimed.

## 7. What this means for the confidence goal

A full *safety* proof of a driver -- memory safety, ownership and lifetime,
ring and ABI invariants, under arbitrary device behaviour, arbitrary operation
sequences and injected failures -- looks achievable with the schema we have,
and stages 1-3 are evidence that the mechanics work on real code. A full
*correctness* proof covering essentially all of their classes now also looks
reachable, because the liveness-shaped findings reduce to safety: handshake
and lock-order invariants for lost wakeups and deadlocks, ranking functions
for genuine progress (section 8 of the design doc). The honest target is
**safety and functional-state correctness by CBMC proof, with conditional
liveness under an explicit device-fairness assumption** -- the boundary being
that assumption, written down, rather than a class of properties we cannot
state.

One more caveat worth keeping in view: these 179 findings are unreviewed by
maintainers, and each is a counterexample in *their model* plus a userspace
test that stubs the kernel and plays the device. Both of our teams are
reasoning about models; the two reports I read in detail held up against the
source, which is a good sign, but a sample of two is not a basis for assuming
all 179 do.
