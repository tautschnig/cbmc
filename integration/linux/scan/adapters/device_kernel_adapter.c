/// \file
/// device_kernel_adapter.c — attaches the
/// device_lifetime property module's `device_live`
/// predicate as a contract precondition on the kernel's
/// `put_device` API.

struct device;

int device_live(struct device *dev);

// External-name contract for direct-call harness links and any
// kernel TU that resolves the call to the external symbol.
void put_device(struct device *dev)
  __CPROVER_requires(dev != (struct device *)0)
  __CPROVER_requires(device_live(dev) == 1)
  __CPROVER_assigns();
