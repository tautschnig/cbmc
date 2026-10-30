#!/bin/bash
set -- a "b c" ""
__CPROVER_assert "$#" -eq 3
__CPROVER_assert "$2" = "b c"
__CPROVER_assert "$*" = "a b c "
n=0
for x; do n=$((n + 1)); done
__CPROVER_assert "$n" -eq 3
s=
for x in pre "$@" post; do s="$s[$x]"; done
__CPROVER_assert "$s" = "[pre][a][b c][][post]"
shift
__CPROVER_assert "$1" = "b c"
__CPROVER_assert "$#" -eq 2
shift 2
__CPROVER_assert "$#" -eq 0
__CPROVER_assert "${1-unset}" = unset
set --
__CPROVER_assert "$*" = ""
x=1 set -- q
__CPROVER_assert "$1" = q
__CPROVER_assert "${010-u}" = u
