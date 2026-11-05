#!/bin/sh
# An operand of test with pathname expansion: one pathname, or the word
# itself, is tested; with several pathnames test fails or reports an
# error.
dir=/etc
[ -n "$dir"/*.conf ]
__CPROVER_assert $? -le 2
if [ -n "$dir"/*.conf ]; then
  :
else
  __CPROVER_assert 0 = 1
fi
