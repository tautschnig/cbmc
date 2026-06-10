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

## Subsystem coverage census (`subsystem_census.py`, data-driven)

`subsystem_census.py` counts leaf subsystems in a kernel tree and marks one
COVERED if any built CodeQL DB contains a compiled `.c` under it (read from
each DB's `src.zip`).  Driven to >50% by WHOLE-CLASS DBs (one `net/` DB
covers all configured net subsystems, etc.) unioned across two trees
(linux-next + 6.12, which enable different subsets), via
`run_threat_survey.sh`:

| class | total | covered | pct |
|-------|------:|--------:|----:|
| net/* | 67 | 63 | 94.0% |
| fs/* | 78 | 73 | 93.6% |
| drivers/* | 144 | 126 | 87.5% |
| sound/* | 25 | 16 | 64.0% |
| top-level atomic | 9 | 8 | 88.9% |
| **TOTAL** | **323** | **286** | **88.5%** |

(Up from 6.2% -> 62.5% -> 88.5%.)  The drivers jump came from the 6.12
whole-`drivers/` build under 6.12's broad distro config (127/144 leaves,
~78 min, 36 GB peak) -- linux-next/mainline configs only enable ~43 driver
leaves, so the broad 6.12 config is what closes the gap.  Whole-class build
cost is ~6-10 min each except 6.12 drivers (~78 min); the residual ~11.5%
is leaves no available config enables (would need allmodconfig).  All
reproducible via `run_threat_survey.sh` (BUILD_TIMEOUT default 6000 s).

## What outcomes do we see? (`outcome_summary.py` + `pipeline_eval.py`)

Coverage is breadth; this is what the analysis *says* about the covered
code.  Outcomes split across three levels.

**Finder-run outcomes** -- running the 8 finders on a whole-class DB:
completes on net/fs/sound (~fast), but **whole-`drivers/` global taint
analysis does not scale** (QUERY-TIMEOUT/ERROR at 360 s even with a 200 GB
cap).  Honest finding: the *structural* finders are subsystem-agnostic and
cheap, but interprocedural taint on a monster DB needs per-leaf scoping
(run with `--module drivers/<leaf>`), not a single whole-drivers query.

**Candidate outcomes** -- aggregate over net-all + fs-all + sound-all
(24 finder runs completed, 1 query-timeout, 7 query-error):

| asset | raw | mitigated/cleared | genuine residual |
|-------|----:|------------------:|-----------------:|
| A1-mem count/index | 225 | (structural) | 225 |
| A1-mem decoded-len | 36 | (structural) | 36 |
| A1-mem skb/cursor | 315 | (structural) | 315 |
| A1-UB div/shift | 44 | 15 | 29 |
| A2 confidentiality | 195 | 87 | 108 |
| A3 integrity/CFI | 4 | (structural) | 4 |
| A4 availability | 113 | 13 | 100 |
| A5 authorization | 31 | (structural) | 31 |
| **TOTAL** | **963** | **115** | **848** |

Of the mitigation-capable assets (A1-UB / A2 / A4: 352 raw), 115 (33%)
clear via a recognised mitigation (memset / clamp / nonzero / shift-bound /
capability gate).  The structural finders (611 raw) carry no auto-mitigation
column -- every hit enters the discharge/dominance stage below.

**Discharge outcomes** -- the CBMC-adjudicated tail (rc7-db funnel, the
ground-truth-rich net surface).  The skb/cursor funnel: 200 raw / 159 funcs
-> 7 precondition-resolved -> 54 skb-pull-weak -> 29 distilled survivors,
of which CBMC adjudicates a handful.  The verdicts seen are NOT all
timeouts -- the full spectrum:

| discharge verdict | example | meaning |
|-------------------|---------|---------|
| REAL, reach REACH | `ieee80211_get_ttlm` | shape-bug witnessed AND reachable in the verbatim body |
| REAL shape, reach BLOCK | `try_rfc959` | bug shape exists but CBMC proves it unreachable -- true negative |
| CALLER-GUARDED | `rxkad_decrypt_ticket` | caller validates the bound (`ticket_len>=4`) -- FP resolved without a harness |
| PRODUCER-GUARDED | (struct-fill sites) | field validated where filled from the wire |
| shape-model / BMC-intractable | `crush_decode` | `u32*u32` cannot overflow `size_t` on 64-bit -- needs a shape model, verbatim BMC intractable |
| CLEAR (mitigated) | A2 padding/full-init | memset/full copy dominates the leak |
| **TIMEOUT** | `parse_uac2_sample_rate_range`, `parse_audio_format_rates_v1` | large descriptor-parser; tracked by `pipeline_eval.py` "CBMC-discharge timeouts: N/M" |

So timeouts are one bucket among several; the dominant outcomes are
mitigation/guard CLEARs, with a small genuine-and-reachable REAL tail and a
shape-model-territory bucket that BMC can't settle verbatim.

## CBMC-obligation discharge status on the new candidates

The discharge mechanism (auto_real_harness / hand-authored) is proven, but
scaling it onto the NEW cross-subsystem candidates hits two characterised
limits:

* **Large functions time out** -- `ext4_xattr_block_set`,
  `parse_uac2_sample_rate_range` and the like are big parsers; auto-harness
  times out (the documented small-leaf-only sweet spot).  These need
  focused hand-authored harnesses (the rxkad-class approach).
* **Large functions time out, now TRACKED** -- `pipeline_eval.py` reports
  `CBMC-discharge timeouts: N / M` over the adjudicated survivors; the
  sound/usb descriptor loops (parse_uac2_sample_rate_range,
  parse_audio_format_rates_v1) are tracked timeouts.  Reproducible via the
  automation, so perf work can target them later.
* **(FIXED) goto-cc front-end gap on linux_6_12** -- the dynamic-debug `_ddebug`
  descriptor (`.function = __func__` in a `__section` static initializer)
  is rejected as a non-constant expression, blocking goto-cc (hence the
  discharge step) on 6_12 sound/usb, hfsplus, jfs.  Narrowed by a minimal
  repro: `.function = __func__` alone compiles fine -- the rejected operand
  is the CONFIG_JUMP_LABEL `static_key` init in `_ddebug.key`.  (The CodeQL
  DBs use the gcc extractor, so the census/eval above are unaffected.)  A
  FIXED (commit 'Simplify member access into a compound literal'):
  member-of-compound-literal now folds, so sound/usb/format.c (and the
  6.12 dynamic-debug-using TUs) goto-cc cleanly and the discharge PATH is
  unblocked -- the sound/usb candidates then run and time out (tracked).

Successfully discharged real candidates remain: net (cgw REAL-OOB, rxkad
caller-guarded, mldv2 mask-bounded SUCCESSFUL), net/nfc (CVE-2026-31622
`real_nfc_llcp_cover.c`: vuln REACHABLE / fixed BLOCKED), ceph
(`__decode_pg_upmap_items` CLEAN).

## DBs (build via build-codeql-db.sh)

broad-next-db, rc7-db (net); hid-db (drivers/hid); ext4-db, hfsplus-db,
f2fs-db (fs); cryp-db (crypto); sndusb-db (sound/usb); jfs-db, fat-db (fs); nfc-db (net/nfc).
