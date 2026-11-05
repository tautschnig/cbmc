#!/bin/bash
# (( expression )) and let: the status is 0 if the last value is not 0.
x=1
(( x += 2 ))
__CPROVER_assert "$x" = 3
if (( x > 5 )); then __CPROVER_assert 0 = 1; fi
(( 0 ))
__CPROVER_assert "$?" = 1
let "y = x * 2" z=5
__CPROVER_assert "$y$z" = 65
let "z - 5"
__CPROVER_assert "$?" = 1
n=0
while (( n < 3 )); do (( n++ )) || :; done
__CPROVER_assert "$n" = 3
