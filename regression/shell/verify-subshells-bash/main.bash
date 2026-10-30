#!/bin/bash
x=a
(x=b; __CPROVER_assert "$x" = b; exit 3)
__CPROVER_assert "$?" -eq 3
__CPROVER_assert "$x" = a
set -- one
(set -- b c; __CPROVER_assert "$#" -eq 2)
__CPROVER_assert "$#" -eq 1
y=$(x=c; exit 4)
__CPROVER_assert "$?" -eq 4
__CPROVER_assert "$x" = a
false
z=$(true) w=$?
__CPROVER_assert "$w" -eq 0
f() { (g() { :; }); }
f
true | (exit 5)
__CPROVER_assert "$?" -eq 5
(exit 6) | true
__CPROVER_assert "$?" -eq 0
v=1
{ v=2; } | true
__CPROVER_assert "$v" -eq 1
u=0
u=1 &
__CPROVER_assert "$u" -eq 0
r=$( __CPROVER_assert "$x" = a; echo hi )
