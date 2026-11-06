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
