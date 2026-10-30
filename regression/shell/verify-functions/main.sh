#!/bin/sh
count=0
inc() { count=$((count + ${1:-1})); }
inc
inc 5
__CPROVER_assert "$count" -eq 6
args() { echo "$#"; __CPROVER_assert "$#" -eq 3; __CPROVER_assert "$2" = "b c"; }
set -- outer
args a "b c" ""
__CPROVER_assert "$1" = outer
st() { return "$1"; }
st 7
__CPROVER_assert "$?" -eq 7
fails() { false; }
fails
__CPROVER_assert "$?" -eq 1
fact() {
  if [ "$1" -le 1 ]; then r=1; return 0; fi
  fact $(( $1 - 1 ))
  r=$(( r * $1 ))
}
fact 5
__CPROVER_assert "$r" -eq 120
g() { echo g1; v=1; }
g
g() { v=2; }
g
__CPROVER_assert "$v" -eq 2
fwd() { args "$@"; }
fwd a "b c" ""
test() { t=mine; }
test 1 = 2
__CPROVER_assert "$t" = mine
quit() { exit 3; }
if [ "$count" -eq 6 ]; then quit; fi
__CPROVER_assert reached = no
