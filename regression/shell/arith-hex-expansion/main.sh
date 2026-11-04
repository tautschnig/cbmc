#!/bin/sh
# 0x$x: the value of x is the digits of a hexadecimal constant; pattern
# removal in arithmetic.
dev_num=fd:1a
__CPROVER_assert "$((0x${dev_num%:*}))" = 253
__CPROVER_assert "$(( 0x${dev_num#*:} + 1 ))" = 27
maj=$1
case "$maj" in
  1F)
    __CPROVER_assert "$(( 0x$maj ))" = 31
    ;;
esac
n=0012
__CPROVER_assert "$((${n##00} * 2))" = 24
