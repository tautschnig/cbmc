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
// v2/v3: shim definitions of __nla_parse and __nla_validate.
//
// The kernel's <net/netlink.h> declares __nla_parse as an extern
// implemented in lib/nlattr.c — but our scan path doesn't compile
// lib/nlattr.c, so the symbol is unresolved at link time.  We
// provide a strong definition here that the linker picks up; CBMC's
// symex executes our shim when it encounters a call to nla_parse
// (or its inline wrapper) in any kernel TU.
//
// v2: the shim simulated a successful parse by marking each
// non-NULL tb[i] entry with a fixed validated_min_size = 8 (the
// largest size we contract on).  This caught handlers that skipped
// validation entirely AND handlers that went through the standard
// API, but it deliberately over-approximated and so could not
// catch type-mismatch shapes (NLA_U32 policy read as nla_get_u64).
//
// v3 reads `policy[i].type` and translates it into a per-attribute
// minimum size:
//   NLA_U8 / NLA_S8           -> 1 byte
//   NLA_U16 / NLA_S16         -> 2 bytes
//   NLA_U32 / NLA_S32         -> 4 bytes
//   NLA_U64 / NLA_S64 / NLA_MSECS -> 8 bytes
//   anything else (NLA_UNSPEC, NLA_FLAG, NLA_STRING, ...)  -> 0
//
// With the precise per-attribute min size, the contract on
// `nla_get_u64(tb[i])` correctly fires when policy[i].type is
// NLA_U32 (validated to 4 bytes, but reading 8).
//
// The loop is manually unrolled to depth 32 because CBMC's per-file
// scan typically uses --unwind 2; a real `for` loop would only
// validate tb[0] and tb[1].  Most kernel netlink protocols have
// MAX_ATTR_TYPE < 32 (e.g. NFTA_*_MAX series tops out around 25 in
// 6.12), so this depth covers the typical case.

// Forward decls.  We define a struct nla_policy compatible with
// the kernel's <net/netlink.h> layout: type (u8) at offset 0,
// validation_type (u8) at offset 1, len (u16) at offset 2, then a
// union member — we model just an opaque 8-byte filler since we
// only read `type`.  The structural-equivalence linker
// reconciles this with the kernel's full layout when both TUs are
// linked.
struct nlattr;
struct netlink_ext_ack;

struct nla_policy
{
  unsigned char type;
  unsigned char validation_type;
  unsigned short len;
  // Kernel struct continues with a union of pointers / s16 pairs /
  // function pointer; on x86_64 the union plus alignment makes the
  // total struct size 16 bytes.  Match that with an opaque tail so
  // policy[i] indexing computes the right offset.
  unsigned long __opaque_union_filler;
};

// NLA_* type enum values from <net/netlink.h>:
//   NLA_UNSPEC       = 0
//   NLA_U8           = 1     NLA_S8           = 12
//   NLA_U16          = 2     NLA_S16          = 13
//   NLA_U32          = 3     NLA_S32          = 14
//   NLA_U64          = 4     NLA_S64          = 15
//   NLA_STRING       = 5
//   NLA_FLAG         = 6
//   NLA_MSECS        = 7
//   NLA_NESTED       = 8     NLA_NESTED_ARRAY = 9
//   NLA_NUL_STRING   = 10    NLA_BINARY       = 11
//   NLA_BITFIELD32   = 16    NLA_REJECT       = 17
//
// We only translate the integer types; everything else returns 0
// (no minimum size) which means nla_get_uX on it will fire the
// contract precondition — that's the desired behaviour for code
// that reads a typed value out of an attribute the policy didn't
// validate as that type.
static unsigned int __nla_min_size_for_policy_type(unsigned int t)
{
  switch(t)
  {
  case 1u:  // NLA_U8
  case 12u: // NLA_S8
    return 1u;
  case 2u:  // NLA_U16
  case 13u: // NLA_S16
    return 2u;
  case 3u:  // NLA_U32
  case 14u: // NLA_S32
    return 4u;
  case 4u:  // NLA_U64
  case 15u: // NLA_S64
  case 7u:  // NLA_MSECS (treated as u64 by the kernel)
    return 8u;
  default:
    return 0u;
  }
}

static void __nla_parse_validate_tb(
  struct nlattr **tb,
  int maxtype,
  const struct nla_policy *policy)
{
  if(!tb)
    return;
// Manually unrolled — see comment above for rationale.  Each step
// reads policy[idx].type (when policy is provided) and derives the
// per-attribute minimum size; without a policy, falls back to the
// v2 over-approximation of 8 bytes so handlers that don't pass a
// policy still see SOME validation.
#define VALIDATE_TB_AT(idx)                                                    \
  do                                                                           \
  {                                                                            \
    if((idx) <= maxtype && tb[(idx)])                                          \
    {                                                                          \
      unsigned int __min_sz = 8u;                                              \
      if(policy)                                                               \
      {                                                                        \
        unsigned int __t = (unsigned int)policy[(idx)].type;                   \
        __min_sz = __nla_min_size_for_policy_type(__t);                        \
      }                                                                        \
      if(__min_sz > 0u)                                                        \
        nla_validate_min_size(tb[(idx)], __min_sz);                            \
    }                                                                          \
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
  (void)validate;
  (void)extack;
  __nla_parse_validate_tb(tb, maxtype, policy);
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
