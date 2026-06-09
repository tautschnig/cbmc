# Plan: a kernel taint-source annotation layer for the ABC oracles

**Date:** 2026-06-09
**Status:** DESIGN ONLY — no implementation in this document.
**Why now:** the precision work (`broad-sweep-triage-2026-06.md`) and the
recall measurement (`oracle-recall-2026-06.md`) both bottomed out on the
*same* unanswerable structural question — **which data is
attacker-controlled?** A taint layer answers it once, for all seven
oracles.

## 1. Motivation — the semantic-trust ceiling

Two concrete dead-ends that are really one problem:

* **Precision (FP).**  `tainted_count_into_fixed_array` flags both
  `link_conf[link_id]` where `link_id = params->beacon.link_id`
  (nl80211-validated → FP) and `targets[n_targets]` where
  `n_targets = ntf->...` (wire data → real).  Structurally identical
  (`local = ptr->field`); only the *trust of the source struct* differs.
* **Recall (FN).**  ceph `process_message_header` /
  `ceph_handle_auth_reply` (CVE-2026-43406/-43407) parse via
  `ceph_decode_32(&p)` over `(void *p, void *end)` — missed because the
  oracles key on `skb->data` / byte-order builtins, not on *the fact
  that `p` is attacker-controlled*.

Both vanish if the oracle can ask: *does this index / size / cursor
derive (interprocedurally) from a taint source, and has it passed a
sanitizer?*  That is a dataflow question CodeQL answers natively.

## 2. Taint sources (what to annotate)

A single `KernelTaint.qll` library enumerating the kernel's
attacker-controlled entry points.  Tiered so we can ship value
incrementally:

**Tier 1 — wire/user buffers (highest yield, covers the recall gaps):**
* `copy_from_user` / `_copy_from_user` / `get_user` destinations & sizes;
* `skb->data`, `skb->head`, `skb_header_pointer` returns;
* netlink: `nla_data`, `nla_get_u8/u16/u32/u64/be*/le*`, `nlmsg_data`;
* ceph: `ceph_decode_8/16/32/64`, `ceph_decode_*`, and the `(void *p,
  void *end)` cursor params they advance;
* byte-order/​unaligned reads off any tainted pointer: `get_unaligned*`,
  `be*_to_cpu`/`le*_to_cpu`, `__builtin_bswap*` (already in
  `decoded_len`).

**Tier 2 — firmware / device / DMA (driver attack surface):**
* `request_firmware(...)->data`, `->size`;
* `memcpy_fromio` / `ioread*` / readl-style MMIO reads;
* descriptor/ring buffers populated by the device (harder; per-driver).

**Tier 3 — other user ingress:**
* sysfs/debugfs/proc `store`/`write` handler `buf`/`count` params;
* ioctl `arg` after `copy_from_user`;
* on-disk filesystem structures (superblock/inode fields read from the
  block device) — for fs parsers (ntfs3, erofs, ext4 extent trees).

Each source is a `DataFlow::Node` predicate.  Sources deliberately do
**not** include validated/in-kernel structs (nl80211 `params`,
`cfg80211_*`), which is exactly what makes `params->beacon.link_id`
*not* tainted while `ntf->n_targets` (filled from an skb) *is*.

## 3. Propagation

* Use CodeQL **`TaintTracking::Global`** (already used by
  `tainted_into_fixed_dest`).  Global = interprocedural, so a length
  decoded in `parse_dcc()` and used in the caller is tracked — this is
  what lifts the oracles out of their current *function-granularity*
  recall limitation (the `tlv_parse_loop_helper`/`skb_field` FP noise).
* Taint flows through: assignments, arithmetic (incl. the round-up /
  multiply the `decoded_len` oracle cares about), struct field
  stores/loads, and `memcpy`/`memmove` into a buffer.
* Field-sensitivity: track taint to specific fields where feasible
  (`ntf->n_targets`) to avoid over-tainting whole structs.

## 4. Sinks (= the existing oracles' dangerous-use points)

No new sinks — the seven oracles already locate them; taint just gates
them:
* array subscript / fixed-array write (count/index);
* copy size argument (tainted_into_fixed_dest, alloc_overflow);
* loop bound driving a buffer walk (tlv loops);
* round-up / multiply feeding a bound or advance (decoded_len);
* field read at an offset before a length check (skb_field).

## 5. How each oracle consumes taint (recall ↑ and precision ↑)

| Oracle | Taint use | Effect |
|--------|-----------|--------|
| tainted_count_into_fixed_array | require index/count be taint-reachable | **kills `link_id`-from-`params` FP** (params untainted); keeps `ntf->n_targets` |
| skb_field_before_lencheck | source = any tainted cursor, not just `skb->data` | **catches ceph `process_message_header`**; drops reads of untainted buffers |
| decoded_len_arith_overflow | decode-source set includes `ceph_decode_*`, `nla_get_*` | **catches `ceph_handle_auth_reply`** + nlattr parsers |
| tlv_parse_loop(+helper) | require the walked `buf` be tainted | removes TX-writer FPs (`mptcp_write_options`, `l2cap_*_send` write kernel-built buffers, untainted) |
| tainted_into_fixed_dest | broaden sources beyond `copy_from_user` params | catches skb/nla-sized copies it currently misses |
| tainted_alloc_overflow | size taint-reachable | fewer benign-constant FPs |

## 6. Sanitizers / barriers (turn recall into precision)

A taint source without sanitizers over-taints (everything from `skb` is
tainted → noise).  Model validators as **`isBarrier`** nodes so a
*checked* value is no longer flagged:
* `pskb_may_pull(skb, n)` / `skb_header_pointer` success → the first `n`
  bytes are safe to read;
* `nla_validate` / `nla_parse` / policy-checked attributes;
* an explicit `if (x < CONST)` / `if (x > end) return` dominating the
  sink (the relational/mask/min guards the oracles already detect — fold
  them into the taint config as barriers);
* `ceph_decode_need(&p, end, n)` for ceph cursors.

This is the lever that makes the difference between "flags every parser"
(useless) and "flags parsers missing a specific check" (the goal).

## 7. Phased implementation

* **Phase 0 — `KernelTaint.qll`.**  Library with Tier-1 sources +
  core sanitizers (`pskb_may_pull`, relational/mask guards, decode-need).
  No oracle changes yet; unit-test the source/sanitizer predicates on the
  synthetic fixtures.
* **Phase 1 — recall win.**  Wire Tier-1 sources into
  `decoded_len_arith_overflow` (add `ceph_decode_*`/`nla_get_*`) and
  generalise `skb_field_before_lencheck` to a tainted bounded cursor.
  **Acceptance:** the two ceph CVEs (43406/43407) become HITs in the
  recall harness; existing 9/9 unchanged.
* **Phase 2 — precision win.**  Gate `tainted_count_into_fixed_array`
  and the `tlv_parse_loop` family on taint-reachability.
  **Acceptance:** `link_id`-from-`params` FPs gone; `mptcp_write_options`
  / `l2cap_*_send` TX-writer FPs gone; genuine `ntf->n_targets`,
  `cgw result_idx`, rxgk hits retained.
* **Phase 3 — sanitizer hardening.**  Add the barrier set; re-measure FP
  on the broad sweep and differential scans.
* **Phase 4 — validate.**  Re-run the recall set (#2) and the
  differential scans (rc6→rc7, next daily); report recall and FP deltas.

## 8. Effort & risk

* **Compute.**  Global taint tracking is the expensive part of CodeQL
  evaluation; on whole-subsystem DBs it can be minutes per query.
  Mitigation: scope sources/sinks tightly, run per-subsystem (the sweep
  already does), cache results.
* **Over-tainting without sanitizers.**  The dominant risk: mark `skb`
  as a source and *everything* becomes tainted → recall up but precision
  collapses.  Phase 3 (sanitizers) is therefore not optional; Phase 1/2
  must land with at least the relational/pull barriers.
* **Source-list maintenance.**  New accessors appear; the source list is
  a living artifact.  Keep it small, tiered, and documented; treat
  additions like the decode-accessor gap (#2) — driven by measured
  misses, not speculation.
* **Soundness honesty.**  Taint reachability is a heuristic for
  "attacker-controlled," not a proof; it sharpens triage, and CBMC
  stage-2 remains the adjudicator for the survivors.

## 9. Success metrics

1. **Recall:** ceph CVE-2026-43406/-43407 flagged; recall set ≥ 11/11.
2. **Precision:** `link_id` direct-index FPs (≈60 residual) and the
   TX-writer FPs eliminated *soundly* (via untaintedness, not heuristics).
3. **Differential scan:** the rc6→rc7 / next-daily candidate sets shrink
   to taint-reachable parsers only, with the known true shapes retained.
4. **No regression:** the 9/9 recall and the synthetic fixtures stay
   green.

## 10. Relationship to the rest of the pipeline

* The decode-accessor source set (Phase 1) is the **same** extension the
  recall measurement (#2) already identified — this plan subsumes it.
* Taint-reachability is the principled replacement for the ad-hoc
  precision filters (`maskGuarded`, `bareParameter`, min-clamp): those
  become *barriers/sources* in one coherent model.
* Stage-2 real-function harnessing (#1) is unchanged — taint sharpens
  *which* candidates reach CBMC; CBMC still delivers the verdict.
