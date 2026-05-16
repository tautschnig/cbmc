/// \file
/// of_node_kernel_adapter.c — attaches the
/// of_node_lifetime property module's `of_node_live`
/// predicate as a contract precondition on the kernel's
/// `of_node_put` API.

struct device_node;

int of_node_live(struct device_node *node);

// External-name contract for direct-call harness links and any
// kernel TU that resolves the call to the external symbol.
void of_node_put(struct device_node *node)
  __CPROVER_requires(node != (struct device_node *)0)
    __CPROVER_requires(of_node_live(node) == 1) __CPROVER_assigns();
