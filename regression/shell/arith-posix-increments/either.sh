#!/bin/sh
x=1
y=$((++x))
z=$((--x))
__CPROVER_assert "$y$z" = 11
