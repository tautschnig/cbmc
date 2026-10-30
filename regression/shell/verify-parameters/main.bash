#!/bin/bash
unset u
e=
__CPROVER_assert "${u-d1}" = d1
__CPROVER_assert "${e-d2}" = ""
__CPROVER_assert "${e:-d3}" = d3
__CPROVER_assert "${u+alt}" = ""
__CPROVER_assert "${e+alt}" = alt
__CPROVER_assert "${e:+alt}" = ""
unset n
: "${n:=new}"
__CPROVER_assert "$n" = new
v=hello
__CPROVER_assert "${#v}" -eq 5
__CPROVER_assert "${#u}" -eq 0
x=7 y=08
__CPROVER_assert "$((x * 6))" = 42
__CPROVER_assert "$((x > 3 ? 1 : 0))" -eq 1
__CPROVER_assert "$((0x10 + 010))" -eq 24
z=$((x += 3, x * 2))
__CPROVER_assert "$x" -eq 10
