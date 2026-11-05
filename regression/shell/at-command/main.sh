#!/bin/sh
# "$@" as a command: $1 runs with the other parameters as arguments.
run() {
  "$@"
}
check() {
  __CPROVER_assert "$#" = 2
  __CPROVER_assert "$1" = a
  ran=yes
}
run check a b
__CPROVER_assert "$ran" = yes
run
__CPROVER_assert "$?" = 0
run false
__CPROVER_assert "$?" = 1
set -- check x y
f() { __CPROVER_assert "$1" = x; }
run f x
