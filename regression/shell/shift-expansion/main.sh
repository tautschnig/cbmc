#!/bin/sh
# shift with an operand that is an expansion, and shifting by more than
# $#, which makes dash exit and busybox and bash as sh fail with 1.
set -- a b c
n=2
shift $n
__CPROVER_assert "$1" = c
__CPROVER_assert "$#" = 1
set -- a b c
shift $(($# - 1))
__CPROVER_assert "$1" = c
shift 2
__CPROVER_assert "$?" = 1
__CPROVER_assert "$1" = c
