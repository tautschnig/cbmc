/// \file
/// alloc_tag.c — reference implementation of the alloc_tag
/// property module.  Per-pointer allocator tag, backed by a
/// fixed-size ghost table keyed on the pointer's address.

#include "alloc_tag.h"

#ifndef ALLOC_TAG_GHOST_TABLE_SIZE
#  define ALLOC_TAG_GHOST_TABLE_SIZE 16
#endif

struct alloc_tag_ghost_entry
{
  void *key;
  alloc_tag_t tag;
};

static struct alloc_tag_ghost_entry
  alloc_tag_ghost_table[ALLOC_TAG_GHOST_TABLE_SIZE];
static unsigned int alloc_tag_ghost_table_len = 0;

static struct alloc_tag_ghost_entry *alloc_tag_ghost_find(void *p)
{
  for(unsigned int i = 0; i < alloc_tag_ghost_table_len; i++)
  {
    if(alloc_tag_ghost_table[i].key == p)
      return &alloc_tag_ghost_table[i];
  }
  return (struct alloc_tag_ghost_entry *)0;
}

static struct alloc_tag_ghost_entry *alloc_tag_ghost_find_or_add(void *p)
{
  struct alloc_tag_ghost_entry *existing = alloc_tag_ghost_find(p);
  if(existing)
    return existing;
  if(alloc_tag_ghost_table_len >= ALLOC_TAG_GHOST_TABLE_SIZE)
    return (struct alloc_tag_ghost_entry *)0;
  struct alloc_tag_ghost_entry *slot =
    &alloc_tag_ghost_table[alloc_tag_ghost_table_len++];
  slot->key = p;
  slot->tag = ALLOC_TAG_UNKNOWN;
  return slot;
}

void alloc_tag_mark(void *p, alloc_tag_t tag)
{
  struct alloc_tag_ghost_entry *slot = alloc_tag_ghost_find_or_add(p);
  if(slot)
    slot->tag = tag;
}

void alloc_tag_clear(void *p)
{
  struct alloc_tag_ghost_entry *slot = alloc_tag_ghost_find(p);
  if(slot)
    slot->tag = ALLOC_TAG_UNKNOWN;
}

alloc_tag_t alloc_tag_of(void *p)
{
  struct alloc_tag_ghost_entry *slot = alloc_tag_ghost_find(p);
  if(!slot)
    return ALLOC_TAG_UNKNOWN;
  return slot->tag;
}

int alloc_tag_kfree_ok(void *p)
{
  if(!p)
    return 1;
  alloc_tag_t tag = alloc_tag_of(p);
  return tag == ALLOC_TAG_UNKNOWN || tag == ALLOC_TAG_KMALLOC ||
             tag == ALLOC_TAG_KVMALLOC || tag == ALLOC_TAG_KMEM_CACHE
           ? 1
           : 0;
}

int alloc_tag_vfree_ok(void *p)
{
  if(!p)
    return 1;
  alloc_tag_t tag = alloc_tag_of(p);
  return tag == ALLOC_TAG_UNKNOWN || tag == ALLOC_TAG_VMALLOC ||
             tag == ALLOC_TAG_KVMALLOC
           ? 1
           : 0;
}

int alloc_tag_free_ok_null_or(void *p, int ok_when_tracked)
{
  if(!p)
    return 1;
  return ok_when_tracked;
}
