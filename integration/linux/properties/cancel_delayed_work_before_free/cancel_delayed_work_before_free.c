/// \file
/// cancel_delayed_work_before_free.c — reference impl.

#include "cancel_delayed_work_before_free.h"

#ifndef CANCEL_DWORK_TABLE_SIZE
#  define CANCEL_DWORK_TABLE_SIZE 16
#endif

struct cancel_dwork_entry
{
  struct delayed_work *key;
  int pending;
};

static struct cancel_dwork_entry table[CANCEL_DWORK_TABLE_SIZE];
static unsigned int table_len = 0;

static struct cancel_dwork_entry *find(struct delayed_work *d)
{
  for(unsigned int i = 0; i < table_len; i++)
    if(table[i].key == d)
      return &table[i];
  return (struct cancel_dwork_entry *)0;
}

static struct cancel_dwork_entry *find_or_add(struct delayed_work *d)
{
  struct cancel_dwork_entry *e = find(d);
  if(e)
    return e;
  if(table_len >= CANCEL_DWORK_TABLE_SIZE)
    return (struct cancel_dwork_entry *)0;
  struct cancel_dwork_entry *slot = &table[table_len++];
  slot->key = d;
  slot->pending = 0;
  return slot;
}

void cancel_dwork_set_pending(struct delayed_work *dwork)
{
  struct cancel_dwork_entry *e = find_or_add(dwork);
  if(e)
    e->pending = 1;
}

void cancel_dwork_clear_pending(struct delayed_work *dwork)
{
  struct cancel_dwork_entry *e = find(dwork);
  if(e)
    e->pending = 0;
}

int cancel_dwork_pending(struct delayed_work *dwork)
{
  if(!dwork)
    return 0;
  struct cancel_dwork_entry *e = find(dwork);
  if(!e)
    return 0;
  return e->pending;
}
