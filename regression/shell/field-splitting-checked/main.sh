#!/bin/sh
x=abc
__CPROVER_assert $x = abc
case "$1" in
  *[[:space:]]* | *[*?[]* | "") ;;
  *) __CPROVER_assert $1 = "$1" ;;
esac
y=$1
[ $y = x ]
