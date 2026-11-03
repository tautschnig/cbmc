# Kernel-wide device-as-attacker census (device_tainted_sink.ql)

**Date:** 2026-11-03
**Tree:** `/home/ubuntu/linux_6_12` (6.12 LTS, the same broad `.config` as the
other censuses: 9781 modules).
**Scope:** 335 units over 24 DMA-capable driver classes
(`drivers/net/ethernet`, `infiniband`, `nvme`, `scsi`, `net/wireless`,
`vdpa`, `virtio`, `vfio`, `block`, `crypto`, `dma`, `mmc`, `usb`, `ata`,
`net/can`, `net/wwan`, `firewire`, `thunderbolt`, `accel`, `media/pci`,
`media/usb`, `gpu/drm`, `misc`, `char`), heavy classes expanded to depth-1
subdirectories.  Driver: `scan/abc-refinement/device_census.py`.

**Status:** census complete, triage **first-pass only**.  No confirmed bug.
The run produced a ranked candidate list and two concrete precision gaps in
the query.

## Result

| | |
|---|---|
| units attempted | 335 |
| units with results | 333 (1 query timeout, 1 query error) |
| build failures | 0 after a serial retry (3 on the parallel pass) |
| distinct sinks | 58248 |
| priority (unguarded fixed-array index or copy length) | 4967 |

By sink kind: 28227 loop-bound, 14636 index-ptr, 8595 index-fixed,
4975 copy-len, 1815 shift.

Two things about these numbers. First, the build failures were races, not
configuration gaps: several units fail when eight `make` jobs share one
kernel tree, and all three recovered on a serial retry.  `build-codeql-db.sh`
now takes `PERCPU_PREPATCHED=1` so parallel builds no longer revert the
`__seg_gs` workaround under each other, which was the other shared-tree
hazard.  Second, 4967 priority candidates is far too many to be a work
queue; the ranking below is what makes the output usable, and the precision
gaps are what should shrink the number.

## Query refinement this run forced

The version that found the amzn-drivers bugs was unusable kernel-wide: on
`drivers/net/ethernet/atheros` alone it reported 233 sinks.  Two causes,
both fixed in `DeviceTaint.qll`:

* **Kernel headers.** `inDriver` only asked for a tree-relative path, which
  is every file in an in-tree DB.  Every inlined `skbuff.h`, `bpf.h` and
  `rbtree_latch.h` helper became a candidate.  Now `include/`,
  `arch/*/include/` and `*/include/linux/` are excluded.
* **Core kernel structs classified as device-written.** The nesting rule
  walked from a DMA-reachable struct into any member type, which reached
  `net_device`, `sk_buff`, `bpf_map` and `proto` -- hence candidates like
  `net_device.addr_len` and `bpf_map.value_size`.  Device ABI structs are
  declared next to the driver (`ena_admin_defs.h`, `efa_io_defs.h`), so
  nesting is now restricted to structs declared in the same file as the
  outer struct.

Effect on `atheros`: 233 sinks -> 12, priority 2.  Recall on the
amzn-drivers DBs is unchanged (identical row counts, F1-F5 all still
reported), which is the check that matters: the refinement removed noise,
not findings.

## Ranking

`--top N` orders priority candidates by the shapes that turned out to be
real in the amzn-drivers run: a device descriptor/response **field** driving
a **copy length** first (the F3/F6 shape), then a field used as a
**fixed-array index** (F1/F5), then MMIO register reads, which are usually
capability or width fields and so produce bad shifts rather than bad stores
(the F7 shape).  A length-ish field name is a weak tiebreak within a kind.

## First-pass triage of the top candidates

I read the top six.  None is a confirmed bug, and two are false positives
whose cause is worth recording.

* `drivers/scsi/qla2xxx/qla_isr.c:3742` (`req_sense_length`) --
  **false positive.** The copy length is clamped two lines earlier
  (`if (sense_len > sizeof(pkt->data)) sense_sz = sizeof(pkt->data)`), but
  the clamp assigns a *different* variable than the one at the sink, so the
  `adv_guard` advisory misses it.
* `drivers/net/can/esd/esdacc.c:455` (`acc_dlc.len`) -- **false positive.**
  The value is masked (`& ACC_DLC_DLC_MASK`) and then passed through
  `can_frame_set_cc_len()`, which maps it via `can_cc_dlc2len()` to at most
  8.  The mask and the clamp are both one call away from the sink, so the
  `adv_mask` advisory misses them.
* `drivers/scsi/be2iscsi/be_mgmt.c:46` (`actual_resp_len`) --
  **false positive, different cause.** The sink is
  `memset(nonemb_cmd->va, 0, nonemb_cmd->size)` where `size` comes from a
  bsg job payload, not from the device.  The device field was linked in by
  field-level association: the query connects a store to field `F` with any
  read of `F` anywhere in the program, and `->size` / `->len` are far too
  common for that to hold in a kernel-sized DB.
* `drivers/net/ethernet/broadcom/asp2/bcmasp_intf.c:568`,
  `drivers/net/ethernet/atheros/atlx/atl2.c:426`,
  `drivers/net/ethernet/atheros/atl1e/atl1e_main.c:1456` -- **the F6 family,
  needs per-driver work.** All three take the RX length from the device
  descriptor, allocate an skb of exactly that size, and `memcpy` that many
  bytes out of the RX buffer. The destination is safe by construction (it is
  sized from the same value); the question is the **source**: if the device
  reports a length larger than the buffer the driver posted, the copy
  over-reads the DMA buffer into a packet that goes up the stack. Deciding
  each case needs the posted RX buffer size for that driver, which is
  per-driver work I have not done.

So the ranking works -- the top of the list is the right shape -- but the
advisories are too local: they only see a guard, mask or clamp applied
directly to the sink expression in the same function.

## Precision gaps to fix next (in order)

1. **Clamp/mask through an intermediate variable or helper.** The existing
   count/index finder already has `minClamped` and `srcGuarded` for exactly
   this; the device query needs the same advisories. This alone would have
   dropped two of the six candidates I read.
2. **Field-level association is too coarse.** Linking a store to field `F`
   with every read of `F` program-wide is fine in a single-driver DB and
   wrong in a kernel-sized one. It should be restricted to the same
   compilation unit, or to the same struct type, rather than the field name.
3. **Subsystem-specific sources.** `drivers/usb/host` alone yields 5099
   sinks because USB transfer buffers are DMA-reachable everywhere; that
   surface is already modelled better by `KernelTaint::UrbBufferAccess`, so
   the device query should defer to it there.

## Reproducing

```
cd integration/linux/scan/abc-refinement
python3 device_census.py --build --workers 8     # resumable; JSONs in /tmp/devcensus
python3 device_census.py --report
python3 device_census.py --report --top 30
```

Builds pre-patch `percpu.h` once for the whole run and revert it at the end;
check `git diff arch/x86/include/asm/percpu.h` is clean afterwards.
