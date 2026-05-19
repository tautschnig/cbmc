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

## v2: nla_parse modelled

The v1 module did not model `nla_parse` / `__nla_parse` /
`__nla_validate` — it only contracted `nla_get_u8/u16/u32/u64`.
That meant any handler that validated via the standard kernel
API (the dominant pattern) produced false positives under
per-file synthesis: the harness initialised tb[] entries with
`validated_min_size = 0`, the kernel function called
`nla_parse(tb, ...)` (which doesn't update the harness's
ghost), and a subsequent `nla_get_u32(tb[i])` fired the
contract precondition spuriously.

v2 closes this gap by providing a SHIM definition of
`__nla_parse` and `__nla_validate` in
[`netlink_attr_validation.c`](netlink_attr_validation.c).
The shim:

* Is linked in alongside the kernel TU under scan, replacing
  the unresolved external `__nla_parse` symbol from
  `<net/netlink.h>`.  The kernel's own `lib/nlattr.c` is not
  in the scan's compile path, so the shim has the only
  definition of `__nla_parse` at link time.
* Iterates `tb[0..maxtype]` and marks every non-NULL entry as
  validated to 8 bytes (an over-approximation that covers
  `nla_get_u8/u16/u32/u64`).
* Is manually unrolled to depth 32 to avoid CBMC unwind
  issues on the per-file scan's typical `--unwind 2` setting.
  Real-world netlink protocols (NFTA_*_MAX etc.) typically
  cap at < 32 in 6.12.

The synthetic test
[`scan/adapters/netlink_attr_validation_kernel_direct_harness.c`](../../scan/adapters/netlink_attr_validation_kernel_direct_harness.c)
exercises the shim path under
`-DTEST_NLA_PARSE_SHIM`: two `tb[i]` are populated, the shim
runs, both subsequent `nla_get_u32(tb[i])` reads satisfy the
contract precondition.

## What this module does NOT cover

* **Type-mismatch shapes** (e.g. policy says `NLA_U32`, code
  reads `nla_get_u64`).  More subtle — the policy validates
  to 4 bytes but the read needs 8.  A v3 enhancement would
  track the validated-by-policy size separately and use it
  in the `nla_get_u64` precondition.

* **`nla_data(attr)` cast-then-read shapes** (where code
  bypasses the typed accessors and casts `nla_data(attr)` to
  some struct pointer).  Related bug class but harder to
  contract uniformly; left for v3.

* **Functions that take `tb[]` as a parameter** (rather than
  parsing internally) without our harness initialising each
  slot.  v2's shim runs only when CBMC's symex executes a
  `__nla_parse` call.  A function whose caller already
  parsed will see uninit `tb[i]` ghosts in the per-file
  harness.  Wrapper-paths-style auto-init for nlattr-pointer
  arrays is a v3 candidate.

* **Reads of `tb[i]` for `i >= 32`.**  See the v2 shim
  unrolling above; handlers with maxtype > 31 leave
  `tb[32..]` unvalidated.

## Honest limitations

The v2 shim is over-approximation: it always marks
`validated_min_size = 8` regardless of the policy.  This
catches handlers that skip validation entirely (the v1 bug
class) AND handlers that go through the standard
`nla_parse(...)` flow, but it does NOT catch type-mismatch
bugs (policy `NLA_U32`, read `nla_get_u64`) — those would
need policy-aware contracts that read the policy array's
`.type` field and translate to a per-`tb[i]` minimum size.
