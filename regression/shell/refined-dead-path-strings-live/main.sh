#!/bin/sh
name=default
while [ $# -gt 0 ]; do
  case $1 in
    --name=?*)
      name=${1#--name=}
      __CPROVER_assert "--name=$name" = "$1"
      ;;
  esac
  shift
done
__CPROVER_assert "$name" != ""
