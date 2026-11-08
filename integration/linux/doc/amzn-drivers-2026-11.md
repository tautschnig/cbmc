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
