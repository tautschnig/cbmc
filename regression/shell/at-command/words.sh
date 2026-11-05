#!/bin/sh
# "$@" and $@ with further words: the command is the first field of all
# the words.
f() { y=$1; z=$#; }
set --
"$@" f a b
__CPROVER_assert "$y" = a
__CPROVER_assert "$z" = 2
set -- "f x" b
$@ c
__CPROVER_assert "$y" = x
__CPROVER_assert "$z" = 3
__CPROVER_assert "$#" = 2
set -- f b
"$@" c
__CPROVER_assert "$y" = b
__CPROVER_assert "$z" = 2
