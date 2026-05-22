/// \file
/// concurrent_double_put_kernel_adapter_probe.c — vacuity-probe.

extern unsigned int __cdp_live;

void __cdp_get(void) __CPROVER_requires(0 == 1) __CPROVER_assigns();

void __cdp_put(void) __CPROVER_requires(0 == 1) __CPROVER_assigns();
