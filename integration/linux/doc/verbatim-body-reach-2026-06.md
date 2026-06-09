# Verbatim-body reach: the `reach` column on real kernel code

**Date:** 2026-06-09. Extends the closed loop / cover-probe so a candidate
can be adjudicated on the **actual kernel function body**, not just the
parameterised shape model.

## What changed

`triage_loop.py` gained a `REAL_HARNESS` registry: a candidate function
name → a harness that wraps the verbatim kernel body (real struct layouts,
real arithmetic, real guards) with `probe_vuln` / `probe_fixed` entries and
a `CHECKPOINT` at the sink. When a candidate has one, both the shape
(bounds/overflow) and reach (cover-probe) verdicts are computed on the real
body; the table marks it `src=REAL`. Candidates without one fall back to
the shape model (`src=shape`).

First real harness: `real_cgw_csum.c` — the verbatim `net/can/gw.c`
`cgw_csum_crc8_pos` body and the verbatim uapi struct layouts
(`canfd_frame`, `cgw_csum_crc8`). The OOB primitive is
`cf->data[crc8->result_idx]` with `result_idx` an `__s8` copied raw from a
netlink attribute (`nla_memcpy`).

```
function           ... src    shape:bug  shape:fix   reach:bug   reach:fix
cgw_csum_crc8_pos  ... REAL   FAILED     SUCCESSFUL  REACHABLE   BLOCKED
cgw_csum_crc8_neg  ... shape  FAILED     SUCCESSFUL  REACHABLE   BLOCKED
```

`probe_vuln` (raw attacker `result_idx`) → shape FAILED / reach REACHABLE;
`probe_fixed` (caller-validated index, as the CVE fix enforces) → shape
SUCCESSFUL / reach BLOCKED — on the real body.

## On the "further C front-end fixes" hypothesis

The expectation was that pushing verbatim kernel code through the front-end
would surface new C front-end gaps. It did **not**: the full translation
units for all four real candidate sources goto-cc cleanly with the current
build —

| TU | goto binary |
|----|-------------|
| `net/can/gw.c` (cgw_csum) | 4.8 MB, clean |
| `net/rxrpc/rxkad.c` (rxkad_decrypt_ticket) | 6.3 MB, clean |
| `net/ceph/osdmap.c` (crush_decode) | 1.3 MB, clean |
| `net/ceph/auth_x.c` (ceph_x_handle_reply) | 2.3 MB, clean |

— so the 8 front-end fixes landed earlier
(`cbmc-frontend-kernel-hardening-2026-06.md`) already cover the kernel
parser/decoder surface these candidates live in. The honest conclusion:
no new front-end fix was required to reach verbatim bodies here; the
remaining work to scale verbatim-body adjudication is harness authoring
(real struct stubs + vuln/fixed entries), not front-end capability.

## Two routes to a verbatim body

1. **Verbatim extraction** (used by `real_cgw_csum.c`,
   `real_nfc_llcp_cover.c`): copy the function body and the small real
   struct definitions; CBMC checks the property in isolation. Cleanest for
   a focused candidate.
2. **Full-TU goto-cc**: build the real `.gb` from the kernel `make V=1`
   command (gcc→goto-cc, strip `-Werror`). Confirmed to load and run, but
   `cbmc --function <static fn>` over a whole TU is noisy (nondet-pointer
   "failures" in unrelated helpers) and needs a wrapping harness to isolate
   the property — i.e. it reduces to route 1 for a clean verdict.

## Files

* `real_cgw_csum.c` — verbatim CVE-2019-3701 (CAN-gw) harness.
* `triage_loop.py` — `REAL_HARNESS` registry + `src` column.
* `real_nfc_llcp_cover.c` — verbatim CVE-2026-31622 (also registered).
