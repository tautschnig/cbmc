#!/bin/sh
a=$1
case $a in
  --*=*)
    value=${a#*=}
    __CPROVER_assert "$value" != on
    ;;
esac
