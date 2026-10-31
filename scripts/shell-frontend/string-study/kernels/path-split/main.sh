#!/bin/sh
p=$1
case $p in
  */*)
    dir=${p%/*}
    base=${p##*/}
    __CPROVER_assert "$dir/$base" = "$p"
    ;;
esac
