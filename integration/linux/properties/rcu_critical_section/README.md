# rcu_critical_section property module

Tracks RCU read-side critical-section depth to catch two
related bug patterns:

1. **`synchronize_rcu()` (or other sleeping operation) inside
   an RCU read-side critical section.**  RCU readers must not
   block; calling `synchronize_rcu()` from within a reader is a
   self-deadlock (it sleeps waiting for all current readers,
   including itself, to finish).

2. **Unbalanced `rcu_read_unlock()`** — `rcu_read_unlock()`
   without a matching prior `rcu_read_lock()`.  Same shape as
   double-mutex-unlock.

Motivating CVE class: RCU misuse — 44 CVEs in the 2023-2026
kernel CVE survey explicitly mention `rcu_dereference`,
`rcu_read_(un)lock`, `synchronize_rcu`, or "sleeping in atomic
/ RCU context".

## Files

- [`rcu_critical_section.h`](rcu_critical_section.h) — public
  ghost variable, helpers, predicates.
- [`rcu_critical_section.c`](rcu_critical_section.c) — reference
  impl.
- [`test_unit.c`](test_unit.c) — depth-counter ghost +
  `rcu_in_csection` / `rcu_outside_csection` predicate test.
- [`rcu_critical_section.cocci`](rcu_critical_section.cocci) —
  Coccinelle prefilter for `synchronize_rcu`,
  `rcu_read_unlock`, `rcu_read_lock` call sites.
- [`run.sh`](run.sh) — regression runner.

## Scan integration

- Kernel adapter:
  [`../../scan/adapters/rcu_critical_section_kernel_adapter.c`](../../scan/adapters/rcu_critical_section_kernel_adapter.c)
  attaches three contracts:
  - `rcu_read_lock` (and the static-inline mangled form):
    `__CPROVER_assigns(__rcu_csection_depth)` and
    `__CPROVER_ensures(depth == old(depth) + 1)`.
  - `rcu_read_unlock`: `requires depth > 0`, `assigns depth`,
    `ensures depth == old(depth) - 1`.
  - `synchronize_rcu`: `requires depth == 0`.
- Direct-call harness:
  [`../../scan/adapters/rcu_critical_section_kernel_direct_harness.c`](../../scan/adapters/rcu_critical_section_kernel_direct_harness.c)
  vuln/fix shape: vuln calls `synchronize_rcu()` between
  `rcu_read_lock` and `rcu_read_unlock`, fix swaps the order.

## What this module does NOT cover (yet)

* **`rcu_dereference(p)` outside an RCU critical section.**
  `rcu_dereference` is a macro chain that eventually expands to
  `__rcu_dereference_check`; contracting the macro is not
  straightforward.  The cocci prefilter could surface candidate
  sites for hand triage.  Deferred to a follow-up.

* **Concurrent reasoning.**  The depth ghost is a single
  process-local counter.  CBMC's per-thread-serial execution
  model makes that faithful for non-concurrent reasoning;
  multi-thread RCU reasoning would need a per-CPU ghost and
  CBMC's `--unwind` over thread schedules, which is much
  slower.

* **`call_rcu(head, fn)` with `fn` taking spinlocks.**  RCU
  callbacks run in softirq context and can't take sleeping
  locks; this is a related but distinct shape.

These are tracked in the Phase-3 plan in
`integration/linux/doc/phase-1-balance-modules-2026-05.md`.
