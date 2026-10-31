#!/bin/sh
s=
for w in a bb ccc dddd; do s="$s$w"; done
x=$1
t="$s$x"
__CPROVER_assert "${#t}" -ne 13
