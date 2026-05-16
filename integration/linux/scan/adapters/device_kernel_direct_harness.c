/// \file
/// device_kernel_direct_harness.c — direct-call
/// harness for the device_lifetime property module.
///
/// Mirrors the cred / kobject pattern: build a sentinel, register
/// it with the ghost table at usage=1, call `put_device`
/// (contract holds), drop the ghost, call again (contract fires).
/// `-DFIXED` initialises with usage=2 so both puts land on a
/// still-live object.

typedef unsigned long size_t;

struct device;

void device_lifetime_init(struct device *dev,
                              unsigned int usage);
void device_lifetime_get(struct device *dev);
void device_lifetime_put(struct device *dev);

void put_device(struct device *dev);

int main(void)
{
  static char device_sentinel[1024];
  struct device *dev =
    (struct device *)device_sentinel;

#ifndef FIXED
  device_lifetime_init(dev, 1);
#else
  device_lifetime_init(dev, 2);
#endif

  put_device(dev);
  device_lifetime_put(dev);

  put_device(dev);
  device_lifetime_put(dev);

  return 0;
}
