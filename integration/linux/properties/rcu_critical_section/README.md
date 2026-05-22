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

## What this module does NOT cover

* **`rcu_dereference(p)` outside an RCU critical section.**
  `rcu_dereference` is a macro chain that eventually expands to
  `__rcu_dereference_check`; contracting the macro is not
  straightforward.  The cocci prefilter could surface candidate
  sites for hand triage.  Deferred to a follow-up.

* **`call_rcu(head, fn)` with `fn` taking spinlocks.**  RCU
  callbacks run in softirq context and can't take sleeping
  locks; this is a related but distinct shape.

## Per-CPU concurrent variant

The single-counter `__rcu_csection_depth` model conflates
multiple concurrent readers.  In a 2-thread harness:

- T1: `rcu_read_lock` (depth: 0→1).
- T2: `rcu_read_unlock` (depth: 1→0).

The single-counter model accepts T2's unlock because the
global depth was raised by T1.  **But T2 never locked** —
the unlock is unbalanced from T2's perspective.  Real RCU
implementations (`CONFIG_PREEMPT_RCU`) track per-thread
depth and would catch this.

The per-CPU variant in this module exposes per-thread depth
ghosts (`__rcu_depth_t1`, `__rcu_depth_t2`) and contract
variants (`rcu_read_lock_t1` / `_t2`, `rcu_read_unlock_t1` /
`_t2`, `synchronize_rcu_per_cpu`).
[`scan/adapters/rcu_critical_section_per_cpu_concurrent_harness.c`](../../scan/adapters/rcu_critical_section_per_cpu_concurrent_harness.c)
spawns two reader threads (one per `_t1` / `_t2`) plus a
writer in main calling `synchronize_rcu_per_cpu`.

* **Vuln**: writer fires `synchronize_rcu_per_cpu`
  concurrently with the readers.  CBMC finds an
  interleaving where t1 (or t2) is inside its section
  when the writer's call runs; the precondition
  `__rcu_depth_t1 == 0 && __rcu_depth_t2 == 0` fails.
  `VERIFICATION FAILED` in ~32 ms.
* **Fix**: readers run to completion sequentially before
  the writer's call.  `VERIFICATION SUCCESSFUL` in ~19 ms.

This is the catalog's **first 3-thread harness** and
demonstrates CBMC's concurrent symex scales acceptably
beyond the 2-thread shape.

### What the per-CPU variant still does NOT cover

* **More than 2 readers.**  Adding `_t3`, `_t4` is
  mechanical but each new thread roughly doubles CBMC's
  exploration cost.

* **Thread-id-keyed lookup at runtime.**  The harness
  ASSIGNS a fixed slot (t1 or t2) to each thread; the
  contracts use the slot statically.  A model where every
  thread reads its own depth from a per-CPU array indexed
  by `current_cpu_id()` would need CBMC contract syntax
  for parameterised access patterns, which is awkward.

These are tracked in the Phase-3 plan in
`integration/linux/doc/phase-1-balance-modules-2026-05.md`.
