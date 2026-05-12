/// \file
/// cred_kernel_direct_harness.c — direct-call harness for the
/// cred_lifetime property module / CVE-2026-23297 class.
///
/// Mirrors the design of aead_kernel_direct_harness.c: build a
/// kernel-layout `struct cred` explicitly (vulnerable or safe,
/// selected by `-DFIXED`) and call the contract target
/// (`put_cred`).  Because --replace-call-with-contract replaces
/// the body of put_cred with only the contract (no ghost-state
/// update), the harness explicitly updates the cred_lifetime
/// ghost alongside each put_cred call — this mirrors the real
/// kernel's `put_cred` body which atomically decrements usage.
///
/// ### Vulnerable harness
///
///   1. A cred is initialised with `usage = 1`.
///   2. `put_cred` is called — the contract precondition holds
///      because the cred is still live.  After the call, the
///      harness manually drops the ghost usage to zero to match
///      the real put_cred semantics.
///   3. `put_cred` is called a second time.  The contract
///      precondition `cred_live(cred) == 1` must FIRE: the cred
///      is no longer live (ghost usage is zero).
///
/// ### Safe harness (-DFIXED)
///
/// Step 1 initialises `usage = 2`.  After the first put the
/// harness drops usage to 1; both put_cred calls therefore see
/// a live cred.

typedef unsigned long size_t;

struct cred
{
  unsigned int usage;
  unsigned long _pad;
};

// Ghost-state API from the property module.
void cred_lifetime_init(struct cred *c, unsigned int usage);
void cred_lifetime_get(struct cred *c);
void cred_lifetime_put(struct cred *c);

// Contract target (declared by the adapter).  Signature matches
// the kernel's <linux/cred.h>:
//   static inline void put_cred(const struct cred *_cred);
void put_cred(const struct cred *_cred);

int main(void)
{
  struct cred c;

#ifndef FIXED
  // Vulnerable: initial usage = 1.  First put drops to 0; second
  // put fires the cred_live precondition.
  cred_lifetime_init(&c, 1);
#else
  // Safe: initial usage = 2.  Both puts land on a still-live
  // cred and the precondition holds throughout.
  cred_lifetime_init(&c, 2);
#endif

  put_cred(&c);
  // Mirror the real put_cred's side effect on ghost state.  The
  // contract replaces the body so the ghost isn't updated by
  // put_cred itself — see the file-level comment above.
  cred_lifetime_put(&c);

  put_cred(&c);
  cred_lifetime_put(&c);

  return 0;
}
