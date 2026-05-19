/// \file
/// netlink_attr_validation.c — reference implementation.
/// Per-pointer ghost table mirroring the balance modules'
/// pattern, but the ghost value is "validated minimum
/// payload size" (an unsigned int), not a refcount.

#include "netlink_attr_validation.h"

#ifndef NLA_VALIDATION_TABLE_SIZE
#  define NLA_VALIDATION_TABLE_SIZE 16
#endif

struct nla_validation_entry
{
  struct nlattr *key;
  unsigned int validated_min_size;
};

static struct nla_validation_entry table[NLA_VALIDATION_TABLE_SIZE];
static unsigned int table_len = 0;

static struct nla_validation_entry *find(struct nlattr *attr)
{
  for(unsigned int i = 0; i < table_len; i++)
    if(table[i].key == attr)
      return &table[i];
  return (struct nla_validation_entry *)0;
}

static struct nla_validation_entry *find_or_add(struct nlattr *attr)
{
  struct nla_validation_entry *e = find(attr);
  if(e)
    return e;
  if(table_len >= NLA_VALIDATION_TABLE_SIZE)
    return (struct nla_validation_entry *)0;
  struct nla_validation_entry *slot = &table[table_len++];
  slot->key = attr;
  slot->validated_min_size = 0;
  return slot;
}

void nla_validate_min_size(struct nlattr *attr, unsigned int min_size)
{
  struct nla_validation_entry *e = find_or_add(attr);
  // Raise the ghost to at least min_size.  Don't lower if a
  // larger validation has already been recorded.
  if(e && e->validated_min_size < min_size)
    e->validated_min_size = min_size;
}

void nla_validate_clear(struct nlattr *attr)
{
  struct nla_validation_entry *e = find(attr);
  if(e)
    e->validated_min_size = 0;
}

unsigned int nla_validated_size(struct nlattr *attr)
{
  struct nla_validation_entry *e = find(attr);
  if(!e)
    return 0;
  return e->validated_min_size;
}

int nla_size_at_least(struct nlattr *attr, unsigned int size)
{
  if(!attr)
    return 0;
  return nla_validated_size(attr) >= size ? 1 : 0;
}

// =====================================================================
// v2: shim definitions of __nla_parse and __nla_validate.
//
// The kernel's <net/netlink.h> declares __nla_parse as an extern
// implemented in lib/nlattr.c — but our scan path doesn't compile
// lib/nlattr.c, so the symbol is unresolved at link time.  We
// provide a strong definition here that the linker picks up; CBMC's
// symex executes our shim when it encounters a call to nla_parse
// (or its inline wrapper) in any kernel TU.
//
// The shim's job: simulate the post-condition that successful
// nla_parse establishes — namely, that each non-NULL tb[i] entry
// has been validated to the policy's minimum size.  We
// over-approximate by marking validated_min_size = 8 (large enough
// to cover nla_get_u8/u16/u32/u64 reads).  This is an honest
// over-approximation; type-mismatch shapes (NLA_U32 policy read as
// nla_get_u64) are intentionally not caught — see the property
// module README's "What this module does NOT cover" section.
//
// The loop is manually unrolled to depth 32 because CBMC's per-file
// scan typically uses --unwind 2; a real `for` loop would only
// validate tb[0] and tb[1].  Most kernel netlink protocols have
// MAX_ATTR_TYPE < 32 (e.g. NFTA_*_MAX series tops out around 25 in
// 6.12), so this depth covers the typical case.  Handlers with
// larger maxtypes will leave their tb[32..] entries unvalidated,
// producing the same false positives the v1 module has — flagged
// in the v2 README as a deliberate trade-off.
//
// We don't intercept nla_parse_deprecated, nla_parse_strict,
// nla_validate, nla_validate_nested, etc. — they all call
// __nla_parse or __nla_validate underneath, and the shim catches
// the underlying primitive.

// Forward decls so we can name the kernel's struct types in our
// shim's signatures without pulling in <net/netlink.h>.
struct nlattr;
struct nla_policy;
struct netlink_ext_ack;

static void __nla_parse_validate_tb(struct nlattr **tb, int maxtype)
{
  if(!tb)
    return;
// Manually unrolled — see comment above for rationale.
#define VALIDATE_TB_AT(idx)                                                    \
  do                                                                           \
  {                                                                            \
    if((idx) <= maxtype && tb[(idx)])                                          \
      nla_validate_min_size(tb[(idx)], 8);                                     \
  } while(0)
  VALIDATE_TB_AT(0);
  VALIDATE_TB_AT(1);
  VALIDATE_TB_AT(2);
  VALIDATE_TB_AT(3);
  VALIDATE_TB_AT(4);
  VALIDATE_TB_AT(5);
  VALIDATE_TB_AT(6);
  VALIDATE_TB_AT(7);
  VALIDATE_TB_AT(8);
  VALIDATE_TB_AT(9);
  VALIDATE_TB_AT(10);
  VALIDATE_TB_AT(11);
  VALIDATE_TB_AT(12);
  VALIDATE_TB_AT(13);
  VALIDATE_TB_AT(14);
  VALIDATE_TB_AT(15);
  VALIDATE_TB_AT(16);
  VALIDATE_TB_AT(17);
  VALIDATE_TB_AT(18);
  VALIDATE_TB_AT(19);
  VALIDATE_TB_AT(20);
  VALIDATE_TB_AT(21);
  VALIDATE_TB_AT(22);
  VALIDATE_TB_AT(23);
  VALIDATE_TB_AT(24);
  VALIDATE_TB_AT(25);
  VALIDATE_TB_AT(26);
  VALIDATE_TB_AT(27);
  VALIDATE_TB_AT(28);
  VALIDATE_TB_AT(29);
  VALIDATE_TB_AT(30);
  VALIDATE_TB_AT(31);
#undef VALIDATE_TB_AT
}

int __nla_parse(
  struct nlattr **tb,
  int maxtype,
  const struct nlattr *head,
  int len,
  const struct nla_policy *policy,
  unsigned int validate,
  struct netlink_ext_ack *extack)
{
  (void)head;
  (void)len;
  (void)policy;
  (void)validate;
  (void)extack;
  __nla_parse_validate_tb(tb, maxtype);
  return 0;
}

int __nla_validate(
  const struct nlattr *head,
  int len,
  int maxtype,
  const struct nla_policy *policy,
  unsigned int validate,
  struct netlink_ext_ack *extack)
{
  (void)head;
  (void)len;
  (void)maxtype;
  (void)policy;
  (void)validate;
  (void)extack;
  // __nla_validate doesn't have an output tb[] to populate —
  // it just checks whether `head` parses cleanly under `policy`.
  // We model success (return 0).  Per-file harnesses that pass
  // the result of __nla_validate into a subsequent nla_get on
  // the same head won't see ghost effects; that's acceptable
  // because the typical pattern is to call nla_parse for the
  // tb[] population, not nla_validate alone.
  return 0;
}
