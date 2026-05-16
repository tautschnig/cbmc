# skb_lifetime property module

Tracks the Linux kernel `struct sk_buff` refcount to catch two
related bug patterns:

1. **Double put.**  `kfree_skb(skb)` decrements the
   refcount and, when it reaches zero, frees the object.  An error
   path that puts a `struct sk_buff` another path already put
   typically lands as UAF on the next caller's reference.

2. **Put without a matching get.**  Code obtains a reference by
   other means and then puts it.  Same UAF outcome as (1).

The shape mirrors `cred_lifetime` / `kobject_lifetime`: a per-
pointer ghost counter tracks each `struct sk_buff`'s notional
refcount; `skb_get` increments and `kfree_skb`
decrements; the `skb_live(skb)`
predicate (ghost > 0) is attached as a contract precondition on
the kernel's `kfree_skb`.

Motivating CVE class: sk_buff UAFs in network stacks and drivers/net (16+ mention skb_put, 12+ mention skb_get).
Subsystem focus: `net/*, drivers/net/*`.

## Files

- [`skb_lifetime.h`](skb_lifetime.h) — public API.
- [`skb_lifetime.c`](skb_lifetime.c) — reference impl.
- [`test_unit.c`](test_unit.c) — four-case unit test on the
  abstract ghost / predicate pair.
- [`skb_lifetime.cocci`](skb_lifetime.cocci) — Coccinelle
  prefilter (call-site + back-to-back-put bug shape).
- [`run.sh`](run.sh) — regression runner (unit test under cbmc).

## Scan integration

- Kernel adapter:
  [`../../scan/adapters/skb_kernel_adapter.c`](../../scan/adapters/skb_kernel_adapter.c)
  attaches `__CPROVER_requires(skb_live(skb) == 1)`
  to `kfree_skb`.
- Direct-call harness:
  [`../../scan/adapters/skb_kernel_direct_harness.c`](../../scan/adapters/skb_kernel_direct_harness.c)
  vuln/fix shape (init=1 vs init=2; second put fires
  precondition in vuln, holds in fix).
- Per-file synthesis: `synthesise_harness.py` initialises any
  parameter typed `struct sk_buff *` with usage=1, so
  per-file scans on functions that take this type get a live
  ghost and produce real verdicts.
