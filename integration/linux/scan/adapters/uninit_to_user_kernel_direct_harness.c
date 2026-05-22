/// \file
/// uninit_to_user_kernel_direct_harness.c
///
/// Vuln: declares a struct, mark_initialised is NOT called
/// before the synthetic copy_to_user checkpoint.  Contract
/// fires.
///
/// Fix (-DFIXED): mark_initialised is called (modelling a
/// memset / explicit field init) before the checkpoint.

void mark_initialised(const void *p);
void mark_uninitialised(const void *p);
void __assert_safe_for_userspace(const void *p);

struct kernel_info
{
  int field_a;
  int field_b;
};

int main(void)
{
  struct kernel_info info;

#ifdef FIXED
  // Fix: mark as initialised (modelling memset+populate).
  mark_initialised(&info);
#endif

  // Synthetic copy_to_user.
  __assert_safe_for_userspace(&info);

  return 0;
}
