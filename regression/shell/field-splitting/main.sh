#!/bin/sh
# Field splitting with the default IFS (XCU 2.6.5) of single unquoted
# expansions in lists of fields: for, function arguments, set --.
x="  a b	c
d  "
set -- $x
__CPROVER_assert "$#" = 4
__CPROVER_assert "$1" = a
__CPROVER_assert "$4" = d
y=
for i in $y; do __CPROVER_assert 0 = 1; done
f() { __CPROVER_assert "$#" = 2; __CPROVER_assert "$2" = "b"; }
z="a b"
f $z
