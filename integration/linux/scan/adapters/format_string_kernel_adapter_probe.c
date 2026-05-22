/// \file
/// format_string_kernel_adapter_probe.c

void __assert_format_safe(const char *fmt) __CPROVER_requires(0 == 1)
  __CPROVER_assigns();
