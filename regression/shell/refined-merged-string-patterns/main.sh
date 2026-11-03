#!/bin/sh
n=0
for s in "$@"; do
  case $s in
    *.gz | *.xz) n=$((n + 1)) ;;
  esac
done
__CPROVER_assert "$n" -le "$#"
