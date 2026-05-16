/// \file
/// kobject_kernel_direct_harness.c — direct-call harness for the
/// kobject_lifetime property module.
///
/// Mirrors cred_kernel_direct_harness.c: build a kobject sentinel,
/// register it with the ghost table, and call the contract target
/// (`kobject_put`) in a vulnerable-or-safe shape selected by
/// `-DFIXED`.  Because --replace-call-with-contract replaces the
/// body of kobject_put with only the contract (no ghost-state
/// update), the harness explicitly updates the kobject_lifetime
/// ghost alongside each kobject_put call — mirroring the kernel's
/// real kobject_put body which atomically decrements and frees.
///
/// ### Vulnerable harness (default)
///
///   1. A kobject is initialised with `usage = 1`.
///   2. `kobject_put` is called — the contract precondition
///      holds because the kobject is still live.  Afterwards
///      the harness manually drops the ghost usage to zero to
///      match real kobject_put semantics.
///   3. `kobject_put` is called a second time.  The contract
///      precondition `kobject_live(k) == 1` must FIRE: the
///      kobject is no longer live.
///
/// ### Safe harness (-DFIXED)
///
/// Step 1 initialises `usage = 2`.  After the first put the
/// harness drops usage to 1; both puts therefore see a live
/// kobject and the precondition holds throughout.
///
/// ### Opaque struct kobject
///
/// `struct kobject` is forward-declared by the property module.
/// We back a sentinel with a byte buffer large enough for any
/// plausible kernel struct kobject (the content doesn't matter
/// — the property only uses pointer identity).

typedef unsigned long size_t;

struct kobject;

// Ghost-state API from the property module.
void kobject_lifetime_init(struct kobject *k, unsigned int usage);
void kobject_lifetime_get(struct kobject *k);
void kobject_lifetime_put(struct kobject *k);

// Contract target (declared by the adapter).  Signature matches
// the kernel's <linux/kobject.h>:
//   void kobject_put(struct kobject *kobj);
void kobject_put(struct kobject *kobj);

int main(void)
{
  // Backing buffer larger than any plausible kernel struct
  // kobject (typically ~80 bytes on x86_64).  Content is
  // irrelevant; only the address is used as a ghost-table key.
  static char kobject_sentinel[1024];
  struct kobject *k = (struct kobject *)kobject_sentinel;

#ifndef FIXED
  // Vulnerable: initial usage = 1.  First put drops to 0; second
  // put fires the kobject_live precondition.
  kobject_lifetime_init(k, 1);
#else
  // Safe: initial usage = 2.  Both puts land on a still-live
  // kobject and the precondition holds throughout.
  kobject_lifetime_init(k, 2);
#endif

  kobject_put(k);
  // Mirror the real kobject_put's side effect on ghost state.
  // The contract replaces the body so the ghost isn't updated
  // by kobject_put itself.
  kobject_lifetime_put(k);

  kobject_put(k);
  kobject_lifetime_put(k);

  return 0;
}
