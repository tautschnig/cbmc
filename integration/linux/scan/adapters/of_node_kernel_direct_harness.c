/// \file
/// of_node_kernel_direct_harness.c — direct-call
/// harness for the of_node_lifetime property module.
///
/// Mirrors the cred / kobject pattern: build a sentinel, register
/// it with the ghost table at usage=1, call `of_node_put`
/// (contract holds), drop the ghost, call again (contract fires).
/// `-DFIXED` initialises with usage=2 so both puts land on a
/// still-live object.

typedef unsigned long size_t;

struct device_node;

void of_node_lifetime_init(struct device_node *node, unsigned int usage);
void of_node_lifetime_get(struct device_node *node);
void of_node_lifetime_put(struct device_node *node);

void of_node_put(struct device_node *node);

int main(void)
{
  static char of_node_sentinel[1024];
  struct device_node *node = (struct device_node *)of_node_sentinel;

#ifndef FIXED
  of_node_lifetime_init(node, 1);
#else
  of_node_lifetime_init(node, 2);
#endif

  of_node_put(node);
  of_node_lifetime_put(node);

  of_node_put(node);
  of_node_lifetime_put(node);

  return 0;
}
