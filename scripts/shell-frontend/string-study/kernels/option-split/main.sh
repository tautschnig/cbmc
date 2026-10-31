#!/bin/sh
a=$1
case $a in
  --*=*)
    name=${a%%=*}
    value=${a#*=}
    __CPROVER_assert "$name=$value" = "$a"
    ;;
esac
