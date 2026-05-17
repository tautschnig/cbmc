/// \file
/// rcu_critical_section_kernel_adapter.c — attaches contracts
/// from the rcu_critical_section property module to the kernel's
/// `rcu_read_lock`, `rcu_read_unlock`, and `synchronize_rcu`
/// APIs.
///
/// ## Mangling
///
/// `rcu_read_lock` and `rcu_read_unlock` are `static inline` in
/// `<linux/rcupdate.h>`, exposed under
/// `__CPROVER_file_local_rcupdate_h_rcu_read_lock` etc.
/// `synchronize_rcu` is an extern in `kernel/rcu/tree.c`; the
/// external name suffices.
///
/// ## Contracts
///
/// `rcu_read_lock`: increments the depth ghost.
/// `rcu_read_unlock`: requires depth > 0; decrements.
/// `synchronize_rcu`: requires depth == 0 (sleeping op outside
/// the critical section).
///
/// ## Ghost-state mutation
///
/// We use `__CPROVER_assigns(__rcu_csection_depth)` plus
/// `__CPROVER_ensures(...)` clauses so
/// `goto-instrument --replace-call-with-contract` substitutes a
/// contract that BOTH constrains the precondition AND mutates
/// the ghost variable in lockstep with the real kernel
/// behaviour.

extern unsigned int __rcu_csection_depth;

// Contract on the static-inline mangled forms.
void __CPROVER_file_local_rcupdate_h_rcu_read_lock(void)
  __CPROVER_assigns(__rcu_csection_depth) __CPROVER_ensures(
    __rcu_csection_depth == __CPROVER_old(__rcu_csection_depth) + 1u);

void __CPROVER_file_local_rcupdate_h_rcu_read_unlock(void)
  __CPROVER_requires(__rcu_csection_depth > 0u)
    __CPROVER_assigns(__rcu_csection_depth) __CPROVER_ensures(
      __rcu_csection_depth == __CPROVER_old(__rcu_csection_depth) - 1u);

// External-name forms (for direct-call harness links and any
// kernel TU that resolves to the external symbol — e.g. tree.c
// for synchronize_rcu).
void rcu_read_lock(void) __CPROVER_assigns(__rcu_csection_depth)
  __CPROVER_ensures(
    __rcu_csection_depth == __CPROVER_old(__rcu_csection_depth) + 1u);

void rcu_read_unlock(void) __CPROVER_requires(__rcu_csection_depth > 0u)
  __CPROVER_assigns(__rcu_csection_depth) __CPROVER_ensures(
    __rcu_csection_depth == __CPROVER_old(__rcu_csection_depth) - 1u);

void synchronize_rcu(void) __CPROVER_requires(__rcu_csection_depth == 0u)
  __CPROVER_assigns();
