/// \file
/// lock_state.h — property module for Linux kernel mutex
/// acquire/release balance.  Catches the "double unlock" /
/// "unlock-without-lock" pattern that shows up in many CVE
/// advisories (examples include unbalanced cleanup paths in
/// drivers that call `mutex_unlock` twice on an error exit, or
/// that unlock a mutex they never locked).
///
/// ## Bug class
///
/// The kernel's mutex API says every `mutex_unlock(m)` must be
/// preceded by a `mutex_lock(m)` (or variant) that has not yet
/// been matched by an `unlock`.  The canonical textual pattern
/// of the bug is `mutex_unlock(&foo); …; mutex_unlock(&foo);`
/// with no intervening `mutex_lock`.  Less-obvious variants
/// unlock a mutex acquired on a different function's stack.
///
/// ## Abstraction
///
/// We track a per-mutex "held count" ghost in a side table
/// keyed by pointer identity.  `mutex_lock(m)` bumps the count;
/// `mutex_unlock(m)` checks `held_count > 0` (the precondition)
/// and then decrements.  The contract on `mutex_unlock` carries
/// the `lock_held(m)` precondition; any call where the ghost
/// says the mutex is not held fires it.
///
/// `struct mutex` is forward-declared as an opaque type
/// (matching the LIM-016 lesson for cred_lifetime).  The
/// property module never dereferences mutex fields; it only
/// uses pointer identity.

#ifndef INTEGRATION_LINUX_PROPERTIES_LOCK_STATE_LOCK_STATE_H
#define INTEGRATION_LINUX_PROPERTIES_LOCK_STATE_LOCK_STATE_H

struct mutex;

// Ghost-state API.
void lock_state_lock(struct mutex *m);
void lock_state_unlock_ghost(struct mutex *m);  // internal ghost dec.
void lock_state_reset(struct mutex *m);
unsigned int lock_state_held_count(struct mutex *m);

// Predicate: the mutex is currently held (by anyone).  Safe
// to call inside a `__CPROVER_requires` clause.
int lock_held(struct mutex *m);

#endif
