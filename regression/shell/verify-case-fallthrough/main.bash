#!/bin/bash
x=two
case "$x" in
  one) r=1 ;;
  two) r=2 ;&
  three) r="${r}3" ;;
  *) r=none ;;
esac
__CPROVER_assert "$r" = 23
