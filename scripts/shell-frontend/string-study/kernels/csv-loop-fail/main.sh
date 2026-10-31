#!/bin/sh
rest=$1
n=0
while [ -n "$rest" ] && [ "$n" -lt 3 ]; do
  field=${rest%%,*}
  case $rest in
    *,*) rest=${rest#*,} ;;
    *) rest= ;;
  esac
  n=$((n + 1))
  __CPROVER_assert "$n" -lt 3
done
