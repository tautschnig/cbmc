#!/bin/sh
h="$1ab"
case "$h" in
  *ab*) ;;
  *) __CPROVER_assert 0 = 2 ;;
esac
