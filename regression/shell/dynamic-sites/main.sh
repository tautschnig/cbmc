#!/bin/sh
# A dynamic command name is a dynamic site; with a resolution, it is one of
# its candidates, which is checked.
greet() { r=greet; }
r=none
case "$1" in
  a) cmd=greet ;;
  *) cmd=true ;;
esac
$cmd
__CPROVER_assert "$r" = greet -o "$r" = none
