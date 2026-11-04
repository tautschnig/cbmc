#!/bin/sh
x=global
f() {
  local x=inner y
  __CPROVER_assert "$x" = inner
  __CPROVER_assert "${y-unset}" = outer -o "${y-unset}" = unset
  g
  [ "$1" = early ] && return 3
  x=changed
}
g() { __CPROVER_assert "$x" = inner; }
y=outer
f
__CPROVER_assert "$x" = global
__CPROVER_assert "$y" = outer
f early
__CPROVER_assert "$?" = 3
__CPROVER_assert "$x" = global
h() {
  local n=0
  n=$((n + 1))
  [ "$1" -gt 0 ] && h $(( $1 - 1 ))
  __CPROVER_assert "$n" = 1
}
h 2
