#!/bin/sh
true && a=1 || a=2
__CPROVER_assert "$a" = 1
false && b=1 || b=2
__CPROVER_assert "$b" = 2
! false
__CPROVER_assert "$?" -eq 0
[ 3 -gt 5 ]
__CPROVER_assert "$?" -eq 1
[ abc -gt 5 ] 2>/dev/null
__CPROVER_assert "$?" -eq 2
[ " 12 " -eq 12 ]
__CPROVER_assert "$?" -eq 0
[ -n "" ]
__CPROVER_assert "$?" -eq 1
[ ! -z "x" ]
__CPROVER_assert "$?" -eq 0
[ "(" x ")" ]
__CPROVER_assert "$?" -eq 0
if false; then :; fi
__CPROVER_assert "$?" -eq 0
c=$((5 / 2)) d=$((-7 % 3)) e=$((1 << 4)) f=$((-16 >> 2))
__CPROVER_assert "$c$d$e$f" = "2-116-4"
