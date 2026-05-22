/// \file
/// use_after_free_generic_kernel_direct_harness.c
///
/// Vuln: kfree(p), then deref p (or kfree it again).
/// Fix (-DFIXED): re-allocate or skip the deref.

void mark_freed(const void *p);
void mark_alive(const void *p);
void __assert_not_freed(const void *p);

static char buf[1024];

int main(void)
{
  void *p = (void *)buf;
  mark_alive(p);

  // First kfree.
  mark_freed(p);

#ifdef FIXED
  // Fix: re-allocate (model as mark_alive).
  mark_alive(p);
#endif

  // Subsequent operation: assert not freed.
  __assert_not_freed(p);

  return 0;
}
