#!/bin/sh
# Saving and restoring IFS in a function, where it is not known.
f() {
  old="$IFS"
  IFS=:
  for p in $1; do
    case "$p" in *:*) __CPROVER_assert 0 = 1 ;; esac
  done
  IFS="$old"
  set -- $2
  __CPROVER_assert "$#" = 2
}
f "a:b" "c d"
f "" "e f"
