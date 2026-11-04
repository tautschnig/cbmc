#!/bin/sh
# break and continue skip no declarations of temporaries.
n=0
for a in x y z; do
  case "$1" in
    "$a") break ;;
    "$a$a") continue ;;
  esac
  r="${1#?}"
  n=$((n + 1))
done
__CPROVER_assert "$n" -le 3
