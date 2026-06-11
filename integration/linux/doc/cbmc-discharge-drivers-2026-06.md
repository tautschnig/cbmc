# CBMC filtering layer on the drivers base set (2026-06)

**Date:** 2026-06-11.  The sound over-approximate collector produces the
candidate set; advisories only RANK; CBMC is the definitive filter.  This
records the first CBMC discharges on the top-ranked drivers HIGH/WRITE
candidates from the sub-scoping base set.

## Top HIGH/WRITE ranking (advisory)

| candidate | site | advisories |
|-----------|------|-----------|
| cec_config_thread_func x4 | cec-adap.c:1466.. | UNGUARDED / mask=none / field |
| altera_execute | altera.c:518 | UNGUARDED / **mask=MASKED** / local |
| hiddev_ioctl_usage, rename_volumes, __cec_s_log_addrs | | GUARDED |

Highest priority = no advisory suggests safety (UNGUARDED, mask=none,
field): the cec_config_thread_func cluster.  altera ranks lower (mask=MASKED).

## CBMC discharge (definitive)

`altera_execute` -- `args[i]` over `for(i<arg_count)`, `arg_count =
(opcode>>6)&3`, `u32 args[3]` (`real_altera_args.c`, verbatim slice):

```
[main.array_bounds.2] line 13 array 'args' upper bound in args[i]: SUCCESS
VERIFICATION SUCCESSFUL
```

CBMC PROVES the write in bounds -- the unsound `adv_mask=MASKED` advisory is
upgraded to a SOUND clear.  (The full 1000-line bytecode interpreter does not
scale under whole-function CBMC -- the documented "large functions need a
hand-authored harness" case; the slice captures exactly the flagged
computation.)

`cec_config_thread_func` -- `log_addr[i]` over `for(i<num_log_addrs)`,
`log_addr[CEC_MAX_LOG_ADDRS=4]` (`real_cec_log_addrs.c`):

```
# without the non-local validator:
[main.array_bounds.1] ... 'las'.log_addr upper bound ...: FAILURE
VERIFICATION FAILED
# with the validator precondition (num_log_addrs <= CEC_MAX_LOG_ADDRS):
VERIFICATION SUCCESSFUL
```

CBMC witnesses the OOB on the verbatim body and **correctly does NOT clear**
the candidate -- validating the sound-collector decision to keep it (the
advisory framework-validation must not drop it).  Safety holds ONLY under the
non-local precondition established at cec-adap.c:1835
(`num_log_addrs > available_log_addrs -> -EINVAL`, `available_log_addrs <=
CEC_MAX_LOG_ADDRS`).  So the correct discharge is a caller-precondition
proof carrying that bound; the verbatim body alone is a live OOB shape.

## CBMC adjustment surfaced

goto-cc on x86 kernel TUs hits `__seg_gs` / `__seg_fs` -- the per-CPU
named-address-space qualifiers (`__attribute__((address_space(__seg_gs)))`,
arch/x86/include/asm/percpu.h).  These are segment-relative addressing
annotations, irrelevant to functional verification, handled with
`-D__seg_gs= -D__seg_fs=` on the goto-cc line (altera.c then builds clean).
A first-class front-end fix (accept + ignore the `address_space` attribute
with an identifier argument) would remove the need for the -D shim; the shim
is sound for verification.

## Takeaway

The layering works end to end: collector (sound, over-approximate) ->
advisory ranking -> CBMC (definitive).  altera = advisory-MASKED, CBMC-proven
safe; cec = advisory-genuine, CBMC-confirmed real-shaped and non-local-
validator-dependent.  Neither verdict came from a heuristic drop.
