# Two DFCC issues, reduced and ready to pick up

**Date:** 2026-11-05
**Found by:** building an all-sequences proof of the DPDK ENA TX path
(`integration/linux/doc/cbmc-driver-proof-design-2026-11.md`). Both issues
blocked that work; both are reduced to self-contained C files that need
nothing from the driver.
**Status:** both unmitigated. An attempted mitigation for issue A
(`--report-unreachable-properties`) was implemented, found to be wrong, and
**reverted** -- see "Attempted mitigation, withdrawn" under issue A. Issue B
is the harder of the two and still has no user-side workaround.

## Environment

| item | value |
|---|---|
| CBMC | `6.9.0 (cbmc-6.9.0-402-g646abfe000a-dirty)`, built from this tree |
| build | CMake `RelWithDebInfo`, `sat_impl=cadical;minisat2` (CaDiCaL default) |
| compiler | gcc 11.4.0 |
| OS / CPU | Ubuntu 22.04.5, x86_64, Intel Xeon Platinum 8259CL |

Neither reproducer needs the local changes in this tree; an upstream 6.9.0
build should show the same behaviour. Reproducers live in
`integration/linux/scan/amzn-drivers/proof/cbmc-limitations/`.

---

## Issue A: a bodyless callee cuts the path, so later properties report SUCCESS

**Files:** `dfcc_bodyless_callee_cuts_path.c`, `dfcc_bodyless_callee_cuts_path.sh`

### What happens

DFCC replaces a function without a body by `assert(false); assume(false);`
(`contracts/dynamic-frames/dfcc.cpp:491` and `dfcc_instrument.cpp:440`, both
selecting the `"assert-false-assume-false"` generator defined at
`goto-instrument/generate_function_bodies.cpp:108-129`). The `assert(false)`
raises a property; the `assume(false)` cuts the path, so **every property
after the call becomes vacuously SUCCESS**.

### Reproducer

```c
void undefined_fn(void);          /* declared, never defined */

int main(void)
{
  undefined_fn();
  __CPROVER_assert(0, "downstream: deliberately false, must FAIL");
  return 0;
}
```

```sh
goto-cc nobody.c -o nb.gb
cbmc nb.gb                                         # plain
goto-instrument --dfcc main nb.gb nb_dfcc.gb
cbmc nb_dfcc.gb                                    # with DFCC
```

### Expected vs actual

| | bodyless-call property | downstream `assert(0)` |
|---|---|---|
| plain CBMC | `no body for callee undefined_fn`: FAILURE | **FAILURE** (checked) |
| with DFCC | `undefined function should be unreachable`: FAILURE | **SUCCESS** |

A literal `assert(0)` is reported as SUCCESS.

### Why it matters

The diagnostic exists, so this is not a silent unsoundness. The hazard is the
ratio and the wording:

* **Ratio.** One failure beside arbitrarily many vacuous passes. In the ENA
  harness two bodyless callees (`ena_com_prepare_tx` and
  `__builtin_ia32_rdtsc`, the latter reached through
  `rte_get_timer_cycles()`) silently emptied every obligation about the
  packet-accept path while 1482 properties reported SUCCESS. We believed that
  result and reported it before noticing.
* **Wording.** "undefined function should be unreachable" reads as a claim
  about the program -- *this code should not be reachable* -- rather than
  about the model: *I have no body, so I cut the path and nothing after it
  was checked.*
* **Asymmetry.** The same goto binary with and without `--dfcc` differs
  enormously in coverage, and the only visible difference is which of two
  similarly-worded properties appears.

### Suggested directions

1. Word the message to say the path was cut.
2. Report properties that became unreachable *only* because of a generated
   stub as `UNKNOWN`/`UNREACHABLE` rather than `SUCCESS`. **The fix has to be
   in symex, not in the property-status post-pass** -- see below for why.

### Attempted mitigation, withdrawn

We implemented (2) as an opt-in `--report-unreachable-properties` in
`src/goto-checker/bmc_util.{h,cpp}`, and it is **reverted** (commit
`a2406e6c055` reverting `08ec62caff5`). Recording why, because the dead end is
informative:

The patch had two halves. The sound half inspected an assertion *step* in the
equation and reported `NOT_REACHABLE` when its guard is `false` (for every
step of that property). The unsound half mapped `NOT_CHECKED` -- a property
with **no** step in the equation -- to `NOT_REACHABLE`.

The unsound half is wrong because symex discharges an assertion whose
condition is statically true **without emitting a step at all**, so "absent
from the equation" does not imply "never reached". Minimal demonstration:

```c
int a = 1;
__CPROVER_assert(a == 1, "reachable but statically true");   /* -> UNREACHABLE */
```

That is a false `UNREACHABLE` on a plainly reachable assertion, which is worse
than the problem it was meant to fix, because it would discredit genuine
reports. On the ENA harness it labelled **1064 of 1303** properties
`UNREACHABLE`.

And removing the unsound half makes the patch useless for issue A: with
`assume(false)` the downstream assertion also never reaches the equation, so
the sound half does not see it either. The remaining signal is exactly the one
CBMC's own comment describes as unavailable: *"This could be a NOT_CHECKED,
NOT_REACHABLE or PASS, but the equation doesn't give us precise
information."*

So a real fix needs symex to distinguish "skipped because the path was cut"
from "statically discharged" -- for instance by recording, when a path is cut
by an `assume(false)` originating from a generated stub, which properties
were abandoned. That information exists at symex time and is lost by the
time the property statuses are computed.

### Not established

Whether a symex-side fix is practical; we did not attempt one.

---

## Issue B: with loop contracts, a callee's own locals are checked against the caller's assigns clause

**Files:** `dfcc_callee_locals_in_loop.c` (the minimal case),
`dfcc_callee_param_in_loop_assigns.c` (the variant first hit),
`dfcc_callee_locals_pass.c` (passing control),
`dfcc_callee_param_in_loop_assigns.sh` (runs all of them, including the
loop-contracts on/off control)

### What happens

Under `--dfcc --apply-loop-contracts`, assignments to variables that are
**local to a callee** are checked against the **caller's** loop assigns
clause. The caller cannot satisfy that check, because callee locals are not
nameable in the caller's contract.

### Reproducer, minimal

```c
static int g[4];

static void callee(int v)
{
  int a = 0;
  for(int k = 0; k < 3; k++)   /* a loop inside the callee, no contract */
    a = a + v;
  g[0] = a;
}

unsigned nd(void) { unsigned v; return v; }

int main(void)
{
  unsigned n = nd();
  for(unsigned i = 0; i < n; i++)
    __CPROVER_assigns(i, __CPROVER_object_whole(g))
    __CPROVER_loop_invariant(i <= n)
    __CPROVER_decreases(n - i)
  {
    callee((int)i);
  }
  return 0;
}
```

```sh
goto-cc loop_inside.c -o li.gb
goto-instrument --no-malloc-may-fail --dfcc main --apply-loop-contracts li.gb li_lc.gb
cbmc li_lc.gb --no-standard-checks --unwindset callee.0:5
```

Actual:

```
[callee.assigns.1] Check that a is assignable: FAILURE
[callee.assigns.2] Check that k is assignable: FAILURE
[callee.assigns.3] Check that g[0] is assignable: SUCCESS
VERIFICATION FAILED
```

`a` is a local of `callee`; `k` is declared inside `callee`'s own `for`. Only
`g[0]`, the one genuinely shared location, should need to be in the caller's
clause.

### The trigger, narrowed

Measured, one factor at a time:

| callee contains | variable | result |
|---|---|---|
| no loop | local assigned (`a = v;`) | no assigns property generated at all |
| no loop | local, address taken | SUCCESS |
| no loop | parameter assigned | no property generated |
| **loop**, variable assigned **inside** it | local `a`, loop counter `k` | **FAILURE** |
| **loop**, variable assigned **outside** it | local `a` | SUCCESS (only `k` fails) |

And the decisive control, same program, outer loop bounded with
`--unwindset main.0:3` instead of a contract:

| | `callee.assigns` properties |
|---|---|
| `--dfcc` only, no `--apply-loop-contracts` | `g[0]`: SUCCESS -> **VERIFICATION SUCCESSFUL** |
| `--dfcc --apply-loop-contracts` | `a`: FAILURE, `k`: FAILURE -> VERIFICATION FAILED |

So: **it requires a loop inside the callee, and it only happens when loop
contracts are applied.** It is not about parameters specifically (an earlier
guess of ours that the reproducer disproves) -- a plain local and even the
inner loop's own counter are flagged. Address-taken locals are not flagged,
which is consistent with them being registered as allocated objects.

### Why it matters

This blocks proving a real driver function under an outer loop contract.
`ena_tx_map_mbuf` walks an mbuf chain with
`while ((mbuf = mbuf->next) != NULL)` while updating its own locals, so with
the ENA TX loop under a contract we get

```
[ena_tx_map_mbuf.assigns.10] Check that mbuf is assignable: FAILURE
[ena_tx_map_mbuf.assigns.11] Check that seg_len is assignable: FAILURE
[ena_tx_map_mbuf.assigns.15] Check that ena_buf is assignable: FAILURE
[ena_tx_map_mbuf.assigns.17] Check that delta is assignable: FAILURE
```

and no caller-side clause can fix it. The practical effect is that
"real callee bodies" and "unbounded burst via a loop contract" cannot be
combined: each works alone.

### Suggested direction

Where DFCC instruments a function it already knows the function's locals (it
registers address-taken ones). The gap looks like loop-contract
instrumentation inside a *callee* treating that callee's frame as if it were
the caller's. Starting points:
`contracts/dynamic-frames/dfcc_instrument.cpp` (which locals are added to the
write set, and when) and `contracts/instrument_spec_assigns.cpp` (where the
checks are inserted).

### Not established

* Whether the inner loop having its *own* contract avoids it -- not tried,
  because the ENA case has no natural ranking function for a linked-list walk.
* Whether the same thing happens with `--enforce-contract` on the callee
  rather than a loop contract in the caller.
* The exact code path responsible: the characterisation above is from
  black-box measurement, not from reading the instrumentation.

---

## Suggested priority

**B first.** It blocks a proof outright and has no workaround from the user
side. A is annoying but has a mitigation in this tree and a clear fix
direction, and its danger is mostly addressed by always running an
independent reachability witness, which we now do.

---

## Independent review of the issue-B fix (branch `dfcc-skipped-loop-locals`)

Reviewed `7fc769d7ea2` by rebuilding the branch in its own worktree and
re-running everything from scratch rather than relying on the branch's own
notes.

**The explanation matches the black-box evidence.** `get_write_set` already
resolved through loops without a contract via
`get_first_id_not_skipped_or_top_level_id`; `get_local_set` and
`get_tracked_set` did not, so `must_check_lhs` saw a callee local as
non-local and checked it against a write set that could not contain it. That
accounts for every observation in the issue-B section above without needing
any of them as an extra assumption: it requires a loop in the callee (a
skipped loop exists), it requires `--apply-loop-contracts` (otherwise nothing
is skipped), it hits locals declared outside the loop *and* the loop's own
counter, and it leaves address-taken locals alone (they are in the tracked
set).

**Verified, with the fix:**

| check | result |
|---|---|
| `dfcc_callee_locals_in_loop` (minimal case) | spurious `a` / `k` checks **removed**, SUCCESSFUL |
| `dfcc_callee_param_in_loop_assigns` | spurious `m` / `acc` checks removed, SUCCESSFUL |
| `dfcc_callee_locals_pass` (control) | still SUCCESSFUL |
| **soundness check written for this review** | a write from the same uncontracted loop to a global the caller's clause omits is **still reported FAILURE**, while the callee's `a` and `k` are not checked |
| the branch's two new regression tests | both pass |
| `regression/contracts-dfcc`, fix vs develop | identical failure sets except `loop_contracts_callee_uncontracted_loop_locals`, which develop fails and the fix repairs. **Nothing newly broken.** |

Note the spurious properties are *removed* rather than merely passing, which
is the right shape: the callee's locals are recognised as local at the scope
the instruction resolves to.

**Effect on the ENA proof** (ring 8, segs 2, burst 1, real callee bodies,
unbounded burst via the loop contract):

* the `ena_tx_map_mbuf` locals failures (`mbuf`, `seg_len`, `ena_buf`,
  `delta`) are **gone**;
* the vacuity witness still fails, so the accept path is live;
* **`INV_wakeup` now verifies SUCCESS** where it was previously vacuous --
  the first obligation this unblocks.

**Still failing, and not attributable to this fix:** `INV`, `INV_SLOT`,
`OWN_UNIQUE`, `OWN_TRANSFER`, with `loop_invariant_step` UNKNOWN. Seven
`assigns` failures remain, all on writes through `tx_info` / `ena_buf`
(`tx_info->mbuf`, `->num_of_bufs`, `->tx_descs`, `->timestamp`,
`ena_buf->paddr`, `->len`) which `__CPROVER_object_whole(tx_ring->tx_buffer_info)`
ought to cover.

The branch's notes suggest the anonymous union in `struct ena_ring` may be
responsible. **Tested and not supported:** naming the harness's file-scope
`tx_buffer_info` array directly in the clause, bypassing the union read
entirely (`tx_loop_contract_fields_direct.json`), gives a byte-identical
outcome -- 25 of 1646, same seven assigns failures. So that hypothesis should
not be carried forward without further evidence; the cause of the remaining
seven is open.

**Verdict:** the fix is correct, well-scoped, sound on the controls I could
devise, and regression-clean. It resolves issue B as reported. It is not
sufficient to close out the ENA obligations, and the next obstacle on that
path is still unidentified.

---

## Issue C: a loop contract is reported as proved on a cut path

Reproducer: `proof/cbmc-limitations/dfcc_loop_contract_vacuous_on_cut_path.{c,sh}`.

A loop whose body calls a bodyless callee reports **every** loop-contract
property SUCCESS, even though the callee's `assume(false)` cuts every path
through the body, so the invariant's step case and the assigns check were
never exercised on a real execution:

```
[helper.assertion.1] undefined function should be unreachable: FAILURE
[main.loop_assigns.1]       Check assigns clause inclusion for loop main.0: SUCCESS
[main.loop_assigns.2]       Check assigns clause inclusion for loop main.0: SUCCESS
[main.loop_decreases.1]     Check variant decreases after step for loop main.0: SUCCESS
[main.loop_invariant_base.1] Check invariant before entry for loop main.0: SUCCESS
[main.loop_invariant_base.2] Check invariant before entry for loop main.0: SUCCESS
[main.loop_invariant_step.1] Check invariant after step for loop main.0: SUCCESS
VERIFICATION FAILED
```

This is issue A specialised to loop contracts, and it is worse in effect. For
a plain assertion, a cut path yields a SUCCESS that a careful reader may
discount. For a loop contract, the SUCCESS is the entire evidence that the
abstraction replacing the loop is sound, and every obligation downstream of
the loop rests on it. The only signal that anything is wrong is a failure
attributed to the *callee*, in a different function, which is easy to
dismiss as a known-abstraction note -- we did exactly that for weeks, and it
is how the LLQ-push sub-path of the ENA proof went unverified while reporting
six obligations SUCCESS.

**Suggested fix.** The same symex-side information issue A needs: when a path
is cut by an `assume(false)` originating from a generated stub, record which
properties were abandoned. A loop-contract property that was abandoned on
every path should not be reported SUCCESS. Short of that, a warning naming the
loop and the stub would be enough to stop the misreading.

**Practical mitigation, which is what we now rely on:** treat any
`undefined function should be unreachable` FAILURE as a hard stop rather than
a note, and require an independent reachability witness plus `--cover location`
on the functions the proof claims to exercise.

### Related, observed but NOT reproduced

At ENA scale we also saw 1280 of 2282 properties reported **UNKNOWN**,
including every `loop_invariant_step`, in runs that had four unrelated
failures; checked individually with `--property`, those same step properties
came back SUCCESS. The mechanism is presumably that equation generation stops
once a failure is found, so properties not yet decided keep the initial
UNKNOWN status instead of being swept by
`update_status_of_unknown_properties`.

We could **not** reduce this to a small reproducer: in every small case tried
(failure inside a contracted loop body, a second uncontracted loop under
`--incremental-loop`, `--stop-on-fail`, a removed callee in the body) CBMC
decided all properties and nothing was left UNKNOWN. A patch warning on
"loop-contract property UNKNOWN while other properties failed" was written
and then **withdrawn**, because a warning whose triggering condition cannot
be demonstrated should not be proposed upstream. Recording the observation
here so it is not lost; it needs a reproducer before it is reportable.

---

## Review of the fixes (branch `cut-path-reporting`)

Three commits, reviewed by building the branch in its own worktree and
re-running our reproducers and the ENA proof against it.

`b165698b344` follow returns to callers and continue past `assume(false)`;
`0ae5f19be61` make the assert-false-assume-false body assertion fatal;
`76dd7385f20` add `--report-unreachable-properties`, decided during symex.

**The soundness trap is avoided.** Our own withdrawn attempt
(`08ec62caff5`) decided this from the equation and treated "not in the
equation" as "unreachable", which mislabels assertions symex discharges
statically. The three-case probe that exposed that bug now behaves correctly
with the option on:

| case | status |
|---|---|
| reachable, statically true (`int a = 1; assert(a == 1)`) | SUCCESS |
| reachable, trivially true but symbolic (`b == b`) | SUCCESS |
| assertion of `false` guarded by a statically false condition | SUCCESS, excluded by design |

Deciding it in symex is the right place, and is what we argued for when
withdrawing our patch: symex knows, at the moment it cuts a path, which
properties it is abandoning.

**Issue A is fixed.** The reproducer's downstream `assert(0)` after a bodyless
callee now **FAILS** where it previously reported SUCCESS. Under DFCC it is
reported UNKNOWN rather than SUCCESS -- not as good as a failure, but no
longer a false proof.

**Issue C is fixed.** With the option, the loop-contract properties of a loop
whose body is cut by a bodyless callee are reported **UNREACHABLE** instead of
SUCCESS -- exactly the behaviour we asked for. Even with the option off the
verdicts become UNKNOWN rather than SUCCESS, because the body assertion is now
fatal. The branch also adds a `Check step was unwound for loop ...` property,
which directly detects the "the step case never ran" vacuity that issue C made
invisible.

**Regression-clean.** `contracts-dfcc` gives an identical failing set with the
branch and with develop (138 each, no newly failing and no newly passing).
`regression/cbmc` likewise, and the branch's three new
`report-unreachable-properties` tests pass.

### What it says about our own proof

The TX step was re-run against the fixed binary with
`--report-unreachable-properties`, which is the strictest setting available:
**VERIFICATION SUCCESSFUL, 0 of 2388, all six obligations SUCCESS, vacuity
witness still FAILURE, and every loop-contract property SUCCESS including the
new `Check step was unwound`.** A tool that now detects vacuous contract
verdicts finds none in it. That is the strongest independent check the proof
has had.

268 properties are reported UNREACHABLE, with a warning giving the count, and
the breakdown is a useful audit of scope rather than a problem:
`ena_tx_mbuf_prepare` 190 (a separate dev_ops entry point, not called by
xmit), **`ena_tx_cleanup` 50** (a separate operation -- precisely why the
cleanup harness is being written), `ena_tx_map_mbuf` 14 (segment and error
paths beyond segs 2), `ena_com_comp_ack` 7 (reached only via cleanup). The one
UNREACHABLE unwinding assertion is inside CBMC's own DFCC write-set library,
not in the driver; `ena_tx_map_mbuf.unwind.0` is SUCCESS, so our inner-loop
bound is still real.

Those 268 were previously counted as SUCCESS, which is the concrete
justification for reporting obligations rather than property totals.
