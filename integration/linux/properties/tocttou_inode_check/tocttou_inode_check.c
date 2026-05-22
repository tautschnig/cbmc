/// \file
/// tocttou_inode_check.c — reference implementation.

#include "tocttou_inode_check.h"

unsigned int __tic_state = 0;

void tic_init(unsigned int initial)
{
  __tic_state = initial;
}

unsigned int tic_check(void)
{
  return __tic_state;
}

void tic_act_assuming(unsigned int checked)
{
  // Reference impl: no-op; the contract enforces the
  // precondition.  The harness can call this between a
  // tic_check and a (possibly racy) action to assert the
  // captured value still matches.
  (void)checked;
}

void tic_change(unsigned int new_state)
{
  __tic_state = new_state;
}
