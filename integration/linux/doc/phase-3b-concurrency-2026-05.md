# Phase 3 outcomes — netlink v2 + concurrent_pointer_publish

This iteration delivered both Phase 3 follow-ups in a single
session:

1. **`netlink_attr_validation` v2** — closes the dominant
   false-positive class of v1 by modelling `nla_parse`.
2. **`concurrent_pointer_publish`** — the catalog's first
   concurrency module, targeting the 232-CVE
   `race_or_toctoue` bucket.

## Delivered

### `netlink_attr_validation` v2

#### What v1 didn't cover

The v1 module contracted `nla_get_uX` accessors to require a
ghost `validated_min_size >= sizeof(uX)`.  But it didn't model
`nla_parse` itself: in real kernel handlers like
`ctnetlink_parse_filter` that call `nla_parse_nested(tb, ...)`
and then `nla_get_u32(tb[X])`, the harness initialised each
`tb[i]` with `validated_min_size = 0`, the `nla_parse` call
left the harness's ghost untouched, and the subsequent
`nla_get_u32` fired the contract precondition spuriously.

#### v2 fix: shim definition of `__nla_parse`

The kernel's `__nla_parse` is declared in `<net/netlink.h>`
with its body in `lib/nlattr.c`.  Our scan path doesn't
compile `lib/nlattr.c`, so the symbol is unresolved at link
time.

The v2 module's `.c` file now provides a strong definition of
`__nla_parse` (and `__nla_validate`).  CBMC's linker resolves
the unresolved external to our shim; CBMC's symex executes
the shim when reaching any `nla_parse(...)` call (or any of
its inline-wrapper relatives — `nla_parse_nested`,
`nla_parse_strict`, `nla_parse_deprecated`, etc., which all
call `__nla_parse` underneath).

The shim:
* Iterates `tb[0..maxtype]` and marks every non-NULL entry as
  validated to 8 bytes (over-approximation that covers
  `nla_get_u8/u16/u32/u64`).
* Is **manually unrolled to depth 32** to avoid CBMC unwind
  issues at the per-file scan's typical `--unwind 2` setting.
  Real-world netlink protocols (NFTA_*_MAX etc.) typically
  cap at less than 32 in 6.12.
* Returns 0 (success).

#### Honest limitations of v2

* **Type-mismatch shapes** (`NLA_U32` policy read as
  `nla_get_u64`) still not caught — the shim
  over-approximates to size 8 regardless of the policy.
  Catching type-mismatch needs policy-aware contracts that
  read `policy[].type`.  Deferred to v3.
* **`nla_data(attr)` cast-then-read** bypasses the typed
  accessors; not contracted.
* **Functions that take `tb[]` as a parameter** without
  parsing internally still see uninit ghost.  Wrapper-paths
  auto-init for nlattr-pointer arrays would address this.
* **Reads of `tb[i]` for `i >= 32`** are outside the
  unrolling bound.

#### Validation

* `TEST_NLA_PARSE_SHIM` direct-call harness: populate
  `tb[0]` and `tb[3]`, call `__nla_parse(tb, 5, ...)`, then
  `nla_get_u32(tb[0])` and `nla_get_u32(tb[3])` —
  `VERIFICATION SUCCESSFUL`.
* Original vuln/fix harnesses still produce the expected
  `FAILED` / `SUCCESSFUL`.

### `concurrent_pointer_publish`

#### What this module catches

Producer thread publishes a "ready" / "valid" / "active" flag
before the data the flag advertises is initialised.  A
concurrent reader can witness `ready=1` but read uninitialised
data.

This is **the catalog's first module using CBMC's concurrent
execution model**.  The harness uses CBMC's `__CPROVER_ASYNC_1`
label to spawn a writer thread; `main` runs as the reader.
CBMC's concurrent symex explores all interleavings of the
writer's writes and the reader's reads.

#### CBMC concurrency caveat: pointers are unsound

CBMC warns `"pointer handling for concurrency is unsound"`
and refuses to prove anything when shared *pointers* are
involved in a concurrent program.  We sidestep this by
modelling the published / initialised state with **two
integer flags** rather than the actual shared pointer.

The bug shape is a **write-ordering question** — whether
`published` becomes `1` before `initialised` does — and that
question is faithful regardless of the value's type.  Cocci
flags real-kernel pointer-publish call sites; the synthetic
harness validates the abstract bug shape on integers.

#### Validation

* Vuln direct-call harness (publish before init): CBMC found
  an interleaving where the reader sees `published=1` with
  `initialised=0`; `__cpp_assert_safe_to_read.precondition.1
  FAILURE`; `VERIFICATION FAILED`.  Per-scan time: **~20 ms**.
* Fix direct-call harness (init before publish): no such
  interleaving; `VERIFICATION SUCCESSFUL`.  Same time.

#### Honest limitations

* **Real pointer publication** is flagged by cocci but
  validated only on the abstract integer-flag model.
* **Memory-ordering models beyond sequential consistency**
  (ARM/POWER weak memory) are out of scope.
* **Multi-thread (>2) cross-CPU races** are out of scope.
  The harness has one writer and one reader.
* **Per-file synthesis** is intentionally NOT supported —
  the per-file harness model doesn't express concurrent
  execution.  Cocci is the primary integration path for
  real kernel source.

## Catalog total

The CBMC property-module catalog now stands at **twenty-one**:

* aead, page_provenance/scatterlist, pipe_buffer (May 2026)
* cred_lifetime, lock_state, refcount_lifetime, alloc_tag,
  kobject_lifetime (May 2026 follow-ups)
* device_lifetime, of_node_lifetime, inode_lifetime,
  dentry_lifetime, fput_lifetime, sock_lifetime, skb_lifetime,
  module_lifetime (Phase 1)
* kref_lifetime, rcu_critical_section, cancel_work_before_free
  (Phase 2)
* netlink_attr_validation (Phase 3a)
* **concurrent_pointer_publish** (Phase 3b)

## Ghost shape categorisation

The catalog now spans **five distinct ghost shapes**:

| Shape | Modules using it | Bug class examples |
|---|---|---|
| Per-pointer balance | cred / kobject / refcount / device / of_node / inode / dentry / fput / sock / skb / module / kref | refcount UAF, double-put |
| Per-pointer flag | page_provenance / pipe_buffer / lock_state / alloc_tag / cancel_work_before_free | shape-specific invariants |
| Single global counter | rcu_critical_section | RCU CS depth |
| Per-pointer integer | netlink_attr_validation | size-tainted bounds |
| **Two integer globals + concurrent execution** | **concurrent_pointer_publish** | publish-before-init races |

## Coverage proxy

By CVE-mention proxy, the catalog covers an estimated
**42-47% of the bug-shape volume** in the 2023-2026 kernel
CVE record (up from ~40-45% after Phase 3a).  The marginal
gain from concurrent_pointer_publish is roughly 232 CVEs in
the `race_or_toctoue` bucket; the v2 netlink work doesn't
add new CVE-volume coverage but reduces the false-positive
rate on the already-covered 649 CVEs in `out_of_bounds`.

## What's next

The remaining Phase 3 candidate from the original survey:

* **`bpf_helper_arg_validation`** — argument-bounds
  preconditions on BPF helpers like `bpf_skb_check_mtu`.
  Targets `bpf_verifier_or_runtime` (28 CVEs).  Still
  requires investigation: the BPF front-end CBMC has from
  other work would need to be wired into this checkout
  before this is tractable.

Plus the natural follow-ups for the modules just landed:

* **`netlink_attr_validation` v3**: policy-aware contracts
  that read `policy[].type` and translate to per-`tb[i]`
  minimum sizes.  Catches type-mismatch bugs (NLA_U32 read
  as nla_get_u64).

* **`concurrent_double_put`** sibling: race on a refcount
  decrement (two threads both call `put_x(p)` on the same
  shared pointer).  Same concurrency primitive; different
  ghost (refcount race).

* **`tocttou_inode_check`** sibling: `f_op->open` checks
  `inode->i_state` and acts on it but the inode state can
  change between check and act.  Concurrency module
  variant.

## Reproducing

```sh
# v2 netlink shim test:
build/bin/goto-cc -DTEST_NLA_PARSE_SHIM \
  integration/linux/properties/netlink_attr_validation/netlink_attr_validation.c \
  integration/linux/scan/adapters/netlink_attr_validation_kernel_adapter.c \
  integration/linux/scan/adapters/netlink_attr_validation_kernel_direct_harness.c \
  -o /tmp/v2.gb
build/bin/goto-instrument --replace-call-with-contract nla_get_u32 \
  /tmp/v2.gb /tmp/v2.inst
build/bin/cbmc /tmp/v2.inst    # VERIFICATION SUCCESSFUL

# concurrent_pointer_publish vuln:
build/bin/goto-cc \
  integration/linux/properties/concurrent_pointer_publish/concurrent_pointer_publish.c \
  integration/linux/scan/adapters/concurrent_pointer_publish_kernel_adapter.c \
  integration/linux/scan/adapters/concurrent_pointer_publish_kernel_direct_harness.c \
  -o /tmp/cpp.gb
build/bin/goto-instrument --replace-call-with-contract __cpp_assert_safe_to_read \
  /tmp/cpp.gb /tmp/cpp.inst
build/bin/cbmc /tmp/cpp.inst   # VERIFICATION FAILED (race detected)
# Add -DFIXED for the safe shape; expect VERIFICATION SUCCESSFUL.
```

## Cross-references

- [CVE survey](cve-survey-2023-2026.md) — prioritisation
  evidence: `out_of_bounds` 649 CVEs, `race_or_toctoue` 232
  CVEs.
- [19-module corpus run](corpus-2026-05-19modules.md) —
  Phase-3-decision evidence.
- [Phase 1](phase-1-balance-modules-2026-05.md) — factory + 8
  balance modules.
- [Phase 2](phase-2-rcu-cancel-2026-05.md) — kref, RCU,
  cancel-before-free.
- [Phase 3a](phase-3a-netlink-2026-05.md) — first netlink
  module + wrapper-paths v4.
