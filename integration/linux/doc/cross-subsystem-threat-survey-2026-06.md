# Cross-subsystem threat-model survey

**Date:** 2026-06-10.  The five-asset threat-model framework + the extended
taint sources, pointed at seven subsystems spanning networking, drivers,
filesystems, crypto and sound.  Each cell is the raw candidate count from
`threat_model_eval.py` (A1-mem split into its three finders).

| subsystem (actor) | A1 count | A1 decoded | A1 skb/cursor | A1-UB | A2 | A3 | A4 | A5 |
|-------------------|---------:|-----------:|--------------:|------:|---:|---:|---:|---:|
| net/ (broad-next) | 92 | 28 | 170 | 10 | 39 | 1 | 47 | 9 |
| drivers/hid (USB/BT) | 4 | 0 | 0 | 0 | 6 | 1 | 2 | 1 |
| fs/ext4 (on-disk) | 1 | 0 | 0 | 0 | 9 | 1 | 10 | 1 |
| crypto/af_alg (user/iov) | 4 | 0 | 68 | 0 | 15 | 1 | 1 | 0 |
| fs/hfsplus (on-disk) | 0 | 2 | 34 | 1 | 0 | 1 | 6 | 0 |
| fs/f2fs (on-disk) | 8 | 0 | 0 | 1 | 4 | 1 | 0 | 0 |
| sound/usb (USB) | 65 | 0 | 0 | 2 | 2 | 1 | 10 | 0 |
| net/nfc (NFC/skb) | 9 | 0 | 55 | 0 | 2 | 1 | 4 | 0 |
| fs/jfs (on-disk) | 0 | 0 | 0 | 0 | 1 | 1 | 0 | 0 |
| fs/fat (on-disk) | 1 | 0 | 0 | 0 | 1 | 1 | 0 | 1 |

Concrete genuine candidates (UNMITIGATED, post-filter):

* **sound/usb A4** -- `parse_uac2_sample_rate_range`,
  `parse_audio_format_rates_v1`, `change_volume` (loops over a
  USB-descriptor-supplied count -- classic attacker-USB-device surface);
  **A1 count/index** 65 (descriptor field indexing).
* **fs/hfsplus A4** -- `hfsplus_bnode_dump`, `hfsplus_uni2asc`,
  `hfsplus_free_fork` (on-disk B-tree node loops); **skb/cursor** 34
  (the bounded-cursor finder firing on `(buf,len)` on-disk record parsing).
* **crypto/af_alg** -- **skb/cursor** 68 (scatter-gather cursor walks --
  the copyfail subsystem); A2 padding-cleared.
* **fs/ext4 A4** -- `ext4_xattr_block_set`, `ext4_update_inline_data`
  (on-disk-sized allocations, via the interproc-alloc finder).
* **drivers/hid** -- `hidraw_fixed_size_ioctl`, `hiddev_read` (A2
  copy-to-user), `hiddev_ioctl_usage` (A4).

## Takeaways

* The framework produces rich cross-asset candidates on **every** subsystem
  class -- networking, USB/HID device input, on-disk filesystems, crypto
  user/iov, and USB audio -- not just net.
* **Structural finders are subsystem-agnostic**: A1-mem count/index and
  A3/A5 fire wherever the control/data shape exists (sound/usb's 65
  descriptor indices, f2fs's 8); A2 fires at every copy-to-user boundary.
* **The bounded-cursor finder generalises to non-net `(buf,len)`/`(p,end)`
  parsers**: 68 on crypto sg-walks, 34 on hfsplus on-disk records.
* **The extended taint sources** (bh->b_data, urb, fw, iov, user, hid_field)
  now drive the taint-gated A4/A1-UB finders across fs (ext4/hfsplus/f2fs)
  and drivers (sound/usb/hid), e.g. sound/usb's 10 A4 loops and ext4's
  10 -- which were 0 before the source extension.
* A3 is uniformly ~1 (direct fn-ptr writes are rare everywhere -- the
  indirect CFI threat routes through A1, by design).

## Subsystem coverage census (`subsystem_census.py`)

Counting leaf subsystems in a kernel tree:

| class | total | covered | pct |
|-------|------:|--------:|----:|
| net/* | 70 | 12 | 17.1% |
| fs/* | 79 | 5 | 6.3% |
| drivers/* | 141 | 1 | 0.7% |
| sound/* | 25 | 1 | 4.0% |
| top-level atomic | 9 | 1 | 11.1% |
| **TOTAL** | **324** | **20** | **6.2%** |

Coverage is breadth-of-validation, not exhaustive scanning: all five major
subsystem CLASSES are exercised, but the per-leaf fraction is small by
design (each DB is built on demand).  `drivers/*` (141) is the largest
uncovered surface -- one driver class (hid) covered so far; raising it is
the biggest lever on the total.

## CBMC-obligation discharge status on the new candidates

The discharge mechanism (auto_real_harness / hand-authored) is proven, but
scaling it onto the NEW cross-subsystem candidates hits two characterised
limits:

* **Large functions time out** -- `ext4_xattr_block_set`,
  `parse_uac2_sample_rate_range` and the like are big parsers; auto-harness
  times out (the documented small-leaf-only sweet spot).  These need
  focused hand-authored harnesses (the rxkad-class approach).
* **goto-cc front-end gap on linux_6_12** -- the dynamic-debug `_ddebug`
  descriptor (`.function = __func__` in a `__section` static initializer)
  is rejected as a non-constant expression, blocking goto-cc (hence the
  discharge step) on 6_12 sound/usb, hfsplus, jfs.  (The CodeQL DBs use the
  gcc extractor, so the census/eval above are unaffected.)  A goto-cc
  hardening item.

Successfully discharged real candidates remain: net (cgw REAL-OOB, rxkad
caller-guarded, mldv2 mask-bounded SUCCESSFUL), net/nfc (CVE-2026-31622
`real_nfc_llcp_cover.c`: vuln REACHABLE / fixed BLOCKED), ceph
(`__decode_pg_upmap_items` CLEAN).

## DBs (build via build-codeql-db.sh)

broad-next-db, rc7-db (net); hid-db (drivers/hid); ext4-db, hfsplus-db,
f2fs-db (fs); cryp-db (crypto); sndusb-db (sound/usb); jfs-db, fat-db (fs); nfc-db (net/nfc).
