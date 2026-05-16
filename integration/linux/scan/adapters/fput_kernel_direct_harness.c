/// \file
/// fput_kernel_direct_harness.c — direct-call
/// harness for the fput_lifetime property module.
///
/// Mirrors the cred / kobject pattern: build a sentinel, register
/// it with the ghost table at usage=1, call `fput`
/// (contract holds), drop the ghost, call again (contract fires).
/// `-DFIXED` initialises with usage=2 so both puts land on a
/// still-live object.

typedef unsigned long size_t;

struct file;

void fput_lifetime_init(struct file *file,
                              unsigned int usage);
void fput_lifetime_get(struct file *file);
void fput_lifetime_put(struct file *file);

void fput(struct file *file);

int main(void)
{
  static char fput_sentinel[1024];
  struct file *file =
    (struct file *)fput_sentinel;

#ifndef FIXED
  fput_lifetime_init(file, 1);
#else
  fput_lifetime_init(file, 2);
#endif

  fput(file);
  fput_lifetime_put(file);

  fput(file);
  fput_lifetime_put(file);

  return 0;
}
