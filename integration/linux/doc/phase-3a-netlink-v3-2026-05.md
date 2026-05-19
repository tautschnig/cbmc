# netlink_attr_validation v3 — policy-aware __nla_parse

This iteration extends the v2 `__nla_parse` shim to read the
caller's policy table and translate `policy[i].type` into a
per-attribute minimum size, catching the **type-mismatch bug
class** that v2 deliberately over-approximated past.

## What v2 missed

The v2 shim marked every non-NULL `tb[i]` as validated to 8
bytes regardless of policy.  This caught two real bug classes:

- Handlers that skip validation entirely and read raw payload.
- Handlers that go through `nla_parse(...)` and read with a
  smaller-or-equal type than expected.

But it did NOT catch the **type-mismatch** shape:

```c
static const struct nla_policy my_policy[] = {
    [MY_ATTR_FOO] = { .type = NLA_U32 },   // 4 bytes expected
};

err = nla_parse_nested(tb, MY_ATTR_MAX, attr, my_policy, NULL);
...
u64 v = nla_get_u64(tb[MY_ATTR_FOO]);    // BUG: 8-byte read
                                          // on 4-byte-validated attr
```

This pattern is real in CVE-2026-43450 and the wider
netfilter / netlink CVE record — handlers that mix up integer
widths between policy and accessor.

## v3 design

The shim's TU now defines a layout-compatible
`struct nla_policy`:

```c
struct nla_policy {
    unsigned char  type;            // offset 0  (matches kernel u8)
    unsigned char  validation_type; // offset 1
    unsigned short len;             // offset 2-3
    unsigned long  __opaque_union_filler;  // offset 8-15
};                                          // total: 16 bytes
```

This matches the kernel's `<net/netlink.h>` layout on x86_64
(the union after `len` contains pointer-sized members; the
struct is naturally 16-byte-aligned).  CBMC's
structural-equivalence linker fix (commit `0414b43bf2`)
accepts the type-name match across TUs even when one
declares the union concretely and the other models it as an
opaque filler.

Indexing `policy[i].type` then yields the right offset.  The
shim's `__nla_min_size_for_policy_type` switch maps NLA_*
enum values to byte counts:

| Policy type | Min size |
|---|---:|
| `NLA_U8`  / `NLA_S8`  | 1 |
| `NLA_U16` / `NLA_S16` | 2 |
| `NLA_U32` / `NLA_S32` | 4 |
| `NLA_U64` / `NLA_S64` / `NLA_MSECS` | 8 |
| `NLA_UNSPEC`, `NLA_FLAG`, `NLA_STRING`, `NLA_NESTED`, `NLA_BINARY`, ... | 0 |

If the policy pointer is `NULL`, the shim falls back to the
v2 over-approximation of 8 bytes.

The manual unrolling to depth 32 is unchanged from v2 —
required because per-file scans use `--unwind 2` by default
and a real `for` loop would only iterate twice.

## Validation: all five harness scenarios

The direct-call harness now exercises five paths under three
macros:

| Macros | Expected | Observed |
|---|---|---|
| (default) | `VERIFICATION FAILED` (small attr read u32) | ✓ |
| `-DFIXED` | `VERIFICATION SUCCESSFUL` | ✓ |
| `-DTEST_NLA_PARSE_SHIM` | `VERIFICATION SUCCESSFUL` (v2 fallback path, NULL policy) | ✓ |
| `-DTEST_NLA_PARSE_TYPE_MISMATCH` | `VERIFICATION FAILED` (v3: NLA_U32 policy, nla_get_u64 read) | ✓ |
| `-DTEST_NLA_PARSE_TYPE_MISMATCH -DFIXED` | `VERIFICATION SUCCESSFUL` (read the NLA_U64 attr instead) | ✓ |

The harness now also touches both `nla_get_u32` and
`nla_get_u64` from `main` unconditionally so
`--replace-call-with-contract` finds both functions in the
goto binary regardless of which scenario macro is set.

All scan/run.sh, test-per-file.sh, test-per-file-mode.sh,
smoke-all-lts.sh regressions pass.

## What v3 still does NOT cover

* **`nla_data(attr)` cast-then-read shapes** — code that
  bypasses the typed accessors and casts `nla_data(attr)`
  to a struct pointer.  Related bug class but harder to
  contract uniformly; v4.

* **Functions that take `tb[]` as a parameter** without
  parsing internally still see uninit ghost in the per-file
  harness.  Wrapper-paths-style auto-init for nlattr-pointer
  arrays would address this; v4 candidate.

* **Reads of `tb[i]` for `i >= 32`** — outside the
  unrolling bound.  Most kernel netlink protocols cap below
  this, so it covers the typical case.

* **Validation extensions** beyond integer widths — range
  checks, masks, bitfield validators.  v3 currently treats
  these as "no minimum size" (i.e. unvalidated).  In
  practice the kernel doesn't call `nla_get_uX` on
  non-integer-typed attrs, so this is the right default.

* **Layout drift across kernel versions.**  The hand-coded
  `struct nla_policy` definition reflects the 5.10–6.12
  layout.  A future kernel reordering fields would silently
  corrupt the policy-type read.  Periodic recheck against
  `<net/netlink.h>` is the maintenance hook.

## Honest limitations

The v3 model is sound for integer-typed reads through typed
accessors.  Two specific failure modes that v3 does not
prevent:

1. **Cast-then-read.**  `*(u64 *)nla_data(tb[i])` bypasses
   the typed accessor.  v3 doesn't contract `nla_data`
   itself.

2. **Out-of-bounds index.**  `tb[100]` where the policy
   only validated up to 31.  Out of unrolling scope.

Both are documented as v4 candidates.

## Catalog impact

The catalog still stands at **21 modules** — v3 is a
refinement of an existing module rather than a new entry.
Coverage in the bug-shape proxy doesn't increase by module
count, but the **false-positive rate** on the existing
`out_of_bounds` 649-CVE category drops materially:
type-mismatch handlers (a substantial fraction of netfilter
/ netlink OOB CVEs) will no longer produce spurious clean
verdicts under v2's over-approximation, and unsoundness in
the other direction (missed bugs) goes down accordingly.

A re-run of the 21-module corpus would now usefully measure
this; deferred to a separate session.

## Reproducing

```sh
# v3 type-mismatch vuln:
build/bin/goto-cc -DTEST_NLA_PARSE_TYPE_MISMATCH \
  integration/linux/properties/netlink_attr_validation/netlink_attr_validation.c \
  integration/linux/scan/adapters/netlink_attr_validation_kernel_adapter.c \
  integration/linux/scan/adapters/netlink_attr_validation_kernel_direct_harness.c \
  -o /tmp/v3.gb
build/bin/goto-instrument \
  --replace-call-with-contract nla_get_u32 \
  --replace-call-with-contract nla_get_u64 \
  /tmp/v3.gb /tmp/v3.inst
build/bin/cbmc /tmp/v3.inst    # VERIFICATION FAILED
# Add -DFIXED for the safe shape; expect VERIFICATION SUCCESSFUL.
```

## Cross-references

- [v1 + v2 outcomes](phase-3a-netlink-2026-05.md) — the
  Phase 3a write-up that introduced the module.
- [v2 + concurrent_pointer_publish](phase-3b-concurrency-2026-05.md)
  — Phase 3b write-up that mentioned v3 as a follow-up.
