#!/bin/sh
greet() { r="hi $1"; }
cmd=greet
$cmd you
__CPROVER_assert "$r" = "hi you"
cmd=false
if "$cmd"; then s=0; else s=1; fi
__CPROVER_assert "$s" = 1
lib=/usr/lib
$lib/tool -l "$@"
r=unchanged
case "$1" in
  greet) "$1" x; __CPROVER_assert "$r" = "hi x" ;;
  /*) "$1" x; __CPROVER_assert "$r" = unchanged ;;
esac
case "$2" in
  __CPROVER_*) "$2" 0 = 1 ;;
esac
