#!/bin/sh
# set with an expansion: its fields replace the positional parameters,
# unless there are none.
x="a b c"
set $x
__CPROVER_assert "$#" = 3
__CPROVER_assert "$2" = b
e=
set $e > /dev/null
__CPROVER_assert "$#" = 3
