/// \file
/// rcu_critical_section_kernel_adapter_probe.c — vacuity-probe.

extern unsigned int __rcu_csection_depth;

void __CPROVER_file_local_rcupdate_h_rcu_read_lock(void)
  __CPROVER_requires(0 == 1) __CPROVER_assigns();

void __CPROVER_file_local_rcupdate_h_rcu_read_unlock(void)
  __CPROVER_requires(0 == 1) __CPROVER_assigns();

void rcu_read_lock(void) __CPROVER_requires(0 == 1) __CPROVER_assigns();

void rcu_read_unlock(void) __CPROVER_requires(0 == 1) __CPROVER_assigns();

void synchronize_rcu(void) __CPROVER_requires(0 == 1) __CPROVER_assigns();
