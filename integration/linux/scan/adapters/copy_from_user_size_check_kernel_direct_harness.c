/// \file
/// copy_from_user_size_check_kernel_direct_harness.c

#include <stddef.h>

void __assert_copy_safe(size_t dst_capacity, size_t len);

#define BUFLEN 64u

int main(void)
{
  size_t user_len;  // unbounded — simulates user-controlled length

#ifndef FIXED
  // Vuln: pass unbounded user_len as the copy length without
  // first bounding it against BUFLEN.
  __assert_copy_safe(BUFLEN, user_len);
#else
  // Fix: clamp user_len to BUFLEN before the copy.
  if(user_len > BUFLEN)
    user_len = BUFLEN;
  __assert_copy_safe(BUFLEN, user_len);
#endif

  return 0;
}
