#!/bin/sh
if [ "$1" = stop ]; then
  exit 3
fi
__CPROVER_assert "$1" != stop
exit
__CPROVER_assert 1 = 2
