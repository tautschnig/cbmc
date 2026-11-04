#!/bin/sh
# "$@" with text around it: the first field starts with the text before,
# and the last one ends with the text after; without positional
# parameters, the text around is one field.
fields() {
  n=$1
  shift
  __CPROVER_assert "$#" = "$n"
  __CPROVER_assert "$1" = "$f1"
}
set -- 1
f1=x1y
fields 1 "x$@y"
set -- "" ""
f1=x
fields 2 "x$@y"
set -- p q r
f() {
  __CPROVER_assert "$#" = 3
  __CPROVER_assert "$1" = "xp"
  __CPROVER_assert "$2" = q
  __CPROVER_assert "$3" = "r b"
}
f x"$@"" b"
set --
f1=xy
fields 1 "x$@y"
f1=
fields 1 "$@"""
for i in "<$@>"; do __CPROVER_assert "$i" = "<>"; done
