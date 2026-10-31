#!/bin/sh
s=
for w in a bb ccc dddd; do s="$s$w"; done
__CPROVER_assert "${#s}" -eq 10
x=$1
t="$s$x"
__CPROVER_assert "${#t}" -ge 10
