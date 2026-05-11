/// \file
/// aead_kernel_harness.c — entry-point harness for
/// `_aead_recvmsg(struct socket *, struct msghdr *, size_t, int)`.
///
/// `cbmc --function _aead_recvmsg` on the linked kernel + adapter +
/// stubs binary times out because cbmc synthesises nondet pointer
/// arguments, forcing its pointer analysis to consider every
/// possible alias.  This harness `__CPROVER_allocate`s concrete
/// storage for the opaque kernel types the function dereferences,
/// so the initial state is a small set of distinct heap objects
/// rather than an unconstrained pointer soup.
///
/// This is the minimum harness that makes the full-function scan
/// tractable; it is not a reproduction of `_aead_recvmsg`'s intended
/// behaviour.  The stubs (aead_kernel_stubs.c) provide the rest.

#include <stddef.h>
#include <stdint.h>

struct socket;
struct msghdr;

extern int _aead_recvmsg(
  struct socket *sock,
  struct msghdr *msg,
  size_t ignored,
  int flags);

// Generous size: kernel struct socket is ~192B, struct msghdr ~88B;
// we allocate 4 KiB each to cover any member access.
#define HARNESS_OBJ_SIZE 4096

int main(void)
{
  struct socket *sock = __CPROVER_allocate(HARNESS_OBJ_SIZE, 0);
  struct msghdr *msg = __CPROVER_allocate(HARNESS_OBJ_SIZE, 0);
  size_t size;
  int flags;
  if(!sock || !msg)
    return 0;
  return _aead_recvmsg(sock, msg, size, flags);
}
