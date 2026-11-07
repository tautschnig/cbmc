# Building the FreeBSD ENA driver for CBMC

A compiling build with the **real** FreeBSD kernel headers, not stubs. Stub
headers would be a false economy here: the invariants are statements about
field offsets in real structures (`INV_DESC_SUM` is about `tx_descs` in
`struct ena_tx_buffer`), so a guessed layout would verify cleanly while
proving nothing about the shipped driver.

## Headers

```sh
git clone --depth 1 --no-checkout https://github.com/freebsd/freebsd-src /tmp/freebsd-src
cd /tmp/freebsd-src && git sparse-checkout init --cone && git sparse-checkout set sys && git checkout
```

`--filter=blob:none --sparse` is a trap: the clone succeeds but leaves blobs
to be fetched lazily, one at a time. Expanding the sparse set afterwards pulls
them in one batch -- 806 MB in under three minutes.

## Generated interface headers

FreeBSD generates `device_if.h`, `bus_if.h` and friends from `.m` files at
build time, using its own awk script:

```sh
cd /tmp/fbsd-build
for m in $(find /tmp/freebsd-src/sys -name '*_if.m'); do
  awk -f /tmp/freebsd-src/sys/tools/makeobjops.awk "$m" -h
done        # 147 headers
```

## Machine include links and flags

```sh
S=/tmp/freebsd-src/sys
ln -sfn $S/amd64/include machine
ln -sfn $S/x86/include   x86
: > opt_global.h                      # config(8) generates these; empty is right for a module
echo '#include <sys/limits.h>' > limits.h
echo '#include <sys/stdint.h>' > stdint.h   # must win over GCC's, which defines int_fast8_t twice
```

```
-nostdinc -isystem $(gcc -print-file-name=include)
-D_KERNEL -DKLD_MODULE -DHAVE_KERNEL_OPTION_HEADERS -include opt_global.h
-I. -I$S -I$E -I$E/ena-com -I$S/contrib/ck/include
-fno-strict-aliasing -ffreestanding -std=gnu99
```

`-isystem` with the compiler's own include directory is how FreeBSD supplies
the freestanding headers (`stdbool.h`, `stddef.h`, `stdarg.h`) under
`-nostdinc`. Stubbing those empty appears to work and silently breaks `bool`.

## Result

All seven translation units compile: `ena.c`, `ena_datapath.c`,
`ena_netmap.c`, `ena_rss.c`, `ena_sysctl.c`, `ena-com/ena_com.c`,
`ena-com/ena_eth_com.c`. Linked, they give a 1.9 MB goto binary with 5129
symbols, and every function a TX proof needs has a body.

`ena_tx_cleanup` is `static`, so `harness_fbsd_cleanup.c` includes
`ena_datapath.c` wholesale, the way the DPDK harnesses include
`ena_ethdev.c`. Link it **without** `ena_datapath.o`.

Cost note: the cleanup loop is bounded by `ENA_TX_BUDGET` (128), not by the
ring size, so this proof does not get the DPDK cleanup proof's free ride from
a concrete ring bound. At `--unwind 12` with MiniSAT the instance reached
5.2M variables and 158M clauses and did not finish; `--sat-solver cadical`
with `--unwind 6` at ring 4 completes.

## Loop contracts on FreeBSD kernel code: blocked

The cleanup loop is bounded by `ENA_TX_BUDGET` (128), not by the ring size, so
unlike the DPDK cleanup proof it gets no free ride from a concrete ring bound:
a sound `--unwinding-assertions` run would need `--unwind 129`, and `--unwind 6`
with full checks already exceeds 2400 s. A loop contract is the right answer,
because it removes the unrolling entirely and gives a result that is unbounded
in the number of completions processed, with `budget` as the ranking function.

The contract is written and syntactically accepted
(`fbsd_cleanup_loop_contract.json`, loop `ena_tx_cleanup.1`): invariant
carrying `next_to_clean < ring_size`, the budget and commit ranges, the class A
clause `!g_freed_while_mapped`, `g_free_calls <= 1`, and the no-dangling clause
at a probe slot; `decreases: budget`.

**It cannot be applied.** `--dfcc` loads the CPROVER C library for its
dynamic-frame bookkeeping, and that library's allocator declarations collide
with the FreeBSD kernel's:

```
file <builtin-library-malloc> line 11: 'malloc'
old definition in module ena, /tmp/freebsd-src/sys/sys/malloc.h line 194
  void * (size_t, struct malloc_type *, signed int)
new definition in module <built-in-library>
  void * (__CPROVER_size_t malloc_size)
```

and the same for `free`, which the kernel declares as
`void free(void *, struct malloc_type *)`. Renaming with `-Dfree=fbsd_kfree`
clears the `free` collision; doing the same for `malloc` breaks FreeBSD's
allocator macro machinery and leaves an implicit declaration at
`ena.c:656`. So DFCC loop contracts are currently unusable on FreeBSD kernel
sources, which is worth raising separately -- it blocks loop contracts for any
FreeBSD driver, not just this one.

Until then the FreeBSD cleanup obligations can only be stated as bounded
claims, and they are **not** currently established -- see the note in
`amzn-drivers-2026-11.md`.

**Correction (follow-up analysis).** The allocator messages above are
warnings, not the failure. Plain `cbmc` on the same goto binary prints the
same two conflicts and carries on: linking attaches the library's
`malloc(size)` and `free(ptr)` bodies to the kernel's declarations, and the
surplus arguments (`M_DEVBUF`, the flags) are ignored. What actually stopped
`goto-instrument --dfcc main --apply-loop-contracts --loop-contracts-file
fbsd_cleanup_loop_contract.json` was an invariant failure much later, in
`goto_inline_class.cpp` (`parameter_assignments`), on the variadic
`device_printf(dev, fmt, ...)`: DFCC appended its write-set argument after the
variadic arguments, so a `uint16_t` argument bound to the write-set pointer
parameter while loop-assigns inference inlined the callees of
`ena_tx_cleanup`.

The allocator signature does matter under DFCC, but differently: the
write-set argument was appended after `M_DEVBUF`, so it bound to `M_DEVBUF`
and every kernel `malloc`/`free` failed the write-set checks. The cleanup
harness never calls them (they are removed as unused), so this did not affect
it.

Both are fixed on branch `dfcc-variadic-write-set` (DFCC now passes the write
set at the position of the write-set parameter, which comes first for
variadic functions, and the inliner no longer aborts on a mismatched
argument). With that build the loop contract is applied without error. A run
with `--unwind 5 --unwinding-assertions --sat-solver cadical` at ring 4 did
not finish within 1800 s, so the obligations are still not established.


## Update: the loop contract applies, and the cost is now tractable

With the five commits of `fbsd-dfcc-findings` cherry-picked, the contract
applies to `ena_tx_cleanup` and the instance is 4.9x smaller.

**My earlier diagnosis was wrong** and is corrected above: the `malloc`/`free`
messages are linking warnings that plain `cbmc` prints too. The abort was
DFCC's handling of the variadic `device_printf`, fixed in `79e4fb92c48`, and
the kernel allocator signatures caused *wrong write-set checks* rather than a
failure, fixed in `d5ce47f63ab`. In my own transcript the real signal was the
`--- begin invariant violation report ---` immediately after the warning, and
I attributed it to the line above.

### What actually cost the clauses

Not what I guessed either. Narrowing the assigns clause from
`__CPROVER_object_whole(&ena_dev)` to the fields the loop writes, and pinning
`io_cq_queues[0].qid = 0`, moved the instance only from 134.9M to 134.8M
clauses -- reproducing the hand-off note's own finding that pinning `qid` does
not help. The reason is the `(uintptr_t)` cast inside ENA's `container_of`:

```c
#define container_of(ptr, type, member) \
    ((type *)((uintptr_t)__p - offsetof(type, member)))
```

CBMC loses object identity across the integer cast, so a *concrete* `qid` does
not recover it. Every field read through that pointer is an 8-byte
`byte_extract` at a symbolic offset over all 99,648 bytes of
`struct ena_com_dev`.

The lever is to stop the reads happening at all. `ena_trace` expands to
`ena_log((ctx)->dmadev, ...)`, so each trace call dereferences that pointer.
Neutralising `ena_trace` -- including `ena_plat.h` first so its include guard
stops the real definitions coming back, then redefining -- removes the two
hot reads in `ena_com_tx_comp_req_id_get`. Logging is in no obligation here,
so this drops nothing the proof is about.

| configuration | variables | clauses | wall |
|---|---|---|---|
| `object_whole(&ena_dev)`, trace live | 30.3M | 134.9M | did not finish in 1500 s |
| narrowed assigns, `qid` pinned, trace live | 30.3M | 134.8M | did not finish |
| **narrowed assigns, trace neutralised** | **8.5M** | **27.5M** | **147 s** (12.7 GB) |

### Three genuine harness gaps, surfaced in sequence

Once the run completed it reported real properties, each a missing piece of
the harness rather than a driver defect:

1. `ena_trigger_reset` writes `adapter->reset_reason` and the flag word on the
   invalid-req_id path; both were missing from the assigns clause, along with
   the `tx_stats` counters.
2. My invariant had `budget >= 0`, but the loop is a `do/while` that
   decrements at the end, so the loop head always has `budget >= 1`. With
   `>= 0` the step case may start at `budget == 0`, decrement to -1 and
   violate the clause.
3. The tail of `ena_tx_cleanup` reads `tx_ring->ena_com_io_sq`, which the
   harness never set -- `tx_ring` is a zero-initialised static, so that was a
   NULL read.

### Still open

After fixing all three, the first failure is
`ena_com_sq_have_enough_space` line 122, `pointer invalid in
io_sq->mem_queue_type`, in the cleanup tail. So the four obligations are
still **not** established. The next step is that pointer's validity, not a
cost problem.

## The FreeBSD cleanup obligations are now established

`run_fbsd_cleanup.sh`, ring size 4, loop contract on `ena_tx_cleanup` with
`budget` as the ranking function, so the result is **unbounded in the number
of completions processed** rather than bounded at an unwinding depth:

```
** 0 of 1487 failed (1 iterations)
VERIFICATION SUCCESSFUL
  FREE_ONCE: no mbuf is released twice:                         SUCCESS
  DMA_UNMAPPED_BEFORE_FREE: no mbuf released while mapped:      SUCCESS
  INV_NTC: the consumer index stays inside the ring:            SUCCESS
  OWN_NO_DANGLING: no slot still owns a released mbuf:          SUCCESS
21 loop-contract properties SUCCESS, including "Check step was unwound"
UNKNOWN 0   UNREACHABLE 0   vacuity witness FAILURE
```

`DMA_UNMAPPED_BEFORE_FREE` is the first coverage of **Unveil class A**
(device DMA into freed memory, 6%): FreeBSD unmaps before `m_freem`, and the
proof now says so for an unbounded number of completions.

The contract is 18 invariant clauses, 18 assigns targets, `decreases: budget`.

### Under two named device assumptions, which mask F11

`ASSUME_DEVICE_REQ_ID_IN_RANGE` constrains the device to report `req_id`
inside the ring **and** excludes the corrupted-descriptor path. Both are
needed because **F11 has two triggers**, and the second is worse:

1. `*req_id` is assigned from the descriptor and then rejected as out of
   range (`ENA_COM_INVAL`); the caller dereferences it anyway.
2. On the must-be-zero check the function returns `ENA_COM_FAULT` **before**
   `*req_id` is assigned at all, so the caller indexes `tx_buffer_info` with
   an **uninitialised stack value**. Constraining the device's `req_id` range
   does not mask this, because the value never comes from the descriptor on
   that path.

### Invariant clauses the proof demanded, and why

* `tx_ring->next_to_clean < ring_size` as well as the local `next_to_clean`.
  The ring field is in the frame, so the havoc destroys it, and it is written
  only in the commit branch. Whatever the frame destroys, the invariant must
  restore.
* `budget >= 1`, not `>= 0`: the `do/while` decrements at the end, so the loop
  head always has at least 1 left. With `>= 0` the step case may start at 0,
  decrement to -1 and break its own clause.
* Ownership stated **over the concrete ring**, not at a probe index. A probe
  clause is universal quantification when *asserted*, but when *assumed* in
  the step case it constrains only that index, and the body frees slot
  `req_id`. Both the slot-to-mbuf mapping and "once released, no slot holds it
  again" had to be written out for all four slots -- otherwise the frame havoc
  may put a released mbuf back into a slot and `FREE_ONCE` fails on a later
  iteration. This makes the invariant ring-size specific, as `INV_DESC_SUM` is
  in the DPDK proof.

### `__CPROVER_object_whole(&tx_ring->tx_stats)`: causal, mechanism unexplained

Replacing `__CPROVER_object_whole(&tx_ring->tx_stats)` with
`tx_ring->tx_stats` in the assigns clause is what made the cleanup tail stop
failing a pointer-validity check on `tx_ring->ena_com_io_sq`; the trace showed
that field assigned correctly and then overwritten to `INVALID`. So the change
was causal.

The explanation first given here -- that `object_whole` on a field address
widens to the whole enclosing object -- **does not hold up**: two minimal
cases, including one in this exact shape (struct field reached through a
pointer parameter, sibling pointer field), show the sibling field surviving.
See `proof/cbmc-limitations/NOT-REPRODUCED.md`. The mechanism is open;
`tx_stats` possibly sitting in one of `struct ena_ring`'s anonymous unions is
the first thing to check, since union-held members are already known to
produce `byte_extract` expressions that CBMC's pointer analysis cannot
resolve.

It is **not** the same trap as `make_wf_mbuf` and the `llq_info` havoc: those
are about `__CPROVER_havoc_object`, whose whole-object behaviour is documented
and confirmed.
