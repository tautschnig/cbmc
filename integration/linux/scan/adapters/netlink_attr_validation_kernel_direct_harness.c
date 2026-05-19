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
/// ## v2 shim coverage (`-DTEST_NLA_PARSE_SHIM`)
///
/// In v2, the property module's __nla_parse shim auto-validates
/// each non-NULL tb[i] in [0..32].  This harness path
/// demonstrates that:
///
///   1. Two tb pointers are populated (tb[0] and tb[3]).
///   2. `__nla_parse(tb, 5, ...)` is called.  After the shim
///      runs, both tb[0] and tb[3] have validated_min_size=8.
///   3. `nla_get_u32(tb[0])` and `nla_get_u32(tb[3])` both
///      satisfy the contract precondition.
///
/// Build with `-DTEST_NLA_PARSE_SHIM` to exercise this path
/// (and -DFIXED isn't needed; the shim makes both reads safe).
///
/// The harness's "validation" calls in the non-shim paths are
/// simulated via the property module's ghost API.

struct nlattr;
struct nla_policy;
struct netlink_ext_ack;

void nla_validate_min_size(struct nlattr *attr, unsigned int min_size);
unsigned int nla_get_u32(const struct nlattr *nla);
int __nla_parse(
  struct nlattr **tb,
  int maxtype,
  const struct nlattr *head,
  int len,
  const struct nla_policy *policy,
  unsigned int validate,
  struct netlink_ext_ack *extack);

int main(void)
{
#ifdef TEST_NLA_PARSE_SHIM
  // v2 shim test: __nla_parse should populate the tb[i] ghost
  // entries to validated_min_size=8.  After the call, both
  // nla_get_u32 reads succeed.
  static char attr0_backing[1024];
  static char attr3_backing[1024];
  struct nlattr *attr0 = (struct nlattr *)attr0_backing;
  struct nlattr *attr3 = (struct nlattr *)attr3_backing;

  struct nlattr *tb[8] = {0};
  tb[0] = attr0;
  tb[3] = attr3;
  // Other tb[i] are NULL.

  // Call the shim (linker resolves __nla_parse to our shim).
  (void)__nla_parse(
    tb,
    5,
    (const struct nlattr *)0,
    0,
    (const struct nla_policy *)0,
    0u,
    (struct netlink_ext_ack *)0);

  // Both attrs should now have validated_min_size >= 4.
  (void)nla_get_u32(tb[0]);
  (void)nla_get_u32(tb[3]);
#else
  static char small_backing[1024];
  static char big_backing[1024];
  struct nlattr *small = (struct nlattr *)small_backing;
  struct nlattr *big = (struct nlattr *)big_backing;

  // small was validated to 2 bytes (e.g. NLA_U16 policy).
  nla_validate_min_size(small, 2);

  // big was validated to 4 bytes (e.g. NLA_U32 policy).
  nla_validate_min_size(big, 4);

#  ifndef FIXED
  // Vulnerable: read u32 from the 2-byte-validated attr.
  (void)nla_get_u32(small);
#  else
  // Safe: read u32 from the 4-byte-validated attr.
  (void)nla_get_u32(big);
#  endif
#endif

  return 0;
}
