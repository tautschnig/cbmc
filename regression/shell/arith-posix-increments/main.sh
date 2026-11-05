#!/bin/sh
# dash has no increment operators (++x is +(+x), x++ is a syntax error);
# bash run as sh and busybox increment.
x=1
y=$((++x))
case "$x$y" in 11 | 22) ;; *) __CPROVER_assert 0 = 1 ;; esac
z=$((x++))
__CPROVER_assert "$z" = "$y"
__CPROVER_assert "$x" = 3
