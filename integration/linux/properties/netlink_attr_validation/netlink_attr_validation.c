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
