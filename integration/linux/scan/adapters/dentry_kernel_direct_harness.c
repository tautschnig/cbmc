/// \file
/// dentry_kernel_direct_harness.c — direct-call
/// harness for the dentry_lifetime property module.
///
/// Mirrors the cred / kobject pattern: build a sentinel, register
/// it with the ghost table at usage=1, call `dput`
/// (contract holds), drop the ghost, call again (contract fires).
/// `-DFIXED` initialises with usage=2 so both puts land on a
/// still-live object.

typedef unsigned long size_t;

struct dentry;

void dentry_lifetime_init(struct dentry *dentry,
                              unsigned int usage);
void dentry_lifetime_get(struct dentry *dentry);
void dentry_lifetime_put(struct dentry *dentry);

void dput(struct dentry *dentry);

int main(void)
{
  static char dentry_sentinel[1024];
  struct dentry *dentry =
    (struct dentry *)dentry_sentinel;

#ifndef FIXED
  dentry_lifetime_init(dentry, 1);
#else
  dentry_lifetime_init(dentry, 2);
#endif

  dput(dentry);
  dentry_lifetime_put(dentry);

  dput(dentry);
  dentry_lifetime_put(dentry);

  return 0;
}
