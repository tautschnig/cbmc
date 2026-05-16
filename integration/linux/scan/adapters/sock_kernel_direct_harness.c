/// \file
/// sock_kernel_direct_harness.c — direct-call
/// harness for the sock_lifetime property module.
///
/// Mirrors the cred / kobject pattern: build a sentinel, register
/// it with the ghost table at usage=1, call `sock_put`
/// (contract holds), drop the ghost, call again (contract fires).
/// `-DFIXED` initialises with usage=2 so both puts land on a
/// still-live object.

typedef unsigned long size_t;

struct sock;

void sock_lifetime_init(struct sock *sk, unsigned int usage);
void sock_lifetime_get(struct sock *sk);
void sock_lifetime_put(struct sock *sk);

void sock_put(struct sock *sk);

int main(void)
{
  static char sock_sentinel[1024];
  struct sock *sk = (struct sock *)sock_sentinel;

#ifndef FIXED
  sock_lifetime_init(sk, 1);
#else
  sock_lifetime_init(sk, 2);
#endif

  sock_put(sk);
  sock_lifetime_put(sk);

  sock_put(sk);
  sock_lifetime_put(sk);

  return 0;
}
