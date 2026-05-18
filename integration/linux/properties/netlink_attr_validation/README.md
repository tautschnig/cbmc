# netlink_attr_validation property module

Tracks netlink-attribute payload-size validation to catch
out-of-bounds reads:

```c
// BUG shape:
u32 v = nla_get_u32(attr);   // attr's payload may be < 4 bytes
```

The standard fix is to validate the payload size before
reading, e.g. via `nla_parse(...)` against a policy that
specifies `NLA_U32` (minimum 4 bytes) for the attribute.

Motivating CVE class: the `out_of_bounds` bucket from the
2023-2026 kernel CVE survey — **649 CVEs (7.4% of the
classified volume), the largest single bug-shape category our
catalog now targets.**  Concrete recent examples:
CVE-2026-43450 (`nfnetlink_cthelper` OOB read),
CVE-2026-43453 (`nft_set_pipapo` OOB read), and the netfilter
nft / cthelper / queue family.

## Ghost shape

This is a **new** ghost shape for the catalog:
**per-`struct nlattr *` integer** "validated minimum payload
size".  Distinct from:

- the per-pointer balance counter (cred / kobject / etc.),
- the per-pointer flag (page_provenance / cancel_work),
- the single global counter (rcu_critical_section).

The same shape will be reusable for any "size-tainted" property
(e.g. `copy_from_user(dst, src, len)` where `len` is bounded
by some validation step).

## Files

- [`netlink_attr_validation.h`](netlink_attr_validation.h) —
  public ghost API + `nla_size_at_least` predicate.
- [`netlink_attr_validation.c`](netlink_attr_validation.c) —
  reference impl: per-pointer table of `(nlattr*, min_size)`.
- [`test_unit.c`](test_unit.c) — seven-case ghost test
  (untracked, validate, raise, lower-no-op, clear, independent
  entries, NULL).
- [`netlink_attr_validation.cocci`](netlink_attr_validation.cocci)
  — Coccinelle prefilter for `nla_get_u16` / `nla_get_u32` /
  `nla_get_u64` call sites.  `nla_get_u8` is not flagged
  (1-byte reads are usually safe by alignment).
- [`run.sh`](run.sh) — regression runner.

## Scan integration

- Kernel adapter:
  [`../../scan/adapters/netlink_attr_validation_kernel_adapter.c`](../../scan/adapters/netlink_attr_validation_kernel_adapter.c)
  attaches contracts to `nla_get_u8 / u16 / u32 / u64` (both
  static-inline mangled forms and the external names).
- Direct-call harness:
  [`../../scan/adapters/netlink_attr_validation_kernel_direct_harness.c`](../../scan/adapters/netlink_attr_validation_kernel_direct_harness.c)
  vuln/fix shape: vuln reads `nla_get_u32` from a 2-byte-
  validated attr; fix reads from a 4-byte-validated attr.
- Per-file synthesis: harness initialises each
  `struct nlattr *` parameter with `validated_min_size = 0`
  (unvalidated).  Functions that read `nla_get_*` without
  first calling `nla_validate_min_size` will fire the
  contract.

## What this module does NOT cover (yet)

* **Modeling `nla_parse` / `nla_validate`** — these set the
  ghost based on the policy.  Until v2 adds policy-aware
  contracts that propagate the policy's minimum sizes into the
  ghost, functions that DO validate via the standard kernel
  API will still produce false-positive verdicts under per-
  file synthesis (because the harness's bootstrap doesn't
  know the validation happened).

* **Type-mismatch shapes** (e.g. policy says `NLA_U32`, code
  reads `nla_get_u64`).  This is more subtle — the policy
  validates to 4 bytes but the read needs 8.  A v2
  enhancement would track the validated-by-policy size
  separately.

* **`nla_data(attr)` cast-then-read shapes**.  Code that
  bypasses the typed accessors and casts `nla_data(attr)` to
  some struct pointer is a related bug class but harder to
  contract uniformly.

* **Nested attributes**.  `nla_nest_validate` would set ghost
  values on each child; we don't model nesting yet.

## Honest limitations

This v1 catches the simplest shape: handlers that read
typed payloads without going through `nla_parse` first.  The
Cocci prefilter still surfaces `nla_get_*` candidate sites
even when the kernel TU goes through `nla_parse`; consumers
should expect a high false-positive rate at corpus scale until
v2's policy-aware contracts land.
