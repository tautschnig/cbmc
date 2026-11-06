#!/bin/sh
# An unquoted hole in an argument may hold several words.
case "$1" in
  *[!a-z\ ]*) exit 1 ;;
esac
eval "set -- -v $1"
__CPROVER_assert "$1" = -v
