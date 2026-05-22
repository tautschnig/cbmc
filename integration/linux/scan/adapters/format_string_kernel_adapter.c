/// \file
/// format_string_kernel_adapter.c

int format_is_constant(const char *fmt);

void __assert_format_safe(const char *fmt)
  __CPROVER_requires(format_is_constant(fmt) == 1) __CPROVER_assigns();
