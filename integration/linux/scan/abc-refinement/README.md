# abc-refinement: a CodeQL → CBMC bug-triage pipeline for the Linux kernel

Stage-1 CodeQL **oracles** flag candidate memory-safety bug *shapes* in
kernel parser/decoder code; a tiered **triage** layer (taint confidence ×
exploitability impact × caller/producer precondition) distils the flood to
a small genuine-concern set; stage-2 **CBMC** adjudicates each survivor on
the bug shape and on the real function body.  The guiding principle (from
the "kernel security in the age of AI" talk): *finding* candidates is
cheap — the value is **triage**, deciding which are real, exploitable, and
not already guarded.

## Pipeline at a glance

```
CodeQL oracle hit
  → confidence tier   (KernelTaintFlow: is the index/count raw-taint-reachable?)   HIGH / MEDIUM
  → impact tier       (WRITE/control vs READ/DoS)
  → precondition      (caller_precondition.ql: is the bound guarded by a caller/producer?)
                        CALLER-GUARDED / PRODUCER-GUARDED / UNGUARDED  → FP filter
  → CBMC shape        (oracle_harness_gen shape model: is the bug shape OOB-reachable?)
  → CBMC reach        (cover_probe: is the OOB precondition path feasible?)
  → CBMC verbatim     (auto_real_harness / hand-authored real_*.c: real body verdict)
```

## Components

### Oracles (stage-1, CodeQL)
* `tainted_count_into_fixed_array.ql` — unvalidated count/index into a
  fixed array (WRITE/READ impact, HIGH/MEDIUM confidence tier).
* `decoded_len_arith_overflow.ql` — wire-decoded length into round-up /
  multiply overflow (ceph/nla/rxgk decode accessors).
* `skb_field_before_lencheck.ql` — `skb->data` / bounded-cursor field read
  with no length guard.
* `tlv_parse_loop.ql`, `tlv_parse_loop_helper.ql` — TLV walk loops.
* `tainted_*`, `pagecache_write_inplace.ql` (copyfail-class needle), etc.

### Taint layer (CodeQL libraries)
* `KernelTaint.qll` — tiered sources (skb->data, `nla_get_*`/`ceph_decode_*`,
  byte-order builtins), the `(p,end)` cursor shape, and sanitizers
  (`pskb_may_pull`, `ceph_decode_need`/`ceph_has_room`).
* `KernelTaintFlow.qll` — `isRawTaintReachable` (raw-wire reachability;
  function-pointer-robust), driving the HIGH/MEDIUM confidence tier.

### Precondition automation (`caller_precondition.ql`)
Determines whether a function-granularity hit is an FP closed by its
callers/producer.  Four bound shapes, control-flow **dominance** + SSA
value-flow (sound for dismissal):
* **len-param** scalar guard (`if (len < N) reject`);
* **`(p,end)` cursor** (`ceph_decode_need`/`end` relational before call);
* **struct-field** producer-side (`cgw_chk_csum_parms` before `nla_memcpy`);
* **skb-pull** interprocedural-depth-3 (`pskb_may_pull` up the rx stack).

### Stage-2 adjudication (CBMC)
* `oracle_harness_gen.py` — parameterised **shape** harness (buggy/fixed +
  cover-probe checkpoint).
* `cover_probe.py` / `cover_probe.h` — **assumption bisection**: probe a
  claimed path, report the first infeasible hop.
* `auto_real_harness.py` — **auto-generated verbatim** harness via
  `goto-harness` (small self-contained fixed-array functions).
* `real_*.c` — **hand-authored verbatim** harnesses for under-provision /
  large / crypto functions (`real_cgw_csum.c`, `real_rxkad_ticket.c`,
  `real_ttlm.c`, `real_ftp_number.c`, `real_nfc_llcp_cover.c`).

### Drivers
* `triage_loop.py` — per-candidate table: confidence × impact × src
  (auto/REAL/shape) × shape:b/f × reach:b/f.
* `pipeline_eval.py` — end-to-end funnel + survivor adjudication + caller
  column + known-CVE ground truth over a whole subsystem DB.

## Running it

```bash
./rebuild.sh synth          # synthetic fixture DBs (fast)
./rebuild.sh gb             # goto binaries from kernel trees (minutes)
RUN_SUBSYS=1 ./rebuild.sh subsys   # subsystem CodeQL DBs (heavy)

python3 pipeline_eval.py --db /tmp/broad-next-db        # funnel + survivors
python3 triage_loop.py --db /tmp/broad-next-db \
    --query tainted_count_into_fixed_array.ql --min-confidence HIGH
python3 auto_real_harness.py --gb /tmp/gw.gb --function cgw_csum_crc8_pos
python3 cover_probe.py --file real_nfc_llcp_cover.c --function probe_vuln \
    --probe-function llcp_parse_vuln
```

## Measured result (broad-next-db, net/ surface)

290 raw hits / 211 functions → **28 distilled genuine concerns**; the four
CVE-2019-3701 `cgw_csum_*` surface at the top tier (HIGH/WRITE), CBMC-
confirmed, and **6/8 distilled survivors are auto-resolved as FPs** by the
precondition automation without a harness.  Generalises to a second surface
(rc7-db: sctp/ipv4/mptcp/wireless) — see `pipeline-eval-2026-06.md`.

## Honest limits

* **Confidence tier** discriminates count/index; for decoded/skb raw-taint
  is baked in, so the **precondition verdict** is the discriminator there.
* **Auto-harness** suits small self-contained fixed-array functions; it
  *misses* under-provision bugs (over-allocates buffers) and *times out* on
  large/crypto/pointer-arith functions — hand-authored harnesses cover
  those.
* **Precondition** is sound for *dismissal* (dominance + SSA); cursor
  CALLER-GUARDED is a heuristic prior (UNGUARDED is the dependable verdict).

## Key docs

`pipeline-eval-2026-06.md` (the measured funnel), `caller-precondition-2026-06.md`,
`verbatim-body-reach-2026-06.md`, `cover-probe-bisection-2026-06.md`,
`closed-loop-triage-2026-06.md`, `taint-layer-status-2026-06.md`,
`talk-inspired-ideas-2026-06.md`, `cbmc-frontend-kernel-hardening-2026-06.md`
(all under `integration/linux/doc/`).
