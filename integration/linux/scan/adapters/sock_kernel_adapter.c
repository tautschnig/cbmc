/// \file
/// sock_kernel_adapter.c — attaches the
/// sock_lifetime property module's `sock_live`
/// predicate as a contract precondition on the kernel's
/// `sock_put` API.

struct sock;

int sock_live(struct sock *sk);

// Contract on the mangled static-inline form.  Parameter name
// `sk` must match the kernel's
// `<net/sock.h>` declaration; mismatch triggers an
// invariant violation in goto-instrument
// --replace-call-with-contract at contract-installation time.
void __CPROVER_file_local_sock_h_sock_put(struct sock *sk)
  __CPROVER_requires(sk != (struct sock *)0)
    __CPROVER_requires(sock_live(sk) == 1) __CPROVER_assigns();

// External-name contract for direct-call harness links and any
// kernel TU that resolves the call to the external symbol.
void sock_put(struct sock *sk) __CPROVER_requires(sk != (struct sock *)0)
  __CPROVER_requires(sock_live(sk) == 1) __CPROVER_assigns();
