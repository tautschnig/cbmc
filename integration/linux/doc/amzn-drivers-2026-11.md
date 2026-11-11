# amzn-drivers: security pipeline run (Linux ENA/EFA, FreeBSD ENA, DPDK ENA)

**Date:** 2026-11-03
**Target:** https://github.com/amzn/amzn-drivers at `9d5ca40`
("linux/efa: Bump driver version to 3.4.0").
**Status:** No bug reachable from userspace or the network.  Seven places
where the drivers use device-written values without a bound.  Each has a
CBMC witness on a code slice, and F1-F6 now also have a **dynamic
confirmation** (KASAN in a QEMU guest, and ASAN in userspace) -- see
"Dynamic confirmation" below.  Still not reproduced on real or emulated
ENA/EFA hardware: the device value is injected directly.

## Threat model

Every finding below needs the device (or whatever emulates it) to send a
value the driver does not expect.  On EC2 the device is Nitro, so the
relevant settings are confidential-computing guests that do not trust the
host (SEV-SNP, TDX) and hardening against device firmware bugs.  The
drivers already validate several device fields (ENA RX/TX `req_id`, admin
`cmd_id`, AENQ group; EFA AENQ group), so their authors treat device input
as untrusted.  The findings are places that are inconsistent with that.

## What was run

* **Build.** Linux drivers built out of tree against `linux_6_12` in a
  scratch copy.  Needed: `KBUILD_MODPOST_WARN=1` (the 6.12 tree has no
  full `Module.symvers` for these symbols); `ENA_PHC_INCLUDE=1
  ENA_DEVLINK_INCLUDE=1` so the PHC and devlink code compiles;
  efa_nv_peermem's CMake ignores `KERNEL_DIR`, so the scratch copy was
  patched.  CodeQL DBs used the usual `percpu.h` `__seg_gs` workaround
  (reverted afterwards).  Every relevant `.c` file has extracted bodies;
  `dim.c`, `net_dim.c`, `ena_lpc.c` are empty because they only compile
  for kernels < 6.12.
* **Existing finders.** All 8 asset queries x 3 DBs completed (queries on
  one DB must run sequentially: CodeQL holds a per-DB cache lock).
  13 results in driver code; the rest are in kernel headers.
* **New: device-as-attacker query.** `DeviceTaint.qll` +
  `device_tainted_sink.ql` (commit `79986fc00c7`).  Sources: fields of
  structs reachable from DMA-coherent memory, MMIO reads, and counters of
  loops whose exit depends on device data.  Sinks: array index, copy
  length, loop bound, shift amount.  129 distinct sinks on ENA+EFA; it
  recovers all four bugs that were first found by the count/index finder
  or by hand, and found F5 and F6.
* **FreeBSD / DPDK.** No FreeBSD tree or DPDK build environment on the
  host, so no CodeQL DB.  Compared the shared `ena_com` code and the RX
  datapaths by reading the source.

## Findings

| # | Driver | Location | Effect | CBMC | Mainline Linux |
|---|---|---|---|---|---|
| F1 | EFA | `efa_request_doorbell_bar` | OOB read of `pdev->resource[17]` | yes | same code |
| F2 | ENA | RX multi-buffer (`ena_fill_rx_frags`, XDP mb) | OOB write of `skb_shinfo()->frags[17]` | yes | same gap |
| F3 | ENA | `ena_com_get_hash_key` | heap overflow of ethtool RSS key buffer | yes | same code |
| F4 | EFA | kverbs `efa_poll_sub_cq` | OOB read of `wrid[]`, OOB write of `wrid_idx_pool[]` | yes | n/a (out of tree only) |
| F5 | EFA | kverbs `efa_create_qp_kernel` | OOB increment/decrement in `sub_cq_arr[]` | yes | n/a (out of tree only) |
| F6 | ENA | RX completion length | `skb_over_panic` (DoS); frag past buffer | yes (modelled assertion) | same gap |
| F7 | EFA | kverbs `efa_get_dma_mr` | `BIT(64)` undefined shift | not run | n/a |

Dynamic status (detail below): F1-F6 all produce a KASAN report or a kernel
BUG; F3 additionally reproduces under ASAN against the real DPDK build.

### F1: EFA doorbell BAR index

`db_bar_idx = dev->dev_attr.db_bar` (u8, copied from the device's
`get_feature(DEVICE_ATTR)` reply).  `int db_bar = BIT(db_bar_idx)`
truncates to 0 for indices 32..63, so `db_bar & EFA_BASE_BAR_MASK` and
`db_bar & ~pci_mem_bars` are both 0 and every check passes.
`pci_resource_start(pdev, db_bar_idx)` then reads `resource[idx]` with
idx up to 255 against an array of `DEVICE_COUNT_RESOURCE` (17 on x86_64).
Indices >= 64 are also an undefined shift.  CBMC witness: `db_bar_idx=32`.
Fix: `if (db_bar_idx >= PCI_STD_NUM_BARS) return -EINVAL;` before `BIT()`.

### F2: ENA RX descriptor count vs `MAX_SKB_FRAGS`

The only bound on the per-packet descriptor count is
`nb_hw_desc <= max_rx_sgl_size = min(ENA_PKT_MAX_BUFS = 19, device value)`.
Descriptor 0 goes to the linear part; descriptors 1..18 become frags.  With
the default `CONFIG_MAX_SKB_FRAGS=17`, the 18th frag is written to
`frags[17]`.  Same in the XDP multi-buffer path (`ena_xdp_add_rx_frag`).
CBMC: fails at 19 descriptors; passes with `MAX_SKB_FRAGS` = 18.
Fix: also cap `max_rx_sgl_size` at `MAX_SKB_FRAGS + 1`.

### F3: ENA RSS hash key length

`ena_com_get_hash_function()` issues `GET_FEATURE(RSS_HASH_FUNCTION)`
with the coherent `rss->hash_key` buffer as the control buffer, so the
device writes the whole struct, including `key_parts`.
`ena_com_get_hash_key()` then does
`memcpy(key, hash_key->key, key_parts * 4)` into the caller's buffer.
On Linux that is ethtool's `rxfh.key`, sized by
`ena_get_rxfh_key_size()` = 40 bytes; `ena_get_rxfh()` calls both
functions on every RSS query.  The source read also runs past
`hash_key->key[10]`.
Fix: clamp to `min(key_parts, ENA_ADMIN_RSS_KEY_PARTS)` or reject.

### F4: EFA kernel-verbs completion `req_id`

`efa_poll_sub_cq()` takes `wrid_idx = cqe->req_id` (device-written) and
does `wq->wrid_idx_pool_next--; wrid_idx_pool[next] = wrid_idx;
wc->wr_id = wq->wrid[wrid_idx];` with no check against `max_wqes`, and no
check that a completion was outstanding (underflow of `wrid_idx_pool_next`
on an unexpected completion).  `efa_data_verbs.c` exists only out of tree
(`ENABLE_KVERBS` defaults to ON in the CMake build).
Fix: reject `req_id >= wq->max_wqes` and `wrid_idx_pool_next == 0`.

### F5: EFA kernel-verbs sub-CQ index

`efa_create_qp_kernel()` passes `create_qp_resp.send_sub_cq_idx` /
`recv_sub_cq_idx` (device reply) to `efa_cq_inc_ref_cnt()`, which does
`cq->sub_cq_arr[idx].ref_cnt++`; `sub_cq_arr` has `sub_cqs_per_cq`
entries.  The same indices are decremented on QP destroy.  No bound
check anywhere.  Found by the device-taint query.
Fix: reject `idx >= cq->num_sub_cqs` at QP creation.

### F6: ENA RX completion length

`cdesc->base.length` (u16) is never compared with the posted buffer size
(one page minus headroom/tailroom, or the XSK frame size).
* Linear path without page reuse: `build_skb(page, ENA_PAGE_SIZE - off)`
  then `skb_put(skb, len)`, so a large `len` hits `skb_over_panic()`
  (BUG, DoS).
* With page reuse, and for frags/XDP/zero-copy: the skb or `xdp->data_end`
  covers `len` bytes past the start of the buffer, which can extend into
  the next buffer or past the page (OOB read into the stack).
The CBMC slice is a modelled assertion (buffer offset + len inside the
page), weaker than the others: witness `len=36859`.
Fix: drop the packet and reset when `len > rx_info->ena_buf.len` (or the
frame size for XSK).

### F7: EFA `efa_get_dma_mr` shift (minor)

`efa_com_get_dma_width()` accepts widths 32..64 from a device register;
`efa_get_dma_mr()` computes `BIT(dma_addr_bits)`, which is undefined for
64.  Out of tree only.

## Dynamic confirmation

Two oracles, because the findings live in two places.  In every case the
only thing injected is the one device-supplied value named in the finding;
no bound is removed and no driver logic is edited.

### KASAN, QEMU guest (F1-F6)

Kernel: 6.12 LTS, `defconfig` + `CONFIG_KASAN=y` (generic, outline) +
`CONFIG_FORTIFY_SOURCE=y` + `CONFIG_MAX_SKB_FRAGS=17` (the default), built
in a scratch copy of the tree.  `CONFIG_FORTIFY_SOURCE` matters: without
it, `memcpy` in module code is the plain arch routine and KASAN does not
see the copy at all -- F3 then corrupts the slab silently and only shows up
later as a general-protection fault on the overwritten neighbour.  With it,
`memcpy` resolves to `__asan_memcpy` and the report is precise.

Harness: `scan/amzn-drivers/dynamic/kasan_enatest_main.c`, built as a module
together with the **real** `amzn-drivers` `ena_com.c` and `ena_eth_com.c`
(`kasan_enatest_Makefile`), run from a BusyBox initramfs
(`kasan_initramfs_init.sh`) under `qemu-system-x86_64`.  Splats in
`scan/amzn-drivers/dynamic/logs/`.

| test | report |
|---|---|
| F1 | `BUG: KASAN: slab-out-of-bounds`, read of size 8, `struct pci_dev` (kmalloc-4k) |
| F2 | `BUG: KASAN: slab-out-of-bounds in skb_add_rx_frag_netmem+0x7c/0x1e0`, write of size 8 past `skbuff_small_head` |
| F3 | `BUG: KASAN: slab-out-of-bounds in ena_com_get_hash_key+0x3e/0x50`, **write of size 256** into kmalloc-64 |
| F4 | `BUG: KASAN: slab-use-after-free`, read of size 8 (index lands in an unrelated object) |
| F5 | `BUG: KASAN: slab-out-of-bounds`, read of size 4 past the `sub_cq_arr` allocation |
| F6 | `skb_over_panic: ... len:36859 put:36859 tail:0x90fb end:0xec0` -> `kernel BUG at net/core/skbuff.c:209` in `skb_put` |

How much real code each test exercises differs, and that matters when
reading the table:

* **F3 is the strongest.**  The faulting frame is the real driver function
  `ena_com_get_hash_key()`, compiled from `amzn-drivers` and called with a
  page-granular DMA buffer (as `dma_alloc_coherent` would give) whose
  `key_parts` the device set to 0x40.  Nothing else is simulated.
* **F2 and F6** drive the real kernel APIs the driver calls
  (`skb_add_rx_frag`, `build_skb` + `skb_put`) with the driver's own bound
  (`descs <= ENA_PKT_MAX_BUFS`) and the CBMC witness length, but the loop
  around them is the harness, not `ena_fill_rx_frags()` itself: that
  function needs a live `ena_ring`.
* **F1, F4, F5** replicate the driver's indexing against real kernel
  structures and allocators (`struct pci_dev` + `pci_resource_start()`,
  `kvzalloc(max_wqes)`, `kvzalloc(sub_cqs_per_cq)`), because the EFA code
  paths need an `ib_device` and real hardware to reach.  The KASAN frame is
  therefore the harness, not the driver.  F4's report says
  `slab-use-after-free` rather than `slab-out-of-bounds` only because
  `wrid[4096]` lands far enough away to hit an unrelated object.

One incidental finding: with this `defconfig`, `PCI_NUM_RESOURCES` is 11
rather than the 17 assumed in the F1 CBMC slice (the IOV and bridge
resource slots are config-dependent).  The out-of-bounds index 32 is past
either bound, so the conclusion is unchanged.

### ASAN, real DPDK build (F3)

The shared `ena_com` code is also reachable from the DPDK PMD, so F3 can be
confirmed in userspace without a kernel at all.  DPDK v24.11 with the
`amzn-drivers` `userspace/dpdk/ena` tree overlaid on `drivers/net/ena`,
built `-Db_sanitize=address`, and the harness linked directly against the
PMD's own `ena_com.c.o`
(`scan/amzn-drivers/dynamic/f3_dpdk_asan_build.sh`).

* heap destination (the Linux ethtool `rxfh->key` shape, 40 bytes):
  `AddressSanitizer: heap-buffer-overflow ... WRITE of size 256`, frame
  `#1 ena_com_get_hash_key`.
* stack destination (the DPDK `ena_get_rss_hash_key` and FreeBSD
  `ena_rss_get_hash_key` shape, `uint8_t hw_key[40]`):
  `AddressSanitizer: stack-buffer-overflow ... WRITE of size 256`, same
  frame.  This is the FreeBSD kernel-stack case.

Note for reproduction: ASAN reserves a large virtual shadow mapping, so
bound the run with a cgroup (`systemd-run -p MemoryMax=`), not `ulimit -v`.

### Not dynamically confirmed

* **F7** (`BIT(64)`): undefined shift, no sanitizer run; UBSAN would be the
  oracle.
* **On real hardware / an emulated ENA or EFA device:** not attempted.
  QEMU has no ENA or EFA device model, and writing one is the natural next
  step if an end-to-end PoC is wanted.  The gap this leaves is *reachability
  of the device value*, not the memory-safety consequence: each test injects
  the value at the point where the driver reads it from the device.

## False positives (from the finders and the device query)

* ENA `irq_idx` into `irq_tbl[129]` (7 hits) and the many
  `num_io_queues` / `max_num_io_queues` loops: clamped to
  `ENA_MAX_NUM_IO_QUEUES` (128) in `ena_calc_max_io_queue_num`; ethtool
  rejects larger counts.
* ENA RSS `tbl_log_size` shifts/loops: validated
  `<= ENA_MAX_INDIR_TABLE_LOG_SIZE` at allocation; all users size from
  the same value.
* ENA flow-steering `tbl_size`, EFA `sub_cqs_per_cq` loops: arrays are
  allocated with the same count.
* ENA RX/TX `req_id`: checked in `ena_com_cdesc_rx_pkt_get` and
  `validate_tx_req_id` before use.  Admin `cmd_id` and AENQ `group`:
  guarded in both drivers.  EFA EQ consumer index: masked by `depth - 1`.
* EFA `qp_state` into `srd_qp_state_table`: the query's field-level
  association over-approximates; the value comes from uverbs, which
  validates it.
* EFA `num_pages` into `inline_array[1]`: guarded by `<= 1`.
* ENA `hwtstamp_config` infoleak: `copy_from_user` fills the whole struct.
* MMIO timeout loops: device can only delay, bounded by the timeout.

## FreeBSD and DPDK ENA

Source comparison only.

* **RX `req_id`:** both ports check `req_id >= q_depth` in
  `ena_com_rx_pkt` (not in `ena_com_cdesc_rx_pkt_get` as Linux does);
  `rx_buffer_info` is sized to the ring.  Safe.
* **F2:** not applicable; both build mbuf chains, no fixed frag array.
* **F3, FreeBSD: present and worse.** The RSS sysctl handler
  (`ena_sysctl.c:1076`) calls `ena_com_get_hash_function()` and then
  `ena_rss_get_hash_key()`, which copies into `u8 hw_key[40]` on the
  kernel stack.  A device-written `key_parts` > 10 overflows the stack.
* **F3, DPDK: not reachable in practice.** `ena_get_rss_hash_key()` never
  calls `ena_com_get_hash_function()`, so `key_parts` keeps the
  driver-set value 10 unless the device writes the coherent buffer
  unprompted.  The `ena_com` code is the same, so the fix should go in
  all three copies.
* **F6:** both ports set the mbuf length to the device length without
  comparing it with the buffer (`m_len = len` in FreeBSD;
  `data_len = len` in DPDK).  In DPDK this corrupts process memory, not
  the kernel.

## Reproducing

CBMC slices: `integration/linux/scan/amzn-drivers/cbmc/`, run with
`run.sh` (each reports `VERIFICATION FAILED`).

Queries: build DBs as above, then for each DB run the 8 asset queries and
`device_tainted_sink.ql` from `scan/abc-refinement` with an empty
`scopePrefix` file.

## Next steps

1. Report F1-F3 and F6 (also in mainline) to the ENA/EFA maintainers
   through Amazon's internal security channel before any public patch;
   F4, F5, F7 and FreeBSD F3 go to the amzn-drivers maintainers.
2. An emulated ENA/EFA device in QEMU would turn the injected values into
   an end-to-end PoC.
3. UBSAN run for F7.

## F8/F9: device-supplied BAR offsets used unvalidated (DPDK ENA)

Found while writing the base case for `ena_queue_start`, not by the taint
pipeline: proving the queue-start post-condition requires an assumption about
device-supplied data, and that assumption turned out to mark an unvalidated
trust boundary.

`ena_com_create_io_sq()` derives the doorbell register pointer from the admin
completion descriptor the **device** writes (`drivers/net/ena/base/ena_com.c`):

```c
io_sq->db_addr = (u32 __iomem *)((uintptr_t)ena_dev->reg_bar +
	(uintptr_t)cmd_completion.sq_doorbell_offset);   /* F8 */

if (io_sq->mem_queue_type == ENA_ADMIN_PLACEMENT_POLICY_DEV)
	io_sq->desc_addr.pbuf_dev_addr =
		(u8 __iomem *)((uintptr_t)ena_dev->mem_bar +
		cmd_completion.llq_descriptors_offset);          /* F9 */
```

`sq_doorbell_offset` and `llq_descriptors_offset` appear **nowhere else in the
tree** -- confirmed by grep over `drivers/net/ena/`: their only occurrences are
these two points of use. Neither is checked against the size of the BAR it is
added to.

Consequences. `db_addr` is written on every TX burst
(`ena_com_write_sq_doorbell`), so F8 gives a device-controlled write target at
an arbitrary offset from the register BAR. F9 is worse in kind: the LLQ push
path `memcpy`s descriptor and header bytes to `pbuf_dev_addr`, so a
device-chosen offset relocates a bulk copy. Both are the same class as F1
(EFA `db_bar_idx` feeding `pci_resource_start`), which we already confirmed
dynamically, and the same class the device census was built to find.

This is why the base-case harness carries an explicit
`ASSUME_DEVICE_HONEST_DOORBELL` switch: without that assumption `db_addr` is
an arbitrary pointer and the invariant is unprovable, which is the proof
restating the finding. The assumption is named and compiled in rather than
hidden so that the gap is visible in the harness itself.

Mitigation is cheap and local: bound each offset by the mapped length of the
corresponding BAR before forming the pointer.

### F8/F9 are in shared code and affect all three ENA drivers

`ena_com.c` is the shared base layer, vendored into each driver, so this is
one defect in one file that ships three times, not three findings:

| variant | path | lines |
|---|---|---|
| DPDK ENA PMD | `drivers/net/ena/base/ena_com.c` | 1317, 1322 |
| Linux ENA | `kernel/linux/common/ena_com/ena_com.c` | 1358, 1363 |
| FreeBSD ENA | `kernel/fbsd/ena/ena-com/ena_com.c` | 1359, 1364 |

Normalising whitespace, the FreeBSD and DPDK copies of `ena_com.c` differ by
289 of about 3500 lines, roughly 8%, and the differences are dominated by a
macro refactor (`ENA_FIELD_PREP` replacing open-coded mask-and-shift). None of
the differences touch the doorbell derivation, the req_id handling, `q_depth`
or `next_to_comp`.

Two consequences. For reporting: one fix to the shared layer covers all three
drivers, which makes it a much better-value fix than its per-driver
appearance suggests. For the proof: the ena_com invariants we have already
established (`INV_COM`, `INV_SQ_ACCOUNTING`, the `ena_com_comp_ack` behaviour
in the cleanup proof) are about code that is substantially identical in all
three variants, so that part of the work transfers. What does not transfer is
the OS glue -- `ena_ethdev.c` for DPDK, `ena_netdev.c` for Linux,
`ena.c`/`ena_datapath.c` for FreeBSD -- which is where the FreeBSD findings
we have no coverage for will be.

## F10: NULL dereference releasing a queue whose setup failed (DPDK ENA)

Found by asking the error-path question rather than the invariant question:
*if the operation fails, what does it leave behind?*

`ena_tx_queue_release()` (`ena_ethdev.c:1010`) begins

```c
struct ena_ring *ring = dev->data->tx_queues[qid];

rte_free(ring->push_buf_intermediate_buf);
rte_free(ring->tx_buffer_info);
rte_free(ring->empty_tx_reqs);
```

with no NULL check on `ring`, while `ena_tx_queue_setup()` publishes the ring
into `dev->data->tx_queues[queue_idx]` **only on success**
(`ena_ethdev.c`, immediately before `return 0`). `ena_rx_queue_release()` has
the same shape.

The sequence is ordinary DPDK usage, and every link was checked:

1. `rte_eth_dev_configure(port, nb_rx, nb_tx)` calls
   `eth_dev_tx_queue_config(dev, nb_tx_q)` (`rte_ethdev.c:1550`), which sets
   `dev->data->nb_tx_queues` and allocates a **zeroed** `tx_queues` array.
2. `rte_eth_dev_tx_queue_setup(port, 0, ...)` reaches ENA's setup, an
   allocation fails, it returns `-ENOMEM`, and `tx_queues[0]` stays NULL.
3. The application, having seen the error, calls `rte_eth_dev_close(port)`.
   `rte_eth_dev_close` (`rte_ethdev.c:1940`) calls
   `(*dev->dev_ops->dev_close)(dev)` directly, with no per-queue NULL
   handling.
4. ENA's close calls `ena_rx_queue_release_all(dev)` and
   `ena_tx_queue_release_all(dev)` (`ena_ethdev.c:920-921`), which loop
   `i < dev->data->nb_tx_queues` -- still 1 -- and call the release above.
5. `ring` is NULL and `ring->push_buf_intermediate_buf` dereferences it.

Confirmed by proof, not by reading alone. `harness_setup_errorpath.c` injects
the allocation failures that are already modelled in the setup base case and
then performs step 4. With the release suppressed, VERIFICATION SUCCESSFUL,
0 of 7402, and all three frame obligations hold. With the release performed,
45 properties fail, every one of them in `ena_tx_queue_release`, including

```
dereference failure: pointer NULL in ring->push_buf_intermediate_buf: FAILURE
dereference failure: pointer NULL in ring->$anon1: FAILURE   (empty_tx_reqs)
dereference failure: pointer NULL in ring->$anon0: FAILURE   (tx_buffer_info)
```

Severity is a NULL-page dereference during error recovery, so denial of
service rather than memory corruption, and it needs an allocation failure to
trigger. The fix is a two-line NULL check in each of the two release
functions, or publishing the ring before the allocations and clearing the slot
on failure.

### What the same harness proves about the failure paths that are correct

Three obligations hold, which is worth stating because the interesting part of
an error path is usually what it *does* clean up:

| obligation | result |
|---|---|
| `ERR_NO_PUBLISH` -- a failed setup does not publish the ring | SUCCESS |
| `ERR_NOT_CONFIGURED` -- a failed setup does not mark the ring configured | SUCCESS |
| `ERR_NO_LEAK` -- a failed setup releases everything it allocated (ghost allocation counter, so this is Unveil class D for this function) | SUCCESS |

So `ena_tx_queue_setup` unwinds its own allocations correctly on every failure
path; the defect is entirely in what the *release* function assumes.

## F11: device-controlled out-of-bounds read in the FreeBSD TX cleanup path

FreeBSD only, confirmed by proof with a concrete counterexample. The DPDK
driver does **not** have this: its `validate_tx_req_id` range-checks before
indexing, and its cleanup loop breaks on error before dereferencing.

Three pieces have to be read together.

`ena_com_tx_comp_req_id_get` (shared, `ena-com/ena_eth_com.h`) writes the
device's value into `*req_id` **before** rejecting it:

```c
*req_id = READ_ONCE16(cdesc->req_id);
if (unlikely(*req_id >= io_cq->q_depth)) {
        ena_trc_err(..., "Invalid req id %d\n", cdesc->req_id);
        return ENA_COM_INVAL;            /* *req_id already holds the bad value */
}
```

FreeBSD's `validate_tx_req_id` (`ena.c:573`) does **no range check at all** --
it only inspects the return code and triggers a device reset:

```c
if (unlikely(tx_req_id_rc != 0)) { ... ena_trigger_reset(...); return (EFAULT); }
return (0);
```

and `ena_get_tx_req_id` (`ena_datapath.c:211`) then dereferences regardless of
what validation said:

```c
int rc = ena_com_tx_comp_req_id_get(io_cq, req_id);
if (unlikely(rc == ENA_COM_TRY_AGAIN))
        return (EAGAIN);
rc = validate_tx_req_id(tx_ring, *req_id, rc);
if (unlikely(tx_ring->tx_buffer_info[*req_id].mbuf == NULL)) {   /* line 222 */
```

So when the device reports a `req_id` outside the ring, the driver reads
`tx_buffer_info[req_id]` anyway.

**Proven, with a counterexample.** `harness_fbsd_cleanup.c` makes the
completion-descriptor ring nondeterministic -- it is device-written memory --
and lets the real `ena_com` code run on it. At ring 4, `--unwind 6`,
`--sat-solver cadical`:

```
[ena_get_tx_req_id.pointer_dereference.29] dereference failure:
  pointer outside object bounds in (tx_buffer_info + *req_id)->mbuf: FAILURE
```

with the trace giving `req_id = 65535`, i.e. a read about
`65535 * sizeof(struct ena_tx_buffer)` bytes past a four-entry array. No
unwinding assertion failed in that run, so the bound was adequate. At
`--unwind 2` the same property reports SUCCESS because the path is not
reached -- a reminder that a passing bounded property means nothing until the
unwinding assertions pass.

Impact is an out-of-bounds **read** at a device-chosen offset, up to 64 Ki
slots past the array: a panic on an unmapped page, and otherwise a branch on
out-of-bounds kernel memory. It is not a write. A device reset is triggered on
this path anyway, so the fix costs nothing: check the range before
dereferencing, or return early when `validate_tx_req_id` fails.

`ena_netmap.c:816` calls the same validator and deserves the same review.

### Not established in the same run

The run also reported 30 failures, and four obligations in the harness --
`FREE_ONCE`, `DMA_UNMAPPED_BEFORE_FREE` (no mbuf released while its DMA
mapping is still loaded, which is Unveil class A), `INV_NTC` and
`OWN_NO_DANGLING` -- report SUCCESS. **Those SUCCESSes are not claimed.** By
our own issue-C rule a verdict from a run containing failures is degraded, and
the failures include bodyless `device_printf`, `__mtx_lock_flags` and
`__mtx_unlock_flags`, which cut paths. Two further failures are the
`descs_per_entry` division by zero in `ena_com_sq_have_enough_space`, which is
the `INV_DEV_LLQ` clause the DPDK work already identified and which this
harness does not yet assume. Making those obligations stand needs stubs for
the three kernel functions and the `INV_DEV_LLQ` assumption, then a re-run.

**Update: the stubs and the assumption were added, and the obligations are
still not established.** Two things happened that are worth recording.

First, a near miss. With the stubs and `INV_DEV_LLQ` in place the run reported
VERIFICATION SUCCESSFUL, 0 of 17773, with all four obligations SUCCESS -- and
it was **vacuous**. `ena_dev` is a zero-initialised static, so
`descs_per_entry` is 0 and `__CPROVER_assume(descs_per_entry >= 1)` is a
contradiction that silently killed every path. A `__CPROVER_assert(0, ...)`
witness passed, which is what caught it; F11's own property had quietly
flipped from FAILURE to SUCCESS, which was the visible symptom. Hoisting the
havoc fixed it, and the harness now carries a permanent switchable vacuity
witness, which FAILS as it must. The second trap was the same one already
documented for `make_wf_mbuf`: `__CPROVER_havoc_object` havocs the **whole
enclosing object**, so havocking `llq_info` after writing the CQ pointers
wiped them.

Second, the cost is structural. FreeBSD's cleanup loop is bounded by
`ENA_TX_BUDGET` (128) rather than by the ring size, so a sound
`--unwinding-assertions` run needs `--unwind 129`; `--unwind 6` with full
checks exceeds 2400 s. Loop contracts are the right answer and the contract is
written, but `--dfcc` cannot be applied to FreeBSD kernel code at all because
the CPROVER library's `malloc`/`free` collide with the kernel's three- and
two-argument versions -- see `proof/fbsd/README-fbsd-build.md`. So these four
obligations stand as written-and-reachable, not proven. F11 is unaffected: it
is a FAILURE with a concrete trace, which path cutting elsewhere cannot
manufacture.

## F12: a failed `ena_com_prepare_tx` leaves the packet in its slot, and the reset frees it (DPDK ENA)

Found by the composition check of the induction, not by any single proof: the
cleanup step assumes `INV_DESC_SUM`, so the xmit step was made to assert it,
and it failed.

`ena_xmit_mbuf` (`ena_ethdev.c`) records ownership before it knows the
packet will be accepted:

```c
req_id = tx_ring->empty_tx_reqs[next_to_use];
tx_info = &tx_ring->tx_buffer_info[req_id];
...
ena_tx_map_mbuf(tx_ring, tx_info, mbuf, ...);   /* line 3065: tx_info->mbuf = mbuf */
...
rc = ena_com_prepare_tx(tx_ring->ena_com_io_sq, &ena_tx_ctx, &nb_hw_desc);
if (unlikely(rc)) {
        ++tx_ring->tx_stats.prepare_ctx_err;
        ena_trigger_reset(tx_ring->adapter, ENA_REGS_RESET_DRIVER_INVALID_STATE);
        return rc;                               /* tx_info->mbuf still set */
}
tx_info->tx_descs = nb_hw_desc;
```

On that error path `eth_ena_xmit_pkts` breaks out of its loop and returns a
count that **excludes** the packet, so by the `rte_eth_tx_burst` contract the
caller still owns it -- but `tx_info->mbuf` still points at it, with a stale
`tx_descs`, and `next_to_use` has not advanced. The error path also requests a
device reset. The reset path is
`ena_dev_reset -> eth_ena_dev_uninit -> ena_close -> ena_stop ->
ena_queue_stop_all -> ena_queue_stop -> ena_tx_queue_release_bufs`, and
`ena_tx_queue_release_bufs` calls `rte_pktmbuf_free` on every slot whose `mbuf`
is non-NULL. It therefore frees a packet the application was told was not
sent. If the application then frees it, retransmits it, or reuses it, that is
a double free or a use-after-free -- Unveil class B, triggered on an error path,
which is the dominant shape in their findings.

The slot is overwritten by the next successful transmit on that queue, since
`next_to_use` did not move, which is why this never shows up in normal
operation. The window is exactly the case that matters: the error requests a
reset, and the reset runs before any further transmit.

**Proven, three ways** (`harness_tx_callees.c`, ring 4, one packet, one
segment, `--max-field-sensitivity-array-size 256`):

| build | result |
|---|---|
| plain | `INV_SLOT` and `INV_DESC_SUM` FAIL; trace: `ena_com_prepare_tx` returns 1, `sent = 0`, slot `req_id` holds the packet with a stale `tx_descs` |
| `-DRELEASE_AFTER_XMIT` (calls `ena_tx_queue_release_bufs` after the burst, with `rte_pktmbuf_free` instrumented) | `RELEASE_SAFE: queue release never frees a packet reported as unsent`: FAILURE, with `sent = 0` and the freed mbuf equal to the unsent packet |
| `-DASSUME_PREPARE_TX_OK` | all seven xmit obligations SUCCESS, including `INV_SLOT` and `INV_DESC_SUM` |

**Not established:** which inputs make the real `ena_com_prepare_tx` fail in
practice. Its error returns are a header longer than `tx_max_header_size`, a
missing push header in LLQ mode, a header that does not fit the bounce-buffer
entry, and failures creating the meta descriptor; the space check it repeats
is already passed by `ena_xmit_mbuf`'s own check. Whether an application can
reach any of them with a legal packet has not been checked. The harness models
`ena_com_prepare_tx` as able to fail for any reason.

Fix: clear `tx_info->mbuf` on the error path, or set it only after
`ena_com_prepare_tx` succeeds.

Why the earlier xmit proof did not see it: its slot invariant bounded
`num_of_bufs` and the identity of `mbuf` but not `tx_descs`, and nothing in
it related the SQ tail to the slots. Every obligation it stated was true. The
invariant was just too weak to compose with cleanup, and nothing checked the
composition until now.

### Both sibling drivers already have the fix (found while starting the FreeBSD TX side)

The fix F12 asks for is not hypothetical: Linux and FreeBSD both do it, on the
same error path, and DPDK is the only one of the three that does not.

| driver | after `ena_com_prepare_tx` fails | clears the slot? |
|---|---|---|
| DPDK `ena_xmit_mbuf` (`ena_ethdev.c:3178-3183`) | bumps `prepare_ctx_err`, calls `ena_trigger_reset`, `return rc` | **no** |
| Linux `ena_xmit_tx_buff` (`ena_netdev.c:2652` -> `error_unmap_dma:` at `:2697`) | `ena_unmap_tx_buff(...); tx_info->skb = NULL;` | yes (`:2699`) |
| FreeBSD `ena_xmit_mbuf` (`ena_datapath.c:1044` -> `dma_error:` at `:1096`) | `tx_info->mbuf = NULL; bus_dmamap_unload(...)` | yes (`:1097`) |

So the one-line change F12 proposes for DPDK is exactly what the other two
copies of this driver already do. That also explains why F12 shows up only in
the DPDK proof: in Linux and FreeBSD the slot is released before the reset
path can reach it.

## F13: DPDK stores the DMA-width error code as the DMA width (DPDK ENA)

Found while verifying the queue-start callee models against the real
`ena_com` code (`harness_ena_com_create.c`).

`eth_ena_dev_init` (`ena_ethdev.c:1892`) does

```c
ena_dev->dma_addr_bits = ena_com_get_dma_width(ena_dev);
```

but `ena_com_get_dma_width` returns a **negative error code** --
`ENA_COM_TIMER_EXPIRED` (`-ETIME`) when the CAPS register read times out, or
`ENA_COM_INVAL` (`-EINVAL`) when the device reports a width outside 32..48 --
and only stores the width itself on success. DPDK assigns the return value
unconditionally, so on either failure `dma_addr_bits` (a `u32`) holds
4294967274 or 4294967234. Every later `ena_com_mem_addr_set` evaluates
`GENMASK_ULL(ena_dev->dma_addr_bits - 1, 0)`, which is then a shift by more
than the width of the type: undefined behaviour, reported by CBMC as

```
[ena_com_mem_addr_set.undefined-shift.1] shift distance too large in
  ~0ull >> (8 * 8 - 1) - (ena_dev->dma_addr_bits - 1): FAILURE
```

for both error values. On x86 the shift count is masked in hardware, so in
practice the DMA-address width check uses a garbage mask: it may reject
valid addresses, or accept addresses wider than the device supports. The value
comes from a device register, so a device that reports an illegal width, or
an MMIO read that times out, triggers it.

The Linux and FreeBSD drivers check:

```c
dma_width = ena_com_get_dma_width(ena_dev);
if (unlikely(dma_width < 0)) { ...; goto err_mmio_read_less; }
```

(`ena_netdev.c:4346`, `ena.c:3038`). DPDK has no such check. Low severity,
cheap fix: the same check DPDK's two siblings already have.

## F14: the TX-burst check under-counts a fully pushed packet (shared ena_com; DPDK, Linux, FreeBSD)

*This section replaces the version committed in `00d204a8b38`. That version
gave the mechanism without having read the code, and said the failure needed
a small `max_tx_burst_size`; neither held up.*

When the device advertises `ENA_ADMIN_LIMIT_TX_BURST`, each LLQ line written
to the device spends one unit of `entries_in_tx_burst_left`, and
`ena_com_write_bounce_buffer_to_dev` returns `ENA_COM_NO_SPACE` (Linux:
`-ENOSPC`) when none is left. All three drivers follow the same per-packet
protocol: call `ena_com_is_doorbell_needed(sq, ctx)`, ring the doorbell if it
returns true (which resets the budget to `max_entries_in_tx_burst`), and then
call `ena_com_prepare_tx`. That protocol is safe only if
`ena_com_is_doorbell_needed` predicts the lines `ena_com_prepare_tx` will
write.

It does not. The estimate starts from `num_descs = ctx->num_bufs`, plus one
for a meta descriptor. But `ena_com_prepare_tx` always writes one data
descriptor (`ena_eth_com.c:520`), even when `num_bufs == 0`. That happens when
the whole packet fits in the pushed header: `ena_tx_map_mbuf` pushes
`min(pkt_len, tx_max_header_size)` bytes and creates no DMA buffer for a
packet that short. The data descriptor is the same one that carries the first
buffer when `num_bufs >= 1`, so only the `num_bufs == 0` case is
under-counted, by one descriptor.

That descriptor needs a second line only when `descs_num_before_header == 1`
and the packet also has a meta descriptor. The meta descriptor then fills the
header line (`get_sq_desc_llq` decrements `descs_left_in_line` to 0, and
`ena_com_sq_update_llq_tail` writes the line out). The data descriptor goes on
a new line, which `ena_com_close_bounce_buffer` writes. That is two lines,
against an estimate of one. If exactly one unit of budget is left when such a
packet arrives, the check doesn't ring the doorbell and the second line write
fails.

**Ingredients.** Each of these is needed, and none is a value the driver
validates:
- The device advertises `LIMIT_TX_BURST`. Any `max_tx_burst_size` large
  enough to hold one packet can still fail: the counterexample uses a budget
  of 5 lines, spent by two 3-buffer packets before a pushed-only one.
- `descs_num_before_header == 1`. DPDK and Linux request 2, but
  `ena_com_config_llq_info` (`ena_com.c:742`) falls back to 1 when the device
  doesn't support 2. This is also why `tx_max_header_size` is 240
  (256 - 1 * 16) in the earlier trace. I had expected 224, which assumed the
  requested value of 2.
- The packet has a meta descriptor, either because the device set
  `DISABLE_META_CACHING` or because an offload changed the meta.
- The packet is no longer than `tx_max_header_size`: 240 bytes for 256-byte
  entries, 112 for 128-byte entries. Packets that small are common.

**Evidence.** `proof/harness_doorbell_budget.c` with
`proof/run_doorbell_budget.sh` runs the real `ena_eth_com.c` protocol on a
sequence of 3 packets of up to 3 buffers. It starts from the SQ state
`ena_com_init_io_sq` leaves, with the LLQ geometry, meta caching and burst
budget (1..64) all chosen by the device. The budget is assumed to cover one
maximal packet. The checked property is that `ena_com_prepare_tx` never
returns `ENA_COM_NO_SPACE`.

| configuration | result |
|---|---|
| real code, no offloads | FAILED |
| real code, offloads | FAILED |
| `descs_num_before_header >= 2` only | SUCCESSFUL |
| meta caching on, no offloads | SUCCESSFUL |
| meta caching on, offloads | FAILED |
| `num_bufs >= 1` only | SUCCESSFUL |
| estimate counts `max(num_bufs, 1)` | SUCCESSFUL; both vacuity witnesses FAIL |

Every run has no other failing property and no failing unwinding assertion.
The result is bounded at 3 packets, 3 buffers and a budget of at most 64; it
is not an unbounded proof of the fix.

**Consequence.**
- **DPDK:** this is the F12 trigger. The slot keeps an mbuf that the
  application was told was not sent, and the reset frees it.
- **Linux** (`ena_netdev.c:135-147`) and **FreeBSD** (`ena_datapath.c:1033`):
  every error other than `ENOMEM` resets the device, so F14 is a reset
  triggered by a short packet.
- **Same code in all three:** `ena_com_is_doorbell_needed` is identical in
  the DPDK, Linux and FreeBSD copies. `ena_com_close_bounce_buffer` and
  `ena_com_sq_update_llq_tail` are identical apart from Linux returning 0
  where the others return `ENA_COM_OK`.

**Fix.** In `ena_com_is_doorbell_needed`, start from
`num_descs = max(num_bufs, 1)`. This is the variant the table shows to be
safe within the bounds above.

**Not established:**
- whether any shipping ENA device advertises `LIMIT_TX_BURST` without
  supporting two descriptors before the header;
- the run I made earlier with "no limit, or `max_tx_burst_size >= 4096`" is
  safe only because a budget of at least 4096 / 256 = 16 lines can't run out
  within the one-packet harness. It is **not** a sound device precondition
  for longer bursts. The precondition the induction may state instead is
  "`LIMIT_TX_BURST` absent, or `descs_num_before_header >= 2`".

## F15: a zero-length mbuf is accepted with no descriptor, and enough of them overwrite in-flight packets (DPDK ENA)

Found while checking the xmit step's `ena_com_prepare_tx` stub against the
real code (`harness_prepare_tx.c`, `PTX_DESCS_MIN`). The stub had claimed at
least one descriptor per packet.

Consider a single-segment mbuf with `pkt_len == 0`. `ena_tx_map_mbuf` pushes
`min(0, tx_max_header_size) = 0` bytes and creates no DMA buffer, so
`header_len == 0` and `num_bufs == 0`. When meta caching is on and no meta
descriptor is due, `ena_com_prepare_tx` takes its "caller doesn't want to send
packets" exit (`ena_eth_com.c:511`) and returns 0 with `nb_hw_desc == 0`.
`ena_xmit_mbuf` then counts the packet as sent:
- it records the mbuf in the slot with `tx_descs = 0`;
- it advances `next_to_use`.

Nothing reaches the device, so no completion ever returns that slot.

The SQ space check (`ena_com_sq_have_enough_space`) counts descriptors, and
these packets use none. So it never stops them, and `ring_size` of them in
flight fill every slot. At ring 4 with a burst of 2 (`ALLOW_EMPTY_IN_FLIGHT`),
the xmit step fails `OWN_NO_LOSS` from this pre-state:
- three empty packets in flight;
- slot 0 free, `next_to_use = 0`.

The first new packet takes slot 0. The second takes
`empty_tx_reqs[1] = 2`, and slot 2 still holds an in-flight mbuf. It is
overwritten (`RTE_ASSERT(tx_info->mbuf == NULL)` is compiled out in release
builds), and the old mbuf is leaked. `INV_FREE_LIST` and `OWN_TRANSFER` fail
in the same run. With a burst of 1 the step fails `INV_FREE_LIST`: the ring
ends with 4 occupied slots and an apparent in-flight count of 0.

Recovery: each stuck slot has a nonzero `timestamp`, so
`check_for_tx_completion_in_queue` eventually counts it as a missed completion
and resets the device. The reset frees what is still in the slots, but an
overwritten mbuf is already lost. The overrun needs only `ring_size` sends,
which is far faster than the watchdog.

Ingredients:
- the application passes a zero-length mbuf;
- meta caching is on (the default) and no offload meta is due.

Neither `eth_ena_prep_pkts` nor `eth_ena_xmit_pkts` checks `pkt_len`. This is
an application error the driver turns into a leak, a stuck-slot reset and
corruption of its own slot state, so it belongs with the driver-contract
items. It is more serious than those because the damage outlives the bad
packet. The fix is to reject `pkt_len == 0` in `ena_xmit_mbuf`, or in
`eth_ena_prep_pkts`.

Evidence: `proof/run_xmit_variant.sh` with `-DALLOW_EMPTY_IN_FLIGHT`, at ring 4,
burst 1 and burst 2. The induction states the application obligation as
`ASSUME_NONEMPTY_PKT`, and with it the xmit step proves `OWN_NO_LOSS`,
`INV_FREE_LIST` and the rest. The Linux and FreeBSD paths compute the same
`header_len == 0, num_bufs == 0` context for an empty packet. Whether a
zero-length skb or mbuf can reach their xmit routines has not been assessed.

**A second stub error found by the same check.** `nb_hw_desc` is
`io_sq->tail - start_tail` on two `u16`s, computed in `int`. So it is negative
(`lines - 65536`) whenever the tail wraps. It is stored in the unsigned
`tx_descs`, summed in cleanup, and passed through `ena_com_comp_ack`'s `u16`
parameter. The driver is therefore correct modulo 2^16 in DPDK, Linux and
FreeBSD. That holds only because every consumer truncates. The invariants now
read the field as `(uint16_t)tx_descs` (`TX_DESCS` in `ena_inv.h`). The
previous "`tx_descs` in 1..19" was false of the real code, and the old stub
hid that.

## F16: DPDK ena_rx_mbuf dereferences a device-chosen req_id's slot without a NULL check (DPDK ENA)

Found starting the RX path. On RX the device, not the driver, chooses which
posted buffer each completion refers to, via the `req_id` field of the
completion descriptor. `ena_com_rx_pkt` validates `req_id < q_depth`
(`ena_eth_com.c:663`, returns `ENA_COM_EIO` otherwise), so the index is in
range. It does **not** check that the slot is actually posted.

`ena_rx_mbuf` (`ena_ethdev.c:2700-2705`) then does:

```c
rx_info = &rx_ring->rx_buffer_info[req_id];
mbuf = rx_info->mbuf;
RTE_ASSERT(mbuf != NULL);
ena_init_rx_mbuf(mbuf, len);   /* writes mbuf->data_len, ->data_off, ... */
```

`RTE_ASSERT` is compiled out in release builds. If the device returns a
completion whose `req_id` names a slot with no posted buffer
(`rx_buffer_info[req_id].mbuf == NULL`) -- which it may, the slot being free is
a normal state -- the driver dereferences NULL in `ena_init_rx_mbuf`. The same
unchecked dereference is in the chain loop (`ena_ethdev.c:2724`,
`RTE_ASSERT(rx_info->mbuf != NULL)` then `mbuf->next = rx_info->mbuf`).

Linux and FreeBSD check and reset instead:
- Linux `ena_rx_skb` (`ena_netdev.c:959`): `if (unlikely(!rx_info->page)) {
  ... ena_reset_device(adapter, ENA_REGS_RESET_INV_RX_REQ_ID); return NULL; }`;
- FreeBSD `ena_rx_mbuf` (`ena_datapath.c:430` and `:474`):
  `if (unlikely(rx_info->mbuf == NULL)) { ... ena_trigger_reset(...); return
  NULL; }`.

So DPDK is missing a check its siblings have. This is the RX analogue of the
device-trust class F1-F9: an in-range but semantically invalid device value
used without validation.

**Evidence.** `proof/harness_rx_reqid.c` calls the real `ena_rx_mbuf` with one
single-buffer completion from an `INV_RX_STARTED` queue, `req_id` constrained
only to `< ring_size` (what `ena_com_rx_pkt` guarantees), length nonzero.
`RX_REQID_POSTED` (`rx_buffer_info[req_id].mbuf != NULL`) FAILS, with a NULL
dereference in `ena_init_rx_mbuf` on the counterexample; the `-DWITNESS` build
confirms a free-slot `req_id` is reachable under the invariant. At ring 4,
`--bounds-check --pointer-check`.

**Fix.** Add the NULL check DPDK is missing, matching Linux and FreeBSD:
reset the device on a `req_id` whose slot is not posted, rather than
dereferencing it. One check covers both triggers, since a repeated `req_id`
presents as an unposted slot on the second reference.

**Severity.** A malfunctioning or malicious device triggers a NULL dereference
in the datapath. It needs a device that returns a completion for an unposted
req_id, which a correct device does not do; it is a device-trust robustness
gap, the same kind this project's Linux findings cover.

## F17: the RX interrupt-enable path dereferences the device queue before the port is started (DPDK ENA)

Found while closing out the RX path.

`ena_rx_queue_intr_enable` and `_disable` both call `ena_rx_queue_intr_set`
(`ena_ethdev.c:3880`), which takes `&adapter->rx_ring[queue_id]` and calls
`ena_com_unmask_intr`. That writes through `io_cq->unmask_reg` after reading
`io_cq->bus` (`ena_eth_com.h:79-83`). Neither function checks that the queue
has been started, and `ena_com_io_cq` together with its `unmask_reg` is filled
in only by `ena_queue_start` -> `ena_create_io_queue` ->
`ena_com_create_io_queue`.

The call order that reaches it is legitimate:
1. `rte_eth_rx_queue_setup` succeeds, and the ethdev layer publishes the queue
   (`ena_ethdev.c:1749` sets `dev->data->rx_queues[queue_idx]`);
2. the application calls `rte_eth_dev_rx_intr_enable` before
   `rte_eth_dev_start`;
3. `eth_dev_validate_rx_queue` (`rte_ethdev.c:767-783`) checks only
   `queue_id < nb_rx_queues` and `rx_queues[queue_id] != NULL`. It does **not**
   check that the device is started;
4. so the PMD op runs on a configured-but-unstarted queue, where
   `ena_com_io_cq` is still the zero the private-data allocation left.

**Evidence.** `proof/harness_rx_errorpath.c` with `-DINTR_ON_UNSTARTED=1`
runs the real `ena_rx_queue_setup`, assumes it succeeded, and then calls
`ena_rx_queue_intr_enable`. Nothing in the harness forces the field; it is
left exactly as a successful setup leaves it. CBMC reports NULL dereferences
of `io_cq->bus` and `io_cq->unmask_reg` in `ena_com_unmask_intr`, ending in a
write through a NULL address in `rte_write32_relaxed`.

**Fix.** Check that the queue is started before touching the device queue --
either a `rx_ring[queue_id].ena_com_io_cq != NULL` test in
`ena_rx_queue_intr_set`, or an adapter-state check against
`ENA_ADAPTER_STATE_RUNNING` as the datapath functions do under
`RTE_ETHDEV_DEBUG_RX`.

**Severity.** An application-triggered NULL write, needing no device
misbehaviour, but it does need an unusual call order (enabling queue
interrupts before starting the port). Unlike F8-F9 and F16 this is not a
device-trust issue; it is the same class as F10, a driver entry point reached
in a state it does not expect.

### The F10 shape on the RX side, now proven rather than read

F10 noted that `ena_rx_queue_release` has the same shape as the TX one. The
same harness (`-DRELEASE_AFTER_FAILURE=1`) confirms it: after a failed
`ena_rx_queue_setup` the ethdev layer never publishes the queue, so
`dev->data->rx_queues[i]` stays NULL, and `ena_rx_queue_release_all` ->
`ena_rx_queue_release` dereferences it at `ena_ethdev.c:992-995` with no NULL
check. `RX_RELEASE_NONNULL` FAILS, and with the release performed CBMC
reports the NULL dereference directly.

## F18: ena_setup_rx_intr returns success after failing and undoing its own setup (DPDK ENA)

Found starting the device-lifecycle phase, by reading `ena_start`'s error
paths and then checking with CBMC.

`ena_setup_rx_intr` (`ena_ethdev.c:3813-3878`) sets `rc` only on its first two
failures. After `rte_intr_efd_enable` succeeds, `rc` is 0, and two later
failures jump into the unwind chain without touching it:

```c
3853   rc = rte_intr_efd_enable(intr_handle, vectors_nb);
3854   if (rc != 0)
3855           goto free_intr_vec;              /* rc set: fine */
3857   if (!rte_intr_allow_others(intr_handle)) {
3860           goto disable_intr_efd;           /* rc still 0 */
3861   }
3863   for (i = 0; i < vectors_nb; ++i)
3864           if (rte_intr_vec_list_index_set(intr_handle, i,
3865                                           RTE_INTR_VEC_RXTX_OFFSET + i))
3866                   goto disable_intr_efd;   /* rc still 0 */
3868   rte_intr_enable(intr_handle);
3869   return 0;
3871 disable_intr_efd:
3872   rte_intr_efd_disable(intr_handle);
3873 free_intr_vec:
3874   rte_intr_vec_list_free(intr_handle);
3875 enable_intr:
3876   rte_intr_enable(intr_handle);
3877   return rc;                                /* 0 on both paths above */
```

So on either path the function disables the event file descriptors, frees the
interrupt vector list, and reports success.

**Consequence.** `ena_start` (`ena_ethdev.c:1319-1321`) treats a zero return as
success, continues, sets `ENA_ADAPTER_STATE_RUNNING` and marks every queue
started. The port comes up with Rx queue interrupts requested by the
application but not configured, so they never fire and nothing reports an
error. That is Unveil class F, a lost event / stuck queue, and it is silent:
the application has no way to learn that the interrupt setup it asked for was
torn down.

**Evidence.** `proof/harness_setup_rx_intr.c` with
`proof/run_setup_rx_intr.sh` runs the real function with the EAL interrupt
calls modelled by ghosts that record what actually happened, and asserts the
contract the caller depends on: a zero return implies the event descriptors
are enabled and the vector list allocated. `F18_SETUP_RC` **FAILS**. All three
vacuity witnesses FAIL, so success, failure and the efd-disabled outcome are
each reachable. No other failing property, no reached bodyless callee, no
failing unwinding assertion.

Both paths were then confirmed separately, since a single counterexample only
exhibits one: `-DONLY_ALLOW_OTHERS_FAILS` (vector-index-set forced to succeed)
and `-DONLY_INDEX_SET_FAILS` (`allow_others` forced to pass) each still fail
the obligation.

**Fix.** Set `rc` on both paths, for example `rc = -ENOTSUP;` before
`goto disable_intr_efd` at line 3860 and `rc = -EINVAL;` (or `-ENOTSUP`) in
the loop at line 3866.

**Not a device-trust issue.** Like F10 and F17 this is driver glue reached in
a state it mishandles; no device misbehaviour is needed, only an EAL call
failing, which is the condition the code is already trying to handle.

**DPDK-specific.** Linux and FreeBSD configure MSI-X through their own kernel
paths (`ena_enable_msix` and `ena_setup_mgmnt_intr`), so there is no
corresponding function to compare against.

## F19: ena_rss_reta_query reads past a stack array and copies it to the application (DPDK ENA)

Found in the config/query phase. The update and query paths of the same file
disagree about validating the same parameter.

`ena_rss_reta_update` checks it (`ena_rss.c:84-89`):

```c
if (reta_size > ENA_RX_RSS_TABLE_SIZE) { ...; return -EINVAL; }
```

`ena_rss_reta_query` does not. It declares a local table, fills it from the
device, and then copies out `reta_size` entries:

```c
132   uint32_t indirect_table[ENA_RX_RSS_TABLE_SIZE];     /* 128 entries */
...
149   rc = ena_mp_indirect_table_get(adapter, indirect_table);
...
156   for (i = 0 ; i < reta_size ; i++) {
157           reta_conf_idx = i / RTE_ETH_RETA_GROUP_SIZE;
158           reta_idx = i % RTE_ETH_RETA_GROUP_SIZE;
159           if (TEST_BIT(reta_conf[reta_conf_idx].mask, reta_idx))
160                   reta_conf[reta_conf_idx].reta[reta_idx] =
161                           ENA_IO_RXQ_IDX_REV(indirect_table[i]);
162   }
```

So with `reta_size > 128` the loop reads off the end of a 128-entry stack
array and writes what it finds into the application's buffer: an
out-of-bounds stack read, and a disclosure of adjacent stack contents to the
caller (Unveil classes G and J).

**The ethdev layer does not bound it either.**
`rte_eth_dev_rss_reta_query` (`rte_ethdev.c:4773`) rejects only a NULL config
and then calls `eth_check_reta_mask` (`:4677`), which merely requires one mask
bit to be set. Neither compares `reta_size` with `dev_info.reta_size`, which
ENA reports as `ENA_RX_RSS_TABLE_SIZE` (`ena_ethdev.c:2646`). So an
application passing a larger size reaches the loop.

**Evidence.** `proof/harness_rss_reta.c` with `proof/run_rss_reta.sh` calls
the real `ena_rss_reta_query` with `reta_size` in 1..512 and a model of
`ena_mp_indirect_table_get` that fills exactly the 128 entries its contract
covers. The single failing property is

```
[ena_rss_reta_query.array_bounds.3] array 'indirect_table' upper bound
    in indirect_table[(signed long int)i]: FAILURE
```

Both vacuity witnesses FAIL: the query can succeed, and an oversized
`reta_size` does reach the driver. With `-DWITH_UPDATE_CHECK`, which adds the
bound the update path already has, the run is VERIFICATION SUCCESSFUL.

**Fix.** Add to `ena_rss_reta_query` the check `ena_rss_reta_update` already
performs. One line, and it is already written three functions earlier in the
same file.

**Severity.** Reachable by an application that passes a `reta_size` larger
than the advertised `reta_size`, which the API says not to do but nothing
enforces. No device misbehaviour is needed. It leaks stack bytes to the
caller, which is worse than the NULL dereferences of F10 and F17 in kind,
though it needs the same sort of API misuse. Same class as those: a driver
entry point reached with an argument it does not validate.

## F20: the DMA rings are freed even when the device never acknowledged the destroy (shared ena_com; DPDK, Linux, FreeBSD)

Found in the DMA-ownership phase (Unveil class A).

`ena_com_destroy_io_queue` tears an IO queue down in three steps
(`ena_com.c:2237-2240`, identical in all three drivers):

```c
ena_com_destroy_io_sq(ena_dev, io_sq);          /* admin command */
ena_com_destroy_io_cq(ena_dev, io_cq);          /* admin command */
ena_com_io_queue_free(ena_dev, io_sq, io_cq);   /* frees the DMA rings */
```

The ordering is right: tell the device first, free afterwards. But **both
return values are discarded, and the free is unconditional.** Each destroy is
an admin command that can fail or time out -- `ena_com_destroy_io_sq` returns
`ENA_COM_TIMER_EXPIRED` when the device does not complete it. On that path the
driver has no evidence the device stopped using the SQ and CQ rings, and frees
them anyway. The memory is then returned to the allocator while the device may
still be writing descriptors into it.

This is in the shared base layer, so one fix covers DPDK, Linux and FreeBSD.

**Evidence.** `proof/harness_dma_order.c` with `proof/run_dma_order.sh` runs
the real `ena_com_destroy_io_queue` with the platform memory macros replaced by
ones that maintain a ghost "the device may still access this region" set,
cleared only when a destroy command succeeds.

| configuration | result |
|---|---|
| both destroy commands succeed | SUCCESSFUL: `DMA_NO_FREE_WHILE_LIVE`, `DMA_ALL_RELEASED`, `DMA_NO_DOUBLE_FREE` |
| either may fail (`-DADMIN_FAILS`) | **`DMA_NO_FREE_WHILE_LIVE` FAILS**, and `DMA_ALL_RELEASED` fails with it |

The vacuity witness (the teardown really frees both rings) FAILS in both. No
other failing property, no reached bodyless callee, no UNKNOWN.

**Severity, stated carefully.** How bad this is depends on what a device does
after failing to acknowledge a destroy, which this work cannot determine. The
alternative to freeing -- leaking the rings -- is also unattractive, so the
usual remedy is not to skip the free but to escalate first: perform a device
reset, which stops all DMA at the hardware level, before releasing the
memory. DPDK's `ena_stop` does reset the device, but only
`if (adapter->trigger_reset)` and only **after** `ena_queue_stop_all` has
already freed the rings (`ena_ethdev.c:1379-1386`), so the reset does not
protect this window.

**Fix.** Check the return values, and on failure request a device reset before
`ena_com_io_queue_free`, or have the caller do so. At minimum the failure
should be visible to the caller, which it currently is not.

**Relation to the FreeBSD TX result.** `FBX_DMA_ORDER` in
`harness_fbsd_xmit.c` proves the data-path side of the same discipline: an
mbuf is never freed while its DMA mapping is loaded. F20 is the control-path
side, and it does not hold.

## F21: a stop/start cycle leaves the reset flag set and permanently disables the watchdog (DPDK ENA)

Found while answering the question phase 4 left open -- is a reset request
ever consumed? Through `rte_eth_dev_reset` it is, because
`eth_ena_dev_init` does `memset(adapter, 0, sizeof(*adapter))`. Through a
stop/start cycle it is not.

`adapter->trigger_reset` is set by `ena_trigger_reset` (`ena_ethdev.c:648`)
from the watchdog, the data path and the control path. **Nothing in the DPDK
driver ever clears it**; the only place it becomes false again is that
`memset`, reached only through probe or `rte_eth_dev_reset`.

`ena_stop` acts on the flag and leaves it set (`:1382-1386`):

```c
if (adapter->trigger_reset) {
        rc = ena_com_dev_reset(ena_dev, adapter->reset_reason);
        ...
}
```

The device has been reset, so the flag is now stale. But
`rte_eth_dev_stop` followed by `rte_eth_dev_start` is a legal sequence that
does not go through `eth_ena_dev_init`, so on the restarted port the flag is
still true, and every watchdog tick begins (`:2083`):

```c
if (unlikely(adapter->trigger_reset))
        return;
```

So the keep-alive check, the admin-state check and the missing-Tx-completion
check never run again on that port. The last of those is the mechanism F15
relies on for recovery, so after this the driver can no longer detect a stuck
queue; and since `ena_stop` will see the flag again, every later stop
re-issues a device reset.

**Both sibling drivers clear the flag explicitly**, which makes this an
oversight rather than a design choice:

| driver | clears the reset flag |
|---|---|
| DPDK | only via the `memset` in `eth_ena_dev_init` (`:2296`) |
| Linux | `clear_bit(ENA_FLAG_TRIGGER_RESET, &adapter->flags)` (`ena_netdev.c:3292`) |
| FreeBSD | `ENA_FLAG_CLEAR_ATOMIC(ENA_FLAG_TRIGGER_RESET, adapter)` (`ena.c:3617`) |

**Evidence.** `proof/harness_reset_flag.c` with `proof/run_reset_flag.sh`
requests a reset, runs the real `ena_stop`, puts the adapter back into the
state a successful restart leaves, and takes one real watchdog tick. The
obligation is that the tick still performs its checks, observed through a
ghost set by the first check it reaches.

| configuration | F21_WD_RUNS |
|---|---|
| a reset was requested before the stop | **FAILURE** |
| no reset was ever requested (control) | SUCCESS |
| the flag is cleared once `ena_stop` has acted on it (the fix) | SUCCESS |

The control case matters: without it the harness could have been measuring
something other than the stale flag. The vacuity witness (the restarted port
is running) FAILS. No other failing property, no reached bodyless callee, no
UNKNOWN.

**Fix.** Clear `trigger_reset` in `ena_stop` once `ena_com_dev_reset` has been
issued, as Linux and FreeBSD do.

**Severity.** No memory unsafety, but the driver silently loses its own
failure detection for the remaining life of the port, after an event that is
itself a sign of trouble. Unveil classes F and K.

## Observation: the admin submit/complete handshake publishes the slot before the buffer

Recorded as an observation rather than a finding because triggering it needs
both a misbehaving device and a two-instruction race window. The proof
pinpoints the precondition precisely, and the fix is a reordering.

The two sides of the admin queue are not mutually excluded. Submission runs
under `admin_queue->q_lock` (`ena_com.c:313-324`); the completion handler,
on the interrupt or alarm thread, takes **no lock at all**
(`ena_com_handle_admin_completion`, `:489-524`).

Submission publishes the slot in this order (`:253-259`), with
`get_comp_ctxt(..., capture = true)` setting `occupied`:

```c
comp_ctx = get_comp_ctxt(admin_queue, cmd_id, true);   /* occupied = 1 */
comp_ctx->status    = ENA_CMD_SUBMITTED;
comp_ctx->comp_size = comp_size_in_bytes;
comp_ctx->user_cqe  = comp;                            /* published last */
```

and the completion handler gates on `occupied` and then writes through
`user_cqe` (`:476-483`):

```c
if (!comp_ctx->occupied)
        return;
...
if (comp_ctx->user_cqe)
        memcpy(comp_ctx->user_cqe, (void *)cqe, comp_ctx->comp_size);
```

So between the first and last of those stores the slot reads as occupied while
`user_cqe` still points at the **previous** command's response buffer, whose
waiter has already returned. Many callers pass a stack-allocated response
struct, so that pointer can be a dead stack frame.

`proof/harness_conc_admin.c` with `proof/run_conc_admin.sh` interleaves the
submit's four steps with the completion handler and tracks whether a write
lands in a released buffer:

| device model | CC2_NO_STALE_CQE | CC2_NO_LOST |
|---|---|---|
| arbitrary: may complete a command it was never told about | **FAILURE** | SUCCESS |
| well behaved: completes only commands it was told about | SUCCESS | SUCCESS |

Both vacuity witnesses FAIL in each configuration.

**So the handshake is sound exactly under the assumption that the device never
sends a stale or unsolicited completion.** That is the same kind of assumption
F8, F9 and F16 are about, and this project does not grant it elsewhere. The
difference here is that exploiting it also requires hitting a two-instruction
window, which is why this is an observation.

**Fix.** Publish `user_cqe` and `comp_size` before `occupied`, so that a
completion which observes the slot as occupied necessarily sees the matching
buffer. On a weakly-ordered machine that needs a release store for `occupied`
and an acquire load in the handler.

This is in the shared `ena_com` layer, so it reads the same in all three
drivers.

## Observation: the reset-request handshake is an unsynchronised read-modify-write

Not reported as a finding: the consequence is a wrong reset reason, not
memory unsafety. Recorded because it is a real data race and the proof
pinpoints its only observable effect.

`ena_trigger_reset` (`ena_ethdev.c:643-650`) is called from the watchdog, the
data path (`validate_tx_req_id`, `:797`) and the control-path poll handler
(`:1966`), on different threads, with no lock and no barrier:

```c
if (likely(!adapter->trigger_reset)) {
        adapter->reset_reason = reason;
        adapter->trigger_reset = true;
}
```

`ena_stop` reads the pair (`:1382-1386`) as two separate loads:

```c
if (adapter->trigger_reset)
        rc = ena_com_dev_reset(ena_dev, adapter->reset_reason);
```

`proof/harness_conc_reset.c` interleaves two requesters and the reader at
every access to the shared pair, splitting `ena_trigger_reset` into its guard
load and its two stores so the reader can run between them. Both store orders
are modelled explicitly, since nothing in the source forbids the compiler or a
weakly-ordered CPU from swapping two plain stores to different fields.

| store order | CC_RESET_NOT_LOST | CC_REASON_SOUND |
|---|---|---|
| reason then flag, as written | SUCCESS | SUCCESS |
| flag then reason, the permitted reordering | SUCCESS | **FAILURE** |

So **no reset request is ever swallowed** by a concurrent one, in either
order, which is the property that would matter for availability. What can go
wrong is narrower: with the stores reordered, a reader that sees
`trigger_reset` set may read `reset_reason` before it was written and pass the
pre-existing value to `ena_com_dev_reset`. The device is then told the wrong
reason for the reset. That is a diagnostic inaccuracy, not corruption, which
is why this is an observation rather than a finding.

It is more than theoretical on arm64, where store-store reordering is
permitted and where these drivers run on Graviton instances. The fix, if
wanted, is a release store for the flag and an acquire load in the reader, or
simply to read the reason into a local before testing the flag.

An earlier version of this harness executed `ena_trigger_reset` atomically in
one interleaving step. Both obligations then held even in the reordered
variant -- the harness could not express the window it was about. That is the
hazard with hand-interleaved concurrency proofs, and the reason the step
granularity is spelled out in the harness.

## Observation: the FreeBSD header-skip loop indexes segs[] with no nsegs guard

Not a finding: no counterexample exists under the DMA layer's contract. It is
recorded because the proof had to assume the contract explicitly, which makes
the dependency worth stating.

`ena_tx_map_mbuf` (FreeBSD, `ena_datapath.c:931`) skips the bytes already
pushed into the LLQ header:

```c
offset = tx_ring->tx_max_header_size;
while (offset > 0) {
        if (offset >= segs[iseg].ds_len) {
                offset -= segs[iseg].ds_len;
        } else {
                ... ena_buf->len = segs[iseg].ds_len - offset; offset = 0;
        }
        iseg++;
}
```

The loop condition is `offset > 0`; there is no `iseg < nsegs` test. `segs` is
the local `bus_dma_segment_t segs[ENA_BUS_DMA_SEGS]` (32 entries) that
`bus_dmamap_load_mbuf_sg` filled with `nsegs` of them. Termination inside the
filled prefix relies on two properties of `bus_dma`:

- the segment lengths sum to the mapped mbuf's length, and
- the guard above the loop (`ena_datapath.c:921`) has already returned when
  `m_pkthdr.len <= tx_max_header_size`, so the sum strictly exceeds `offset`.

Both hold for a correct `bus_dma`, so the loop is safe. The proof is sensitive
to it: a first version of `harness_fbsd_xmit.c` modelled
`bus_dmamap_load_mbuf_sg` as returning segments with arbitrary nonzero
lengths, and CBMC then read past `segs[]`. The harness now states the
partition property explicitly, which is the right place for it.

A zero-length segment would be worse than an overrun: `offset` would never
decrease and `iseg` would walk the array indefinitely. `bus_dma` does not
produce those.

The DPDK equivalent (`ena_tx_map_mbuf`, `ena_ethdev.c:3108`) walks
`mbuf->next` instead and has no analogous array, so this is FreeBSD-specific.

## #4 result: the device-init preconditions are established, with one caveat

The same harness checks, with no transmit (`-DDEVINIT_ONLY`), what the
induction states as device-init facts but does not prove. At ring 8:

| device-init obligation | result |
|---|---|
| the LLQ geometry of INV_DEV_LLQ (`descs_per_entry` in 1..16, `descs_num_before_header` <= 16) | SUCCESS |
| `sgl_size <= ENA_PKT_MAX_BUFS` | SUCCESS |
| a maximal pushed header fits in one LLQ entry | SUCCESS |

So `ena_com_config_dev_mode` and `ena_calc_io_queue_size` really do establish
them from an arbitrary device response -- these come off the induction's
ledger of bare assumptions and become proven consequences of device init.

**Caveat, and a correction to INV_DEV_LLQ.** The `disable_meta_caching ==
false` clause of INV_DEV_LLQ is *not* established: the device may advertise
`ENA_ADMIN_DISABLE_META_CACHING`, and init passes it straight through to
`llq_info.disable_meta_caching`. So that clause is a genuine restriction of the
induction (it covers meta-caching-enabled queues only), not a device-init
fact, and the harness asserts the LLQ-geometry clauses without it.

`sgl_size` can also be 0 if the device reports `max_per_packet_tx_descs ==
0`. An earlier version of this note called that harmless without checking. On
the TX path, `sgl_size` bounds no array access; the per-slot `bufs` array is
sized by `ENA_PKT_MAX_BUFS`. Its uses are:
- `eth_ena_prep_pkts` rejects every packet (`nb_segs >= 0`);
- `dev_info` reports `nb_seg_max = 0`;
- `eth_ena_xmit_pkts` doesn't consult it.

So applications that skip `rte_eth_tx_prepare` still transmit normally. The
RX analogue, `max_rx_sgl_size == 0`, is different. `ena_com_rx_pkt` returns
`ENA_COM_NO_SPACE` for any completion (`nb_hw_desc > max_bufs`), and
`eth_ena_recv_pkts` turns that into a reset. That is read from the code, not
proven, since the RX path has no harness yet.

