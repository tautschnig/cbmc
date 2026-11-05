#!/bin/bash
# Indexed arrays: assignment, appending, elements, "${a[@]}" and counts.
a=(x "y z")
a+=(w)
__CPROVER_assert "${#a[@]}" = 3
__CPROVER_assert "${a[1]}" = "y z"
__CPROVER_assert "$a" = x
i=1
__CPROVER_assert "${a[i+1]}" = w
set -- "${a[@]}"
__CPROVER_assert "$#" = 3
__CPROVER_assert "$2" = "y z"
n=0
for e in "${a[@]}"; do n=$((n + 1)); done
__CPROVER_assert "$n" = 3
a[3]=v
__CPROVER_assert "${a[*]}" = "x y z w v"
a=s
__CPROVER_assert "${a[0]}${#a[@]}" = s4
b=()
__CPROVER_assert "${#b[@]}" = 0
b+=(1)
b[0]+=2
__CPROVER_assert "${b[0]}" = 12
s=ab
s+=cd
__CPROVER_assert "$s" = abcd
unset a
__CPROVER_assert "${#a[@]}" = 0
