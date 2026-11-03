# amzn-drivers: security pipeline run (Linux ENA/EFA, FreeBSD ENA, DPDK ENA)

**Date:** 2026-11-03
**Target:** https://github.com/amzn/amzn-drivers at `9d5ca40`
("linux/efa: Bump driver version to 3.4.0").
**Status:** No bug reachable from userspace or the network.  Seven places
where the drivers use device-written values without a bound; all have a
CBMC witness on a code slice.  None has been reproduced on hardware or an
emulated device.

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
2. Dynamic confirmation: a QEMU device model or a patched
   firmware-response hook that returns the CBMC witness values, under
   KASAN.
3. Run `device_tainted_sink.ql` across the kernel-wide driver DBs.
