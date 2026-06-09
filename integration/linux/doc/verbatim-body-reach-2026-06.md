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

First real harnesses: `real_cgw_csum.c` (the verbatim `net/can/gw.c`
`cgw_csum_crc8_pos` body) and `real_rxkad_ticket.c` (the verbatim
bounded-cursor ticket-parse core of `net/rxrpc/rxkad.c`
`rxkad_decrypt_ticket`).

### cgw_csum_crc8_pos

The OOB primitive is `cf->data[crc8->result_idx]` with `result_idx` an
`__s8` copied raw from a netlink attribute (`nla_memcpy`).

```
function           ... src    shape:bug  shape:fix   reach:bug   reach:fix
cgw_csum_crc8_pos  ... REAL   FAILED     SUCCESSFUL  REACHABLE   BLOCKED
cgw_csum_crc8_neg  ... shape  FAILED     SUCCESSFUL  REACHABLE   BLOCKED
```

`probe_vuln` (raw attacker `result_idx`) → shape FAILED / reach REACHABLE;
`probe_fixed` (caller-validated index, as the CVE fix enforces) → shape
SUCCESSFUL / reach BLOCKED — on the real body.

### rxkad_decrypt_ticket (verbatim resolves a function-granularity FP)

The bounded-cursor oracle flags this at *function* granularity: the flags
byte `*p` is read with no in-function length check.  But the OOB is closed
by the **caller's** precondition — `rxkad_verify_response` (rxkad.c:1167)
rejects `ticket_len < 4`.  The verbatim harness makes this precise:

```
function              ... src    shape:bug  shape:fix   reach:bug   reach:fix
rxkad_decrypt_ticket  ... REAL   FAILED     SUCCESSFUL  REACHABLE   BLOCKED
```

`probe_vuln` (function in isolation, `ticket_len` unconstrained) is
OOB-reachable; `probe_fixed` (with the real caller guard `ticket_len >= 4`)
is safe.  So the verbatim+precondition analysis adjudicates the
function-granularity hit as an FP closed by the caller — exactly the
triage distinction the talk calls for.

### crush_decode — not a verbatim target (BMC-intractable), by design

`net/ceph/osdmap.c crush_decode` (flagged by the decoded-length oracle for
a count→multiply) is deliberately *not* harnessed verbatim: its bucket
loop iterates a `u32` count (up to 2³²), which bounded model checking
cannot unwind without bounding the count — and bounding it removes the
overflow scenario.  Moreover, on 64-bit a `u32 × sizeof` cannot overflow
`size_t`, and the real code uses `kzalloc_objs`→`array_size`/`size_mul`
(overflow-checked) regardless.  The arithmetic-overflow question is
precisely what the loop-free **shape model** answers (which `triage_loop`
already runs for the `decoded` oracle); the verbatim unbounded-count loop
is the wrong tool.  An honest boundary of the verbatim approach.

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

## Auto-generated verbatim harnesses (scaling src=REAL)

Hand-authoring a harness per function does not scale.  `auto_real_harness.py`
generates the verbatim reach verdict for ANY function in a buildable TU
using CBMC's `goto-harness`:

```
goto-cc (full TU) -> TU.gb
goto-harness --harness-type call-function --function F  -> harness.gb
cbmc harness.gb --function h_F --bounds-check --pointer-check
```

`goto-harness` nondet-initialises and *validly allocates* F's pointer
arguments (so there is no NULL-pointer noise — the deref checks pass), then
CBMC checks the verbatim body.  A failing `array_bounds` / outside-object
property in F means the body is OOB-reachable for some input — the
function-in-isolation (worst-case / `probe_vuln`) view, with **no manual
struct stubs or entries**.  Combined with `caller_precondition.ql` it gives
the full triage (isolation-OOB + UNGUARDED = genuine; + GUARDED = the
caller/producer saves it).

Results (auto, zero authoring):
* `cgw_csum_crc8_pos` (gw.gb) → **REAL-OOB**, both sites — the loop read
  `cf->data[i]` (line 410) and the write `cf->data[crc8->result_idx]`
  (line 427).  Matches the hand-authored `real_cgw_csum.c`.
* `__decode_pg_upmap_items` (osdmap.gb) → **CLEAN** — a ceph decoder using
  `ceph_decode_*_safe`; CBMC clears it (auto true-negative).

Honest boundary: auto-harness scales to self-contained functions;
crypto/alloc-heavy ones still need a hand-authored stub.
`rxkad_decrypt_ticket` **times out** (goto-harness allocates the
`skcipher_request` and CBMC symexes `crypto_skcipher_decrypt`) — which is
exactly why `real_rxkad_ticket.c` stubs the in-place decrypt.  So the two
approaches are complementary: auto-harness for breadth, hand-authored for
the heavy dependencies.

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

* `auto_real_harness.py` — auto-generated verbatim harness via goto-harness
  (scales src=REAL to any self-contained function in a buildable TU).
* `real_cgw_csum.c` — verbatim CVE-2019-3701 (CAN-gw) harness.
* `real_rxkad_ticket.c` — verbatim rxkad ticket-parse core (FP-resolved).
* `triage_loop.py` — `REAL_HARNESS` registry, `src` column, `--only`.
* `real_nfc_llcp_cover.c` — verbatim CVE-2026-31622 (also registered).
