/// \file
/// rcu_critical_section.c — reference implementation of the
/// rcu_critical_section property module.

#include "rcu_critical_section.h"

unsigned int __rcu_csection_depth = 0;

void rcu_csection_enter(void)
{
  __rcu_csection_depth++;
}

void rcu_csection_leave(void)
{
  if(__rcu_csection_depth > 0)
    __rcu_csection_depth--;
}

unsigned int rcu_csection_depth(void)
{
  return __rcu_csection_depth;
}

int rcu_in_csection(void)
{
  return __rcu_csection_depth > 0 ? 1 : 0;
}

int rcu_outside_csection(void)
{
  return __rcu_csection_depth == 0 ? 1 : 0;
}
