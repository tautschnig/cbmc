/// \file
/// netlink_attr_validation_kernel_direct_harness.c — direct-call
/// harness for the netlink_attr_validation property module.
///
/// Multiple test paths under one binary, controlled by macros:
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
///   2. `__nla_parse(tb, 5, ..., NULL, ...)` is called with no
///      policy.  After the shim runs, both tb[0] and tb[3]
///      have validated_min_size=8 (v2 over-approximation).
///   3. `nla_get_u32(tb[0])` and `nla_get_u32(tb[3])` both
///      satisfy the contract precondition.
///
/// ## v3 type-mismatch shape (`-DTEST_NLA_PARSE_TYPE_MISMATCH`)
///
/// In v3, the shim reads `policy[i].type` and translates to
/// per-attribute minimum size.  This path demonstrates the
/// type-mismatch bug class:
///
///   1. Two tb pointers populated, with a policy that says
///      tb[0] is NLA_U32 (4 bytes) and tb[1] is NLA_U64 (8
///      bytes).
///   2. `__nla_parse(tb, 1, ..., policy, ...)` runs.  After
///      the shim, tb[0] has min_size=4 and tb[1] has min_size=8.
///   3. `nla_get_u64(tb[0])` is called — 8-byte read on the
///      NLA_U32-validated attr.  The v3 shim's per-attribute
///      sizing causes the contract precondition to FIRE
///      (validated to 4 bytes only).
///   4. `nla_get_u64(tb[1])` would succeed (validated to 8).
///
/// ## v3 type-mismatch fix (`-DTEST_NLA_PARSE_TYPE_MISMATCH -DFIXED`)
///
/// Same setup but the read uses `nla_get_u64(tb[1])` instead
/// of `tb[0]`.  Precondition holds.

struct nlattr;
struct nla_policy;
struct netlink_ext_ack;

void nla_validate_min_size(struct nlattr *attr, unsigned int min_size);
unsigned int nla_get_u32(const struct nlattr *nla);
unsigned long long nla_get_u64(const struct nlattr *nla);

int __nla_parse(
  struct nlattr **tb,
  int maxtype,
  const struct nlattr *head,
  int len,
  const struct nla_policy *policy,
  unsigned int validate,
  struct netlink_ext_ack *extack);

#ifdef TEST_NLA_PARSE_TYPE_MISMATCH
// Locally redeclare the shim's struct nla_policy to fill the
// policy[] literal.  Layout matches the shim's view.
struct nla_policy_local
{
  unsigned char type;
  unsigned char validation_type;
  unsigned short len;
  unsigned long __opaque_union_filler;
};
#endif

int main(void)
{
  // Touch all typed accessors so the linker keeps both
  // nla_get_u32 and nla_get_u64 in the goto binary.  Without
  // this, --replace-call-with-contract complains "function not
  // found" for the unused accessor.  Volatile to suppress
  // dead-code elimination.
  static char __touch_attr_backing[1024];
  struct nlattr *__touch_attr = (struct nlattr *)__touch_attr_backing;
  nla_validate_min_size(__touch_attr, 8u);
  volatile unsigned int __t32 = nla_get_u32(__touch_attr);
  volatile unsigned long long __t64 = nla_get_u64(__touch_attr);
  (void)__t32;
  (void)__t64;

#ifdef TEST_NLA_PARSE_TYPE_MISMATCH
  // v3 shim test: build a policy table.  policy[0].type = NLA_U32 (3),
  // policy[1].type = NLA_U64 (4).  After the shim runs, tb[0] has
  // min_size=4; tb[1] has min_size=8.
  static char attr0_backing[1024];
  static char attr1_backing[1024];
  struct nlattr *attr0 = (struct nlattr *)attr0_backing;
  struct nlattr *attr1 = (struct nlattr *)attr1_backing;

  struct nlattr *tb[4] = {0};
  tb[0] = attr0;
  tb[1] = attr1;

  static const struct nla_policy_local policy[4] = {
    {3u, 0u, 0u, 0u}, // tb[0] NLA_U32
    {4u, 0u, 0u, 0u}, // tb[1] NLA_U64
    {0u, 0u, 0u, 0u},
    {0u, 0u, 0u, 0u},
  };

  (void)__nla_parse(
    tb,
    1, // maxtype = 1 covers tb[0]..tb[1]
    (const struct nlattr *)0,
    0,
    (const struct nla_policy *)policy,
    0u,
    (struct netlink_ext_ack *)0);

#  ifndef FIXED
  // Vuln: read u64 from the NLA_U32-validated tb[0].  Contract
  // requires nla_size_at_least(tb[0], 8) — the shim only marked
  // it as 4 (NLA_U32).  Should fire.
  (void)nla_get_u64(tb[0]);
#  else
  // Fix: read u64 from the NLA_U64-validated tb[1].  Contract
  // holds.
  (void)nla_get_u64(tb[1]);
#  endif

#elif defined(TEST_NLA_PARSE_SHIM)
  // v2 shim test (no policy): __nla_parse populates the tb[i]
  // ghost entries to validated_min_size=8.  After the call, both
  // nla_get_u32 reads succeed.
  static char attr0_backing[1024];
  static char attr3_backing[1024];
  struct nlattr *attr0 = (struct nlattr *)attr0_backing;
  struct nlattr *attr3 = (struct nlattr *)attr3_backing;

  struct nlattr *tb[8] = {0};
  tb[0] = attr0;
  tb[3] = attr3;

  // NULL policy → shim falls back to v2 over-approximation (8 bytes).
  (void)__nla_parse(
    tb,
    5,
    (const struct nlattr *)0,
    0,
    (const struct nla_policy *)0,
    0u,
    (struct netlink_ext_ack *)0);

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
