/// \file
/// module_kernel_direct_harness.c — direct-call
/// harness for the module_lifetime property module.
///
/// Mirrors the cred / kobject pattern: build a sentinel, register
/// it with the ghost table at usage=1, call `module_put`
/// (contract holds), drop the ghost, call again (contract fires).
/// `-DFIXED` initialises with usage=2 so both puts land on a
/// still-live object.

typedef unsigned long size_t;

struct module;

void module_lifetime_init(struct module *module,
                              unsigned int usage);
void module_lifetime_get(struct module *module);
void module_lifetime_put(struct module *module);

void module_put(struct module *module);

int main(void)
{
  static char module_sentinel[1024];
  struct module *module =
    (struct module *)module_sentinel;

#ifndef FIXED
  module_lifetime_init(module, 1);
#else
  module_lifetime_init(module, 2);
#endif

  module_put(module);
  module_lifetime_put(module);

  module_put(module);
  module_lifetime_put(module);

  return 0;
}
