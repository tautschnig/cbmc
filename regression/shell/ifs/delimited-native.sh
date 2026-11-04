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
  :a::)
    set -- $x
    __CPROVER_assert "$#" = 3
    __CPROVER_assert "$1$3" = ""
    __CPROVER_assert "$2" = a
    ;;
  ab)
    set -- $x
    __CPROVER_assert "$#" = 1
    ;;
  "")
    set -- $x
    __CPROVER_assert "$#" = 0
    ;;
  *[*?[]*) ;;
  ?|??|???)
    n=0
    for f in $x; do
      case "$f" in *:*) __CPROVER_assert 0 = 1 ;; esac
      n=$((n + 1))
    done
    ;;
esac
