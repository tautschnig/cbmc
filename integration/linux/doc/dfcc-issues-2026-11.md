# Two DFCC issues, reduced and ready to pick up

**Date:** 2026-11-05
**Found by:** building an all-sequences proof of the DPDK ENA TX path
(`integration/linux/doc/cbmc-driver-proof-design-2026-11.md`). Both issues
blocked that work; both are reduced to self-contained C files that need
nothing from the driver.
**Status:** issue A has a mitigation implemented in this tree
(`--report-unreachable-properties`); issue B is unmitigated and is the harder
of the two.

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
   stub as `UNKNOWN`/`UNREACHABLE` rather than `SUCCESS`. This tree implements
   a version of (2) as the opt-in `--report-unreachable-properties`
   (`src/goto-checker/bmc_util.{h,cpp}`, two regression tests under
   `regression/cbmc/report-unreachable-properties*`). On the reproducer the
   downstream assertion becomes `UNREACHABLE`. Whether it should be the
   default is open: as a default it would change the reported status of
   properties in unreached loop iterations and dead branches across many
   existing tests.

### Not established

Whether making it the default breaks existing regression tests -- not
measured. The full suite has not been run against the mitigation; only
`regression/contracts-dfcc` (identical results with and without) and the two
new tests.

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
