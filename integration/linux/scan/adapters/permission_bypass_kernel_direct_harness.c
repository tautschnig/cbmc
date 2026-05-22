/// \file
/// permission_bypass_kernel_direct_harness.c
///
/// Vuln: privileged op without capability check.
/// Fix (-DFIXED): capability check first.

void cap_check_passed(void);
void cap_check_clear(void);
void __assert_privileged(void);

int main(void)
{
  cap_check_clear();

#ifdef FIXED
  // Simulate a successful capable() check.
  cap_check_passed();
#endif

  __assert_privileged();
  return 0;
}
