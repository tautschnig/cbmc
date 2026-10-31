#!/bin/sh
x=$1
case $x in
  --*) y=${x#--}; __CPROVER_assert "${#y}" -eq $(( ${#x} - 1 )) ;;
esac
