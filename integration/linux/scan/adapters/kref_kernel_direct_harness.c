/// \file
/// kref_kernel_direct_harness.c — direct-call
/// harness for the kref_lifetime property module.
///
/// Mirrors the cred / kobject pattern: build a sentinel, register
/// it with the ghost table at usage=1, call `kref_put`
/// (contract holds), drop the ghost, call again (contract fires).
/// `-DFIXED` initialises with usage=2 so both puts land on a
/// still-live object.

typedef unsigned long size_t;

struct kref;

void kref_lifetime_init(struct kref *kref, unsigned int usage);
void kref_lifetime_get(struct kref *kref);
void kref_lifetime_put(struct kref *kref);

int kref_put(struct kref *kref, void (*release)(struct kref *kref));

static void __harness_release_stub(struct kref *kref)
{
  (void)kref;
}

int main(void)
{
  static char kref_sentinel[1024];
  struct kref *kref = (struct kref *)kref_sentinel;

#ifndef FIXED
  kref_lifetime_init(kref, 1);
#else
  kref_lifetime_init(kref, 2);
#endif

  (void)kref_put(kref, __harness_release_stub);
  kref_lifetime_put(kref);

  (void)kref_put(kref, __harness_release_stub);
  kref_lifetime_put(kref);

  return 0;
}
