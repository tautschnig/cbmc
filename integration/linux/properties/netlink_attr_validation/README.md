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

## v2: nla_parse modelled (over-approximated)

The v1 module did not model `nla_parse` / `__nla_parse` /
`__nla_validate` — it only contracted `nla_get_u8/u16/u32/u64`.
That meant any handler that validated via the standard kernel
API (the dominant pattern) produced false positives under
per-file synthesis: the harness initialised tb[] entries with
`validated_min_size = 0`, the kernel function called
`nla_parse(tb, ...)` (which doesn't update the harness's
ghost), and a subsequent `nla_get_u32(tb[i])` fired the
contract precondition spuriously.

v2 closed this by providing a SHIM definition of `__nla_parse`
and `__nla_validate` in
[`netlink_attr_validation.c`](netlink_attr_validation.c).
The shim:

* Is linked in alongside the kernel TU under scan, replacing
  the unresolved external `__nla_parse` symbol from
  `<net/netlink.h>`.  The kernel's own `lib/nlattr.c` is not
  in the scan's compile path, so the shim has the only
  definition of `__nla_parse` at link time.
* In v2, the shim marked every non-NULL `tb[i]` as validated
  to 8 bytes (over-approximation that covers all the typed
  accessors but doesn't catch type mismatches).
* Is manually unrolled to depth 32 to avoid CBMC unwind
  issues on the per-file scan's typical `--unwind 2` setting.
  Real-world netlink protocols (NFTA_*_MAX etc.) typically
  cap at < 32 in 6.12.

## v3: policy-aware sizing

v3 extends the shim to **read `policy[i].type` and translate
it into a per-attribute minimum size**:

| Policy type        | Min size (bytes) |
|--------------------|-----------------:|
| `NLA_U8` / `NLA_S8`           | 1 |
| `NLA_U16` / `NLA_S16`         | 2 |
| `NLA_U32` / `NLA_S32`         | 4 |
| `NLA_U64` / `NLA_S64` / `NLA_MSECS` | 8 |
| Anything else (UNSPEC, FLAG, STRING, NESTED, ...) | 0 |

The shim's TU defines a layout-compatible
`struct nla_policy`:

```c
struct nla_policy {
    unsigned char  type;
    unsigned char  validation_type;
    unsigned short len;
    unsigned long  __opaque_union_filler;   /* matches kernel's union */
};
```

Indexing `policy[i]` then yields the right offset for
`policy[i].type`.

This **catches type-mismatch bugs**: if `policy[0].type =
NLA_U32` (4-byte minimum) but the code reads
`nla_get_u64(tb[0])` (needs 8 bytes), the shim only marks
`tb[0]` as validated to 4, and the contract precondition
on `nla_get_u64` fires.

If the policy pointer is NULL, the shim falls back to the v2
over-approximation of 8 bytes (so handlers that don't pass a
policy still see SOME validation).

The synthetic test
[`scan/adapters/netlink_attr_validation_kernel_direct_harness.c`](../../scan/adapters/netlink_attr_validation_kernel_direct_harness.c)
exercises four scenarios under macros:

| Macros | Expected verdict |
|---|---|
| (default) | `VERIFICATION FAILED` — small attr (validated to 2) read as u32 |
| `-DFIXED` | `VERIFICATION SUCCESSFUL` — big attr (validated to 4) read as u32 |
| `-DTEST_NLA_PARSE_SHIM` | `VERIFICATION SUCCESSFUL` — v2 shim path (no policy → fallback to 8) |
| `-DTEST_NLA_PARSE_TYPE_MISMATCH` | `VERIFICATION FAILED` — v3: NLA_U32 policy, nla_get_u64 read |
| `-DTEST_NLA_PARSE_TYPE_MISMATCH -DFIXED` | `VERIFICATION SUCCESSFUL` — read NLA_U64 attr instead |

## What this module does NOT cover

* **`nla_data(attr)` cast-then-read shapes** (where code
  bypasses the typed accessors and casts `nla_data(attr)` to
  some struct pointer).  Related bug class but harder to
  contract uniformly; left for v4.

* **Functions that take `tb[]` as a parameter** (rather than
  parsing internally) without our harness initialising each
  slot.  v3's shim runs only when CBMC's symex executes a
  `__nla_parse` call.  A function whose caller already
  parsed will see uninit `tb[i]` ghosts in the per-file
  harness.  Wrapper-paths-style auto-init for nlattr-pointer
  arrays is a v4 candidate.

* **Reads of `tb[i]` for `i >= 32`.**  See the shim
  unrolling above; handlers with maxtype > 31 leave
  `tb[32..]` unvalidated.

* **Layout drift across kernel versions.**  The
  `struct nla_policy` definition in our shim hard-codes the
  kernel's 5.10–6.12 layout (type, validation_type, len, then
  an 8-byte union member, total 16 bytes with alignment).
  If a future kernel reorders fields the offsets we read
  will wrong; we'd see false reads of other fields as
  `type`.  Periodic recheck against `<net/netlink.h>` is
  the maintenance hook.

## Honest limitations

The shim layout is hand-matched to the kernel's
`struct nla_policy`.  CBMC's structural-equivalence linker
fix accepts type-name matches even when the field layouts
differ in detail, but a wholesale layout change in a future
kernel would silently corrupt the policy-type read.  That's
explicitly recorded above.

The translation from `policy[i].type` to a minimum size is
the **typed-integer subset** only.  Strings, binary, nested
attributes, and the various `NLA_POLICY_*` validation
extensions (range checks, mask checks, etc.) are not
modelled — they all map to "no minimum size", which means
`nla_get_uX` on them would fire the precondition.  In
practice the kernel doesn't call `nla_get_uX` on
non-integer-typed attributes, so this is the right default.
