# Phase 3 closeout — concurrency siblings + BPF investigation

This iteration's roadmap was: netlink v3 (already landed in
the previous session), then concurrency siblings and the BPF
follow-up.  Outcomes:

## Delivered

### Two new concurrency-using modules

1. **`concurrent_double_put`** — refcount race.  Two threads
   each call an "if-live-then-put" sequence on the same shared
   object.  Without atomic check-and-decrement, both reads of
   the live state see "still live" before either decrement
   runs.  CBMC's concurrent symex finds the bug interleaving;
   the fix uses `__CPROVER_atomic_begin/_end` to model an
   atomic guard.

2. **`tocttou_inode_check`** — generic check-then-act TOCTOU.
   A thread reads shared state, decides based on the read,
   and acts; a concurrent mutator can change the state in
   between.  Three contracts model `__tic_check` (pure read),
   `__tic_act` (requires captured value still matches), and
   `__tic_change` (mutator).

Both modules use the **single-counter ghost shape** (the
fourth shape we landed via `rcu_critical_section`) with
**concurrent execution** on top.  Each module's harness has
~25 ms CBMC time per direction (vuln + fix) — the same
order of magnitude as `concurrent_pointer_publish`.

### CVE coverage

The `race_or_toctoue` bucket from the 2023-2026 CVE survey
contained **232 CVEs (2.7% of classified volume)**.  Until
this iteration, only `concurrent_pointer_publish` addressed
this category, and only the publish-before-init subset.
`concurrent_double_put` adds the refcount-race subset; and
`tocttou_inode_check` adds the generic check-then-act subset.

These three modules together cover the dominant subshapes
within the 232-CVE bucket.  The Phase 3 effort that started
with `netlink_attr_validation` (649 OOB CVEs) and the v2/v3
refinements has now also addressed the next-largest non-
balance category (`race_or_toctoue`).

### BPF front-end investigation: not present in this checkout

The Phase 3 list mentioned `bpf_helper_arg_validation` as a
candidate, predicated on the BPF front-end CBMC has from
other work being available.  After investigating:

- No BPF-related options in `goto-cc` / `cbmc` / `goto-instrument`.
- No BPF source code in `src/` or `jbmc/src/`.
- No BPF-named branches across the 12 configured remotes
  (LAJW, NathanJPhillips, bschiff, danpoe, github (diffblue),
  markrtuttle, mrtuttle, origin (tautschnig), polgreen,
  romainbrenguier, rurban, smowton, thk123, zhixing-xu).
- The only BPF artefact in the tree is a single
  `bpf-faulty.i` preprocessed C file at the repo root,
  apparently from a sister `brimstone.git` tree.  Not a
  front-end.

The user's note that "in other work we have a BPF front-end
for CBMC" is true but applies to a separate repository or
unmerged branch.  `bpf_helper_arg_validation` is therefore
**deferred until the BPF front-end is integrated** into this
checkout, or until the bug class is recast as a regular
C-source-code module on the helper-function bodies (which
loses the BPF-instruction-level precision).

### v4 netlink cast-then-read: deferred

The other roadmap item was `nla_data(attr)` cast-then-read
shapes — code that bypasses `nla_get_uX` and casts
`nla_data(attr)` to a struct pointer.  Catching this shape
needs either:

* A contract on `nla_data` itself, with a return-type-aware
  size requirement (CBMC's contract language doesn't
  naturally express "the size required by my caller's cast").
* A goto-instrument pass that intercepts pointer casts of
  `nla_data` returns.

Both are substantial enough to warrant their own session.
Deferred.

## Catalog total

The CBMC property-module catalog now stands at **twenty-three**:

* aead, page_provenance/scatterlist, pipe_buffer (May 2026)
* cred_lifetime, lock_state, refcount_lifetime, alloc_tag,
  kobject_lifetime (May 2026 follow-ups)
* device_lifetime, of_node_lifetime, inode_lifetime,
  dentry_lifetime, fput_lifetime, sock_lifetime, skb_lifetime,
  module_lifetime (Phase 1)
* kref_lifetime, rcu_critical_section, cancel_work_before_free
  (Phase 2)
* netlink_attr_validation [v1+v2+v3] (Phase 3a)
* concurrent_pointer_publish (Phase 3b)
* **concurrent_double_put, tocttou_inode_check** (this iteration)

By CVE-mention proxy, the catalog now covers an estimated
**45-50% of the bug-shape volume** in the 2023-2026 kernel
CVE record (up from ~42-47% after Phase 3b).  The marginal
gain from this iteration's two modules is roughly the
refcount-race + TOCTOU subsets of the 232-CVE
`race_or_toctoue` bucket — perhaps ~150 CVEs in scope.

## Ghost-shape distribution

The catalog still spans **five distinct ghost shapes**:

| Shape | Modules | Concurrency-using? |
|---|---|---|
| Per-pointer balance | 12 modules (cred / kobject / refcount / device / of_node / inode / dentry / fput / sock / skb / module / kref) | no |
| Per-pointer flag | 5 modules (page_provenance / pipe_buffer / lock_state / alloc_tag / cancel_work_before_free) | no |
| Single global counter | 4 modules (rcu_critical_section / concurrent_pointer_publish / concurrent_double_put / tocttou_inode_check) | 3 of 4 use concurrency |
| Per-pointer integer | 1 module (netlink_attr_validation) | no |
| Per-pointer multi-bit composite | 1 module (aead, composes page_provenance + scatterlist) | no |

The single-global-counter shape has now produced four
modules across very different bug classes — RCU section
depth, publish-before-init, double-put race, TOCTOU — with
three of the four using CBMC's concurrent execution model.
This validates the shape as a robust pattern for
concurrency-related properties.

## Honest limitations and caveats

1. **All three concurrency modules use integer ghost state**,
   not real shared pointers.  CBMC's pointer-concurrency
   model is unsound (refuses to prove anything for shared-
   pointer programs).  The modules faithfully capture
   write-ordering / mutation-ordering bug classes; they do
   not directly model the literal pointer races, which the
   cocci prefilters surface for hand triage.

2. **Per-file synthesis is intentionally NOT supported** for
   any of the three concurrency modules.  Concurrent
   reasoning across arbitrary kernel functions doesn't fit
   the per-file model directly — extending it would need
   per-function thread-spawn synthesis, which is a separate
   research project.  Cocci is the primary integration path
   for surfacing real-kernel candidates.

3. **Sequential consistency only.**  ARM/POWER weak-memory
   model bugs that need `--mm tso` etc. are out of scope.
   Most kernel race fixes assume SC under barriers anyway.

4. **Two-thread harnesses only.**  Multi-thread (>2)
   cross-CPU races aren't modelled.

## What's next (post-iteration roadmap)

Items still in the pipeline:

* **`netlink_attr_validation` v4** — cast-then-read for
  `nla_data`, plus auto-init for nlattr-pointer-array
  function parameters.
* **`bpf_helper_arg_validation`** — pending BPF front-end
  integration into this checkout.
* **A re-run of the 23-module corpus** across all four LTS
  kernels.  The wrapper-paths v4 fix (commit `5074b9dc20`)
  flipped a class of false positives, the netlink v3 shim
  refined another, and the three new concurrency modules
  add new dimensions.  A corpus re-run would measure how
  the verdict landscape has shifted.

Earlier deferred items still in scope:

* **Concurrency for `rcu_critical_section`**: per-CPU depth
  ghost so the property holds across thread interleavings.
  The current single-global model is process-local.
* **Sibling cancel-before-free modules** for
  `delayed_work` and `timer_list`.
* **More balance modules** for less-cited APIs from the
  Tier 1 list (pid, key, dst, netdev, css, clk,
  pm_runtime).  Diminishing returns at this point — most
  remaining APIs have single-digit CVE-mention counts.

## Reproducing

```sh
# concurrent_double_put vuln:
build/bin/goto-cc \
  integration/linux/properties/concurrent_double_put/concurrent_double_put.c \
  integration/linux/scan/adapters/concurrent_double_put_kernel_adapter.c \
  integration/linux/scan/adapters/concurrent_double_put_kernel_direct_harness.c \
  -o /tmp/cdp.gb
build/bin/goto-instrument \
  --replace-call-with-contract __cdp_get \
  --replace-call-with-contract __cdp_put \
  /tmp/cdp.gb /tmp/cdp.inst
build/bin/cbmc /tmp/cdp.inst    # VERIFICATION FAILED (race detected)

# tocttou_inode_check vuln:
build/bin/goto-cc \
  integration/linux/properties/tocttou_inode_check/tocttou_inode_check.c \
  integration/linux/scan/adapters/tocttou_inode_check_kernel_adapter.c \
  integration/linux/scan/adapters/tocttou_inode_check_kernel_direct_harness.c \
  -o /tmp/tic.gb
build/bin/goto-instrument \
  --replace-call-with-contract __tic_check \
  --replace-call-with-contract __tic_act \
  --replace-call-with-contract __tic_change \
  /tmp/tic.gb /tmp/tic.inst
build/bin/cbmc /tmp/tic.inst    # VERIFICATION FAILED (race detected)
# Add -DFIXED for the safe shape; expect VERIFICATION SUCCESSFUL.
```

## Cross-references

- [CVE survey](cve-survey-2023-2026.md) — prioritisation
  evidence: `race_or_toctoue` 232 CVEs.
- [Phase 3a netlink v1+v2](phase-3a-netlink-2026-05.md)
- [Phase 3b concurrency](phase-3b-concurrency-2026-05.md)
- [Phase 3a netlink v3](phase-3a-netlink-v3-2026-05.md)
