#!/bin/sh
__CPROVER_assume "$#" -eq 1
__CPROVER_assume "$1" != yes
if [ "$1" = yes ]; then
  r=unreachable
else
  r=reached
fi
__CPROVER_assert "$r" = reached
__CPROVER_assert "$2" = ""
