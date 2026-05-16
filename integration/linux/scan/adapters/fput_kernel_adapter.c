/// \file
/// fput_kernel_adapter.c — attaches the
/// fput_lifetime property module's `fput_live`
/// predicate as a contract precondition on the kernel's
/// `fput` API.

struct file;

int fput_live(struct file *file);

// External-name contract for direct-call harness links and any
// kernel TU that resolves the call to the external symbol.
void fput(struct file *file)
  __CPROVER_requires(file != (struct file *)0)
  __CPROVER_requires(fput_live(file) == 1)
  __CPROVER_assigns();
