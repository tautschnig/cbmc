/// \file
/// netlink_attr_validation_kernel_adapter_probe.c — vacuity-probe.

struct nlattr;

unsigned int
__CPROVER_file_local_netlink_h_nla_get_u32(const struct nlattr *nla)
  __CPROVER_requires(0 == 1) __CPROVER_assigns();

unsigned long long
__CPROVER_file_local_netlink_h_nla_get_u64(const struct nlattr *nla)
  __CPROVER_requires(0 == 1) __CPROVER_assigns();

unsigned int nla_get_u32(const struct nlattr *nla) __CPROVER_requires(0 == 1)
  __CPROVER_assigns();

unsigned long long nla_get_u64(const struct nlattr *nla)
  __CPROVER_requires(0 == 1) __CPROVER_assigns();
