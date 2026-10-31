#!/bin/sh
p=$1
case $p in
  */*)
    base=${p##*/}
    __CPROVER_assert "$base" != lib
    ;;
esac
