/// \file
/// netlink_attr_validation_kernel_direct_harness.c — direct-call
/// harness for the netlink_attr_validation property module.
///
/// ## Vulnerable shape (default)
///
///   1. Two attributes constructed: `small` (validated to 2
///      bytes) and `big` (validated to 4 bytes).
///   2. `nla_get_u32(small)` is called — 4-byte read on a
///      2-byte-validated attr.  Contract precondition
///      `nla_size_at_least(attr, 4) == 1` MUST FIRE.
///
/// ## Safe shape (`-DFIXED`)
///
///   1. Same setup.
///   2. `nla_get_u32(big)` is called — 4-byte read on a
///      4-byte-validated attr.  Precondition holds.
///
/// The harness's "validation" calls are simulated via the
/// property module's ghost API; in real kernel code these
/// would be implicit consequences of `nla_parse(...)` running
/// against a policy that specified minimum sizes.

struct nlattr;

void nla_validate_min_size(struct nlattr *attr, unsigned int min_size);
unsigned int nla_get_u32(const struct nlattr *nla);

int main(void)
{
  static char small_backing[1024];
  static char big_backing[1024];
  struct nlattr *small = (struct nlattr *)small_backing;
  struct nlattr *big = (struct nlattr *)big_backing;

  // small was validated to 2 bytes (e.g. NLA_U16 policy).
  nla_validate_min_size(small, 2);

  // big was validated to 4 bytes (e.g. NLA_U32 policy).
  nla_validate_min_size(big, 4);

#ifndef FIXED
  // Vulnerable: read u32 from the 2-byte-validated attr.
  (void)nla_get_u32(small);
#else
  // Safe: read u32 from the 4-byte-validated attr.
  (void)nla_get_u32(big);
#endif

  return 0;
}
