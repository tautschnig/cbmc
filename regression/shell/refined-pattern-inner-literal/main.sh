#!/bin/sh
g="$1/tmp.$2"
case "$g" in
  */tmp.*) ;;
  *) __CPROVER_assert 0 = 1 ;;
esac
