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
