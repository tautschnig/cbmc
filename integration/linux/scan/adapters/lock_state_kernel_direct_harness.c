/// \file
/// lock_state_kernel_direct_harness.c — direct-call harness for
/// the lock_state property module.
///
/// Constructs a mutex sentinel, takes the lock, unlocks, then
/// either (default) unlocks a second time for the double-unlock
/// vulnerable shape, or (with -DFIXED) stops after the single
/// balanced unlock.
///
/// As with the cred_lifetime direct harness (see
/// cred_kernel_direct_harness.c), goto-instrument
/// --replace-call-with-contract replaces the body of
/// mutex_unlock with only the contract — so the harness
/// explicitly updates the lock_state ghost alongside each
/// mutex_unlock call, mirroring the real kernel's body.
///
/// `struct mutex` is forward-declared; the sentinel is backed
/// by a 1 KiB static char buffer to avoid depending on the
/// kernel struct's actual layout.

struct mutex;

// Ghost-state API from the property module.
void lock_state_lock(struct mutex *m);
void lock_state_unlock_ghost(struct mutex *m);

// Contract target (declared by the adapter).
void mutex_unlock(struct mutex *lock);

int main(void)
{
  static char mutex_sentinel[1024];
  struct mutex *m = (struct mutex *)mutex_sentinel;

  // Lock once so the first unlock's precondition holds.
  lock_state_lock(m);

  mutex_unlock(m);
  // Mirror the real mutex_unlock's side effect on ghost state.
  lock_state_unlock_ghost(m);

#ifndef FIXED
  // Vulnerable: second unlock with held_count already at zero.
  // The contract precondition lock_held(m) == 1 fires here.
  mutex_unlock(m);
  lock_state_unlock_ghost(m);
#endif

  return 0;
}
