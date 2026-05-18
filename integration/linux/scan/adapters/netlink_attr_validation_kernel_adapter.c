/// \file
/// netlink_attr_validation_kernel_adapter.c — attaches contracts
/// from the netlink_attr_validation property module to the
/// kernel's `nla_get_u8 / u16 / u32 / u64` accessors.
///
/// All four are `static inline` in `<net/netlink.h>`, exposed
/// under `__CPROVER_file_local_netlink_h_nla_get_*` mangled
/// names in every kernel TU that includes the header.  We
/// attach contracts to both forms for robustness.
///
/// ## Contracts
///
/// `nla_get_uX(nla)` requires:
///   - `nla != NULL`, and
///   - `nla_size_at_least(nla, sizeof(uX)) == 1`, i.e. the
///     payload has been validated to at least `sizeof(uX)`
///     bytes (e.g. via `nla_parse` with a policy).
///
/// CBMC discharges the precondition iff the kernel function's
/// path through the property module's ghost API has raised
/// the validated_min_size to >= sizeof(uX).

struct nlattr;

int nla_size_at_least(struct nlattr *attr, unsigned int size);

// Static-inline mangled forms.

unsigned char
__CPROVER_file_local_netlink_h_nla_get_u8(const struct nlattr *nla)
  __CPROVER_requires(nla != (const struct nlattr *)0)
    __CPROVER_requires(nla_size_at_least((struct nlattr *)nla, 1) == 1)
      __CPROVER_assigns();

unsigned short
__CPROVER_file_local_netlink_h_nla_get_u16(const struct nlattr *nla)
  __CPROVER_requires(nla != (const struct nlattr *)0)
    __CPROVER_requires(nla_size_at_least((struct nlattr *)nla, 2) == 1)
      __CPROVER_assigns();

unsigned int
__CPROVER_file_local_netlink_h_nla_get_u32(const struct nlattr *nla)
  __CPROVER_requires(nla != (const struct nlattr *)0)
    __CPROVER_requires(nla_size_at_least((struct nlattr *)nla, 4) == 1)
      __CPROVER_assigns();

unsigned long long
__CPROVER_file_local_netlink_h_nla_get_u64(const struct nlattr *nla)
  __CPROVER_requires(nla != (const struct nlattr *)0)
    __CPROVER_requires(nla_size_at_least((struct nlattr *)nla, 8) == 1)
      __CPROVER_assigns();

// External-name forms (for direct-call harness links).

unsigned char nla_get_u8(const struct nlattr *nla)
  __CPROVER_requires(nla != (const struct nlattr *)0)
    __CPROVER_requires(nla_size_at_least((struct nlattr *)nla, 1) == 1)
      __CPROVER_assigns();

unsigned short nla_get_u16(const struct nlattr *nla)
  __CPROVER_requires(nla != (const struct nlattr *)0)
    __CPROVER_requires(nla_size_at_least((struct nlattr *)nla, 2) == 1)
      __CPROVER_assigns();

unsigned int nla_get_u32(const struct nlattr *nla)
  __CPROVER_requires(nla != (const struct nlattr *)0)
    __CPROVER_requires(nla_size_at_least((struct nlattr *)nla, 4) == 1)
      __CPROVER_assigns();

unsigned long long nla_get_u64(const struct nlattr *nla)
  __CPROVER_requires(nla != (const struct nlattr *)0)
    __CPROVER_requires(nla_size_at_least((struct nlattr *)nla, 8) == 1)
      __CPROVER_assigns();
