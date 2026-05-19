/// \file
/// test_unit.c — unit tests for concurrent_pointer_publish.
/// Sequential / single-threaded sanity tests of the ghost
/// state and predicates.

#include "concurrent_pointer_publish.h"

#ifndef __CPROVER
#  include <assert.h>
#  define __CPROVER_assert(cond, msg) assert(cond)
#endif

int main(void)
{
  // test 1: fresh state -> nothing published / initialised.
  cpp_clear();
  __CPROVER_assert(cpp_is_published() == 0, "fresh: not published");
  __CPROVER_assert(cpp_is_initialised() == 0, "fresh: not initialised");
  __CPROVER_assert(
    cpp_safe_to_read() == 1, "fresh: safe (vacuously, !published)");

  // test 2: publish only (the bug shape) -> NOT safe.
  cpp_publish();
  __CPROVER_assert(cpp_is_published() == 1, "after publish: published");
  __CPROVER_assert(cpp_is_initialised() == 0, "after publish: not init");
  __CPROVER_assert(
    cpp_safe_to_read() == 0, "after publish (only): NOT safe — bug shape");

  // test 3: init follows -> safe.
  cpp_initialise();
  __CPROVER_assert(cpp_is_initialised() == 1, "after init: initialised");
  __CPROVER_assert(cpp_safe_to_read() == 1, "after init: safe");

  // test 4: clear -> back to fresh.
  cpp_clear();
  __CPROVER_assert(cpp_is_published() == 0, "cleared: not published");
  __CPROVER_assert(cpp_is_initialised() == 0, "cleared: not initialised");

  // test 5: init-then-publish (the safe order) -> safe at all
  //         intermediate steps.
  cpp_initialise();
  __CPROVER_assert(
    cpp_safe_to_read() == 1, "init only: safe (still !published)");
  cpp_publish();
  __CPROVER_assert(cpp_safe_to_read() == 1, "init then publish: safe");

  return 0;
}
