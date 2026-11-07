#!/bin/sh
g="$1/tmp.$2"
case "$g" in
  */tmp.*) ;;
  *) __CPROVER_assert 0 = 1 ;;
esac
h="$1ab$2"
case "$h" in
  *ab*) ;;
  *) __CPROVER_assert 0 = 2 ;;
esac
