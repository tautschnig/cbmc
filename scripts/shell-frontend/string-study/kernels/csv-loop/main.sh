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
  case $field in *,*) __CPROVER_assert 1 = 0 ;; esac
done
