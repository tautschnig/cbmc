#!/bin/sh
# Field splitting of values not known during conversion at an IFS without
# white space: each byte of IFS ends a field.
IFS=:
x="$1"
case "$x" in
  a:b:)
    set -- $x
    __CPROVER_assert "$#" = 2
    __CPROVER_assert "$2" = b
    ;;
  ab)
    set -- $x
    __CPROVER_assert "$#" = 1
    ;;
esac
