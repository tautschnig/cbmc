/// \file
/// lock_state_kernel_adapter.c — attaches the lock_state
/// property module's `lock_held` predicate as a contract
/// precondition on the kernel's `mutex_unlock` API.
///
/// ## Why mutex_unlock?
///
/// `mutex_unlock` is an ordinary `extern void mutex_unlock(
/// struct mutex *lock);` in `<linux/mutex.h>` — not a static
/// inline, so there's no per-TU name-mangling.  The contract
/// installs on the single external symbol and fires wherever
/// the kernel code calls it with a mutex whose ghost held_count
/// is zero.
///
/// ## The precondition
///
///     __CPROVER_requires(lock != NULL)
///     __CPROVER_requires(lock_held(lock) == 1)
///
/// Translation: at every `mutex_unlock` call site the ghost
/// must say the mutex is currently held.  Double-unlock,
/// unlock-without-lock, and unlock-on-freed-mutex all fire
/// this.
///
/// ## Struct layout
///
/// `struct mutex` is left forward-declared here.  At link time
/// the kernel TU's full struct is unified in; the adapter never
/// dereferences mutex fields, only uses pointer identity via
/// the property module's ghost table.  This avoids the LIM-016
/// struct-body-mismatch trap that we learned about with
/// cred_lifetime.

struct mutex;

// Property module predicate.
int lock_held(struct mutex *m);

// External-name contract.  mutex_unlock is not static-inline, so
// a single declaration suffices.
void mutex_unlock(struct mutex *lock)
  __CPROVER_requires(lock != (struct mutex *)0)
    __CPROVER_requires(lock_held(lock) == 1) __CPROVER_assigns();
