# sock_lifetime property module

Tracks the Linux kernel `struct sock` refcount to catch two
related bug patterns:

1. **Double put.**  `sock_put(sk)` decrements the
   refcount and, when it reaches zero, frees the object.  An error
   path that puts a `struct sock` another path already put
   typically lands as UAF on the next caller's reference.

2. **Put without a matching get.**  Code obtains a reference by
   other means and then puts it.  Same UAF outcome as (1).

The shape mirrors `cred_lifetime` / `kobject_lifetime`: a per-
pointer ghost counter tracks each `struct sock`'s notional
refcount; `sock_hold` increments and `sock_put`
decrements; the `sock_live(sk)`
predicate (ghost > 0) is attached as a contract precondition on
the kernel's `sock_put`.

Motivating CVE class: AF_VSOCK / netlink / Bluetooth socket UAFs (24+ CVE descriptions mention sock_put, 18+ mention sock_hold).
Subsystem focus: `net/*`.

## Files

- [`sock_lifetime.h`](sock_lifetime.h) — public API.
- [`sock_lifetime.c`](sock_lifetime.c) — reference impl.
- [`test_unit.c`](test_unit.c) — four-case unit test on the
  abstract ghost / predicate pair.
- [`sock_lifetime.cocci`](sock_lifetime.cocci) — Coccinelle
  prefilter (call-site + back-to-back-put bug shape).
- [`run.sh`](run.sh) — regression runner (unit test under cbmc).

## Scan integration

- Kernel adapter:
  [`../../scan/adapters/sock_kernel_adapter.c`](../../scan/adapters/sock_kernel_adapter.c)
  attaches `__CPROVER_requires(sock_live(sk) == 1)`
  to `sock_put`.  Because `sock_put` is
  `static inline` in `<net/sock.h>` the adapter
  attaches the contract to both the unmangled name and the
  goto-cc `--export-file-local-symbols` mangled form
  `__CPROVER_file_local_sock_h_sock_put` so it applies in every kernel TU that
  includes the header.
- Direct-call harness:
  [`../../scan/adapters/sock_kernel_direct_harness.c`](../../scan/adapters/sock_kernel_direct_harness.c)
  vuln/fix shape (init=1 vs init=2; second put fires
  precondition in vuln, holds in fix).
- Per-file synthesis: `synthesise_harness.py` initialises any
  parameter typed `struct sock *` with usage=1, so
  per-file scans on functions that take this type get a live
  ghost and produce real verdicts.
