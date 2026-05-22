/// \file
/// format_string_kernel_direct_harness.c

void mark_format_constant(const char *fmt);
void mark_format_tainted(const char *fmt);
void __assert_format_safe(const char *fmt);

int main(void)
{
  static char buf[256];
  const char *fmt = (const char *)buf;

#ifndef FIXED
  // Vuln: format string is attacker-controlled (untracked).
  mark_format_tainted(fmt);
#else
  // Fix: format string is a compile-time constant.
  mark_format_constant(fmt);
#endif

  __assert_format_safe(fmt);
  return 0;
}
