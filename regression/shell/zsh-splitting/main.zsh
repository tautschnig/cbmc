#!/bin/zsh
# zsh does not split parameter expansions, and does not apply pathname
# expansion to their results.
x="a b"
set -- $x
__CPROVER_assert "$#" = 1
y="$1"
set -- $y
__CPROVER_assert "$#" = 1
__CPROVER_assert "$1" = "$y"
e=
set -- $e
__CPROVER_assert "$#" = 0
z="f*"
set -- $z
__CPROVER_assert "$1" = "f*"
