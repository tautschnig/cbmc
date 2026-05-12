# lock_state property module

Tracks the Linux kernel mutex held/unheld state to catch the
"double unlock" / "unlock-without-lock" bug pattern.

## What the property catches

Every `mutex_unlock(m)` must be preceded by a matching
`mutex_lock(m)` (or variant) that has not yet been unmatched.
The ghost table tracks a per-mutex `held_count`; the contract
on `mutex_unlock` requires `lock_held(m) == 1` (`held_count >
0`).  Double-unlock, unlock-without-lock, and unlock-on-
deallocated-mutex all fire.

The fourth property module in the integration/linux suite —
follows the cred_lifetime pattern exactly, demonstrating the
four-step methodology (property module + CVE regression +
Coccinelle prefilter + kernel adapter) generalises cleanly to
yet another kernel bug class.

## Files

- [`lock_state.h`](lock_state.h) — public API.  `struct mutex`
  is forward-declared (LIM-016 lesson): callers that need to
  stack-allocate provide a local concrete definition, the scan
  pipeline links with the kernel's full struct at link time.
- [`lock_state.c`](lock_state.c) — ghost table + `lock_held`
  predicate.  Keyed by pointer identity.
- [`test_unit.c`](test_unit.c) — five-case unit test.
- [`lock_state.cocci`](lock_state.cocci) — Coccinelle prefilter
  flagging every `mutex_unlock` / `spin_unlock` call site.
- [`run.sh`](run.sh) — regression runner.

## Scan integration

- Kernel adapter:
  [`../../scan/adapters/lock_state_kernel_adapter.c`](../../scan/adapters/lock_state_kernel_adapter.c)
  attaches `__CPROVER_requires(lock_held(m) == 1)` to
  `mutex_unlock`.  Unlike `put_cred` in cred_lifetime, this is
  an ordinary `extern void` — not a static inline — so no
  mangled form is needed.
- Direct-call harness:
  [`../../scan/adapters/lock_state_kernel_direct_harness.c`](../../scan/adapters/lock_state_kernel_direct_harness.c)
  constructs a mutex sentinel, locks once, then either
  (default) unlocks twice (vulnerable), or (`-DFIXED`) unlocks
  once (safe).  The second unlock's contract precondition
  fires in the vulnerable direction.
- scan/run.sh case 7 exercises the end-to-end pipeline on
  Linux 5.10 `kernel/bpf/dispatcher.c`, which contains
  `mutex_unlock(&trampoline_mutex)` calls in its dispatcher-
  update path.
