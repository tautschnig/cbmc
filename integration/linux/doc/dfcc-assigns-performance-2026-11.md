# Why the widened ENA TX loop contract did not terminate: measurement and root cause

**Date:** 2026-11-04 (revised; supersedes the version of the same date in
commit `fef2085d469`)
**Context:** the amzn-drivers ENA proof, `cbmc-driver-proof-design-2026-11.md`
sections 8.5 and 8.5a.
**Status:** root cause found and confirmed by fixing it. The non-termination was
**not** caused by the assigns clause. It was an unbounded inner loop in a
callee. Bounding that loop alone made the proof finish in about 10 s.

## 0. Correction to the first version of this document

The first version (commit `fef2085d469`) concluded that DFCC's write-set check
had a quadratic cost in the number of objects a symbolic write pointer may
designate, attributed it to `set->allocated.elems[__CPROVER_POINTER_OBJECT(ptr)]`
in `cprover_contracts.c`, said the ENA harness hit that term through three
multipliers, and proposed reporting the encoding upstream. Further measurement
disproved all four claims:

| first-version claim | what the measurements show |
|---|---|
| the superlinear cost is DFCC's write-set check | the same growth appears with **no DFCC, no contract and no loop**: one dereference of a K-target pointer (section 4) |
| the encoding `allocated.elems[POINTER_OBJECT(ptr)]` is the quadratic term | DFCC's share of symex time *falls* as K grows (8.5x at K=2, 1.3x at K=128) |
| the ENA harness has symbolic write pointers with large points-to sets | the largest points-to set anywhere in the ENA TX path is **2** objects (section 5) |
| the widened clause is too expensive, especially `object_whole(adapter)` | the same clause, 178 KB adapter target included, verifies in **10 s** once an unrelated inner loop is bounded (section 6) |

The first version was wrong because it inferred a mechanism from a scaling
curve without a baseline. It never ran the same program without DFCC, and it
never measured the ENA points-to sets. It reasoned from static object counts
instead. Sections 4 and 5 are the two missing controls. The upstream-report
suggestion is withdrawn: it would have sent the CBMC maintainers to the wrong
code.

## 1. Summary

* **The ENA proof's non-termination was an unbounded inner loop.** The widened
  contract on the outer TX loop of `eth_ena_xmit_pkts` havocs the ring, so the
  `mbuf->next` chain walked by the callee `ena_tx_map_mbuf`
  (`while ((mbuf = mbuf->next) != NULL)`, `ena_ethdev.c:3108`) is unconstrained
  inside the havocked loop body. That inner loop has no contract, and the run
  gave no `--unwind`, so symex unwound it without bound: 2269 iterations of
  `ena_tx_map_mbuf.0` against at most 4 of any other loop, until the run was
  killed. With `--unwindset ena_tx_map_mbuf.0:4 --unwinding-assertions` the
  proof finishes in 10 s and **the unwinding assertion passes**, so the bound is
  complete for this harness.
* **The superlinear scaling the first benchmark found is real but is not a DFCC
  cost.** It is CBMC symex dereferencing a pointer that may designate any of K
  objects. One dereference costs about 7x per doubling of K, with or without
  contracts.
* **What DFCC does add, measurably, is formula size.** At K=128 an
  `__CPROVER_object_whole(p)` target produces 2.74 M clauses and the precise
  target `p[0].buf[0]` produces 0.15 M, about 18x. In the ENA case this did not
  matter (section 6).

## 2. Environment

| item | value |
|---|---|
| CBMC | `6.9.0 (cbmc-6.9.0-360-gbaebc53ee6-dirty)`, built from this repo at `cbmc-6.9.0-395-g23684c811f1` |
| build type | CMake `RelWithDebInfo` |
| SAT backend | built with `sat_impl=cadical;minisat2`; CaDiCaL is the default |
| compiler | gcc 11.4.0 (Ubuntu 11.4.0-1ubuntu1~22.04.3) |
| CMake | 3.22.1 |
| OS | Ubuntu 22.04.5 LTS, x86_64 |
| CPU | Intel Xeon Platinum 8259CL @ 2.50GHz, 96 threads |
| RAM | 755 GiB |
| profiler | perf 6.8.12, `--call-graph dwarf` |
| DPDK (section 5-6 only) | v24.11 |
| amzn-drivers (section 5-6 only) | `9d5ca40` |

Every timing is wall-clock from a single run on an idle machine, with
`ulimit -v 32000000` (≈32 GiB) and a `timeout`. Repeated runs agreed to within
about 5% (compare the two runs of section 4). Read the *shape* of each curve,
not the absolute seconds.

Sections 3-4 need nothing but a CBMC build. Sections 5-6 also need upstream
PR #8747 (constructor/destructor priority attribute), because DPDK's
`RTE_PMD_REGISTER_PCI` uses `__attribute__((constructor(prio)))`.

### Building CBMC

```sh
git clone https://github.com/diffblue/cbmc.git && cd cbmc
git submodule update --init
cmake -S . -Bbuild -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel $(nproc)
```

For sections 5-6 only:

```sh
git fetch https://github.com/diffblue/cbmc.git pull/8747/head:pr8747
git cherry-pick 317d889dadf
cmake -S . -Bbuild        # REQUIRED: the PR adds a source file an existing
                          # build directory will not glob; skipping this gives
                          # an undefined-symbol link error
cmake --build build --parallel $(nproc)
```

## 3. Single-dimension scaling of DFCC loop contracts

Scripts are in `integration/linux/scan/amzn-drivers/proof/cbmc-limitations/`.
Each takes `BIN=/path/to/cbmc/build/bin`.

### 3.1 Four single-dimension families — `dfcc_assigns_scaling.sh`

Each program has one loop with an unbounded trip count (`n` nondet), an
invariant `i <= n`, a variant `n - i`, and an `__CPROVER_assigns` clause. The
pipeline is

```sh
goto-cc prog.c -o b.gb
goto-instrument --no-malloc-may-fail --dfcc main --apply-loop-contracts b.gb b_lc.gb
cbmc b_lc.gb --no-standard-checks
```

and each family varies one dimension:

```
== (1) unrelated objects, concrete write pointer ==
objects=4         0.1s  VERIFICATION SUCCESSFUL
objects=16        0.1s  VERIFICATION SUCCESSFUL
objects=64        0.1s  VERIFICATION SUCCESSFUL
objects=256       0.1s  VERIFICATION SUCCESSFUL
== (2) assigns targets, concrete write pointer ==
targets=1         0.1s  VERIFICATION SUCCESSFUL
targets=4         0.1s  VERIFICATION SUCCESSFUL
targets=16        0.2s  VERIFICATION SUCCESSFUL
targets=32        0.5s  VERIFICATION SUCCESSFUL
== (3) object_whole target size ==
bytes=128         0.1s  VERIFICATION SUCCESSFUL
bytes=8192        0.3s  VERIFICATION SUCCESSFUL
bytes=32768       1.4s  VERIFICATION SUCCESSFUL
bytes=131072      6.1s  VERIFICATION SUCCESSFUL
== (4) objects a SYMBOLIC write pointer may designate ==
K=2             0.1s  VERIFICATION SUCCESSFUL
K=8             0.3s  VERIFICATION SUCCESSFUL
K=32            1.3s  VERIFICATION SUCCESSFUL
K=128          16.5s  VERIFICATION SUCCESSFUL
```

Family 4 is the only steep one. In family 4 a pointer `p` is set from one of K
distinct globals under a nondet `switch`, the clause is
`__CPROVER_object_whole(p)`, and the body writes `p[0].buf[0]`.

### 3.2 Family 4 by phase — `dfcc_assigns_phases.sh`

Same program as family 4, with CBMC's own phase timers (`--verbosity 9`) and
formula size. `VARIANT=whole` (default), `precise` (target `p[0].buf[0]`), or
`nodfcc` (same program, no contract, `--unwind 2`).

```sh
KS="2 4 8 16 32 64 128 256" VARIANT=whole   ./dfcc_assigns_phases.sh
KS="2 8 32 64 128 256"      VARIANT=nodfcc  ./dfcc_assigns_phases.sh
KS="2 8 32 64 128"          VARIANT=precise ./dfcc_assigns_phases.sh
```

`VARIANT=whole`:

```
K         total    symex  convert   solver      steps       vars     clauses  result
2           0.1 0.0357957 0.0188894 0.0470749        985      26812       48533  VERIFICATION SUCCESSFUL
4           0.2 0.0456105 0.0315272 0.0674109       1395      41308       91820  VERIFICATION SUCCESSFUL
8           0.3 0.0702549 0.0577542 0.104874       2215      70301      175499  VERIFICATION SUCCESSFUL
16          0.5 0.145228 0.109023  0.20423       3855     128294      343450  VERIFICATION SUCCESSFUL
32          1.3 0.438139 0.218859 0.533406       7135     244294      680203  VERIFICATION SUCCESSFUL
64          3.8  1.96221 0.446948  1.28483      13695     476326     1359189  VERIFICATION SUCCESSFUL
128        16.9  11.9945 0.986079  3.61693      26815     940452     2738892  VERIFICATION SUCCESSFUL
256        87.5  87.1425                        53055                         TIMEOUT
```

The K=256 row is **not a timeout**. CBMC aborted after about 90 s, during SSA
conversion, with

```
too many addressed objects: maximum number of objects is set to 2^n=256 (with n=8);
use the `--object-bits n` option to increase the maximum number
```

DFCC's instrumentation adds objects of its own, so K=256 user objects exceed
the default 8 object bits. The script prints `TIMEOUT` for any run that lacks a
`VERIFICATION` line. Read that column with this in mind.

`VARIANT=nodfcc` (no convert/solver columns because every property is decided
during symex and the solver is never invoked):

```
K         total    symex  convert   solver      steps       vars     clauses  result
2           0.0 0.00419307                          238                         VERIFICATION SUCCESSFUL
8           0.0 0.0161516                          790                         VERIFICATION SUCCESSFUL
32          0.2 0.214636                         2998                         VERIFICATION SUCCESSFUL
64          1.3  1.28607                         5942                         VERIFICATION SUCCESSFUL
128         9.4  9.31424                        11830                         VERIFICATION SUCCESSFUL
256        75.0  74.8766                        23606                         VERIFICATION SUCCESSFUL
```

`VARIANT=precise`:

```
K         total    symex  convert   solver      steps       vars     clauses  result
2           0.1 0.0312422 0.00637897 0.032666        769      14520        7326  VERIFICATION SUCCESSFUL
8           0.1 0.0473564 0.0100531 0.0399795       1339      21529       12626  VERIFICATION SUCCESSFUL
32          0.5 0.248021 0.0244373 0.174081       3619      49602       31890  VERIFICATION SUCCESSFUL
64          2.0  1.38837 0.0498919 0.525636       6659      87074       63708  VERIFICATION SUCCESSFUL
128        11.1  9.14028 0.100633  1.76612      12739     162080      149075  VERIFICATION SUCCESSFUL
```

Reading the three tables together:

| K | symex, no DFCC | symex, `object_whole` | DFCC symex overhead | clauses, `object_whole` | clauses, precise |
|---|---|---|---|---|---|
| 2 | 0.004 s | 0.036 s | 8.5x | 48,533 | 7,326 |
| 32 | 0.21 s | 0.44 s | 2.0x | 680,203 | 31,890 |
| 64 | 1.29 s | 1.96 s | 1.5x | 1,359,189 | 63,708 |
| 128 | 9.31 s | 11.99 s | 1.3x | 2,738,892 | 149,075 |

* **Symex dominates and grows about 7x per doubling of K** (64 to 128: 7.2x
  without DFCC, 6.1x with it). The symex step count grows only 2x per doubling.
* **DFCC's overhead on symex shrinks as K grows.** It is a constant-ish
  addition on top of a cost that exists without it, not the source of the
  growth.
* **DFCC's real, attributable cost is clause count.** It is linear in K, and
  `object_whole` gives 7-18x the clauses of a precise target.

## 4. The control: one dereference, no DFCC — `deref_k_targets.sh`

The no-DFCC baseline already shows the growth, so the next step is to strip
everything else away. Three programs, no loop, no contract, `--object-bits 12`
so that K=256 fits:

* **A** — a K-way `switch` assigns `p`, and `p` is never dereferenced;
* **B** — the same `switch`, followed by one write `p[0].buf[0] = 1`;
* **C** — `p` loaded from a K-entry pointer table (no `switch`), followed by
  the same write.

```sh
KS="32 64 128 256" ./deref_k_targets.sh
```

```
case                                symex    steps  result
K=32   A switch, no deref         0.04187     2817  VERIFICATION SUCCESSFUL
K=64   A switch, no deref         0.08501     5601  VERIFICATION SUCCESSFUL
K=128  A switch, no deref         0.17426    11169  VERIFICATION SUCCESSFUL
K=256  A switch, no deref         0.36960    22305  VERIFICATION SUCCESSFUL
K=32   B switch + deref           0.12672     2881  VERIFICATION SUCCESSFUL
K=64   B switch + deref           0.68654     5729  VERIFICATION SUCCESSFUL
K=128  B switch + deref           4.81298    11425  VERIFICATION SUCCESSFUL
K=256  B switch + deref            37.855    22817  VERIFICATION SUCCESSFUL
K=32   C table + deref            0.12584     2750  VERIFICATION SUCCESSFUL
K=64   C table + deref            0.70236     5470  VERIFICATION SUCCESSFUL
K=128  C table + deref            4.78515    10655  VERIFICATION SUCCESSFUL
K=256  C table + deref            37.8102    21279  VERIFICATION SUCCESSFUL
```

* A is linear (2.1x per doubling), so building the pointer is cheap.
* B and C agree to within 2%, so the cost does not depend on how the pointer
  was produced.
* One dereference of a K-target pointer costs 5.4-7.9x per doubling of K. That
  is the whole superlinear term of section 3.

**Where the time goes.** `perf record --call-graph dwarf` on program C at K=64
shows about 65% of the run under

```
goto_symext::symex_assign
 symex_assignt::assign_rec / assign_non_struct_symbol
  symex_assignt::assign_if
   simplify_exprt::simplify_if_preorder
    simplify_expr_with_value_sett::simplify_inequality
```

with the leaf time in `irept` reference counting (`sharing_treet::remove_ref`,
`detach`), `irept::find`, and `malloc`/`free`. Symex builds a K-way
`if`-chain for the dereference, and the simplifier rewrites it.

Two checks rule out the obvious readings:

* **The simplifier is not wasted work.** `--no-simplify` makes program C far
  slower: 42 s at K=32 instead of 0.13 s, 194 s at K=64, and over 600 s at
  K=128. The simplifier is what keeps the expression small.
* **The output is not large.** The SSA program `--program-only` prints grows
  linearly: 45 KB, 93 KB and 199 KB for K = 8, 16, 32, with the longest line
  217, 358 and 710 characters. The superlinear time goes into building and
  simplifying *intermediate* expressions that end up linear in size.

This is a property of CBMC's ordinary symbolic execution of pointer
dereferences. It does not involve DFCC.

## 5. The ENA harness is not in the K-target regime

The first version claimed the ENA harness hit section 4's term because it
includes the whole DPDK translation unit (83 static-lifetime objects) and
writes through symbolic pointers. Both facts are true, but they are the wrong
measurement. What matters is how many objects each *write pointer* may
designate, and that can be read off directly:

```sh
goto-instrument --show-value-sets w2_lib.gb > vs.txt     # ~33 MB
```

Parsing every value-set line inside `eth_ena_xmit_pkts`, `ena_xmit_mbuf` and
`ena_tx_map_mbuf`, the **largest points-to set is 2 objects**. The data
pointers in the TX path are single-target, for example

```
main::1::ring.$anon1.tx_buffer_info = { <main::1::tx_buffer_info[0], 0, struct tag-ena_tx_buffer> }
main::1::tx_pkts$object             = { <segs[0], 0, struct tag-rte_mbuf> }
```

The harness allocates each structure as one static object. A symbolic
*index* into a single known object does not change cost measurably, and
neither does holding the pointer in an anonymous union:
`dfcc_union_index_control.sh` runs the four combinations of {pointer held in a
union, plain pointer} x {symbolic index, constant index} under a DFCC loop
contract, and all four take 0.1 s. So K is 1 or 2
in the code under proof, and section 4's term cannot explain anything there.

This also weakens the first version's suggested link to the
anonymous-union `ERROR` item (`cbmc-driver-proof-design-2026-11.md` section
6c). A union-held pointer did not widen the points-to set here:
`ring.$anon1.tx_buffer_info` resolves to exactly one object. The two problems
are probably independent.

## 6. The actual root cause, and confirmation

### 6.1 What the run was doing

The non-terminating run, rerun in the background with `--verbosity 9`:

```sh
cbmc w2_na.gb --bounds-check --pointer-check --no-standard-checks --verbosity 9
```

Of the 2281 `Unwinding loop` messages before the run was killed, **2269 are
`ena_tx_map_mbuf.0`**. Every other loop appears at most 4 times. The log ends
mid-unwind:

```
Unwinding loop ena_tx_map_mbuf.0 iteration 2269 file .../ena_ethdev.c line 3108 function ena_tx_map_mbuf thread 0
```

Line 3108 is the segment walk in the callee:

```c
	while ((mbuf = mbuf->next) != NULL) {
		seg_len = mbuf->data_len;
		...
	}
```

### 6.2 Why it is unbounded

The harness builds a well-formed mbuf chain of at most `MAX_SEGS` segments
(`make_wf_mbuf`), so without a loop contract the chain is finite and
`--unwind 16` covered it (stage 1 of the design doc). The widened contract on
the outer loop changes that. DFCC havocs every assigns target at the loop head,
and `__CPROVER_object_whole(tx_ring->tx_buffer_info)` covers the
`tx_buffer_info[req_id].mbuf` slots, so the pointers the loop body reaches are
no longer the harness's well-formed chain. The *inner* loop in
`ena_tx_map_mbuf` has no contract of its own, and the command had no
`--unwind`, so symex follows `mbuf->next` indefinitely.

The cost was never in the assigns clause. It came from what the havoc lets the
uncontracted inner loop do.

### 6.3 Confirmation by fixing it

Adding only a bound for that one loop, with the unwinding assertion turned on
so the bound must be shown sufficient:

```sh
cbmc w2_na.gb --bounds-check --pointer-check --no-standard-checks \
     --unwindset ena_tx_map_mbuf.0:4 --unwinding-assertions --verbosity 9
```

| clause | result | wall | symex | solver | variables | clauses |
|---|---|---|---|---|---|---|
| widened, **without** `object_whole(adapter)` (`tx_loop_contract_noadapter.json`) | **VERIFICATION SUCCESSFUL**, 0 of 1408 failed | 13.2 s | 1.90 s | 9.34 s | 5,232,958 | 2,358,315 |
| widened, **with** the 178 KB `object_whole(adapter)` (`tx_loop_contract_wide.json`) | **VERIFICATION SUCCESSFUL**, 0 of 1414 failed | 10.4 s | 1.90 s | 6.64 s | 5,233,471 | 2,358,991 |

Both runs pass `ena_tx_map_mbuf.unwind.0`, so 4 iterations cover every
execution of the inner loop under this harness. The bound is complete here, not
a cut. Both also discharge the outer loop's contract: `loop_invariant_base`,
`loop_invariant_step`, `loop_decreases` and `loop_assigns` all `SUCCESS`.

The 178 KB adapter target changes the formula by 513 variables and 676 clauses
out of millions. **The large `object_whole` target was not a material cost
either.** The first version's family-3 result (6.1 s for one 128 KB target)
still holds for that program shape. It does not carry over to the ENA harness,
where the havoc is dominated by other targets and the target is never written.

### 6.4 The same fix with the current harness

The two runs in 6.3 used a goto binary built from an earlier version of the
stage-1 harness, before the ownership clauses existed. Re-running section 7's
commands verbatim against the harness now in the repository
(`proof/harness_tx_callees.c`, same geometry `MAX_BURST=2 MAX_SEGS=2
RING_SIZE=2`, `tx_loop_contract_wide.json`) gives

```
** 0 of 1482 failed (1 iterations)
VERIFICATION SUCCESSFUL
  main.assertion.1 API: sent <= nb_pkts: SUCCESS
  main.assertion.2 INV preserved by eth_ena_xmit_pkts: SUCCESS
  main.assertion.3 INV_wakeup: no packet left pushed without a doorbell: SUCCESS
  main.assertion.4 OWN_UNIQUE: no mbuf owned by two tx_buffer_info slots: SUCCESS
  main.assertion.5 OWN_TRANSFER: accepted mbuf recorded in its req_id slot: SUCCESS
[ena_tx_map_mbuf.unwind.0] line 3108 unwinding assertion loop 0: SUCCESS
```

in 13.3 s. So the one proof that combines the real callee bodies, an unbounded
TX burst, and all five named obligations, including the two ownership clauses,
now goes through. Two qualifications: the ring and segment geometry are still
small (2 and 2), and the inner segment walk is bounded at 4 with an unwinding
assertion rather than discharged by its own loop contract.

## 7. Reproducing sections 5-6

Prerequisites, from `proof/run.sh`: DPDK v24.11 checked out at `/tmp/dpdk` with
`amzn-drivers/userspace/dpdk/ena` copied over `drivers/net/ena`, configured as
`build-ql`, and `flags.txt` extracted from its `compile_commands.json`.

```sh
P=integration/linux/scan/amzn-drivers/proof
cd /tmp/dpdk/build-ql
goto-cc $(cat /tmp/ena-proof/flags.txt) -DPEDANTIC \
  -DMAX_BURST=2 -DMAX_SEGS=2 -DRING_SIZE=2 $P/harness_tx_callees.c -o w2.gb

RM=""
for f in rte_pktmbuf_free rte_pktmbuf_free_seg rte_pktmbuf_prefree_seg \
         rte_pktmbuf_read rte_pktmbuf_alloc_bulk rte_pktmbuf_detach \
         rte_mempool_put rte_mempool_put_bulk rte_mempool_get_bulk \
         rte_mempool_generic_put rte_mempool_generic_get \
         rte_mempool_do_generic_put rte_mempool_do_generic_get \
         rte_mempool_cache_flush rte_mbuf_raw_free; do
  RM="$RM --remove-function-body $f"
done
goto-instrument $RM w2.gb w2_rm.gb
goto-instrument --add-library w2_rm.gb w2_lib.gb           # must precede --dfcc
goto-instrument --no-malloc-may-fail \
  --loop-contracts-file $P/tx_loop_contract_wide.json \
  --dfcc main --apply-loop-contracts w2_lib.gb w2_lc.gb

# section 6.1: does not terminate; watch it unwind ena_tx_map_mbuf.0
cbmc w2_lc.gb --bounds-check --pointer-check --no-standard-checks --verbosity 9

# section 6.3: terminates in ~10 s, VERIFICATION SUCCESSFUL
cbmc w2_lc.gb --bounds-check --pointer-check --no-standard-checks \
     --unwindset ena_tx_map_mbuf.0:4 --unwinding-assertions

# section 5: points-to set sizes
goto-instrument --show-value-sets w2_lib.gb > vs.txt
```

Two notes for anyone rerunning this. The repository's
`harness_tx_callees.c` includes the ownership clauses, so the expected result
is the 1482-property run of section 6.4, not the 1408/1414 of section 6.3.
And the `--show-value-sets` output is about 33 MB, with one block per program
location. The 2-object maximum came from scanning the
`<object, offset, type>` entries for variables in the three TX functions.

## 8. What this does and does not establish

Established:

* the ENA widened-contract proof did not terminate because the uncontracted
  inner loop `ena_tx_map_mbuf.0` was unwound without bound under the outer
  loop's havoc; bounding it, with the unwinding assertion proving the bound
  sufficient, makes the proof pass in 10-13 s, including with the current
  harness and its ownership clauses (sections 6.3, 6.4);
* the assigns clause, including its 178 KB `object_whole` target, contributes
  negligibly to that proof (section 6.3);
* the superlinear cost in the first benchmark is CBMC symex dereferencing a
  K-target pointer, present without DFCC; DFCC's added symex share falls from
  8.5x to 1.3x as K grows (sections 3.2 and 4);
* DFCC's attributable cost is clause count, linear in K, and 7-18x larger for
  `object_whole` than for a precise target (section 3.2);
* the ENA TX path's write pointers have points-to sets of at most 2 (section 5).

Not established:

* the exact complexity of the dereference cost. About 7x per doubling suggests
  a cubic term, but four points do not pin an exponent, and no single function
  in the profile accounts for it on its own;
* whether section 4's cost is an inefficiency worth reporting. It may be the
  expected price of value-set-based dereferencing with simplification. Before
  calling it a bug it needs comparing against the theoretical size of the
  K-way case split, and the profile should be read by someone who knows
  `symex_assignt::assign_if`;
* why the havoc leaves the chain *unbounded* rather than merely longer. The
  run does not show whether the havocked `mbuf` can form a cycle or is a fresh
  unbounded chain, and `--unwinding-assertions` at 4 only shows that the
  harness's own chains are covered once the bound is in place;
* anything about other solvers, CBMC versions, or hosts.

## 9. What this changes

For the ENA proof, which matters most:

1. **Item 1 of the revised plan is done, not blocked.** One proof with the
   real callee bodies, an unbounded TX burst, and all five named obligations
   including `OWN_UNIQUE` and `OWN_TRANSFER` passes in 13 s (section 6.4). The
   fix is to bound (or give a contract to) the inner segment walk, with
   `--unwinding-assertions` so the bound is checked. The next steps are the
   larger geometry, and a loop contract on `ena_tx_map_mbuf.0` in place of the
   unwind bound.
2. **Diagnosis practice:** when a contract-based proof does not terminate, run
   it with `--verbosity 9` and count `Unwinding loop` lines per loop *before*
   suspecting the contract machinery. In this case that one check would have
   located the cause immediately.

On the earlier action list:

* **"shrink K"** — not relevant to the ENA proof (K ≤ 2 there). Still good
  advice for harnesses that really do dereference many-target pointers.
* **"never put a large aggregate in `object_whole`"** — not supported for the
  ENA proof (513 extra variables). The general cost of section 3.1 family 3
  still holds where a large target is *written*.
* **"prefer precise targets"** — supported as a formula-size reduction (7-18x
  fewer clauses in section 3.2). The time effect is secondary.
* **"report the encoding upstream"** — **withdrawn.** The cost it targeted is
  not in DFCC. If anything goes upstream it is section 4 (one K-target
  dereference, no contracts). That needs the analysis listed under "not
  established" before filing.
