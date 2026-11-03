#!/bin/sh
name=
while [ $# -gt 0 ]; do
  case $1 in
    --name=*) name=${1#--name=} ;;
  esac
  shift
done
__CPROVER_assert "$name" != x
