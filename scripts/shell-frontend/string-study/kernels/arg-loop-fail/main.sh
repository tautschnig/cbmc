#!/bin/sh
name=
verbose=0
while [ $# -gt 0 ]; do
  case $1 in
    --name=*) name=${1#--name=} ;;
    -v) verbose=1 ;;
  esac
  shift
done
if [ "$verbose" = 1 ]; then __CPROVER_assert "$name" != x; fi
