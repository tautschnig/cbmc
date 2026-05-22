/// \file
/// resource_leak_on_error_path_kernel_direct_harness.c
///
/// Simulates "alloc + early-return-without-free":
///
/// Vuln: alloc, then nondet-fail returns without freeing.
/// CBMC explores the failing path; the exit-checkpoint
/// fires because leak_outstanding is still 1.
///
/// Fix (`-DFIXED`): the failing path frees first.

void leak_alloc_track(const void *p);
void leak_alloc_freed(const void *p);
void __assert_no_leak_at_exit(const void *p);

static char buf[1024];

int main(void)
{
  void *p = (void *)buf;

  // Allocation.
  leak_alloc_track(p);

  // Nondet decision: simulate an error path.
  int err;
  if(err)
  {
#ifdef FIXED
    // Fix: free before early return.
    leak_alloc_freed(p);
#endif
    __assert_no_leak_at_exit(p);
    return -1;
  }

  // Success path: free at end.
  leak_alloc_freed(p);
  __assert_no_leak_at_exit(p);
  return 0;
}
