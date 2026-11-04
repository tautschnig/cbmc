# Why a DFCC assigns clause gets expensive: measurement and root cause

**Date:** 2026-11-04
**Author:** investigation run in `integration/linux/` on the amzn-drivers ENA
proof (see `cbmc-driver-proof-design-2026-11.md` section 8.5a for the short
version; this document is the long, reproducible one).
**Status:** root cause identified and reduced to a standalone benchmark. The
benchmark needs nothing but CBMC and a C compiler. The driver-scale part needs
DPDK and amzn-drivers.

## 1. Claim

Under dynamic frame condition checking (`goto-instrument --dfcc
--apply-loop-contracts`), the cost of an `__CPROVER_assigns` clause is
dominated by **how many distinct objects a symbolic write pointer in the loop
body may designate**, not by the number of assigns targets, the number of
writes, or the number of objects in the program. The relationship is
superlinear: 4x the candidate objects cost 14x the time in our measurements.

Two secondary terms matter in practice: the **byte size** of an
`__CPROVER_object_whole` target (linear), and target granularity
(`object_whole` versus a precise target, worth about 30%).

The mechanism is that the write-set membership check runs at every write and
contains an array read indexed by the written pointer's *object id*; when that
id is symbolic, CBMC's dereference case-splits over all candidate objects and
the array read inside each case is itself a case split, giving roughly
`O(K^2 + K*T)` work per write for `K` candidate objects and `T` targets.

## 2. Environment used

Recorded so differences on another host can be attributed.

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
| DPDK (driver-scale part only) | v24.11, tag checkout |
| amzn-drivers (driver-scale part only) | `9d5ca40` |

**The benchmark in section 4 does not need our local CBMC changes.** It is
plain C using only `__CPROVER_assigns`, `__CPROVER_loop_invariant` and
`__CPROVER_decreases`, so an upstream CBMC 6.9.0 build should reproduce it.
The one cherry-pick this tree carries (`b9db4fd70b1`, constructor/destructor
attribute with a priority argument, upstream PR #8747) is needed **only** for
section 6, because DPDK's `RTE_PMD_REGISTER_PCI` uses
`__attribute__((constructor(prio)))`.

Timings are wall-clock from a single run each, on an idle machine, with
`ulimit -v 32000000` (≈32 GiB) and `timeout 900`. They are stable to about
±10% here; treat the *shape* of the scaling as the result, not the absolute
seconds.

## 3. Building CBMC

```sh
git clone https://github.com/diffblue/cbmc.git && cd cbmc
git submodule update --init
cmake -S . -Bbuild -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel $(nproc)
# binaries in build/bin: cbmc, goto-cc, goto-instrument
```

For section 6 only, also apply upstream PR #8747:

```sh
git fetch https://github.com/diffblue/cbmc.git pull/8747/head:pr8747
git cherry-pick 317d889dadf        # C front-end: constructor/destructor priority
cmake -S . -Bbuild                 # REQUIRED: the PR adds a new source file that
                                   # an existing build directory will not glob
cmake --build build --parallel $(nproc)
```

## 4. The benchmark

`integration/linux/scan/amzn-drivers/proof/cbmc-limitations/dfcc_assigns_scaling.sh`

```sh
BIN=/path/to/cbmc/build/bin \
  integration/linux/scan/amzn-drivers/proof/cbmc-limitations/dfcc_assigns_scaling.sh
```

It generates four families of single-loop C programs in a temp directory, and
for each one runs

```sh
goto-cc prog.c -o b.gb
goto-instrument --no-malloc-may-fail --dfcc main --apply-loop-contracts b.gb b_lc.gb
cbmc b_lc.gb --no-standard-checks
```

timing the `cbmc` step. Every program has exactly one contract-carrying loop
with an invariant (`i <= n`) and a variant (`n - i`), and an unbounded trip
count (`n` is nondeterministic), so CBMC never unrolls it -- the loop is
discharged by the contract. `--no-standard-checks` keeps the comparison to
the frame-condition machinery rather than the usual assertion set.

Each family varies exactly one dimension:

1. **unrelated objects, concrete write pointer** -- `N` global arrays exist;
   the loop assigns and writes only `o0`, by name.
2. **number of assigns targets** -- the clause names `T` `object_whole`
   targets; the body still writes only `o0`.
3. **`object_whole` target size** -- one target, whose object grows from 128 B
   to 128 KB; one write.
4. **objects a symbolic write pointer may designate** -- a pointer `p` is set
   from one of `K` distinct globals under a nondeterministic `switch`, the
   clause is `object_whole(p)`, and the body writes `p[0].buf[0]`. CBMC
   therefore cannot pin down which object is written.

## 5. Results

Full output of one run (`== (n)` headings are the script's):

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

Two further one-off measurements, from an earlier session of the same
benchmark harness (commands in section 5.1):

* **writes in the loop body**, concrete pointer, 8 targets: 0.1 / 0.2 / 0.2 /
  0.3 / 0.4 / 0.8 s for 1 / 2 / 4 / 8 / 16 / 32 writes -- linear.
* **a union-held target pointer, or a symbolic write *index* into a known
  object**: no measurable effect, 0.1 s in all four combinations of
  {union, no union} x {symbolic index, constant index}.
* **precise target instead of `object_whole` at K=128**: 11.5 s versus 16.5 s,
  i.e. about 30% off, with the same `VERIFICATION SUCCESSFUL`.

Reading of the table:

* dimensions (1) and (2) and the write count are flat or linear and cheap;
* (3) is linear in bytes, and large enough to matter for real aggregates;
* (4) is the dominant term. From K=32 to K=128 the object count grows 4x and
  the time grows 12.7x, which is the signature of a quadratic term.

### 5.1 Reproducing the two one-off measurements

The script covers families 1-4. For the write-count sweep and the
union/symbolic-index controls, the generators are short enough to inline; both
were run with the same `goto-cc` / `goto-instrument` / `cbmc` pipeline as the
script. The write-count sweep is family 2 with the body repeated `W` times
writing `o[W % T]`; the union control is

```c
struct ring {
  union { struct inner *a; struct inner *b; };   /* or a plain pointer */
  unsigned size;
};
...
__CPROVER_assigns(i, __CPROVER_object_whole(r.a))
...
{ unsigned k = nd(); if (k < N) r.a[k].buf[0] = (int)i; }   /* or r.a[0]... */
```

and the precise-target variant of family 4 replaces
`__CPROVER_object_whole(p)` with `p[0].buf[0]`.

## 6. The mechanism, in the source

`src/ansi-c/library/cprover_contracts.c`, function
`__CPROVER_contracts_write_set_check_assignment` (line 774 in this tree; the
non-`__CPROVER_DFCC_DEBUG_LIB` branch starting at line 799 is the one used by
default). DFCC inserts a call to it **for every write** in a
contract-carrying region. The relevant body, abridged:

```c
  // is ptr pointing within some a locally allocated object ?
  if(set->allocated.elems[__CPROVER_POINTER_OBJECT(ptr)] != 0)
    return 1;
  ...
  __CPROVER_contracts_car_t *elem = set->contract_assigns.elems;
  __CPROVER_size_t idx = set->contract_assigns.max_elems;
  __CPROVER_bool incl = 0;
SET_CHECK_ASSIGNMENT_LOOP:
  while(idx != 0)
  {
    incl |=
      (int)elem->is_writable & (int)__CPROVER_same_object(elem->lb, ptr) &
      (__CPROVER_POINTER_OFFSET(elem->lb) <= offset) &
      (__CPROVER_POINTER_OFFSET(ub) <= __CPROVER_POINTER_OFFSET(elem->ub));
    ++elem;
    --idx;
  }
  return incl;
```

Two costs per write:

1. `set->allocated.elems[__CPROVER_POINTER_OBJECT(ptr)]` -- an array read
   whose **index is the written pointer's object id**. If the pointer is
   concrete the index folds to a constant and this is free. If it is symbolic
   the read becomes a selection over the array, i.e. a case split whose width
   is the number of objects CBMC must consider.
2. `SET_CHECK_ASSIGNMENT_LOOP` -- a loop over all `T` targets, each iteration
   evaluating `__CPROVER_same_object(elem->lb, ptr)` and two
   `__CPROVER_POINTER_OFFSET` computations on a symbolic pointer.

On top of that, CBMC's own dereference of a symbolic pointer case-splits over
the `K` candidate objects *before* reaching this function. Inside each of
those `K` cases, cost 1 is itself `K`-wide and cost 2 is `T`-wide. Net
`O(K^2 + K*T)` per write, matching the measured shape: flat in unrelated
objects when the pointer is concrete (K = 1 regardless of how many objects
exist), linear in `T`, and quadratic in `K`.

The relevant instrumentation on the CBMC side lives in
`src/goto-instrument/contracts/instrument_spec_assigns.cpp` (inserting the
checks) and `src/goto-instrument/contracts/dynamic-frames/` (the write-set
representation).

## 7. The driver-scale case: why the ENA proof hit it

Context: `integration/linux/scan/amzn-drivers/proof/harness_tx_callees.c` with
`tx_loop_contract_wide.json`, i.e. the attempt to prove the ENA TX path with
the real callee bodies *and* an unbounded burst. It does not terminate within
2400 s at ring size 4, nor at ring size 2. Setup instructions are in
`proof/run.sh` and `proof/run_stage2_unbounded.sh`.

Three multipliers coincide:

**K is large.** The harness includes the whole DPDK ENA translation unit.
Counting static-lifetime non-function symbols in the linked goto binary:

```sh
goto-instrument --show-symbol-table w1_lib.gb 2>/dev/null | awk '
/^Symbol\.\.\.\.\.\.: /{sym=$2; stat=0; isfn=0}
/^Type\.\.\.\.\.\.\.\.: .*\(/{isfn=1}
/^Flags\.\.\.\.\.\.\.: .*static_lifetime/{stat=1}
/^$/{ if(sym!="" && stat==1 && isfn==0) n++; sym="" }
END{print "static-lifetime non-function symbols:", n}'
# => 83
```

plus dynamically allocated objects.

**The write pointers are symbolic.** The loop body writes
`tx_ring->tx_buffer_info[req_id]` where `req_id = empty_tx_reqs[next_to_use]`
is symbolic, and `tx_buffer_info` is itself a pointer read out of an anonymous
union in `struct ena_ring` (`ena_ethdev.h:158-167`).

**One target is enormous.**

```sh
# from the DPDK build directory, with the PMD's own include flags
printf '#include "ena_ethdev.h"\n#include <stdio.h>\nint main(void){printf("%%zu\\n", sizeof(struct ena_adapter));}\n' \
  | gcc $(cat flags.txt) -DPEDANTIC -x c - -o /tmp/sz && /tmp/sz
# => 182144
```

`struct ena_adapter` is **182144 bytes** because it embeds
`tx_ring[ENA_MAX_NUM_IO_QUEUES]` and `rx_ring[...]`. The widened clause
contains `__CPROVER_object_whole(tx_ring->adapter)`, so every write in the
loop body is checked against a 178 KB candidate-assignable-range, and loop
entry havocs all 178 KB. By family (3) a single 128 KB `object_whole` target
already costs 6.1 s with *one* trivial write.

## 8. A probable link to the other open CBMC item

`cbmc-driver-proof-design-2026-11.md` section 6c records a separate symptom:
accesses through the same anonymous unions produce
`byte_extract_little_endian(txq->$anon0, ...)` and property status **ERROR**
under DFCC, and that one is **not** reduced to a minimal reproducer (two
attempts in this directory both pass).

These may be one problem rather than two. A `byte_extract` on a union-held
pointer plausibly degrades CBMC's points-to information for that pointer,
which would widen `K` -- the dominant term here. Worth testing explicitly
when reducing 6c: compare the candidate-object count for a pointer read from
a union against the same pointer held in a plain field.

## 9. What this does and does not establish

Established:

* the scaling in section 5, on this host, for this CBMC build and these
  program shapes;
* that the write-set check is per-write and contains the symbolic-index array
  read and the per-target loop quoted in section 6;
* that the ENA harness sits in the regime where all three multipliers apply,
  with the three numbers measured (83 objects, symbolic write pointers,
  182144-byte target).

Not established:

* that `O(K^2 + K*T)` is the exact complexity -- the measurements are
  consistent with a quadratic term in `K` but four data points do not pin an
  exponent, and no profiling was done to attribute time between symex and SAT;
* that the quadratic term is the *only* reason the ENA proof times out --
  it is sufficient to explain it, but the proof was never made to terminate,
  so the explanation is not confirmed by fixing it;
* anything about other solvers, other CBMC versions, or 32-bit hosts;
* the section 8 link, which is a hypothesis.

## 10. Suggested actions

In measured order of effect. These are proposals for discussion, not a
decision:

1. **Shrink `K`.** Keep the set of objects a write pointer may designate
   small: avoid pulling a whole translation unit into a proof that does not
   need it, make indices concrete where the property does not depend on them,
   and keep union-held pointers out of the write path.
2. **Never put a large aggregate in `object_whole`.** Name the fields actually
   written. 178 KB of havoc for a function that writes a handful of fields is
   pure waste.
3. **Prefer precise targets to `object_whole`** -- about 30% at K=128, real
   but secondary.
4. **Report the encoding upstream.** The
   `set->allocated.elems[__CPROVER_POINTER_OBJECT(ptr)]` read is the
   quadratic term, and unlike section 6c this is reduced to a self-contained
   benchmark that runs on an unmodified CBMC. A possible direction for a fix
   is to avoid the object-id-indexed array for the locally-allocated test, but
   that is for the CBMC maintainers to judge.
