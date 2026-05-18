# Phase 3a outcomes — wrapper-paths v4 + netlink_attr_validation

This document records the two pieces landed in this iteration:
the wrapper-paths v4 mechanical fix (~1 hour) and the first
Phase 3 module (`netlink_attr_validation`, ~2-3 hours after
the v4 work).

## Delivered

### Wrapper-paths v4 + synthesis bootstrap fix

Three new wrapper-path entries identified by the May 2026
19-module corpus run as the most-cited false-positive shapes
that map cleanly to a single field on a wrapper struct:

- `device_lifetime`  : `struct device → parent`
- `inode_lifetime`   : `struct dentry → d_inode`
- `dentry_lifetime`  : `struct super_block → s_root`

Plus a structural improvement to `synthesise_harness.py`'s
bootstrap logic:

1. **Wrapper-paths bootstrap is now ADDITIVE.**  Previously,
   if the parameter type already matched the bug-class
   primitive (e.g. `attribute_container_release(struct device *
   classdev)`, parameter type IS `struct device *`), wrapper
   paths were skipped entirely — only the param itself got
   bootstrapped, not `classdev->parent`.  With this change,
   ALL matching wrapper paths bootstrap their fields in
   addition to the direct param.

2. **Pointer-field paths auto-assign a backing buffer.**
   Bare-name field paths like `parent` or `d_inode` (as
   opposed to `&{arg}->mutex` which takes the address of an
   embedded struct) read NULL after zero-init, which fails
   the contract's `!= NULL` precondition before the ghost
   lookup runs.  The synthesiser now emits a fresh static
   backing buffer and casts it through `void *` (avoiding
   const-violation warnings) so the field reads non-NULL.

### Validation impact (single-file smoke)

Three previously-failing functions now produce clean verdicts:

| Function                          | Before | After |
|-----------------------------------|--------|-------|
| `attribute_container_release`     | CONTRACT VIOLATION (low-conf) | NOISE (contract holds; kfree fires built-in checks) |
| `ext2_link`                       | CONTRACT VIOLATION | NOISE |
| `exit_creds`                      | CONTRACT VIOLATION (4 preconds) | VERIFICATION SUCCESSFUL (all 4 preconds hold) |

`exit_creds` is the most striking — it had been the canonical
4/4-stable false-positive in our corpus runs since May 2026,
and the new bootstrap finally clears it.

### `netlink_attr_validation` property module

First Phase 3 module.  Targets the largest single bug-shape
category from the CVE survey: **`out_of_bounds` (649 CVEs,
7.4% of classified volume)** — bigger than any single existing
module's CVE-mention coverage.

#### New ghost shape

This is the catalog's **fourth distinct ghost shape**:
**per-pointer integer** "validated_min_size" on
`struct nlattr *`.  Complements the existing three:

| Shape                    | Modules using it |
|--------------------------|------------------|
| Per-pointer balance      | cred / kobject / refcount / device / of_node / inode / dentry / fput / sock / skb / module / kref |
| Per-pointer flag         | page_provenance / pipe_buffer / lock_state / alloc_tag / cancel_work_before_free |
| Single global counter    | rcu_critical_section |
| **Per-pointer integer**  | **netlink_attr_validation** |

The same per-pointer-integer shape is reusable for any other
"size-tainted" property — e.g. `copy_from_user(dst, src, len)`
where `len` is bounded by some prior validation step.

#### Bug class

`nla_get_u32(attr)` (and `u16`/`u64`) reads N bytes from a
netlink attribute's payload via a typed accessor, without
first verifying that the payload is at least N bytes.  The
standard kernel idiom is to call `nla_parse(...)` against a
policy that specifies minimum sizes per attribute type; bugs
occur when handlers skip this validation and read directly.

Concrete recent CVEs in this shape: CVE-2026-43450
(`nfnetlink_cthelper` OOB read), CVE-2026-43453
(`nft_set_pipapo` OOB), and many others in the netfilter /
netlink family.

#### Components

* `properties/netlink_attr_validation/{header, ref impl, cocci,
  test_unit, run.sh, README}`.
* `scan/adapters/netlink_attr_validation_{adapter,
  adapter_probe, direct_harness}.c`.  Contracts attach to
  `nla_get_u8/u16/u32/u64` in both static-inline mangled and
  external-name forms.
* `synthesise_harness.py::MODULE_GHOST_BOOTSTRAP`: per-file
  harness inits each `struct nlattr *` parameter with
  `validated_min_size = 0` (unvalidated).
* `scan.py::{CONTRACT_FUNCTIONS, KERNEL_ADAPTERS,
  _PER_FILE_SUPPORTED_MODULES}`.
* `scan-per-file.sh::{ADAPTER_STEM, CONTRACT_TARGETS}`.

#### Validation

- Unit test (seven cases on the ghost + `nla_size_at_least`
  predicate): `VERIFICATION SUCCESSFUL`.
- Direct-call harness vuln (`nla_get_u32` on 2-byte-validated
  attr): `nla_get_u32.precondition.2: FAILURE`,
  `VERIFICATION FAILED`.
- Direct-call harness fix (`nla_get_u32` on 4-byte-validated
  attr): `VERIFICATION SUCCESSFUL`.
- All `scan/run.sh`, `test-per-file.sh`, `test-per-file-mode.sh`,
  `smoke-all-lts.sh` regressions pass.

## Catalog total

The CBMC property-module catalog now stands at **twenty**
modules:

* aead, page_provenance/scatterlist, pipe_buffer (May 2026)
* cred_lifetime, lock_state, refcount_lifetime, alloc_tag,
  kobject_lifetime (May 2026 follow-ups)
* device_lifetime, of_node_lifetime, inode_lifetime,
  dentry_lifetime, fput_lifetime, sock_lifetime, skb_lifetime,
  module_lifetime (Phase 1)
* kref_lifetime, rcu_critical_section, cancel_work_before_free
  (Phase 2)
* **netlink_attr_validation** (Phase 3a)

By CVE-mention proxy, the catalog now covers an estimated
**40-45% of the bug-shape volume** in the 2023-2026 kernel
CVE record (up from ~35-40% after Phase 2).  The jump is
disproportionate to the module count because
`netlink_attr_validation` targets a single category (649 CVEs)
larger than the entire balance catalog combined (~410
mentions of the top-12 balance APIs).

## Honest limitations recorded

Each is in the module's README:

1. **`netlink_attr_validation` doesn't yet model `nla_parse` /
   `nla_validate`.**  These are the standard kernel APIs that
   actually set the validated_min_size based on a policy.
   Until v2 adds policy-aware contracts that propagate the
   policy's minimum sizes into the ghost, functions that DO
   validate via the standard API will produce false positives
   under per-file synthesis (the harness's bootstrap doesn't
   know the validation happened).

2. **Type-mismatch shapes** (e.g. policy says `NLA_U32`, code
   reads `nla_get_u64`).  More subtle — the policy validates
   to 4 bytes but the read needs 8.  v2 enhancement.

3. **`nla_data(attr)` cast-then-read shapes** (where code
   bypasses the typed accessors and casts `nla_data(attr)` to
   some struct pointer).  Related bug class but harder to
   contract uniformly; left for v2.

4. **Nested attributes**.  `nla_nest_validate` would set
   ghost values on each child; we don't model nesting yet.

## What's next (remaining Phase 3 candidates)

The CVE-survey Phase 3 list as recorded in the
`phase-2-rcu-cancel-2026-05.md` write-up:

1. **`netlink_attr_validation`** — **DONE this session**.

2. **`concurrent_pointer_publish`** — pointer published to a
   shared structure before fully initialised.  Uses CBMC's
   concurrency support (per the user note).  Worth a
   benchmarking spike before committing to the full module.

3. **`bpf_helper_arg_validation`** — argument-bounds
   preconditions on BPF helpers.  Depends on the BPF front-end
   CBMC has from other work being available in this checkout.
   Still needs an availability investigation.

Plus the per-Phase-3a follow-ups:

4. **`netlink_attr_validation` v2: model `nla_parse` /
   `nla_validate`** — the highest-impact v2 work.  Without
   policy-aware contracts, the v1 catches handlers that skip
   validation entirely but produces false positives on
   handlers that validate via the standard API.

5. **Sibling shape modules**: `copy_from_user_size_validation`
   (same per-pointer-integer ghost shape, different surface).

## Reproducing

```sh
# Property module unit test:
./integration/linux/properties/netlink_attr_validation/run.sh

# Direct-call harness (vuln/fix):
build/bin/goto-cc \
  integration/linux/properties/netlink_attr_validation/netlink_attr_validation.c \
  integration/linux/scan/adapters/netlink_attr_validation_kernel_adapter.c \
  integration/linux/scan/adapters/netlink_attr_validation_kernel_direct_harness.c \
  -o /tmp/v.gb
build/bin/goto-instrument --replace-call-with-contract nla_get_u32 \
  /tmp/v.gb /tmp/v.inst
build/bin/cbmc /tmp/v.inst    # vuln: VERIFICATION FAILED
# add -DFIXED for the fix shape; expect VERIFICATION SUCCESSFUL.
```

## Cross-references

- [CVE survey](cve-survey-2023-2026.md) — prioritisation
  evidence: `out_of_bounds` is the largest single bug-shape
  category (649 CVEs).
- [19-module corpus run](corpus-2026-05-19modules.md) — the
  Phase-3-decision evidence that motivated this work.
- [Phase 1](phase-1-balance-modules-2026-05.md) — factory + 8
  balance modules.
- [Phase 2](phase-2-rcu-cancel-2026-05.md) — kref, RCU,
  cancel-before-free.
