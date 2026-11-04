#!/bin/sh
# The values of unquoted expansions in patterns are patterns.
pattern='*.ko'
case /lib/foo.ko in
  */$pattern) ;;
  *) __CPROVER_assert 0 = 1 ;;
esac
size=12k
unit=${size#${size%?}}
__CPROVER_assert "$unit" = k
x=$1
case "$x" in
  *[*?[\\]*) ;;
  *)
    case "a${x}b" in
      a${x}b) ;;
      *) __CPROVER_assert 0 = 1 ;;
    esac
    ;;
esac
