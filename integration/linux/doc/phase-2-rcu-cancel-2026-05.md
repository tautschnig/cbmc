# Phase 2 outcomes — kref_lifetime, rcu_critical_section,
# cancel_work_before_free

Phase 2 of the CVE-survey-driven catalog expansion is landed.
Three new modules cover bug shapes the existing balance pattern
does NOT address.

## Delivered

### Modules

1. **`kref_lifetime`** (factory variant)

   Foundational refcount primitive that many type-specific
   lifetime modules wrap.  `kref_put(struct kref *kref,
   void (*release)(struct kref *kref))` is `static inline int`
   in `<linux/kref.h>` — a non-void return type and a
   function-pointer second argument.  The factory was extended
   to support both via two new `ModuleConfig` fields:
   `extra_params: list[tuple[str, str]]` and `return_type: str`.

   30+ direct CVE mentions of `kref_put` in the 2023-2026
   survey; transitively underlies many more bugs (every
   refcount-balance bug in a struct that uses `struct kref` is
   a candidate).

2. **`rcu_critical_section`** (hand-authored)

   Ghost shape novel to the catalog: a *single integer* counter
   `__rcu_csection_depth` rather than a per-pointer table.  The
   adapter contracts three kernel APIs:

   - `rcu_read_lock`: `assigns(__rcu_csection_depth)`;
     `ensures` depth incremented.
   - `rcu_read_unlock`: `requires` depth > 0; assigns;
     ensures depth decremented.
   - `synchronize_rcu`: `requires` depth == 0 — the bug class
     is "sleeping operation inside an RCU read-side critical
     section".

   Static-inline `rcu_read_(un)lock` get both the unmangled
   contract and the goto-cc-mangled
   `__CPROVER_file_local_rcupdate_h_*` form.

3. **`cancel_work_before_free`** (hand-authored, cocci-driven
   v1)

   Per-`work_struct *` ghost flag tracks whether work is
   pending.  Cocci prefilter does the bug-finding on real
   kernel source; CBMC validates a synthetic harness that
   exercises the bug shape.  The kfree side of the contract is
   *not* yet wired to actual kfree call sites — see "Honest
   limitations" below.

### Tooling

* **Factory enhancements**:
  - Optional `extra_params: list[tuple[str, str]]` for APIs
    that take additional arguments beyond the primary
    `<type> *<param>`.  Function-pointer types are detected
    via `(*name)` syntax and the name suffix is suppressed
    (avoiding the `void (*release)(...) release` syntax error
    we hit on the first kref-template attempt).
  - Optional `return_type: str` for APIs returning non-void.
    The harness wraps the call in `(void)<call>` so the
    return value is discarded.

  Both changes are upward-compatible: existing eight balance
  modules don't supply these fields and use the documented
  defaults (`extra_params=[]`, `return_type="void"`).

## Validation

### Unit tests
All three modules' `properties/<module>/run.sh` tests pass
(`VERIFICATION SUCCESSFUL`).

### Direct-call harnesses
- `kref_lifetime`: vuln (`init=1`, two puts) → CBMC reports
  precondition.4 FAILURE; fix (`init=2`) → SUCCESSFUL.
- `rcu_critical_section`: vuln (`synchronize_rcu` between
  `rcu_read_lock` and `rcu_read_unlock`) →
  `synchronize_rcu.precondition.1` FAILURE; fix (swap order)
  → SUCCESSFUL.
- `cancel_work_before_free`: vuln (skips
  `cancel_work_clear_pending`) →
  `__assert_no_pending_work.precondition` FAILURE; fix (calls
  it) → SUCCESSFUL.

### Existing regressions
All `scan/run.sh` (10 cases), `test-per-file.sh`,
`test-per-file-mode.sh`, `smoke-all-lts.sh` (4 LTS kernels)
regressions pass.

## Catalog total

The CBMC property-module catalog now stands at **nineteen**:

* aead, page_provenance/scatterlist, pipe_buffer (May 2026)
* cred_lifetime, lock_state, refcount_lifetime, alloc_tag,
  kobject_lifetime (May 2026 follow-ups)
* device_lifetime, of_node_lifetime, inode_lifetime,
  dentry_lifetime, fput_lifetime, sock_lifetime, skb_lifetime,
  module_lifetime (Phase 1)
* **kref_lifetime, rcu_critical_section,
  cancel_work_before_free** (this phase)

By CVE-mention proxy, the catalog now covers an estimated
35-40% of the bug-shape volume in the 2023-2026 kernel CVE
record (up from ~30-35% after Phase 1).

## Ghost shapes covered

| Shape | Modules | Bug class |
|---|---|---|
| Per-pointer balance | cred, refcount, kobject, device, of_node, inode, dentry, fput, sock, skb, module, kref | UAF / refcount imbalance |
| Per-pointer flag | pipe_buffer, lock_state, page_provenance, alloc_tag, cancel_work_before_free | shape-specific invariants |
| Single global counter | rcu_critical_section | RCU CS depth |
| Per-pointer multi-bit | aead (composes page_provenance + scatterlist) | crypto API |

Three of these shapes were new before Phase 2 / Phase 1; the
**single global counter** shape introduced by
`rcu_critical_section` is genuinely novel and reusable for
future modules (e.g. preempt-disable depth, irq-disable
state).

## Honest limitations

Three are explicitly recorded in the modules' READMEs:

1. **`cancel_work_before_free` does not yet verify real
   kernel functions.**  The synthetic harness exercises the
   bug shape; the cocci prefilter finds candidate sites in
   real source.  But there's no automatic connection from
   real `kfree(x)` call sites in kernel TUs to the
   `__assert_no_pending_work` checkpoint — that needs either
   per-type contracts (one per struct that contains a
   `work_struct`) or a goto-instrument pass that auto-injects
   the checkpoint before each `kfree(x)` where `x`'s type
   contains a `work_struct` field.  The cocci prefilter is
   the primary integration path for now.

2. **`rcu_critical_section` does not cover `rcu_dereference`
   outside an RCU CS.**  `rcu_dereference` is a macro chain
   ending in `__rcu_dereference_check`; contracting it cleanly
   requires either macro-aware contract installation (CBMC
   doesn't have this) or a goto-instrument pass.  Deferred.

3. **`rcu_critical_section`'s depth ghost is process-local.**
   Multi-threaded RCU reasoning would need a per-CPU ghost and
   CBMC's multi-thread mode (slow but tractable per the user's
   note).  Phase 3 candidate.

## What's next (Phase 3)

The remaining three Phase-2 list candidates from the CVE
survey, all bigger commitments than this session:

1. **`netlink_attr_validation`** — bounds and presence checks
   on `nla_data` / `nla_get_*` returns.  Targets `out_of_bounds`
   in `net/netlink/` and `net/netfilter/`.  Needs a size-tainted
   ghost.

2. **`concurrent_pointer_publish`** — pointer published to a
   shared structure before fully initialised.  Uses CBMC's
   concurrency support; needs benchmarking against the per-
   file budget (CBMC concurrency is slow).

3. **`bpf_helper_arg_validation`** — argument-bounds
   preconditions on BPF helpers like `bpf_skb_check_mtu`.
   Depends on the BPF front-end CBMC has from other work being
   available in this checkout.

Plus follow-ups specific to Phase 2:

4. **Real-kernel wiring for `cancel_work_before_free`.**
   Either per-type contracts (auto-generated from cocci hits)
   or a goto-instrument pass that auto-injects checkpoints.

5. **Sibling modules to `cancel_work_before_free`**:
   - `cancel_delayed_work_before_free` (`struct delayed_work`)
   - `del_timer_sync_before_free` (`struct timer_list`)
   - `flush_workqueue_before_destroy`

6. **Concurrent `rcu_critical_section`**: per-CPU depth ghost
   so the property holds across thread interleavings.

## Reproducing

```sh
# Generate a kref-style module (factory):
./integration/linux/scan/balance_module_factory.py \
  --module kref_lifetime

# Hand-author a non-balance module: see the rcu_critical_section
# directory for the structure (header, .c, .cocci, test_unit,
# run.sh, README, plus three adapters).

# Validate:
./integration/linux/properties/kref_lifetime/run.sh
./integration/linux/properties/rcu_critical_section/run.sh
./integration/linux/properties/cancel_work_before_free/run.sh
```

## Cross-references

- [CVE survey](cve-survey-2023-2026.md) — prioritisation
  evidence.
- [Phase 1 outcomes](phase-1-balance-modules-2026-05.md) —
  factory + 8 balance modules.
- [`balance_module_factory.py`](../scan/balance_module_factory.py)
  — extended this phase.
