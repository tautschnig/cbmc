/// \file
/// rcu_critical_section.h — property module for Linux kernel
/// RCU read-side critical-section misuse.
///
/// ## Bug class
///
/// The kernel's RCU primitives bracket read-side critical
/// sections with `rcu_read_lock()` and `rcu_read_unlock()`.
/// Inside such a section the caller must not sleep, must not
/// `synchronize_rcu()` (which sleeps waiting for all current
/// readers to finish — calling it from within a reader is a
/// self-deadlock).  Two bug shapes recur in the CVE record:
///
///   1. **`synchronize_rcu()` (or other sleeping op) inside
///      an RCU read-side critical section.**  Cocci finds
///      every `synchronize_rcu()` call site; CBMC checks that
///      the depth ghost is zero at entry.
///
///   2. **Unbalanced `rcu_read_unlock()`** — `rcu_read_unlock()`
///      called when no prior `rcu_read_lock()` is in scope.
///      Same shape as a double-mutex-unlock.
///
/// Motivating CVE class: RCU misuse (44 CVEs in the 2023-2026
/// kernel CVE survey explicitly mention `rcu_dereference`,
/// `rcu_read_(un)lock`, `synchronize_rcu`, or "sleeping in
/// atomic / RCU context").
///
/// ## Abstraction
///
/// A single integer ghost `__rcu_csection_depth` counts
/// concurrently-open RCU critical sections in the current
/// thread.  CBMC's per-thread serial execution makes this a
/// faithful model for non-concurrent reasoning.
///
/// **Per-CPU extension** (post-Phase-3 follow-up): we also
/// expose per-thread depth ghosts `__rcu_depth_t1` and
/// `__rcu_depth_t2` and contract variants
/// `rcu_read_lock_t1` / `rcu_read_unlock_t1` / `_t2`.  These
/// let a concurrent harness model two RCU readers separately
/// and exercise the **cross-thread unbalanced unlock** bug
/// class — thread B calling `rcu_read_unlock` when thread A
/// is the one that holds a section.  The single-counter
/// model couldn't catch this because A's enter raised the
/// global counter for B to subsequently decrement.

#ifndef INTEGRATION_LINUX_PROPERTIES_RCU_CRITICAL_SECTION_H
#define INTEGRATION_LINUX_PROPERTIES_RCU_CRITICAL_SECTION_H

// Public ghost: how many RCU read-side critical sections are
// open right now.  Defined in rcu_critical_section.c; declared
// here so the kernel adapter's contracts can reference it.
extern unsigned int __rcu_csection_depth;

// Per-thread depth ghosts for the per-CPU concurrent variant.
// Each models one CPU's RCU read-side depth.  The harness
// assigns one thread to t1 and another to t2.
extern unsigned int __rcu_depth_t1;
extern unsigned int __rcu_depth_t2;

// Helpers for the unit test and the direct-call harness.  The
// kernel adapter doesn't use these — it manipulates
// `__rcu_csection_depth` directly via the contract's
// ensures/assigns clauses.
void rcu_csection_enter(void);
void rcu_csection_leave(void);
unsigned int rcu_csection_depth(void);

// Predicates for use in `__CPROVER_requires` clauses.
int rcu_in_csection(void);
int rcu_outside_csection(void);

// Per-thread predicates: any thread is in a critical section.
int rcu_any_thread_in_csection(void);

#endif
