#!/bin/sh
x=$1
case "$x" in
  *[\'\\]*) exit 1 ;;
esac
y="$x'\\''z"
eval "set -- 'pre$y'"
__CPROVER_assert "$1" = "pre${x}'z"
