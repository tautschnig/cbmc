#!/bin/sh
# Options that are not known during conversion.
while getopts "ab:" opt; do
  case "$opt" in
    a | b | "?") ;;
    *) __CPROVER_assert 0 = 1 ;;
  esac
done
__CPROVER_assert "$opt" = "?"
shift $((OPTIND - 1))
