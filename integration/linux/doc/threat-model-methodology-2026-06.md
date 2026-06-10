# Threat-model-driven analysis (methodology pivot)

**Date:** 2026-06-10.

So far the pipeline has worked **bottom-up**: each oracle is a *threat
template* distilled from a previously-seen CVE, asking "is there more of
the same shape?".  This pivots to **top-down**: for each piece of code, we
first build its **threat model** — its assets, the actors over its data,
and the resulting threats — and only then choose (or derive) the template
that finds candidates and the CBMC obligation that proves the threat is
mitigated.

```
code unit
  └─ assets (what must hold) × actors (who controls what)
        └─ threats (asset violated by an actor)
              └─ CodeQL finder template   (locate candidate sites)
              └─ CBMC obligation          (prove the threat is mitigated)
```

The existing oracles are then seen as **one family** — memory-safety — and
the model makes the *other* assets we had ignored explicit and actionable.

## Asset taxonomy

| asset | violated when | example threat |
|-------|---------------|----------------|
| **A1 well-definedness (absence of UB)** | the program leaves defined C semantics (see sub-classes below) | attacker operand triggers UB |
| **A2 confidentiality** | non-public kernel data reaches a lower-privilege actor | uninitialised padding copied to user (info leak) |
| **A3 integrity / CFI** | control data or protected state overwritten | function-pointer / vtable overwrite, write-what-where |
| **A4 availability / termination** | unbounded work or resource on attacker input | unbounded loop / huge alloc (DoS) |
| **A5 authorization** | an action taken without the required capability/namespace check | missing `capable()` / cross-namespace access |

### A1 is an umbrella: UB sub-classes and where they route

"Absence of UB" is broad — **memory safety is only its most
exploitation-relevant sub-class**, not the whole of it.  Integer overflow,
division by zero, oversized shifts, uninitialised reads, etc. are sibling
sub-classes.  Two things place a UB instance in the threat model: an
**actor** must control an operand (an internal constant-bounded counter
overflowing is not a threat), and the violation **chains** to a downstream
asset.  Each sub-class has its **own** CBMC obligation:

| UB sub-class | CBMC obligation | downstream asset | finder template |
|--------------|-----------------|------------------|-----------------|
| spatial memory (OOB r/w) | `--bounds-check` | A1→A3/A2 (exploit primitive) | count/index, skb_field, tlv |
| temporal (UAF) / null deref | `--pointer-check` | A1/A3 ; A4 (oops) | (UAF design docs) |
| **signed/unsigned overflow** | `--signed/unsigned-overflow-check` | **A1 (wrong size/index) or miscompile** | `decoded_len_arith_overflow` |
| **division / modulo by zero** | `--div-by-zero-check` | **A4 (oops / DoS)** | *(to derive)* |
| **oversized / negative shift** | `--undefined-shift-check` | correctness → A1 | *(to derive)* |
| **uninitialised read** | modelled (see `infoleak_test.c`) | **A2 (info leak)** | `infoleak_uninit_to_user` |
| invalid pointer arithmetic | `--pointer-overflow-check` | A1 | *(to derive)* |
| lossy conversion / enum range | `--conversion-check` / `--enum-range-check` | A1 (truncated size) | *(to derive)* |

So the answer to "where do integer overflow / div-by-zero fit": they are
**A1 sub-classes**, each with a ready CBMC obligation (verified in
`ub_test.c`: div-by-zero, signed overflow, and undefined shift all FAIL on
the attacker-operand case and pass once guarded).  Their *finder* templates
are mostly still to derive — but the proof half already exists in CBMC, and
the actor gate (which operand is attacker-controlled?) is the same taint
analysis A1-memory-safety already uses.  `decoded_len_arith_overflow` is in
fact an arithmetic-overflow (UB) finder already; div-by-zero and shift
finders are cheap to add for the same reason.

## Actor taxonomy

* **Attacker-controlled input** — syscall args, `copy_from_user`, netlink
  (`nla_*`), `skb->data`, ioctl, device/DMA, firmware responses.
* **Trusted kernel state** — validated config, internal invariants.
* **Concurrent actors** — other CPUs, softirq/IRQ handlers, timers,
  RCU readers, DMA engines (TOCTOU / data-race threats).
* **Privilege boundaries** — user vs kernel, unprivileged vs `CAP_*`,
  network namespace, container.

The actor analysis is *which parameter / global carries which actor's
data* — exactly what the taint layer (`KernelTaint`) and the precondition
analysis (`caller_precondition.ql`) already encode for A1.

## Threat → (finder template, CBMC obligation) map

| asset | finder template | CBMC obligation | status |
|-------|-----------------|-----------------|--------|
| A1 (memory safety) | `tainted_count_into_fixed_array`, `decoded_len_arith_overflow`, `skb_field_before_lencheck`, `tlv_parse_loop*` | `--bounds-check` / `--*-overflow-check` (shape / verbatim / cover-probe) | **mature** |
| A1 (other UB: div0, shift, conv) | **`tainted_ub_arith.ql`** (div/mod + shift; same taint actor-gate) | `--div-by-zero-check` / `--undefined-shift-check` / `--conversion-check` (ready; see `ub_test.c`) | **finder + obligation ready** |
| A2 | **`infoleak_uninit_to_user.ql`** (NEW) | all copied bytes initialised (`infoleak_test.c`) | **prototype** |
| A3 | (to derive) write to a `*_ops`/function-pointer field from tainted data | post-write the pointer is among the legitimate set | gap |
| A4 | **`av_unbounded.ql`** (taint-reachable loop bound / alloc size) | loop terminates within K (`--unwinding-assertions`) / size ≤ K (`av_test.c`) | **finder + obligation** |
| A5 | **`auth_missing_capable.ql`** (privileged sink not dominated by a `capable()` check; reuses the dominance relation) | privileged action ⇒ capability held (`auth_test.c`) | **finder + obligation** |

## The first top-down template: A2 confidentiality (info leak)

`infoleak_uninit_to_user.ql` — a LOCAL (stack) struct/array whose address
is handed to a kernel→user/wire copy sink (`copy_to_user`, `nla_put`,
`skb_put_data`) with **no zeroing memset** of that local.  Struct padding
and unset fields then leak leftover kernel stack.  CBMC obligation
(`infoleak_test.c`): every byte of the copied region is initialised —
`reply_buggy` FAILED (padding leak), `reply_fixed` (memset) SUCCESSFUL.

On broad-next-db (net/) the template finds **34 candidates** the
memory-safety family would never flag — netlink fill/dump handlers
(`cgw_put_job`, `br_fill_info`, `__build_packet_message`, `__fifo_dump`).

## Worked threat model: net/can/gw.c

The CAN-gateway code, analysed top-down, has **two** distinct assets at
risk — and our prior (memory-safety-only) lens saw just the first:

**Actors.** A `CAP_NET_ADMIN` user creates/dumps gateway rules over
RTNETLINK; rule fields (`cgw_csum_*` indices, mod frames) are
attacker-controlled netlink payload.

* **A1 memory safety** — `cgw_csum_crc8_pos` writes
  `cf->data[crc8->result_idx]` with a raw `__s8` index.
  *Finder:* `tainted_count_into_fixed_array` (HIGH/WRITE).
  *CBMC:* verbatim `real_cgw_csum.c` → REAL-OOB in isolation.
  *Mitigation:* `cgw_chk_csum_parms` validates the indices at the producer
  → `caller_precondition.ql` = PRODUCER-GUARDED. **Discharged.**

* **A2 confidentiality** — `cgw_put_job` builds a local
  `struct cgw_frame_mod mb;`, fills `mb.cf` and `mb.modtype`, then
  `nla_put(skb, …, sizeof(mb), &mb)` with **no memset**.  Any padding in
  `cgw_frame_mod` leaks leftover kernel stack into the netlink reply read
  back by the user.
  *Finder:* `infoleak_uninit_to_user.ql` (flags it).
  *CBMC:* the `infoleak_test.c` obligation (all copied bytes initialised).
  *Mitigation:* a dominating `memset(&mb, 0, sizeof(mb))`. **Open** until
  the obligation is discharged on the real struct layout.

The memory-safety lens (all of our prior work) **never raised A2** for this
file — the same code, a different asset, a different threat, a different
template, a different CBMC obligation.  That is the point of the pivot.

## Coverage gaps (templates to derive next)

* **A3 CFI / integrity** — tainted write into a function-pointer / `*_ops`
  field (the UAF→vtable-hijack class the talk described); obligation: the
  pointer stays within the legitimate set.
* **A4 availability** — attacker-controlled loop bound / allocation size;
  obligation: termination / `size ≤ K` (CBMC unwinding assertions already
  give the loop half).
* **A5 authorization** — state-changing path lacking a dominating
  `capable()` / policy check; obligation reuses the precondition-dominance
  machinery, now over capability guards instead of length guards.
* **Concurrency** — TOCTOU between a check and use across actors
  (IRQ/other-CPU); obligation: the invariant holds under interleaving.

## Files

* `infoleak_uninit_to_user.ql` — A2 confidentiality finder template.
* `infoleak_test.c` — the A2 CBMC obligation (buggy/fixed).
* `ub_test.c` — A1 non-memory UB obligations (div-by-zero, signed
  overflow, undefined shift) discharged by CBMC's per-class checks.
* `av_unbounded.ql` / `av_test.c` — A4 availability finder + obligation
  (loop termination / size<=K); 47 loop candidates on broad-next-db (ceph
  count-loop decoders, br IGMP/MLD reports, CAN bcm/cgw loops).
* `auth_missing_capable.ql` / `auth_test.c` — A5 authorization finder +
  obligation (privileged action ⇒ capability held); 9 candidates on each
  of broad-next-db and rc7-db (`devinet_ioctl`/`dev_change_flags`, bridge
  promisc ops, `call_usermodehelper`).
* `tainted_ub_arith.ql` — A1 non-memory UB finder (tainted divisor /
  shift amount); 10 candidates on broad-next-db (`__qdisc_calculate_pkt_len`
  div+mod, `mldv2_mrd`, `rate_idx_match_*` shifts).
