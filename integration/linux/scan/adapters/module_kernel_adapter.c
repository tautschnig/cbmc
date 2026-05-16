/// \file
/// module_kernel_adapter.c — attaches the
/// module_lifetime property module's `module_live`
/// predicate as a contract precondition on the kernel's
/// `module_put` API.

struct module;

int module_live(struct module *module);

// External-name contract for direct-call harness links and any
// kernel TU that resolves the call to the external symbol.
void module_put(struct module *module)
  __CPROVER_requires(module != (struct module *)0)
  __CPROVER_requires(module_live(module) == 1)
  __CPROVER_assigns();
