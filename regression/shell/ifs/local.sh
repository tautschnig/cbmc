#!/bin/sh
# local IFS: the caller's IFS is restored when the function returns.
f() {
  local IFS=:
  set -- $1
  __CPROVER_assert "$#" = 2
}
g() {
  local IFS
  x="a b"
  set -- $x
}
f "a:b"
x="a b"
set -- $x
__CPROVER_assert "$#" = 2
g
set -- $x
__CPROVER_assert "$#" = 2
