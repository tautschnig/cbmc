# Kernel-wide all-class candidate base set (2026-06)

**Date:** 2026-06-11.  The sound over-approximate collector run across ALL
subsystem classes (not just `drivers/`), via `full_census.py` -- the same
per-leaf-DB unit model (heavy leaves sub-scoped into depth-1 subdirs),
generalised to `net/* fs/* sound/* drivers/*` + the top-level atomic
subsystems, reusing the cached drivers units.  Parallel + resumable.

## Base set (533/598 units; ~65 heavy sub-units resume on re-run)

| class | units | raw candidates | genuine |
|-------|------:|---------------:|--------:|
| net | 96 | 4331 | 4211 |
| fs | 74 | 1355 | 1328 |
| drivers | 305 | 10600 | 10348 |
| sound | 35 | 1053 | 1049 |
| atomic | 9 | 481 | 443 |
| **TOTAL** | **519** | **17820** | **17379** |

By asset (all classes): A1-mem count/index 10696, skb/cursor 4758,
A2 848 (353 mitigated), A4 735, A3 485, A1-UB 226, decoded-len 138,
A5 121.  This is the sound base set: advisories only rank; CBMC / sound
checks / manual review are the sole definitive filters.

Enabled by the `__seg_fs`/`__seg_gs` front-end fix, which unblocks goto-cc
on x86 kernel TUs (needed before any of these candidates can reach CBMC).

## Cross-subsystem HIGH/WRITE ranking (the OOB-write tier)

21 HIGH-confidence count/index WRITE candidates across all classes.  Ranked
by advisories (top = nothing suggests safety: UNGUARDED, mask=none, field):

| candidate | site | advisories | note |
|-----------|------|-----------|------|
| cec_config_thread_func x4 | cec-adap.c:1466.. | UNGUARDED / none / field | CBMC: non-local-validator-dependent (cec-adap.c:1835) |
| mqprio_enable_offload x2 | net/sched/sch_mqprio.c:58 | UNGUARDED / none / field | NEW -- tc qdisc num_tc offload |
| taprio_change | net/sched/sch_taprio.c:1900 | UNGUARDED / none / field | NEW -- tc qdisc |
| altera_execute | altera.c:518 | UNGUARDED / **MASKED** / local | CBMC-PROVEN safe (mask bound) |
| __cec_s_log_addrs, hiddev_ioctl_usage, rename_volumes, megasas_mgmt_fw_ioctl x2, ax25_rt_add x2, afs_deliver_vl_get_entry_by_name_u x4 | | GUARDED / ... | local guard advisory -> lower rank |

The net/sched `mqprio`/`taprio` candidates are the freshest genuine-ranked
OOB-write targets surfaced by going all-class; the CEC cluster is already
CBMC-characterised (real-shaped, validator-dependent).  These are the next
CBMC discharge targets.

## Honest status

* 533/598 units; ~65 heavy sub-units (gpu/staging/usb/sound-soc subdirs)
  resume on re-run -- the full batch is large under the slower sound
  collector.
* The base set is the FINDER layer (candidates).  17379 genuine candidates
  are now reachable across the kernel; the HIGH/WRITE tier (21) is the
  ranked OOB-write working set for the CBMC filtering layer.
