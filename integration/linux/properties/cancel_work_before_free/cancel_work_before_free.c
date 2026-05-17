/// \file
/// cancel_work_before_free.c — reference impl.

#include "cancel_work_before_free.h"

#ifndef CANCEL_WORK_TABLE_SIZE
#  define CANCEL_WORK_TABLE_SIZE 16
#endif

struct cancel_work_entry
{
  struct work_struct *key;
  int pending;
};

static struct cancel_work_entry table[CANCEL_WORK_TABLE_SIZE];
static unsigned int table_len = 0;

static struct cancel_work_entry *find(struct work_struct *w)
{
  for(unsigned int i = 0; i < table_len; i++)
    if(table[i].key == w)
      return &table[i];
  return (struct cancel_work_entry *)0;
}

static struct cancel_work_entry *find_or_add(struct work_struct *w)
{
  struct cancel_work_entry *e = find(w);
  if(e)
    return e;
  if(table_len >= CANCEL_WORK_TABLE_SIZE)
    return (struct cancel_work_entry *)0;
  struct cancel_work_entry *slot = &table[table_len++];
  slot->key = w;
  slot->pending = 0;
  return slot;
}

void cancel_work_set_pending(struct work_struct *work)
{
  struct cancel_work_entry *e = find_or_add(work);
  if(e)
    e->pending = 1;
}

void cancel_work_clear_pending(struct work_struct *work)
{
  struct cancel_work_entry *e = find(work);
  if(e)
    e->pending = 0;
}

int cancel_work_pending(struct work_struct *work)
{
  if(!work)
    return 0;
  struct cancel_work_entry *e = find(work);
  if(!e)
    return 0;
  return e->pending;
}
